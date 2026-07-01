#ifndef LOGOS_PLAIN_JSON_MAPPING_H
#define LOGOS_PLAIN_JSON_MAPPING_H

// Shared RpcValue ↔ nlohmann::json conversion used by both JsonCodec
// (dump / parse as text) and CborCodec (to_cbor / from_cbor as bytes).
// The JSON representation is the canonical in-memory form; codecs differ
// only in how they serialize that form to the wire.

#include "rpc_message.h"
#include "rpc_value.h"
#include "wire_codec.h"

#include <nlohmann/json.hpp>

namespace logos::plain {

nlohmann::json   messageToJson(const AnyMessage& msg);
AnyMessage       jsonToMessage(MessageType tag, const nlohmann::json& j);

// Per-value RpcValue <-> json (the canonical in-memory form). Exposed so the
// Qt-free provider/consumer dispatch paths can convert wire values to/from
// JSON without going through QVariant.
nlohmann::json        valueToJson(const RpcValue& v);
RpcValue              jsonToValue(const nlohmann::json& j);
nlohmann::json        argsToJson(const std::vector<RpcValue>& args);
std::vector<RpcValue> argsFromJson(const nlohmann::json& j);
MethodMetadata        methodFromJson(const nlohmann::json& j);

} // namespace logos::plain

#endif // LOGOS_PLAIN_JSON_MAPPING_H
