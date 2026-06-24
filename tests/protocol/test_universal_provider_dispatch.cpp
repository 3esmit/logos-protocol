#include <gtest/gtest.h>

#include <QByteArray>
#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include "logos_provider_interface.h"

namespace {

class TypedSampleProvider : public LogosProviderObject {
public:
    QVariant callMethod(const QString& methodName, const QVariantList& args) override {
        return callMethodStdBridge(methodName, args);
    }
    QJsonArray getMethods() override { return getMethodsStdBridge(); }
    void setEventListener(EventCallback cb) override { setEventListenerStdBridge(std::move(cb)); }
    bool informModuleToken(const QString&, const QString&) override { return true; }
    void init(void*) override {}
    QString providerName() const override { return QStringLiteral("typed_sample"); }
    QString providerVersion() const override { return QStringLiteral("1.0.0"); }

    nlohmann::json callMethodStd(const std::string& methodName,
                                 const nlohmann::json& args) override {
        // Generic echo: capture and re-emit args[0] unchanged. Used by all
        // echo-style tests so the same path serves every type.
        if (methodName == "echo") {
            if (!args.is_array() || args.empty()) {
                lastArg = nullptr;
                return nullptr;
            }
            lastArg = args[0];
            return args[0];
        }

        // ---- Primitive returns -------------------------------------------
        if (methodName == "returnBool")        return true;
        if (methodName == "returnInt")         return static_cast<int64_t>(-42);
        if (methodName == "returnUint")        return static_cast<uint64_t>(9001);
        if (methodName == "returnDouble")      return 3.5;
        if (methodName == "returnString")      return std::string("hello world");
        if (methodName == "returnEmptyString") return std::string("");
        if (methodName == "returnNull")        return nullptr;

        // ---- Bytes (tagged via the canonical "_bytes" object) ------------
        if (methodName == "returnBytes") {
            // "a\0b\0c" -> base64url "YQBiAGM" (no padding). Embedded NUL is
            // the historical loss case the tagged form exists to prevent.
            return nlohmann::json{{"_bytes", "YQBiAGM"}};
        }

        // ---- Container returns -------------------------------------------
        if (methodName == "returnStringList")
            return nlohmann::json::array({"alpha", "beta", "gamma"});
        if (methodName == "returnEmptyStringList")
            return nlohmann::json::array();
        if (methodName == "returnMixedList")
            return nlohmann::json::array({"alpha", 42, true, nullptr});
        if (methodName == "returnNumericList")
            return nlohmann::json::array({1, 2, 3});
        if (methodName == "returnMap")
            return nlohmann::json{{"name", "wallet"}, {"version", 2}};
        if (methodName == "returnEmptyMap")
            return nlohmann::json::object();

        // ---- Nested: array of maps, map containing array, deep nesting ---
        if (methodName == "returnArrayOfMaps") {
            return nlohmann::json::array({
                {{"id", 1}, {"name", "a"}},
                {{"id", 2}, {"name", "b"}},
            });
        }
        if (methodName == "returnMapWithList") {
            return nlohmann::json{
                {"clients", nlohmann::json::array({"u1", "u2"})},
                {"count", 2}
            };
        }
        if (methodName == "returnDeepNesting") {
            // map -> array -> map -> array -> string
            return nlohmann::json{
                {"groups", nlohmann::json::array({
                    nlohmann::json{
                        {"name", "g1"},
                        {"members", nlohmann::json::array({"m1", "m2"})}
                    }
                })}
            };
        }

        return nullptr;
    }
    nlohmann::json lastArg;
};

} // namespace

class UniversalProviderDispatchTest : public ::testing::Test {
protected:
    TypedSampleProvider p;
};

TEST_F(UniversalProviderDispatchTest, ReturnBool)
{
    QVariant r = p.callMethod(QStringLiteral("returnBool"), {});
    ASSERT_TRUE(r.isValid());
    EXPECT_EQ(r.userType(), QMetaType::Bool);
    EXPECT_TRUE(r.toBool());
}

TEST_F(UniversalProviderDispatchTest, ReturnInt)
{
    QVariant r = p.callMethod(QStringLiteral("returnInt"), {});
    ASSERT_TRUE(r.isValid());
    EXPECT_EQ(r.toLongLong(), -42);
}

TEST_F(UniversalProviderDispatchTest, ReturnUint)
{
    QVariant r = p.callMethod(QStringLiteral("returnUint"), {});
    ASSERT_TRUE(r.isValid());
    EXPECT_EQ(r.toULongLong(), 9001u);
}

TEST_F(UniversalProviderDispatchTest, ReturnDouble)
{
    QVariant r = p.callMethod(QStringLiteral("returnDouble"), {});
    ASSERT_TRUE(r.isValid());
    EXPECT_DOUBLE_EQ(r.toDouble(), 3.5);
}

