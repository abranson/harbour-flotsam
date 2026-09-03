/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "syncdaemon.h"
#include "notificationtoken.h"

#include "reconciler.h"

#include <networkmanager.h>

#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QUuid>
#include <algorithm>

namespace {

const char HelperService[] = "org.harbour.flotsam.Connman";
const char HelperPath[] = "/org/harbour/flotsam/Connman";
const char HelperInterface[] = "org.harbour.flotsam.Connman";

QString phaseName(int phase)
{
    switch (phase) {
    case 0: return QStringLiteral("idle");
    case 1: return QStringLiteral("authenticating");
    case 2: return QStringLiteral("creating-layout");
    case 3: return QStringLiteral("listing-remote");
    case 4: return QStringLiteral("fetching-remote");
    case 5: return QStringLiteral("listing-local");
    case 6: return QStringLiteral("applying");
    }
    return QStringLiteral("unknown");
}

bool validNetworkId(const QString &networkId)
{
    return networkId.size() == 64
            && QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(networkId).hasMatch();
}

QString helperStatus(const QString &json, QString *message)
{
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
    if (!document.isObject()) {
        if (message) {
            *message = QStringLiteral("The ConnMan helper returned malformed data");
        }
        return QStringLiteral("Failed");
    }
    const QJsonObject object = document.object();
    if (message) {
        *message = object.value(QStringLiteral("message")).toString();
    }
    return object.value(QStringLiteral("status")).toString();
}

Flotsam::NetworkRecord versioned(const Flotsam::NetworkRecord &source,
                                 const Flotsam::NetworkRecord *base,
                                 const QJsonObject &state)
{
    Flotsam::NetworkRecord result = source;
    result.schemaVersion = Flotsam::NetworkRecord::CurrentSchemaVersion;
    result.parentRevision = base ? base->revision : QString();
    result.revision = QUuid::createUuid().toString();
    result.deviceUuid = state.value(QStringLiteral("deviceUuid")).toString();
    result.deviceLabel = state.value(QStringLiteral("deviceLabel")).toString();
    result.updatedAt = QDateTime::currentDateTimeUtc();
    return result;
}

void addResolutionContext(QJsonObject *resolution,
                          const Flotsam::NetworkRecord *local,
                          const Flotsam::NetworkRecord *remote,
                          const QString &etag)
{
    resolution->insert(QStringLiteral("contextVersion"), 1);
    resolution->insert(QStringLiteral("localPresent"), local != nullptr);
    resolution->insert(QStringLiteral("remotePresent"), remote != nullptr);
    resolution->insert(QStringLiteral("localFingerprint"), local
                       ? QString::fromLatin1(local->contentFingerprint()) : QString());
    resolution->insert(QStringLiteral("remoteFingerprint"), remote
                       ? QString::fromLatin1(remote->contentFingerprint()) : QString());
    resolution->insert(QStringLiteral("remoteRevision"), remote ? remote->revision : QString());
    resolution->insert(QStringLiteral("remoteEtag"), etag);
}

bool resolutionContextMatches(const QJsonObject &resolution,
                              const Flotsam::NetworkRecord *local,
                              const Flotsam::NetworkRecord *remote,
                              const QString &etag)
{
    if (resolution.value(QStringLiteral("contextVersion")).toInt() != 1
            || resolution.value(QStringLiteral("localPresent")).toBool() != (local != nullptr)
            || resolution.value(QStringLiteral("remotePresent")).toBool() != (remote != nullptr)) {
        return false;
    }
    if (local && resolution.value(QStringLiteral("localFingerprint")).toString().toLatin1()
            != local->contentFingerprint()) {
        return false;
    }
    if (remote && (resolution.value(QStringLiteral("remoteFingerprint")).toString().toLatin1()
                   != remote->contentFingerprint()
                   || resolution.value(QStringLiteral("remoteRevision")).toString()
                   != remote->revision)) {
        return false;
    }
    return resolution.value(QStringLiteral("remoteEtag")).toString() == etag;
}

}

