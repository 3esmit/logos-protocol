#ifndef MOCK_TRANSPORT_H
#define MOCK_TRANSPORT_H

#include "../../logos_transport.h"
#include "../../logos_object.h"
#include "mock_store.h"
#include <QString>
#include <QList>
#include <QTimer>

/**
 * @brief LogosObject implementation for mock mode.
 *
 * Stores the module name and delegates callMethod to MockStore.
 * Event operations are no-ops in mock mode.
 */
class MockLogosObject : public LogosObject {
public:
    explicit MockLogosObject(const QString& moduleName)
        : m_moduleName(moduleName) {}

    const QString& moduleName() const { return m_moduleName; }

    QVariant callMethod(const QString& /*authToken*/,
                        const QString& methodName,
                        const QVariantList& args,
                        int /*timeoutMs*/) override
    {
        return MockStore::instance().recordAndReturn(m_moduleName, methodName, args);
    }

    void callMethodAsync(const QString& /*authToken*/,
                         const QString& methodName,
                         const QVariantList& args,
                         int /*timeoutMs*/,
                         AsyncResultCallback callback) override
    {
        if (!callback) return;
        QString mod = m_moduleName;
        QTimer::singleShot(0, [mod, methodName, args, callback]() {
            QVariant result = MockStore::instance().recordAndReturn(mod, methodName, args);
            callback(result);
        });
    }

    bool informModuleToken(const QString& /*authToken*/,
                           const QString& moduleName,
                           const QString& /*token*/,
                           int /*timeoutMs*/) override
    {
        Q_UNUSED(moduleName)
        return true;
    }

    void onEvent(const QString& /*eventName*/, EventCallback /*callback*/) override {}
    void disconnectEvents() override {}
    void emitEvent(const QString& /*eventName*/, const QVariantList& /*data*/) override {}

    QJsonArray getMethods() override { return QJsonArray(); }

    void release() override
    {
        MockStore::instance().incrementMockObjectReleaseProbeIfSet();
        delete this;
    }

    quintptr id() const override { return reinterpret_cast<quintptr>(this); }

private:
    QString m_moduleName;
};

/**
 * @brief No-op provider-side transport for mock mode.
 *
 * publishObject / unpublishObject succeed silently; there is no real
 * IPC endpoint and no object needs to be made available.
 */
class MockTransportHost : public LogosTransportHost {
public:
    bool publishObject(const QString& name, QObject* object) override;
    void unpublishObject(const QString& name) override;
};

/**
 * @brief Consumer-side transport for mock mode.
 *
 * requestObject returns a MockLogosObject tagged with the module name.
 */
class MockTransportConnection : public LogosTransportConnection {
public:
    bool connectToHost() override;
    bool isConnected() const override;
    bool reconnect() override;
    LogosObject* requestObject(const QString& objectName, int timeoutMs) override;
};

#endif // MOCK_TRANSPORT_H
