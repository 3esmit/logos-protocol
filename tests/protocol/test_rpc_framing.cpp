#include <gtest/gtest.h>

#include "rpc_framing.h"
#include "json_codec.h"

#include <cstdint>
#include <vector>

using namespace logos::plain;

// Encode one frame, decode it back, compare.
TEST(RpcFramingTest, RoundTripCallMessage)
{
    JsonCodec codec;
    CallMessage call;
    call.id = 42;
    call.authToken = "tok";
    call.object = "core_service";
    call.method = "loadModule";
    call.args.push_back(RpcValue{std::string{"chat"}});

    auto frame = encodeFrame(codec, AnyMessage{call});

    FrameReader reader;
    reader.append(frame);

    MessageType tag;
    std::vector<uint8_t> payload;
    ASSERT_TRUE(reader.next(tag, payload));
    EXPECT_EQ(tag, MessageType::Call);

    AnyMessage decoded = codec.decode(tag, payload.data(), payload.size());
    auto* c = std::get_if<CallMessage>(&decoded);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->id, 42u);
    EXPECT_EQ(c->authToken, "tok");
    EXPECT_EQ(c->object, "core_service");
    EXPECT_EQ(c->method, "loadModule");
    ASSERT_EQ(c->args.size(), 1u);
    EXPECT_TRUE(c->args[0].isString());
    EXPECT_EQ(c->args[0].asString(), "chat");
}

// Feed the frame byte-by-byte; reader must wait for the whole frame.
TEST(RpcFramingTest, PartialReadsCoalesce)
{
    JsonCodec codec;
    EventMessage evt;
    evt.object = "core_service";
    evt.eventName = "module_event";
    evt.data.push_back(RpcValue{std::string{"test_basic_module"}});
    evt.data.push_back(RpcValue{std::string{"testEvent"}});

    auto frame = encodeFrame(codec, AnyMessage{evt});

    FrameReader reader;
    MessageType tag;
    std::vector<uint8_t> payload;

    // Feed one byte at a time; only the last byte should yield a complete frame.
    for (std::size_t i = 0; i < frame.size() - 1; ++i) {
        reader.append(&frame[i], 1);
        EXPECT_FALSE(reader.next(tag, payload));
    }
    reader.append(&frame.back(), 1);
    ASSERT_TRUE(reader.next(tag, payload));

    auto decoded = codec.decode(tag, payload.data(), payload.size());
    auto* e = std::get_if<EventMessage>(&decoded);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->object, "core_service");
    EXPECT_EQ(e->eventName, "module_event");
    ASSERT_EQ(e->data.size(), 2u);
    EXPECT_EQ(e->data[0].asString(), "test_basic_module");
    EXPECT_EQ(e->data[1].asString(), "testEvent");
}

// Two frames concatenated; reader yields them in order.
TEST(RpcFramingTest, ConcatenatedFrames)
{
    JsonCodec codec;
    SubscribeMessage sub;
    sub.object = "obj1";
    sub.eventName = "evt1";

    TokenMessage tok;
    tok.authToken = "auth";
    tok.moduleName = "mod";
    tok.token = "tok";

    auto f1 = encodeFrame(codec, AnyMessage{sub});
    auto f2 = encodeFrame(codec, AnyMessage{tok});
    std::vector<uint8_t> both(f1.begin(), f1.end());
    both.insert(both.end(), f2.begin(), f2.end());

    FrameReader reader;
    reader.append(both);

    MessageType t1, t2;
    std::vector<uint8_t> p1, p2;
    ASSERT_TRUE(reader.next(t1, p1));
    ASSERT_TRUE(reader.next(t2, p2));
    EXPECT_EQ(t1, MessageType::Subscribe);
    EXPECT_EQ(t2, MessageType::Token);
}

// Oversized length prefix rejected.
TEST(RpcFramingTest, RejectsOversizedFrame)
{
    FrameReader reader;
    std::vector<uint8_t> badPrefix = { 0xff, 0xff, 0xff, 0xff, 0x00 }; // 4 GiB
    reader.append(badPrefix);

    MessageType tag;
    std::vector<uint8_t> payload;
    EXPECT_THROW({ reader.next(tag, payload); }, FramingError);
}

// Zero length rejected.
TEST(RpcFramingTest, RejectsZeroLength)
{
    FrameReader reader;
    std::vector<uint8_t> zero = { 0x00, 0x00, 0x00, 0x00 };
    reader.append(zero);

    MessageType tag;
    std::vector<uint8_t> payload;
    EXPECT_THROW({ reader.next(tag, payload); }, FramingError);
}
