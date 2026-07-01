#ifndef LP_PROVIDER_DISPATCH_H
#define LP_PROVIDER_DISPATCH_H

#include "logos_protocol.h" // lp_dispatch_cb / lp_getmethods_cb / lp_token_cb

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace logos::plain {

// -----------------------------------------------------------------------------
// LpProviderDispatch — the Qt-free provider-side dispatcher for the plain
// transport. It wraps the lp_provider_* C callbacks plus a per-module token
// store, and is called DIRECTLY by PlainTransportHost on the Boost.Asio I/O
// thread — no ModuleProxy, no QObject, no Qt event loop, no QCoreApplication.
// This is the plain analog of the (Qt) ModuleProxy.
//
// Thread-safety: callMethod runs on I/O threads while saveToken/emitEvent run
// on the caller's thread, so the token store and event sink are mutex-guarded.
// The provided callbacks may therefore be invoked from a protocol I/O thread.
// -----------------------------------------------------------------------------
class LpProviderDispatch {
public:
    // (eventName, dataJson) — dataJson is a JSON array payload.
    using EventSink = std::function<void(const std::string& eventName,
                                         const std::string& dataJson)>;

    LpProviderDispatch(std::string moduleName,
                       lp_dispatch_cb dispatch,
                       lp_getmethods_cb getMethods,
                       lp_token_cb onToken,
                       void* userData);

    const std::string& moduleName() const { return m_name; }

    // Dispatch an incoming call. The ungated introspection calls
    // (getPluginInterface/getPluginMethods/getPluginEvents) are answered
    // directly; every other method requires an authorized token. On success
    // sets *ok=true and returns the result JSON; on failure sets *ok=false and
    // fills *errCode ("UNAUTHORIZED" / "METHOD_FAILED"), returning "".
    std::string callMethod(const std::string& authToken,
                           const std::string& method,
                           const std::string& argsJson,
                           bool* ok, std::string* errCode);

    // The module's full interface JSON (methods + events, each "type"-tagged) —
    // the getMethods callback verbatim.
    std::string interfaceJson();

    // Inbound token registration (a caller presenting a capability token).
    bool informModuleToken(const std::string& authToken,
                           const std::string& fromModule,
                           const std::string& token);

    // Issue/record a token for a caller (lp_provider_save_token) — authorizes it.
    bool saveToken(const std::string& fromModule, const std::string& token);

    // lp_provider_emit_event → fan out through the host-installed sink.
    void emitEvent(const std::string& eventName, const std::string& dataJson);
    void setEventSink(EventSink sink);

    // Quiesce for teardown: blocks until any in-flight callMethod / emitEvent /
    // informModuleToken finishes, then forbids new ones (they become no-ops).
    // lp_provider_destroy calls this BEFORE dropping the hosts / this object, so
    // the io-thread dispatch and the caller's emit can never touch a torn-down
    // host or invoke a user callback on a torn-down module. Mirrors the client's
    // CbGuard teardown latch.
    void shutdown();

private:
    // m_mu must be held for the FULL duration of callMethod/emitEvent (across the
    // user callback + the event sink), so shutdown() can wait them out. Recursive
    // because the introspection fast-path calls interfaceJson() re-entrantly.
    bool isAuthorized(const std::string& token) const;

    std::string      m_name;
    lp_dispatch_cb   m_dispatch;
    lp_getmethods_cb m_getMethods;
    lp_token_cb      m_onToken;
    void*            m_userData;

    mutable std::recursive_mutex                 m_mu;
    bool                                         m_alive = true;
    std::unordered_map<std::string, std::string> m_tokens;
    EventSink                                    m_sink;
};

} // namespace logos::plain

#endif // LP_PROVIDER_DISPATCH_H
