#include <gtest/gtest.h>
#include "mock_store.h"

class MockStoreTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        MockStore::instance().reset();
    }
};

TEST_F(MockStoreTest, WhenThenReturn)
{
    MockStore::instance().when("mod", "method").thenReturn(QVariant(42));

    QVariant result = MockStore::instance().recordAndReturn("mod", "method", {});
    EXPECT_EQ(result.toInt(), 42);
}

TEST_F(MockStoreTest, WithArgsMatching)
{
    MockStore::instance().when("mod", "greet")
        .withArgs({QVariant("hello")})
        .thenReturn(QVariant("world"));

    QVariant hit = MockStore::instance().recordAndReturn("mod", "greet", {QVariant("hello")});
    EXPECT_EQ(hit.toString(), "world");

    QVariant miss = MockStore::instance().recordAndReturn("mod", "greet", {QVariant("bye")});
    EXPECT_FALSE(miss.isValid());
}

TEST_F(MockStoreTest, MatchAnyArgsByDefault)
{
    MockStore::instance().when("mod", "fn").thenReturn(QVariant(true));

    QVariant r = MockStore::instance().recordAndReturn("mod", "fn", {QVariant(1), QVariant(2)});
    EXPECT_TRUE(r.toBool());
}

TEST_F(MockStoreTest, LIFOOrder)
{
    MockStore::instance().when("mod", "fn").thenReturn(QVariant("first"));
    MockStore::instance().when("mod", "fn").thenReturn(QVariant("second"));

    QVariant r = MockStore::instance().recordAndReturn("mod", "fn", {});
    EXPECT_EQ(r.toString(), "second");
}

TEST_F(MockStoreTest, NoMatchReturnsInvalid)
{
    QVariant r = MockStore::instance().recordAndReturn("mod", "noSuchMethod", {});
    EXPECT_FALSE(r.isValid());
}

TEST_F(MockStoreTest, WasCalled)
{
    EXPECT_FALSE(MockStore::instance().wasCalled("mod", "fn"));

    MockStore::instance().recordAndReturn("mod", "fn", {});
    EXPECT_TRUE(MockStore::instance().wasCalled("mod", "fn"));
}

TEST_F(MockStoreTest, WasCalledWith)
{
    MockStore::instance().recordAndReturn("mod", "fn", {QVariant(10)});
    EXPECT_TRUE(MockStore::instance().wasCalledWith("mod", "fn", {QVariant(10)}));
    EXPECT_FALSE(MockStore::instance().wasCalledWith("mod", "fn", {QVariant(20)}));
}

TEST_F(MockStoreTest, CallCount)
{
    EXPECT_EQ(MockStore::instance().callCount("mod", "fn"), 0);
    MockStore::instance().recordAndReturn("mod", "fn", {});
    MockStore::instance().recordAndReturn("mod", "fn", {});
    MockStore::instance().recordAndReturn("mod", "fn", {});
    EXPECT_EQ(MockStore::instance().callCount("mod", "fn"), 3);
}

TEST_F(MockStoreTest, LastArgs)
{
    MockStore::instance().recordAndReturn("mod", "fn", {QVariant("a")});
    MockStore::instance().recordAndReturn("mod", "fn", {QVariant("b")});

    QVariantList last = MockStore::instance().lastArgs("mod", "fn");
    EXPECT_EQ(last.size(), 1);
    EXPECT_EQ(last.at(0).toString(), "b");
}

TEST_F(MockStoreTest, AllCalls)
{
    MockStore::instance().recordAndReturn("m1", "f1", {});
    MockStore::instance().recordAndReturn("m2", "f2", {QVariant(1)});

    auto calls = MockStore::instance().allCalls();
    EXPECT_EQ(calls.size(), 2);
    EXPECT_EQ(calls.at(0).module, "m1");
    EXPECT_EQ(calls.at(1).module, "m2");
}

TEST_F(MockStoreTest, ResetClearsEverything)
{
    MockStore::instance().when("mod", "fn").thenReturn(QVariant(1));
    MockStore::instance().recordAndReturn("mod", "fn", {});

    MockStore::instance().reset();

    EXPECT_FALSE(MockStore::instance().wasCalled("mod", "fn"));
    QVariant r = MockStore::instance().recordAndReturn("mod", "fn", {});
    EXPECT_FALSE(r.isValid());
}
