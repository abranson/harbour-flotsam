/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FLOTSAM_NOTIFICATIONTOKEN_H
#define FLOTSAM_NOTIFICATIONTOKEN_H

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace Flotsam {

struct NotificationTokenResult
{
    bool valid = false;
    QString networkId;
    QString error;
};

class NotificationToken
{
public:
    static NotificationTokenResult take(QJsonObject *tokens,
                                        const QString &token,
                                        const QString &action,
                                        const QStringList &pendingNetworkIds,
                                        const QDateTime &now);
};

}

#endif
