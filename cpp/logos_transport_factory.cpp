#include "logos_transport_factory.h"
#include "logos_transport.h"
#include "logos_mode.h"
#include "logos_transport_config.h"
#include "implementations/qt_local/local_transport.h"
#include "implementations/qt_remote/remote_transport.h"
#include "implementations/mock/mock_transport.h"
#include "implementations/plain/plain_transport_connection.h"
#include "implementations/plain/plain_transport_host.h"

#include <QDebug>

namespace LogosTransportFactory {

// Single resolution rule for both `createHost` overloads:
//   LogosMode::Mock                 → MockTransportHost   (cfg ignored)
//   LogosMode::Local                → LocalTransportHost  (cfg ignored)
//   LogosMode::Remote + LocalSocket → RemoteTransportHost (QRO)
//   LogosMode::Remote + Tcp/TcpSsl  → PlainTransportHost(cfg)
//
// Mode is consulted *first* so test fixtures setting Mock/Local always
// get the right transport regardless of which createHost overload (or
// LogosAPIProvider constructor) was used. The no-cfg overload below
// just delegates with `LogosTransportConfigGlobal::getDefault()` so
// there's exactly one path.
std::unique_ptr<LogosTransportHost>
createHost(const LogosTransportConfig& cfg, const QString& registryUrl)
{
    if (LogosModeConfig::isLocal()) {
        return std::make_unique<LocalTransportHost>();
    }
    if (LogosModeConfig::isMock()) {
        return std::make_unique<MockTransportHost>();
    }
    switch (cfg.protocol) {
    case LogosProtocol::Tcp:
    case LogosProtocol::TcpSsl: {
        auto host = std::make_unique<logos::plain::PlainTransportHost>(cfg);
        if (!host->start()) {
            qCritical() << "LogosTransportFactory: PlainTransportHost::start() failed";
            return nullptr;
        }
        return host;
    }
    case LogosProtocol::LocalSocket:
    default:
        return std::make_unique<RemoteTransportHost>(registryUrl);
    }
}

std::unique_ptr<LogosTransportHost> createHost(const QString& registryUrl)
{
    return createHost(LogosTransportConfigGlobal::getDefault(), registryUrl);
}

// Same resolution rule as createHost — see the comment block above.
std::unique_ptr<LogosTransportConnection>
createConnection(const LogosTransportConfig& cfg, const QString& registryUrl)
{
    if (LogosModeConfig::isLocal()) {
        return std::make_unique<LocalTransportConnection>();
    }
    if (LogosModeConfig::isMock()) {
        return std::make_unique<MockTransportConnection>();
    }
    switch (cfg.protocol) {
    case LogosProtocol::Tcp:
    case LogosProtocol::TcpSsl:
        return std::make_unique<logos::plain::PlainTransportConnection>(cfg);
    case LogosProtocol::LocalSocket:
    default:
        return std::make_unique<RemoteTransportConnection>(registryUrl);
    }
}

std::unique_ptr<LogosTransportConnection> createConnection(const QString& registryUrl)
{
    return createConnection(LogosTransportConfigGlobal::getDefault(), registryUrl);
}

}
