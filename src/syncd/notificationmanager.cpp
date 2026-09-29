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
#include <QRegularExpression>
#include <QVariantMap>

namespace {

QString encodedArgument(const QVariant &argument)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream << argument;
    return QString::fromLatin1(data.toBase64());
}

QString openNetworkCall(const QString &deduplicationKey)
{
    const int separator = deduplicationKey.indexOf(QLatin1Char(':'));
    const QString networkId = separator >= 0
            ? deduplicationKey.mid(separator + 1) : QString();
    const QStringList uris = QStringList()
            << QStringLiteral("flotsam://network/%1").arg(networkId);
    return QStringLiteral("uk.co.nationalfantastic.harbour-flotsam "
                          "/uk/co/nationalfantastic/harbour_flotsam "
                          "org.freedesktop.Application Open %1 %2")
            .arg(encodedArgument(QVariant::fromValue(uris)),
                 encodedArgument(QVariant::fromValue(QVariantMap())));
}

}

namespace Flotsam {

NotificationManager::NotificationManager(QObject *parent)
    : QObject(parent)
{
}

void NotificationManager::showNewNetwork(const QString &deduplicationKey,
                                         const QString &displayName, bool readded)
{
    QString safeName = displayName;
    safeName.replace(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f-\\x9f]")),
                     QStringLiteral(" "));
    show(deduplicationKey, readded ? QStringLiteral("Wi-Fi network added again")
                                 : QStringLiteral("New Wi-Fi network found"),
         (readded ? QStringLiteral("Would you like to sync '%1' again?")
                  : QStringLiteral("Would you like to sync '%1'?")).arg(safeName),
         QStringLiteral("New Wi-Fi network found"),
         QStringLiteral("Open Flotsam to choose whether to synchronize it."));
}

void NotificationManager::showAttention(const QString &deduplicationKey,
                                        const QString &kind)
{
    Q_UNUSED(kind)
    show(deduplicationKey, QStringLiteral("Wi-Fi sync needs attention"),
         QStringLiteral("Open Flotsam to review a synchronization decision."),
         QStringLiteral("Wi-Fi sync needs attention"),
         QStringLiteral("Open Flotsam to review a synchronization decision."));
}

void NotificationManager::show(const QString &deduplicationKey, const QString &summary,
                               const QString &body,
                               const QString &previewSummary, const QString &previewBody)
{
    QDBusInterface notifications(QStringLiteral("org.freedesktop.Notifications"),
                                 QStringLiteral("/org/freedesktop/Notifications"),
                                 QStringLiteral("org.freedesktop.Notifications"),
                                 QDBusConnection::sessionBus());
    QVariantMap hints;
    hints.insert(QStringLiteral("x-nemo-preview-summary"), previewSummary);
    hints.insert(QStringLiteral("x-nemo-preview-body"), previewBody);
    hints.insert(QStringLiteral("desktop-entry"), QStringLiteral("harbour-flotsam"));
    hints.insert(QStringLiteral("x-nemo-remote-action-default"),
                 openNetworkCall(deduplicationKey));
    QStringList notificationActions = QStringList()
            << QStringLiteral("default") << QString();
    QDBusPendingCall call = notifications.asyncCall(
            QStringLiteral("Notify"), QStringLiteral("Flotsam"),
            m_notificationIds.value(deduplicationKey, 0),
            QStringLiteral("harbour-flotsam"), summary, body,
            notificationActions, hints, -1);
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

}
