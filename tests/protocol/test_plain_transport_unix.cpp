#include <gtest/gtest.h>

#include "logos_protocol.h"
#include "logos_transport_config.h"
#include "logos_transport_config_json.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>

#include <unistd.h> // getpid

// -----------------------------------------------------------------------------
// The plain LOCAL (Unix-domain socket) transport, end-to-end and fully Qt-free.
//
// A provider registered via lp_provider_* binds a Unix socket; a consumer built
// via lp_client_* dials the same socket path. Both run on Boost.Asio threads —
// no QCoreApplication, no Qt event loop. This proves the new PlainLocal
// transport is a real peer of TCP/TCP+SSL, and that the existing QtRO
// LocalSocket transport (the default) is untouched by it.
// -----------------------------------------------------------------------------

namespace {

using namespace std::chrono_literals;

char* echoDispatch(const char* method, const char* args_json, void*)
{
    if (std::string(method) != "echo") return nullptr;
    nlohmann::json a = nlohmann::json::parse(args_json, nullptr, false);
    nlohmann::json r;
    r["echo"] = (a.is_array() && !a.empty()) ? a[0] : nlohmann::json();
    return strdup(r.dump().c_str());
}
char* echoGetMethods(void*)
{
    nlohmann::json arr = nlohmann::json::array();
    arr.push_back({{"name", "echo"}, {"type", "method"}});
    arr.push_back({{"name", "tick"}, {"type", "event"}});
    return strdup(arr.dump().c_str());
}
int acceptToken(const char*, const char*, void*) { return LP_OK; }

// Pick a socket path that stays under the OS sun_path cap (~104 bytes on macOS).
// Prefer $TMPDIR, but fall back to a bare relative name (resolved against the
// CWD, so only the short string counts) if the temp dir is long — nix build
// dirs can be deep.
std::string sockPath(const char* tag)
{
    namespace fs = std::filesystem;
    const std::string name =
        std::string("lpu_") + tag + "_" + std::to_string(::getpid()) + ".sock";
    std::error_code ec;
    fs::path dir = fs::temp_directory_path(ec);
    if (!ec) {
        fs::path full = dir / name;
        if (full.string().size() < 100) return full.string();
    }
    return name; // relative — kernel stores only this short string
}

std::string tobj(const std::string& path)
{
    return std::string("{\"protocol\":\"plain_local\",\"socket_path\":\"")
        + path + "\",\"codec\":\"json\"}";
}
std::string tset(const std::string& path) { return "[" + tobj(path) + "]"; }

} // namespace