TEST_F(UniversalProviderDispatchTest, ReturnString)
{
    QVariant r = p.callMethod(QStringLiteral("returnString"), {});
    EXPECT_EQ(r.toString(), QStringLiteral("hello world"));
}

TEST_F(UniversalProviderDispatchTest, ReturnEmptyString)
{
    QVariant r = p.callMethod(QStringLiteral("returnEmptyString"), {});
    ASSERT_TRUE(r.isValid());
    EXPECT_EQ(r.toString(), QString());
}

TEST_F(UniversalProviderDispatchTest, ReturnBytes)
{
    QVariant r = p.callMethod(QStringLiteral("returnBytes"), {});
    ASSERT_EQ(r.userType(), QMetaType::QByteArray);
    // Embedded NULs preserved through tagged base64url encoding.
    EXPECT_EQ(r.toByteArray(), QByteArray("a\0b\0c", 5));
    EXPECT_EQ(r.toByteArray().size(), 5);
}

TEST_F(UniversalProviderDispatchTest, ReturnNull)
{
    QVariant r = p.callMethod(QStringLiteral("returnNull"), {});
    EXPECT_FALSE(r.isValid());
}

// The wallet/accounts-ui regression case: pre-fix this returned an empty
// QStringList silently. Universal-path mirror of qt-sdk's GetCategories.
TEST_F(UniversalProviderDispatchTest, ReturnStringList)
{
    QVariant r = p.callMethod(QStringLiteral("returnStringList"), {});
    EXPECT_EQ(r.toStringList(),
              (QStringList{} << "alpha" << "beta" << "gamma"));
}

TEST_F(UniversalProviderDispatchTest, ReturnEmptyStringList)
{
    QVariant r = p.callMethod(QStringLiteral("returnEmptyStringList"), {});
    // The empty case is important: pre-fix returned [] for *every* StringList,
    // so any test that only checked emptiness would have passed by accident.
    // Cross-checked by the non-empty test above.
    EXPECT_TRUE(r.toStringList().isEmpty());
}

TEST_F(UniversalProviderDispatchTest, ReturnMixedList)
{
    QVariant r = p.callMethod(QStringLiteral("returnMixedList"), {});
    const QVariantList list = r.toList();

    ASSERT_EQ(list.size(), 4);
    EXPECT_EQ(list.at(0).toString(), QStringLiteral("alpha"));
    EXPECT_EQ(list.at(1).toLongLong(), 42);
    EXPECT_TRUE(list.at(2).toBool());
    EXPECT_FALSE(list.at(3).isValid());
}

TEST_F(UniversalProviderDispatchTest, ReturnNumericList)
{
    QVariant r = p.callMethod(QStringLiteral("returnNumericList"), {});
    const QVariantList list = r.toList();
    ASSERT_EQ(list.size(), 3);
    EXPECT_EQ(list.at(0).toInt(), 1);
    EXPECT_EQ(list.at(1).toInt(), 2);
    EXPECT_EQ(list.at(2).toInt(), 3);
}

TEST_F(UniversalProviderDispatchTest, ReturnMap)
{
    QVariant r = p.callMethod(QStringLiteral("returnMap"), {});
    const QVariantMap map = r.toMap();
    EXPECT_EQ(map.value(QStringLiteral("name")).toString(), QStringLiteral("wallet"));
    EXPECT_EQ(map.value(QStringLiteral("version")).toLongLong(), 2);
}

TEST_F(UniversalProviderDispatchTest, ReturnEmptyMap)
{
    QVariant r = p.callMethod(QStringLiteral("returnEmptyMap"), {});
    EXPECT_TRUE(r.toMap().isEmpty());
}

// ===========================================================================
// Type matrix — nested containers. These are the cases the recursive
// conversion specifically buys: a shallow wrap would land any nested array
// as QJsonArray inside a QVariantMap and .toStringList() on it would come
// back empty.
// ===========================================================================

TEST_F(UniversalProviderDispatchTest, ReturnArrayOfMaps)
{
    QVariant r = p.callMethod(QStringLiteral("returnArrayOfMaps"), {});
    const QVariantList list = r.toList();

    ASSERT_EQ(list.size(), 2);
    const QVariantMap m0 = list.at(0).toMap();
    const QVariantMap m1 = list.at(1).toMap();
    EXPECT_EQ(m0.value(QStringLiteral("id")).toLongLong(), 1);
    EXPECT_EQ(m0.value(QStringLiteral("name")).toString(), QStringLiteral("a"));
    EXPECT_EQ(m1.value(QStringLiteral("id")).toLongLong(), 2);
    EXPECT_EQ(m1.value(QStringLiteral("name")).toString(), QStringLiteral("b"));
}

