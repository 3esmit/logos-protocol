#include "lp_provider_dispatch.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <utility>

namespace logos::plain {

namespace {

// Length-independent constant-time token compare — the Qt-free twin of the
// helper in module_proxy.cpp. Scans the longer of the two lengths so timing
// leaks neither a correct prefix nor the secret's length.
bool constantTimeEquals(const std::string& a, const std::string& b)
{
    const size_t n = std::max(a.size(), b.size());
    unsigned diff = static_cast<unsigned>(a.size() ^ b.size());
    for (size_t i = 0; i < n; ++i) {
        const unsigned char ca = i < a.size() ? static_cast<unsigned char>(a[i]) : 0;
        const unsigned char cb = i < b.size() ? static_cast<unsigned char>(b[i]) : 0;
        diff |= static_cast<unsigned>(ca ^ cb);
    }
    return diff == 0;
}

// Adopt a char* returned by an lp_* callback into a std::string and free it via
// the documented lp_string_free contract (the provider allocates, we free).
std::string takeString(char* s)
{
    if (!s) return {};
    std::string out(s);
    lp_string_free(s);
    return out;
}

} // namespace

LpProviderDispatch::LpProviderDispatch(std::string moduleName,
                                       lp_dispatch_cb dispatch,
                                       lp_getmethods_cb getMethods,
                                       lp_token_cb onToken,
                                       void* userData)
    : m_name(std::move(moduleName))
    , m_dispatch(dispatch)
    , m_getMethods(getMethods)
    , m_onToken(onToken)
    , m_userData(userData)
{}

std::string LpProviderDispatch::interfaceJson()
{
    std::lock_guard<std::recursive_mutex> g(m_mu);
    if (!m_getMethods) return "[]";
    std::string s = takeString(m_getMethods(m_userData));
    return s.empty() ? "[]" : s;
}

std::string LpProviderDispatch::callMethod(const std::string& authToken,
                                           const std::string& method,
                                           const std::string& argsJson,
                                           bool* ok, std::string* errCode)
{
    std::lock_guard<std::recursive_mutex> g(m_mu);
    auto fail = [&](const char* code) -> std::string {
        if (ok) *ok = false;
        if (errCode) *errCode = code;
        return std::string();
    };
    if (!m_alive) return fail("UNAVAILABLE"); // provider shutting down

    // Ungated introspection — a caller discovers the interface before any token
    // exists (mirrors ModuleProxy's getPlugin* fast-path). getMethods returns
    // the full interface; getPluginMethods/Events split it by the "type" tag.
    if (method == "getPluginInterface" || method == "getPluginMethods"
        || method == "getPluginEvents") {
        nlohmann::json iface = nlohmann::json::parse(interfaceJson(), nullptr, false);
        if (!iface.is_array()) iface = nlohmann::json::array();
        if (method == "getPluginInterface") { if (ok) *ok = true; return iface.dump(); }
        const bool keepEvents = (method == "getPluginEvents");
        nlohmann::json out = nlohmann::json::array();
        for (const auto& e : iface) {
            const bool isEvent =
                e.is_object() && e.value("type", std::string()) == "event";
            if (isEvent == keepEvents) out.push_back(e);
        }
        if (ok) *ok = true;
        return out.dump();
    }

    if (!isAuthorized(authToken)) {
        spdlog::warn("[lp_provider] {}: rejecting unauthorized call to {}",
                     m_name, method);
        return fail("UNAUTHORIZED");
    }
    if (!m_dispatch) return fail("METHOD_FAILED");

    // Per the ABI, the dispatch callback returns NULL on failure and any
    // non-NULL heap string (even empty) on success. Check the raw pointer so an
    // empty-string result is NOT misread as a failure.
    char* raw = m_dispatch(method.c_str(), argsJson.c_str(), m_userData);
    if (!raw) return fail("METHOD_FAILED");
    std::string result(raw);
    lp_string_free(raw);
    if (ok) *ok = true;
    return result;
}

bool LpProviderDispatch::saveToken(const std::string& fromModule, const std::string& token)
{
    if (fromModule.empty() || token.empty()) return false;
    std::lock_guard<std::recursive_mutex> g(m_mu);
    m_tokens[fromModule] = token;
    return true;
}

bool LpProviderDispatch::informModuleToken(const std::string& /*authToken*/,
                                           const std::string& /*fromModule*/,
                                           const std::string& /*token*/)
{
    // SECURITY: the plain transport is unauthenticated at the wire, so an
    // inbound TokenMessage carries no proof the caller is the trusted
    // core/capability_module channel. The Qt path (ModuleProxy::informModuleToken)
    // gates this by constant-time-matching the trusted token in TokenManager; the
    // Qt-free equivalent lands with the Phase-2 capability handshake. Until then
    // we DEFAULT-REJECT inbound token registration — otherwise any peer that can
    // open the socket could self-authorize via a permissive on_token callback.
    // A provider authorizes callers only via lp_provider_save_token (a token it
    // issued itself).
    return false;
}

bool LpProviderDispatch::isAuthorized(const std::string& token) const
{
    if (token.empty()) return false; // fail closed
    std::lock_guard<std::recursive_mutex> g(m_mu);
    bool authorized = false;
    for (const auto& kv : m_tokens)
        authorized |= constantTimeEquals(token, kv.second); // never early-out
    return authorized;
}

void LpProviderDispatch::setEventSink(EventSink sink)
{
    std::lock_guard<std::recursive_mutex> g(m_mu);
    m_sink = std::move(sink);
}

void LpProviderDispatch::emitEvent(const std::string& eventName, const std::string& dataJson)
{
    // Hold m_mu across the sink invocation so shutdown() waits out an in-flight
    // emit before the host (captured by the sink) is torn down. The sink posts
    // the socket write onto the connection's Asio strand, so calling it under
    // this lock never blocks on I/O. NB: nothing takes the host lock and THEN
    // this lock (unpublish no longer touches the dispatch), so there is no
    // dispatch->host / host->dispatch lock-order cycle.
    std::lock_guard<std::recursive_mutex> g(m_mu);
    if (!m_alive || !m_sink) return;
    m_sink(eventName, dataJson);
}

void LpProviderDispatch::shutdown()
{
    std::lock_guard<std::recursive_mutex> g(m_mu); // waits out in-flight callMethod/emitEvent
    m_alive = false;
    m_sink = nullptr; // drop the host-capturing sink so no later emit reaches it
}

} // namespace logos::plain