namespace Flotsam {

SyncDaemon::SyncDaemon(QObject *parent)
    : QObject(parent)
    , m_stateStore()
    , m_accounts(this)
    , m_webDav(this)
    , m_notifications(this)
{
    connect(&m_accounts, &AccountProvider::credentialsReady,
            this, &SyncDaemon::credentialsReady);
    connect(&m_accounts, &AccountProvider::authenticationError,
            this, &SyncDaemon::authenticationError);
    connect(&m_webDav, &WebDavClient::layoutCreated,
            this, &SyncDaemon::layoutCreated);
    connect(&m_webDav, &WebDavClient::formatReceived,
            this, &SyncDaemon::formatReceived);
    connect(&m_webDav, &WebDavClient::recordsListed,
            this, &SyncDaemon::recordsListed);
    connect(&m_webDav, &WebDavClient::recordReceived,
            this, &SyncDaemon::recordReceived);
    connect(&m_webDav, &WebDavClient::recordWritten,
            this, &SyncDaemon::recordWritten);
    connect(&m_webDav, &WebDavClient::requestFailed,
            this, &SyncDaemon::webDavFailed);
    connect(&m_notifications, &NotificationManager::actionInvoked,
            this, &SyncDaemon::notificationInvoked);

    m_periodicTimer.setInterval(6 * 60 * 60 * 1000);
    connect(&m_periodicTimer, &QTimer::timeout, this, [this]() {
        requestSync(QStringLiteral("periodic reconciliation"));
    });
    m_eventTimer.setSingleShot(true);
    m_eventTimer.setInterval(10000);
    connect(&m_eventTimer, &QTimer::timeout, this, [this]() {
        requestSync(QStringLiteral("saved Wi-Fi changed"));
    });
    m_retryTimer.setSingleShot(true);
    connect(&m_retryTimer, &QTimer::timeout, this, &SyncDaemon::retryTimeout);
}

bool SyncDaemon::initialize(QString *error)
{
    if (!m_stateStore.load(error)) {
        return false;
    }
    m_state = m_stateStore.object();
    m_lastResult = m_state.value(QStringLiteral("lastResult")).toString();
    m_lastError = m_state.value(QStringLiteral("lastError")).toString();
    m_lastSync = QDateTime::fromString(m_state.value(QStringLiteral("lastSync")).toString(),
                                      Qt::ISODate);
    m_networkManager = new NetworkManager(this);
    connect(m_networkManager, &NetworkManager::stateChanged,
            this, &SyncDaemon::networkStateChanged);
    connect(m_networkManager, &NetworkManager::servicesChanged,
            this, &SyncDaemon::networkServicesChanged);
    connect(m_networkManager, &NetworkManager::savedServicesChanged,
            this, &SyncDaemon::networkServicesChanged);
    m_periodicTimer.start();
    requestLocalInventory(false);
    if (m_state.value(QStringLiteral("setupComplete")).toBool()) {
        QTimer::singleShot(5000, this, [this]() {
            requestSync(QStringLiteral("startup"));
        });
    }
    return true;
}

QVariantMap SyncDaemon::Status() const
{
    QVariantMap status;
    status.insert(QStringLiteral("version"), 1);
    status.insert(QStringLiteral("phase"), phaseName(m_phase));
    status.insert(QStringLiteral("syncing"), m_phase != Idle);
    status.insert(QStringLiteral("setupComplete"),
                  m_state.value(QStringLiteral("setupComplete")).toBool());
    status.insert(QStringLiteral("setupStage"),
                  m_state.value(QStringLiteral("setupStage")).toString());
    status.insert(QStringLiteral("accountId"),
                  m_state.value(QStringLiteral("accountId")).toInt());
    // Keep older application builds able to pass their former setup gate.
    status.insert(QStringLiteral("plaintextWarningAcknowledged"), true);
    status.insert(QStringLiteral("lastResult"), m_lastResult);
    status.insert(QStringLiteral("lastError"), m_lastError);
    status.insert(QStringLiteral("lastSync"), m_lastSync.toString(Qt::ISODate));
    status.insert(QStringLiteral("needsAttention"),
                  m_state.value(QStringLiteral("conflicts")).toObject().size()
                  + m_state.value(QStringLiteral("forgotten")).toObject().size()
                  + m_state.value(QStringLiteral("errors")).toObject().size());
    return status;
}

QString SyncDaemon::Accounts() const
{
    return m_accounts.accountsJson();
}

QString SyncDaemon::categoryFor(const QString &networkId) const
{
    if (m_state.value(QStringLiteral("conflicts")).toObject().contains(networkId)
            || m_state.value(QStringLiteral("forgotten")).toObject().contains(networkId)
            || m_state.value(QStringLiteral("errors")).toObject().contains(networkId)) {
        return QStringLiteral("Needs attention");
    }
    if (isBlocked(networkId)) {
        return QStringLiteral("This device only");
    }
    if (m_state.value(QStringLiteral("pending")).toObject().contains(networkId)
            || m_state.value(QStringLiteral("pendingTombstones")).toObject().contains(networkId)
            || isApproved(networkId)
            || (m_local.contains(networkId)
                && !m_state.value(QStringLiteral("bases")).toObject().contains(networkId))) {
        return QStringLiteral("Pending");
    }
    return QStringLiteral("Synced");
}

NetworkRecord SyncDaemon::recordFromObject(const QJsonObject &object, bool requireRevision,
                                           QString *error)
{
    return NetworkRecord::fromJson(object, error, requireRevision);
}

NetworkRecord SyncDaemon::baseRecord(const QString &networkId, bool *exists) const
{
    const QJsonObject bases = m_state.value(QStringLiteral("bases")).toObject();
    if (exists) {
        *exists = bases.contains(networkId);
    }
    if (!bases.contains(networkId)) {
        return NetworkRecord();
    }
    return recordFromObject(bases.value(networkId).toObject(), true, nullptr);
}

NetworkRecord SyncDaemon::recordForDisplay(const QString &networkId, bool *exists) const
{
    if (m_local.contains(networkId)) {
        if (exists) {
            *exists = true;
        }
        return m_local.value(networkId).record;
    }
    const QJsonObject conflicts = m_state.value(QStringLiteral("conflicts")).toObject();
    if (conflicts.contains(networkId)) {
        const QJsonObject conflict = conflicts.value(networkId).toObject();
        const QJsonObject candidate = conflict.value(QStringLiteral("local")).toObject().isEmpty()
                ? conflict.value(QStringLiteral("remote")).toObject()
                : conflict.value(QStringLiteral("local")).toObject();
        if (exists) {
            *exists = true;
        }
        return recordFromObject(candidate, !candidate.value(QStringLiteral("revision")).toString().isEmpty(), nullptr);
    }
    bool baseExists = false;
    const NetworkRecord base = baseRecord(networkId, &baseExists);
    if (exists) {
        *exists = baseExists;
    }
    return base;
}

QString SyncDaemon::ListNetworks() const
{
    QSet<QString> idSet;
    for (const QString &id : m_local.keys()) {
        idSet.insert(id);
    }
    const QStringList objectNames = QStringList()
            << QStringLiteral("bases") << QStringLiteral("pending")
            << QStringLiteral("conflicts") << QStringLiteral("forgotten")
            << QStringLiteral("errors") << QStringLiteral("pendingTombstones");
    for (const QString &name : objectNames) {
        const QJsonObject object = m_state.value(name).toObject();
        for (const QString &id : object.keys()) {
            idSet.insert(id);
        }
    }
    QList<QString> ids = idSet.values();
    const QStringList categories = QStringList() << QStringLiteral("Needs attention")
            << QStringLiteral("Synced") << QStringLiteral("Pending")
            << QStringLiteral("This device only");
    std::sort(ids.begin(), ids.end(), [this, categories](const QString &a, const QString &b) {
        const int categoryA = categories.indexOf(categoryFor(a));
        const int categoryB = categories.indexOf(categoryFor(b));
        if (categoryA != categoryB) {
            return categoryA < categoryB;
        }
        return recordForDisplay(a).displayName().localeAwareCompare(
                recordForDisplay(b).displayName()) < 0;
    });

    QJsonArray array;
    const QJsonObject errors = m_state.value(QStringLiteral("errors")).toObject();
    const QJsonObject conflicts = m_state.value(QStringLiteral("conflicts")).toObject();
    const QJsonObject forgotten = m_state.value(QStringLiteral("forgotten")).toObject();
    for (const QString &id : ids) {
        bool exists = false;
        const NetworkRecord record = recordForDisplay(id, &exists);
        QJsonObject item;
        if (exists) {
            item = QJsonObject::fromVariantMap(record.toVariantMap(false));
        } else if (errors.contains(id)) {
            item.insert(QStringLiteral("displayName"),
                        QStringLiteral("Remote record %1…").arg(id.left(8)));
            item.insert(QStringLiteral("securityFamily"), QString());
            item.insert(QStringLiteral("qrAvailable"), false);
        } else {
            continue;
        }
        item.insert(QStringLiteral("networkId"), id);
        item.insert(QStringLiteral("category"), categoryFor(id));
        item.insert(QStringLiteral("blocked"), isBlocked(id));
        item.insert(QStringLiteral("presentLocally"), m_local.contains(id));
        item.insert(QStringLiteral("active"), m_local.contains(id)
                    && (m_local.value(id).state == QLatin1String("ready")
                        || m_local.value(id).state == QLatin1String("online")));
        item.insert(QStringLiteral("conflict"), conflicts.contains(id));
        item.insert(QStringLiteral("forgotten"), forgotten.contains(id));
        if (errors.contains(id)) {
            item.insert(QStringLiteral("reason"), errors.value(id).toString());
        } else if (conflicts.contains(id)) {
            item.insert(QStringLiteral("reason"), QStringLiteral("Local and Nextcloud changes conflict"));
        } else if (forgotten.contains(id)) {
            item.insert(QStringLiteral("reason"), QStringLiteral("Forgotten in Sailfish Settings"));
        }
        array.append(item);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QString SyncDaemon::Details(const QString &networkId, bool revealSecret) const
{
    if (!validNetworkId(networkId)) {
        return QStringLiteral("{}");
    }
    bool exists = false;
    const NetworkRecord record = recordForDisplay(networkId, &exists);
    if (!exists) {
        return QStringLiteral("{}");
    }
    QJsonObject result = QJsonObject::fromVariantMap(record.toVariantMap(revealSecret));
    result.insert(QStringLiteral("networkId"), networkId);
    result.insert(QStringLiteral("category"), categoryFor(networkId));
    result.insert(QStringLiteral("blocked"), isBlocked(networkId));
    result.insert(QStringLiteral("presentLocally"), m_local.contains(networkId));
    result.insert(QStringLiteral("active"), m_local.contains(networkId)
                  && (m_local.value(networkId).state == QLatin1String("ready")
                      || m_local.value(networkId).state == QLatin1String("online")));
    const QJsonObject conflicts = m_state.value(QStringLiteral("conflicts")).toObject();
    if (conflicts.contains(networkId)) {
        const QJsonObject conflict = conflicts.value(networkId).toObject();
        QString ignored;
        const NetworkRecord local = recordFromObject(conflict.value(QStringLiteral("local")).toObject(),
                                                     false, &ignored);
        const NetworkRecord remote = recordFromObject(conflict.value(QStringLiteral("remote")).toObject(),
                                                      true, &ignored);
        result.insert(QStringLiteral("localCandidate"),
                      QJsonObject::fromVariantMap(local.toVariantMap(revealSecret)));
        result.insert(QStringLiteral("remoteCandidate"),
                      QJsonObject::fromVariantMap(remote.toVariantMap(revealSecret)));
        result.insert(QStringLiteral("qrAvailable"), false);
    }
    result.insert(QStringLiteral("forgotten"),
                  m_state.value(QStringLiteral("forgotten")).toObject().contains(networkId));
    result.insert(QStringLiteral("error"),
                  m_state.value(QStringLiteral("errors")).toObject().value(networkId).toString());
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

QString SyncDaemon::QrPayload(const QString &networkId) const
{
    if (m_state.value(QStringLiteral("conflicts")).toObject().contains(networkId)) {
        return QString();
    }
    bool exists = false;
    const NetworkRecord record = recordForDisplay(networkId, &exists);
    if (!exists) {
        return QString();
    }
    return record.qrPayload(nullptr);
}

void SyncDaemon::ManualSync()
{
    requestSync(QStringLiteral("manual request"));
}

void SyncDaemon::requestSync(const QString &reason)
{
    if (!m_state.value(QStringLiteral("setupComplete")).toBool()) {
        reportError(QStringLiteral("Complete setup before synchronizing"));
        return;
    }
    if (m_phase != Idle) {
        m_resyncRequested = true;
        return;
    }
    if (m_networkManager && !m_networkManager->connected()) {
        m_lastResult = QStringLiteral("Waiting for a network connection");
        emitChanged();
        return;
    }
    m_syncReason = reason;
    m_lastError.clear();
    m_retryNeeded = false;
    m_phase = Authenticating;
    emitChanged();
    m_accounts.authenticate(m_state.value(QStringLiteral("accountId")).toInt());
}

void SyncDaemon::BeginSetup(int accountId, bool reserved)
{
    Q_UNUSED(reserved)
    if (accountId <= 0) {
        reportError(QStringLiteral("Select an account"));
        return;
    }
    if (m_phase != Idle) {
        reportError(QStringLiteral("Another operation is still running"));
        return;
    }
    if (m_state.value(QStringLiteral("accountId")).toInt() != accountId) {
        SetAccount(accountId);
    }
    m_state.insert(QStringLiteral("accountId"), accountId);
    m_state.insert(QStringLiteral("setupComplete"), false);
    m_state.insert(QStringLiteral("setupStage"), QStringLiteral("creating"));
    saveState();
    m_setupAfterAuthentication = true;
    m_phase = Authenticating;
    emitChanged();
    m_accounts.authenticate(accountId);
}

void SyncDaemon::CompleteSetup(const QStringList &selectedNetworkIds)
{
    if (m_state.value(QStringLiteral("setupStage")).toString() != QLatin1String("selection")) {
        reportError(QStringLiteral("The setup checklist is not ready"));
        return;
    }
    QSet<QString> selected;
    for (const QString &id : selectedNetworkIds) {
        if (validNetworkId(id) && m_local.contains(id)) {
            selected.insert(id);
        }
    }
    QStringList blocks = jsonStringList(m_state.value(QStringLiteral("blocks")).toArray());
    QStringList approved = jsonStringList(m_state.value(QStringLiteral("approvedLocal")).toArray());
    for (const QString &id : m_local.keys()) {
        blocks.removeAll(id);
        approved.removeAll(id);
        if (selected.contains(id)) {
            approved.append(id);
        } else {
            blocks.append(id);
        }
    }
    m_state.insert(QStringLiteral("blocks"), stringJsonArray(blocks));
    m_state.insert(QStringLiteral("approvedLocal"), stringJsonArray(approved));
    m_state.insert(QStringLiteral("setupComplete"), true);
    m_state.insert(QStringLiteral("setupStage"), QStringLiteral("done"));
    saveState();
    emitChanged();
    requestSync(QStringLiteral("initial setup"));
}

void SyncDaemon::SetAccount(int accountId)
{
    if (accountId <= 0 || m_phase != Idle) {
        reportError(QStringLiteral("Cannot switch accounts during an operation"));
        return;
    }
    m_state.insert(QStringLiteral("accountId"), accountId);
    m_state.insert(QStringLiteral("setupComplete"), false);
    m_state.insert(QStringLiteral("setupStage"), QStringLiteral("account"));
    const QStringList resetObjects = QStringList() << QStringLiteral("bases")
            << QStringLiteral("remoteEtags") << QStringLiteral("pending")
            << QStringLiteral("conflicts") << QStringLiteral("forgotten")
            << QStringLiteral("resolutions") << QStringLiteral("errors")
            << QStringLiteral("pendingTombstones") << QStringLiteral("notificationTokens");
    for (const QString &key : resetObjects) {
        m_state.insert(key, QJsonObject());
    }
    m_state.insert(QStringLiteral("approvedLocal"), QJsonArray());
    saveState();
    emitChanged();
}

void SyncDaemon::credentialsReady(const WebDavCredentials &credentials)
{
    m_credentials = credentials;
    m_webDav.configure(credentials);
    if (m_setupAfterAuthentication) {
        m_setupAfterAuthentication = false;
        m_phase = CreatingLayout;
        emitChanged();
        m_webDav.createLayout();
    } else {
        m_phase = ListingRemote;
        emitChanged();
        m_webDav.getFormat();
    }
}

void SyncDaemon::authenticationError(const QString &message)
{
    m_setupAfterAuthentication = false;
    notifyAttention(QStringLiteral("credentials"), QStringLiteral("credentials"), false);
    finishSync(false, message);
}

void SyncDaemon::layoutCreated()
{
    m_state.insert(QStringLiteral("setupStage"), QStringLiteral("selection"));
    saveState();
    requestLocalInventory(false);
}

void SyncDaemon::formatReceived(const QByteArray &data)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        finishSync(false, QStringLiteral("The remote format file is malformed"));
        notifyAttention(QStringLiteral("format"), QStringLiteral("format"), false);
        return;
    }
    const QJsonObject object = document.object();
    const int schemaVersion = object.value(QStringLiteral("schemaVersion")).toInt(-1);
    const int minimumReaderVersion = object.value(
            QStringLiteral("minimumReaderVersion")).toInt(-1);
    if (object.value(QStringLiteral("format")).toString()
            != QLatin1String("org.harbour.flotsam.network-sync")
            || schemaVersion != NetworkRecord::CurrentSchemaVersion
            || minimumReaderVersion < 1
            || minimumReaderVersion > NetworkRecord::CurrentSchemaVersion) {
        finishSync(false, schemaVersion > NetworkRecord::CurrentSchemaVersion
                   || minimumReaderVersion > NetworkRecord::CurrentSchemaVersion
                   ? QStringLiteral("The remote directory uses an unsupported newer format")
                   : QStringLiteral("The remote directory format is unsupported or malformed"));
        notifyAttention(QStringLiteral("format"), QStringLiteral("format"), false);
        return;
    }
    m_webDav.listRecords();
}

void SyncDaemon::recordsListed(const QMap<QString, QString> &etags)
{
    m_remote.clear();
    m_remoteEtags = etags;
    m_remoteUnavailable.clear();
    m_remotePhaseFatal = false;
    m_remainingGets = etags.size();
    if (m_remainingGets == 0) {
        requestLocalInventory(true);
        return;
    }
    m_phase = FetchingRemote;
    emitChanged();
    for (const QString &networkId : etags.keys()) {
        m_webDav.getRecord(networkId);
    }
}

void SyncDaemon::recordReceived(const QString &networkId, const QByteArray &data,
                                const QString &etag)
{
    QString error;
    const NetworkRecord record = NetworkRecord::fromJsonData(data, &error, true);
    if (!error.isEmpty() || record.networkId() != networkId) {
        m_remoteUnavailable.insert(networkId);
        setNetworkError(networkId, error.isEmpty()
                        ? QStringLiteral("Remote filename does not match its record") : error);
        notifyAttention(networkId, QStringLiteral("malformed"), false);
    } else {
        m_remote.insert(networkId, record);
        QJsonObject errors = m_state.value(QStringLiteral("errors")).toObject();
        errors.remove(networkId);
        m_state.insert(QStringLiteral("errors"), errors);
        if (!etag.isEmpty()) {
            m_remoteEtags.insert(networkId, etag);
        }
    }
    --m_remainingGets;
    if (m_remainingGets == 0) {
        if (m_remotePhaseFatal) {
            finishSync(false, m_lastError);
        } else {
            requestLocalInventory(true);
        }
    }
}

void SyncDaemon::requestLocalInventory(bool forSync)
{
    m_inventoryForSync = forSync;
    m_phase = ListingLocal;
    emitChanged();
    QDBusInterface helper(QString::fromLatin1(HelperService), QString::fromLatin1(HelperPath),
                          QString::fromLatin1(HelperInterface), QDBusConnection::systemBus());
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
            helper.asyncCall(QStringLiteral("List")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &SyncDaemon::localInventoryFinished);
}

void SyncDaemon::localInventoryFinished(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<QString> reply = *watcher;
    watcher->deleteLater();
    if (reply.isError()) {
        finishSync(false, QStringLiteral("Cannot enumerate saved Wi-Fi networks: %1")
                   .arg(reply.error().message()));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(reply.value().toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        finishSync(false, QStringLiteral("The ConnMan helper returned a malformed network list"));
        return;
    }
    m_local.clear();
    for (const QJsonValue &value : document.array()) {
        const QJsonObject entry = value.toObject();
        QString error;
        const NetworkRecord record = recordFromObject(entry.value(QStringLiteral("record")).toObject(),
                                                      false, &error);
        const QString id = record.networkId();
        if (!error.isEmpty() || !validNetworkId(id)) {
            continue;
        }
        LocalNetwork network;
        network.record = record;
        network.path = entry.value(QStringLiteral("path")).toString();
        network.state = entry.value(QStringLiteral("state")).toString();
        const bool candidateActive = network.state == QLatin1String("ready")
                || network.state == QLatin1String("online");
        const bool oldActive = m_local.contains(id)
                && (m_local.value(id).state == QLatin1String("ready")
                    || m_local.value(id).state == QLatin1String("online"));
        if (!m_local.contains(id) || (candidateActive && !oldActive)) {
            m_local.insert(id, network);
        }
    }
    const bool forSync = m_inventoryForSync;
    m_inventoryForSync = false;
    if (forSync) {
        reconcileAll();
    } else {
        m_phase = Idle;
        emitChanged();
    }
}

void SyncDaemon::reconcileAll()
{
    m_phase = Applying;
    m_actions.clear();
    QJsonObject bases = m_state.value(QStringLiteral("bases")).toObject();
    QJsonObject pending = m_state.value(QStringLiteral("pending")).toObject();
    QJsonObject conflicts = m_state.value(QStringLiteral("conflicts")).toObject();
    QJsonObject forgotten = m_state.value(QStringLiteral("forgotten")).toObject();
    QJsonObject resolutions = m_state.value(QStringLiteral("resolutions")).toObject();
    QJsonObject storedEtags = m_state.value(QStringLiteral("remoteEtags")).toObject();
    QJsonObject tombstones = m_state.value(QStringLiteral("pendingTombstones")).toObject();

    QSet<QString> ids;
    for (const QString &id : m_local.keys()) ids.insert(id);
    for (const QString &id : m_remote.keys()) ids.insert(id);
    for (const QString &id : bases.keys()) ids.insert(id);
    for (const QString &id : tombstones.keys()) ids.insert(id);

    for (const QString &id : ids) {
        if (m_remoteUnavailable.contains(id)) {
            continue;
        }
        bool hasBase = false;
        NetworkRecord base = baseRecord(id, &hasBase);
        const NetworkRecord *basePtr = hasBase ? &base : nullptr;
        const NetworkRecord *localPtr = m_local.contains(id) ? &m_local[id].record : nullptr;
        const NetworkRecord *remotePtr = m_remote.contains(id) ? &m_remote[id] : nullptr;

        if (tombstones.contains(id)) {
            QString error;
            const NetworkRecord tombstone = recordFromObject(tombstones.value(id).toObject(), true, &error);
            if (!error.isEmpty()) {
                setNetworkError(id, error);
                continue;
            }
            if (remotePtr && NetworkRecord::sameContent(tombstone, *remotePtr)) {
                bases.insert(id, remotePtr->toJson());
                if (localPtr) {
                    SyncAction action;
                    action.type = SyncAction::Remove;
                    action.id = id;
                    action.record = *remotePtr;
                    action.expectedFingerprint = QString::fromLatin1(localPtr->contentFingerprint());
                    m_actions.enqueue(action);
                } else {
                    tombstones.remove(id);
                }
            } else {
                SyncAction action;
                action.type = SyncAction::Upload;
                action.id = id;
                action.record = tombstone;
                action.etag = m_remoteEtags.value(id);
                action.createOnly = !remotePtr;
                m_actions.enqueue(action);
            }
            continue;
        }

        if (resolutions.contains(id)) {
            const QJsonObject resolution = resolutions.value(id).toObject();
            if (!resolutionContextMatches(resolution, localPtr, remotePtr,
                                          m_remoteEtags.value(id))) {
                resolutions.remove(id);
            } else {
                const QString choice = resolution.value(QStringLiteral("choice")).toString();
                if (choice == QLatin1String("nextcloud") && remotePtr) {
                    SyncAction action;
                    action.type = SyncAction::Apply;
                    action.id = id;
                    action.record = *remotePtr;
                    action.etag = m_remoteEtags.value(id);
                    action.expectedFingerprint = localPtr
                            ? QString::fromLatin1(localPtr->contentFingerprint()) : QString();
                    m_actions.enqueue(action);
                } else if ((choice == QLatin1String("device") || choice == QLatin1String("edit"))
                           && (localPtr || resolution.contains(QStringLiteral("record")))) {
                    NetworkRecord source = localPtr ? *localPtr : NetworkRecord();
                    if (resolution.contains(QStringLiteral("record"))) {
                        source = recordFromObject(
                                resolution.value(QStringLiteral("record")).toObject(),
                                false, nullptr);
                    }
                    const NetworkRecord desired = versioned(source, basePtr, m_state);
                    if (choice == QLatin1String("edit") && localPtr
                            && !NetworkRecord::sameContent(*localPtr, desired)) {
                        SyncAction localAction;
                        localAction.type = SyncAction::Apply;
                        localAction.id = id;
                        localAction.record = desired;
                        localAction.expectedFingerprint = QString::fromLatin1(
                                localPtr->contentFingerprint());
                        localAction.localPreparation = true;
                        m_actions.enqueue(localAction);
                    }
                    SyncAction uploadAction;
                    uploadAction.type = SyncAction::Upload;
                    uploadAction.id = id;
                    uploadAction.record = desired;
                    uploadAction.etag = m_remoteEtags.value(id);
                    uploadAction.createOnly = !remotePtr;
                    m_actions.enqueue(uploadAction);
                }
                continue;
            }
        }

        Reconciler::Input input;
        input.base = basePtr;
        input.local = localPtr;
        input.remote = remotePtr;
        input.locallyBlocked = isBlocked(id);
        input.approvedLocal = isApproved(id);
        const Reconciler::Result result = Reconciler::reconcile(input);
        switch (result.action) {
        case Reconciler::NoOp:
        case Reconciler::AdoptMatching:
            if (remotePtr) {
                bases.insert(id, remotePtr->toJson());
                storedEtags.insert(id, m_remoteEtags.value(id));
            }
            pending.remove(id);
            conflicts.remove(id);
            forgotten.remove(id);
            {
                QJsonObject errors = m_state.value(QStringLiteral("errors")).toObject();
                errors.remove(id);
                m_state.insert(QStringLiteral("errors"), errors);
                resolutions.remove(id);
            }
            break;
        case Reconciler::UploadLocal: {
            if (basePtr && basePtr->tombstone) {
                setListMembership(QStringLiteral("blocks"), id, false);
            }
            SyncAction action;
            action.type = SyncAction::Upload;
            action.id = id;
            action.record = versioned(*localPtr, basePtr, m_state);
            action.etag = m_remoteEtags.value(id);
            action.createOnly = !remotePtr;
            m_actions.enqueue(action);
            break;
        }
        case Reconciler::ApplyRemote: {
            SyncAction action;
            action.type = SyncAction::Apply;
            action.id = id;
            action.record = *remotePtr;
            action.etag = m_remoteEtags.value(id);
            action.expectedFingerprint = localPtr
                    ? QString::fromLatin1(localPtr->contentFingerprint()) : QString();
            m_actions.enqueue(action);
            break;
        }
        case Reconciler::RemoveLocal: {
            SyncAction action;
            action.type = SyncAction::Remove;
            action.id = id;
            action.record = *remotePtr;
            action.etag = m_remoteEtags.value(id);
            action.expectedFingerprint = QString::fromLatin1(localPtr->contentFingerprint());
            m_actions.enqueue(action);
            break;
        }
        case Reconciler::NewLocalDecision:
            pending.insert(id, localPtr->toJson());
            notifyAttention(id, QStringLiteral("new"), true);
            break;
        case Reconciler::Conflict: {
            QJsonObject conflict;
            if (basePtr) conflict.insert(QStringLiteral("base"), basePtr->toJson());
            if (localPtr) conflict.insert(QStringLiteral("local"), localPtr->toJson());
            if (remotePtr) conflict.insert(QStringLiteral("remote"), remotePtr->toJson());
            conflict.insert(QStringLiteral("etag"), m_remoteEtags.value(id));
            conflicts.insert(id, conflict);
            notifyAttention(id, QStringLiteral("conflict"), false);
            break;
        }
        case Reconciler::ForgottenDecision:
            forgotten.insert(id, remotePtr ? remotePtr->toJson() : base.toJson());
            if (remotePtr) {
                storedEtags.insert(id, m_remoteEtags.value(id));
            }
            notifyAttention(id, QStringLiteral("forgotten"), false);
            break;
        case Reconciler::Blocked:
            pending.remove(id);
            conflicts.remove(id);
            forgotten.remove(id);
            break;
        case Reconciler::RemoteMissing:
            setNetworkError(id, result.reason);
            notifyAttention(id, QStringLiteral("missing"), false);
            break;
        }
    }
    m_state.insert(QStringLiteral("bases"), bases);
    m_state.insert(QStringLiteral("pending"), pending);
    m_state.insert(QStringLiteral("conflicts"), conflicts);
    m_state.insert(QStringLiteral("forgotten"), forgotten);
    m_state.insert(QStringLiteral("resolutions"), resolutions);
    m_state.insert(QStringLiteral("remoteEtags"), storedEtags);
    m_state.insert(QStringLiteral("pendingTombstones"), tombstones);
    saveState();
    emitChanged();
    processNextAction();
}

void SyncDaemon::processNextAction()
{
    if (m_actions.isEmpty()) {
        const int attention = m_state.value(QStringLiteral("conflicts")).toObject().size()
                + m_state.value(QStringLiteral("forgotten")).toObject().size()
                + m_state.value(QStringLiteral("errors")).toObject().size();
        const QString result = attention
                ? QStringLiteral("Synchronization completed; %1 item(s) need attention").arg(attention)
                : QStringLiteral("Synchronization completed");
        finishSync(m_lastError.isEmpty(), m_lastError.isEmpty() ? result : m_lastError);
        return;
    }
    m_currentAction = m_actions.dequeue();
    if (m_currentAction.type == SyncAction::Upload) {
        m_webDav.putRecord(m_currentAction.id, m_currentAction.record.toJsonData(),
                           m_currentAction.etag, m_currentAction.createOnly);
    } else {
        invokeHelper(m_currentAction);
    }
}

void SyncDaemon::invokeHelper(const SyncAction &action)
{
    QDBusInterface helper(QString::fromLatin1(HelperService), QString::fromLatin1(HelperPath),
                          QString::fromLatin1(HelperInterface), QDBusConnection::systemBus());
    QDBusPendingCall call = action.type == SyncAction::Apply
            ? helper.asyncCall(QStringLiteral("CompareAndApply"), action.expectedFingerprint,
                               QString::fromUtf8(action.record.toJsonData()), true)
            : helper.asyncCall(QStringLiteral("CompareAndRemove"), action.id,
                               action.expectedFingerprint);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &SyncDaemon::mutationFinished);
}

void SyncDaemon::mutationFinished(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<QString> reply = *watcher;
    const QString userOperation = watcher->property("userOperation").toString();
    const QString userNetworkId = watcher->property("networkId").toString();
    watcher->deleteLater();
    if (reply.isError()) {
        const QString message = reply.error().message();
        if (!userOperation.isEmpty()) {
            m_phase = Idle;
            reportError(message);
            if (userOperation == QLatin1String("forget")) {
                requestSync(QStringLiteral("pending tombstone"));
            }
            return;
        }
        setNetworkError(m_currentAction.id, message);
        m_lastError = message;
        m_retryNeeded = true;
        processNextAction();
        return;
    }
    QString message;
    const QString status = helperStatus(reply.value(), &message);
    if (!userOperation.isEmpty()) {
        m_phase = Idle;
        if (status == QLatin1String("Ok")) {
            if (userOperation == QLatin1String("edit")) {
                setListMembership(QStringLiteral("approvedLocal"), userNetworkId, true);
            }
            saveState();
            emitChanged();
            requestSync(userOperation == QLatin1String("edit")
                        ? QStringLiteral("network edited") : QStringLiteral("forget everywhere"));
        } else {
            reportError(message.isEmpty() ? status : message);
            if (userOperation == QLatin1String("forget")) {
                requestSync(QStringLiteral("pending tombstone"));
            }
        }
        return;
    }

    if (status == QLatin1String("Ok")) {
        if (m_currentAction.localPreparation) {
            processNextAction();
            return;
        }
        QJsonObject bases = m_state.value(QStringLiteral("bases")).toObject();
        bases.insert(m_currentAction.id, m_currentAction.record.toJson());
        m_state.insert(QStringLiteral("bases"), bases);
        QJsonObject etags = m_state.value(QStringLiteral("remoteEtags")).toObject();
        etags.insert(m_currentAction.id, m_currentAction.etag);
        m_state.insert(QStringLiteral("remoteEtags"), etags);
        if (m_currentAction.type == SyncAction::Remove && m_currentAction.record.tombstone) {
            QJsonObject tombstones = m_state.value(QStringLiteral("pendingTombstones")).toObject();
            tombstones.remove(m_currentAction.id);
            m_state.insert(QStringLiteral("pendingTombstones"), tombstones);
        }
        clearAttention(m_currentAction.id);
    } else if (status == QLatin1String("Conflict")) {
        QJsonObject resolutions = m_state.value(QStringLiteral("resolutions")).toObject();
        resolutions.remove(m_currentAction.id);
        m_state.insert(QStringLiteral("resolutions"), resolutions);
        setNetworkError(m_currentAction.id,
                        QStringLiteral("The local network changed during synchronization"));
        m_resyncRequested = true;
        if (m_currentAction.localPreparation) {
            m_actions.clear();
        }
    } else {
        const QString error = message.isEmpty() ? status : message;
        setNetworkError(m_currentAction.id, error);
        m_lastError = error;
        m_retryNeeded = true;
        if (m_currentAction.localPreparation) {
            m_actions.clear();
        }
    }
    saveState();
    emitChanged();
    processNextAction();
}

void SyncDaemon::recordWritten(const QString &networkId, const QString &etag)
{
    if (m_currentAction.type != SyncAction::Upload || m_currentAction.id != networkId) {
        return;
    }
    QJsonObject bases = m_state.value(QStringLiteral("bases")).toObject();
    bases.insert(networkId, m_currentAction.record.toJson());
    m_state.insert(QStringLiteral("bases"), bases);
    QJsonObject storedEtags = m_state.value(QStringLiteral("remoteEtags")).toObject();
    storedEtags.insert(networkId, etag.isEmpty() ? m_currentAction.etag : etag);
    m_state.insert(QStringLiteral("remoteEtags"), storedEtags);
    QJsonObject tombstones = m_state.value(QStringLiteral("pendingTombstones")).toObject();
    if (!m_currentAction.record.tombstone || !m_local.contains(networkId)) {
        tombstones.remove(networkId);
    }
    m_state.insert(QStringLiteral("pendingTombstones"), tombstones);
    setListMembership(QStringLiteral("approvedLocal"), networkId, false);
    clearAttention(networkId);
    if (m_currentAction.record.tombstone && m_local.contains(networkId)) {
        SyncAction removal;
        removal.type = SyncAction::Remove;
        removal.id = networkId;
        removal.record = m_currentAction.record;
        removal.etag = etag.isEmpty() ? m_currentAction.etag : etag;
        removal.expectedFingerprint = QString::fromLatin1(
                m_local.value(networkId).record.contentFingerprint());
        m_actions.prepend(removal);
    }
    saveState();
    emitChanged();
    processNextAction();
}

void SyncDaemon::webDavFailed(const QString &operation, const QString &networkId,
                              int httpStatus, const QString &message,
                              bool transient, bool preconditionFailed)
{
    if (httpStatus == 401) {
        m_accounts.markCredentialsExpired(m_credentials.accountId, m_credentials.serviceName);
        notifyAttention(QStringLiteral("credentials"), QStringLiteral("credentials"), false);
    }
    if (operation == QLatin1String("get")) {
        m_remoteUnavailable.insert(networkId);
        setNetworkError(networkId, message);
        notifyAttention(networkId, QStringLiteral("remote"), false);
        m_retryNeeded = m_retryNeeded || transient;
        if (transient || httpStatus == 401) {
            m_lastError = message;
        }
        if (httpStatus == 401) {
            m_remotePhaseFatal = true;
        }
        --m_remainingGets;
        if (m_remainingGets == 0) {
            if (m_remotePhaseFatal) {
                finishSync(false, m_lastError);
            } else {
                requestLocalInventory(true);
            }
        }
        return;
    }
    if (operation == QLatin1String("put")) {
        if (httpStatus == 401) {
            m_actions.clear();
            m_lastError = message;
            finishSync(false, message);
            return;
        }
        if (preconditionFailed) {
            m_actions.clear();
            QJsonObject resolutions = m_state.value(QStringLiteral("resolutions")).toObject();
            resolutions.remove(networkId);
            m_state.insert(QStringLiteral("resolutions"), resolutions);
            ++m_preconditionRetries;
            if (m_preconditionRetries <= 3) {
                m_resyncRequested = true;
                m_lastError = QStringLiteral("Nextcloud changed during upload; refetching before reconciliation");
            } else {
                m_resyncRequested = false;
                m_retryNeeded = true;
                setNetworkError(networkId,
                                QStringLiteral("Nextcloud kept changing during reconciliation"));
                m_lastError = QStringLiteral("Nextcloud kept changing; synchronization will retry later");
            }
            finishSync(false, m_lastError);
            return;
        }
        setNetworkError(networkId, message);
        m_lastError = message;
        m_retryNeeded = m_retryNeeded || transient;
        processNextAction();
        return;
    }
    if ((operation == QLatin1String("format") ||
         operation == QLatin1String("list")) && httpStatus == 404) {
        notifyAttention(QStringLiteral("directory"), QStringLiteral("directory"), false);
        m_lastError = QStringLiteral("The NetworkSync directory was removed from Nextcloud");
        finishSync(false, m_lastError);
        return;
    }
    m_retryNeeded = transient;
    finishSync(false, message);
}

void SyncDaemon::finishSync(bool success, const QString &message)
{
    m_phase = Idle;
    m_lastSync = QDateTime::currentDateTimeUtc();
    if (success) {
        m_lastResult = message;
        m_lastError.clear();
        m_state.insert(QStringLiteral("retryAttempt"), 0);
        m_retryTimer.stop();
        m_preconditionRetries = 0;
    } else {
        m_lastError = message;
        m_lastResult = QStringLiteral("Synchronization paused");
        if (m_retryNeeded) {
            scheduleRetry(message);
        }
    }
    m_state.insert(QStringLiteral("lastResult"), m_lastResult);
    m_state.insert(QStringLiteral("lastError"), m_lastError);
    m_state.insert(QStringLiteral("lastSync"), m_lastSync.toString(Qt::ISODate));
    saveState();
    emitChanged();
    if (m_resyncRequested) {
        m_resyncRequested = false;
        QTimer::singleShot(0, this, [this]() {
            requestSync(QStringLiteral("conditional-write reconciliation"));
        });
    }
}

void SyncDaemon::scheduleRetry(const QString &message)
{
    Q_UNUSED(message)
    static const int minutes[] = { 1, 5, 15, 60 };
    const int attempt = qBound(0, m_state.value(QStringLiteral("retryAttempt")).toInt(), 3);
    m_state.insert(QStringLiteral("retryAttempt"), qMin(attempt + 1, 3));
    m_retryTimer.start(minutes[attempt] * 60 * 1000);
}

void SyncDaemon::retryTimeout()
{
    requestSync(QStringLiteral("retry"));
}

void SyncDaemon::networkStateChanged(const QString &state)
{
    if ((state == QLatin1String("online") || state == QLatin1String("ready"))
            && m_state.value(QStringLiteral("setupComplete")).toBool()) {
        requestSync(QStringLiteral("network online"));
    }
}

void SyncDaemon::networkServicesChanged()
{
    if (m_phase == Idle) {
        requestLocalInventory(false);
    }
    if (m_state.value(QStringLiteral("setupComplete")).toBool()) {
        m_eventTimer.start();
    }
}

QStringList SyncDaemon::jsonStringList(const QJsonArray &array)
{
    QStringList result;
    for (const QJsonValue &value : array) {
        if (value.isString() && !result.contains(value.toString())) {
            result.append(value.toString());
        }
    }
    return result;
}

QJsonArray SyncDaemon::stringJsonArray(const QStringList &strings)
{
    QJsonArray result;
    for (const QString &value : strings) {
        result.append(value);
    }
    return result;
}

bool SyncDaemon::isBlocked(const QString &networkId) const
{
    return jsonStringList(m_state.value(QStringLiteral("blocks")).toArray()).contains(networkId);
}

bool SyncDaemon::isApproved(const QString &networkId) const
{
    return jsonStringList(m_state.value(QStringLiteral("approvedLocal")).toArray()).contains(networkId);
}

void SyncDaemon::setListMembership(const QString &key, const QString &value, bool present)
{
    QStringList list = jsonStringList(m_state.value(key).toArray());
    list.removeAll(value);
    if (present) {
        list.append(value);
    }
    m_state.insert(key, stringJsonArray(list));
}

void SyncDaemon::NewNetworkChoice(const QString &networkId, const QString &choice)
{
    if (!validNetworkId(networkId) || !m_local.contains(networkId)
            || !m_state.value(QStringLiteral("pending")).toObject().contains(networkId)) {
        reportError(QStringLiteral("That pending network is no longer available"));
        return;
    }
    if (choice == QLatin1String("sync")) {
        setListMembership(QStringLiteral("blocks"), networkId, false);
        setListMembership(QStringLiteral("approvedLocal"), networkId, true);
    } else if (choice == QLatin1String("keep")) {
        setListMembership(QStringLiteral("blocks"), networkId, true);
        setListMembership(QStringLiteral("approvedLocal"), networkId, false);
    } else {
        reportError(QStringLiteral("Unknown new-network choice"));
        return;
    }
    clearAttention(networkId);
    saveState();
    emitChanged();
    if (choice == QLatin1String("sync")) {
        requestSync(QStringLiteral("new-network choice"));
    }
}

void SyncDaemon::Edit(const QString &networkId, const QString &passphrase,
                      bool hidden, bool autoConnect, bool activeEditConfirmed)
{
    if (!m_local.contains(networkId)) {
        reportError(QStringLiteral("The network is not installed on this device"));
        return;
    }
    NetworkRecord record = m_local.value(networkId).record;
    record.passphrase = passphrase;
    record.hidden = hidden;
    record.autoConnect = autoConnect;
    QString error;
    if (!record.isValid(&error, false)) {
        reportError(error);
        return;
    }
    if (record.networkId() != networkId) {
        reportError(QStringLiteral("SSID and security family are immutable"));
        return;
    }
    const bool active = m_local.value(networkId).state == QLatin1String("ready")
            || m_local.value(networkId).state == QLatin1String("online");
    if (active && !activeEditConfirmed) {
        reportError(QStringLiteral("Confirm disconnecting and reconnecting the active network"));
        return;
    }
    applyUserRecord(record, activeEditConfirmed);
}

void SyncDaemon::applyUserRecord(const NetworkRecord &source, bool activeEditConfirmed)
{
    if (m_phase != Idle) {
        reportError(QStringLiteral("Wait for synchronization to finish"));
        return;
    }
    bool hasBase = false;
    const NetworkRecord base = baseRecord(source.networkId(), &hasBase);
    NetworkRecord record = versioned(source, hasBase ? &base : nullptr, m_state);
    const QString id = record.networkId();
    const QString expected = m_local.contains(id)
            ? QString::fromLatin1(m_local.value(id).record.contentFingerprint()) : QString();
    QDBusInterface helper(QString::fromLatin1(HelperService), QString::fromLatin1(HelperPath),
                          QString::fromLatin1(HelperInterface), QDBusConnection::systemBus());
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
            helper.asyncCall(QStringLiteral("CompareAndApply"), expected,
                             QString::fromUtf8(record.toJsonData()), activeEditConfirmed), this);
    watcher->setProperty("userOperation", QStringLiteral("edit"));
    watcher->setProperty("networkId", id);
    m_phase = Applying;
    emitChanged();
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &SyncDaemon::mutationFinished);
}

