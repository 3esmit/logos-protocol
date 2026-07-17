#include <gtest/gtest.h>

#include "logos_json_convert.h"
#include "logos_types.h"

#include <QByteArray>
#include <QMetaType>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

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

TEST(JsonConvertBytes, IntegersStayIntegersNotDoubles)
{
    // QJsonValue::fromVariant degrades every numeric to double; the canonical
    // C-ABI converter must not — a strict consumer (e.g. a generated dispatch
    // reading an int param) rejects 5.0 where it expects 5.
    nlohmann::json a = qvariantToNlohmann(QVariant(static_cast<qulonglong>(5)));
    EXPECT_TRUE(a.is_number_integer() || a.is_number_unsigned());
    EXPECT_EQ(a.get<int64_t>(), 5);

    nlohmann::json b = qvariantToNlohmann(QVariant(static_cast<qlonglong>(-7)));
    EXPECT_TRUE(b.is_number_integer());
    EXPECT_EQ(b.get<int64_t>(), -7);

    nlohmann::json c = qvariantToNlohmann(QVariant(42));
    EXPECT_TRUE(c.is_number_integer());
    EXPECT_EQ(c.get<int64_t>(), 42);

    // Doubles stay doubles.
    nlohmann::json d = qvariantToNlohmann(QVariant(3.5));
    EXPECT_TRUE(d.is_number_float());
}

TEST(JsonConvertArrays, StringArrayRoundTripsToQStringList)
{
    nlohmann::json j = nlohmann::json::array({"a", "b", "c"});
    QVariant v = nlohmannToQVariant(j);

    const QStringList list = v.toStringList();
    ASSERT_EQ(list.size(), 3);
    EXPECT_EQ(list.at(0), "a");
    EXPECT_EQ(list.at(1), "b");
    EXPECT_EQ(list.at(2), "c");
}

TEST(JsonConvertArrays, MixedArrayRoundTripsToQVariantList)
{
    nlohmann::json j = nlohmann::json::array({"a", 42, true, nullptr});
    QVariant v = nlohmannToQVariant(j);

    const QVariantList list = v.toList();
    ASSERT_EQ(list.size(), 4);
    EXPECT_EQ(list.at(0).toString(), "a");
    EXPECT_EQ(list.at(1).toLongLong(), 42);
    EXPECT_EQ(list.at(2).toBool(), true);
    EXPECT_FALSE(list.at(3).isValid());
}

TEST(JsonConvertArrays, ObjectRoundTripsToQVariantMap)
{
    nlohmann::json j = {{"name", "wallet_module"}, {"version", 1}};
    QVariant v = nlohmannToQVariant(j);

    const QVariantMap map = v.toMap();
    EXPECT_EQ(map.value("name").toString(), "wallet_module");
    EXPECT_EQ(map.value("version").toLongLong(), 1);
}

TEST(JsonConvertArrays, NestedArrayInsideObjectIsAlsoConverted)
{
    nlohmann::json j = {{"clients", nlohmann::json::array({"u1", "u2"})}};
    QVariant v = nlohmannToQVariant(j);

    const QVariantMap map = v.toMap();
    const QStringList inner = map.value("clients").toStringList();
    ASSERT_EQ(inner.size(), 2);
    EXPECT_EQ(inner.at(0), "u1");
    EXPECT_EQ(inner.at(1), "u2");
}

TEST(JsonConvertArgs, NestedStringArrayArgRoundTripsToQStringList)
{
    nlohmann::json args = nlohmann::json::array({nlohmann::json::array({"p1", "p2", "p3"})});
    const QVariantList qArgs = nlohmannArgsToQVariantList(args);

    ASSERT_EQ(qArgs.size(), 1);
    const QStringList inner = qArgs.at(0).toStringList();
    ASSERT_EQ(inner.size(), 3);
    EXPECT_EQ(inner.at(0), "p1");
    EXPECT_EQ(inner.at(1), "p2");
    EXPECT_EQ(inner.at(2), "p3");
}

TEST(JsonConvertArgs, NestedObjectArgRoundTripsToQVariantMap)
{
    nlohmann::json args = nlohmann::json::array({
        {{"port", 30303}, {"name", "node-a"}}
    });
    const QVariantList qArgs = nlohmannArgsToQVariantList(args);

    ASSERT_EQ(qArgs.size(), 1);
    const QVariantMap inner = qArgs.at(0).toMap();
    EXPECT_EQ(inner.value("port").toLongLong(), 30303);
    EXPECT_EQ(inner.value("name").toString(), "node-a");
}

// Integers inside a container must NOT be degraded to doubles. The historical
// bug: QJsonValue::fromVariant turned a QVariantList of ints into a float array,
// so a strict `.get<std::vector<int64_t>>()` on the C-ABI side threw and the
// generated dispatch decoded an EMPTY vector (a `[int]` param arrived empty).
TEST(JsonConvertInts, IntListElementsStayIntegers)
{
    const QVariantList list{1, 2, 3};
    nlohmann::json j = qvariantToNlohmann(QVariant(list));

    ASSERT_TRUE(j.is_array());
    ASSERT_EQ(j.size(), 3u);
    for (const auto& e : j)
        EXPECT_TRUE(e.is_number_integer()) << "degraded element: " << e.dump();
    EXPECT_EQ(j.get<std::vector<int64_t>>(), (std::vector<int64_t>{1, 2, 3}));
}

TEST(JsonConvertInts, LongLongListElementsStayIntegers)
{
    // Over QtRO ints commonly arrive as qlonglong; same requirement. Use values
    // ABOVE 2^53 so an accidental round-trip through IEEE-754 double would lose
    // precision (or serialize in scientific notation) and fail the assertion —
    // small values <= 2^53 survive a double detour and wouldn't pin the bug.
    const qlonglong big  = Q_INT64_C(9007199254740993);   // 2^53 + 1
    const qlonglong huge = Q_INT64_C(9223372036854775807); // INT64_MAX
    QVariantList list;
    list << big << huge;
    nlohmann::json j = qvariantToNlohmann(QVariant(list));

    ASSERT_TRUE(j.is_array());
    for (const auto& e : j)
        EXPECT_TRUE(e.is_number_integer()) << "degraded element: " << e.dump();
    EXPECT_EQ(j.get<std::vector<int64_t>>(), (std::vector<int64_t>{big, huge}));
}

TEST(JsonConvertInts, NestedMapIntValueStaysInteger)
{
    QVariantMap m;
    m.insert("n", 42);
    m.insert("s", QStringLiteral("x"));
    nlohmann::json j = qvariantToNlohmann(QVariant(m));

    ASSERT_TRUE(j.is_object());
    EXPECT_TRUE(j["n"].is_number_integer());
    EXPECT_EQ(j["n"].get<int64_t>(), 42);
    EXPECT_EQ(j["s"].get<std::string>(), "x");
}

TEST(JsonConvertInts, StringListStillDecodesAsStrings)
{
    // Regression guard: the fix must not disturb the [tstr] path.
    const QStringList sl{"a", "b"};
    nlohmann::json j = qvariantToNlohmann(QVariant(sl));

    ASSERT_TRUE(j.is_array());
    EXPECT_EQ(j.get<std::vector<std::string>>(), (std::vector<std::string>{"a", "b"}));
}
