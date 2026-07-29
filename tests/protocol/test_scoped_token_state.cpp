#include <gtest/gtest.h>

#include "logos_api_client.h"
#include "logos_instance.h"
#include "logos_mode.h"
#include "logos_provider_interface.h"
#include "module_proxy.h"
#include "remote_transport.h"
#include "token_manager.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QVariantList>

#include <atomic>
#include <chrono>
#include <thread>

namespace {

QCoreApplication* ensureApp()
{
    static int argc = 0;
    static char* argv[] = { nullptr };
    if (!QCoreApplication::instance())
        new QCoreApplication(argc, argv);
    return QCoreApplication::instance();
}

class PingProvider final : public LogosProviderObject {
public:
    explicit PingProvider(QString label)
        : m_label(std::move(label))
    {
    }

    QVariant callMethod(const QString& method, const QVariantList&) override
    {
        return method == QStringLiteral("ping") ? QVariant(m_label) : QVariant();
    }

    bool informModuleToken(const QString& moduleName, const QString& token) override
    {
        return m_proxy && m_proxy->saveToken(moduleName, token);
    }

    QJsonArray getMethods() override { return {}; }
    void setEventListener(EventCallback) override {}
    void init(void*) override {}
    QString providerName() const override { return QStringLiteral("lez_indexer_module"); }
    QString providerVersion() const override { return QStringLiteral("1.0.0"); }

    void bindProxy(ModuleProxy* proxy) { m_proxy = proxy; }

private:
    QString m_label;
    ModuleProxy* m_proxy = nullptr;
};

class ScopedCapabilityProvider final : public LogosProviderObject {
public:
    void bindTarget(const QString& instanceId, ModuleProxy* proxy)
    {
        m_targets.insert(instanceId, proxy);
    }

    void rejectScopedRequests(bool reject)
    {
        m_rejectScopedRequests = reject;
    }

    QVariant callMethod(const QString& method, const QVariantList& args) override
    {
        if (method == QStringLiteral("requestModule")) {
            ++m_legacyRequestCount;
            return QStringLiteral("legacy-token-must-not-be-used");
        }

        if (method == QStringLiteral("requestModuleScoped") && args.size() == 3) {
            const QString origin = args.at(0).toString();
            const QString target = args.at(1).toString();
            const QString instanceId = args.at(2).toString();
            ++m_scopedRequestCount;
            m_scopedRequests.push_back({origin, target, instanceId});
            if (m_rejectScopedRequests)
                return {};

            ModuleProxy* targetProxy = m_targets.value(instanceId, nullptr);
            if (!targetProxy || target != QStringLiteral("lez_indexer_module"))
                return {};

            const QString token = QStringLiteral("issued-%1").arg(instanceId);
            if (!targetProxy->saveToken(origin, token))
                return {};
            return token;
        }

        if (method == QStringLiteral("informModuleTokenScoped") && args.size() == 4) {
            ++m_scopedRegistrationCount;
            m_lastRegistration = args;
            return args.at(0).toString() == QStringLiteral("bootstrap-token");
        }

        return {};
    }

    bool informModuleToken(const QString&, const QString&) override { return true; }
    QJsonArray getMethods() override { return {}; }
    void setEventListener(EventCallback) override {}
    void init(void*) override {}
    QString providerName() const override { return QStringLiteral("capability_module"); }
    QString providerVersion() const override { return QStringLiteral("1.0.0"); }

    int legacyRequestCount() const { return m_legacyRequestCount; }
    int scopedRequestCount() const { return m_scopedRequestCount; }
    int scopedRegistrationCount() const { return m_scopedRegistrationCount; }
    const QList<QVariantList>& scopedRequests() const { return m_scopedRequests; }
    const QVariantList& lastRegistration() const { return m_lastRegistration; }

private:
    QHash<QString, ModuleProxy*> m_targets;
    bool m_rejectScopedRequests = false;
    int m_legacyRequestCount = 0;
    int m_scopedRequestCount = 0;
    int m_scopedRegistrationCount = 0;
    QList<QVariantList> m_scopedRequests;
    QVariantList m_lastRegistration;
};

} // namespace

class ScopedTokenStateTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        ensureApp();
        m_savedMode = LogosModeConfig::getMode();
        LogosModeConfig::setMode(LogosMode::Remote);
        qputenv("LOGOS_INSTANCE_ID", "scoped_token_state_test");
        TokenManager::instance().clearAllTokens();
    }

    void TearDown() override
    {
        TokenManager::instance().clearAllTokens();
        LogosModeConfig::setMode(m_savedMode);
        qunsetenv("LOGOS_INSTANCE_ID");
    }

    void waitForConnected(LogosAPIClient& client)
    {
        for (int i = 0; i < 100 && !client.isConnected(); ++i) {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        ASSERT_TRUE(client.isConnected());
    }

    void authorizeCapability(ModuleProxy& proxy, const QString& origin)
    {
        const QString bootstrap = QStringLiteral("bootstrap-token");
        TokenManager::instance().saveToken(QStringLiteral("capability_module"), bootstrap);
        ASSERT_TRUE(proxy.saveToken(origin, bootstrap));
    }

    LogosMode m_savedMode;
};

TEST(ScopedModuleTokenKeyTest, LengthDelimitedKeyCannotAliasAndIsNotLegacyName)
{
    const QString first = logos::scopedModuleTokenKey(QStringLiteral("a:b"),
                                                       QStringLiteral("c"));
    const QString second = logos::scopedModuleTokenKey(QStringLiteral("a"),
                                                        QStringLiteral("b:c"));
    const QString scoped = logos::scopedModuleTokenKey(
        QStringLiteral("lez_indexer_module"), QStringLiteral("zone_alpha"));

    EXPECT_NE(first, second);
    EXPECT_NE(scoped, QStringLiteral("lez_indexer_module"));
}

TEST_F(ScopedTokenStateTest, SameModuleDifferentInstancesUseIndependentScopedTokens)
{
    const QString origin = QStringLiteral("basecamp_host");
    RemoteTransportHost capabilityHost(LogosInstance::id("capability_module"));
    RemoteTransportHost alphaHost(LogosInstance::id("lez_indexer_module", "zone_alpha"));
    RemoteTransportHost betaHost(LogosInstance::id("lez_indexer_module", "zone_beta"));

    ScopedCapabilityProvider capabilityProvider;
    ModuleProxy capabilityProxy(&capabilityProvider);
    authorizeCapability(capabilityProxy, origin);

    PingProvider alphaProvider(QStringLiteral("zone_alpha"));
    ModuleProxy alphaProxy(&alphaProvider);
    alphaProvider.bindProxy(&alphaProxy);
    capabilityProvider.bindTarget(QStringLiteral("zone_alpha"), &alphaProxy);

    PingProvider betaProvider(QStringLiteral("zone_beta"));
    ModuleProxy betaProxy(&betaProvider);
    betaProvider.bindProxy(&betaProxy);
    capabilityProvider.bindTarget(QStringLiteral("zone_beta"), &betaProxy);

    ASSERT_TRUE(capabilityHost.publishObject("capability_module", &capabilityProxy));
    ASSERT_TRUE(alphaHost.publishObject("lez_indexer_module", &alphaProxy));
    ASSERT_TRUE(betaHost.publishObject("lez_indexer_module", &betaProxy));

    LogosAPIClient alpha(QStringLiteral("lez_indexer_module"), origin,
                          &TokenManager::instance(), QStringLiteral("zone_alpha"));
    LogosAPIClient beta(QStringLiteral("lez_indexer_module"), origin,
                         &TokenManager::instance(), QStringLiteral("zone_beta"));
    waitForConnected(alpha);
    waitForConnected(beta);

    EXPECT_EQ(alpha.invokeRemoteMethod(QStringLiteral("lez_indexer_module"),
                                       QStringLiteral("ping"), QVariantList{}).toString(),
              QStringLiteral("zone_alpha"));
    EXPECT_EQ(beta.invokeRemoteMethod(QStringLiteral("lez_indexer_module"),
                                      QStringLiteral("ping"), QVariantList{}).toString(),
              QStringLiteral("zone_beta"));

    EXPECT_EQ(capabilityProvider.legacyRequestCount(), 0);
    EXPECT_EQ(capabilityProvider.scopedRequestCount(), 2);
    ASSERT_EQ(capabilityProvider.scopedRequests().size(), 2);
    EXPECT_EQ(capabilityProvider.scopedRequests().at(0).at(2).toString(),
              QStringLiteral("zone_alpha"));
    EXPECT_EQ(capabilityProvider.scopedRequests().at(1).at(2).toString(),
              QStringLiteral("zone_beta"));

    const QString alphaKey = logos::scopedModuleTokenKey(
        QStringLiteral("lez_indexer_module"), QStringLiteral("zone_alpha"));
    const QString betaKey = logos::scopedModuleTokenKey(
        QStringLiteral("lez_indexer_module"), QStringLiteral("zone_beta"));
    EXPECT_EQ(TokenManager::instance().getToken(alphaKey), QStringLiteral("issued-zone_alpha"));
    EXPECT_EQ(TokenManager::instance().getToken(betaKey), QStringLiteral("issued-zone_beta"));
    EXPECT_TRUE(TokenManager::instance().getToken(QStringLiteral("lez_indexer_module")).isEmpty());
}

