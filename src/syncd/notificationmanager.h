/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FLOTSAM_NOTIFICATIONMANAGER_H
#define FLOTSAM_NOTIFICATIONMANAGER_H

#include <QDBusAbstractInterface>
#include <QDBusConnection>
#include <QHash>
#include <QObject>
#include <QString>

namespace Flotsam {

class NotificationsInterfaceProxy : public QDBusAbstractInterface
{
    Q_OBJECT

public:
    explicit NotificationsInterfaceProxy(QObject *parent = nullptr);

signals:
    void ActionInvoked(uint id, const QString &actionKey);
};

class NotificationManager : public QObject
{
    Q_OBJECT

public:
    explicit NotificationManager(QObject *parent = nullptr);

    void showNewNetwork(const QString &deduplicationKey, const QString &token,
                        const QString &displayName, bool readded);
    void showAttention(const QString &deduplicationKey, const QString &kind);

signals:
    void actionInvoked(const QString &token, const QString &action);

private slots:
    void notificationAction(uint id, const QString &actionKey);
    void notificationReplyFinished();

private:
    void show(const QString &deduplicationKey, const QString &summary,
              const QString &body, const QStringList &actions,
              const QString &previewSummary, const QString &previewBody);

    NotificationsInterfaceProxy *m_interface;
    QHash<QString, uint> m_notificationIds;
};

}

#endif
