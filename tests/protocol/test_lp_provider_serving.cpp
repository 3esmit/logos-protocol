#include <gtest/gtest.h>

#include "logos_protocol.h"

// Internal plain-transport headers — used to drive a raw, fully Qt-free client
// (no lp_client / LogosAPIClient / QObject) straight at the provider. This is
// what proves the point: a provider registered via lp_provider_* serves calls
// and events over plain TCP with NO QCoreApplication and NO Qt event loop
// anywhere in the test. (The old in-process lp_client<->provider TCP test
// dead-locked precisely because both sides needed the Qt loop; the Qt-free
// provider dispatches on the Boost.Asio I/O thread instead.)
#include "implementations/plain/io_context_pool.h"
#include "implementations/plain/json_codec.h"
#include "implementations/plain/json_mapping.h"
#include "implementations/plain/rpc_connection.h"
#include "implementations/plain/rpc_message.h"

#include <nlohmann/json.hpp>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <memory>
#include <string>

namespace {

using namespace std::chrono_literals;

// A trivial C provider: echo(x) -> {"echo": x}; declares one event "tick".
char* echoDispatch(const char* method, const char* args_json, void*)
{
    if (std::string(method) != "echo") return nullptr;
    nlohmann::json a = nlohmann::json::parse(args_json, nullptr, /*allow_exceptions=*/false);
    nlohmann::json r;
    r["echo"] = (a.is_array() && !a.empty()) ? a[0] : nlohmann::json();
    return strdup(r.dump().c_str()); // freed by the library via lp_string_free
}

char* echoGetMethods(void*)
{
    nlohmann::json arr = nlohmann::json::array();
    arr.push_back({{"name", "echo"}, {"type", "method"}});
    arr.push_back({{"name", "tick"}, {"type", "event"}});
    return strdup(arr.dump().c_str());
}

int acceptToken(const char*, const char*, void*) { return LP_OK; }

// Connect a fresh raw plain (TCP/JSON) RpcConnection to 127.0.0.1:port.
std::shared_ptr<logos::plain::RpcConnectionBase> connectPlain(uint16_t port)
{
    using tcp = boost::asio::ip::tcp;
    auto& ioc = logos::plain::IoContextPool::shared().ioContext();
    tcp::resolver resolver(ioc);
    auto endpoints = resolver.resolve("127.0.0.1", std::to_string(port));
    tcp::socket socket(ioc);
    boost::asio::connect(socket, endpoints);
    auto codec = std::make_shared<logos::plain::JsonCodec>();
    auto conn = std::make_shared<logos::plain::RpcConnection<tcp::socket>>(
        std::move(socket), codec, /*handler=*/nullptr);
    conn->start();
    return conn;
}

} // namespace

TEST(LpProviderServing, ServesCallsAndEventsOverPlainTcpWithoutQtLoop)
{
    constexpr uint16_t kPort = 47921;
    const std::string tset =
        R"([{"protocol":"tcp","host":"127.0.0.1","port":47921,"codec":"json"}])";

    lp_provider* prov = lp_provider_create("serve_module", tset.c_str());
    ASSERT_NE(prov, nullptr);
    ASSERT_EQ(lp_provider_register(prov, echoDispatch, echoGetMethods, acceptToken, nullptr),
              LP_OK);
    // Seed the token the caller will present (provider-side issuance).
    ASSERT_EQ(lp_provider_save_token(prov, "origin", "tok-1"), LP_OK);

    auto conn = connectPlain(kPort);

    // Subscribe FIRST. Messages on one connection are processed in order, so the
    // round-trip call below guarantees the provider registered this sink before
    // we emit.
    std::promise<logos::plain::EventMessage> evP;
    auto evF = evP.get_future();
    std::atomic<bool> evSet{false};
    {
        logos::plain::SubscribeMessage sub;
        sub.object = "serve_module";
        sub.eventName = "tick";
        conn->sendSubscribe(std::move(sub), [&](logos::plain::EventMessage ev) {
            if (!evSet.exchange(true)) evP.set_value(std::move(ev));
        });
    }

    // (1) Authorized call is dispatched to the C provider on the Asio thread.
    {
        logos::plain::CallMessage c;
        c.id = conn->nextId();
        c.authToken = "tok-1";
        c.object = "serve_module";
        c.method = "echo";
        c.args = logos::plain::argsFromJson(nlohmann::json::parse(R"(["hi"])"));
        auto f = conn->sendCall(std::move(c));
        ASSERT_EQ(f.wait_for(3s), std::future_status::ready) << "call timed out";
        auto res = f.get();
        ASSERT_TRUE(res.ok) << res.err;
        nlohmann::json v = logos::plain::valueToJson(res.value);
        EXPECT_EQ(v.value("echo", std::string()), "hi");
    }

    // (2) Event: the provider emits, the subscriber receives it over the wire.
    ASSERT_EQ(lp_provider_emit_event(prov, "tick", "[42]"), LP_OK);
    ASSERT_EQ(evF.wait_for(3s), std::future_status::ready) << "event not delivered";
    auto ev = evF.get();
    ASSERT_EQ(ev.data.size(), 1u);
    EXPECT_EQ(logos::plain::valueToJson(ev.data[0]).get<int>(), 42);

    // (3) Unauthorized call is rejected (unknown token).
    {
        logos::plain::CallMessage c;
        c.id = conn->nextId();
        c.authToken = "wrong-token";
        c.object = "serve_module";
        c.method = "echo";
        c.args = logos::plain::argsFromJson(nlohmann::json::parse(R"(["hi"])"));
        auto f = conn->sendCall(std::move(c));
        ASSERT_EQ(f.wait_for(3s), std::future_status::ready);
        auto res = f.get();
        EXPECT_FALSE(res.ok);
        EXPECT_EQ(res.errCode, "UNAUTHORIZED");
    }

    conn->stop();
    lp_provider_destroy(prov);
}

TEST(LpProviderServing, ArgChecks)
{
    auto dispatch = [](const char*, const char*, void*) -> char* { return nullptr; };
    EXPECT_EQ(lp_provider_create(nullptr, nullptr), nullptr);
    EXPECT_EQ(lp_provider_register(nullptr, dispatch, nullptr, nullptr, nullptr),
              LP_ERR_INVALID_ARG);

    lp_provider* prov = lp_provider_create("m", "[]");
    ASSERT_NE(prov, nullptr);
    // Not registered yet → serving entry points report INTERNAL, not a crash.
    EXPECT_EQ(lp_provider_emit_event(prov, "e", "[]"), LP_ERR_INTERNAL);
    EXPECT_EQ(lp_provider_save_token(prov, "m", "t"), LP_ERR_INTERNAL);
    EXPECT_EQ(lp_provider_emit_event(prov, nullptr, "[]"), LP_ERR_INVALID_ARG);
    lp_provider_destroy(prov);
    lp_provider_destroy(nullptr); // no-op
}
