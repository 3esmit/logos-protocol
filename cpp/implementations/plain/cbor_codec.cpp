#include "cbor_codec.h"
#include "json_mapping.h"

#include <nlohmann/json.hpp>

namespace logos::plain {

using json = nlohmann::json;

std::vector<uint8_t> CborCodec::encode(const AnyMessage& msg)
{
    const json j = messageToJson(msg);
    return json::to_cbor(j);
}

AnyMessage CborCodec::decode(MessageType tag, const uint8_t* data, std::size_t len)
{
    json j;
    try {
        j = json::from_cbor(data, data + len, /*strict=*/true, /*allow_exceptions=*/true);
    } catch (const std::exception& e) {
        throw CodecError(std::string("cbor parse failed: ") + e.what());
    }
    return jsonToMessage(tag, j);
}

} // namespace logos::plain
