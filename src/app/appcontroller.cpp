/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "appcontroller.h"
#include "networkrecord.h"

#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTimer>

namespace {

const char SyncService[] = "org.harbour.flotsam.Sync";
const char SyncPath[] = "/org/harbour/flotsam/Sync";
const char SyncInterface[] = "org.harbour.flotsam.Sync";

}

namespace Flotsam {

SyncInterfaceProxy::SyncInterfaceProxy(QObject *parent)
    : QDBusAbstractInterface(QString::fromLatin1(SyncService),
                             QString::fromLatin1(SyncPath),
                             SyncInterface,
                             QDBusConnection::sessionBus(), parent)
{
}

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_interface(new SyncInterfaceProxy(this))
{
    connect(m_interface, &SyncInterfaceProxy::StatusChanged,
            this, &AppController::remoteStatusChanged);
    connect(m_interface, &SyncInterfaceProxy::NetworksChanged,
            this, &AppController::remoteNetworksChanged);
    connect(m_interface, &SyncInterfaceProxy::OperationFailed,
            this, &AppController::remoteError);
    connect(m_interface, &SyncInterfaceProxy::ImportFinished,
            this, &AppController::remoteImportFinished);
    QTimer::singleShot(0, this, &AppController::refresh);
}

QVariantMap AppController::status() const { return m_status; }
QVariantList AppController::accounts() const { return m_accounts; }
QVariantList AppController::networks() const { return m_networks; }
QVariantMap AppController::details() const { return m_details; }
QString AppController::qrPayload() const { return m_qrPayload; }
QString AppController::error() const { return m_error; }
bool AppController::busy() const { return m_pendingCalls > 0; }

void AppController::refresh()
{
    setError(QString());
    ++m_pendingCalls;
    emit busyChanged();
    QDBusPendingCallWatcher *statusWatcher = new QDBusPendingCallWatcher(
            m_interface->asyncCall(QStringLiteral("Status")), this);
    statusWatcher->setProperty("kind", QStringLiteral("status"));
    connect(statusWatcher, &QDBusPendingCallWatcher::finished,
            this, &AppController::callFinished);
    startStringCall(QStringLiteral("Accounts"), QStringLiteral("accounts"));
    startStringCall(QStringLiteral("ListNetworks"), QStringLiteral("networks"));
}

void AppController::startStringCall(const QString &method, const QString &kind,
                                    const QVariantList &arguments)
{
    if (kind != QLatin1String("accounts") && kind != QLatin1String("networks")) {
        setError(QString());
    }
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
            m_interface->asyncCallWithArgumentList(method, arguments), this);
    watcher->setProperty("kind", kind);
    ++m_pendingCalls;
    emit busyChanged();
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &AppController::callFinished);
}

void AppController::startVoidCall(const QString &method, const QVariantList &arguments)
{
    setError(QString());
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
            m_interface->asyncCallWithArgumentList(method, arguments), this);
    watcher->setProperty("kind", QStringLiteral("void"));
    ++m_pendingCalls;
    emit busyChanged();
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &AppController::callFinished);
}

QVariantList AppController::parseArray(const QString &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (error) *error = QStringLiteral("The service returned malformed list data");
        return QVariantList();
    }
    return document.array().toVariantList();
}

QVariantMap AppController::parseObject(const QString &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("The service returned malformed detail data");
        return QVariantMap();
    }
    return document.object().toVariantMap();
}

void AppController::callFinished(QDBusPendingCallWatcher *watcher)
{
    const QString kind = watcher->property("kind").toString();
    if (kind == QLatin1String("status")) {
        QDBusPendingReply<QVariantMap> reply = *watcher;
        if (reply.isError()) {
            setError(reply.error().message());
        } else {
            m_status = reply.value();
            emit statusChanged();
        }
    } else if (kind != QLatin1String("void") && kind != QLatin1String("import")) {
        QDBusPendingReply<QString> reply = *watcher;
        if (reply.isError()) {
            setError(reply.error().message());
        } else {
            QString error;
            if (kind == QLatin1String("accounts")) {
                m_accounts = parseArray(reply.value(), &error);
                emit accountsChanged();
            } else if (kind == QLatin1String("networks")) {
                m_networks = parseArray(reply.value(), &error);
                emit networksChanged();
            } else if (kind == QLatin1String("details")) {
                m_details = parseObject(reply.value(), &error);
                emit detailsChanged();
            } else if (kind == QLatin1String("qr")) {
                m_qrPayload = reply.value();
                emit qrPayloadChanged();
            }
            if (!error.isEmpty()) setError(error);
        }
    } else {
        QDBusPendingReply<> reply = *watcher;
        if (reply.isError()) {
            setError(reply.error().message());
            if (kind == QLatin1String("import")) {
                emit importFinished(false, reply.error().message());
            }
        } else if (kind == QLatin1String("void")) {
            QTimer::singleShot(100, this, &AppController::refresh);
        }
    }
    watcher->deleteLater();
    m_pendingCalls = qMax(0, m_pendingCalls - 1);
    emit busyChanged();
}

