#ifndef QT_REMOTE_REGISTRY_H
#define QT_REMOTE_REGISTRY_H

#include "../../logos_registry.h"
#include <QString>

class QRemoteObjectRegistryHost;

/**
 * @brief LogosRegistry implementation backed by QRemoteObjectRegistryHost.
 *
 * Used in Remote (multi-process) mode.  The registry host is created in the
 * constructor and torn down in the destructor, so the lifetime of this object
 * directly controls the lifetime of the IPC rendezvous point.
 */
class QtRemoteRegistry : public LogosRegistry {
public:
    explicit QtRemoteRegistry(const QString& url);
    ~QtRemoteRegistry() override;

    bool isInitialized() const override;

private:
    QRemoteObjectRegistryHost* m_registryHost;
};

#endif // QT_REMOTE_REGISTRY_H
