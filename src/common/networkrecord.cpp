/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "networkrecord.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMap>
#include <QRegularExpression>
#include <QUuid>

namespace {

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

bool isStrictUtf8(const QByteArray &data, QString *decoded)
{
    const QString text = QString::fromUtf8(data.constData(), data.size());
    if (text.toUtf8() != data || text.contains(QChar::ReplacementCharacter)) {
        return false;
    }
    if (decoded) {
        *decoded = text;
    }
    return true;
}

bool onlyKeys(const QJsonObject &object, const QStringList &allowed, QString *error)
{
    for (QJsonObject::const_iterator it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!allowed.contains(it.key())) {
            setError(error, QStringLiteral("Unknown key: %1").arg(it.key()));
            return false;
        }
    }
    return true;
}

bool requireType(const QJsonObject &object, const QString &key,
                 QJsonValue::Type type, QString *error)
{
    if (!object.contains(key) || object.value(key).type() != type) {
        setError(error, QStringLiteral("Missing or invalid %1").arg(key));
        return false;
    }
    return true;
}

QString escapedWifiField(const QString &value)
{
    QString result;
    result.reserve(value.size() * 2);
    for (int i = 0; i < value.size(); ++i) {
        const QChar character = value.at(i);
        if (character == QLatin1Char('\\') || character == QLatin1Char(';')
                || character == QLatin1Char(',') || character == QLatin1Char(':')
                || character == QLatin1Char('"')) {
            result.append(QLatin1Char('\\'));
        }
        result.append(character);
    }
    return result;
}

bool parseWifiFields(const QString &payload, QMap<QString, QString> *fields,
                     QString *error)
{
    if (!payload.startsWith(QStringLiteral("WIFI:"))) {
        setError(error, QStringLiteral("This is not a Wi-Fi QR code"));
        return false;
    }

    QString key;
    QString value;
    bool readingValue = false;
    bool escaped = false;
    const auto finishField = [&]() -> bool {
        if (key.isEmpty() && !readingValue) {
            return true;
        }
        if (key.isEmpty() || !readingValue) {
            setError(error, QStringLiteral("Malformed Wi-Fi QR field"));
            return false;
        }
        const QString normalizedKey = key.trimmed().toUpper();
        if (fields->contains(normalizedKey)) {
            setError(error, QStringLiteral("Duplicate Wi-Fi QR field: %1")
                     .arg(normalizedKey));
            return false;
        }
        fields->insert(normalizedKey, value);
        return true;
    };

    for (int i = 5; i < payload.size(); ++i) {
        const QChar character = payload.at(i);
        if (escaped) {
            (readingValue ? value : key).append(character);
            escaped = false;
        } else if (character == QLatin1Char('\\')) {
            escaped = true;
        } else if (!readingValue && character == QLatin1Char(':')) {
            readingValue = true;
        } else if (character == QLatin1Char(';')) {
            if (!finishField()) {
                return false;
            }
            key.clear();
            value.clear();
            readingValue = false;
        } else {
            (readingValue ? value : key).append(character);
        }
    }
    if (escaped) {
        setError(error, QStringLiteral("Wi-Fi QR code ends with an escape character"));
        return false;
    }
    return finishField();
}

}

