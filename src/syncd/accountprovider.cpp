/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "accountprovider.h"

#include <Accounts/Account>
#include <Accounts/Manager>
#include <Accounts/Provider>
#include <Accounts/Service>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace Flotsam {

AccountProvider::AccountProvider(QObject *parent)
    : QObject(parent)
    , m_authenticator(this)
{
    qRegisterMetaType<WebDavCredentials>();
    connect(&m_authenticator, &AccountAuthenticator::signInCompleted,
            this, &AccountProvider::signInCompleted);
    connect(&m_authenticator, &AccountAuthenticator::signInError,
            this, &AccountProvider::signInError);
}

QString AccountProvider::serviceForAccount(int accountId) const
{
    Accounts::Manager manager;
    Accounts::Account *account = manager.account(accountId);
    if (!account || !account->isEnabled()) {
        delete account;
        return QString();
    }
    const QStringList preference = QStringList()
            << QStringLiteral("nextcloud-sharing")
            << QStringLiteral("nextcloud-backup")
            << QStringLiteral("nextcloud-images")
            << QStringLiteral("nextcloud-carddav")
            << QStringLiteral("nextcloud-caldav");
    const Accounts::ServiceList services = account->enabledServices();
    QString selected;
    for (const QString &candidate : preference) {
        for (const Accounts::Service &service : services) {
            if (service.name() == candidate) {
                selected = candidate;
                break;
            }
        }
        if (!selected.isEmpty()) {
            break;
        }
    }
    delete account;
    return selected;
}

QString AccountProvider::accountsJson() const
{
    Accounts::Manager manager;
    QJsonArray array;
    for (Accounts::AccountId accountId : manager.accountList()) {
        Accounts::Account *account = manager.account(accountId);
        if (!account || !account->isEnabled()) {
            delete account;
            continue;
        }
        const QString service = serviceForAccount(accountId);
        if (account->providerName() != QLatin1String("nextcloud") || service.isEmpty()) {
            delete account;
            continue;
        }
        QJsonObject item;
        item.insert(QStringLiteral("id"), int(accountId));
        item.insert(QStringLiteral("displayName"), account->displayName());
        item.insert(QStringLiteral("provider"), manager.provider(account->providerName()).displayName());
        item.insert(QStringLiteral("service"), service);
        array.append(item);
        delete account;
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

void AccountProvider::authenticate(int accountId)
{
    const QString service = serviceForAccount(accountId);
    if (service.isEmpty()) {
        emit authenticationError(QStringLiteral("The selected Nextcloud account has no enabled WebDAV service"));
        return;
    }
    m_authenticator.signIn(accountId, service);
}

void AccountProvider::markCredentialsExpired(int accountId, const QString &serviceName)
{
    m_authenticator.setCredentialsNeedUpdate(accountId, serviceName);
}

void AccountProvider::signInCompleted(int accountId, const QString &serviceName,
                                      const AccountAuthenticatorCredentials &credentials)
{
    WebDavCredentials result;
    result.accountId = accountId;
    result.serviceName = serviceName;
    result.serverAddress = credentials.serviceSettings.value(
            QStringLiteral("server_address")).toString();
    result.webDavPath = credentials.serviceSettings.value(
            QStringLiteral("webdav_path")).toString();
    result.username = credentials.username;
    result.password = credentials.password;
    result.accessToken = credentials.accessToken;
    result.ignoreSslErrors = credentials.serviceSettings.value(
            QStringLiteral("ignore_ssl_errors")).toBool();
    const bool needsDefaultWebDavPath = result.webDavPath.isEmpty();
    if (needsDefaultWebDavPath) {
        QUrl server(result.serverAddress);
        QString serverPath = server.path();
        if (serverPath.endsWith(QLatin1Char('/'))) {
            serverPath.chop(1);
        }
        result.webDavPath = serverPath + QStringLiteral("/remote.php/dav/files/")
                + result.username;
    }
    const bool basicCredentialsIncomplete = result.accessToken.isEmpty()
            && (result.username.isEmpty() || result.password.isEmpty());
    if (result.serverAddress.isEmpty() || result.webDavPath.isEmpty()
            || basicCredentialsIncomplete
            || (needsDefaultWebDavPath && result.username.isEmpty())) {
        emit authenticationError(QStringLiteral("Nextcloud credentials are incomplete"));
        return;
    }
    emit credentialsReady(result);
}

void AccountProvider::signInError(int accountId, const QString &serviceName,
                                  const QString &errorString)
{
    Q_UNUSED(accountId)
    Q_UNUSED(serviceName)
    Q_UNUSED(errorString)
    emit authenticationError(QStringLiteral("Unable to retrieve Nextcloud credentials"));
}

}