void AppController::loadDetails(const QString &networkId, bool revealSecret)
{
    startStringCall(QStringLiteral("Details"), QStringLiteral("details"),
                    QVariantList() << networkId << revealSecret);
}

void AppController::loadQr(const QString &networkId)
{
    m_qrPayload.clear();
    emit qrPayloadChanged();
    startStringCall(QStringLiteral("QrPayload"), QStringLiteral("qr"),
                    QVariantList() << networkId);
}

QString AppController::passphraseError(const QString &securityFamily,
                                       const QString &passphrase) const
{
    QString error;
    NetworkRecord::validatePassphrase(securityFamily, passphrase, &error);
    return error;
}

QVariantMap AppController::parseWifiQr(const QString &payload) const
{
    QString error;
    const NetworkRecord record = NetworkRecord::fromWifiQr(payload, &error);
    if (!error.isEmpty()) {
        QVariantMap result;
        result.insert(QStringLiteral("valid"), false);
        result.insert(QStringLiteral("error"), error);
        return result;
    }
    QVariantMap result = record.toVariantMap(false);
    result.insert(QStringLiteral("valid"), true);
    return result;
}

void AppController::importWifiQr(const QString &payload)
{
    setError(QString());
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
            m_interface->asyncCall(QStringLiteral("ImportWifiQr"), payload), this);
    watcher->setProperty("kind", QStringLiteral("import"));
    ++m_pendingCalls;
    emit busyChanged();
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &AppController::callFinished);
}

void AppController::manualSync() { startVoidCall(QStringLiteral("ManualSync")); }
void AppController::beginSetup(int id) {
    // The trailing value keeps the version 1 D-Bus method signature compatible.
    startVoidCall(QStringLiteral("BeginSetup"), QVariantList() << id << true);
}
void AppController::completeSetup(const QVariantList &ids) {
    QStringList selected;
    for (const QVariant &id : ids) selected.append(id.toString());
    startVoidCall(QStringLiteral("CompleteSetup"), QVariantList() << QVariant::fromValue(selected));
}
void AppController::setAccount(int id) {
    startVoidCall(QStringLiteral("SetAccount"), QVariantList() << id);
}
void AppController::newNetworkChoice(const QString &id, const QString &choice) {
    startVoidCall(QStringLiteral("NewNetworkChoice"), QVariantList() << id << choice);
}
void AppController::edit(const QString &id, const QString &passphrase,
                         bool hidden, bool autoConnect, bool confirmed) {
    startVoidCall(QStringLiteral("Edit"), QVariantList() << id << passphrase
                  << hidden << autoConnect << confirmed);
}
void AppController::block(const QString &id) {
    startVoidCall(QStringLiteral("Block"), QVariantList() << id);
}
void AppController::unblock(const QString &id) {
    startVoidCall(QStringLiteral("Unblock"), QVariantList() << id);
}
void AppController::resolveConflict(const QString &id, const QString &choice,
                                    const QVariantMap &editedFields) {
    startVoidCall(QStringLiteral("ResolveConflict"), QVariantList() << id << choice << editedFields);
}
void AppController::resolveForgotten(const QString &id, const QString &choice) {
    startVoidCall(QStringLiteral("ResolveForgotten"), QVariantList() << id << choice);
}
void AppController::forgetEverywhere(const QString &id) {
    startVoidCall(QStringLiteral("ForgetEverywhere"), QVariantList() << id);
}

void AppController::remoteStatusChanged(const QVariantMap &status)
{
    m_status = status;
    emit statusChanged();
}

void AppController::remoteNetworksChanged()
{
    startStringCall(QStringLiteral("ListNetworks"), QStringLiteral("networks"));
}

void AppController::remoteError(const QString &message)
{
    setError(message);
}

void AppController::remoteImportFinished(bool success, const QString &message)
{
    if (!success) {
        setError(message);
    } else {
        QTimer::singleShot(100, this, &AppController::refresh);
    }
    emit importFinished(success, message);
}

void AppController::setError(const QString &error)
{
    if (m_error == error) return;
    m_error = error;
    emit errorChanged();
}

}
