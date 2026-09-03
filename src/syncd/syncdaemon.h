/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FLOTSAM_SYNCDAEMON_H
#define FLOTSAM_SYNCDAEMON_H

#include "accountprovider.h"
#include "atomicstate.h"
#include "networkrecord.h"
#include "notificationmanager.h"
#include "webdavclient.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QTimer>

class NetworkManager;
class QDBusPendingCallWatcher;

namespace Flotsam {

class SyncDaemon : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.harbour.flotsam.Sync")

public:
    explicit SyncDaemon(QObject *parent = nullptr);

    bool initialize(QString *error = nullptr);

public slots:
    QVariantMap Status() const;
    QString Accounts() const;
    QString ListNetworks() const;
    QString Details(const QString &networkId, bool revealSecret) const;
    QString QrPayload(const QString &networkId) const;
    void ManualSync();
    void BeginSetup(int accountId, bool reserved);
    void CompleteSetup(const QStringList &selectedNetworkIds);
    void SetAccount(int accountId);
    void NewNetworkChoice(const QString &networkId, const QString &choice);
    void Edit(const QString &networkId, const QString &passphrase,
              bool hidden, bool autoConnect, bool activeEditConfirmed);
    void Block(const QString &networkId);
    void Unblock(const QString &networkId);
    void ResolveConflict(const QString &networkId, const QString &choice,
                         const QVariantMap &editedFields);
    void ResolveForgotten(const QString &networkId, const QString &choice);
    void ForgetEverywhere(const QString &networkId);
    void NotificationAction(const QString &token, const QString &action);

signals:
    void StatusChanged(const QVariantMap &status);
    void NetworksChanged();
    void OperationFailed(const QString &message);

private slots:
    void credentialsReady(const Flotsam::WebDavCredentials &credentials);
    void authenticationError(const QString &message);
    void layoutCreated();
    void formatReceived(const QByteArray &data);
    void recordsListed(const QMap<QString, QString> &etags);
    void recordReceived(const QString &networkId, const QByteArray &data,
                        const QString &etag);
    void recordWritten(const QString &networkId, const QString &etag);
    void webDavFailed(const QString &operation, const QString &networkId,
                      int httpStatus, const QString &message,
                      bool transient, bool preconditionFailed);
    void localInventoryFinished(QDBusPendingCallWatcher *watcher);
    void mutationFinished(QDBusPendingCallWatcher *watcher);
    void networkStateChanged(const QString &state);
    void networkServicesChanged();
    void retryTimeout();
    void notificationInvoked(const QString &token, const QString &action);

private:
    enum Phase {
        Idle,
        Authenticating,
        CreatingLayout,
        ListingRemote,
        FetchingRemote,
        ListingLocal,
        Applying
    };

    struct LocalNetwork {
        NetworkRecord record;
        QString path;
        QString state;
    };

    struct SyncAction {
        enum Type { Upload, Apply, Remove } type = Upload;
        QString id;
        NetworkRecord record;
        QString etag;
        QString expectedFingerprint;
        bool createOnly = false;
        bool localPreparation = false;
    };

    void requestSync(const QString &reason);
    void requestLocalInventory(bool forSync);
    void reconcileAll();
    void processNextAction();
    void finishSync(bool success, const QString &message);
    void scheduleRetry(const QString &message);
    void saveState();
    void emitChanged();
    void reportError(const QString &message);
    void setNetworkError(const QString &networkId, const QString &message);
    void clearAttention(const QString &networkId);
    void notifyAttention(const QString &networkId, const QString &kind, bool actions);
    void invokeHelper(const SyncAction &action);
    void applyUserRecord(const NetworkRecord &record, bool activeEditConfirmed);

    NetworkRecord baseRecord(const QString &networkId, bool *exists = nullptr) const;
    NetworkRecord recordForDisplay(const QString &networkId, bool *exists = nullptr) const;
    static NetworkRecord recordFromObject(const QJsonObject &object, bool requireRevision,
                                          QString *error = nullptr);
    static QStringList jsonStringList(const QJsonArray &array);
    static QJsonArray stringJsonArray(const QStringList &strings);
    QString categoryFor(const QString &networkId) const;
    bool isBlocked(const QString &networkId) const;
    bool isApproved(const QString &networkId) const;
    void setListMembership(const QString &key, const QString &value, bool present);

    AtomicState m_stateStore;
    QJsonObject m_state;
    AccountProvider m_accounts;
    WebDavClient m_webDav;
    NotificationManager m_notifications;
    NetworkManager *m_networkManager = nullptr;
    QTimer m_periodicTimer;
    QTimer m_eventTimer;
    QTimer m_retryTimer;
    Phase m_phase = Idle;
    bool m_inventoryForSync = false;
    bool m_resyncRequested = false;
    bool m_setupAfterAuthentication = false;
    bool m_retryNeeded = false;
    bool m_remotePhaseFatal = false;
    int m_remainingGets = 0;
    int m_preconditionRetries = 0;
    QString m_syncReason;
    QString m_lastError;
    QString m_lastResult;
    QDateTime m_lastSync;
    WebDavCredentials m_credentials;
    QMap<QString, LocalNetwork> m_local;
    QMap<QString, NetworkRecord> m_remote;
    QMap<QString, QString> m_remoteEtags;
    QSet<QString> m_remoteUnavailable;
    QQueue<SyncAction> m_actions;
    SyncAction m_currentAction;
};

}

#endif
