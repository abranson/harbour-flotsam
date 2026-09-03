/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "connmanutil.h"

#include <QHostAddress>
#include <QRegularExpression>

namespace {

void append(Flotsam::ConnmanSettings *settings, const QString &key, const QString &value,
            bool includeEmpty = false)
{
    if (includeEmpty || !value.isEmpty()) {
        settings->append(qMakePair(key, value));
    }
}

QString joined(const QVariant &value)
{
    const QStringList strings = value.toStringList();
    return strings.isEmpty() ? QString() : strings.join(QStringLiteral(";")) + QLatin1Char(';');
}

int prefixLength(const QString &netmask)
{
    QHostAddress address(netmask);
    if (address.protocol() != QAbstractSocket::IPv4Protocol) {
        return -1;
    }
    quint32 mask = address.toIPv4Address();
    int prefix = 0;
    bool zeroSeen = false;
    for (int bit = 31; bit >= 0; --bit) {
        const bool one = mask & (quint32(1) << bit);
        if (one && zeroSeen) {
            return -1;
        }
        if (one) {
            ++prefix;
        } else {
            zeroSeen = true;
        }
    }
    return prefix;
}

void appendIpConfiguration(Flotsam::ConnmanSettings *settings, const QString &prefix,
                           const QVariantMap &configuration)
{
    if (configuration.isEmpty()) {
        return;
    }
    append(settings, prefix + QStringLiteral("method"),
           configuration.value(QStringLiteral("Method")).toString().toLower());
    append(settings, prefix + QStringLiteral("local_address"),
           configuration.value(QStringLiteral("Address")).toString());
    append(settings, prefix + QStringLiteral("gateway"),
           configuration.value(QStringLiteral("Gateway")).toString());
    int prefixValue = configuration.value(QStringLiteral("PrefixLength"), -1).toInt();
    if (prefixValue < 0 && prefix == QLatin1String("IPv4.")) {
        prefixValue = prefixLength(configuration.value(QStringLiteral("Netmask")).toString());
    }
    if (prefixValue >= 0) {
        append(settings, prefix + QStringLiteral("netmask_prefixlen"), QString::number(prefixValue));
    }
    append(settings, prefix + QStringLiteral("privacy"),
           configuration.value(QStringLiteral("Privacy")).toString().toLower());
}

}

namespace Flotsam {

bool ConnmanUtil::validServicePath(const QString &path)
{
    static const QRegularExpression expression(
            QStringLiteral("^/net/connman/service/wifi_[0-9A-Fa-f]+_((?:[0-9A-Fa-f]{2}){1,32})_managed_[A-Za-z0-9_-]+$"));
    return path.size() <= 512 && expression.match(path).hasMatch();
}

QByteArray ConnmanUtil::ssidFromServicePath(const QString &path)
{
    static const QRegularExpression expression(
            QStringLiteral("^/net/connman/service/wifi_[0-9A-Fa-f]+_((?:[0-9A-Fa-f]{2}){1,32})_managed_[A-Za-z0-9_-]+$"));
    const QRegularExpressionMatch match = expression.match(path);
    if (!match.hasMatch()) {
        return QByteArray();
    }
    return QByteArray::fromHex(match.captured(1).toLatin1());
}

QString ConnmanUtil::nativeSecurity(const NetworkRecord &record, const QVariantMap &existing)
{
    if (record.securityFamily == QLatin1String("open")) {
        return QStringLiteral("none");
    }
    if (record.securityFamily == QLatin1String("wep")) {
        return QStringLiteral("wep");
    }
    QStringList existingSecurity = existing.value(QStringLiteral("Security")).toStringList();
    if (existingSecurity.isEmpty()) {
        existingSecurity.append(existing.value(QStringLiteral("Security")).toString());
    }
    if (NetworkRecord::normalizeSecurityFamily(existingSecurity) == QLatin1String("personal")) {
        return NetworkRecord::normalizeSecurityHint(existingSecurity);
    }
    const QString hint = record.securityHint.toLower();
    if (hint == QLatin1String("sae") || hint == QLatin1String("psksae")
            || hint == QLatin1String("rsn") || hint == QLatin1String("psk")
            || hint == QLatin1String("wpa")) {
        return hint;
    }
    return QStringLiteral("psk");
}

ConnmanSettings ConnmanUtil::mergedSettings(const QVariantMap &existing,
                                            const QByteArray &ssid,
                                            const QString &existingPassphrase,
                                            const NetworkRecord &portable)
{
    ConnmanSettings settings;
    append(&settings, QStringLiteral("Type"), QStringLiteral("wifi"));
    append(&settings, QStringLiteral("Name"), portable.displayName());
    append(&settings, QStringLiteral("SSID"), QString::fromLatin1(ssid.toHex()));
    append(&settings, QStringLiteral("Security"), nativeSecurity(portable, existing));
    append(&settings, QStringLiteral("Favorite"), QStringLiteral("true"));
    append(&settings, QStringLiteral("AutoConnect"), portable.autoConnect
           ? QStringLiteral("true") : QStringLiteral("false"));
    append(&settings, QStringLiteral("Hidden"), portable.hidden
           ? QStringLiteral("true") : QStringLiteral("false"));
    append(&settings, QStringLiteral("Passphrase"), portable.passphrase, true);

    append(&settings, QStringLiteral("Frequency"),
           existing.value(QStringLiteral("Frequency")).toString());
    appendIpConfiguration(&settings, QStringLiteral("IPv4."),
                          existing.value(QStringLiteral("IPv4.Configuration")).toMap());
    appendIpConfiguration(&settings, QStringLiteral("IPv6."),
                          existing.value(QStringLiteral("IPv6.Configuration")).toMap());
    append(&settings, QStringLiteral("Nameservers"),
           joined(existing.value(QStringLiteral("Nameservers.Configuration"))));
    append(&settings, QStringLiteral("Timeservers"),
           joined(existing.value(QStringLiteral("Timeservers.Configuration"))));
    append(&settings, QStringLiteral("Domains"),
           joined(existing.value(QStringLiteral("Domains.Configuration"))));

    const QVariantMap proxy = existing.value(QStringLiteral("Proxy.Configuration")).toMap();
    append(&settings, QStringLiteral("Proxy.Method"),
           proxy.value(QStringLiteral("Method")).toString().toLower());
    append(&settings, QStringLiteral("Proxy.URL"), proxy.value(QStringLiteral("URL")).toString());
    append(&settings, QStringLiteral("Proxy.Servers"), joined(proxy.value(QStringLiteral("Servers"))));
    append(&settings, QStringLiteral("Proxy.Excludes"), joined(proxy.value(QStringLiteral("Excludes"))));
    append(&settings, QStringLiteral("mDNS"), existing.value(QStringLiteral("mDNS")).toBool()
           ? QStringLiteral("true") : QString());

    Q_UNUSED(existingPassphrase)
    return settings;
}

QByteArray ConnmanUtil::fingerprint(const QVariantMap &properties, const QByteArray &ssid,
                                    const QString &passphrase, QString *error)
{
    const NetworkRecord record = NetworkRecord::fromConnman(properties, ssid, passphrase, error);
    if (record.securityFamily.isEmpty()) {
        return QByteArray();
    }
    return record.contentFingerprint();
}

bool ConnmanUtil::compareFingerprint(const QByteArray &actual, const QByteArray &expected,
                                     QString *error)
{
    if (actual == expected) {
        return true;
    }
    if (error) {
        *error = QStringLiteral("Conflict");
    }
    return false;
}

}
