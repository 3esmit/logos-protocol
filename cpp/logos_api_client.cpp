#include "logos_api_client.h"
#include "logos_api_consumer.h"
#include "logos_object.h"
#include "logos_types.h"
#include "logos_json_convert.h"
#include "logos_thread_marshal.h"
#include "token_manager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QMetaObject>
#include <QMetaType>
#include <QPointer>
#include <string>

using logos::qvariantToNlohmann;
using logos::nlohmannArgsToQVariantList;

LogosAPIClient::LogosAPIClient(const QString& module_to_talk_to,
                               const QString& origin_module,
                               TokenManager* token_manager,
                               const LogosTransportConfig& target_transport,
                               const LogosTransportConfig& capability_transport,
                               QObject *parent)
    : QObject(parent)
    , m_consumer(new LogosAPIConsumer(module_to_talk_to, origin_module,
                                      token_manager, target_transport, this))
    , m_token_manager(token_manager)
    , m_origin_module(origin_module)
    // Pre-build the capability_module consumer once. We skip it for
    // the capability_module client itself — the auto-`requestModule`
    // path is gated by `objectName != "capability_module"` so we'd
    // never use it, and constructing one would be a redundant
    // self-connection. Init-list order matches the declaration order
    // in the header — `m_capability_consumer` is appended at the end
    // for ABI stability (see header comment).
    , m_capability_consumer(module_to_talk_to == QStringLiteral("capability_module")
        ? nullptr
        : new LogosAPIConsumer(QStringLiteral("capability_module"),
                                origin_module, token_manager,
                                capability_transport, this))
{
}

LogosAPIClient::LogosAPIClient(const QString& module_to_talk_to,
                               const QString& origin_module,
                               TokenManager* token_manager,
                               QObject *parent)
    : LogosAPIClient(module_to_talk_to, origin_module, token_manager,
                     LogosTransportConfigGlobal::getDefault(),
                     LogosTransportConfigGlobal::getDefault(), parent)
{
}

LogosAPIClient::~LogosAPIClient()
{
}

LogosObject* LogosAPIClient::requestObject(const QString& objectName, Timeout timeout)
{
    // Marshal to the owner thread: the replica is acquired and lives there.
    return logos::runOnOwnerThread(this, [&]() -> LogosObject* {
        return m_consumer->requestObject(objectName, timeout);
    });
}

bool LogosAPIClient::isConnected() const
{
    return m_consumer->isConnected();
}

QString LogosAPIClient::registryUrl() const
{
    return m_consumer->registryUrl();
}

bool LogosAPIClient::reconnect()
{
    return m_consumer->reconnect();
}

QVariant LogosAPIClient::invokeRemoteMethod(const QString& objectName, const QString& methodName,
                                   const QVariantList& args, Timeout timeout)
{
    // Marshal the whole operation (capability/token fetch + the call) onto the
    // owner thread so a worker thread (e.g. an HTTP handler) can call other
    // modules. Same-thread callers run directly. See logos_thread_marshal.h.
    return logos::runOnOwnerThread(this, [&]() -> QVariant {
    qDebug() << "LogosAPIClient: invoking remote method" << objectName << methodName << "args_count:" << args.size();

    QString token = getToken(objectName);

    if (token.isEmpty() && objectName != "capability_module" && m_capability_consumer) {
        qDebug() << "LogosAPIClient: calling requestModule for" << objectName;
        QString capabilityToken = getToken("capability_module");
        QVariant result = m_capability_consumer->invokeRemoteMethod(
            capabilityToken, "capability_module", "requestModule",
            QVariantList() << m_origin_module << objectName, timeout);
        qDebug() << "LogosAPIClient: requestModule result for" << objectName << ":" << result.toString();
        token = result.toString();
    }

    return m_consumer->invokeRemoteMethod(token, objectName, methodName, args, timeout);
    });
}

QVariant LogosAPIClient::invokeRemoteMethod(const QString& objectName, const QString& methodName,
                                   const QVariant& arg, Timeout timeout)
{
    return invokeRemoteMethod(objectName, methodName, QVariantList() << arg, timeout);
}

QVariant LogosAPIClient::invokeRemoteMethod(const QString& objectName, const QString& methodName,
                                   const QVariant& arg1, const QVariant& arg2, Timeout timeout)
{
    return invokeRemoteMethod(objectName, methodName, QVariantList() << arg1 << arg2, timeout);
}

QVariant LogosAPIClient::invokeRemoteMethod(const QString& objectName, const QString& methodName,
                                   const QVariant& arg1, const QVariant& arg2, const QVariant& arg3, Timeout timeout)
{
    return invokeRemoteMethod(objectName, methodName, QVariantList() << arg1 << arg2 << arg3, timeout);
}

