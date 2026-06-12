#include <gtest/gtest.h>
#include "logos_mode.h"
#include "logos_registry.h"
#include "logos_registry_factory.h"

class RegistryFactoryTest : public ::testing::Test {
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

TEST_F(RegistryFactoryTest, MockModeCreatesInitializedRegistry)
{
    LogosModeConfig::setMode(LogosMode::Mock);
    auto reg = LogosRegistryFactory::create("local:test");
    ASSERT_NE(reg, nullptr);
    EXPECT_TRUE(reg->isInitialized());
}

TEST_F(RegistryFactoryTest, LocalModeCreatesInitializedRegistry)
{
    LogosModeConfig::setMode(LogosMode::Local);
    auto reg = LogosRegistryFactory::create("local:test");
    ASSERT_NE(reg, nullptr);
    EXPECT_TRUE(reg->isInitialized());
}

TEST_F(RegistryFactoryTest, ModeSwitchProducesDifferentRegistries)
{
    LogosModeConfig::setMode(LogosMode::Mock);
    auto mockReg = LogosRegistryFactory::create("url");

    LogosModeConfig::setMode(LogosMode::Local);
    auto localReg = LogosRegistryFactory::create("url");

    // Both initialized, but different objects
    EXPECT_TRUE(mockReg->isInitialized());
    EXPECT_TRUE(localReg->isInitialized());
    EXPECT_NE(mockReg.get(), localReg.get());
}
