#include "qt_remote_registry.h"
#include "../../logos_socket_paths.h"
#include "qt_socket_path.h"
#include <QRemoteObjectRegistryHost>
#include <QUrl>
#include <QDebug>

using logos::qtremote::localSocketFilePath;

QtRemoteRegistry::QtRemoteRegistry(const QString& url)
    : m_registryHost(nullptr)
{
    // Empty-URL ctor does not listen; setRegistryUrl() does, and returns the
    // result so a failed bind doesn't leave a silently-broken host.
    auto* host = new QRemoteObjectRegistryHost();
    if (!host->setRegistryUrl(QUrl(url))) {
        qCritical() << "QtRemoteRegistry: failed to listen at" << url
                    << "- error" << host->lastError()
                    << "- socket path" << localSocketFilePath(url);
        delete host;
        return;
    }
    m_registryHost = host;

    if (QUrl(url).scheme() == QLatin1String("local")) {
        std::string permErr;
        if (!logos::applySocketPerms(localSocketFilePath(url).toStdString(), &permErr))
            qWarning() << "QtRemoteRegistry: could not apply socket perms:"
                       << QString::fromStdString(permErr);
    }
    qDebug() << "QtRemoteRegistry: Registry host created at:" << url;
}

QtRemoteRegistry::~QtRemoteRegistry()
{
    delete m_registryHost;
    m_registryHost = nullptr;
    qDebug() << "QtRemoteRegistry: Registry host destroyed";
}

bool QtRemoteRegistry::isInitialized() const
{
    return m_registryHost != nullptr;
}
