#include "remote_transport.h"
#include <QRemoteObjectRegistryHost>
#include <QRemoteObjectNode>
#include <QRemoteObjectReplica>
#include <QRemoteObjectPendingCall>
#include <QRemoteObjectPendingCallWatcher>
#include <QTimer>
#include <QDebug>
#include <QUrl>
#include <QMetaObject>
#include <QTime>
#include <QJsonArray>

// ── RemoteLogosObject ────────────────────────────────────────────────────────

namespace {

class RemoteEventHelper : public QObject {
    Q_OBJECT
public:
    explicit RemoteEventHelper(QObject* parent = nullptr) : QObject(parent) {}

    void addCallback(const QString& eventName, LogosObject::EventCallback cb) {
        m_callbacks[eventName].append(std::move(cb));
    }

public slots:
    void onEventResponse(const QString& eventName, const QVariantList& data) {
        // Dispatch to callbacks registered for this specific event name,
        // plus any wildcard subscribers (callbacks registered with an
        // empty event name, meaning "receive every event").
        auto cbs = m_callbacks.value(eventName);
        cbs.append(m_callbacks.value(QString()));
        if (!cbs.isEmpty()) {
            qDebug() << "[LogosObject] Remote EventHelper: dispatching event" << eventName << "to" << cbs.size() << "callback(s) (via IPC)";
        }
        for (const auto& cb : cbs) {
            try { cb(eventName, data); } catch (...) {}
        }
    }

private:
    QHash<QString, QList<LogosObject::EventCallback>> m_callbacks;
};

} // anonymous namespace

class RemoteLogosObject : public LogosObject {
public:
    explicit RemoteLogosObject(QObject* replica)
        : m_replica(replica), m_helper(nullptr)
    {
        qDebug() << "[LogosObject] Created RemoteLogosObject wrapping QRemoteObjectReplica" << reinterpret_cast<quintptr>(replica);
    }

    ~RemoteLogosObject() override {
        qDebug() << "[LogosObject] Destroying RemoteLogosObject" << reinterpret_cast<quintptr>(m_replica);
        delete m_helper;
    }

    QVariant callMethod(const QString& authToken,
                        const QString& methodName,
                        const QVariantList& args,
                        int timeoutMs) override
    {
        if (!m_replica) {
            qWarning() << "RemoteLogosObject: Cannot call method on null replica";
            return QVariant();
        }
        qDebug() << "[LogosObject] RemoteLogosObject::callMethod" << methodName << "args:" << args.size();

        QRemoteObjectPendingCall pendingCall;
        bool success = QMetaObject::invokeMethod(
            m_replica,
            "callRemoteMethod",
            Qt::DirectConnection,
            Q_RETURN_ARG(QRemoteObjectPendingCall, pendingCall),
            Q_ARG(QString, authToken),
            Q_ARG(QString, methodName),
            Q_ARG(QVariantList, args)
        );

        if (!success) {
            qWarning() << "RemoteLogosObject: Failed to invoke callRemoteMethod on replica";
            return QVariant();
        }

        pendingCall.waitForFinished(timeoutMs);

        if (!pendingCall.isFinished() || pendingCall.error() != QRemoteObjectPendingCall::NoError) {
            qWarning() << "RemoteLogosObject: callRemoteMethod failed or timed out:" << pendingCall.error();
            return QVariant();
        }

        return pendingCall.returnValue();
    }

