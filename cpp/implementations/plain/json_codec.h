#ifndef LOGOS_PLAIN_JSON_CODEC_H
#define LOGOS_PLAIN_JSON_CODEC_H

#include "wire_codec.h"

namespace logos::plain {

// JsonCodec — uses nlohmann::json::dump / parse for the payload bytes.
// Default codec for now; a future CborCodec will swap in by using
// json::to_cbor / from_cbor on the same message structs.
class JsonCodec : public IWireCodec {
public:
    std::vector<uint8_t> encode(const AnyMessage&) override;
    AnyMessage           decode(MessageType tag,
                                const uint8_t* data,
                                std::size_t len) override;
    std::string          name() const override { return "json"; }
};

} // namespace logos::plain

#endif // LOGOS_PLAIN_JSON_CODEC_H
