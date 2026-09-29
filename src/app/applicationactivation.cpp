/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "applicationactivation.h"

#include <QRegularExpression>
#include <QUrl>

namespace Flotsam {

ApplicationActivation::ApplicationActivation(QObject *parent)
    : QObject(parent)
{
}

void ApplicationActivation::Activate(const QVariantMap &platformData)
{
    Q_UNUSED(platformData)
    emit activateRequested();
}

void ApplicationActivation::Open(const QStringList &uris,
                                 const QVariantMap &platformData)
{
    Q_UNUSED(platformData)
    emit activateRequested();
    if (uris.isEmpty()) {
        return;
    }

    const QUrl url(uris.first());
    const QString networkId = url.path().mid(1);
    static const QRegularExpression NetworkIdPattern(QStringLiteral("^[0-9a-f]{64}$"));
    if (url.scheme() == QLatin1String("flotsam")
            && url.host() == QLatin1String("network")
            && NetworkIdPattern.match(networkId).hasMatch()) {
        emit openNetworkRequested(networkId);
    }
}

}
