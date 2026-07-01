#include <gtest/gtest.h>

#include "logos_protocol.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>

// -----------------------------------------------------------------------------
// The Qt-free plain CONSUMER, end-to-end through the public lp_client_* C ABI.
//
// A provider registered via lp_provider_* (Phase-1, serving on Boost.Asio
// threads) is consumed via lp_client_* over plain TCP. When the transport is
// plain, lp_client_create builds an LpPlainClient — no LogosAPIClient, no
// QObject, no QCoreApplication, no Qt event loop anywhere in the process. Sync
// calls block on std::future, async callbacks fire on the Asio I/O thread, and
// events are std callbacks — none of it needs a running qApp (there is none).
// -----------------------------------------------------------------------------

namespace {

using namespace std::chrono_literals;

// Echo provider: echo(x) -> {"echo": x}; declares one event "tick".
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

// capability_module stub: requestModule(origin, target) -> "cap-minted-token".
char* capDispatch(const char* method, const char*, void*)
{
    if (std::string(method) != "requestModule") return nullptr;
    return strdup(nlohmann::json("cap-minted-token").dump().c_str());
}
char* capGetMethods(void*)
{
    nlohmann::json arr = nlohmann::json::array();
    arr.push_back({{"name", "requestModule"}, {"type", "method"}});
    return strdup(arr.dump().c_str());
}

int acceptToken(const char*, const char*, void*) { return LP_OK; }

// Single-object transport JSON (lp_client_create form).
std::string tobj(uint16_t port)
{
    return std::string("{\"protocol\":\"tcp\",\"host\":\"127.0.0.1\",\"port\":")
        + std::to_string(port) + ",\"codec\":\"json\"}";
}
// Transport-set JSON (lp_provider_create form — an array).
std::string tset(uint16_t port) { return "[" + tobj(port) + "]"; }

} // namespace

TEST(LpPlainClientConsumer, SyncAsyncEventsAndMethodsNoQtLoop)
{
    constexpr uint16_t kPort = 47931;

    lp_provider* prov = lp_provider_create("echo_mod", tset(kPort).c_str());
    ASSERT_NE(prov, nullptr);
    ASSERT_EQ(lp_provider_register(prov, echoDispatch, echoGetMethods, acceptToken, nullptr),
              LP_OK);
    ASSERT_EQ(lp_provider_save_token(prov, "origin_mod", "tok-1"), LP_OK);

    // Pre-seed the target token so no capability handshake is needed here.
    ASSERT_EQ(lp_token_save("echo_mod", "tok-1"), LP_OK);

    // capability transport points at the same port (unused — token pre-seeded).
    lp_client* client = lp_client_create("echo_mod", "origin_mod",
                                         tobj(kPort).c_str(), tobj(kPort).c_str());
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

    // (1) Synchronous invoke.
    {
        char* result = nullptr; char* error = nullptr;
        ASSERT_EQ(lp_invoke(client, "echo", "[\"hi\"]", 3000, &result, &error), LP_OK)
            << (error ? error : "");
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(nlohmann::json::parse(result).value("echo", std::string()), "hi");
        lp_string_free(result);
    }

    // (2) Provider emits — the subscriber receives it over the wire.
    ASSERT_EQ(lp_provider_emit_event(prov, "tick", "[7]"), LP_OK);
    {
        std::unique_lock<std::mutex> lk(ev.mu);
        ASSERT_TRUE(ev.cv.wait_for(lk, 3s, [&] { return ev.got; }))
            << "event not delivered";
    }
    EXPECT_EQ(ev.name, "tick");
    EXPECT_EQ(nlohmann::json::parse(ev.data).at(0).get<int>(), 7);

    // (3) Asynchronous invoke — the callback fires on the Asio I/O thread with
    // NO Qt event loop to pump it (the old plain path dropped it without qApp).
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
    ASSERT_EQ(lp_invoke_async(client, "echo", "[\"async\"]", 3000, asyncCb, &box), LP_OK);
    {
        std::unique_lock<std::mutex> lk(box.mu);
        ASSERT_TRUE(box.cv.wait_for(lk, 3s, [&] { return box.done; }))
            << "async callback never fired";
    }
    EXPECT_EQ(box.ok, 1);
    EXPECT_EQ(nlohmann::json::parse(box.json).value("echo", std::string()), "async");

    // (4) Introspection.
    {
        char* methods = lp_get_methods(client);
        ASSERT_NE(methods, nullptr);
        nlohmann::json m = nlohmann::json::parse(methods);
        ASSERT_TRUE(m.is_array());
        bool hasEcho = false;
        for (const auto& e : m)
            if (e.value("name", std::string()) == "echo") hasEcho = true;
        EXPECT_TRUE(hasEcho);
        lp_string_free(methods);
    }

    // (5) Unauthorized call (unknown token) is rejected with a canonical error.
    {
        ASSERT_EQ(lp_token_save("echo_mod", "wrong-token"), LP_OK); // clobber cache
        char* result = nullptr; char* error = nullptr;
        const int rc = lp_invoke(client, "echo", "[\"x\"]", 3000, &result, &error);
        EXPECT_EQ(rc, LP_ERR_UNAVAILABLE);
        EXPECT_EQ(result, nullptr);
        ASSERT_NE(error, nullptr);
        nlohmann::json e = nlohmann::json::parse(error, nullptr, false);
        EXPECT_EQ(e.value("code", std::string{}), "UNAUTHORIZED");
        lp_string_free(error);
    }

    lp_unsubscribe(sub);
    lp_client_destroy(client);
    lp_provider_destroy(prov);
}