namespace Flotsam {

NetworkRecord NetworkRecord::fromWifiQr(const QString &payload, QString *error)
{
    if (error) {
        error->clear();
    }
    NetworkRecord record;
    if (payload.toUtf8().size() > 4096) {
        setError(error, QStringLiteral("Wi-Fi QR code is too large"));
        return record;
    }

    QMap<QString, QString> fields;
    if (!parseWifiFields(payload, &fields, error) || !fields.contains(QStringLiteral("S"))) {
        if (error && error->isEmpty()) {
            setError(error, QStringLiteral("Wi-Fi QR code has no network name"));
        }
        return record;
    }

    const QString type = fields.value(QStringLiteral("T")).trimmed().toUpper();
    if (type.isEmpty() || type == QLatin1String("NOPASS")
            || type == QLatin1String("OPEN")) {
        record.securityFamily = QStringLiteral("open");
        record.securityHint = QStringLiteral("none");
    } else if (type == QLatin1String("WEP")) {
        record.securityFamily = QStringLiteral("wep");
        record.securityHint = QStringLiteral("wep");
    } else if (type == QLatin1String("WPA") || type == QLatin1String("WPA2")
               || type == QLatin1String("WPA/WPA2")
               || type == QLatin1String("WPA-PSK")
               || type == QLatin1String("WPA2-PSK")) {
        record.securityFamily = QStringLiteral("personal");
        record.securityHint = QStringLiteral("psk");
    } else if (type == QLatin1String("WPA3") || type == QLatin1String("SAE")
               || type == QLatin1String("WPA3-SAE")
               || type == QLatin1String("WPA3-PERSONAL")
               || type == QLatin1String("WPA2/WPA3")) {
        record.securityFamily = QStringLiteral("personal");
        record.securityHint = QStringLiteral("sae");
    } else {
        setError(error, QStringLiteral("Unsupported Wi-Fi QR security type"));
        return NetworkRecord();
    }

    const QString hidden = fields.value(QStringLiteral("H")).trimmed().toLower();
    if (!hidden.isEmpty() && hidden != QLatin1String("true")
            && hidden != QLatin1String("false") && hidden != QLatin1String("1")
            && hidden != QLatin1String("0")) {
        setError(error, QStringLiteral("Invalid hidden-network value"));
        return NetworkRecord();
    }
    record.ssid = fields.value(QStringLiteral("S")).toUtf8();
    record.passphrase = fields.value(QStringLiteral("P"));
    record.hidden = hidden == QLatin1String("true") || hidden == QLatin1String("1");
    record.autoConnect = true;
    if (!record.isValid(error, false)) {
        return NetworkRecord();
    }
    return record;
}

QString NetworkRecord::deriveNetworkId(const QByteArray &rawSsid, const QString &family)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(rawSsid);
    hash.addData("\0", 1);
    hash.addData(family.toUtf8());
    return QString::fromLatin1(hash.result().toHex());
}

QString NetworkRecord::networkId() const
{
    return deriveNetworkId(ssid, securityFamily);
}

bool NetworkRecord::hasTextSsid() const
{
    QString ignored;
    return isStrictUtf8(ssid, &ignored) && !ssid.contains('\0');
}

QString NetworkRecord::displayName() const
{
    QString text;
    if (isStrictUtf8(ssid, &text) && !text.contains(QChar::Null)) {
        return text;
    }
    return QStringLiteral("SSID %1").arg(QString::fromLatin1(ssid.toHex()));
}

QString NetworkRecord::normalizeSecurityFamily(const QStringList &security)
{
    for (const QString &item : security) {
        const QString value = item.toLower();
        if (value == QLatin1String("psk") || value == QLatin1String("wpa")
                || value == QLatin1String("rsn") || value == QLatin1String("psksae")
                || value == QLatin1String("psk_sae")
                || value == QLatin1String("psk-sae") || value == QLatin1String("sae")) {
            return QStringLiteral("personal");
        }
    }
    if (security.contains(QStringLiteral("wep"), Qt::CaseInsensitive)) {
        return QStringLiteral("wep");
    }
    if (security.contains(QStringLiteral("none"), Qt::CaseInsensitive)
            || security.contains(QStringLiteral("open"), Qt::CaseInsensitive)) {
        return QStringLiteral("open");
    }
    return QString();
}

QString NetworkRecord::normalizeSecurityHint(const QStringList &security)
{
    const QStringList preference = QStringList()
            << QStringLiteral("sae") << QStringLiteral("psksae")
            << QStringLiteral("psk_sae")
            << QStringLiteral("psk-sae") << QStringLiteral("rsn")
            << QStringLiteral("psk") << QStringLiteral("wpa")
            << QStringLiteral("wep") << QStringLiteral("none");
    for (const QString &candidate : preference) {
        for (const QString &item : security) {
            if (item.compare(candidate, Qt::CaseInsensitive) == 0) {
            return candidate == QLatin1String("psk-sae")
                        || candidate == QLatin1String("psk_sae")
                        ? QStringLiteral("psksae") : candidate;
            }
        }
    }
    return QString();
}

