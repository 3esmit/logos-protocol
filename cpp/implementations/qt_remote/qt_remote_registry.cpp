#include "qt_remote_registry.h"
#include <QRemoteObjectRegistryHost>
#include <QUrl>
#include <QDebug>

QtRemoteRegistry::QtRemoteRegistry(const QString& url)
    : m_registryHost(nullptr)
{
    m_registryHost = new QRemoteObjectRegistryHost(QUrl(url));

    if (m_registryHost) {
        qDebug() << "QtRemoteRegistry: Registry host created at:" << url;
    } else {
        qCritical() << "QtRemoteRegistry: Failed to create registry host at:" << url;
    }
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
