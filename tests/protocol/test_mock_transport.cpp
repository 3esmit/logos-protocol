#include <gtest/gtest.h>
#include "mock_transport.h"
#include "mock_store.h"

class MockTransportTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        MockStore::instance().reset();
    }
};

// -- MockTransportHost --

TEST_F(MockTransportTest, HostPublishObjectIsNoOp)
{
    MockTransportHost host;
    EXPECT_TRUE(host.publishObject("module", nullptr));
}

TEST_F(MockTransportTest, HostUnpublishObjectIsNoOp)
{
    MockTransportHost host;
    // Should not crash
    host.unpublishObject("module");
}

// -- MockTransportConnection --

TEST_F(MockTransportTest, ConnectionAlwaysConnected)
{
    MockTransportConnection conn;
    EXPECT_TRUE(conn.isConnected());
}

TEST_F(MockTransportTest, ConnectToHostReturnsTrue)
{
    MockTransportConnection conn;
    EXPECT_TRUE(conn.connectToHost());
}

TEST_F(MockTransportTest, ReconnectReturnsTrue)
{
    MockTransportConnection conn;
    EXPECT_TRUE(conn.reconnect());
}

TEST_F(MockTransportTest, RequestObjectReturnsMockObject)
{
    MockTransportConnection conn;
    LogosObject* obj = conn.requestObject("test_mod", 5000);
    ASSERT_NE(obj, nullptr);
    obj->release();
}

// -- MockLogosObject --

TEST_F(MockTransportTest, CallMethodDelegatesToMockStore)
{
    MockStore::instance().when("test_mod", "fn").thenReturn(QVariant(42));

    MockLogosObject obj("test_mod");
    QVariant r = obj.callMethod("auth", "fn", {}, 5000);
    EXPECT_EQ(r.toInt(), 42);
    EXPECT_TRUE(MockStore::instance().wasCalled("test_mod", "fn"));
}

TEST_F(MockTransportTest, CallMethodRecordsArgs)
{
    MockLogosObject obj("mod");
    obj.callMethod("auth", "fn", {QVariant("a"), QVariant("b")}, 5000);
    EXPECT_TRUE(MockStore::instance().wasCalledWith("mod", "fn", {QVariant("a"), QVariant("b")}));
}

TEST_F(MockTransportTest, InformModuleTokenReturnsTrue)
{
    MockLogosObject obj("mod");
    EXPECT_TRUE(obj.informModuleToken("auth", "target", "tok", 5000));
}

TEST_F(MockTransportTest, EventOperationsAreNoOps)
{
    MockLogosObject obj("mod");
    // Should not crash
    obj.onEvent("event", [](const QString&, const QVariantList&) {});
    obj.disconnectEvents();
    obj.emitEvent("event", {QVariant(1)});
}

TEST_F(MockTransportTest, GetMethodsReturnsEmptyArray)
{
    MockLogosObject obj("mod");
    EXPECT_TRUE(obj.getMethods().isEmpty());
}

TEST_F(MockTransportTest, IdReturnsNonZero)
{
    MockLogosObject obj("mod");
    EXPECT_NE(obj.id(), quintptr(0));
}

TEST_F(MockTransportTest, ModuleName)
{
    MockLogosObject obj("my_module");
    EXPECT_EQ(obj.moduleName(), "my_module");
}