bool NetworkRecord::validatePassphrase(const QString &family, const QString &passphrase,
                                       QString *error)
{
    const QByteArray bytes = passphrase.toUtf8();
    if (passphrase.contains(QChar::Null)
            || QString::fromUtf8(bytes.constData(), bytes.size()) != passphrase) {
        setError(error, QStringLiteral("Wi-Fi passwords must be valid text without NUL characters"));
        return false;
    }
    if (family == QLatin1String("open")) {
        if (!passphrase.isEmpty()) {
            setError(error, QStringLiteral("Open networks cannot have a passphrase"));
            return false;
        }
        return true;
    }
    if (family == QLatin1String("wep")) {
        const int length = bytes.size();
        const bool asciiLength = length == 5 || length == 13;
        const bool hexLength = (length == 10 || length == 26)
                && QRegularExpression(QStringLiteral("^[0-9A-Fa-f]+$")).match(passphrase).hasMatch();
        if (!asciiLength && !hexLength) {
            setError(error, QStringLiteral("WEP keys must be 5 or 13 bytes, or 10 or 26 hexadecimal digits"));
            return false;
        }
        return true;
    }
    if (family == QLatin1String("personal")) {
        const bool hexadecimalPsk = bytes.size() == 64
                && QRegularExpression(QStringLiteral("^[0-9A-Fa-f]{64}$")).match(passphrase).hasMatch();
        if (!hexadecimalPsk && (bytes.size() < 8 || bytes.size() > 63)) {
            setError(error, QStringLiteral("Personal Wi-Fi passwords must be 8 to 63 bytes, or 64 hexadecimal digits"));
            return false;
        }
        return true;
    }
    setError(error, QStringLiteral("Unsupported security family"));
    return false;
}

bool NetworkRecord::isValid(QString *error, bool requireRevision) const
{
    if (schemaVersion != CurrentSchemaVersion) {
        setError(error, schemaVersion > CurrentSchemaVersion
                ? QStringLiteral("Record uses a newer schema")
                : QStringLiteral("Unsupported record schema"));
        return false;
    }
    if (ssid.isEmpty() || ssid.size() > 32) {
        setError(error, QStringLiteral("SSID must contain between 1 and 32 bytes"));
        return false;
    }
    if (securityFamily != QLatin1String("open") && securityFamily != QLatin1String("wep")
            && securityFamily != QLatin1String("personal")) {
        setError(error, QStringLiteral("Unsupported security family"));
        return false;
    }
    if (normalizeSecurityFamily(QStringList() << securityHint) != securityFamily) {
        setError(error, QStringLiteral("Security hint does not match its family"));
        return false;
    }
    if (networkId().size() != 64) {
        setError(error, QStringLiteral("Invalid network identity"));
        return false;
    }
    if (requireRevision && QUuid(revision).isNull()) {
        setError(error, QStringLiteral("Missing or invalid revision UUID"));
        return false;
    }
    if (!parentRevision.isEmpty() && QUuid(parentRevision).isNull()) {
        setError(error, QStringLiteral("Invalid parent revision UUID"));
        return false;
    }
    if (deviceLabel.toUtf8().size() > 128 || deviceUuid.toUtf8().size() > 64) {
        setError(error, QStringLiteral("Device metadata is too large"));
        return false;
    }
    if (requireRevision && QUuid(deviceUuid).isNull()) {
        setError(error, QStringLiteral("Missing or invalid device UUID"));
        return false;
    }
    if (requireRevision && !updatedAt.isValid()) {
        setError(error, QStringLiteral("Missing or invalid display timestamp"));
        return false;
    }
    if (tombstone) {
        if (!passphrase.isEmpty()) {
            setError(error, QStringLiteral("Tombstones cannot contain a passphrase"));
            return false;
        }
        return true;
    }
    return validatePassphrase(securityFamily, passphrase, error);
}

QByteArray NetworkRecord::contentFingerprint() const
{
    QJsonObject portable;
    portable.insert(QStringLiteral("autoconnect"), autoConnect);
    portable.insert(QStringLiteral("deleted"), tombstone);
    portable.insert(QStringLiteral("hidden"), hidden);
    portable.insert(QStringLiteral("passphrase"), tombstone ? QString() : passphrase);
    portable.insert(QStringLiteral("securityFamily"), securityFamily);
    portable.insert(QStringLiteral("ssidBase64"), QString::fromLatin1(ssid.toBase64()));
    const QByteArray canonical = QJsonDocument(portable).toJson(QJsonDocument::Compact);
    return QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex();
}

bool NetworkRecord::sameContent(const NetworkRecord &a, const NetworkRecord &b)
{
    return a.contentFingerprint() == b.contentFingerprint();
}

