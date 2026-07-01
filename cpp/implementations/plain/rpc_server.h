#ifndef LOGOS_PLAIN_RPC_SERVER_H
#define LOGOS_PLAIN_RPC_SERVER_H

#include "incoming_call_handler.h"
#include "rpc_connection.h"
#include "wire_codec.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace logos::plain {

// -----------------------------------------------------------------------------
// RpcServer (TCP) — accepts TCP connections, wraps each in an
// RpcConnection, keeps them alive until they drop.
//
// Every accepted connection uses the same shared IncomingCallHandler so
// the provider layer can dispatch regardless of which client is talking.
// The server doesn't multiplex objects by itself; it's the handler's job
// to look up the target object for each incoming CallMessage.
// -----------------------------------------------------------------------------

using TcpStream = boost::asio::ip::tcp::socket;
using TcpConnection = RpcConnection<TcpStream>;

class RpcServerTcp : public std::enable_shared_from_this<RpcServerTcp> {
public:
    RpcServerTcp(boost::asio::io_context& ioc,
                 const std::string& host,
                 uint16_t port,
                 std::shared_ptr<IWireCodec> codec,
                 IncomingCallHandler* handler);

    // Start accepting. Returns false if bind fails.
    bool start();

    // Actual bound port (useful when the caller requested port=0).
    uint16_t boundPort() const { return m_boundPort; }

    void stop();

private:
    void doAccept();

    boost::asio::ip::tcp::acceptor    m_acceptor;
    std::shared_ptr<IWireCodec>       m_codec;
    IncomingCallHandler*              m_handler;
    std::string                       m_host;
    uint16_t                          m_port;
    uint16_t                          m_boundPort = 0;

    std::mutex                                    m_mu;
    std::vector<std::shared_ptr<TcpConnection>>   m_conns;
    bool                                          m_stopped = false;
};

// -----------------------------------------------------------------------------
// RpcServer (TLS) — same as TCP but wraps every accepted socket in an
// asio::ssl::stream and completes the handshake before spinning up the
// RpcConnection.
// -----------------------------------------------------------------------------

using SslStream = boost::asio::ssl::stream<boost::asio::ip::tcp::socket>;
using SslConnection = RpcConnection<SslStream>;

class RpcServerSsl : public std::enable_shared_from_this<RpcServerSsl> {
public:
    RpcServerSsl(boost::asio::io_context& ioc,
                 const std::string& host,
                 uint16_t port,
                 boost::asio::ssl::context sslCtx,
                 std::shared_ptr<IWireCodec> codec,
                 IncomingCallHandler* handler);

    bool start();
    uint16_t boundPort() const { return m_boundPort; }
    void stop();

private:
    void doAccept();

    boost::asio::ip::tcp::acceptor   m_acceptor;
    boost::asio::ssl::context        m_sslCtx;
    std::shared_ptr<IWireCodec>      m_codec;
    IncomingCallHandler*             m_handler;
    std::string                      m_host;
    uint16_t                         m_port;
    uint16_t                         m_boundPort = 0;

    std::mutex                                    m_mu;
    std::vector<std::shared_ptr<SslConnection>>   m_conns;
    bool                                          m_stopped = false;
};

// -----------------------------------------------------------------------------
// RpcServer (Unix domain socket) — same accept/dispatch shape as RpcServerTcp,
// but bound to a filesystem path instead of host:port. There is no port and no
// TLS (a Unix socket is already local + kernel-enforced by file permissions).
// RpcConnection<Stream> is generic over the Asio stream, so the connection code
// is reused verbatim for the local::stream_protocol::socket.
// -----------------------------------------------------------------------------

using UnixStream = boost::asio::local::stream_protocol::socket;
using UnixConnection = RpcConnection<UnixStream>;

class RpcServerUnix : public std::enable_shared_from_this<RpcServerUnix> {
public:
    RpcServerUnix(boost::asio::io_context& ioc,
                  const std::string& socketPath,
                  std::shared_ptr<IWireCodec> codec,
                  IncomingCallHandler* handler);

    // Start accepting. Unlinks any stale socket file at the path first; returns
    // false if the path is empty or bind/listen fails.
    bool start();

    // The bound socket path (empty until start() succeeds).
    const std::string& socketPath() const { return m_socketPath; }

    void stop();

private:
    void doAccept();

    boost::asio::local::stream_protocol::acceptor m_acceptor;
    std::shared_ptr<IWireCodec>       m_codec;
    IncomingCallHandler*              m_handler;
    std::string                       m_socketPath;
    bool                              m_bound = false;

    std::mutex                                    m_mu;
    std::vector<std::shared_ptr<UnixConnection>>  m_conns;
    bool                                          m_stopped = false;
};

} // namespace logos::plain

#endif // LOGOS_PLAIN_RPC_SERVER_H
