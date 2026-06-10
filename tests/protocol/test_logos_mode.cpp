#include <gtest/gtest.h>
#include "logos_mode.h"

class LogosModeTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_saved = LogosModeConfig::getMode();
    }
    void TearDown() override
    {
        LogosModeConfig::setMode(m_saved);
    }
private:
    LogosMode m_saved;
};

TEST_F(LogosModeTest, DefaultIsRemote)
{
    LogosModeConfig::setMode(LogosMode::Remote);
    EXPECT_EQ(LogosModeConfig::getMode(), LogosMode::Remote);
    EXPECT_TRUE(LogosModeConfig::isRemote());
    EXPECT_FALSE(LogosModeConfig::isLocal());
    EXPECT_FALSE(LogosModeConfig::isMock());
}

TEST_F(LogosModeTest, SetLocal)
{
    LogosModeConfig::setMode(LogosMode::Local);
    EXPECT_EQ(LogosModeConfig::getMode(), LogosMode::Local);
    EXPECT_TRUE(LogosModeConfig::isLocal());
    EXPECT_FALSE(LogosModeConfig::isRemote());
    EXPECT_FALSE(LogosModeConfig::isMock());
}

TEST_F(LogosModeTest, SetMock)
{
    LogosModeConfig::setMode(LogosMode::Mock);
    EXPECT_EQ(LogosModeConfig::getMode(), LogosMode::Mock);
    EXPECT_TRUE(LogosModeConfig::isMock());
    EXPECT_FALSE(LogosModeConfig::isRemote());
    EXPECT_FALSE(LogosModeConfig::isLocal());
}

TEST_F(LogosModeTest, SwitchBetweenModes)
{
    LogosModeConfig::setMode(LogosMode::Local);
    EXPECT_TRUE(LogosModeConfig::isLocal());

    LogosModeConfig::setMode(LogosMode::Mock);
    EXPECT_TRUE(LogosModeConfig::isMock());

    LogosModeConfig::setMode(LogosMode::Remote);
    EXPECT_TRUE(LogosModeConfig::isRemote());
}
