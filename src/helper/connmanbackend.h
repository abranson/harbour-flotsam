/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FLOTSAM_CONNMANBACKEND_H
#define FLOTSAM_CONNMANBACKEND_H

#include "connmanutil.h"

#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QList>
#include <QString>
#include <QVariantMap>

namespace Flotsam {

struct ConnmanServiceData {
    QString path;
    QVariantMap properties;
};

class ConnmanBackend
{
public:
    virtual ~ConnmanBackend() {}

    virtual QList<ConnmanServiceData> services(QString *error) = 0;
    virtual QString getStringProperty(const QString &path, const QString &name,
                                      QString *error) = 0;
    virtual bool setProperty(const QString &path, const QString &name,
                             const QVariant &value, QString *error) = 0;
    virtual QString createService(const ConnmanSettings &settings, QString *error) = 0;
    virtual bool removeService(const QString &path, QString *error) = 0;
    virtual bool disconnectService(const QString &path, QString *error) = 0;
    virtual bool connectService(const QString &path, QString *error) = 0;
};

class RealConnmanBackend : public ConnmanBackend
{
public:
    QList<ConnmanServiceData> services(QString *error) override;
    QString getStringProperty(const QString &path, const QString &name,
                              QString *error) override;
    bool setProperty(const QString &path, const QString &name,
                     const QVariant &value, QString *error) override;
    QString createService(const ConnmanSettings &settings, QString *error) override;
    bool removeService(const QString &path, QString *error) override;
    bool disconnectService(const QString &path, QString *error) override;
    bool connectService(const QString &path, QString *error) override;
};

QDBusArgument &operator<<(QDBusArgument &argument, const ConnmanSetting &setting);
const QDBusArgument &operator>>(const QDBusArgument &argument, ConnmanSetting &setting);

}

#endif
