#ifndef LOGOS_RPC_STATUS_H
#define LOGOS_RPC_STATUS_H

#include <QVariant>
#include <QVariantMap>
#include <QJsonObject>
#include <QString>
#include <QMetaType>

// A provider returns one of these reserved maps INSTEAD of a bare QVariant()
// when it needs to communicate a transport-level call status. Backward
// compatible by construction:
//
//   * OLD consumers convert it exactly like a bare QVariant() for every
//     scalar / string / LogosResult return (`.toString()` -> "",
//     `qvariant_cast<T>` -> T{}, `.value<LogosResult>()` -> default), so they
//     keep seeing today's "empty/failed" result — nothing new to handle.
//   * NEW consumers (LogosAPIClient) detect it BELOW the generated typed
//     wrapper, drop the stale token, re-run capability_module.requestModule and
//     retry the call once. The sentinel is stripped before it ever reaches the
//     wrapper.
//
// The key is namespaced + double-underscored so it never collides with a real
// field, and every detector requires its exact reserved shape, so a legitimate
// map result can't false-match. It is the ONLY provider->consumer signal
// available on every transport (qt_local/qt_remote/plain) without an ABI
// break, because the QtRO dispatch slot returns a single QVariant.
namespace logos {

inline constexpr char kRpcStatusKey[]             = "__logos_rpc_status__";
inline constexpr char kRpcStatusMessageKey[]      = "__logos_rpc_message__";
inline constexpr char kRpcStatusUnauthorized[]    = "unauthorized";
inline constexpr char kRpcStatusProviderFailure[] = "provider_failure";

// The provider-side rejection value. QVariantMap round-trips faithfully on all
// three transports (qt_local pass-through; qt_remote QtRO-serialized; plain via
// qvariant_rpc_value.cpp map<->JSON), so the consumer always sees a QVariantMap.
inline QVariant makeUnauthorizedSentinel()
{
    QVariantMap m;
    m.insert(QString::fromLatin1(kRpcStatusKey),
             QString::fromLatin1(kRpcStatusUnauthorized));
    return m;
}

// True only for the exact rejection sentinel. Handles QVariantMap (the normal
// case on every transport) and QJsonObject (defensive: some json_convert paths
// historically produced QJsonObject). Never matches a normal empty result.
inline bool isUnauthorizedSentinel(const QVariant& v)
{
    const QString key  = QString::fromLatin1(kRpcStatusKey);
    const QString want = QString::fromLatin1(kRpcStatusUnauthorized);
    switch (v.userType()) {
    case QMetaType::QVariantMap: {
        const QVariantMap m = v.toMap();
        return m.size() == 1 && m.value(key).toString() == want;
    }
    case QMetaType::QJsonObject: {
        const QJsonObject o = v.toJsonObject();
        return o.size() == 1 && o.value(key).toString() == want;
    }
    default:
        return false;
    }
}

// A provider-side call threw before it could produce a method result. The
// message is deliberately fixed: provider exception text can contain request
// data, which must not cross a generic transport boundary.
inline QVariant makeProviderFailureSentinel()
{
    QVariantMap m;
    m.insert(QString::fromLatin1(kRpcStatusKey),
             QString::fromLatin1(kRpcStatusProviderFailure));
    m.insert(QString::fromLatin1(kRpcStatusMessageKey),
             QStringLiteral("provider invocation failed"));
    return m;
}

// True only for the exact provider-failure sentinel. `message` is optional so
// callers that only need to classify the result do not have to copy it.
inline bool isProviderFailureSentinel(const QVariant& v, QString* message = nullptr)
{
    const QString statusKey = QString::fromLatin1(kRpcStatusKey);
    const QString messageKey = QString::fromLatin1(kRpcStatusMessageKey);
    const QString want = QString::fromLatin1(kRpcStatusProviderFailure);
    auto matches = [&](const auto& map) {
        if (map.size() != 2 || map.value(statusKey).toString() != want
            || !map.contains(messageKey)) {
            return false;
        }
        if (message)
            *message = map.value(messageKey).toString();
        return true;
    };

    switch (v.userType()) {
    case QMetaType::QVariantMap:
        return matches(v.toMap());
    case QMetaType::QJsonObject:
        return matches(v.toJsonObject());
    default:
        return false;
    }
}

} // namespace logos

#endif // LOGOS_RPC_STATUS_H
