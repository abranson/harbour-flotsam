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

class NotificationManager : public QObject
{
    Q_OBJECT

public:
    explicit NotificationManager(QObject *parent = nullptr);

    void showNewNetwork(const QString &deduplicationKey,
                        const QString &displayName, bool readded);
    void showAttention(const QString &deduplicationKey, const QString &kind);

private slots:
    void notificationReplyFinished();

private:
    void show(const QString &deduplicationKey, const QString &summary,
              const QString &body,
              const QString &previewSummary, const QString &previewBody);

    QHash<QString, uint> m_notificationIds;
};

}

#endif
