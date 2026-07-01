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

class LpProviderDispatch;

// -----------------------------------------------------------------------------
// PlainTransportHost — publishes providers over plain-C++ TCP or TCP+SSL.
//
// Owns an RpcServer (TCP or SSL variant), an IWireCodec (per config), and a
// registry mapping object name → published provider. It serves two kinds of
// provider:
//   * a Qt ModuleProxy (QObject), via publishObject(name, QObject*) — dispatch
//     is queued to the proxy's thread (needs a Qt event loop; the QtRO path).
//   * a Qt-free LpProviderDispatch, via publishObjectStd(name, dispatch) —
//     dispatch runs DIRECTLY on the Asio I/O thread (no QObject, no loop).
// Events fan out to every subscribed RPC connection either way.
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

    // Qt-free provider publish: serves an LpProviderDispatch directly on the
    // Asio I/O thread (no QObject/ModuleProxy, no Qt event loop). The dispatch
    // is owned by the caller and must outlive the publication (unpublish clears
    // the host's event sink into it). Returns false if a name is already published.
    bool publishObjectStd(const QString& name, LpProviderDispatch* dispatch);

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
        // Exactly one of these is set. `object` = the legacy Qt ModuleProxy
        // path; `stdDispatch` = the Qt-free LpProviderDispatch path.
        QObject* object = nullptr;
        LpProviderDispatch* stdDispatch = nullptr;
        // Tracked event subscribers per event name (including "" wildcard).
        std::map<std::string, std::map<const void*, EventSink>> sinksByEvent;
        QMetaObject::Connection eventConn; // only used for the QObject path
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
