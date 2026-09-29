/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "connmanhelper.h"
#include <QDBusConnection>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace {

bool validNetworkId(const QString &networkId)
{
    return networkId.size() == 64
            && QRegularExpression(QStringLiteral("^[0-9a-f]{64}$"))
               .match(networkId).hasMatch();
}

bool isActive(const QVariantMap &properties)
{
    const QString state = properties.value(QStringLiteral("State")).toString();
    return state == QLatin1String("ready") || state == QLatin1String("online")
            || state == QLatin1String("association") || state == QLatin1String("configuration");
}

}

namespace Flotsam {

ConnmanHelper::ConnmanHelper(ConnmanBackend *backend, QObject *parent)
    : QObject(parent)
    , m_backend(backend)
{
}

bool ConnmanHelper::authorizeCaller()
{
    if (!calledFromDBus()) {
        return true;
    }
    // Authorization is the kernel-enforced protected Unix socket. Reject any
    // accidental registration on a message bus, including the session bus.
    if (connection().interface()) {
        sendErrorReply(QStringLiteral("org.harbour.flotsam.Error.AccessDenied"),
                       QStringLiteral("Use the protected helper endpoint"));
        return false;
    }
    return true;
}

QList<ConnmanHelper::ExportedService> ConnmanHelper::exportedServices(QString *error)
{
    QList<ExportedService> output;
    const QList<ConnmanServiceData> services = m_backend->services(error);
    for (const ConnmanServiceData &service : services) {
        if (!ConnmanUtil::validServicePath(service.path)
                || service.properties.value(QStringLiteral("Type")).toString() != QLatin1String("wifi")
                || !service.properties.value(QStringLiteral("Favorite")).toBool()
                || service.properties.value(QStringLiteral("Immutable")).toBool()) {
            continue;
        }
        const QByteArray ssid = ConnmanUtil::ssidFromServicePath(service.path);
        if (ssid.isEmpty()) {
            continue;
        }
        QString propertyError;
        const QString passphrase = m_backend->getStringProperty(
                service.path, QStringLiteral("Passphrase"), &propertyError);
        QString recordError;
        const NetworkRecord record = NetworkRecord::fromConnman(
                service.properties, ssid, passphrase, &recordError);
        if (!recordError.isEmpty()) {
            continue;
        }
        ExportedService item;
        item.service = service;
        item.record = record;
        item.passphrase = passphrase;
        output.append(item);
    }
    return output;
}

QList<ConnmanHelper::ExportedService> ConnmanHelper::matchingServices(
        const QString &networkId, QString *error)
{
    QList<ExportedService> result;
    const QList<ExportedService> all = exportedServices(error);
    for (const ExportedService &item : all) {
        if (item.record.networkId() == networkId) {
            result.append(item);
        }
    }
    return result;
}

QString ConnmanHelper::result(const QString &status, const QString &message, const QString &path)
{
    QJsonObject object;
    object.insert(QStringLiteral("status"), status);
    if (!message.isEmpty()) {
        object.insert(QStringLiteral("message"), message);
    }
    if (!path.isEmpty()) {
        object.insert(QStringLiteral("path"), path);
    }
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QString ConnmanHelper::List()
{
    if (!authorizeCaller()) {
        return QString();
    }
    QString error;
    const QList<ExportedService> services = exportedServices(&error);
    if (!error.isEmpty() && services.isEmpty()) {
        sendErrorReply(QStringLiteral("org.harbour.flotsam.Error.Connman"), error);
        return QString();
    }
    QJsonArray array;
    for (const ExportedService &item : services) {
        QJsonObject entry;
        entry.insert(QStringLiteral("path"), item.service.path);
        entry.insert(QStringLiteral("state"), item.service.properties.value(
                         QStringLiteral("State")).toString());
        entry.insert(QStringLiteral("record"), item.record.toJson());
        array.append(entry);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QString ConnmanHelper::Export(const QString &networkId)
{
    if (!authorizeCaller()) {
        return QString();
    }
    if (networkId.size() != 64 || !networkId.contains(QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")))) {
        sendErrorReply(QStringLiteral("org.harbour.flotsam.Error.InvalidArguments"),
                       QStringLiteral("Invalid network identity"));
        return QString();
    }
    QString error;
    const QList<ExportedService> matches = matchingServices(networkId, &error);
    if (matches.isEmpty()) {
        sendErrorReply(QStringLiteral("org.harbour.flotsam.Error.NotFound"),
                       error.isEmpty() ? QStringLiteral("Network not found") : error);
        return QString();
    }
    return QString::fromUtf8(matches.first().record.toJsonData());
}

bool ConnmanHelper::rollback(const ExportedService &original, const QString &newPath)
{
    QString ignored;
    if (newPath != original.service.path
            && ConnmanUtil::validServicePath(newPath)
            && !m_backend->removeService(newPath, &ignored)) {
        return false;
    }
    const ConnmanSettings settings = ConnmanUtil::mergedSettings(
            original.service.properties, original.record.ssid, original.passphrase, original.record);
    const QString restoredPath = m_backend->createService(settings, &ignored);
    if (restoredPath.isEmpty()) {
        return false;
    }
    if (!m_backend->setProperty(restoredPath, QStringLiteral("AutoConnect"),
                                original.record.autoConnect, &ignored)) {
        return false;
    }
    return !isActive(original.service.properties)
            || m_backend->connectService(restoredPath, &ignored);
}

QString ConnmanHelper::CompareAndApply(const QString &expectedFingerprint,
                                       const QString &recordJson,
                                       bool allowActiveReconnect)
{
    if (!authorizeCaller()) {
        return QString();
    }
    const bool fingerprintValid = expectedFingerprint.isEmpty()
            || (expectedFingerprint.size() == 64
                && QRegularExpression(QStringLiteral("^[0-9a-f]{64}$"))
                   .match(expectedFingerprint).hasMatch());
    if (recordJson.toUtf8().size() > 64 * 1024 || !fingerprintValid) {
        return result(QStringLiteral("Invalid"), QStringLiteral("Input exceeds its size limit"));
    }
    QString parseError;
    const NetworkRecord record = NetworkRecord::fromJsonData(recordJson.toUtf8(), &parseError);
    if (!parseError.isEmpty() || record.tombstone) {
        return result(QStringLiteral("Invalid"), parseError.isEmpty()
                      ? QStringLiteral("Cannot apply a tombstone") : parseError);
    }

    QString listError;
    QList<ExportedService> matches = matchingServices(record.networkId(), &listError);
    if (!listError.isEmpty() && matches.isEmpty()) {
        return result(QStringLiteral("Failed"), listError);
    }
    if (matches.isEmpty() && !expectedFingerprint.isEmpty()) {
        return result(QStringLiteral("Conflict"), QStringLiteral("The expected network no longer exists"));
    }
    if (!matches.isEmpty()) {
        const QByteArray expected = expectedFingerprint.toLatin1();
        for (const ExportedService &item : matches) {
            if (!ConnmanUtil::compareFingerprint(item.record.contentFingerprint(), expected, nullptr)) {
                return result(QStringLiteral("Conflict"),
                              QStringLiteral("The network changed while it was being synchronized"));
            }
        }
    } else if (!expectedFingerprint.isEmpty()) {
        return result(QStringLiteral("Conflict"));
    }

    ExportedService original;
    QVariantMap nativeProperties;
    QString nativePassphrase;
    bool active = false;
    if (!matches.isEmpty()) {
        int selected = 0;
        for (int i = 0; i < matches.size(); ++i) {
            if (isActive(matches.at(i).service.properties)) {
                selected = i;
                break;
            }
        }
        original = matches.at(selected);
        nativeProperties = original.service.properties;
        nativePassphrase = original.passphrase;
        active = isActive(nativeProperties);
        const bool changed = !NetworkRecord::sameContent(original.record, record);
        if (!changed) {
            return result(QStringLiteral("Ok"), QString(), original.service.path);
        }
        if (active && !allowActiveReconnect) {
            return result(QStringLiteral("ActiveConfirmationRequired"),
                          QStringLiteral("Editing the active network requires confirmation"));
        }
    }

    QString operationError;
    if (active && !m_backend->disconnectService(original.service.path, &operationError)) {
        return result(QStringLiteral("Failed"), operationError);
    }
    const ConnmanSettings settings = ConnmanUtil::mergedSettings(
            nativeProperties, record.ssid, nativePassphrase, record);
    const QString path = m_backend->createService(settings, &operationError);
    if (path.isEmpty() || !ConnmanUtil::validServicePath(path)) {
        if (active) {
            m_backend->connectService(original.service.path, nullptr);
        }
        return result(QStringLiteral("Failed"), operationError.isEmpty()
                      ? QStringLiteral("ConnMan returned an invalid service path") : operationError);
    }
    if (!m_backend->setProperty(path, QStringLiteral("AutoConnect"),
                                record.autoConnect, &operationError)) {
        const bool restored = matches.isEmpty()
                ? m_backend->removeService(path, nullptr) : rollback(original, path);
        return result(QStringLiteral("Failed"), operationError
                      + (restored ? QString() : QStringLiteral("; rollback failed")));
    }
    if (active && !m_backend->connectService(path, &operationError)) {
        const bool restored = rollback(original, path);
        return result(QStringLiteral("Failed"), operationError
                      + (restored ? QString() : QStringLiteral("; rollback failed")));
    }
    return result(QStringLiteral("Ok"), QString(), path);
}

QString ConnmanHelper::CompareAndRemove(const QString &networkId,
                                        const QString &expectedFingerprint)
{
    if (!authorizeCaller()) {
        return QString();
    }
    if (!validNetworkId(networkId) || expectedFingerprint.size() != 64
            || !QRegularExpression(QStringLiteral("^[0-9a-f]{64}$"))
                .match(expectedFingerprint).hasMatch()) {
        return result(QStringLiteral("Invalid"), QStringLiteral("Invalid identity or fingerprint"));
    }
    QString error;
    const QList<ExportedService> matches = matchingServices(networkId, &error);
    if (!error.isEmpty() && matches.isEmpty()) {
        return result(QStringLiteral("Failed"), error);
    }
    if (matches.isEmpty()) {
        return result(QStringLiteral("Ok"));
    }
    const QByteArray expected = expectedFingerprint.toLatin1();
    for (const ExportedService &item : matches) {
        if (!ConnmanUtil::compareFingerprint(item.record.contentFingerprint(), expected, nullptr)) {
            return result(QStringLiteral("Conflict"),
                          QStringLiteral("The network changed while it was being synchronized"));
        }
    }
    for (const ExportedService &item : matches) {
        if (!m_backend->removeService(item.service.path, &error)) {
            return result(QStringLiteral("Failed"), error);
        }
    }
    return result(QStringLiteral("Ok"));
}

}