TEST_F(ScopedTokenStateTest, AsyncHandshakesCoalesceWithinButNotAcrossInstances)
{
    const QString origin = QStringLiteral("basecamp_host");
    RemoteTransportHost capabilityHost(LogosInstance::id("capability_module"));
    RemoteTransportHost alphaHost(LogosInstance::id("lez_indexer_module", "zone_alpha"));
    RemoteTransportHost betaHost(LogosInstance::id("lez_indexer_module", "zone_beta"));

    ScopedCapabilityProvider capabilityProvider;
    ModuleProxy capabilityProxy(&capabilityProvider);
    authorizeCapability(capabilityProxy, origin);

    PingProvider alphaProvider(QStringLiteral("zone_alpha"));
    ModuleProxy alphaProxy(&alphaProvider);
    alphaProvider.bindProxy(&alphaProxy);
    capabilityProvider.bindTarget(QStringLiteral("zone_alpha"), &alphaProxy);

    PingProvider betaProvider(QStringLiteral("zone_beta"));
    ModuleProxy betaProxy(&betaProvider);
    betaProvider.bindProxy(&betaProxy);
    capabilityProvider.bindTarget(QStringLiteral("zone_beta"), &betaProxy);

    ASSERT_TRUE(capabilityHost.publishObject("capability_module", &capabilityProxy));
    ASSERT_TRUE(alphaHost.publishObject("lez_indexer_module", &alphaProxy));
    ASSERT_TRUE(betaHost.publishObject("lez_indexer_module", &betaProxy));

    LogosAPIClient alpha(QStringLiteral("lez_indexer_module"), origin,
                          &TokenManager::instance(), QStringLiteral("zone_alpha"));
    LogosAPIClient beta(QStringLiteral("lez_indexer_module"), origin,
                         &TokenManager::instance(), QStringLiteral("zone_beta"));
    waitForConnected(alpha);
    waitForConnected(beta);

    std::atomic<int> completed{0};
    std::atomic<int> alphaResponses{0};
    std::atomic<int> betaResponses{0};
    const auto collectAlpha = [&](QVariant result) {
        if (result.toString() == QStringLiteral("zone_alpha"))
            ++alphaResponses;
        ++completed;
    };
    const auto collectBeta = [&](QVariant result) {
        if (result.toString() == QStringLiteral("zone_beta"))
            ++betaResponses;
        ++completed;
    };

    alpha.invokeRemoteMethodAsync(QStringLiteral("lez_indexer_module"),
                                  QStringLiteral("ping"), QVariantList{}, collectAlpha);
    alpha.invokeRemoteMethodAsync(QStringLiteral("lez_indexer_module"),
                                  QStringLiteral("ping"), QVariantList{}, collectAlpha);
    beta.invokeRemoteMethodAsync(QStringLiteral("lez_indexer_module"),
                                 QStringLiteral("ping"), QVariantList{}, collectBeta);

    for (int i = 0; i < 200 && completed.load() != 3; ++i) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(completed.load(), 3);
    EXPECT_EQ(alphaResponses.load(), 2);
    EXPECT_EQ(betaResponses.load(), 1);
    EXPECT_EQ(capabilityProvider.legacyRequestCount(), 0);
    EXPECT_EQ(capabilityProvider.scopedRequestCount(), 2);
}

