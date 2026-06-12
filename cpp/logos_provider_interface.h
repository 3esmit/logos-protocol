#ifndef LOGOS_PROVIDER_INTERFACE_H
#define LOGOS_PROVIDER_INTERFACE_H

#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QJsonArray>
#include <nlohmann/json.hpp>
#include <functional>
#include <string>
#include <vector>

#include "logos_json_convert.h"

// ---------------------------------------------------------------------------
// LogosProviderObject — abstract provider-side interface (framework internal)
//
// This is the provider-side counterpart of LogosObject (consumer side).
// ModuleProxy wraps a LogosProviderObject* and publishes it via the transport.
// Module authors do NOT implement this directly — they inherit
// LogosProviderBase (which lives in logos-cpp-sdk, layered above this repo,
// because it hands a LogosAPI* to module code).
//
// Two parallel virtual interfaces:
//   Qt interface:        callMethod / getMethods / setEventListener (pure virtual)
//   Universal interface: callMethodStd / getMethodsStd / setEventListenerStd (defaulted)
//
// Providers override ONE set. Those going Qt-free override the Std versions
// and delegate the Qt ones via the provided callMethodStdBridge / getMethodsStdBridge
// helpers (one-line overrides).
// ---------------------------------------------------------------------------
class LogosProviderObject {
public:
    virtual ~LogosProviderObject() = default;

    using EventCallback = std::function<void(const QString&, const QVariantList&)>;
    using UniversalEventCallback = std::function<void(const std::string&, const std::string&)>;

    // --- Qt interface (pure virtual — existing providers override these) ---
    virtual QVariant callMethod(const QString& methodName, const QVariantList& args) = 0;
    virtual bool informModuleToken(const QString& moduleName, const QString& token) = 0;
    // Returns the module's full interface as a QJsonArray: both methods and
    // events, each entry tagged with a "type" of "method" or "event" (events
    // omit returnType/isInvokable — they are void/fire-and-forget). Events ride
    // inside getMethods() ON PURPOSE: this avoids adding a separate getEvents()
    // vtable slot, so the vtable layout never shifts and old/new hosts and
    // modules stay binary-compatible. An entry with no "type" is a method (so
    // pre-events modules degrade cleanly). Callers split the list by "type"
    // (see ModuleProxy::getPluginMethods/getPluginEvents/getPluginInterface).
    virtual QJsonArray getMethods() = 0;
    virtual void setEventListener(EventCallback callback) = 0;
    virtual void init(void* apiInstance) = 0;
    virtual QString providerName() const = 0;
    virtual QString providerVersion() const = 0;

    // --- Universal interface (override these to stay Qt-free) ---
    virtual nlohmann::json callMethodStd(const std::string& methodName, const nlohmann::json& args);
    virtual std::vector<LogosMethodMetadata> getMethodsStd();
    virtual void setEventListenerStd(UniversalEventCallback callback);

protected:
    // Bridging helpers for Qt-free providers: override callMethod/getMethods
    // with a one-liner delegating to these.
    QVariant callMethodStdBridge(const QString& methodName, const QVariantList& args);
    QJsonArray getMethodsStdBridge();
    void setEventListenerStdBridge(EventCallback callback);
};

#endif // LOGOS_PROVIDER_INTERFACE_H
