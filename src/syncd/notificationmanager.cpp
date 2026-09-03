/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "notificationmanager.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDataStream>
#include <QVariantMap>

namespace {

QString remoteCall(const QString &token, const QString &action)
{
    QString call = QStringLiteral("org.harbour.flotsam.Sync "
                                  "/org/harbour/flotsam/Sync "
                                  "org.harbour.flotsam.Sync NotificationAction");
    const QVariantList arguments = QVariantList() << token << action;
    for (const QVariant &argument : arguments) {
        QByteArray data;
        QDataStream stream(&data, QIODevice::WriteOnly);
        stream << argument;
        call += QLatin1Char(' ') + QString::fromLatin1(data.toBase64());
    }
    return call;
}

}

namespace Flotsam {

NotificationsInterfaceProxy::NotificationsInterfaceProxy(QObject *parent)
    : QDBusAbstractInterface(QStringLiteral("org.freedesktop.Notifications"),
                             QStringLiteral("/org/freedesktop/Notifications"),
                             "org.freedesktop.Notifications",
                             QDBusConnection::sessionBus(), parent)
{
}

NotificationManager::NotificationManager(QObject *parent)
    : QObject(parent)
    , m_interface(new NotificationsInterfaceProxy(this))
{
    connect(m_interface, &NotificationsInterfaceProxy::ActionInvoked,
            this, &NotificationManager::notificationAction);
}

void NotificationManager::showNewNetwork(const QString &deduplicationKey,
                                         const QString &token)
{
    show(deduplicationKey, QStringLiteral("Wi-Fi sync needs a choice"),
         QStringLiteral("Open Flotsam to review a newly saved network."),
         QStringList() << QStringLiteral("sync:%1").arg(token) << QStringLiteral("Sync")
                       << QStringLiteral("keep:%1").arg(token) << QStringLiteral("Keep only here"));
}

void NotificationManager::showAttention(const QString &deduplicationKey,
                                        const QString &kind)
{
    Q_UNUSED(kind)
    show(deduplicationKey, QStringLiteral("Wi-Fi sync needs attention"),
         QStringLiteral("Open Flotsam to review a synchronization decision."),
         QStringList());
}

void NotificationManager::show(const QString &deduplicationKey, const QString &summary,
                               const QString &body, const QStringList &actions)
{
    QDBusInterface notifications(QStringLiteral("org.freedesktop.Notifications"),
                                 QStringLiteral("/org/freedesktop/Notifications"),
                                 QStringLiteral("org.freedesktop.Notifications"),
                                 QDBusConnection::sessionBus());
    QVariantMap hints;
    hints.insert(QStringLiteral("x-nemo-preview-summary"), summary);
    hints.insert(QStringLiteral("x-nemo-preview-body"), body);
    hints.insert(QStringLiteral("desktop-entry"), QStringLiteral("harbour-flotsam"));
    for (int i = 0; i + 1 < actions.size(); i += 2) {
        const QString actionKey = actions.at(i);
        const int separator = actionKey.indexOf(QLatin1Char(':'));
        if (separator > 0) {
            hints.insert(QStringLiteral("x-nemo-remote-action-") + actionKey,
                         remoteCall(actionKey.mid(separator + 1),
                                    actionKey.left(separator)));
        }
    }
    QDBusPendingCall call = notifications.asyncCall(
            QStringLiteral("Notify"), QStringLiteral("Flotsam"),
            m_notificationIds.value(deduplicationKey, 0),
            QStringLiteral("harbour-flotsam"), summary, body, actions, hints, -1);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
    watcher->setProperty("deduplicationKey", deduplicationKey);
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &NotificationManager::notificationReplyFinished);
}

void NotificationManager::notificationReplyFinished()
{
    QDBusPendingCallWatcher *watcher = qobject_cast<QDBusPendingCallWatcher *>(sender());
    QDBusPendingReply<uint> reply = *watcher;
    if (!reply.isError()) {
        m_notificationIds.insert(watcher->property("deduplicationKey").toString(), reply.value());
    }
    watcher->deleteLater();
}

void NotificationManager::notificationAction(uint id, const QString &actionKey)
{
    Q_UNUSED(id)
    const int separator = actionKey.indexOf(QLatin1Char(':'));
    if (separator <= 0) {
        return;
    }
    emit actionInvoked(actionKey.mid(separator + 1), actionKey.left(separator));
}

}
