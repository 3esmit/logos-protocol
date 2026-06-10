#include <gtest/gtest.h>

#include "logos_json_convert.h"
#include "logos_types.h"

#include <QByteArray>
#include <QMetaType>
#include <QVariant>
#include <QVariantList>

// The canonical C-ABI bytes encoding: QByteArray ⇄ {"_bytes":"<base64url>"}
// (single-key object, unpadded base64url — same as the plain wire's
// json_mapping.cpp). These tests pin the encoding and the round-trip,
// including the historical loss case: bytes with embedded NUL.

using logos::qvariantToNlohmann;
using logos::nlohmannToQVariant;
using logos::nlohmannArgsToQVariantList;

TEST(JsonConvertBytes, ByteArrayEncodesAsTaggedObject)
{
    const QByteArray bytes("\x00\x01\xfe", 3);
    nlohmann::json j = qvariantToNlohmann(QVariant(bytes));

    ASSERT_TRUE(j.is_object());
    ASSERT_EQ(j.size(), 1u);
    ASSERT_TRUE(j.contains("_bytes"));
    // 0x00 0x01 0xFE → base64 "AAH+" → base64url "AAH-" (no padding needed).
    EXPECT_EQ(j["_bytes"].get<std::string>(), "AAH-");
}

TEST(JsonConvertBytes, NulByteRoundTripPreservesSize)
{
    // The regression this encoding exists to prevent: embedded NUL used to
    // be truncated/mangled by the string fallback.
    const QByteArray original("a\0b\0c", 5);
    ASSERT_EQ(original.size(), 5);

    nlohmann::json j = qvariantToNlohmann(QVariant(original));
    QVariant back = nlohmannToQVariant(j);

    ASSERT_EQ(back.userType(), QMetaType::QByteArray);
    const QByteArray bytes = back.toByteArray();
    EXPECT_EQ(bytes.size(), 5);  // explicit byteArraySize assertion
    EXPECT_EQ(bytes, original);
}

TEST(JsonConvertBytes, AllByteValuesRoundTrip)
{
    QByteArray original;
    for (int i = 0; i < 256; ++i)
        original.append(static_cast<char>(i));

    QVariant back = nlohmannToQVariant(qvariantToNlohmann(QVariant(original)));
    ASSERT_EQ(back.userType(), QMetaType::QByteArray);
    EXPECT_EQ(back.toByteArray(), original);
}

TEST(JsonConvertBytes, EmptyByteArrayRoundTrips)
{
    QVariant back = nlohmannToQVariant(qvariantToNlohmann(QVariant(QByteArray())));
    ASSERT_EQ(back.userType(), QMetaType::QByteArray);
    EXPECT_TRUE(back.toByteArray().isEmpty());
}

TEST(JsonConvertBytes, ArgsListDecodesTaggedBytes)
{
    nlohmann::json args = nlohmann::json::array();
    args.push_back("plain string");
    args.push_back(qvariantToNlohmann(QVariant(QByteArray("x\0y", 3))));

    QVariantList list = nlohmannArgsToQVariantList(args);
    ASSERT_EQ(list.size(), 2);
    EXPECT_EQ(list[0].toString(), "plain string");
    ASSERT_EQ(list[1].userType(), QMetaType::QByteArray);
    EXPECT_EQ(list[1].toByteArray(), QByteArray("x\0y", 3));
}

TEST(JsonConvertBytes, LogosResultValueBytesAreTagged)
{
    qRegisterMetaType<LogosResult>("LogosResult");

    LogosResult lr;
    lr.success = true;
    lr.value = QVariant(QByteArray("p\0q", 3));

    nlohmann::json j = qvariantToNlohmann(QVariant::fromValue(lr));
    ASSERT_TRUE(j.is_object());
    EXPECT_TRUE(j["success"].get<bool>());
    ASSERT_TRUE(j["value"].is_object());
    ASSERT_TRUE(j["value"].contains("_bytes"));

    QVariant back = nlohmannToQVariant(j["value"]);
    ASSERT_EQ(back.userType(), QMetaType::QByteArray);
    EXPECT_EQ(back.toByteArray(), QByteArray("p\0q", 3));
}

TEST(JsonConvertBytes, OrdinaryObjectsAreNotMistakenForBytes)
{
    // Two keys → a real map, even though one key is "_bytes".
    nlohmann::json twoKeys = {{"_bytes", "AAA"}, {"other", 1}};
    QVariant v1 = nlohmannToQVariant(twoKeys);
    EXPECT_NE(v1.userType(), QMetaType::QByteArray);

    // "_bytes" with a non-string value → a real map.
    nlohmann::json nonString = {{"_bytes", 42}};
    QVariant v2 = nlohmannToQVariant(nonString);
    EXPECT_NE(v2.userType(), QMetaType::QByteArray);

    // A plain object stays an object.
    nlohmann::json plain = {{"a", 1}, {"b", 2}};
    QVariant v3 = nlohmannToQVariant(plain);
    EXPECT_NE(v3.userType(), QMetaType::QByteArray);
}
