#ifndef LOGOS_PLAIN_CBOR_CODEC_H
#define LOGOS_PLAIN_CBOR_CODEC_H

#include "wire_codec.h"

namespace logos::plain {

// CborCodec — same logical message layout as JsonCodec (shared via
// json_mapping.{h,cpp}), serialized with nlohmann::json::to_cbor /
// from_cbor. Matches JSON wire-for-wire in logical content; wire bytes
// are binary CBOR rather than UTF-8 JSON text.
//
// Useful when you want smaller/faster on-the-wire encoding without
// swapping to a wholly different codec family. Paired transports on the
// daemon can offer both JSON and CBOR; clients pick per-connection via
// --client-codec.
class CborCodec : public IWireCodec {
public:
    std::vector<uint8_t> encode(const AnyMessage&) override;
    AnyMessage           decode(MessageType tag,
                                const uint8_t* data,
                                std::size_t len) override;
    std::string          name() const override { return "cbor"; }
};

} // namespace logos::plain

#endif // LOGOS_PLAIN_CBOR_CODEC_H
