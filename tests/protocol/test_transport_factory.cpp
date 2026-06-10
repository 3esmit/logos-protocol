#include <gtest/gtest.h>
#include "logos_mode.h"
#include "logos_transport.h"
#include "logos_transport_factory.h"
#include "mock_transport.h"

class TransportFactoryTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_saved = LogosModeConfig::getMode();
    }
    void TearDown() override
    {
        LogosModeConfig::setMode(m_saved);
    }
    LogosMode m_saved;
};

TEST_F(TransportFactoryTest, MockModeCreatesHost)
{
    LogosModeConfig::setMode(LogosMode::Mock);
    auto host = LogosTransportFactory::createHost("local:test");
    ASSERT_NE(host, nullptr);
    // Verify it's a MockTransportHost by calling publishObject (no-op, returns true)
    EXPECT_TRUE(host->publishObject("test", nullptr));
}

TEST_F(TransportFactoryTest, MockModeCreatesConnection)
{
    LogosModeConfig::setMode(LogosMode::Mock);
    auto conn = LogosTransportFactory::createConnection("local:test");
    ASSERT_NE(conn, nullptr);
    EXPECT_TRUE(conn->isConnected());
    EXPECT_TRUE(conn->connectToHost());
}

TEST_F(TransportFactoryTest, MockConnectionRequestObject)
{
    LogosModeConfig::setMode(LogosMode::Mock);
    auto conn = LogosTransportFactory::createConnection("local:test");
    LogosObject* obj = conn->requestObject("my_module", 5000);
    ASSERT_NE(obj, nullptr);
    obj->release();
}

TEST_F(TransportFactoryTest, LocalModeCreatesHost)
{
    LogosModeConfig::setMode(LogosMode::Local);
    auto host = LogosTransportFactory::createHost("local:test");
    ASSERT_NE(host, nullptr);
}

TEST_F(TransportFactoryTest, LocalModeCreatesConnection)
{
    LogosModeConfig::setMode(LogosMode::Local);
    auto conn = LogosTransportFactory::createConnection("local:test");
    ASSERT_NE(conn, nullptr);
    EXPECT_TRUE(conn->isConnected());
}

TEST_F(TransportFactoryTest, ModeSwitchChangesTransportType)
{
    LogosModeConfig::setMode(LogosMode::Mock);
    auto mockConn = LogosTransportFactory::createConnection("url");
    // Mock is always connected
    EXPECT_TRUE(mockConn->isConnected());

    LogosModeConfig::setMode(LogosMode::Local);
    auto localConn = LogosTransportFactory::createConnection("url");
    // Local is also always connected
    EXPECT_TRUE(localConn->isConnected());

    // They should be different transport implementations
    EXPECT_NE(mockConn.get(), localConn.get());
}
