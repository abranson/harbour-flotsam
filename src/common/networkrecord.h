/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FLOTSAM_NETWORKRECORD_H
#define FLOTSAM_NETWORKRECORD_H

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace Flotsam {

class NetworkRecord
{
public:
    enum { CurrentSchemaVersion = 1 };

    int schemaVersion = CurrentSchemaVersion;
    QByteArray ssid;
    QString securityFamily;
    QString securityHint;
    QString passphrase;
    bool hidden = false;
    bool autoConnect = false;
    bool tombstone = false;
    QString revision;
    QString parentRevision;
    QString deviceUuid;
    QString deviceLabel;
    QDateTime updatedAt;

    QString networkId() const;
    QString displayName() const;
    bool hasTextSsid() const;
    bool isValid(QString *error = nullptr, bool requireRevision = true) const;
    QByteArray contentFingerprint() const;
    QString qrPayload(QString *error = nullptr) const;

    QJsonObject toJson() const;
    QByteArray toJsonData() const;
    QVariantMap toVariantMap(bool includeSecret = false) const;

    static NetworkRecord fromJson(const QJsonObject &object, QString *error = nullptr,
                                  bool requireRevision = true);
    static NetworkRecord fromJsonData(const QByteArray &data, QString *error = nullptr,
                                      bool requireRevision = true);
    static NetworkRecord fromConnman(const QVariantMap &properties,
                                     const QByteArray &rawSsid,
                                     const QString &passphrase,
                                     QString *error = nullptr);
    static NetworkRecord fromWifiQr(const QString &payload, QString *error = nullptr);

    static QString normalizeSecurityFamily(const QStringList &security);
    static QString normalizeSecurityHint(const QStringList &security);
    static bool validatePassphrase(const QString &family, const QString &passphrase,
                                   QString *error = nullptr);
    static QString deriveNetworkId(const QByteArray &rawSsid, const QString &family);
    static bool sameContent(const NetworkRecord &a, const NetworkRecord &b);
};

}

#endif
