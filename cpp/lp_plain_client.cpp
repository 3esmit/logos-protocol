#include "lp_plain_client.h"

#include "logos_async_dispatch.h"
#include "token_manager.h"

#include "implementations/plain/json_mapping.h"
#include "implementations/plain/plain_transport_connection.h"
#include "implementations/plain/rpc_connection.h"
#include "implementations/plain/rpc_message.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <thread>
#include <utility>

namespace logos::plain {

namespace {

using json = nlohmann::json;
using clock_t_ = std::chrono::steady_clock;

// The sentinel key / completion event name, as std strings (single source of
// truth in logos_async_dispatch.h; QString→std here, no event loop involved).
const std::string kPendingKey     = logos::pendingCallKey().toStdString();
const std::string kCompleteEvent  = logos::callCompleteEvent().toStdString();

bool isPlainProtocol(const LogosTransportConfig& cfg)
{
    return cfg.protocol == LogosProtocol::Tcp
        || cfg.protocol == LogosProtocol::TcpSsl;
}

int effectiveTimeout(int timeoutMs)
{
    return timeoutMs > 0 ? timeoutMs : 30000;
}

// Connect `tc` with a deadline-driven retry (same 5s budget as LogosAPIConsumer,
// covering a cold-start listener that lags us). Returns whether it connected;
// either way the caller keeps the dialer.
bool connectWithRetry(PlainTransportConnection& tc)
{
    const auto deadline = clock_t_::now() + std::chrono::milliseconds(5000);
    while (true) {
        if (tc.connectToHost()) return true;
        if (clock_t_::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

} // namespace

LpPlainClient::LpPlainClient(std::string targetModule, std::string originModule)
    : m_target(std::move(targetModule))
    , m_origin(std::move(originModule))
    , m_g(std::make_shared<Guard>())
{
}

LpPlainClient::~LpPlainClient()
{
    shutdown();
}

std::unique_ptr<LpPlainClient> LpPlainClient::create(
    std::string targetModule, std::string originModule,
    const LogosTransportConfig& targetCfg,
    const LogosTransportConfig& capabilityCfg)
{
    if (targetModule.empty() || !isPlainProtocol(targetCfg)) return nullptr;

    // Best-effort connect with a deadline retry (tolerates a cold-start
    // listener). We KEEP the client even if the target is currently
    // unreachable — like LogosAPIConsumer, construction never fails on a
    // refused connect; invoke() then surfaces a clean object_unavailable so a
    // consumer can be built before its target comes up (the documented ABI).
    auto targetTc = std::make_unique<PlainTransportConnection>(targetCfg);
    if (!connectWithRetry(*targetTc))
        spdlog::info("[lp_plain_client] target '{}' not reachable yet; "
                     "calls will report object_unavailable until it is",
                     targetModule);

    // Capability connection is best-effort and only when it's a plain endpoint
    // distinct from a self-connection to capability_module. Single attempt — a
    // missing capability endpoint just disables the requestModule handshake, so
    // don't spend the retry budget on it.
    std::unique_ptr<PlainTransportConnection> capTc;
    if (targetModule != "capability_module" && isPlainProtocol(capabilityCfg)) {
        capTc = std::make_unique<PlainTransportConnection>(capabilityCfg);
        capTc->connectToHost();
    }

    std::unique_ptr<LpPlainClient> c(
        new LpPlainClient(std::move(targetModule), std::move(originModule)));
    c->m_targetConn = targetTc->connection(); // may be null (unreachable)
    c->m_targetTc   = std::move(targetTc);
    if (capTc) {
        c->m_capConn = capTc->connection();   // may be null
        c->m_capTc   = std::move(capTc);
    }
    return c;
}

// ── token store (shared process-wide TokenManager) ──────────────────────────

std::string LpPlainClient::getToken(const std::string& module) const
{
    return TokenManager::instance().getToken(module);
}

void LpPlainClient::saveToken(const std::string& module, const std::string& token) const
{
    if (!module.empty() && !token.empty())
        TokenManager::instance().saveToken(module, token);
}

// ── synchronous capability handshake ────────────────────────────────────────

std::string LpPlainClient::resolveTokenSync()
{
    std::string token = getToken(m_target);
    if (!token.empty()) return token;
    if (m_target == "capability_module" || !m_capConn || !m_capConn->isOpen())
        return token; // empty — no handshake available

    CallMessage msg;
    msg.id        = m_capConn->nextId();
    msg.authToken = getToken("capability_module");
    msg.object    = "capability_module";
    msg.method    = "requestModule";
    msg.args      = argsFromJson(json::array({m_origin, m_target}));

    auto fut = m_capConn->sendCall(std::move(msg));
    if (fut.wait_for(std::chrono::milliseconds(20000)) != std::future_status::ready)
        return {};
    auto res = fut.get();
    if (!res.ok) return {};
    const json v = valueToJson(res.value);
    if (v.is_string()) token = v.get<std::string>();
    if (!token.empty()) saveToken(m_target, token);
    return token;
}

// ── synchronous invoke ──────────────────────────────────────────────────────

bool LpPlainClient::invoke(const std::string& method, const std::string& argsJson,
                           int timeoutMs, std::string* resultJson,
                           std::string* errCode, std::string* errMsg)
{
    auto fail = [&](const char* code, const std::string& msg) {
        if (errCode) *errCode = code;
        if (errMsg)  *errMsg  = msg;
        return false;
    };
    // No live connection == the target object couldn't be acquired. Mirror the
    // Qt consumer's canonical error (object_unavailable) so the C-ABI surface is
    // identical whether the backend is QtRO or plain.
    if (!m_targetConn || !m_targetConn->isOpen())
        return fail("object_unavailable",
                    "failed to acquire remote object '" + m_target +
                    "' (module not loaded, not published, or transport failure)");

    // Parse args first so a bad payload fails before any network work.
    std::vector<RpcValue> args;
    if (!argsJson.empty()) {
        json parsed = json::parse(argsJson, nullptr, /*allow_exceptions=*/false);
        if (parsed.is_discarded() || !parsed.is_array())
            return fail("invalid_args", "args_json must be a JSON array");
        args = argsFromJson(parsed);
    }

    ensureCompletionSub();
    const std::string token = resolveTokenSync();
    const int eff = effectiveTimeout(timeoutMs);

    CallMessage msg;
    msg.id        = m_targetConn->nextId();
    msg.authToken = token;
    msg.object    = m_target;
    msg.method    = method;
    msg.args      = std::move(args);

    auto fut = m_targetConn->sendCall(std::move(msg));
    if (fut.wait_for(std::chrono::milliseconds(eff)) != std::future_status::ready)
        return fail("TIMEOUT", "call timed out");
    auto res = fut.get();
    if (!res.ok)
        return fail(res.errCode.empty() ? "METHOD_FAILED" : res.errCode.c_str(),
                    res.err);

    json v = valueToJson(res.value);
    // Deferred "multi" completion: the provider returned a pending sentinel and
    // will push the real result as a completion event keyed by callId.
    if (v.is_object() && v.contains(kPendingKey) && v[kPendingKey].is_string()) {
        std::string out;
        if (!awaitCompletionSync(v[kPendingKey].get<std::string>(), eff, &out))
            return fail("TIMEOUT", "deferred call timed out");
        if (resultJson) *resultJson = std::move(out);
        return true;
    }
    if (resultJson) *resultJson = v.dump();
    return true;
}

// ── asynchronous invoke ─────────────────────────────────────────────────────

void LpPlainClient::invokeAsync(const std::string& method, const std::string& argsJson,
                                int timeoutMs, ResultCb cb)
{
    if (!cb) return;
    if (!m_targetConn || !m_targetConn->isOpen()) {
        cb(false, "", "TRANSPORT_CLOSED", "connection closed");
        return;
    }

    std::vector<RpcValue> args;
    if (!argsJson.empty()) {
        json parsed = json::parse(argsJson, nullptr, /*allow_exceptions=*/false);
        if (parsed.is_discarded() || !parsed.is_array()) {
            cb(false, "", "invalid_args", "args_json must be a JSON array");
            return;
        }
        args = argsFromJson(parsed);
    }

    ensureCompletionSub();
    const int eff = effectiveTimeout(timeoutMs);

    // Resolve the token; coalesce concurrent first-calls behind ONE handshake so
    // overlapping requestModule calls don't mint tokens that overwrite each
    // other at the target (mirrors LogosAPIClient::invokeRemoteMethodAsync).
    std::string token;
    bool queued  = false;
    bool startHs = false;
    {
        std::lock_guard<std::mutex> lk(m_hsMu);
        token = getToken(m_target);
        if (token.empty() && m_target != "capability_module"
            && m_capConn && m_capConn->isOpen()) {
            m_pendingHs.push_back({method, std::move(args), eff, std::move(cb)});
            startHs = (m_pendingHs.size() == 1); // first caller starts the handshake
            queued  = true;
        }
    }
    if (queued) {
        if (startHs) startHandshakeAsync(); // outside the lock (it re-locks)
        return;                             // args/cb were moved into the queue
    }

    // Token already known (or no handshake possible) — dispatch directly.
    dispatchAsyncCall(token, method, std::move(args), eff, std::move(cb));
}

void LpPlainClient::startHandshakeAsync()
{
    if (!m_capConn || !m_capConn->isOpen()) {
        // Can't handshake — drain everyone with a failure so callers don't hang.
        std::vector<PendingCall> calls;
        {
            std::lock_guard<std::mutex> lk(m_hsMu);
            calls.swap(m_pendingHs);
        }
        for (auto& c : calls)
            if (c.cb) c.cb(false, "", "UNAVAILABLE", "capability_module unreachable");
        return;
    }

    CallMessage msg;
    msg.id        = m_capConn->nextId();
    msg.authToken = getToken("capability_module");
    msg.object    = "capability_module";
    msg.method    = "requestModule";
    msg.args      = argsFromJson(json::array({m_origin, m_target}));

    std::shared_ptr<Guard> g = m_g;
    m_capConn->sendCallCb(std::move(msg), [this, g](ResultMessage res) {
        std::lock_guard<std::recursive_mutex> lock(g->mu);
        if (!g->alive) return;

        std::string tok;
        if (res.ok) {
            const json v = valueToJson(res.value);
            if (v.is_string()) tok = v.get<std::string>();
        }
        if (!tok.empty()) saveToken(m_target, tok);

        std::vector<PendingCall> calls;
        {
            std::lock_guard<std::mutex> lk(m_hsMu);
            calls.swap(m_pendingHs);
        }
        // Drain every queued call with the one minted token. An empty tok
        // (handshake failed) still flows through: the target rejects the call
        // and each callback fires with an error, so nobody hangs.
        for (auto& c : calls)
            dispatchAsyncCall(tok, c.method, std::move(c.args), c.timeoutMs, std::move(c.cb));
    });
}

void LpPlainClient::dispatchAsyncCall(const std::string& token,
                                      const std::string& method,
                                      std::vector<RpcValue> args,
                                      int timeoutMs, ResultCb cb)
{
    if (!m_targetConn || !m_targetConn->isOpen()) {
        if (cb) cb(false, "", "TRANSPORT_CLOSED", "connection closed");
        return;
    }
    CallMessage msg;
    msg.id        = m_targetConn->nextId();
    msg.authToken = token;
    msg.object    = m_target;
    msg.method    = method;
    msg.args      = std::move(args);

    std::shared_ptr<Guard> g = m_g;
    (void)timeoutMs; // async has no per-call timer; the deferred path relies on
                     // the provider completing (Qt-free providers never defer).
    m_targetConn->sendCallCb(std::move(msg), [this, g, cb](ResultMessage res) {
        std::lock_guard<std::recursive_mutex> lock(g->mu);
        if (!g->alive) return;
        if (!res.ok) {
            cb(false, "",
               res.errCode.empty() ? "METHOD_FAILED" : res.errCode, res.err);
            return;
        }
        json v = valueToJson(res.value);
        if (v.is_object() && v.contains(kPendingKey) && v[kPendingKey].is_string()) {
            // Deferred completion: fire cb when the completion event lands.
            registerCompletionWaiterAsync(
                v[kPendingKey].get<std::string>(),
                [this, g, cb](std::string resultJson) {
                    std::lock_guard<std::recursive_mutex> lock2(g->mu);
                    if (!g->alive) return;
                    cb(true, resultJson, "", "");
                });
            return;
        }
        cb(true, v.dump(), "", "");
    });
}

// ── multi-dispatch completion rendezvous ────────────────────────────────────

void LpPlainClient::ensureCompletionSub()
{
    {
        std::lock_guard<std::mutex> lk(m_complMu);
        if (m_complSubscribed) return;
        m_complSubscribed = true;
    }
    if (!m_targetConn) return;

    SubscribeMessage sub;
    sub.object    = m_target;
    sub.eventName = kCompleteEvent;
    std::shared_ptr<Guard> g = m_g;
    m_targetConn->sendSubscribe(std::move(sub), [this, g](EventMessage evt) {
        std::lock_guard<std::recursive_mutex> lock(g->mu);
        if (!g->alive) return;
        if (evt.data.size() != 2) return;
        const json callId = valueToJson(evt.data[0]);
        if (!callId.is_string()) return;
        onCompletion(callId.get<std::string>(), valueToJson(evt.data[1]).dump());
    });
}

void LpPlainClient::onCompletion(const std::string& callId, std::string resultJson)
{
    std::function<void(std::string)> waiter;
    {
        std::lock_guard<std::mutex> lk(m_complMu);
        if (m_complClosed) return;
        auto it = m_complWaiters.find(callId);
        if (it != m_complWaiters.end()) {
            waiter = std::move(it->second);
            m_complWaiters.erase(it);
        } else {
            // No waiter yet (result raced ahead) — buffer + wake sync waiters.
            m_complResults[callId] = std::move(resultJson);
            m_complCv.notify_all();
            return;
        }
    }
    waiter(std::move(resultJson));
}

bool LpPlainClient::awaitCompletionSync(const std::string& callId, int timeoutMs,
                                        std::string* out)
{
    std::unique_lock<std::mutex> lk(m_complMu);
    const auto deadline = clock_t_::now()
        + std::chrono::milliseconds(effectiveTimeout(timeoutMs));
    const bool got = m_complCv.wait_until(lk, deadline, [&] {
        return m_complClosed || m_complResults.count(callId) > 0;
    });
    if (!got || m_complClosed) return false;
    auto it = m_complResults.find(callId);
    if (it == m_complResults.end()) return false;
    if (out) *out = std::move(it->second);
    m_complResults.erase(it);
    return true;
}

void LpPlainClient::registerCompletionWaiterAsync(const std::string& callId,
                                                  std::function<void(std::string)> cb)
{
    std::string buffered;
    bool haveBuffered = false;
    {
        std::lock_guard<std::mutex> lk(m_complMu);
        if (m_complClosed) return;
        auto it = m_complResults.find(callId);
        if (it == m_complResults.end()) {
            m_complWaiters[callId] = std::move(cb);
            return;
        }
        buffered = std::move(it->second);
        m_complResults.erase(it);
        haveBuffered = true;
    }
    if (haveBuffered && cb) cb(std::move(buffered));
}

// ── events / introspection / tokens ─────────────────────────────────────────

void LpPlainClient::subscribe(const std::string& eventName, EventCb cb)
{
    if (!m_targetConn || !m_targetConn->isOpen() || !cb) return;
    SubscribeMessage sub;
    sub.object    = m_target;
    sub.eventName = eventName;
    std::shared_ptr<Guard> g = m_g;
    m_targetConn->sendSubscribe(std::move(sub), [g, cb](EventMessage evt) {
        std::lock_guard<std::recursive_mutex> lock(g->mu);
        if (!g->alive) return;
        cb(evt.eventName, argsToJson(evt.data).dump());
    });
}

std::string LpPlainClient::getMethodsJson()
{
    if (!m_targetConn || !m_targetConn->isOpen()) return "[]";
    MethodsMessage msg;
    msg.id     = m_targetConn->nextId();
    msg.object = m_target;
    auto fut = m_targetConn->sendMethods(std::move(msg));
    if (fut.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
        return "[]";
    auto res = fut.get();
    if (!res.ok) return "[]";
    json arr = json::array();
    for (const auto& md : res.methods) arr.push_back(methodToJson(md));
    return arr.dump();
}

bool LpPlainClient::informModuleToken(const std::string& authToken,
                                      const std::string& moduleName,
                                      const std::string& token)
{
    RpcConnectionBase* conn = m_capConn ? m_capConn.get() : m_targetConn.get();
    if (!conn || !conn->isOpen()) return false;
    TokenMessage msg;
    msg.authToken  = authToken;
    msg.moduleName = moduleName;
    msg.token      = token;
    conn->sendToken(std::move(msg));
    return true; // fire-and-forget
}

// ── teardown ────────────────────────────────────────────────────────────────

void LpPlainClient::shutdown()
{
    if (m_shutdown) return;
    m_shutdown = true;

    // Stop the wire first: fail() releases every pending sync future and async
    // callback with a transport error, so no caller hangs. Those async
    // continuations fire on the calling thread here and gate on m_g (still
    // alive), touching `this` safely.
    if (m_targetConn) m_targetConn->stop("client shutdown");
    if (m_capConn)    m_capConn->stop("client shutdown");

    // Wake any sync completion waiter and forbid new completions.
    {
        std::lock_guard<std::mutex> lk(m_complMu);
        m_complClosed = true;
    }
    m_complCv.notify_all();

    // Quiesce: block until any in-flight I/O-thread callback finishes, then
    // forbid new ones. After this returns, destroying `this` can't race them.
    {
        std::lock_guard<std::recursive_mutex> g(m_g->mu);
        m_g->alive = false;
    }
}

} // namespace logos::plain
