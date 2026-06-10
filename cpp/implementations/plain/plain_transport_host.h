#ifndef LOGOS_PLAIN_TRANSPORT_HOST_H
#define LOGOS_PLAIN_TRANSPORT_HOST_H

#include "logos_transport.h"
#include "logos_transport_config.h"

#include "incoming_call_handler.h"
#include "rpc_server.h"

#include <QObject>

#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace logos::plain {

// -----------------------------------------------------------------------------
// PlainTransportHost — publishes QObjects over plain-C++ TCP or TCP+SSL.
//
// Owns an RpcServer (TCP or SSL variant), an IWireCodec (per config), and
// a registry mapping object name → published QObject. For each object it
// hooks into the QObject's `eventResponse(QString, QVariantList)` Qt signal
// so emitted events fan out to every subscribed RPC connection.
// -----------------------------------------------------------------------------
class PlainTransportHost
    : public LogosTransportHost
    , public IncomingCallHandler
{
public:
    explicit PlainTransportHost(LogosTransportConfig cfg);
    ~PlainTransportHost() override;

    // LogosTransportHost
    bool publishObject(const QString& name, QObject* object) override;
    void unpublishObject(const QString& name) override;
    QString bindUrl(const QString& instanceId,
                    const QString& moduleName) override;

    // Reports the bound endpoint URL ("tcp://host:port") once start() has
    // succeeded. Empty string until then.
    QString endpoint() const;

    // Must be called once after constructing + publishing is wired up,
    // so the acceptor starts listening. Idempotent.
    bool start();

    // IncomingCallHandler
    void onCall(const CallMessage& req, CallReply reply) override;
    void onMethods(const MethodsMessage& req, MethodsReply reply) override;
    void onSubscribe(const SubscribeMessage& req, EventSink sink,
                     const void* connectionId) override;
    void onUnsubscribe(const UnsubscribeMessage& req,
                       const void* connectionId) override;
    void onConnectionClosed(const void* connectionId) override;
    void onToken(const TokenMessage& req) override;

    // Internal: deliver an event emitted by the wrapped QObject to every
    // subscribed connection (both matching-name and wildcard subscribers).
    void fanOutEvent(const std::string& name, EventMessage msg);

private:
    struct Published {
        QObject* object = nullptr;
        // Tracked event subscribers per event name (including "" wildcard).
        std::map<std::string, std::map<const void*, EventSink>> sinksByEvent;
        QMetaObject::Connection eventConn;
    };

    LogosTransportConfig                    m_cfg;
    std::shared_ptr<RpcServerTcp>           m_tcp;
    std::shared_ptr<RpcServerSsl>           m_ssl;
    uint16_t                                m_boundPort = 0;

    mutable std::mutex                      m_mu;
    std::map<std::string, Published>        m_published;
    bool                                    m_started = false;
};

} // namespace logos::plain

#endif // LOGOS_PLAIN_TRANSPORT_HOST_H
