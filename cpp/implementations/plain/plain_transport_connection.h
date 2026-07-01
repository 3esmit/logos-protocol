#ifndef LOGOS_PLAIN_TRANSPORT_CONNECTION_H
#define LOGOS_PLAIN_TRANSPORT_CONNECTION_H

#include "logos_transport.h"
#include "logos_transport_config.h"

#include "rpc_connection.h"

#include <memory>
#include <string>

namespace logos::plain {

// -----------------------------------------------------------------------------
// PlainTransportConnection — consumer-side LogosTransportConnection.
//
// connectToHost() opens a TCP (or TLS) socket to the daemon's endpoint from
// the LogosTransportConfig and starts the RPC read loop. requestObject()
// returns a PlainLogosObject sharing that connection.
// -----------------------------------------------------------------------------
class PlainTransportConnection : public LogosTransportConnection {
public:
    explicit PlainTransportConnection(LogosTransportConfig cfg);
    ~PlainTransportConnection() override;

    bool connectToHost() override;
    bool isConnected() const override;
    bool reconnect() override;
    LogosObject* requestObject(const QString& objectName, int timeoutMs) override;
    QString endpointUrl(const QString& instanceId,
                        const QString& moduleName) override;

    // The raw, Qt-free RPC connection opened by connectToHost() (null until
    // then). Exposed so the Qt-free consumer (LpPlainClient) can drive the wire
    // directly with std/json instead of going through the QVariant-typed
    // PlainLogosObject. Ownership stays here — the caller keeps this
    // PlainTransportConnection alive for as long as it uses the connection.
    std::shared_ptr<RpcConnectionBase> connection() const { return m_conn; }

private:
    LogosTransportConfig               m_cfg;
    std::shared_ptr<RpcConnectionBase> m_conn;
    bool                               m_connected = false;
};

} // namespace logos::plain

#endif // LOGOS_PLAIN_TRANSPORT_CONNECTION_H
