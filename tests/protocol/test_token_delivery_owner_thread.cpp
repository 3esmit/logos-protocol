// Regression coverage for token delivery from a foreign caller thread.
//
// LogosAPIClient owns its consumer and transport on the thread that constructs
// it. Token delivery used to bypass that ownership boundary and call directly
// into the consumer, allowing a worker to drive Qt transport state. Local mode
// makes the boundary observable without a socket: the registered provider must
// receive the delivery on the client's owner thread.

#include <gtest/gtest.h>

#include "logos_api_client.h"
#include "logos_mode.h"
#include "logos_provider_interface.h"
#include "module_proxy.h"
#include "plugin_registry.h"
#include "token_manager.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QThread>

#include <atomic>
#include <chrono>
#include <optional>
#include <thread>

namespace {

class LocalModeScope {
public:
    LocalModeScope()
        : m_previousMode(LogosModeConfig::getMode())
    {
        LogosModeConfig::setMode(LogosMode::Local);
        TokenManager::instance().clearAllTokens();
    }

    ~LocalModeScope()
    {
        TokenManager::instance().clearAllTokens();
        LogosModeConfig::setMode(m_previousMode);
    }

private:
    LogosMode m_previousMode;
};

class LocalPluginRegistration {
public:
    LocalPluginRegistration(const QString& name, QObject* object)
        : m_name(name)
    {
        PluginRegistry::registerPlugin(object, m_name);
    }

    ~LocalPluginRegistration()
    {
        PluginRegistry::unregisterPlugin(m_name);
    }

private:
    QString m_name;
};

class ThreadCapturingProvider final : public LogosProviderObject {
public:
    QVariant callMethod(const QString& method, const QVariantList&) override
    {
        if (method == QLatin1String("informModuleTokenScoped")) {
            m_deliveryThread = QThread::currentThread();
            return true;
        }
        return QVariant();
    }

    bool informModuleToken(const QString&, const QString&) override
    {
        m_deliveryThread = QThread::currentThread();
        return true;
    }

    QJsonArray getMethods() override { return QJsonArray(); }
    void setEventListener(EventCallback) override {}
    void init(void*) override {}
    QString providerName() const override { return QStringLiteral("capability_module"); }
    QString providerVersion() const override { return QStringLiteral("1.0.0"); }

    QThread* deliveryThread() const { return m_deliveryThread; }

private:
    QThread* m_deliveryThread = nullptr;
};

template <typename Fn>
bool callFromWorkerAndPump(Fn&& fn)
{
    std::atomic<bool> complete{false};
    bool result = false;
    std::thread worker([&]() {
        result = fn();
        complete.store(true, std::memory_order_release);
    });

    QElapsedTimer timer;
    timer.start();
    while (!complete.load(std::memory_order_acquire) && timer.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    worker.join();
    return complete.load(std::memory_order_acquire) && result;
}

} // namespace

class TokenDeliveryOwnerThreadTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_localMode.emplace();
    }

    void TearDown() override
    {
        m_localMode.reset();
    }

private:
    std::optional<LocalModeScope> m_localMode;
};

TEST_F(TokenDeliveryOwnerThreadTest, InformModuleTokenRunsOnClientOwnerThread)
{
    ThreadCapturingProvider provider;
    ModuleProxy proxy(&provider);
    LocalPluginRegistration registration(QStringLiteral("capability_module"), &proxy);
    TokenManager::instance().saveToken(QStringLiteral("core"), QStringLiteral("trusted-token"));

    LogosAPIClient client(QStringLiteral("core"), QStringLiteral("core"),
                          &TokenManager::instance());
    QThread* const clientThread = client.thread();

    EXPECT_TRUE(callFromWorkerAndPump([&client]() {
        return client.informModuleToken(QStringLiteral("trusted-token"),
                                        QStringLiteral("accounts_module"),
                                        QStringLiteral("issued-token"));
    }));
    EXPECT_EQ(provider.deliveryThread(), clientThread);
}

TEST_F(TokenDeliveryOwnerThreadTest, InformModuleTokenModuleRunsOnClientOwnerThread)
{
    ThreadCapturingProvider provider;
    ModuleProxy proxy(&provider);
    LocalPluginRegistration registration(QStringLiteral("origin_module"), &proxy);
    TokenManager::instance().saveToken(QStringLiteral("core"), QStringLiteral("trusted-token"));

    LogosAPIClient client(QStringLiteral("core"), QStringLiteral("core"),
                          &TokenManager::instance());
    QThread* const clientThread = client.thread();

    EXPECT_TRUE(callFromWorkerAndPump([&client]() {
        return client.informModuleToken_module(QStringLiteral("trusted-token"),
                                               QStringLiteral("origin_module"),
                                               QStringLiteral("accounts_module"),
                                               QStringLiteral("issued-token"));
    }));
    EXPECT_EQ(provider.deliveryThread(), clientThread);
}

TEST_F(TokenDeliveryOwnerThreadTest, InformModuleTokenScopedRunsOnClientOwnerThread)
{
    ThreadCapturingProvider provider;
    ModuleProxy proxy(&provider);
    LocalPluginRegistration registration(QStringLiteral("capability_module"), &proxy);
    TokenManager::instance().saveToken(QStringLiteral("core"), QStringLiteral("trusted-token"));

    LogosAPIClient client(QStringLiteral("core"), QStringLiteral("core"),
                          &TokenManager::instance());
    QThread* const clientThread = client.thread();

    EXPECT_TRUE(callFromWorkerAndPump([&client]() {
        return client.informModuleTokenScoped(QStringLiteral("trusted-token"),
                                              QStringLiteral("accounts_module"),
                                              QStringLiteral("zone-alpha"),
                                              QStringLiteral("issued-token"));
    }));
    EXPECT_EQ(provider.deliveryThread(), clientThread);
}
