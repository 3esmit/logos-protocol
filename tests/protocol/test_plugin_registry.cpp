#include <gtest/gtest.h>
#include "plugin_registry.h"

class PluginRegistryTest : public ::testing::Test {
protected:
    void TearDown() override
    {
        // Clean up any registered plugins
        PluginRegistry::unregisterPlugin("test_plugin");
        PluginRegistry::unregisterPlugin("another_plugin");
    }
};

TEST_F(PluginRegistryTest, RegisterAndHasPlugin)
{
    QObject obj;
    PluginRegistry::registerPlugin(&obj, "test_plugin");
    EXPECT_TRUE(PluginRegistry::hasPlugin("test_plugin"));
}

TEST_F(PluginRegistryTest, UnregisterPlugin)
{
    QObject obj;
    PluginRegistry::registerPlugin(&obj, "test_plugin");
    EXPECT_TRUE(PluginRegistry::unregisterPlugin("test_plugin"));
    EXPECT_FALSE(PluginRegistry::hasPlugin("test_plugin"));
}

TEST_F(PluginRegistryTest, GetPluginReturnsCorrectObject)
{
    QObject obj;
    PluginRegistry::registerPlugin(&obj, "test_plugin");
    QObject* retrieved = PluginRegistry::getPlugin<QObject>("test_plugin");
    EXPECT_EQ(retrieved, &obj);
}

TEST_F(PluginRegistryTest, GetPluginReturnsNullForMissing)
{
    QObject* retrieved = PluginRegistry::getPlugin<QObject>("nonexistent");
    EXPECT_EQ(retrieved, nullptr);
}

TEST_F(PluginRegistryTest, HasPluginReturnsFalseForMissing)
{
    EXPECT_FALSE(PluginRegistry::hasPlugin("nonexistent"));
}

TEST_F(PluginRegistryTest, EmptyNameGuards)
{
    EXPECT_FALSE(PluginRegistry::hasPlugin(""));
    EXPECT_FALSE(PluginRegistry::unregisterPlugin(""));
    EXPECT_EQ(PluginRegistry::getPlugin<QObject>(""), nullptr);
}

TEST_F(PluginRegistryTest, KeyNormalization)
{
    EXPECT_EQ(PluginRegistry::toPluginKey("My Module"),
              "logos_plugin_my_module");
    EXPECT_EQ(PluginRegistry::toPluginKey("TEST"),
              "logos_plugin_test");
}

TEST_F(PluginRegistryTest, MultiplePlugins)
{
    QObject obj1, obj2;
    PluginRegistry::registerPlugin(&obj1, "test_plugin");
    PluginRegistry::registerPlugin(&obj2, "another_plugin");
    EXPECT_TRUE(PluginRegistry::hasPlugin("test_plugin"));
    EXPECT_TRUE(PluginRegistry::hasPlugin("another_plugin"));
    EXPECT_NE(PluginRegistry::getPlugin<QObject>("test_plugin"),
              PluginRegistry::getPlugin<QObject>("another_plugin"));
}

TEST_F(PluginRegistryTest, OverwritePlugin)
{
    QObject obj1, obj2;
    PluginRegistry::registerPlugin(&obj1, "test_plugin");
    PluginRegistry::registerPlugin(&obj2, "test_plugin");
    EXPECT_EQ(PluginRegistry::getPlugin<QObject>("test_plugin"), &obj2);
}