TEST_F(UniversalProviderDispatchTest, ReturnMapWithListInside)
{
    QVariant r = p.callMethod(QStringLiteral("returnMapWithList"), {});
    const QVariantMap map = r.toMap();

    const QStringList clients = map.value(QStringLiteral("clients")).toStringList();
    ASSERT_EQ(clients.size(), 2);
    EXPECT_EQ(clients.at(0), QStringLiteral("u1"));
    EXPECT_EQ(clients.at(1), QStringLiteral("u2"));
    EXPECT_EQ(map.value(QStringLiteral("count")).toLongLong(), 2);
}

TEST_F(UniversalProviderDispatchTest, ReturnDeepNesting)
{
    QVariant r = p.callMethod(QStringLiteral("returnDeepNesting"), {});
    const QVariantMap root = r.toMap();

    const QVariantList groups = root.value(QStringLiteral("groups")).toList();
    ASSERT_EQ(groups.size(), 1);
    const QVariantMap g0 = groups.at(0).toMap();
    EXPECT_EQ(g0.value(QStringLiteral("name")).toString(), QStringLiteral("g1"));

    const QStringList members = g0.value(QStringLiteral("members")).toStringList();
    ASSERT_EQ(members.size(), 2);
    EXPECT_EQ(members.at(0), QStringLiteral("m1"));
    EXPECT_EQ(members.at(1), QStringLiteral("m2"));
}

// ===========================================================================
// Type matrix — args side. Each test sends a known QVariant in, the
// provider records the JSON it saw, and the same value comes back as the
// return so we get a free round-trip assertion.
//
// The args-side conversion happens in nlohmannArgsToQVariantList; the bug
// was the same wrap-as-QJsonArray/QJsonObject pattern for nested values.
// ===========================================================================

TEST_F(UniversalProviderDispatchTest, ArgBoolRoundTrip)
{
    QVariant r = p.callMethod(QStringLiteral("echo"), {QVariant(true)});
    ASSERT_TRUE(p.lastArg.is_boolean());
    EXPECT_TRUE(p.lastArg.get<bool>());
    EXPECT_TRUE(r.toBool());
}

TEST_F(UniversalProviderDispatchTest, ArgIntRoundTrip)
{
    QVariant r = p.callMethod(QStringLiteral("echo"), {QVariant(qlonglong(-1234))});
    ASSERT_TRUE(p.lastArg.is_number_integer());
    EXPECT_EQ(p.lastArg.get<int64_t>(), -1234);
    EXPECT_EQ(r.toLongLong(), -1234);
}

TEST_F(UniversalProviderDispatchTest, ArgUintRoundTrip)
{
    QVariant r = p.callMethod(QStringLiteral("echo"), {QVariant(qulonglong(42u))});
    ASSERT_TRUE(p.lastArg.is_number_unsigned() || p.lastArg.is_number_integer());
    EXPECT_EQ(p.lastArg.get<uint64_t>(), 42u);
    EXPECT_EQ(r.toULongLong(), 42u);
}

TEST_F(UniversalProviderDispatchTest, ArgDoubleRoundTrip)
{
    QVariant r = p.callMethod(QStringLiteral("echo"), {QVariant(2.5)});
    ASSERT_TRUE(p.lastArg.is_number_float());
    EXPECT_DOUBLE_EQ(p.lastArg.get<double>(), 2.5);
    EXPECT_DOUBLE_EQ(r.toDouble(), 2.5);
}

TEST_F(UniversalProviderDispatchTest, ArgStringRoundTrip)
{
    QVariant r = p.callMethod(QStringLiteral("echo"), {QVariant(QStringLiteral("payload"))});
    ASSERT_TRUE(p.lastArg.is_string());
    EXPECT_EQ(p.lastArg.get<std::string>(), "payload");
    EXPECT_EQ(r.toString(), QStringLiteral("payload"));
}

TEST_F(UniversalProviderDispatchTest, ArgBytesRoundTrip)
{
    // The historical loss case — embedded NULs must survive both directions.
    const QByteArray original("x\0y", 3);
    QVariant r = p.callMethod(QStringLiteral("echo"), {QVariant(original)});

    // Bytes encode as the canonical {"_bytes": "<base64url>"} object on the
    // wire, so the provider's lastArg is an object, not a string.
    ASSERT_TRUE(p.lastArg.is_object());
    ASSERT_TRUE(p.lastArg.contains("_bytes"));

    // Round-trip back into QByteArray (the bridge decodes the tagged form).
    ASSERT_EQ(r.userType(), QMetaType::QByteArray);
    EXPECT_EQ(r.toByteArray(), original);
    EXPECT_EQ(r.toByteArray().size(), 3);
}

