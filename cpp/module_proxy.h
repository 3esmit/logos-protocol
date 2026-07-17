#ifndef MODULE_PROXY_H
#define MODULE_PROXY_H

#include <QObject>
#include <QVariant>
#include <QVariantList>
#include <QHash>
#include <QString>
#include <QJsonArray>

#include <functional>
#include <utility>

class LogosProviderObject;

/**
 * @brief ModuleProxy wraps a LogosProviderObject and exposes it as a QObject
 *        so that Qt Remote Objects can publish it.
 *
 * All method dispatch, introspection, and event forwarding is delegated
 * to the underlying LogosProviderObject*.  For legacy QObject-based plugins,
 * that provider is a QtProviderObject adapter; for new-API plugins it is
 * the plugin's own LogosProviderObject subclass.
 */
class ModuleProxy : public QObject
{
    Q_OBJECT

public:
    // A host-installed extra authorizer. Returns true if `token` is valid for a
    // call arriving over `transportProtocol` ("local" | "tcp" | "tcp_ssl").
    // Consulted IN ADDITION to the built-in issued-token scan, so installing one
    // only ever grants access to tokens the built-in scan wouldn't (e.g. the
    // daemon backs it with TokenStore::lookupByToken to make operator-issued
    // named tokens work, with per-token expiry and local_only enforced by the
    // transport it's handed).
    using TokenValidator = std::function<bool(const QString& token,
                                              const QString& transportProtocol)>;

    explicit ModuleProxy(LogosProviderObject* provider, QObject* parent = nullptr);
    ~ModuleProxy();

    void setTokenValidator(TokenValidator validator);

    // Two explicit Q_INVOKABLE overloads rather than one with a defaulted
    // transport arg: the Qt meta-object system matches by full parameter list
    // and does not apply C++ default arguments, so the existing QtRO/local
    // 3-arg call must remain a real 3-arg method. It forwards to the
    // transport-aware 4-arg form with "local" (RemoteTransportHost is always
    // local); remote hosts that know their wire (PlainTransportHost) call the
    // 4-arg form so a transport-sensitive validator (local_only tokens) can
    // enforce it.
    Q_INVOKABLE QVariant callRemoteMethod(const QString& authToken, const QString& methodName, const QVariantList& args = QVariantList());
    Q_INVOKABLE QVariant callRemoteMethod(const QString& authToken, const QString& methodName, const QVariantList& args, const QString& transportProtocol);
    Q_INVOKABLE bool informModuleToken(const QString& authToken, const QString& moduleName, const QString& token);
    bool saveToken(const QString& from_module_name, const QString& token);
    // getPluginInterface() returns the module's whole interface (methods AND
    // events, each tagged with a "type"); getPluginMethods()/getPluginEvents()
    // are the type-filtered views. All three derive from the provider's single
    // getMethods() call — there is no separate getEvents() vtable method, which
    // is what keeps the provider ABI stable across SDK versions.
    Q_INVOKABLE QJsonArray getPluginMethods();
    Q_INVOKABLE QJsonArray getPluginEvents();
    Q_INVOKABLE QJsonArray getPluginInterface();

signals:
    void eventResponse(const QString& eventName, const QVariantList& data);

private:
    // Returns true when authToken matches a token THIS module has issued (via
    // saveToken / informModuleToken) OR the host-installed validator accepts it
    // for `transportProtocol`. Empty/unknown tokens are rejected. The built-in
    // comparison is constant-time and never early-outs, so neither a correct
    // prefix nor the number of issued tokens leaks through timing.
    bool isAuthorized(const QString& authToken, const QString& transportProtocol) const;

    LogosProviderObject* m_provider;
    QHash<QString, QString> m_tokens;
    TokenValidator m_validator;
};

#endif // MODULE_PROXY_H
