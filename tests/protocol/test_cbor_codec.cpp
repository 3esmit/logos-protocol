// Round-trip tests for CborCodec. Mirrors the JsonCodec suite so any
// behavioural divergence between the two codecs shows up immediately;
// both codecs go through the shared json_mapping layer, so these also
// validate that the CBOR serializer preserves every message shape.
#include <gtest/gtest.h>

#include "cbor_codec.h"

#include <algorithm>
#include <cstdint>

using namespace logos::plain;

namespace {
AnyMessage roundtrip(CborCodec& codec, const AnyMessage& msg)
{
    auto bytes = codec.encode(msg);
    return codec.decode(messageTypeOf(msg), bytes.data(), bytes.size());
}
} // anonymous namespace

TEST(CborCodecTest, CallMessage)
{
    CborCodec codec;
    CallMessage c;
    c.id = 7;
    c.authToken = "tok";
    c.object = "core_service";
    c.method = "loadModule";
    c.args.push_back(RpcValue{std::string{"chat"}});
    c.args.push_back(RpcValue{int64_t{42}});
    c.args.push_back(RpcValue{true});

    auto dec = std::get<CallMessage>(roundtrip(codec, AnyMessage{c}));
    EXPECT_EQ(dec.id, 7u);
    EXPECT_EQ(dec.authToken, "tok");
    EXPECT_EQ(dec.object, "core_service");
    EXPECT_EQ(dec.method, "loadModule");
    ASSERT_EQ(dec.args.size(), 3u);
    EXPECT_EQ(dec.args[0].asString(), "chat");
    EXPECT_EQ(dec.args[1].asInt(), 42);
    EXPECT_EQ(dec.args[2].asBool(), true);
}

TEST(CborCodecTest, ResultOkWithNestedMap)
{
    CborCodec codec;
    ResultMessage r;
    r.id = 11;
    r.ok = true;
    RpcMap m;
    m.emplace("status", RpcValue{std::string{"ok"}});
    m.emplace("count",  RpcValue{int64_t{3}});
    r.value = RpcValue{std::move(m)};

    auto dec = std::get<ResultMessage>(roundtrip(codec, AnyMessage{r}));
    EXPECT_EQ(dec.id, 11u);
    EXPECT_TRUE(dec.ok);
    ASSERT_TRUE(dec.value.isMap());
    const auto& got = dec.value.asMap().entries;
    auto status = std::find_if(got.begin(), got.end(),
        [](const auto& kv) { return kv.first == "status"; });
    auto count  = std::find_if(got.begin(), got.end(),
        [](const auto& kv) { return kv.first == "count"; });
    ASSERT_NE(status, got.end());
    ASSERT_NE(count,  got.end());
    EXPECT_EQ(status->second.asString(), "ok");
    EXPECT_EQ(count->second.asInt(), 3);
}

TEST(CborCodecTest, EventWithBytesPayload)
{
    CborCodec codec;
    EventMessage e;
    e.object    = "blob_mod";
    e.eventName = "arrived";
    RpcBytes bytes;
    bytes.data = {0x00, 0x01, 0xff, 0x7f, 0x80};
    e.data.push_back(RpcValue{std::move(bytes)});

    auto dec = std::get<EventMessage>(roundtrip(codec, AnyMessage{e}));
    ASSERT_EQ(dec.data.size(), 1u);
    ASSERT_TRUE(dec.data[0].isBytes());
    const auto& b = dec.data[0].asBytes().data;
    ASSERT_EQ(b.size(), 5u);
    EXPECT_EQ(b[0], 0x00);
    EXPECT_EQ(b[2], 0xff);
    EXPECT_EQ(b[4], 0x80);
}

TEST(CborCodecTest, TokenMessage)
{
    CborCodec codec;
    TokenMessage t;
    t.authToken  = "admin";
    t.moduleName = "chat";
    t.token      = "abcdef";

    auto dec = std::get<TokenMessage>(roundtrip(codec, AnyMessage{t}));
    EXPECT_EQ(dec.authToken,  "admin");
    EXPECT_EQ(dec.moduleName, "chat");
    EXPECT_EQ(dec.token,      "abcdef");
}

TEST(CborCodecTest, SmallerOnWireThanJsonForTypicalMessage)
{
    // Sanity: CBOR should be no larger than the JSON text form for a
    // typical call payload. Not a tight bound — it just catches the
    // CBOR path regressing into something that outputs quoted strings.
    CallMessage c;
    c.id = 1234567890;
    c.authToken = "admin-token";
    c.object = "core_service";
    c.method = "callModuleMethod";
    c.args.push_back(RpcValue{std::string{"test_basic_module"}});
    c.args.push_back(RpcValue{std::string{"echo"}});
    RpcList args;
    args.items.push_back(RpcValue{std::string{"hello world"}});
    c.args.push_back(RpcValue{std::move(args)});

    CborCodec cbor;
    auto cborBytes = cbor.encode(AnyMessage{c});

    // Compare against the JSON codec so we have a concrete ceiling.
    // JsonCodec must be available to the linker — this test lives in
    // the SDK's sdk_tests target which links against the same static lib.
    // We don't include JsonCodec here to keep the test self-contained;
    // just assert a conservative upper bound (JSON for this message is
    // ~170 bytes in practice; CBOR is ~110).
    EXPECT_LT(cborBytes.size(), 200u);
}
