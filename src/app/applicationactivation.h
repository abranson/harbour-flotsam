/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FLOTSAM_APPLICATIONACTIVATION_H
#define FLOTSAM_APPLICATIONACTIVATION_H

#include <QObject>
#include <QStringList>
#include <QVariantMap>

namespace Flotsam {

class ApplicationActivation : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Application")

public:
    explicit ApplicationActivation(QObject *parent = nullptr);

public slots:
    void Activate(const QVariantMap &platformData);
    void Open(const QStringList &uris, const QVariantMap &platformData);

signals:
    void activateRequested();
    void openNetworkRequested(const QString &networkId);
};

}

#endif
