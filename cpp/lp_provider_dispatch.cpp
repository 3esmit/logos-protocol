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
    if (!m_getMethods) return "[]";
    std::string s = takeString(m_getMethods(m_userData));
    return s.empty() ? "[]" : s;
}

std::string LpProviderDispatch::callMethod(const std::string& authToken,
                                           const std::string& method,
                                           const std::string& argsJson,
                                           bool* ok, std::string* errCode)
{
    auto fail = [&](const char* code) -> std::string {
        if (ok) *ok = false;
        if (errCode) *errCode = code;
        return std::string();
    };

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

    std::string result =
        takeString(m_dispatch(method.c_str(), argsJson.c_str(), m_userData));
    if (result.empty()) return fail("METHOD_FAILED"); // NULL/empty = provider failure
    if (ok) *ok = true;
    return result;
}

bool LpProviderDispatch::saveToken(const std::string& fromModule, const std::string& token)
{
    if (fromModule.empty() || token.empty()) return false;
    std::lock_guard<std::mutex> g(m_mu);
    m_tokens[fromModule] = token;
    return true;
}

bool LpProviderDispatch::informModuleToken(const std::string& /*authToken*/,
                                           const std::string& fromModule,
                                           const std::string& token)
{
    // NOTE: the inbound gating (only the trusted core/capability channel may
    // register a token) is enforced Qt-side by ModuleProxy today. The Qt-free
    // path defers that gating to the transport-level capability handshake
    // (Phase 2); until then we only accept an inbound token when the module
    // itself opts in via its on_token callback (a module with no on_token
    // rejects, so nothing is auto-authorized).
    if (!m_onToken) return false;
    if (m_onToken(fromModule.c_str(), token.c_str(), m_userData) != LP_OK) return false;
    return saveToken(fromModule, token);
}

bool LpProviderDispatch::isAuthorized(const std::string& token) const
{
    if (token.empty()) return false; // fail closed
    std::lock_guard<std::mutex> g(m_mu);
    bool authorized = false;
    for (const auto& kv : m_tokens)
        authorized |= constantTimeEquals(token, kv.second); // never early-out
    return authorized;
}

void LpProviderDispatch::setEventSink(EventSink sink)
{
    std::lock_guard<std::mutex> g(m_mu);
    m_sink = std::move(sink);
}

void LpProviderDispatch::emitEvent(const std::string& eventName, const std::string& dataJson)
{
    EventSink sink;
    {
        std::lock_guard<std::mutex> g(m_mu);
        sink = m_sink;
    }
    if (sink) sink(eventName, dataJson);
}

} // namespace logos::plain
