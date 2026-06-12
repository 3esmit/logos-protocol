#include "mock_transport.h"
#include <QDebug>

// ── MockTransportHost ────────────────────────────────────────────────────────

bool MockTransportHost::publishObject(const QString& name, QObject* /*object*/)
{
    qDebug() << "MockTransportHost: publishObject (no-op)" << name;
    return true;
}

void MockTransportHost::unpublishObject(const QString& name)
{
    qDebug() << "MockTransportHost: unpublishObject (no-op)" << name;
}

// ── MockTransportConnection ──────────────────────────────────────────────────

bool MockTransportConnection::connectToHost()
{
    qDebug() << "MockTransportConnection: connectToHost (no-op, always connected)";
    return true;
}

bool MockTransportConnection::isConnected() const
{
    return true;
}

bool MockTransportConnection::reconnect()
{
    return true;
}

LogosObject* MockTransportConnection::requestObject(const QString& objectName, int /*timeoutMs*/)
{
    qDebug() << "MockTransportConnection: requestObject" << objectName;
    return new MockLogosObject(objectName);
}