QVariant LogosAPIClient::invokeRemoteMethod(const QString& objectName, const QString& methodName,
                                   const QVariant& arg1, const QVariant& arg2, const QVariant& arg3,
                                   const QVariant& arg4, Timeout timeout)
{
    return invokeRemoteMethod(objectName, methodName, QVariantList() << arg1 << arg2 << arg3 << arg4, timeout);
}

QVariant LogosAPIClient::invokeRemoteMethod(const QString& objectName, const QString& methodName,
                                   const QVariant& arg1, const QVariant& arg2, const QVariant& arg3,
                                   const QVariant& arg4, const QVariant& arg5, Timeout timeout)
{
    return invokeRemoteMethod(objectName, methodName, QVariantList() << arg1 << arg2 << arg3 << arg4 << arg5, timeout);
}

void LogosAPIClient::invokeRemoteMethodAsync(const QString& objectName, const QString& methodName,
                                              const QVariantList& args, AsyncResultCallback callback,
                                              Timeout timeout)
{
    if (!callback) return;

    // The async path acquires a replica too, so it must also run on the owner
    // thread. Unlike the sync path we post non-blocking (QueuedConnection): the
    // worker caller returns immediately and the result callback fires on the
    // owner thread when the reply arrives.
    if (QThread::currentThread() != this->thread()) {
        QMetaObject::invokeMethod(this,
            [this, objectName, methodName, args,
             callback = std::move(callback), timeout]() mutable {
                invokeRemoteMethodAsync(objectName, methodName, args,
                                        std::move(callback), timeout);
            },
            Qt::QueuedConnection);
        return;
    }

    QString token = getToken(objectName);

    if (token.isEmpty() && objectName != "capability_module" && m_capability_consumer) {
        // Async-chain: dispatch the requestModule call asynchronously,
        // and only fire the real method's invokeRemoteMethodAsync from
        // its callback. The previous version called `requestModule`
        // synchronously here, which made the "async" entry point
        // block its caller for the full requestModule round-trip
        // (a real perf hit when capability_module has any latency).
        //
        // Lifetime: the inner callback captures m_consumer through a
        // QPointer guard. If the LogosAPIClient (and thus its
        // QObject-parented m_consumer) is destroyed while the
        // requestModule round-trip is still in flight, the QPointer
        // goes null and the inner dispatch is suppressed instead of
        // dereferencing dangling memory.
        const QString capabilityToken = getToken("capability_module");
        const QString origin = m_origin_module;
        QPointer<LogosAPIConsumer> consumer = m_consumer;
        auto outerCallback = std::move(callback);
        m_capability_consumer->invokeRemoteMethodAsync(
            capabilityToken,
            QStringLiteral("capability_module"),
            QStringLiteral("requestModule"),
            QVariantList() << origin << objectName,
            [consumer, objectName, methodName, args, timeout,
             outerCallback = std::move(outerCallback)]
            (const QVariant& tokenResult) mutable {
                if (!consumer) {
                    // Client was destroyed mid-flight. Honour the
                    // contract by firing the outer callback with an
                    // invalid QVariant so callers don't deadlock
                    // waiting for a result that'll never come.
                    if (outerCallback) outerCallback(QVariant{});
                    return;
                }
                consumer->invokeRemoteMethodAsync(
                    tokenResult.toString(),
                    objectName, methodName, args,
                    std::move(outerCallback), timeout);
            },
            timeout);
        return;
    }

    m_consumer->invokeRemoteMethodAsync(token, objectName, methodName, args, std::move(callback), timeout);
}

void LogosAPIClient::invokeRemoteMethodAsync(const QString& objectName, const QString& methodName,
                                              const QVariant& arg, AsyncResultCallback callback,
                                              Timeout timeout)
{
    invokeRemoteMethodAsync(objectName, methodName, QVariantList() << arg, std::move(callback), timeout);
}

void LogosAPIClient::invokeRemoteMethodAsync(const QString& objectName, const QString& methodName,
                                              const QVariant& arg1, const QVariant& arg2,
                                              AsyncResultCallback callback, Timeout timeout)
{
    invokeRemoteMethodAsync(objectName, methodName, QVariantList() << arg1 << arg2, std::move(callback), timeout);
}

void LogosAPIClient::invokeRemoteMethodAsync(const QString& objectName, const QString& methodName,
                                              const QVariant& arg1, const QVariant& arg2, const QVariant& arg3,
                                              AsyncResultCallback callback, Timeout timeout)
{
    invokeRemoteMethodAsync(objectName, methodName, QVariantList() << arg1 << arg2 << arg3, std::move(callback), timeout);
}

