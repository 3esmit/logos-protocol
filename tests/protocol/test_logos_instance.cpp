#include <gtest/gtest.h>
#include "logos_instance.h"

class LogosInstanceTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Clear env so we get a fresh ID
        qunsetenv("LOGOS_INSTANCE_ID");
    }
    void TearDown() override
    {
        qunsetenv("LOGOS_INSTANCE_ID");
    }
};

TEST_F(LogosInstanceTest, IdReturnsNonEmpty)
{
    QString id = LogosInstance::id();
    EXPECT_FALSE(id.isEmpty());
}

TEST_F(LogosInstanceTest, IdIs12Chars)
{
    QString id = LogosInstance::id();
    EXPECT_EQ(id.length(), 12);
}

TEST_F(LogosInstanceTest, IdIsStableAcrossCalls)
{
    QString id1 = LogosInstance::id();
    QString id2 = LogosInstance::id();
    EXPECT_EQ(id1, id2);
}

TEST_F(LogosInstanceTest, IdSetsEnvVar)
{
    QString id = LogosInstance::id();
    QByteArray env = qgetenv("LOGOS_INSTANCE_ID");
    EXPECT_EQ(QString::fromUtf8(env), id);
}

TEST_F(LogosInstanceTest, IdRespectsExistingEnvVar)
{
    qputenv("LOGOS_INSTANCE_ID", "custom_id_123");
    QString id = LogosInstance::id();
    EXPECT_EQ(id, "custom_id_123");
}

TEST_F(LogosInstanceTest, RegistryUrlFormat)
{
    qputenv("LOGOS_INSTANCE_ID", "abc123def456");
    QString url = LogosInstance::id("my_module");
    EXPECT_EQ(url, "local:logos_my_module_abc123def456");
}