    void callMethodAsync(const QString& authToken,
                         const QString& methodName,
                         const QVariantList& args,
                         int timeoutMs,
                         AsyncResultCallback callback) override
    {
        if (!callback) return;
        if (!m_replica) {
            QTimer::singleShot(0, [callback]() { callback(QVariant()); });
            return;
        }

        qDebug() << "[LogosObject] RemoteLogosObject::callMethodAsync" << methodName << "args:" << args.size();

        QRemoteObjectPendingCall pendingCall;
        bool success = QMetaObject::invokeMethod(
            m_replica,
            "callRemoteMethod",
            Qt::DirectConnection,
            Q_RETURN_ARG(QRemoteObjectPendingCall, pendingCall),
            Q_ARG(QString, authToken),
            Q_ARG(QString, methodName),
            Q_ARG(QVariantList, args)
        );

        if (!success) {
            qWarning() << "RemoteLogosObject: Failed to invoke callRemoteMethod on replica (async)";
            QTimer::singleShot(0, [callback]() { callback(QVariant()); });
            return;
        }

        auto* watcher = new QRemoteObjectPendingCallWatcher(pendingCall);

        // Timeout timer -- parented to the watcher so it is auto-deleted
        // when the watcher is destroyed, preventing late timeout callbacks.
        auto* timer = new QTimer(watcher);
        timer->setSingleShot(true);

        // Success handler -- delivers result on the consumer's thread
        QObject::connect(watcher, &QRemoteObjectPendingCallWatcher::finished,
                         watcher, [callback, timer](QRemoteObjectPendingCallWatcher* w) {
            timer->stop(); // cancel timeout
            QVariant result;
            if (w->error() == QRemoteObjectPendingCall::NoError) {
                result = w->returnValue();
            } else {
                qWarning() << "RemoteLogosObject: async callMethod error:" << w->error();
            }
            callback(result);
            w->deleteLater();
        }, Qt::QueuedConnection);

        // Timeout handler -- stops the watcher and delivers empty result
        QObject::connect(timer, &QTimer::timeout, watcher, [watcher, callback]() {
            qWarning() << "RemoteLogosObject: async callMethod timed out";
            callback(QVariant());
            watcher->deleteLater(); // also destroys the timer (child)
        });

        timer->start(timeoutMs);
    }

    bool informModuleToken(const QString& authToken,
                           const QString& moduleName,
                           const QString& token,
                           int timeoutMs) override
    {
        if (!m_replica) {
            qWarning() << "RemoteLogosObject: Cannot call informModuleToken on null replica";
            return false;
        }

        QRemoteObjectPendingCall pendingCall;
        bool success = QMetaObject::invokeMethod(
            m_replica,
            "informModuleToken",
            Qt::DirectConnection,
            Q_RETURN_ARG(QRemoteObjectPendingCall, pendingCall),
            Q_ARG(QString, authToken),
            Q_ARG(QString, moduleName),
            Q_ARG(QString, token)
        );

        if (!success) {
            qWarning() << "RemoteLogosObject: Failed to invoke informModuleToken on replica";
            return false;
        }

        pendingCall.waitForFinished(timeoutMs);

        if (!pendingCall.isFinished() || pendingCall.error() != QRemoteObjectPendingCall::NoError) {
            qWarning() << "RemoteLogosObject: informModuleToken failed or timed out:" << pendingCall.error();
            return false;
        }

        return pendingCall.returnValue().toBool();
    }

    void onEvent(const QString& eventName, EventCallback callback) override
    {
        if (!m_replica) return;

        qDebug() << "[LogosObject] RemoteLogosObject::onEvent subscribing to event:" << eventName;
        if (!m_helper) {
            m_helper = new RemoteEventHelper();
            QObject::connect(m_replica, SIGNAL(eventResponse(QString,QVariantList)),
                             m_helper, SLOT(onEventResponse(QString,QVariantList)));
            qDebug() << "[LogosObject] RemoteLogosObject: connected EventHelper to QRemoteObjectReplica signals (IPC)";
        }
        m_helper->addCallback(eventName, std::move(callback));
    }

    void disconnectEvents() override
    {
        delete m_helper;
        m_helper = nullptr;
    }

    void emitEvent(const QString& eventName, const QVariantList& data) override
    {
        if (!m_replica) return;
        qDebug() << "[LogosObject] RemoteLogosObject::emitEvent" << eventName << "data:" << data.size() << "items (via IPC)";
        QMetaObject::invokeMethod(m_replica, "eventResponse",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, eventName),
                                  Q_ARG(QVariantList, data));
    }

    QJsonArray getMethods() override
    {
        // Remote introspection not implemented — callers should use
        // the local module inspection tools (lm) instead.
        return QJsonArray();
    }

    void release() override
    {
        disconnectEvents();
        delete m_replica;
        m_replica = nullptr;
        delete this;
    }

    quintptr id() const override { return reinterpret_cast<quintptr>(m_replica); }

