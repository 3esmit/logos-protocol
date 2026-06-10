#ifndef MOCK_STORE_H
#define MOCK_STORE_H

#include <atomic>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QList>
#include <QMutex>

class MockLogosObject;

/**
 * @brief Records a single intercepted call to a mocked module method.
 */
struct MockCallRecord {
    QString module;
    QString method;
    QVariantList args;
};

/**
 * @brief Stores a single configured expectation (module + method -> return value).
 *
 * If matchAnyArgs is true the expectation matches regardless of arguments.
 * Otherwise it only matches when args equal expectedArgs exactly.
 */
struct MockExpectation {
    QString module;
    QString method;
    QVariantList expectedArgs;
    QVariant returnValue;
    bool matchAnyArgs = true;
};

/**
 * @brief Singleton store that holds mock expectations and records calls.
 *
 * MockStore is the central registry used by MockTransportConnection.
 * Tests set up expectations via when() and verify them via wasCalled() etc.
 * Call reset() at the start of every test to clear state from previous runs.
 */
class MockStore {
public:
    static MockStore& instance();

    /**
     * @brief Remove all expectations and call records.
     */
    void reset();

    // ── Fluent expectation builder ───────────────────────────────────────────

    class ExpectationBuilder {
    public:
        ExpectationBuilder(MockStore& store, const QString& module, const QString& method);

        /**
         * @brief Restrict this expectation to calls with exactly these arguments.
         */
        ExpectationBuilder& withArgs(const QVariantList& args);

        /**
         * @brief Set the value returned when the expectation is matched.
         */
        ExpectationBuilder& thenReturn(const QVariant& value);

    private:
        MockStore& m_store;
        int m_index;  // index into m_expectations
    };

    /**
     * @brief Begin configuring an expectation for module::method.
     *
     * Multiple calls to when() for the same module/method are allowed; the
     * last matching expectation wins (LIFO order).
     */
    ExpectationBuilder when(const QString& module, const QString& method);

    // ── Called by MockTransportConnection ───────────────────────────────────

    /**
     * @brief Record a call and return the configured return value.
     *
     * If no expectation matches an invalid QVariant() is returned.
     */
    QVariant recordAndReturn(const QString& module, const QString& method,
                             const QVariantList& args);

    // ── Verification helpers ─────────────────────────────────────────────────

    bool wasCalled(const QString& module, const QString& method) const;
    bool wasCalledWith(const QString& module, const QString& method,
                       const QVariantList& args) const;
    int callCount(const QString& module, const QString& method) const;
    QVariantList lastArgs(const QString& module, const QString& method) const;
    QList<MockCallRecord> allCalls() const;

    /**
     * @brief When non-null, MockLogosObject::release() increments *counter exactly once.
     *
     * Used by sdk_tests to assert LogosAPIConsumer invokes the async user callback before
     * calling plugin->release(). Cleared by reset().
     */
    void setMockObjectReleaseProbe(std::atomic<int>* counter);

private:
    friend class MockLogosObject;
    void incrementMockObjectReleaseProbeIfSet();

    MockStore() = default;
    MockStore(const MockStore&) = delete;
    MockStore& operator=(const MockStore&) = delete;

    mutable QMutex m_mutex;
    QList<MockExpectation> m_expectations;
    QList<MockCallRecord> m_calls;
    std::atomic<int>* m_mockObjectReleaseProbe = nullptr;

    friend class ExpectationBuilder;
};

#endif // MOCK_STORE_H
