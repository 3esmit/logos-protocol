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

} // namespace logos::plain

#endif // LOGOS_PLAIN_JSON_MAPPING_H
