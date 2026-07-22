#include "logos_protocol.h"

#include "logos_api_client.h"
#include "logos_json_convert.h"
#include "logos_mode.h"
#include "logos_object.h"
#include "logos_transport_config.h"
#include "logos_transport_config_json.h"
#include "logos_types.h"
#include "token_manager.h"

#include <nlohmann/json.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaType>
#include <QString>
#include <QThread>
#include <QVariant>
#include <QVariantList>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

namespace {

// Heap-copy a std::string for handing across the C boundary.
// Counterpart of lp_string_free (which is plain free()).
char* lpStrdup(const std::string& s)
{
    char* out = static_cast<char*>(std::malloc(s.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, s.data(), s.size() + 1);
    return out;
}

std::string makeErrorJson(const char* code, const std::string& message,
                          const std::string& origin)
{
    nlohmann::json e;
    e["code"] = code;
    e["message"] = message;
    e["origin"] = origin;
    return e.dump();
}

// Parse a single-transport JSON object (the lp_* shape) by reusing the
// transport-set parser (which expects an array). NULL / empty / "null"
// fall back to the process default.
bool parseTransportJson(const char* transport_json, LogosTransportConfig& out)
{
    if (!transport_json || !*transport_json
        || std::strcmp(transport_json, "null") == 0) {
        out = LogosTransportConfigGlobal::getDefault();
        return true;
    }
    const LogosTransportSet set = logos::transportSetFromJsonString(
        std::string("[") + transport_json + "]");
    if (set.empty()) return false;  // parse error (parser yields empty set)
    out = set.front();
    return true;
}

// Callback guard shared between an lp handle and its in-flight callbacks.
// Invocations hold the mutex while calling user code; teardown takes the
// mutex and clears `alive`, so once lp_client_destroy / lp_unsubscribe
// returns, no further user callback can fire (the cancellation contract in
// logos_protocol.h). recursive_mutex so a callback may itself unsubscribe.
struct CbGuard {
    std::recursive_mutex mutex;
    bool alive = true;
};

Timeout lpTimeout(int timeout_ms)
{
    return timeout_ms > 0 ? Timeout(timeout_ms) : Timeout();
}

// Parse args_json (NULL → empty array) into a QVariantList.
// Returns false (+fills error) when args_json is not a JSON array.
bool parseArgs(const char* args_json, const QString& origin,
               QVariantList& out, std::string& error)
{
    if (!args_json || !*args_json) return true;
    nlohmann::json parsed = nlohmann::json::parse(args_json, nullptr,
                                                  /*allow_exceptions=*/false);
    if (parsed.is_discarded() || !parsed.is_array()) {
        error = makeErrorJson("invalid_args",
                              "args_json must be a JSON array",
                              origin.toStdString());
        return false;
    }
    out = logos::nlohmannArgsToQVariantList(parsed);
    return true;
}

} // namespace

struct lp_client {
    LogosAPIClient* client = nullptr;
    QString target;
    QString origin;
    std::shared_ptr<CbGuard> guard;
};

struct lp_subscription {
    std::shared_ptr<CbGuard> guard;
};

struct lp_provider {
    std::string moduleName;
    std::string transportSetJson;
    lp_dispatch_cb dispatch = nullptr;
    lp_getmethods_cb getMethods = nullptr;
    lp_token_cb onToken = nullptr;
    void* userData = nullptr;
};

extern "C" {

/* ---------------------------------------------------------------- version */

const char* lp_protocol_version(void)
{
    return LOGOS_PROTOCOL_VERSION_STRING;
}

int lp_protocol_abi_major(void)
{
    return LOGOS_PROTOCOL_VERSION_MAJOR;
}

/* ----------------------------------------------------------------- memory */

void lp_string_free(char* s)
{
    std::free(s);
}

/* ----------------------------------------------------- mode / transports */

int lp_set_mode(const char* mode)
{
    if (!mode) return LP_ERR_INVALID_ARG;
    if (std::strcmp(mode, "remote") == 0) {
        LogosModeConfig::setMode(LogosMode::Remote);
    } else if (std::strcmp(mode, "local") == 0) {
        LogosModeConfig::setMode(LogosMode::Local);
    } else if (std::strcmp(mode, "mock") == 0) {
        LogosModeConfig::setMode(LogosMode::Mock);
    } else {
        return LP_ERR_INVALID_ARG;
    }
    return LP_OK;
}

const char* lp_get_mode(void)
{
    switch (LogosModeConfig::getMode()) {
    case LogosMode::Local: return "local";
    case LogosMode::Mock:  return "mock";
    case LogosMode::Remote: break;
    }
    return "remote";
}

int lp_set_default_transport(const char* transport_json)
{
    if (!transport_json) return LP_ERR_INVALID_ARG;
    LogosTransportConfig cfg;
    if (!parseTransportJson(transport_json, cfg)) return LP_ERR_INVALID_ARG;
    LogosTransportConfigGlobal::setDefault(cfg);
    return LP_OK;
}

/* ---------------------------------------------------------------- clients */

lp_client* lp_client_create(const char* target_module,
                            const char* origin_module,
                            const char* target_transport_json,
                            const char* capability_transport_json)
{
    if (!target_module || !*target_module || !origin_module) return nullptr;

    LogosTransportConfig targetCfg;
    LogosTransportConfig capabilityCfg;
    if (!parseTransportJson(target_transport_json, targetCfg)) return nullptr;
    if (!parseTransportJson(capability_transport_json, capabilityCfg)) return nullptr;

    // Same registration LogosAPI's constructor performs — lp-only consumers
    // never construct a LogosAPI, so do it here (idempotent).
    qRegisterMetaType<LogosResult>("LogosResult");

    auto* handle = new lp_client();
    handle->target = QString::fromUtf8(target_module);
    handle->origin = QString::fromUtf8(origin_module);
    handle->guard = std::make_shared<CbGuard>();
    // No QObject parent: the handle owns the client. Constructed on the
    // calling thread, which becomes the owner thread (see header contract).
    handle->client = new LogosAPIClient(handle->target, handle->origin,
                                        &TokenManager::instance(),
                                        targetCfg, capabilityCfg);
    return handle;
}

void lp_client_destroy(lp_client* client)
{
    if (!client) return;
    {
        // Block until no user callback is mid-flight, then forbid new ones.
        std::lock_guard<std::recursive_mutex> lock(client->guard->mutex);
        client->guard->alive = false;
    }
    // The client and its consumers own Qt transport objects (for QtRO: a node
    // and its QLocalSocket, with socket notifiers) that belong to the owner
    // thread. Destroying them from another thread makes Qt disable a notifier
    // cross-thread and closes the fd under the owner's event dispatcher, which
    // faults. Foreign-thread destroys are real: any binding that keeps a client
    // share inside a worker (an event subscription moved into a Rust worker
    // thread, say) runs this on that worker when the last share drops.
    //
    // deleteLater() hands the destruction to the owner thread, matching the
    // marshaling every call path already does (logos::runOnOwnerThread). A
    // *blocking* marshal is not usable here: the owner thread is typically the
    // module's dispatch thread, and it may be blocked joining the very worker
    // running this destroy — that would deadlock. Deferring instead is
    // invisible to callers because the guard above, not the delete, is what
    // enforces the ABI's "no callbacks after this returns" contract.
    //
    // If the owner's event loop never runs again (a process already tearing
    // down), the deferred delete never fires and the client leaks. That is the
    // deliberate trade: a leak at exit beats a crash.
    if (client->client) {
        if (client->client->thread() == QThread::currentThread())
            delete client->client;
        else
            client->client->deleteLater();
    }
    delete client;
}

/* ----------------------------------------------------------------- invoke */

int lp_invoke(lp_client* client,
              const char* method,
              const char* args_json,
              int timeout_ms,
              char** out_result_json,
              char** out_error_json)
{
    if (out_result_json) *out_result_json = nullptr;
    if (out_error_json) *out_error_json = nullptr;
    if (!client || !client->client || !method || !*method) {
        if (out_error_json)
            *out_error_json = lpStrdup(makeErrorJson(
                "invalid_arg", "client and method are required", ""));
        return LP_ERR_INVALID_ARG;
    }

    QVariantList args;
    std::string error;
    if (!parseArgs(args_json, client->origin, args, error)) {
        if (out_error_json) *out_error_json = lpStrdup(error);
        return LP_ERR_INVALID_ARG;
    }

    logos::CallError callErr;
    const QVariant result = client->client->invokeRemoteMethod(
        client->target, QString::fromUtf8(method), args, lpTimeout(timeout_ms), &callErr);

    if (!callErr.ok()) {
        if (out_error_json)
            *out_error_json = lpStrdup(makeErrorJson(
                callErr.code.c_str(), callErr.message, callErr.origin));
        return LP_ERR_UNAVAILABLE;
    }

    if (out_result_json)
        *out_result_json = lpStrdup(logos::qvariantToNlohmann(result).dump());
    return LP_OK;
}

int lp_invoke_async(lp_client* client,
                    const char* method,
                    const char* args_json,
                    int timeout_ms,
                    lp_result_cb cb,
                    void* user_data)
{
    if (!client || !client->client || !method || !*method || !cb)
        return LP_ERR_INVALID_ARG;

    QVariantList args;
    std::string error;
    if (!parseArgs(args_json, client->origin, args, error))
        return LP_ERR_INVALID_ARG;

    std::shared_ptr<CbGuard> guard = client->guard;
    client->client->invokeRemoteMethodAsync(
        client->target, QString::fromUtf8(method), args,
        [guard, cb, user_data](QVariant result) {
            std::lock_guard<std::recursive_mutex> lock(guard->mutex);
            if (!guard->alive) return;  // client destroyed: drop the result
            const std::string json = logos::qvariantToNlohmann(result).dump();
            cb(1, json.c_str(), user_data);
        },
        lpTimeout(timeout_ms));
    return LP_OK;
}

/* ------------------------------------------------------------- subscribe */

lp_subscription* lp_subscribe(lp_client* client,
                              const char* event_name,
                              lp_event_cb cb,
                              void* user_data)
{
    if (!client || !client->client || !event_name || !*event_name || !cb)
        return nullptr;

    LogosObject* object = client->client->requestObject(client->target);
    if (!object) return nullptr;

    auto* sub = new lp_subscription();
    sub->guard = std::make_shared<CbGuard>();

    std::shared_ptr<CbGuard> subGuard = sub->guard;
    std::shared_ptr<CbGuard> clientGuard = client->guard;
    client->client->onEvent(
        object, QString::fromUtf8(event_name),
        [subGuard, clientGuard, cb, user_data](const QString& name,
                                               const QVariantList& data) {
            std::lock_guard<std::recursive_mutex> subLock(subGuard->mutex);
            if (!subGuard->alive) return;  // unsubscribed
            std::lock_guard<std::recursive_mutex> clientLock(clientGuard->mutex);
            if (!clientGuard->alive) return;  // client destroyed
            nlohmann::json payload = nlohmann::json::array();
            for (const QVariant& v : data)
                payload.push_back(logos::qvariantToNlohmann(v));
            const std::string json = payload.dump();
            const QByteArray nameUtf8 = name.toUtf8();
            cb(nameUtf8.constData(), json.c_str(), user_data);
        });
    return sub;
}

void lp_unsubscribe(lp_subscription* sub)
{
    if (!sub) return;
    {
        // The underlying transport keeps its listener; this guard makes it
        // inert, which is what the ABI promises ("the callback will not
        // fire again").
        std::lock_guard<std::recursive_mutex> lock(sub->guard->mutex);
        sub->guard->alive = false;
    }
    delete sub;
}

/* ------------------------------------------------------------ introspect */

char* lp_get_methods(lp_client* client)
{
    if (!client || !client->client) return nullptr;
    LogosObject* object = client->client->requestObject(client->target);
    if (!object) return nullptr;
    const QJsonArray methods = object->getMethods();
    const QByteArray json =
        QJsonDocument(methods).toJson(QJsonDocument::Compact);
    return lpStrdup(std::string(json.constData(),
                                static_cast<size_t>(json.size())));
}

/* ----------------------------------------------------------------- tokens */

char* lp_token_get(const char* module_name)
{
    if (!module_name) return nullptr;
    const QString token =
        TokenManager::instance().getToken(QString::fromUtf8(module_name));
    if (token.isEmpty()) return nullptr;
    return lpStrdup(token.toStdString());
}

int lp_token_save(const char* module_name, const char* token)
{
    if (!module_name || !token) return LP_ERR_INVALID_ARG;
    TokenManager::instance().saveToken(QString::fromUtf8(module_name),
                                       QString::fromUtf8(token));
    return LP_OK;
}

int lp_inform_module_token(lp_client* client,
                           const char* auth_token,
                           const char* module_name,
                           const char* token)
{
    if (!client || !client->client || !auth_token || !module_name || !token)
        return LP_ERR_INVALID_ARG;
    const bool ok = client->client->informModuleToken(
        QString::fromUtf8(auth_token), QString::fromUtf8(module_name),
        QString::fromUtf8(token));
    return ok ? LP_OK : LP_ERR_INTERNAL;
}

/* -------------------------------------------------- provider (groundwork) */

lp_provider* lp_provider_create(const char* module_name,
                                const char* transport_set_json)
{
    if (!module_name || !*module_name) return nullptr;
    auto* provider = new lp_provider();
    provider->moduleName = module_name;
    provider->transportSetJson =
        transport_set_json ? transport_set_json : "[]";
    return provider;
}

void lp_provider_destroy(lp_provider* provider)
{
    delete provider;
}

int lp_provider_register(lp_provider* provider,
                         lp_dispatch_cb dispatch,
                         lp_getmethods_cb get_methods,
                         lp_token_cb on_token,
                         void* user_data)
{
    if (!provider || !dispatch) return LP_ERR_INVALID_ARG;
    provider->dispatch = dispatch;
    provider->getMethods = get_methods;
    provider->onToken = on_token;
    provider->userData = user_data;
    return LP_OK;
}

int lp_provider_emit_event(lp_provider* provider,
                           const char* event_name,
                           const char* data_json)
{
    (void)event_name;
    (void)data_json;
    if (!provider) return LP_ERR_INVALID_ARG;
    // Groundwork only: serving a provider over the transports through the
    // C ABI lands with the common cdylib module-impl ABI (module authoring
    // phase). The registered callbacks above define the contract today.
    return LP_ERR_UNSUPPORTED;
}

int lp_provider_save_token(lp_provider* provider,
                           const char* module_name,
                           const char* token)
{
    (void)module_name;
    (void)token;
    if (!provider) return LP_ERR_INVALID_ARG;
    return LP_ERR_UNSUPPORTED;
}

} // extern "C"
