#include "json_codec.h"
#include "json_mapping.h"

#include <nlohmann/json.hpp>

namespace logos::plain {

using json = nlohmann::json;

std::vector<uint8_t> JsonCodec::encode(const AnyMessage& msg)
{
    const json j = messageToJson(msg);
    const std::string s = j.dump();
    return std::vector<uint8_t>(s.begin(), s.end());
}

AnyMessage JsonCodec::decode(MessageType tag, const uint8_t* data, std::size_t len)
{
    json j;
    try {
        j = json::parse(data, data + len);
    } catch (const std::exception& e) {
        throw CodecError(std::string("json parse failed: ") + e.what());
    }
    return jsonToMessage(tag, j);
}

} // namespace logos::plain