void SyncDaemon::Block(const QString &networkId)
{
    if (!validNetworkId(networkId)) return;
    setListMembership(QStringLiteral("blocks"), networkId, true);
    setListMembership(QStringLiteral("approvedLocal"), networkId, false);
    clearAttention(networkId);
    saveState();
    emitChanged();
}

void SyncDaemon::Unblock(const QString &networkId)
{
    if (!validNetworkId(networkId)) return;
    setListMembership(QStringLiteral("blocks"), networkId, false);
    setListMembership(QStringLiteral("approvedLocal"), networkId, true);
    saveState();
    emitChanged();
    requestSync(QStringLiteral("network unblocked"));
}

void SyncDaemon::ResolveConflict(const QString &networkId, const QString &choice,
                                 const QVariantMap &editedFields)
{
    const QJsonObject conflicts = m_state.value(QStringLiteral("conflicts")).toObject();
    if (!conflicts.contains(networkId)) {
        reportError(QStringLiteral("That conflict is stale"));
        return;
    }
    if (choice != QLatin1String("device") && choice != QLatin1String("nextcloud")
            && choice != QLatin1String("edit")) {
        reportError(QStringLiteral("Unknown conflict resolution"));
        return;
    }
    const QJsonObject conflict = conflicts.value(networkId).toObject();
    QString contextError;
    const bool hasLocalCandidate = conflict.contains(QStringLiteral("local"));
    const bool hasRemoteCandidate = conflict.contains(QStringLiteral("remote"));
    const NetworkRecord localCandidate = hasLocalCandidate
            ? recordFromObject(conflict.value(QStringLiteral("local")).toObject(),
                               false, &contextError) : NetworkRecord();
    const NetworkRecord remoteCandidate = hasRemoteCandidate
            ? recordFromObject(conflict.value(QStringLiteral("remote")).toObject(),
                               true, &contextError) : NetworkRecord();
    if (!contextError.isEmpty() || !hasRemoteCandidate) {
        reportError(QStringLiteral("That conflict is stale"));
        return;
    }
    QJsonObject resolution;
    resolution.insert(QStringLiteral("choice"), choice);
    addResolutionContext(&resolution,
                         hasLocalCandidate ? &localCandidate : nullptr,
                         &remoteCandidate,
                         conflict.value(QStringLiteral("etag")).toString());
    if (choice == QLatin1String("edit")) {
        if (!hasLocalCandidate) {
            reportError(QStringLiteral("That conflict has no local candidate to edit"));
            return;
        }
        NetworkRecord record = localCandidate;
        record.passphrase = editedFields.value(QStringLiteral("passphrase"), record.passphrase).toString();
        record.hidden = editedFields.value(QStringLiteral("hidden"), record.hidden).toBool();
        record.autoConnect = editedFields.value(QStringLiteral("autoconnect"), record.autoConnect).toBool();
        const bool active = m_local.contains(networkId)
                && (m_local.value(networkId).state == QLatin1String("ready")
                    || m_local.value(networkId).state == QLatin1String("online"));
        if (active && !editedFields.value(QStringLiteral("activeConfirmed")).toBool()) {
            reportError(QStringLiteral("Confirm disconnecting and reconnecting the active network"));
            return;
        }
        QString error;
        if (!record.isValid(&error, false)) {
            reportError(error);
            return;
        }
        resolution.insert(QStringLiteral("record"), record.toJson());
    }
    QJsonObject resolutions = m_state.value(QStringLiteral("resolutions")).toObject();
    resolutions.insert(networkId, resolution);
    m_state.insert(QStringLiteral("resolutions"), resolutions);
    saveState();
    requestSync(QStringLiteral("conflict resolution"));
}