TEST(PlainLocalTransport, ProviderAndConsumerOverUnixSocketNoQtLoop)
{
    const std::string path = sockPath("e2e");
    std::error_code ec;
    std::filesystem::remove(path, ec); // clean any stale file up front

    lp_provider* prov = lp_provider_create("echo_unix", tset(path).c_str());
    ASSERT_NE(prov, nullptr);
    ASSERT_EQ(lp_provider_register(prov, echoDispatch, echoGetMethods, acceptToken, nullptr),
              LP_OK);
    ASSERT_EQ(lp_provider_save_token(prov, "origin_unix", "tok-u"), LP_OK);
    // The socket file must now exist on disk (proves we bound a real Unix socket).
    EXPECT_TRUE(std::filesystem::exists(path));

    // Pre-seed the target token so no capability handshake is needed.
    ASSERT_EQ(lp_token_save("echo_unix", "tok-u"), LP_OK);

    lp_client* client = lp_client_create("echo_unix", "origin_unix",
                                         tobj(path).c_str(), tobj(path).c_str());
    ASSERT_NE(client, nullptr);

    // Subscribe first; the sync call below (same connection, FIFO) guarantees
    // the provider registered the sink before we emit.
    struct EvBox {
        std::mutex mu; std::condition_variable cv; bool got = false;
        std::string name, data;
    } ev;
    auto evCb = [](const char* name, const char* data_json, void* ud) {
        auto* e = static_cast<EvBox*>(ud);
        std::lock_guard<std::mutex> g(e->mu);
        e->name = name; e->data = data_json ? data_json : ""; e->got = true;
        e->cv.notify_all();
    };
    lp_subscription* sub = lp_subscribe(client, "tick", evCb, &ev);
    ASSERT_NE(sub, nullptr);

    // (1) Sync invoke over the Unix socket.
    {
        char* result = nullptr; char* error = nullptr;
        ASSERT_EQ(lp_invoke(client, "echo", "[\"unix\"]", 3000, &result, &error), LP_OK)
            << (error ? error : "");
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(nlohmann::json::parse(result).value("echo", std::string()), "unix");
        lp_string_free(result);
    }

    // (2) Event delivery.
    ASSERT_EQ(lp_provider_emit_event(prov, "tick", "[99]"), LP_OK);
    {
        std::unique_lock<std::mutex> lk(ev.mu);
        ASSERT_TRUE(ev.cv.wait_for(lk, 3s, [&] { return ev.got; }))
            << "event not delivered over unix socket";
    }
    EXPECT_EQ(ev.name, "tick");
    EXPECT_EQ(nlohmann::json::parse(ev.data).at(0).get<int>(), 99);

    // (3) Async invoke — callback on the Asio I/O thread, no Qt loop.
    struct AsyncBox {
        std::mutex mu; std::condition_variable cv; bool done = false;
        int ok = -1; std::string json;
    } box;
    auto asyncCb = [](int ok, const char* json, void* ud) {
        auto* b = static_cast<AsyncBox*>(ud);
        std::lock_guard<std::mutex> g(b->mu);
        b->ok = ok; b->json = json ? json : ""; b->done = true;
        b->cv.notify_all();
    };
    ASSERT_EQ(lp_invoke_async(client, "echo", "[\"a-unix\"]", 3000, asyncCb, &box), LP_OK);
    {
        std::unique_lock<std::mutex> lk(box.mu);
        ASSERT_TRUE(box.cv.wait_for(lk, 3s, [&] { return box.done; }))
            << "async callback never fired";
    }
    EXPECT_EQ(box.ok, 1);
    EXPECT_EQ(nlohmann::json::parse(box.json).value("echo", std::string()), "a-unix");

    // (4) Unauthorized call is rejected.
    {
        ASSERT_EQ(lp_token_save("echo_unix", "nope"), LP_OK);
        char* result = nullptr; char* error = nullptr;
        const int rc = lp_invoke(client, "echo", "[\"x\"]", 3000, &result, &error);
        EXPECT_EQ(rc, LP_ERR_UNAVAILABLE);
        EXPECT_EQ(result, nullptr);
        ASSERT_NE(error, nullptr);
        EXPECT_EQ(nlohmann::json::parse(error, nullptr, false).value("code", std::string{}),
                  "UNAUTHORIZED");
        lp_string_free(error);
    }

    lp_unsubscribe(sub);
    lp_client_destroy(client);
    lp_provider_destroy(prov);

    // The provider's stop() removes the socket file it created.
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(PlainLocalTransport, DialingAMissingSocketYieldsCanonicalError)
{
    // No provider is listening on this path. The plain_local config still parses
    // and routes to the Qt-free plain consumer; the dial fails, and a call
    // surfaces the canonical object_unavailable error (same contract as TCP).
    const std::string path = sockPath("dead");
    std::error_code ec;
    std::filesystem::remove(path, ec);

    ASSERT_EQ(lp_token_save("nobody_unix", "t"), LP_OK); // skip the handshake
    lp_client* client = lp_client_create("nobody_unix", "origin",
                                         tobj(path).c_str(), tobj(path).c_str());
    ASSERT_NE(client, nullptr);

    char* result = nullptr; char* error = nullptr;
    const int rc = lp_invoke(client, "anyMethod", "[]", 1000, &result, &error);
    EXPECT_EQ(rc, LP_ERR_UNAVAILABLE);
    EXPECT_EQ(result, nullptr);
    ASSERT_NE(error, nullptr);
    nlohmann::json e = nlohmann::json::parse(error, nullptr, false);
    EXPECT_EQ(e.value("code", std::string{}), "object_unavailable");
    EXPECT_EQ(e.value("origin", std::string{}), "nobody_unix");
    lp_string_free(error);

    lp_client_destroy(client);
}

TEST(PlainLocalTransport, ConfigWithStrayPortKeyIsNotDropped)
{
    // A plain_local entry that carries an (irrelevant, out-of-range) "port" key
    // must still parse — the port range check only applies to the TCP
    // transports, so it must not silently drop the whole entry.
    const std::string j =
        "[{\"protocol\":\"plain_local\",\"socket_path\":\"/tmp/x.sock\","
        "\"codec\":\"json\",\"port\":-1}]";
    LogosTransportSet set = logos::transportSetFromJsonString(j);
    ASSERT_EQ(set.size(), 1u);
    EXPECT_EQ(set[0].protocol, LogosProtocol::PlainLocal);
    EXPECT_EQ(set[0].socketPath, "/tmp/x.sock");
}
