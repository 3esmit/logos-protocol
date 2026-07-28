#include <gtest/gtest.h>

#include "logos_api_client.h"
#include "logos_api_consumer.h"
#include "logos_instance.h"
#include "logos_mode.h"
#include "logos_transport_config.h"
#include "mock_transport.h"
#include "token_manager.h"

class InstanceEndpointTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_savedMode = LogosModeConfig::getMode();
        LogosModeConfig::setMode(LogosMode::Mock);
        qputenv("LOGOS_INSTANCE_ID", "shared_root");
    }

    void TearDown() override
    {
        LogosModeConfig::setMode(m_savedMode);
        qunsetenv("LOGOS_INSTANCE_ID");
    }

    LogosMode m_savedMode;
};

TEST_F(InstanceEndpointTest, ConsumerSeparatesTargetEndpointFromLogicalName)
{
    LogosTransportConfig transport;
    TokenManager* tokens = &TokenManager::instance();

    LogosAPIConsumer first("lez_indexer_module", "core", tokens,
                            transport, "zone_alpha");
    LogosAPIConsumer second("lez_indexer_module", "core", tokens,
                             transport, "zone_beta");
    LogosAPIConsumer legacy("lez_indexer_module", "core", tokens, transport);

    EXPECT_EQ(first.registryUrl(), "local:logos_lez_indexer_module_zone_alpha");
    EXPECT_EQ(second.registryUrl(), "local:logos_lez_indexer_module_zone_beta");
    EXPECT_EQ(legacy.registryUrl(), "local:logos_lez_indexer_module_shared_root");
    EXPECT_NE(first.registryUrl(), second.registryUrl());
    EXPECT_NE(first.registryUrl(), legacy.registryUrl());
}

TEST_F(InstanceEndpointTest, DefaultTransportEndpointHelpersUseExplicitInstance)
{
    MockTransportHost host;
    MockTransportConnection connection;

    EXPECT_EQ(host.bindUrl("zone_alpha", "lez_indexer_module"),
              "local:logos_lez_indexer_module_zone_alpha");
    EXPECT_EQ(connection.endpointUrl("zone_beta", "lez_indexer_module"),
              "local:logos_lez_indexer_module_zone_beta");
}

TEST_F(InstanceEndpointTest, ClientForwardsTargetInstanceToTargetConsumer)
{
    LogosTransportConfig transport;
    LogosAPIClient client("lez_indexer_module", "core",
                          &TokenManager::instance(),
                          transport, transport, "zone_alpha");

    EXPECT_EQ(client.registryUrl(), "local:logos_lez_indexer_module_zone_alpha");
    EXPECT_TRUE(client.isConnected());
}
