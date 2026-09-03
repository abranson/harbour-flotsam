/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FLOTSAM_CONNMANUTIL_H
#define FLOTSAM_CONNMANUTIL_H

#include "networkrecord.h"

#include <QPair>
#include <QString>
#include <QVariantMap>

namespace Flotsam {

typedef QPair<QString, QString> ConnmanSetting;
typedef QList<ConnmanSetting> ConnmanSettings;

class ConnmanUtil
{
public:
    static bool validServicePath(const QString &path);
    static QByteArray ssidFromServicePath(const QString &path);
    static QString nativeSecurity(const NetworkRecord &record,
                                  const QVariantMap &existing = QVariantMap());
    static ConnmanSettings mergedSettings(const QVariantMap &existing,
                                           const QByteArray &ssid,
                                           const QString &existingPassphrase,
                                           const NetworkRecord &portable);
    static QByteArray fingerprint(const QVariantMap &properties,
                                  const QByteArray &ssid,
                                  const QString &passphrase,
                                  QString *error = nullptr);
    static bool compareFingerprint(const QByteArray &actual,
                                   const QByteArray &expected,
                                   QString *error = nullptr);
};

}

Q_DECLARE_METATYPE(Flotsam::ConnmanSetting)
Q_DECLARE_METATYPE(Flotsam::ConnmanSettings)

#endif
