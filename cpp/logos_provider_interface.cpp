#include "logos_provider_interface.h"
#include "logos_json_convert.h"
#include <QDebug>
#include <QJsonDocument>

// ---------------------------------------------------------------------------
// LogosProviderObject — universal virtual defaults
// ---------------------------------------------------------------------------

nlohmann::json LogosProviderObject::callMethodStd(const std::string& /*methodName*/,
                                                   const nlohmann::json& /*args*/)
{
    return nullptr;
}

std::vector<LogosMethodMetadata> LogosProviderObject::getMethodsStd()
{
    return {};
}

void LogosProviderObject::setEventListenerStd(UniversalEventCallback /*callback*/)
{
}

// ---------------------------------------------------------------------------
// LogosProviderObject — bridge helpers (Qt-free providers delegate here)
// ---------------------------------------------------------------------------

QVariant LogosProviderObject::callMethodStdBridge(const QString& methodName, const QVariantList& args)
{
    nlohmann::json jArgs = nlohmann::json::array();
    for (const QVariant& a : args)
        jArgs.push_back(logos::qvariantToNlohmann(a));

    nlohmann::json result = callMethodStd(methodName.toStdString(), jArgs);
    return logos::nlohmannToQVariant(result);
}

QJsonArray LogosProviderObject::getMethodsStdBridge()
{
    return logos::methodsToJsonArray(getMethodsStd());
}

void LogosProviderObject::setEventListenerStdBridge(EventCallback callback)
{
    setEventListenerStd([callback](const std::string& eventName, const std::string& data) {
        if (!callback) return;
        QVariantList qData;
        QJsonDocument doc = QJsonDocument::fromJson(
            QByteArray::fromStdString(data));
        if (doc.isArray()) {
            for (const QJsonValue& v : doc.array())
                qData.append(v.toVariant());
        } else {
            qData.append(QString::fromStdString(data));
        }
        callback(QString::fromStdString(eventName), qData);
    });
}