void SyncDaemon::ResolveForgotten(const QString &networkId, const QString &choice)
{
    const QJsonObject forgotten = m_state.value(QStringLiteral("forgotten")).toObject();
    if (!forgotten.contains(networkId)) {
        reportError(QStringLiteral("That forgotten-network decision is stale"));
        return;
    }
    if (choice == QLatin1String("restore")) {
        QString contextError;
        const NetworkRecord remote = recordFromObject(forgotten.value(networkId).toObject(),
                                                      true, &contextError);
        if (!contextError.isEmpty()) {
            reportError(QStringLiteral("That forgotten-network decision is stale"));
            return;
        }
        QJsonObject resolutions = m_state.value(QStringLiteral("resolutions")).toObject();
        QJsonObject resolution;
        resolution.insert(QStringLiteral("choice"), QStringLiteral("nextcloud"));
        addResolutionContext(&resolution, nullptr, &remote,
                             m_state.value(QStringLiteral("remoteEtags")).toObject()
                             .value(networkId).toString());
        resolutions.insert(networkId, resolution);
        m_state.insert(QStringLiteral("resolutions"), resolutions);
        setListMembership(QStringLiteral("blocks"), networkId, false);
        saveState();
        requestSync(QStringLiteral("restore forgotten network"));
    } else if (choice == QLatin1String("keep-off")) {
        setListMembership(QStringLiteral("blocks"), networkId, true);
        clearAttention(networkId);
        saveState();
        emitChanged();
    } else if (choice == QLatin1String("everywhere")) {
        ForgetEverywhere(networkId);
    } else {
        reportError(QStringLiteral("Unknown forgotten-network resolution"));
    }
}

