#ifndef LOCAL_TRANSPORT_H
#define LOCAL_TRANSPORT_H

#include "../../logos_transport.h"
#include "../../logos_object.h"

class ModuleProxy;

class LocalTransportHost : public LogosTransportHost {
public:
    bool publishObject(const QString& name, QObject* object) override;
    void unpublishObject(const QString& name) override;
};

class LocalTransportConnection : public LogosTransportConnection {
public:
    bool connectToHost() override;
    bool isConnected() const override;
    bool reconnect() override;
    LogosObject* requestObject(const QString& objectName, int timeoutMs) override;
};

#endif // LOCAL_TRANSPORT_H
