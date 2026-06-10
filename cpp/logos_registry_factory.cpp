#include "logos_registry_factory.h"
#include "logos_registry.h"
#include "logos_mode.h"
#include "implementations/qt_remote/qt_remote_registry.h"
#include "implementations/mock/mock_registry.h"

namespace {

/**
 * @brief No-op registry used in Local (in-process) mode.
 *
 * In Local mode all modules live in the same process and discover each
 * other through PluginRegistry, so no IPC rendezvous point is required.
 */
class NullRegistry : public LogosRegistry {
public:
    bool isInitialized() const override { return true; }
};

} // anonymous namespace

namespace LogosRegistryFactory {

std::unique_ptr<LogosRegistry> create(const QString& url)
{
    if (LogosModeConfig::isLocal()) {
        return std::make_unique<NullRegistry>();
    }
    if (LogosModeConfig::isMock()) {
        return std::make_unique<MockRegistry>();
    }
    return std::make_unique<QtRemoteRegistry>(url);
}

} // namespace LogosRegistryFactory