TEST(LpPlainClientConsumer, RequestModuleHandshakeMintsAndCachesToken)
{
    constexpr uint16_t kEchoPort = 47932;
    constexpr uint16_t kCapPort  = 47933;

    // Echo provider that only accepts the token the capability stub will mint.
    lp_provider* echo = lp_provider_create("echo2_mod", tset(kEchoPort).c_str());
    ASSERT_NE(echo, nullptr);
    ASSERT_EQ(lp_provider_register(echo, echoDispatch, echoGetMethods, acceptToken, nullptr),
              LP_OK);
    ASSERT_EQ(lp_provider_save_token(echo, "origin2", "cap-minted-token"), LP_OK);

    // capability_module stub returning that token, gated on the client's
    // capability token.
    lp_provider* cap = lp_provider_create("capability_module", tset(kCapPort).c_str());
    ASSERT_NE(cap, nullptr);
    ASSERT_EQ(lp_provider_register(cap, capDispatch, capGetMethods, acceptToken, nullptr),
              LP_OK);
    ASSERT_EQ(lp_provider_save_token(cap, "origin2", "cap-auth"), LP_OK);
    ASSERT_EQ(lp_token_save("capability_module", "cap-auth"), LP_OK);

    // No pre-seeded echo2_mod token → the client must auto-handshake.
    lp_client* client = lp_client_create("echo2_mod", "origin2",
                                         tobj(kEchoPort).c_str(), tobj(kCapPort).c_str());
    ASSERT_NE(client, nullptr);

    char* result = nullptr; char* error = nullptr;
    ASSERT_EQ(lp_invoke(client, "echo", "[\"handshook\"]", 4000, &result, &error), LP_OK)
        << (error ? error : "");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(nlohmann::json::parse(result).value("echo", std::string()), "handshook");
    lp_string_free(result);

    // The minted token is now cached in the shared TokenManager.
    char* tok = lp_token_get("echo2_mod");
    ASSERT_NE(tok, nullptr);
    EXPECT_EQ(std::string(tok), "cap-minted-token");
    lp_string_free(tok);

    lp_client_destroy(client);
    lp_provider_destroy(cap);
    lp_provider_destroy(echo);
}
