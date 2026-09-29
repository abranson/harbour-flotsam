/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FLOTSAM_CONNMANHELPER_H
#define FLOTSAM_CONNMANHELPER_H

#include "connmanbackend.h"

#include <QDBusContext>
#include <QObject>

namespace Flotsam {

class ConnmanHelper : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.harbour.flotsam.Connman")

public:
    explicit ConnmanHelper(ConnmanBackend *backend, QObject *parent = nullptr);

public slots:
    Q_SCRIPTABLE QString List();
    Q_SCRIPTABLE QString Export(const QString &networkId);
    Q_SCRIPTABLE QString CompareAndApply(const QString &expectedFingerprint,
                            const QString &recordJson,
                            bool allowActiveReconnect);
    Q_SCRIPTABLE QString CompareAndRemove(const QString &networkId,
                             const QString &expectedFingerprint);

private:
    struct ExportedService {
        ConnmanServiceData service;
        NetworkRecord record;
        QString passphrase;
    };

    bool authorizeCaller();
    QList<ExportedService> exportedServices(QString *error);
    QList<ExportedService> matchingServices(const QString &networkId, QString *error);
    static QString result(const QString &status, const QString &message = QString(),
                          const QString &path = QString());
    bool rollback(const ExportedService &original, const QString &newPath);

    ConnmanBackend *m_backend;
};

}

#endif
