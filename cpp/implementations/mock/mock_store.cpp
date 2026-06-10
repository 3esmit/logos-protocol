#include "mock_store.h"
#include <QMutexLocker>
#include <QDebug>

MockStore& MockStore::instance()
{
    static MockStore s;
    return s;
}

void MockStore::reset()
{
    QMutexLocker lock(&m_mutex);
    m_expectations.clear();
    m_calls.clear();
    m_mockObjectReleaseProbe = nullptr;
}

void MockStore::setMockObjectReleaseProbe(std::atomic<int>* counter)
{
    QMutexLocker lock(&m_mutex);
    m_mockObjectReleaseProbe = counter;
}

void MockStore::incrementMockObjectReleaseProbeIfSet()
{
    QMutexLocker lock(&m_mutex);
    if (m_mockObjectReleaseProbe)
        ++(*m_mockObjectReleaseProbe);
}

// ── ExpectationBuilder ───────────────────────────────────────────────────────

MockStore::ExpectationBuilder::ExpectationBuilder(MockStore& store,
                                                  const QString& module,
                                                  const QString& method)
    : m_store(store)
{
    QMutexLocker lock(&store.m_mutex);
    MockExpectation exp;
    exp.module = module;
    exp.method = method;
    exp.matchAnyArgs = true;
    store.m_expectations.append(exp);
    m_index = store.m_expectations.size() - 1;
}

MockStore::ExpectationBuilder& MockStore::ExpectationBuilder::withArgs(const QVariantList& args)
{
    QMutexLocker lock(&m_store.m_mutex);
    m_store.m_expectations[m_index].expectedArgs = args;
    m_store.m_expectations[m_index].matchAnyArgs = false;
    return *this;
}

MockStore::ExpectationBuilder& MockStore::ExpectationBuilder::thenReturn(const QVariant& value)
{
    QMutexLocker lock(&m_store.m_mutex);
    m_store.m_expectations[m_index].returnValue = value;
    return *this;
}

// ── MockStore ────────────────────────────────────────────────────────────────

MockStore::ExpectationBuilder MockStore::when(const QString& module, const QString& method)
{
    return ExpectationBuilder(*this, module, method);
}

QVariant MockStore::recordAndReturn(const QString& module, const QString& method,
                                    const QVariantList& args)
{
    QMutexLocker lock(&m_mutex);

    MockCallRecord record;
    record.module = module;
    record.method = method;
    record.args   = args;
    m_calls.append(record);

    // Search expectations in reverse (last registered wins)
    for (int i = m_expectations.size() - 1; i >= 0; --i) {
        const MockExpectation& exp = m_expectations.at(i);
        if (exp.module != module || exp.method != method) continue;
        if (!exp.matchAnyArgs && exp.expectedArgs != args) continue;
        qDebug() << "MockStore: matched expectation for" << module << "::" << method
                 << "-> returning" << exp.returnValue;
        return exp.returnValue;
    }

    qWarning() << "MockStore: no expectation registered for" << module << "::" << method
               << "- returning invalid QVariant";
    return QVariant();
}

bool MockStore::wasCalled(const QString& module, const QString& method) const
{
    QMutexLocker lock(&m_mutex);
    for (const MockCallRecord& r : m_calls) {
        if (r.module == module && r.method == method) return true;
    }
    return false;
}

bool MockStore::wasCalledWith(const QString& module, const QString& method,
                              const QVariantList& args) const
{
    QMutexLocker lock(&m_mutex);
    for (const MockCallRecord& r : m_calls) {
        if (r.module == module && r.method == method && r.args == args) return true;
    }
    return false;
}

int MockStore::callCount(const QString& module, const QString& method) const
{
    QMutexLocker lock(&m_mutex);
    int count = 0;
    for (const MockCallRecord& r : m_calls) {
        if (r.module == module && r.method == method) ++count;
    }
    return count;
}

QVariantList MockStore::lastArgs(const QString& module, const QString& method) const
{
    QMutexLocker lock(&m_mutex);
    for (int i = m_calls.size() - 1; i >= 0; --i) {
        const MockCallRecord& r = m_calls.at(i);
        if (r.module == module && r.method == method) return r.args;
    }
    return QVariantList();
}

QList<MockCallRecord> MockStore::allCalls() const
{
    QMutexLocker lock(&m_mutex);
    return m_calls;
}