TEST_F(ScopedTokenStateTest, ScopedRequestFailureNeverDowngradesToDefaultCapabilityMethod)
{
    const QString origin = QStringLiteral("basecamp_host");
    RemoteTransportHost capabilityHost(LogosInstance::id("capability_module"));
    RemoteTransportHost targetHost(LogosInstance::id("lez_indexer_module", "zone_alpha"));

    ScopedCapabilityProvider capabilityProvider;
    capabilityProvider.rejectScopedRequests(true);
    ModuleProxy capabilityProxy(&capabilityProvider);
    authorizeCapability(capabilityProxy, origin);

    PingProvider targetProvider(QStringLiteral("zone_alpha"));
    ModuleProxy targetProxy(&targetProvider);
    targetProvider.bindProxy(&targetProxy);
    capabilityProvider.bindTarget(QStringLiteral("zone_alpha"), &targetProxy);

    ASSERT_TRUE(capabilityHost.publishObject("capability_module", &capabilityProxy));
    ASSERT_TRUE(targetHost.publishObject("lez_indexer_module", &targetProxy));

    LogosAPIClient client(QStringLiteral("lez_indexer_module"), origin,
                          &TokenManager::instance(), QStringLiteral("zone_alpha"));
    waitForConnected(client);

    const QVariant result = client.invokeRemoteMethod(QStringLiteral("lez_indexer_module"),
                                                       QStringLiteral("ping"), QVariantList{});
    EXPECT_FALSE(result.isValid());
    EXPECT_EQ(capabilityProvider.legacyRequestCount(), 0);
    EXPECT_GE(capabilityProvider.scopedRequestCount(), 1);
    EXPECT_TRUE(TokenManager::instance().getToken(logos::scopedModuleTokenKey(
        QStringLiteral("lez_indexer_module"), QStringLiteral("zone_alpha"))).isEmpty());
}

TEST_F(ScopedTokenStateTest, ScopedBootstrapRegistrationUsesExplicitCapabilityMethod)
{
    const QString origin = QStringLiteral("core");
    RemoteTransportHost capabilityHost(LogosInstance::id("capability_module"));

    ScopedCapabilityProvider capabilityProvider;
    ModuleProxy capabilityProxy(&capabilityProvider);
    authorizeCapability(capabilityProxy, origin);
    ASSERT_TRUE(capabilityHost.publishObject("capability_module", &capabilityProxy));

    LogosAPIClient capabilityClient(QStringLiteral("capability_module"), origin,
                                    &TokenManager::instance());
    waitForConnected(capabilityClient);

    EXPECT_TRUE(capabilityClient.informModuleTokenScoped(
        QStringLiteral("bootstrap-token"),
        QStringLiteral("lez_indexer_module"),
        QStringLiteral("zone_alpha"),
        QStringLiteral("target-bootstrap-token")));
    EXPECT_EQ(capabilityProvider.scopedRegistrationCount(), 1);
    ASSERT_EQ(capabilityProvider.lastRegistration().size(), 4);
    EXPECT_EQ(capabilityProvider.lastRegistration().at(0).toString(),
              QStringLiteral("bootstrap-token"));
    EXPECT_EQ(capabilityProvider.lastRegistration().at(1).toString(),
              QStringLiteral("lez_indexer_module"));
    EXPECT_EQ(capabilityProvider.lastRegistration().at(2).toString(),
              QStringLiteral("zone_alpha"));

    // Generic RPC accepts any issued token. The scoped provider must also see
    // the envelope token and reject a business credential that is not its
    // trusted bootstrap channel.
    ASSERT_TRUE(capabilityProxy.saveToken(QStringLiteral("another_module"),
                                          QStringLiteral("business-token")));
    EXPECT_FALSE(capabilityClient.informModuleTokenScoped(
        QStringLiteral("business-token"),
        QStringLiteral("lez_indexer_module"),
        QStringLiteral("zone_beta"),
        QStringLiteral("target-bootstrap-token")));
    EXPECT_EQ(capabilityProvider.scopedRegistrationCount(), 2);
    EXPECT_EQ(capabilityProvider.lastRegistration().at(0).toString(),
              QStringLiteral("business-token"));
}