void LogosAPIClient::invokeRemoteMethodAsync(const QString& objectName, const QString& methodName,
                                              const QVariant& arg1, const QVariant& arg2, const QVariant& arg3,
                                              const QVariant& arg4, AsyncResultCallback callback,
                                              Timeout timeout)
{
    invokeRemoteMethodAsync(objectName, methodName, QVariantList() << arg1 << arg2 << arg3 << arg4, std::move(callback), timeout);
}

void LogosAPIClient::invokeRemoteMethodAsync(const QString& objectName, const QString& methodName,
                                              const QVariant& arg1, const QVariant& arg2, const QVariant& arg3,
                                              const QVariant& arg4, const QVariant& arg5,
                                              AsyncResultCallback callback, Timeout timeout)
{
    invokeRemoteMethodAsync(objectName, methodName, QVariantList() << arg1 << arg2 << arg3 << arg4 << arg5, std::move(callback), timeout);
}

void LogosAPIClient::onEvent(LogosObject* originObject, const QString& eventName, std::function<void(const QString&, const QVariantList&)> callback)
{
    // Marshal to the owner thread: event registration touches the replica.
    logos::runOnOwnerThread(this, [&]() {
        m_consumer->onEvent(originObject, eventName, std::move(callback));
    });
}

void LogosAPIClient::onEventResponse(LogosObject* object, const QString& eventName, const QVariantList& data)
{
    qDebug() << "[LogosObject] LogosAPIClient::onEventResponse" << eventName << "-> LogosObject::emitEvent";

    if (eventName.isEmpty()) {
        qWarning() << "LogosAPIClient: Event name cannot be empty";
        return;
    }

    if (!object) {
        qWarning() << "LogosAPIClient: Cannot emit event on null object";
        return;
    }

    object->emitEvent(eventName, data);
}

void LogosAPIClient::onEventResponse(QObject* object, const QString& eventName, const QVariantList& data)
{
    qDebug() << "[LogosObject] LogosAPIClient::onEventResponse (QObject* compat)" << eventName;

    if (eventName.isEmpty()) {
        qWarning() << "LogosAPIClient: Event name cannot be empty";
        return;
    }

    if (!object) {
        qWarning() << "LogosAPIClient: Cannot emit event on null QObject";
        return;
    }

    QMetaObject::invokeMethod(object, "eventResponse",
                              Qt::DirectConnection,
                              Q_ARG(QString, eventName),
                              Q_ARG(QVariantList, data));
}

bool LogosAPIClient::informModuleToken(const QString& authToken, const QString& moduleName, const QString& token)
{
    return m_consumer->informModuleToken(authToken, moduleName, token);
}

bool LogosAPIClient::informModuleToken(const std::string& authToken, const std::string& moduleName, const std::string& token)
{
    return informModuleToken(QString::fromStdString(authToken),
                             QString::fromStdString(moduleName),
                             QString::fromStdString(token));
}

bool LogosAPIClient::informModuleToken_module(const QString& authToken, const QString& originModule, const QString& moduleName, const QString& token)
{
    return m_consumer->informModuleToken_module(authToken, originModule, moduleName, token);
}

TokenManager* LogosAPIClient::getTokenManager() const
{
    return m_token_manager;
}

QString LogosAPIClient::getToken(const QString& module_name)
{
    qDebug() << "LogosAPIClient: getToken for module:" << module_name;

    QString token = m_token_manager->getToken(module_name);
    if (!token.isEmpty()) {
        qDebug() << "LogosAPIClient: Found token for module:" << module_name;
        return token;
    }

    qDebug() << "LogosAPIClient: No token found for module:" << module_name;
    return "";
}

// ---------------------------------------------------------------------------
// nlohmann::json overloads
// ---------------------------------------------------------------------------

nlohmann::json LogosAPIClient::invokeRemoteMethod(const std::string& objectName,
                                                   const std::string& methodName,
                                                   const nlohmann::json& args,
                                                   Timeout timeout)
{
    QVariantList qArgs = nlohmannArgsToQVariantList(args);
    QVariant result = invokeRemoteMethod(
        QString::fromStdString(objectName),
        QString::fromStdString(methodName),
        qArgs, timeout);
    return qvariantToNlohmann(result);
}

void LogosAPIClient::onEvent(LogosObject* originObject, const std::string& eventName,
                              std::function<void(const std::string&, const nlohmann::json&)> callback)
{
    onEvent(originObject, QString::fromStdString(eventName),
        [cb = std::move(callback)](const QString& name, const QVariantList& data) {
            nlohmann::json jData = nlohmann::json::array();
            for (const QVariant& v : data)
                jData.push_back(qvariantToNlohmann(v));
            cb(name.toStdString(), jData);
        });
}
