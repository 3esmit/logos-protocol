#ifndef QT_REMOTE_SOCKET_PATH_H
#define QT_REMOTE_SOCKET_PATH_H

#include <QDir>
#include <QString>
#include <QUrl>

namespace logos::qtremote {

// Reproduce Qt's QLocalServer name->path rule so we can post-process the socket
// file QtRO created — QtRO never exposes its underlying QLocalServer, so we
// can't ask it for the path or set its options. A relative name lands under
// QDir::tempPath(); an absolute path (e.g. from a relocated socket dir) is used
// verbatim. Only meaningful for `local:` URLs. Shared by RemoteTransportHost
// and QtRemoteRegistry so the two never drift.
inline QString localSocketFilePath(const QString& registryUrl)
{
    const QString path = QUrl(registryUrl).path();
    if (path.startsWith(QLatin1Char('/')))
        return path;
    return QDir::cleanPath(QDir::tempPath()) + QLatin1Char('/') + path;
}

}  // namespace logos::qtremote

#endif  // QT_REMOTE_SOCKET_PATH_H
