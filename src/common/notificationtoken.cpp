/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "notificationtoken.h"

namespace Flotsam {

NotificationTokenResult NotificationToken::take(QJsonObject *tokens,
                                                 const QString &token,
                                                 const QString &action,
                                                 const QStringList &pendingNetworkIds,
                                                 const QDateTime &now)
{
    NotificationTokenResult result;
    if (!tokens || !tokens->contains(token)) {
        result.error = QStringLiteral("That notification action is stale");
        return result;
    }

    const QJsonObject entry = tokens->take(token).toObject();
    const QDateTime createdAt = QDateTime::fromString(
            entry.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
    const qint64 age = createdAt.secsTo(now);
    result.networkId = entry.value(QStringLiteral("networkId")).toString();
    if (!createdAt.isValid() || age < 0 || age > 24 * 60 * 60
            || !pendingNetworkIds.contains(result.networkId)) {
        result.error = QStringLiteral("That notification action is stale");
        return result;
    }
    if (entry.value(QStringLiteral("kind")).toString() != QLatin1String("new")
            || (action != QLatin1String("sync") && action != QLatin1String("keep"))) {
        result.error = QStringLiteral("That notification action is no longer valid");
        return result;
    }
    result.valid = true;
    return result;
}

}