QString NetworkRecord::qrPayload(QString *error) const
{
    if (tombstone) {
        setError(error, QStringLiteral("Deleted networks cannot be exported"));
        return QString();
    }
    QString ssidText;
    if (!isStrictUtf8(ssid, &ssidText) || ssidText.contains(QChar::Null)) {
        setError(error, QStringLiteral("This SSID is not text"));
        return QString();
    }
    QString type;
    if (securityFamily == QLatin1String("open")) {
        type = QStringLiteral("nopass");
    } else if (securityFamily == QLatin1String("wep")) {
        type = QStringLiteral("WEP");
    } else if (securityFamily == QLatin1String("personal")) {
        type = QStringLiteral("WPA");
    } else {
        setError(error, QStringLiteral("Unsupported security family"));
        return QString();
    }
    QString payload = QStringLiteral("WIFI:T:%1;S:%2;")
            .arg(type, escapedWifiField(ssidText));
    if (securityFamily != QLatin1String("open")) {
        payload += QStringLiteral("P:%1;").arg(escapedWifiField(passphrase));
    }
    if (hidden) {
        payload += QStringLiteral("H:true;");
    }
    payload += QLatin1Char(';');
    return payload;
}

QJsonObject NetworkRecord::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("schemaVersion"), schemaVersion);
    object.insert(QStringLiteral("networkId"), networkId());
    object.insert(QStringLiteral("ssid"), hasTextSsid() ? displayName() : QString());
    object.insert(QStringLiteral("ssidBase64"), QString::fromLatin1(ssid.toBase64()));
    object.insert(QStringLiteral("securityFamily"), securityFamily);
    object.insert(QStringLiteral("securityHint"), securityHint);
    object.insert(QStringLiteral("passphrase"), tombstone ? QString() : passphrase);
    object.insert(QStringLiteral("hidden"), hidden);
    object.insert(QStringLiteral("autoconnect"), autoConnect);
    object.insert(QStringLiteral("deleted"), tombstone);
    object.insert(QStringLiteral("revision"), revision);
    object.insert(QStringLiteral("parentRevision"), parentRevision);
    QJsonObject device;
    device.insert(QStringLiteral("uuid"), deviceUuid);
    device.insert(QStringLiteral("label"), deviceLabel);
    object.insert(QStringLiteral("device"), device);
    object.insert(QStringLiteral("updatedAt"), updatedAt.toUTC().toString(Qt::ISODate));
    object.insert(QStringLiteral("contentFingerprint"), QString::fromLatin1(contentFingerprint()));
    return object;
}

QByteArray NetworkRecord::toJsonData() const
{
    return QJsonDocument(toJson()).toJson(QJsonDocument::Indented);
}

QVariantMap NetworkRecord::toVariantMap(bool includeSecret) const
{
    QVariantMap map = toJson().toVariantMap();
    map.insert(QStringLiteral("hasPassphrase"), !passphrase.isEmpty());
    if (!includeSecret) {
        map.remove(QStringLiteral("passphrase"));
    }
    map.insert(QStringLiteral("displayName"), displayName());
    map.insert(QStringLiteral("textSsid"), hasTextSsid());
    QString qrError;
    map.insert(QStringLiteral("qrAvailable"), !qrPayload(&qrError).isEmpty());
    return map;
}