void SyncDaemon::ForgetEverywhere(const QString &networkId)
{
    bool exists = false;
    NetworkRecord source = recordForDisplay(networkId, &exists);
    if (!exists || m_phase != Idle) {
        reportError(QStringLiteral("The network cannot be forgotten right now"));
        return;
    }
    bool hasBase = false;
    const NetworkRecord base = baseRecord(networkId, &hasBase);
    NetworkRecord tombstone = source;
    tombstone.tombstone = true;
    tombstone.passphrase.clear();
    tombstone.revision = QUuid::createUuid().toString();
    tombstone.parentRevision = hasBase ? base.revision : source.revision;
    tombstone.deviceUuid = m_state.value(QStringLiteral("deviceUuid")).toString();
    tombstone.deviceLabel = m_state.value(QStringLiteral("deviceLabel")).toString();
    tombstone.updatedAt = QDateTime::currentDateTimeUtc();
    QJsonObject tombstones = m_state.value(QStringLiteral("pendingTombstones")).toObject();
    tombstones.insert(networkId, tombstone.toJson());
    m_state.insert(QStringLiteral("pendingTombstones"), tombstones);
    setListMembership(QStringLiteral("blocks"), networkId, false);
    saveState();

    if (!m_local.contains(networkId)) {
        requestSync(QStringLiteral("forget everywhere"));
        return;
    }
    QDBusInterface helper(QString::fromLatin1(HelperService), QString::fromLatin1(HelperPath),
                          QString::fromLatin1(HelperInterface), QDBusConnection::systemBus());
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
            helper.asyncCall(QStringLiteral("CompareAndRemove"), networkId,
                             QString::fromLatin1(m_local.value(networkId).record.contentFingerprint())), this);
    watcher->setProperty("userOperation", QStringLiteral("forget"));
    watcher->setProperty("networkId", networkId);
    m_phase = Applying;
    emitChanged();
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &SyncDaemon::mutationFinished);
}

