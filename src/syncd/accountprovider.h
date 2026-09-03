/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FLOTSAM_ACCOUNTPROVIDER_H
#define FLOTSAM_ACCOUNTPROVIDER_H

#include "webdavcredentials.h"

#include <QObject>
#include <QString>
#include <QVariantMap>

#include <accountauthenticator.h>

namespace Flotsam {

class AccountProvider : public QObject
{
    Q_OBJECT

public:
    explicit AccountProvider(QObject *parent = nullptr);

    QString accountsJson() const;
    void authenticate(int accountId);
    void markCredentialsExpired(int accountId, const QString &serviceName);

signals:
    void credentialsReady(const Flotsam::WebDavCredentials &credentials);
    void authenticationError(const QString &message);

private slots:
    void signInCompleted(int accountId, const QString &serviceName,
                         const AccountAuthenticatorCredentials &credentials);
    void signInError(int accountId, const QString &serviceName,
                     const QString &errorString);

private:
    QString serviceForAccount(int accountId) const;

    AccountAuthenticator m_authenticator;
};

}

#endif
