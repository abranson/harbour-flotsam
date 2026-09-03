/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FLOTSAM_APPCONTROLLER_H
#define FLOTSAM_APPCONTROLLER_H

#include <QDBusAbstractInterface>
#include <QDBusConnection>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class QDBusPendingCallWatcher;

namespace Flotsam {

class SyncInterfaceProxy : public QDBusAbstractInterface
{
    Q_OBJECT

public:
    explicit SyncInterfaceProxy(QObject *parent = nullptr);

signals:
    void StatusChanged(const QVariantMap &status);
    void NetworksChanged();
    void OperationFailed(const QString &message);
};

class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY accountsChanged)
    Q_PROPERTY(QVariantList networks READ networks NOTIFY networksChanged)
    Q_PROPERTY(QVariantMap details READ details NOTIFY detailsChanged)
    Q_PROPERTY(QString qrPayload READ qrPayload NOTIFY qrPayloadChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit AppController(QObject *parent = nullptr);

    QVariantMap status() const;
    QVariantList accounts() const;
    QVariantList networks() const;
    QVariantMap details() const;
    QString qrPayload() const;
    QString error() const;
    bool busy() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void loadDetails(const QString &networkId, bool revealSecret = false);
    Q_INVOKABLE void loadQr(const QString &networkId);
    Q_INVOKABLE QString passphraseError(const QString &securityFamily,
                                        const QString &passphrase) const;
    Q_INVOKABLE void manualSync();
    Q_INVOKABLE void beginSetup(int accountId);
    Q_INVOKABLE void completeSetup(const QVariantList &selectedNetworkIds);
    Q_INVOKABLE void setAccount(int accountId);
    Q_INVOKABLE void newNetworkChoice(const QString &networkId, const QString &choice);
    Q_INVOKABLE void edit(const QString &networkId, const QString &passphrase,
                          bool hidden, bool autoConnect, bool activeEditConfirmed);
    Q_INVOKABLE void block(const QString &networkId);
    Q_INVOKABLE void unblock(const QString &networkId);
    Q_INVOKABLE void resolveConflict(const QString &networkId, const QString &choice,
                                     const QVariantMap &editedFields = QVariantMap());
    Q_INVOKABLE void resolveForgotten(const QString &networkId, const QString &choice);
    Q_INVOKABLE void forgetEverywhere(const QString &networkId);

signals:
    void statusChanged();
    void accountsChanged();
    void networksChanged();
    void detailsChanged();
    void qrPayloadChanged();
    void errorChanged();
    void busyChanged();

private slots:
    void callFinished(QDBusPendingCallWatcher *watcher);
    void remoteStatusChanged(const QVariantMap &status);
    void remoteNetworksChanged();
    void remoteError(const QString &message);

private:
    void startStringCall(const QString &method, const QString &kind,
                         const QVariantList &arguments = QVariantList());
    void startVoidCall(const QString &method, const QVariantList &arguments = QVariantList());
    void setError(const QString &error);
    static QVariantList parseArray(const QString &json, QString *error);
    static QVariantMap parseObject(const QString &json, QString *error);

    SyncInterfaceProxy *m_interface;
    QVariantMap m_status;
    QVariantList m_accounts;
    QVariantList m_networks;
    QVariantMap m_details;
    QString m_qrPayload;
    QString m_error;
    int m_pendingCalls = 0;
};

}

#endif
