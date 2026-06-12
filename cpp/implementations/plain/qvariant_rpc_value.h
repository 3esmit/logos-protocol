#ifndef LOGOS_PLAIN_QVARIANT_RPC_VALUE_H
#define LOGOS_PLAIN_QVARIANT_RPC_VALUE_H

// Qt ↔ plain adapter. This is THE place the plain-C++ transport tier
// touches Qt — isolated here so when Qt is eventually removed from the
// SDK interface, there's one file to delete.

#include "rpc_message.h"
#include "rpc_value.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace logos::plain {

RpcValue qvariantToRpcValue(const QVariant& v);
QVariant rpcValueToQVariant(const RpcValue& v);

std::vector<RpcValue> qvariantListToRpcList(const QVariantList& list);
QVariantList          rpcListToQVariantList(const std::vector<RpcValue>& list);

// Method metadata round-trip (used for introspection).
QJsonArray              methodsToJsonArray(const std::vector<MethodMetadata>& methods);
std::vector<MethodMetadata> methodsFromJsonArray(const QJsonArray& arr);

} // namespace logos::plain

#endif // LOGOS_PLAIN_QVARIANT_RPC_VALUE_H