NetworkRecord NetworkRecord::fromJson(const QJsonObject &object, QString *error,
                                      bool requireRevision)
{
    NetworkRecord record;
    const QStringList allowed = QStringList()
            << QStringLiteral("schemaVersion") << QStringLiteral("networkId")
            << QStringLiteral("ssid") << QStringLiteral("ssidBase64")
            << QStringLiteral("securityFamily") << QStringLiteral("securityHint")
            << QStringLiteral("passphrase") << QStringLiteral("hidden")
            << QStringLiteral("autoconnect") << QStringLiteral("deleted")
            << QStringLiteral("revision") << QStringLiteral("parentRevision")
            << QStringLiteral("device") << QStringLiteral("updatedAt")
            << QStringLiteral("contentFingerprint");
    if (!onlyKeys(object, allowed, error)) {
        record.schemaVersion = -1;
        return record;
    }
    if (!requireType(object, QStringLiteral("schemaVersion"), QJsonValue::Double, error)
            || !requireType(object, QStringLiteral("networkId"), QJsonValue::String, error)
            || !requireType(object, QStringLiteral("ssid"), QJsonValue::String, error)
            || !requireType(object, QStringLiteral("ssidBase64"), QJsonValue::String, error)
            || !requireType(object, QStringLiteral("securityFamily"), QJsonValue::String, error)
            || !requireType(object, QStringLiteral("securityHint"), QJsonValue::String, error)
            || !requireType(object, QStringLiteral("passphrase"), QJsonValue::String, error)
            || !requireType(object, QStringLiteral("hidden"), QJsonValue::Bool, error)
            || !requireType(object, QStringLiteral("autoconnect"), QJsonValue::Bool, error)
            || !requireType(object, QStringLiteral("deleted"), QJsonValue::Bool, error)
            || !requireType(object, QStringLiteral("revision"), QJsonValue::String, error)
            || !requireType(object, QStringLiteral("parentRevision"), QJsonValue::String, error)
            || !requireType(object, QStringLiteral("device"), QJsonValue::Object, error)
            || !requireType(object, QStringLiteral("updatedAt"), QJsonValue::String, error)
            || !requireType(object, QStringLiteral("contentFingerprint"), QJsonValue::String, error)) {
        record.schemaVersion = -1;
        return record;
    }
    record.schemaVersion = object.value(QStringLiteral("schemaVersion")).toInt(-1);
    const QByteArray encodedSsid = object.value(QStringLiteral("ssidBase64")).toString().toLatin1();
    record.ssid = QByteArray::fromBase64(encodedSsid);
    if (record.ssid.toBase64() != encodedSsid) {
        setError(error, QStringLiteral("SSID is not canonical base64"));
        record.schemaVersion = -1;
        return record;
    }
    record.securityFamily = object.value(QStringLiteral("securityFamily")).toString();
    record.securityHint = object.value(QStringLiteral("securityHint")).toString();
    record.passphrase = object.value(QStringLiteral("passphrase")).toString();
    record.hidden = object.value(QStringLiteral("hidden")).toBool();
    record.autoConnect = object.value(QStringLiteral("autoconnect")).toBool();
    record.tombstone = object.value(QStringLiteral("deleted")).toBool();
    record.revision = object.value(QStringLiteral("revision")).toString();
    record.parentRevision = object.value(QStringLiteral("parentRevision")).toString();
    const QJsonObject device = object.value(QStringLiteral("device")).toObject();
    if (!onlyKeys(device, QStringList() << QStringLiteral("uuid") << QStringLiteral("label"), error)) {
        record.schemaVersion = -1;
        return record;
    }
    if (!requireType(device, QStringLiteral("uuid"), QJsonValue::String, error)
            || !requireType(device, QStringLiteral("label"), QJsonValue::String, error)) {
        record.schemaVersion = -1;
        return record;
    }
    record.deviceUuid = device.value(QStringLiteral("uuid")).toString();
    record.deviceLabel = device.value(QStringLiteral("label")).toString();
    record.updatedAt = QDateTime::fromString(object.value(QStringLiteral("updatedAt")).toString(), Qt::ISODate);

    QString validationError;
    if (!record.isValid(&validationError, requireRevision)) {
        setError(error, validationError);
        return record;
    }
    if (object.value(QStringLiteral("networkId")).toString() != record.networkId()) {
        setError(error, QStringLiteral("Network identity does not match its contents"));
        record.schemaVersion = -1;
        return record;
    }
    const QString suppliedFingerprint = object.value(QStringLiteral("contentFingerprint")).toString();
    if (suppliedFingerprint.toLatin1() != record.contentFingerprint()) {
        setError(error, QStringLiteral("Content fingerprint does not match the record"));
        record.schemaVersion = -1;
    }
    return record;
}

NetworkRecord NetworkRecord::fromJsonData(const QByteArray &data, QString *error,
                                          bool requireRevision)
{
    NetworkRecord record;
    if (data.size() > 64 * 1024) {
        record.schemaVersion = -1;
        setError(error, QStringLiteral("Record exceeds 64 KiB"));
        return record;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        record.schemaVersion = -1;
        setError(error, QStringLiteral("Malformed JSON record"));
        return record;
    }
    return fromJson(document.object(), error, requireRevision);
}

NetworkRecord NetworkRecord::fromConnman(const QVariantMap &properties,
                                         const QByteArray &rawSsid,
                                         const QString &secret,
                                         QString *error)
{
    NetworkRecord record;
    record.ssid = rawSsid;
    QStringList security = properties.value(QStringLiteral("Security")).toStringList();
    if (security.isEmpty() && properties.value(QStringLiteral("Security")).type() == QVariant::String) {
        security.append(properties.value(QStringLiteral("Security")).toString());
    }
    record.securityFamily = normalizeSecurityFamily(security);
    record.securityHint = normalizeSecurityHint(security);
    record.passphrase = secret;
    record.hidden = properties.value(QStringLiteral("Hidden")).toBool();
    record.autoConnect = properties.value(QStringLiteral("AutoConnect")).toBool();
    QString validationError;
    if (!record.isValid(&validationError, false)) {
        setError(error, validationError);
    }
    return record;
}

}