private:
    QObject* m_replica;
    RemoteEventHelper* m_helper;
};

// ── RemoteTransportHost ──────────────────────────────────────────────────────

RemoteTransportHost::RemoteTransportHost(const QString& registryUrl)
    : m_registryHost(nullptr)
    , m_registryUrl(registryUrl)
{
}

RemoteTransportHost::~RemoteTransportHost()
{
    delete m_registryHost;
}

bool RemoteTransportHost::publishObject(const QString& name, QObject* object)
{
    if (!m_registryHost) {
        m_registryHost = new QRemoteObjectRegistryHost(QUrl(m_registryUrl));
        if (!m_registryHost) {
            qCritical() << "RemoteTransportHost: Failed to create registry host";
            return false;
        }
        qDebug() << "RemoteTransportHost: Created registry host with URL:" << m_registryUrl;
    }

    bool success = m_registryHost->enableRemoting(object, name);
    if (success) {
        qDebug() << "RemoteTransportHost: Published object:" << name;
    } else {
        qCritical() << "RemoteTransportHost: Failed to publish object:" << name;
    }
    return success;
}

void RemoteTransportHost::unpublishObject(const QString& /*name*/)
{
}

// ── RemoteTransportConnection ────────────────────────────────────────────────

RemoteTransportConnection::RemoteTransportConnection(const QString& registryUrl)
    : m_node(new QRemoteObjectNode())
    , m_registryUrl(registryUrl)
    , m_connected(false)
{
}

RemoteTransportConnection::~RemoteTransportConnection()
{
    delete m_node;
}

bool RemoteTransportConnection::connectToHost()
{
    return connectToRegistry();
}

bool RemoteTransportConnection::isConnected() const
{
    return m_connected;
}

bool RemoteTransportConnection::reconnect()
{
    qDebug() << "RemoteTransportConnection: Attempting to reconnect to registry:" << m_registryUrl;

    if (m_connected) {
        delete m_node;
        m_node = new QRemoteObjectNode();
        m_connected = false;
    }

    return connectToRegistry();
}

bool RemoteTransportConnection::connectToRegistry()
{
    if (!m_node) {
        qWarning() << "RemoteTransportConnection: Remote object node is null";
        return false;
    }

    if (m_registryUrl.isEmpty()) {
        qWarning() << "RemoteTransportConnection: Registry URL is empty";
        return false;
    }

    qDebug() << "RemoteTransportConnection: Connecting to registry:" << m_registryUrl
             << "at" << QTime::currentTime().toString("hh:mm:ss.zzz");

    QUrl url(m_registryUrl);
    bool success = m_node->connectToNode(url);

    if (success) {
        m_connected = true;
        qDebug() << "RemoteTransportConnection: Successfully connected to registry:" << m_registryUrl;
    } else {
        m_connected = false;
        qWarning() << "RemoteTransportConnection: Failed to connect to registry:" << m_registryUrl;
    }
    qDebug() << "RemoteTransportConnection: Connected to registry at"
             << QTime::currentTime().toString("hh:mm:ss.zzz");

    return m_connected;
}

LogosObject* RemoteTransportConnection::requestObject(const QString& objectName, int timeoutMs)
{
    if (!m_connected) {
        qWarning() << "RemoteTransportConnection: Not connected. Cannot request object:" << objectName;
        return nullptr;
    }

    qDebug() << "RemoteTransportConnection: Requesting object:" << objectName
             << "at" << QTime::currentTime().toString("hh:mm:ss.zzz");

    QRemoteObjectReplica* replica = m_node->acquireDynamic(objectName);
    if (!replica) {
        qWarning() << "RemoteTransportConnection: Failed to acquire replica for:" << objectName;
        return nullptr;
    }

    if (!replica->waitForSource(timeoutMs)) {
        qWarning() << "RemoteTransportConnection: Timeout waiting for replica:" << objectName;
        delete replica;
        return nullptr;
    }

    qDebug() << "[LogosObject] RemoteTransportConnection: returning RemoteLogosObject for:" << objectName;
    return new RemoteLogosObject(replica);
}

#include "remote_transport.moc"