TEST_F(UniversalProviderDispatchTest, ArgStringListRoundTrip)
{
    const QStringList input{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")};
    QVariant r = p.callMethod(QStringLiteral("echo"), {QVariant(input)});

    ASSERT_TRUE(p.lastArg.is_array());
    EXPECT_EQ(p.lastArg.size(), 3u);
    EXPECT_EQ(p.lastArg[1].get<std::string>(), "b");

    EXPECT_EQ(r.toStringList(), input);
}

TEST_F(UniversalProviderDispatchTest, ArgEmptyStringListRoundTrip)
{
    QVariant r = p.callMethod(QStringLiteral("echo"), {QVariant(QStringList())});
    ASSERT_TRUE(p.lastArg.is_array());
    EXPECT_EQ(p.lastArg.size(), 0u);
    EXPECT_TRUE(r.toStringList().isEmpty());
}

TEST_F(UniversalProviderDispatchTest, ArgVariantListRoundTrip)
{
    QVariantList input;
    input << QVariant(QStringLiteral("alpha"))
          << QVariant(qlonglong(42))
          << QVariant(true);
    QVariant r = p.callMethod(QStringLiteral("echo"), {QVariant(input)});

    ASSERT_TRUE(p.lastArg.is_array());
    EXPECT_EQ(p.lastArg.size(), 3u);
    EXPECT_EQ(p.lastArg[0].get<std::string>(), "alpha");
    EXPECT_EQ(p.lastArg[1].get<int64_t>(), 42);
    EXPECT_EQ(p.lastArg[2].get<bool>(), true);

    const QVariantList out = r.toList();
    ASSERT_EQ(out.size(), 3);
    EXPECT_EQ(out.at(0).toString(), QStringLiteral("alpha"));
    EXPECT_EQ(out.at(1).toLongLong(), 42);
    EXPECT_TRUE(out.at(2).toBool());
}

TEST_F(UniversalProviderDispatchTest, ArgVariantMapRoundTrip)
{
    QVariantMap input;
    input.insert(QStringLiteral("port"), 30303);
    input.insert(QStringLiteral("name"), QStringLiteral("node-a"));
    QVariant r = p.callMethod(QStringLiteral("echo"), {QVariant(input)});

    ASSERT_TRUE(p.lastArg.is_object());
    EXPECT_EQ(p.lastArg["port"].get<int64_t>(), 30303);
    EXPECT_EQ(p.lastArg["name"].get<std::string>(), "node-a");

    const QVariantMap out = r.toMap();
    EXPECT_EQ(out.value(QStringLiteral("port")).toLongLong(), 30303);
    EXPECT_EQ(out.value(QStringLiteral("name")).toString(), QStringLiteral("node-a"));
}

TEST_F(UniversalProviderDispatchTest, ArgEmptyVariantMapRoundTrip)
{
    QVariant r = p.callMethod(QStringLiteral("echo"), {QVariant(QVariantMap())});
    ASSERT_TRUE(p.lastArg.is_object());
    EXPECT_TRUE(p.lastArg.empty());
    EXPECT_TRUE(r.toMap().isEmpty());
}

TEST_F(UniversalProviderDispatchTest, ArgNestedMapWithListRoundTrip)
{
    // map -> list -> string. The recursive conversion is what makes this
    // work; a shallow wrap would empty the inner list on the consumer side
    // and on lastArg's inspection.
    QVariantMap input;
    input.insert(QStringLiteral("clients"),
                 QStringList{QStringLiteral("u1"), QStringLiteral("u2")});
    input.insert(QStringLiteral("count"), 2);
    QVariant r = p.callMethod(QStringLiteral("echo"), {QVariant(input)});

    // In-path: the provider sees the nested array as a real JSON array.
    ASSERT_TRUE(p.lastArg.is_object());
    ASSERT_TRUE(p.lastArg["clients"].is_array());
    ASSERT_EQ(p.lastArg["clients"].size(), 2u);
    EXPECT_EQ(p.lastArg["clients"][0].get<std::string>(), "u1");

    // Out-path: consumer's accessors work on the nested members too.
    const QVariantMap out = r.toMap();
    const QStringList clients = out.value(QStringLiteral("clients")).toStringList();
    ASSERT_EQ(clients.size(), 2);
    EXPECT_EQ(clients.at(0), QStringLiteral("u1"));
    EXPECT_EQ(clients.at(1), QStringLiteral("u2"));
    EXPECT_EQ(out.value(QStringLiteral("count")).toLongLong(), 2);
}

TEST_F(UniversalProviderDispatchTest, ArgNullRoundTrip)
{
    QVariant r = p.callMethod(QStringLiteral("echo"), {QVariant()});
    EXPECT_TRUE(p.lastArg.is_null());
    EXPECT_FALSE(r.isValid());
}
