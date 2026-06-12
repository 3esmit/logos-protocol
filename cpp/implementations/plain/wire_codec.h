#ifndef LOGOS_PLAIN_WIRE_CODEC_H
#define LOGOS_PLAIN_WIRE_CODEC_H

#include "rpc_message.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace logos::plain {

// -----------------------------------------------------------------------------
// IWireCodec — pluggable (de)serializer for the wire message set.
//
// The plan calls out CDDL/CBOR as the intended future format. Shipping
// implementation is `JsonCodec` (nlohmann::json::dump / parse). A future
// `CborCodec` uses `to_cbor` / `from_cbor` on the same message structs; the
// messages don't change.
//
// `encode` / `decode` produce / consume bare payload bytes — they do NOT
// handle framing (length prefix or type tag). Framing is `rpc_framing.{h,cpp}`.
// -----------------------------------------------------------------------------

class CodecError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class IWireCodec {
public:
    virtual ~IWireCodec() = default;

    // Encode one message to bytes. The caller stamps the type tag and
    // length prefix around the returned payload.
    virtual std::vector<uint8_t> encode(const AnyMessage&) = 0;

    // Decode one payload of the given type (learned from the 1-byte tag).
    // Throws CodecError on malformed input.
    virtual AnyMessage decode(MessageType tag,
                              const uint8_t* data,
                              std::size_t len) = 0;

    // Human-readable name: "json" | "cbor" | ...
    virtual std::string name() const = 0;
};

} // namespace logos::plain

#endif // LOGOS_PLAIN_WIRE_CODEC_H
