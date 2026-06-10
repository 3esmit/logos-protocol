#include "local_transport.h"
#include "../../plugin_registry.h"
#include "../../module_proxy.h"
#include <QDebug>
#include <QMetaObject>
#include <QTimer>

// ── LocalLogosObject ─────────────────────────────────────────────────────────

namespace {

class EventHelper : public QObject {
    Q_OBJECT
public:
    explicit EventHelper(QObject* parent = nullptr) : QObject(parent) {}

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
            qDebug() << "[LogosObject] Local EventHelper: dispatching event" << eventName << "to" << cbs.size() << "callback(s)";
        }
        for (const auto& cb : cbs) {
            try { cb(eventName, data); } catch (...) {}
        }
    }

private:
    QHash<QString, QList<LogosObject::EventCallback>> m_callbacks;
};

} // anonymous namespace

class LocalLogosObject : public LogosObject {
public:
    explicit LocalLogosObject(ModuleProxy* proxy)
        : m_proxy(proxy), m_helper(nullptr)
    {
        qDebug() << "[LogosObject] Created LocalLogosObject wrapping ModuleProxy" << reinterpret_cast<quintptr>(proxy);
    }

    ~LocalLogosObject() override {
        qDebug() << "[LogosObject] Destroying LocalLogosObject" << reinterpret_cast<quintptr>(m_proxy);
        delete m_helper;
    }

    QVariant callMethod(const QString& authToken,
                        const QString& methodName,
                        const QVariantList& args,
                        int /*timeoutMs*/) override
    {
        if (!m_proxy) return QVariant();
        qDebug() << "[LogosObject] LocalLogosObject::callMethod" << methodName << "args:" << args.size();
        return m_proxy->callRemoteMethod(authToken, methodName, args);
    }

    void callMethodAsync(const QString& authToken,
                         const QString& methodName,
                         const QVariantList& args,
                         int /*timeoutMs*/,
                         AsyncResultCallback callback) override
    {
        if (!callback) return;
        if (!m_proxy) {
            QTimer::singleShot(0, [callback]() { callback(QVariant()); });
            return;
        }
        ModuleProxy* proxy = m_proxy;
        QTimer::singleShot(0, [proxy, authToken, methodName, args, callback]() {
            QVariant result = proxy->callRemoteMethod(authToken, methodName, args);
            callback(result);
        });
    }

    bool informModuleToken(const QString& authToken,
                           const QString& moduleName,
                           const QString& token,
                           int /*timeoutMs*/) override
    {
        if (!m_proxy) return false;
        return m_proxy->informModuleToken(authToken, moduleName, token);
    }

    void onEvent(const QString& eventName, EventCallback callback) override
    {
        if (!m_proxy) return;

        qDebug() << "[LogosObject] LocalLogosObject::onEvent subscribing to event:" << eventName;
        if (!m_helper) {
            m_helper = new EventHelper();
            QObject::connect(m_proxy, SIGNAL(eventResponse(QString,QVariantList)),
                             m_helper, SLOT(onEventResponse(QString,QVariantList)));
            qDebug() << "[LogosObject] LocalLogosObject: connected EventHelper to ModuleProxy signals";
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
        if (!m_proxy) return;
        qDebug() << "[LogosObject] LocalLogosObject::emitEvent" << eventName << "data:" << data.size() << "items";
        QMetaObject::invokeMethod(m_proxy, "eventResponse",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, eventName),
                                  Q_ARG(QVariantList, data));
    }

    QJsonArray getMethods() override
    {
        if (!m_proxy) return QJsonArray();
        return m_proxy->getPluginMethods();
    }

    void release() override
    {
        // Local mode: we don't own the ModuleProxy, just stop using it
        disconnectEvents();
    }

    quintptr id() const override { return reinterpret_cast<quintptr>(m_proxy); }

private:
    ModuleProxy* m_proxy;
    EventHelper* m_helper;
};

// ── LocalTransportHost ───────────────────────────────────────────────────────

bool LocalTransportHost::publishObject(const QString& name, QObject* object)
{
    PluginRegistry::registerPlugin(object, name);
    qDebug() << "LocalTransportHost: Published object:" << name;
    return true;
}

void LocalTransportHost::unpublishObject(const QString& name)
{
    if (!name.isEmpty()) {
        PluginRegistry::unregisterPlugin(name);
        qDebug() << "LocalTransportHost: Unpublished object:" << name;
    }
}

// ── LocalTransportConnection ─────────────────────────────────────────────────

bool LocalTransportConnection::connectToHost()
{
    qDebug() << "LocalTransportConnection: Local mode - no connection needed";
    return true;
}

bool LocalTransportConnection::isConnected() const
{
    return true;
}

bool LocalTransportConnection::reconnect()
{
    return true;
}

LogosObject* LocalTransportConnection::requestObject(const QString& objectName, int /*timeoutMs*/)
{
    QObject* plugin = PluginRegistry::getPlugin<QObject>(objectName);
    if (!plugin) {
        qWarning() << "LocalTransportConnection: Plugin not found in registry:" << objectName;
        return nullptr;
    }

    ModuleProxy* proxy = qobject_cast<ModuleProxy*>(plugin);
    if (!proxy) {
        qWarning() << "LocalTransportConnection: Plugin is not a ModuleProxy:" << objectName;
        return nullptr;
    }

    qDebug() << "[LogosObject] LocalTransportConnection: returning LocalLogosObject for:" << objectName;
    return new LocalLogosObject(proxy);
}

#include "local_transport.moc"
