#ifndef LOGOS_TRANSPORT_FACTORY_H
#define LOGOS_TRANSPORT_FACTORY_H

#include "logos_transport_config.h"

#include <memory>
#include <QString>

class LogosTransportHost;
class LogosTransportConnection;

namespace LogosTransportFactory {

    /**
     * @brief Create a transport host for `cfg`, honoring the process-wide
     * LogosMode.
     *
     * Resolution rule:
     *   - LogosMode::Mock                 → MockTransportHost   (cfg ignored)
     *   - LogosMode::Local                → LocalTransportHost  (cfg ignored)
     *   - LogosMode::Remote + LocalSocket → RemoteTransportHost (QRO)
     *   - LogosMode::Remote + Tcp/TcpSsl  → PlainTransportHost(cfg)
     *
     * Mode wins over `cfg.protocol` so test fixtures that switch the
     * process into Mock/Local always get the test transport, regardless
     * of which overload (or which LogosAPIProvider constructor) was
     * used. In Remote mode, `cfg` chooses the wire protocol and
     * carries the bind/dial address + TLS material.
     */
    std::unique_ptr<LogosTransportHost>
        createHost(const LogosTransportConfig& cfg,
                   const QString& registryUrl);

    /**
     * @brief Convenience: createHost using the process-global default
     * LogosTransportConfig. Equivalent to
     * `createHost(LogosTransportConfigGlobal::getDefault(), registryUrl)`.
     */
    std::unique_ptr<LogosTransportHost> createHost(const QString& registryUrl);

    /**
     * @brief Create a transport connection for `cfg`, honoring the
     * process-wide LogosMode. Same resolution rule as createHost — see
     * its doc-comment for the full table.
     */
    std::unique_ptr<LogosTransportConnection>
        createConnection(const LogosTransportConfig& cfg,
                         const QString& registryUrl);

    /**
     * @brief Convenience: createConnection using the process-global default
     * LogosTransportConfig. Equivalent to
     * `createConnection(LogosTransportConfigGlobal::getDefault(), registryUrl)`.
     */
    std::unique_ptr<LogosTransportConnection> createConnection(const QString& registryUrl);

    /**
     * @brief Whether a connection resolved from `cfg` owns Qt objects that only
     * work on a thread running a Qt event loop.
     *
     * True for the qt_remote (LocalSocket) transport — a QRemoteObjectNode plus
     * its QLocalSocket, whose replica acquisition and reply delivery both ride
     * the owning thread's event loop — and for the in-process qt_local
     * transport, which invokes on QObjects living on the module's main thread.
     * False for the plain Tcp/TcpSsl transports (Qt-free by design) and for
     * Mock (no sockets at all).
     *
     * Callers use this to decide *which thread must construct* a client: see
     * lp_client_create(). Mirrors the createConnection resolution rule above —
     * keep the two in sync.
     */
    bool needsQtEventLoop(const LogosTransportConfig& cfg);

}

#endif // LOGOS_TRANSPORT_FACTORY_H
