#ifndef LOGOS_MOCK_H
#define LOGOS_MOCK_H

/**
 * @file logos_mock.h
 * @brief Convenience header for unit tests that use the mock transport.
 *
 * Include this file in test source files.  It pulls in everything needed to
 * set up mock expectations and verify calls:
 *
 *   #include "logos_mock.h"
 *
 *   LOGOS_TEST(my_test) {
 *       LogosMockSetup mock;
 *       mock.when("other_module", "someMethod").thenReturn(QVariant(42));
 *
 *       // ... exercise code under test ...
 *
 *       LOGOS_ASSERT(mock.wasCalled("other_module", "someMethod"));
 *   }
 */

#include "../../logos_mode.h"
#include "../../token_manager.h"
#include "mock_store.h"
#include <QString>
#include <QVariant>
#include <QVariantList>

/**
 * @brief RAII guard that activates mock mode for the duration of a test.
 *
 * Construction:
 *  - Switches the SDK to LogosMode::Mock.
 *  - Resets MockStore (clears all expectations and call records).
 *  - Clears the TokenManager singleton so that no stale tokens from
 *    previous tests influence token lookup in LogosAPIClient.
 *
 * Destruction:
 *  - Restores the previous LogosMode.
 *  - Resets MockStore again (belt-and-suspenders cleanup).
 *
 * Token pre-seeding:
 *  when() automatically registers a non-empty dummy token for the target
 *  module so that LogosAPIClient::invokeRemoteMethod() does not attempt to
 *  call capability_module.requestModule() before invoking the mock.
 */
class LogosMockSetup {
public:
    LogosMockSetup()
        : m_previousMode(LogosModeConfig::getMode())
    {
        LogosModeConfig::setMode(LogosMode::Mock);
        MockStore::instance().reset();
        TokenManager::instance().clearAllTokens();
    }

    ~LogosMockSetup()
    {
        MockStore::instance().reset();
        LogosModeConfig::setMode(m_previousMode);
    }

    /**
     * @brief Register a mock expectation for module::method.
     *
     * Also seeds a dummy token for the module so LogosAPIClient does not
     * try to call capability_module.requestModule().
     *
     * @return A fluent builder to configure arguments and return value.
     */
    MockStore::ExpectationBuilder when(const QString& module, const QString& method)
    {
        // Pre-seed a token so LogosAPIClient skips the capability_module lookup
        TokenManager::instance().saveToken(module, "mock-token-" + module);
        return MockStore::instance().when(module, method);
    }

    // ── Verification helpers (delegates to MockStore) ────────────────────────

    bool wasCalled(const QString& module, const QString& method) const
    {
        return MockStore::instance().wasCalled(module, method);
    }

    bool wasCalledWith(const QString& module, const QString& method,
                       const QVariantList& args) const
    {
        return MockStore::instance().wasCalledWith(module, method, args);
    }

    int callCount(const QString& module, const QString& method) const
    {
        return MockStore::instance().callCount(module, method);
    }

    QVariantList lastArgs(const QString& module, const QString& method) const
    {
        return MockStore::instance().lastArgs(module, method);
    }

private:
    LogosMode m_previousMode;
};

#endif // LOGOS_MOCK_H
