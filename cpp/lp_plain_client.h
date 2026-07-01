#ifndef LOGOS_LP_PLAIN_CLIENT_H
#define LOGOS_LP_PLAIN_CLIENT_H

#include "implementations/plain/rpc_value.h"
#include "logos_transport_config.h"

#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace logos::plain {

class RpcConnectionBase;
class PlainTransportConnection;

// -----------------------------------------------------------------------------
// LpPlainClient — the Qt-free consumer twin of LogosAPIClient.
//
// It dials a target module (and, when needed, capability_module) over a PLAIN
// transport (TCP / TCP+SSL / plain-local) using RpcConnection directly. There
// is NO QObject, NO QCoreApplication, and NO Qt event loop on this path:
//   * sync invoke() blocks on std::future,
//   * async invokeAsync() completes on the connection's Asio I/O thread (via
//     RpcConnection::sendCallCb) — never posted to a Qt event loop,
//   * events are std::function callbacks delivered on the I/O thread,
//   * the auto-`requestModule` capability handshake + token store are done in
//     std (mirroring LogosAPIClient), reading/writing the shared TokenManager
//     so lp_token_save/lp_token_get interoperate.
//
// A "multi" (concurrent-dispatch) provider that returns the pending sentinel
// and later pushes a completion event is handled transparently, same as
// PlainLogosObject, but keyed on the JSON result rather than QVariantMap.
//
// Teardown: an internal guard quiesces any in-flight I/O-thread callback before
// the object is destroyed (mirrors Phase-1's LpProviderDispatch::shutdown()).
// -----------------------------------------------------------------------------
class LpPlainClient {
public:
    // (ok, resultJson, errCode, errMsg). On success errCode/errMsg are empty.
    using ResultCb = std::function<void(bool ok,
                                        const std::string& resultJson,
                                        const std::string& errCode,
                                        const std::string& errMsg)>;
    // (eventName, dataJson) where dataJson is a JSON array of the event args.
    using EventCb = std::function<void(const std::string& eventName,
                                       const std::string& dataJson)>;

    // Dials the target (and, if capabilityCfg is a plain protocol and distinct
    // work is possible, capability_module). Returns nullptr only if the TARGET
    // connection can't be established — the capability connection is best-effort
    // (a missing capability endpoint just means the handshake will fail later,
    // exactly like LogosAPIClient leaving m_capability_consumer disconnected).
    static std::unique_ptr<LpPlainClient> create(
        std::string targetModule,
        std::string originModule,
        const LogosTransportConfig& targetCfg,
        const LogosTransportConfig& capabilityCfg);

    ~LpPlainClient();

    LpPlainClient(const LpPlainClient&) = delete;
    LpPlainClient& operator=(const LpPlainClient&) = delete;

    // Synchronous call. Fills *resultJson on success; *errCode/*errMsg on
    // failure. Any out-pointer may be null.
    bool invoke(const std::string& method, const std::string& argsJson,
                int timeoutMs, std::string* resultJson,
                std::string* errCode, std::string* errMsg);

    // Asynchronous call. `cb` fires exactly once on the Asio I/O thread.
    void invokeAsync(const std::string& method, const std::string& argsJson,
                     int timeoutMs, ResultCb cb);

    // Subscribe to an event on the target. `cb` fires on the I/O thread for each
    // emission until shutdown. Re-subscribing the same event replaces the sink
    // (one sink per event, matching the plain wire's per-(object,event) map).
    void subscribe(const std::string& eventName, EventCb cb);

    // The target's method interface as a JSON array string ("[]" on failure).
    std::string getMethodsJson();

    // Fire-and-forget token registration toward capability_module (or the
    // target if no capability connection). Returns false if nothing to send on.
    bool informModuleToken(const std::string& authToken,
                           const std::string& moduleName,
                           const std::string& token);

    // Stop the connections and quiesce in-flight I/O-thread callbacks. Safe to
    // call more than once; the destructor calls it.
    void shutdown();

private:
    LpPlainClient(std::string targetModule, std::string originModule);

    // Callback-lifetime latch shared with every I/O-thread continuation.
    struct Guard {
        std::recursive_mutex mu;
        bool alive = true;
    };

    // A call queued behind an in-flight requestModule handshake for a target.
    struct PendingCall {
        std::string           method;
        std::vector<RpcValue> args;
        int                   timeoutMs;
        ResultCb              cb;
    };

    std::string resolveTokenSync();                 // may block on the handshake
    void        startHandshakeAsync();              // one handshake per target burst
    void        dispatchAsyncCall(const std::string& token,
                                  const std::string& method,
                                  std::vector<RpcValue> args,
                                  int timeoutMs, ResultCb cb);

    // Multi-dispatch completion rendezvous (sentinel → completion event).
    void ensureCompletionSub();
    void onCompletion(const std::string& callId, std::string resultJson);
    bool awaitCompletionSync(const std::string& callId, int timeoutMs,
                             std::string* out);
    void registerCompletionWaiterAsync(const std::string& callId,
                                       std::function<void(std::string)> cb);

    std::string getToken(const std::string& module) const;
    void        saveToken(const std::string& module, const std::string& token) const;

    std::string                        m_target;
    std::string                        m_origin;
    // The dialers own the sockets; we drive their raw connections directly.
    std::unique_ptr<PlainTransportConnection> m_targetTc;
    std::unique_ptr<PlainTransportConnection> m_capTc; // may be null
    std::shared_ptr<RpcConnectionBase> m_targetConn;
    std::shared_ptr<RpcConnectionBase> m_capConn; // may be null
    std::shared_ptr<Guard>             m_g;

    // Async handshake coalescing (mirrors LogosAPIClient::m_pendingHandshakes).
    std::mutex                                m_hsMu;
    std::vector<PendingCall>                  m_pendingHs;

    // Completion registry (deferred "multi" results).
    std::mutex                                          m_complMu;
    std::condition_variable                             m_complCv;
    std::map<std::string, std::string>                  m_complResults;
    std::map<std::string, std::function<void(std::string)>> m_complWaiters;
    bool                                                m_complClosed = false;
    bool                                                m_complSubscribed = false;

    bool m_shutdown = false;
};

} // namespace logos::plain

#endif // LOGOS_LP_PLAIN_CLIENT_H