void SyncDaemon::notifyAttention(const QString &networkId, const QString &kind, bool actions)
{
    QJsonObject tokens = m_state.value(QStringLiteral("notificationTokens")).toObject();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (const QString &token : tokens.keys()) {
        const QJsonObject entry = tokens.value(token).toObject();
        const QDateTime created = QDateTime::fromString(
                entry.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
        const qint64 age = created.secsTo(now);
        if (!created.isValid() || age < 0 || age > 24 * 60 * 60) {
            tokens.remove(token);
        } else if (entry.value(QStringLiteral("networkId")).toString() == networkId
                   && entry.value(QStringLiteral("kind")).toString() == kind) {
            m_state.insert(QStringLiteral("notificationTokens"), tokens);
            return;
        }
    }
    const QString token = QUuid::createUuid().toString();
    QJsonObject entry;
    entry.insert(QStringLiteral("networkId"), networkId);
    entry.insert(QStringLiteral("kind"), kind);
    entry.insert(QStringLiteral("createdAt"), now.toString(Qt::ISODate));
    tokens.insert(token, entry);
    m_state.insert(QStringLiteral("notificationTokens"), tokens);
    saveState();
    const QString key = kind + QLatin1Char(':') + networkId;
    if (actions) {
        m_notifications.showNewNetwork(key, token, recordForDisplay(networkId).displayName());
    } else {
        m_notifications.showAttention(key, kind);
    }
}

void SyncDaemon::notificationInvoked(const QString &token, const QString &action)
{
    NotificationAction(token, action);
}

void SyncDaemon::NotificationAction(const QString &token, const QString &action)
{
    QJsonObject tokens = m_state.value(QStringLiteral("notificationTokens")).toObject();
    const NotificationTokenResult result = NotificationToken::take(
            &tokens, token, action,
            m_state.value(QStringLiteral("pending")).toObject().keys(),
            QDateTime::currentDateTimeUtc());
    m_state.insert(QStringLiteral("notificationTokens"), tokens);
    saveState();
    if (!result.valid) {
        reportError(result.error);
        return;
    }
    NewNetworkChoice(result.networkId, action);
}

void SyncDaemon::clearAttention(const QString &networkId)
{
    const QStringList keys = QStringList() << QStringLiteral("pending")
            << QStringLiteral("conflicts") << QStringLiteral("forgotten")
            << QStringLiteral("errors") << QStringLiteral("resolutions");
    for (const QString &key : keys) {
        QJsonObject object = m_state.value(key).toObject();
        object.remove(networkId);
        m_state.insert(key, object);
    }
    QJsonObject tokens = m_state.value(QStringLiteral("notificationTokens")).toObject();
    for (const QString &token : tokens.keys()) {
        if (tokens.value(token).toObject().value(QStringLiteral("networkId")).toString()
                == networkId) {
            tokens.remove(token);
        }
    }
    m_state.insert(QStringLiteral("notificationTokens"), tokens);
}

void SyncDaemon::setNetworkError(const QString &networkId, const QString &message)
{
    QJsonObject errors = m_state.value(QStringLiteral("errors")).toObject();
    errors.insert(networkId, message);
    m_state.insert(QStringLiteral("errors"), errors);
}

void SyncDaemon::saveState()
{
    m_stateStore.setObject(m_state);
    QString error;
    if (!m_stateStore.save(&error)) {
        m_lastError = QStringLiteral("Cannot save private synchronization state: %1").arg(error);
        emit OperationFailed(m_lastError);
    }
}

void SyncDaemon::emitChanged()
{
    emit StatusChanged(Status());
    emit NetworksChanged();
}

void SyncDaemon::reportError(const QString &message)
{
    m_lastError = message;
    m_state.insert(QStringLiteral("lastError"), m_lastError);
    saveState();
    emit OperationFailed(message);
    emitChanged();
}

}
