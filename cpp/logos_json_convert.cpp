#include "logos_json_convert.h"
#include "logos_types.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QMetaType>

namespace logos {

namespace {

// Canonical C-ABI bytes encoding: {"_bytes": "<base64url, unpadded>"} — a
// single-key object, lossless for arbitrary bytes incl. embedded NUL.
// Matches the plain wire's encoding (implementations/plain/json_mapping.cpp);
// Qt's Base64UrlEncoding|OmitTrailingEquals produces the identical alphabet
// and padding-free form.
nlohmann::json byteArrayToTaggedJson(const QByteArray& bytes)
{
    nlohmann::json obj;
    obj["_bytes"] = bytes.toBase64(QByteArray::Base64UrlEncoding
                                   | QByteArray::OmitTrailingEquals)
                         .toStdString();
    return obj;
}

bool isTaggedBytes(const nlohmann::json& j)
{
    return j.is_object() && j.size() == 1 && j.contains("_bytes")
        && j["_bytes"].is_string();
}

QByteArray taggedJsonToByteArray(const nlohmann::json& j)
{
    return QByteArray::fromBase64(
        QByteArray::fromStdString(j["_bytes"].get<std::string>()),
        QByteArray::Base64UrlEncoding);
}

} // namespace

nlohmann::json qvariantToNlohmann(const QVariant& v)
{
    if (v.userType() == QMetaType::QByteArray)
        return byteArrayToTaggedJson(v.toByteArray());

    const int logosResultId = QMetaType::fromName("LogosResult").id();
    if (logosResultId != QMetaType::UnknownType && v.userType() == logosResultId) {
        const LogosResult lr = v.value<LogosResult>();
        nlohmann::json obj;
        obj["success"] = lr.success;
        if (lr.value.userType() == QMetaType::QByteArray) {
            // Bytes inside a LogosResult take the same tagged form — the
            // QJsonValue::fromVariant fallback below would mangle them.
            obj["value"] = byteArrayToTaggedJson(lr.value.toByteArray());
        } else {
        QJsonValue valJson = QJsonValue::fromVariant(lr.value);
        if (valJson.isObject() || valJson.isArray()) {
            QJsonDocument d = valJson.isObject() ? QJsonDocument(valJson.toObject())
                                                 : QJsonDocument(valJson.toArray());
            try { obj["value"] = nlohmann::json::parse(d.toJson(QJsonDocument::Compact).toStdString()); }
            catch (...) { obj["value"] = nullptr; }
        } else if (valJson.isString()) obj["value"] = valJson.toString().toStdString();
        else if (valJson.isBool())     obj["value"] = valJson.toBool();
        else if (valJson.isDouble())   obj["value"] = valJson.toDouble();
        else                           obj["value"] = nullptr;
        }
        QJsonValue errJson = QJsonValue::fromVariant(lr.error);
        obj["error"] = errJson.isString() ? nlohmann::json(errJson.toString().toStdString())
                                           : nullptr;
        return obj;
    }

    if (v.canConvert<QJsonObject>()) {
        QJsonDocument doc(v.toJsonObject());
        try { return nlohmann::json::parse(doc.toJson(QJsonDocument::Compact).toStdString()); }
        catch (...) {}
    }
    if (v.canConvert<QJsonArray>()) {
        QJsonDocument doc(qvariant_cast<QJsonArray>(v));
        try { return nlohmann::json::parse(doc.toJson(QJsonDocument::Compact).toStdString()); }
        catch (...) {}
    }

    // Integers stay integers: QJsonValue::fromVariant degrades every numeric
    // to double, which a strict consumer on the other side of the C ABI
    // (e.g. a generated dispatch reading an int param) must not see as 5.0.
    switch (v.userType()) {
    case QMetaType::Int:       return v.toInt();
    case QMetaType::UInt:      return v.toUInt();
    case QMetaType::LongLong:  return static_cast<int64_t>(v.toLongLong());
    case QMetaType::ULongLong: return static_cast<uint64_t>(v.toULongLong());
    default: break;
    }

    QJsonValue jv = QJsonValue::fromVariant(v);
    if (jv.isString())  return jv.toString().toStdString();
    if (jv.isBool())    return jv.toBool();
    if (jv.isDouble())  return jv.toDouble();
    if (jv.isObject() || jv.isArray()) {
        QJsonDocument doc = jv.isObject() ? QJsonDocument(jv.toObject())
                                           : QJsonDocument(jv.toArray());
        try { return nlohmann::json::parse(doc.toJson(QJsonDocument::Compact).toStdString()); }
        catch (...) {}
    }
    return nullptr;
}

QVariant nlohmannToQVariant(const nlohmann::json& j)
{
    if (j.is_null())
        return QVariant();
    if (j.is_boolean())
        return QVariant(j.get<bool>());
    if (j.is_number_unsigned())
        return QVariant(static_cast<qulonglong>(j.get<uint64_t>()));
    if (j.is_number_integer())
        return QVariant(static_cast<qlonglong>(j.get<int64_t>()));
    if (j.is_number_float())
        return QVariant(j.get<double>());
    if (j.is_string())
        return QVariant(QString::fromStdString(j.get<std::string>()));
    if (isTaggedBytes(j))
        return QVariant(taggedJsonToByteArray(j));
    if (j.is_object()) {
        QJsonDocument doc = QJsonDocument::fromJson(
            QByteArray::fromStdString(j.dump()));
        return QVariant::fromValue(doc.object());
    }
    if (j.is_array()) {
        QJsonDocument doc = QJsonDocument::fromJson(
            QByteArray::fromStdString(j.dump()));
        return QVariant::fromValue(doc.array());
    }
    return QVariant();
}

QVariantList nlohmannArgsToQVariantList(const nlohmann::json& args)
{
    QVariantList result;
    if (!args.is_array()) return result;
    for (const auto& arg : args) {
        if (arg.is_string())
            result.append(QString::fromStdString(arg.get<std::string>()));
        else if (arg.is_boolean())
            result.append(arg.get<bool>());
        else if (arg.is_number_unsigned())
            result.append(static_cast<qulonglong>(arg.get<uint64_t>()));
        else if (arg.is_number_integer())
            result.append(static_cast<qlonglong>(arg.get<int64_t>()));
        else if (arg.is_number_float())
            result.append(arg.get<double>());
        else if (arg.is_null())
            result.append(QVariant());
        else if (isTaggedBytes(arg))
            result.append(QVariant(taggedJsonToByteArray(arg)));
        else if (arg.is_object() || arg.is_array()) {
            QJsonDocument doc = QJsonDocument::fromJson(
                QByteArray::fromStdString(arg.dump()));
            result.append(arg.is_object()
                ? QVariant::fromValue(doc.object())
                : QVariant::fromValue(doc.array()));
        } else {
            result.append(QVariant());
        }
    }
    return result;
}

QJsonArray methodsToJsonArray(const std::vector<LogosMethodMetadata>& methods)
{
    QJsonArray out;
    for (const auto& m : methods) {
        QJsonObject o;
        o["name"]        = QString::fromStdString(m.name);
        o["signature"]   = QString::fromStdString(m.signature);
        o["returnType"]  = QString::fromStdString(m.returnType);
        o["isInvokable"] = m.isInvokable;
        if (m.parameters.is_array()) {
            QJsonDocument paramDoc = QJsonDocument::fromJson(
                QByteArray::fromStdString(m.parameters.dump()));
            o["parameters"] = paramDoc.array();
        } else {
            o["parameters"] = QJsonArray();
        }
        out.append(o);
    }
    return out;
}

} // namespace logos
