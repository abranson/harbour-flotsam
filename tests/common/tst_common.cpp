/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "atomicstate.h"
#include "applicationactivation.h"
#include "appcontroller.h"
#include "connmanhelper.h"
#include "connmanutil.h"
#include "networkrecord.h"
#include "notificationtoken.h"
#include "reconciler.h"
#include "privatebus.h"
#include "security.h"
#include "uibus.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QProcess>
#include <QSignalSpy>
#include <unistd.h>
#include <grp.h>
#include <sys/stat.h>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
#include <QUuid>

using namespace Flotsam;

namespace {

NetworkRecord record(const QString &password = QStringLiteral("password1"),
                     const QString &security = QStringLiteral("personal"))
{
    NetworkRecord value;
    value.ssid = QByteArrayLiteral("Cafe;One");
    value.securityFamily = security;
    value.securityHint = security == QLatin1String("personal")
            ? QStringLiteral("psk") : security == QLatin1String("wep")
              ? QStringLiteral("wep") : QStringLiteral("none");
    value.passphrase = security == QLatin1String("open") ? QString() : password;
    value.autoConnect = true;
    value.revision = QUuid::createUuid().toString();
    value.deviceUuid = QUuid::createUuid().toString();
    value.deviceLabel = QStringLiteral("Test device");
    value.updatedAt = QDateTime::currentDateTimeUtc();
    return value;
}

QVariantMap properties(const QString &security = QStringLiteral("psk"))
{
    QVariantMap result;
    result.insert(QStringLiteral("Type"), QStringLiteral("wifi"));
    result.insert(QStringLiteral("Favorite"), true);
    result.insert(QStringLiteral("Security"), QStringList() << security);
    result.insert(QStringLiteral("AutoConnect"), true);
    result.insert(QStringLiteral("Hidden"), false);
    result.insert(QStringLiteral("State"), QStringLiteral("idle"));
    return result;
}

class FakeBackend : public ConnmanBackend
{
public:
    QList<ConnmanServiceData> values;
    QHash<QString, QString> passwords;
    QString createdPath = QStringLiteral("/net/connman/service/wifi_aabbccddeeff_436166653b4f6e65_managed_psk");
    bool failSet = false;
    int createCalls = 0;
    int listCalls = 0;
    QStringList removed;

    QList<ConnmanServiceData> services(QString *) override { ++listCalls; return values; }
    QString getStringProperty(const QString &path, const QString &, QString *) override {
        return passwords.value(path);
    }
    bool setProperty(const QString &, const QString &, const QVariant &, QString *error) override {
        if (failSet) {
            failSet = false;
            if (error) *error = QStringLiteral("injected set failure");
            return false;
        }
        return true;
    }
    QString createService(const ConnmanSettings &, QString *) override {
        ++createCalls;
        return createdPath;
    }
    bool removeService(const QString &path, QString *) override {
        removed.append(path);
        return true;
    }
    bool disconnectService(const QString &, QString *) override { return true; }
    bool connectService(const QString &, QString *) override { return true; }
};

}

class CommonTest : public QObject
{
    Q_OBJECT

private slots:
    void privilegedUiPackaging();
    void systemBusUiGate();
    void processIdentityChecks();
    void securityNormalization();
    void macIndependentIdentity();
    void passwordValidation_data();
    void passwordValidation();
    void qrEscaping();
    void wifiQrParsing();
    void wifiQrRejectsInvalidData_data();
    void wifiQrRejectsInvalidData();
    void fingerprintStability();
    void strictJsonValidation();
    void tombstoneRules();
    void preservesNativeSettings();
    void pathValidation();
    void reconcile_data();
    void reconcile();
    void readditionLifecycle();
    void completedDeletionVisibility();
    void atomicPrivateState();
    void unsafeStateRejected();
    void legacyStateMigration();
    void helperRejectsMessageBus();
    void privateBusAllowsOnlyExplicitApi();
    void privateBusRejectsUnsafeSocket();
    void uiProxyUsesPrivatePeer();
    void crossUidBoundary();
    void helperCompareAndSwap();
    void helperRemovesMultipleAdapters();
    void helperRollsBack();
    void helperRemovesFailedNewService();
    void notificationTokensRejectStaleActions();
    void applicationActivationRoutesNetworks();
};

void CommonTest::privilegedUiPackaging()
{
    QFile desktop(QFINDTESTDATA("../../data/harbour-flotsam.desktop"));
    QVERIFY(desktop.open(QIODevice::ReadOnly));
    const QByteArray entry = desktop.readAll();
    QVERIFY(entry.contains("\nPermissions=Privileged;Flotsam;Camera\n"));
    QVERIFY(!entry.contains("Sandboxing=Disabled"));
    QVERIFY(entry.contains("\nExec=/usr/bin/harbour-flotsam\n"));

    QFile permission(QFINDTESTDATA("../../data/Flotsam.permission"));
    QVERIFY(permission.open(QIODevice::ReadOnly));
    const QByteArray rules = permission.readAll();
    QVERIFY(rules.contains("dbus-system.call org.harbour.flotsam.Sync="));
    QVERIFY(rules.contains("org.harbour.flotsam.Identity1.VerifyDaemon@/org/harbour/flotsam/Connman"));
    QVERIFY(!rules.contains("whitelist /run/"));
    QVERIFY(rules.contains("\nblacklist /run/harbour-flotsam-helper\n"));
    QVERIFY(rules.contains("\nblacklist /var/lib/harbour-flotsam\n"));
    QVERIFY(!rules.contains("privileged-data"));
    QVERIFY(!rules.contains("Accounts.permission"));

    QFile busPolicy(QFINDTESTDATA("../../data/dbus-1/system.d/org.harbour.flotsam.Sync.conf"));
    QVERIFY(busPolicy.open(QIODevice::ReadOnly));
    QVERIFY(!busPolicy.readAll().contains("send_type=\"signal\""));
}

void CommonTest::securityNormalization()
{
    QCOMPARE(NetworkRecord::normalizeSecurityFamily(QStringList() << "psk"), QString("personal"));
    QCOMPARE(NetworkRecord::normalizeSecurityFamily(QStringList() << "psk_sae" << "sae"),
             QString("personal"));
    QCOMPARE(NetworkRecord::normalizeSecurityFamily(QStringList() << "psksae"),
             QString("personal"));
    QCOMPARE(NetworkRecord::normalizeSecurityHint(QStringList() << "psk_sae"),
             QString("psksae"));
    QCOMPARE(NetworkRecord::normalizeSecurityFamily(QStringList() << "sae"), QString("personal"));
    QCOMPARE(NetworkRecord::normalizeSecurityFamily(QStringList() << "wep"), QString("wep"));
    QCOMPARE(NetworkRecord::normalizeSecurityFamily(QStringList() << "none"), QString("open"));
    QVERIFY(NetworkRecord::normalizeSecurityFamily(QStringList() << "ieee8021x").isEmpty());
}

void CommonTest::macIndependentIdentity()
{
    const QByteArray ssid = QByteArray::fromHex("43616665");
    QCOMPARE(NetworkRecord::deriveNetworkId(ssid, QStringLiteral("personal")),
             NetworkRecord::deriveNetworkId(ssid, QStringLiteral("personal")));
    QCOMPARE(ConnmanUtil::ssidFromServicePath(
                     QStringLiteral("/net/connman/service/wifi_aabbccddeeff_43616665_managed_psk")), ssid);
    QCOMPARE(ConnmanUtil::ssidFromServicePath(
                     QStringLiteral("/net/connman/service/wifi_112233445566_43616665_managed_sae")), ssid);
    QCOMPARE(NetworkRecord::deriveNetworkId(ssid, QStringLiteral("personal")),
             NetworkRecord::deriveNetworkId(ssid,
                                             NetworkRecord::normalizeSecurityFamily(QStringList() << "sae")));
}

void CommonTest::applicationActivationRoutesNetworks()
{
    ApplicationActivation activation;
    int activateCount = 0;
    QString openedNetworkId;
    connect(&activation, &ApplicationActivation::activateRequested,
            [&activateCount]() { ++activateCount; });
    connect(&activation, &ApplicationActivation::openNetworkRequested,
            [&openedNetworkId](const QString &networkId) {
        openedNetworkId = networkId;
    });

    activation.Activate(QVariantMap());
    QCOMPARE(activateCount, 1);
    QVERIFY(openedNetworkId.isEmpty());

    const QString networkId(64, QLatin1Char('a'));
    activation.Open(QStringList() << QStringLiteral("flotsam://network/") + networkId,
                    QVariantMap());
    QCOMPARE(activateCount, 2);
    QCOMPARE(openedNetworkId, networkId);

    openedNetworkId.clear();
    activation.Open(QStringList() << QStringLiteral("https://example.com/not-a-network"),
                    QVariantMap());
    QCOMPARE(activateCount, 3);
    QVERIFY(openedNetworkId.isEmpty());
}

void CommonTest::passwordValidation_data()
{
    QTest::addColumn<QString>("family");
    QTest::addColumn<QString>("password");
    QTest::addColumn<bool>("valid");
    QTest::newRow("open") << "open" << "" << true;
    QTest::newRow("open-secret") << "open" << "password" << false;
    QTest::newRow("wep-ascii-5") << "wep" << "abcde" << true;
    QTest::newRow("wep-ascii-13") << "wep" << "abcdefghijklm" << true;
    QTest::newRow("wep-hex-10") << "wep" << "0123456789" << true;
    QTest::newRow("wep-bad") << "wep" << "short!" << false;
    QTest::newRow("wpa-min") << "personal" << "12345678" << true;
    QTest::newRow("wpa-short") << "personal" << "1234567" << false;
    QTest::newRow("wpa-hex") << "personal" << QString(64, QLatin1Char('a')) << true;
    QTest::newRow("wpa-nonhex64") << "personal" << QString(63, QLatin1Char('a')) + "z" << false;
}

void CommonTest::passwordValidation()
{
    QFETCH(QString, family);
    QFETCH(QString, password);
    QFETCH(bool, valid);
    QCOMPARE(NetworkRecord::validatePassphrase(family, password), valid);
}

void CommonTest::qrEscaping()
{
    NetworkRecord value = record(QStringLiteral("pa\\ss:word"));
    const QString payload = value.qrPayload();
    QCOMPARE(payload, QStringLiteral("WIFI:T:WPA;S:Cafe\\;One;P:pa\\\\ss\\:word;;"));
    QString error;
    const NetworkRecord parsed = NetworkRecord::fromWifiQr(payload, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(parsed.ssid, value.ssid);
    QCOMPARE(parsed.securityFamily, value.securityFamily);
    QCOMPARE(parsed.passphrase, value.passphrase);
    value.ssid = QByteArray::fromHex("ff00");
    QVERIFY(value.qrPayload().isEmpty());
}

void CommonTest::wifiQrParsing()
{
    QString error;
    NetworkRecord parsed = NetworkRecord::fromWifiQr(
            QStringLiteral("WIFI:T:nopass;S:Guest\\; Wi-Fi;H:false;;"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(parsed.displayName(), QStringLiteral("Guest; Wi-Fi"));
    QCOMPARE(parsed.securityFamily, QStringLiteral("open"));
    QCOMPARE(parsed.securityHint, QStringLiteral("none"));
    QVERIFY(parsed.autoConnect);
    QVERIFY(!parsed.hidden);

    parsed = NetworkRecord::fromWifiQr(
            QStringLiteral("WIFI:T:WPA3;S:Private;P:password1;H:1;;"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(parsed.securityFamily, QStringLiteral("personal"));
    QCOMPARE(parsed.securityHint, QStringLiteral("sae"));
    QVERIFY(parsed.hidden);
}

void CommonTest::wifiQrRejectsInvalidData_data()
{
    QTest::addColumn<QString>("payload");
    QTest::newRow("not-wifi") << QStringLiteral("https://example.com");
    QTest::newRow("missing-ssid") << QStringLiteral("WIFI:T:WPA;P:password1;;");
    QTest::newRow("enterprise")
            << QStringLiteral("WIFI:T:WPA2-EAP;S:Office;P:password1;;");
    QTest::newRow("short-password")
            << QStringLiteral("WIFI:T:WPA;S:Private;P:short;;");
    QTest::newRow("open-password")
            << QStringLiteral("WIFI:T:nopass;S:Guest;P:not-empty;;");
    QTest::newRow("duplicate-ssid")
            << QStringLiteral("WIFI:T:nopass;S:One;S:Two;;");
    QTest::newRow("bad-hidden")
            << QStringLiteral("WIFI:T:nopass;S:Guest;H:perhaps;;");
    QTest::newRow("trailing-escape")
            << QStringLiteral("WIFI:T:nopass;S:Guest\\");
}

void CommonTest::wifiQrRejectsInvalidData()
{
    QFETCH(QString, payload);
    QString error;
    const NetworkRecord parsed = NetworkRecord::fromWifiQr(payload, &error);
    QVERIFY(!error.isEmpty());
    QVERIFY(parsed.securityFamily.isEmpty());
}

void CommonTest::fingerprintStability()
{
    NetworkRecord first = record();
    const QVariantMap concealed = first.toVariantMap(false);
    const QVariantMap revealed = first.toVariantMap(true);
    QVERIFY(concealed.value(QStringLiteral("hasPassphrase")).toBool());
    QVERIFY(!concealed.contains(QStringLiteral("passphrase")));
    QVERIFY(revealed.value(QStringLiteral("hasPassphrase")).toBool());
    QCOMPARE(revealed.value(QStringLiteral("passphrase")).toString(), first.passphrase);
    NetworkRecord second = first;
    second.revision = QUuid::createUuid().toString();
    second.parentRevision = first.revision;
    second.deviceUuid = QUuid::createUuid().toString();
    second.deviceLabel = QStringLiteral("Other device");
    second.updatedAt = first.updatedAt.addYears(10);
    QCOMPARE(first.contentFingerprint(), second.contentFingerprint());
    second.hidden = !first.hidden;
    QVERIFY(first.contentFingerprint() != second.contentFingerprint());
}

void CommonTest::strictJsonValidation()
{
    NetworkRecord value = record();
    QJsonObject object = value.toJson();
    QString error;
    QVERIFY(NetworkRecord::fromJson(object, &error).isValid());

    object.insert(QStringLiteral("ssidBase64"), QStringLiteral("%%%"));
    error.clear();
    NetworkRecord::fromJson(object, &error);
    QVERIFY(!error.isEmpty());

    object = value.toJson();
    object.remove(QStringLiteral("hidden"));
    error.clear();
    NetworkRecord::fromJson(object, &error);
    QVERIFY(!error.isEmpty());

    object = value.toJson();
    object.insert(QStringLiteral("contentFingerprint"), QString(64, QLatin1Char('0')));
    error.clear();
    NetworkRecord::fromJson(object, &error);
    QVERIFY(!error.isEmpty());

    object = value.toJson();
    object.insert(QStringLiteral("schemaVersion"), NetworkRecord::CurrentSchemaVersion + 1);
    error.clear();
    NetworkRecord::fromJson(object, &error);
    QVERIFY(error.contains(QStringLiteral("newer")));
}

void CommonTest::tombstoneRules()
{
    NetworkRecord value = record();
    value.tombstone = true;
    QVERIFY(!value.isValid());
    value.passphrase.clear();
    QVERIFY(value.isValid());
    QVERIFY(value.qrPayload().isEmpty());
}

void CommonTest::preservesNativeSettings()
{
    QVariantMap native = properties();
    QVariantMap ipv4;
    ipv4.insert(QStringLiteral("Method"), QStringLiteral("manual"));
    ipv4.insert(QStringLiteral("Address"), QStringLiteral("192.0.2.4"));
    ipv4.insert(QStringLiteral("Netmask"), QStringLiteral("255.255.255.0"));
    ipv4.insert(QStringLiteral("Gateway"), QStringLiteral("192.0.2.1"));
    native.insert(QStringLiteral("IPv4.Configuration"), ipv4);
    native.insert(QStringLiteral("Nameservers.Configuration"), QStringList() << "192.0.2.53");
    QVariantMap proxy;
    proxy.insert(QStringLiteral("Method"), QStringLiteral("manual"));
    proxy.insert(QStringLiteral("Servers"), QStringList() << "proxy.example:8080");
    native.insert(QStringLiteral("Proxy.Configuration"), proxy);
    native.insert(QStringLiteral("Frequency"), 5180);

    const ConnmanSettings settings = ConnmanUtil::mergedSettings(
            native, QByteArrayLiteral("Cafe;One"), QStringLiteral("old-password"), record());
    QMap<QString, QString> map;
    for (const ConnmanSetting &setting : settings) map.insert(setting.first, setting.second);
    QCOMPARE(map.value(QStringLiteral("IPv4.method")), QStringLiteral("manual"));
    QCOMPARE(map.value(QStringLiteral("IPv4.local_address")), QStringLiteral("192.0.2.4"));
    QCOMPARE(map.value(QStringLiteral("IPv4.netmask_prefixlen")), QStringLiteral("24"));
    QCOMPARE(map.value(QStringLiteral("Nameservers")), QStringLiteral("192.0.2.53;"));
    QCOMPARE(map.value(QStringLiteral("Proxy.Servers")), QStringLiteral("proxy.example:8080;"));
    QCOMPARE(map.value(QStringLiteral("Frequency")), QStringLiteral("5180"));
    QCOMPARE(map.value(QStringLiteral("Passphrase")), QStringLiteral("password1"));
}

void CommonTest::pathValidation()
{
    QVERIFY(ConnmanUtil::validServicePath(
            QStringLiteral("/net/connman/service/wifi_aabbccddeeff_43616665_managed_psk")));
    QVERIFY(!ConnmanUtil::validServicePath(QStringLiteral("/net/connman/service/../../etc/shadow")));
    QVERIFY(!ConnmanUtil::validServicePath(QStringLiteral("/net/connman/service/ethernet_x")));
}

void CommonTest::reconcile_data()
{
    QTest::addColumn<QString>("scenario");
    QTest::addColumn<int>("expected");
    QTest::newRow("matching-first") << "matching-first" << int(Reconciler::AdoptMatching);
    QTest::newRow("first-difference") << "first-difference" << int(Reconciler::Conflict);
    QTest::newRow("local-only") << "local-only" << int(Reconciler::NewLocalDecision);
    QTest::newRow("approved-local") << "approved-local" << int(Reconciler::UploadLocal);
    QTest::newRow("remote-only") << "remote-only" << int(Reconciler::ApplyRemote);
    QTest::newRow("local-edit") << "local-edit" << int(Reconciler::UploadLocal);
    QTest::newRow("remote-edit") << "remote-edit" << int(Reconciler::ApplyRemote);
    QTest::newRow("simultaneous") << "simultaneous" << int(Reconciler::Conflict);
    QTest::newRow("forgotten") << "forgotten" << int(Reconciler::ForgottenDecision);
    QTest::newRow("remote-missing") << "remote-missing" << int(Reconciler::RemoteMissing);
    QTest::newRow("blocked") << "blocked" << int(Reconciler::Blocked);
    QTest::newRow("tombstone") << "tombstone" << int(Reconciler::RemoveLocal);
    QTest::newRow("re-added") << "re-added" << int(Reconciler::NewLocalDecision);
    QTest::newRow("re-added-approved") << "re-added-approved" << int(Reconciler::UploadLocal);
    QTest::newRow("re-added-blocked") << "re-added-blocked" << int(Reconciler::Blocked);
    QTest::newRow("newer-deletion") << "newer-deletion" << int(Reconciler::RemoveLocal);
    QTest::newRow("deleted-absent") << "deleted-absent" << int(Reconciler::AdoptMatching);
    QTest::newRow("remote-re-added") << "remote-re-added" << int(Reconciler::ApplyRemote);
    QTest::newRow("remote-re-added-blocked") << "remote-re-added-blocked" << int(Reconciler::Blocked);
}

void CommonTest::reconcile()
{
    QFETCH(QString, scenario);
    QFETCH(int, expected);
    NetworkRecord base = record();
    NetworkRecord local = base;
    NetworkRecord remote = base;
    Reconciler::Input input;
    if (scenario == QLatin1String("matching-first")) {
        input.local = &local; input.remote = &remote;
    } else if (scenario == QLatin1String("first-difference")) {
        remote.hidden = true; input.local = &local; input.remote = &remote;
    } else if (scenario == QLatin1String("local-only") || scenario == QLatin1String("approved-local")) {
        input.local = &local;
        input.approvedLocal = scenario == QLatin1String("approved-local");
    } else if (scenario == QLatin1String("remote-only")) {
        input.remote = &remote;
    } else if (scenario == QLatin1String("local-edit")) {
        local.hidden = true; input.base = &base; input.local = &local; input.remote = &remote;
    } else if (scenario == QLatin1String("remote-edit")) {
        remote.hidden = true; input.base = &base; input.local = &local; input.remote = &remote;
    } else if (scenario == QLatin1String("simultaneous")) {
        local.hidden = true; remote.autoConnect = false;
        input.base = &base; input.local = &local; input.remote = &remote;
    } else if (scenario == QLatin1String("forgotten")) {
        input.base = &base; input.remote = &remote;
    } else if (scenario == QLatin1String("remote-missing")) {
        input.base = &base; input.local = &local;
    } else if (scenario == QLatin1String("blocked")) {
        input.base = &base; input.local = &local; input.remote = &remote; input.locallyBlocked = true;
    } else if (scenario == QLatin1String("tombstone")) {
        remote.tombstone = true; remote.passphrase.clear();
        input.base = &base; input.local = &local; input.remote = &remote; input.locallyBlocked = true;
    } else if (scenario.startsWith(QLatin1String("re-added"))
               || scenario == QLatin1String("newer-deletion")) {
        base.tombstone = true; base.passphrase.clear();
        remote = base;
        input.base = &base; input.local = &local; input.remote = &remote;
        input.approvedLocal = scenario == QLatin1String("re-added-approved");
        input.locallyBlocked = scenario == QLatin1String("re-added-blocked");
        if (scenario == QLatin1String("newer-deletion")) {
            remote.revision = QUuid::createUuid().toString();
            input.approvedLocal = true;
            input.locallyBlocked = true;
        }
    } else if (scenario == QLatin1String("deleted-absent")) {
        base.tombstone = true; base.passphrase.clear(); remote = base;
        input.base = &base; input.remote = &remote;
    } else if (scenario.startsWith(QLatin1String("remote-re-added"))) {
        base.tombstone = true; base.passphrase.clear();
        remote.parentRevision = base.revision;
        remote.revision = QUuid::createUuid().toString();
        input.base = &base; input.remote = &remote;
        input.locallyBlocked = scenario.endsWith(QLatin1String("blocked"));
    }
    QCOMPARE(int(Reconciler::reconcile(input).action), expected);
}

void CommonTest::readditionLifecycle()
{
    NetworkRecord base = record();
    NetworkRecord local = base;
    NetworkRecord remote = base;
    remote.tombstone = true;
    remote.passphrase.clear();
    remote.parentRevision = base.revision;
    remote.revision = QUuid::createUuid().toString();
    Reconciler::Input input;
    input.base = &base; input.local = &local; input.remote = &remote;
    // An offline device's old profile must not resurrect a deletion.
    QCOMPARE(Reconciler::reconcile(input).action, Reconciler::RemoveLocal);
    base = remote;
    input.local = nullptr;
    QCOMPARE(Reconciler::reconcile(input).action, Reconciler::AdoptMatching);
    input.local = &local;
    QCOMPARE(Reconciler::reconcile(input).action, Reconciler::NewLocalDecision);
    input.locallyBlocked = true;
    QCOMPARE(Reconciler::reconcile(input).action, Reconciler::Blocked);
    input.locallyBlocked = false;
    input.approvedLocal = true;
    QCOMPARE(Reconciler::reconcile(input).action, Reconciler::UploadLocal);
    // Another device that applied the deletion restores the approved revival.
    remote = local;
    remote.parentRevision = base.revision;
    remote.revision = QUuid::createUuid().toString();
    input.local = nullptr;
    input.approvedLocal = false;
    QCOMPARE(Reconciler::reconcile(input).action, Reconciler::ApplyRemote);
}

void CommonTest::completedDeletionVisibility()
{
    NetworkRecord value = record();
    QVERIFY(!Reconciler::isCompletedDeletion(value, false, false));
    value.tombstone = true;
    value.passphrase.clear();
    QVERIFY(Reconciler::isCompletedDeletion(value, false, false));
    QVERIFY(!Reconciler::isCompletedDeletion(value, true, false));
    QVERIFY(!Reconciler::isCompletedDeletion(value, false, true));
    QVERIFY(!Reconciler::isCompletedDeletion(value, true, true));
}

void CommonTest::atomicPrivateState()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AtomicState state(directory.path() + QStringLiteral("/private/state.json"));
    QVERIFY(state.save());
    const QFile::Permissions permissions = QFileInfo(state.path()).permissions();
    QVERIFY(permissions & QFileDevice::ReadOwner);
    QVERIFY(permissions & QFileDevice::WriteOwner);
    QVERIFY(!(permissions & QFileDevice::ReadGroup));
    QVERIFY(!(permissions & QFileDevice::ReadOther));
    AtomicState loaded(state.path());
    QVERIFY(loaded.load());
    QCOMPARE(loaded.object().value(QStringLiteral("schemaVersion")).toInt(), 1);
    QVERIFY(!loaded.object().contains(QStringLiteral("warningAcknowledged")));
}

void CommonTest::unsafeStateRejected()
{
    QTemporaryDir directory;
    const QString path = directory.path() + QStringLiteral("/state.json");
    const QString alias = directory.path() + QStringLiteral("/alias.json");
    AtomicState state(path);
    QVERIFY(state.save());
    QVERIFY(QFile::link(path, alias));
    AtomicState linked(alias);
    QVERIFY(!linked.load());
    QVERIFY(!linked.save());
    QVERIFY(QFile::remove(alias));
    QVERIFY(::link(QFile::encodeName(path).constData(), QFile::encodeName(alias).constData()) == 0);
    QVERIFY(!state.load());
    QVERIFY(!state.save());
    QVERIFY(QFile::remove(alias));
    QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadOther));
    QVERIFY(!state.load());
    QVERIFY(!state.save());
    QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    QJsonObject oversized = AtomicState::initialObject();
    oversized.insert(QStringLiteral("padding"), QString(1024 * 1024, QLatin1Char('x')));
    state.setObject(oversized);
    QVERIFY(!state.save());
    AtomicState original(path);
    QVERIFY(original.load());
    QVERIFY(!original.object().contains(QStringLiteral("padding")));
}

void CommonTest::legacyStateMigration()
{
    QTemporaryDir directory;
    const QString legacy = directory.path() + QStringLiteral("/old/state.json");
    const QString current = directory.path() + QStringLiteral("/new/state.json");
    AtomicState old(legacy);
    QJsonObject object = old.object();
    object.insert(QStringLiteral("deviceLabel"), QStringLiteral("preserved"));
    object.insert(QStringLiteral("pendingTombstones"), QJsonObject{{QStringLiteral("fixture"), true}});
    object.insert(QStringLiteral("notificationTokens"), QJsonObject{{QStringLiteral("old"), true}});
    old.setObject(object);
    QVERIFY(old.save());
    AtomicState migration(current, legacy);
    QString error;
    QVERIFY2(migration.load(&error), qPrintable(error));
    QVERIFY(!QFile::exists(legacy));
    QVERIFY(migration.object().value(QStringLiteral("migrationNeedsReview")).toBool());
    QCOMPARE(migration.object().value(QStringLiteral("deviceLabel")).toString(), QStringLiteral("preserved"));
    QVERIFY(!migration.object().value(QStringLiteral("pendingTombstones")).toObject().isEmpty());
    QVERIFY(migration.object().value(QStringLiteral("notificationTokens")).toObject().isEmpty());
    AtomicState reload(current, legacy);
    QVERIFY(reload.load());
    QVERIFY(reload.object().value(QStringLiteral("migrationNeedsReview")).toBool());
    QVERIFY(old.save());
    QVERIFY(!reload.load(&error)); // Never overwrite either copy after an interrupted cleanup.
    QVERIFY(QFile::exists(legacy));
    AtomicState protectedCopy(current);
    QVERIFY(protectedCopy.load());
    QCOMPARE(protectedCopy.object(), migration.object());
}

void CommonTest::helperRejectsMessageBus()
{
    // An isolated, real bus and separate caller process, never the user's bus.
    QProcess bus;
    bus.start(QStringLiteral("dbus-daemon"), QStringList()
              << QStringLiteral("--session") << QStringLiteral("--nofork")
              << QStringLiteral("--print-address=1"));
    QVERIFY(bus.waitForStarted());
    QVERIFY(bus.waitForReadyRead());
    const QString address = QString::fromUtf8(bus.readLine()).trimmed();
    QDBusConnection connection = QDBusConnection::connectToBus(address, QStringLiteral("test-bus"));
    QVERIFY(connection.isConnected());
    FakeBackend backend;
    ConnmanHelper helper(&backend);
    QVERIFY(connection.registerObject(QStringLiteral("/helper"), &helper,
                                     QDBusConnection::ExportScriptableSlots));
    QVERIFY(connection.registerService(QStringLiteral("org.harbour.flotsam.Test")));
    for (const QString &method : {QStringLiteral("List"), QStringLiteral("Export"),
                                 QStringLiteral("CompareAndApply"), QStringLiteral("CompareAndRemove")}) {
        QProcess caller;
        caller.start(QCoreApplication::applicationFilePath(), QStringList()
                     << QStringLiteral("--probe") << address << QStringLiteral("bus")
                     << method << QStringLiteral("org.harbour.flotsam.Error.AccessDenied"));
        QVERIFY(caller.waitForStarted());
        QTRY_COMPARE(caller.state(), QProcess::NotRunning);
        QCOMPARE(caller.exitCode(), 0);
    }
    QCOMPARE(backend.listCalls, 0);
    QCOMPARE(backend.createCalls, 0);
    QVERIFY(backend.removed.isEmpty());
    QDBusConnection::disconnectFromBus(connection.name());
    bus.terminate();
    QVERIFY(bus.waitForFinished());
}

void CommonTest::privateBusAllowsOnlyExplicitApi()
{
    QTemporaryDir directory;
    const QString socket = directory.path() + QStringLiteral("/peer");
    FakeBackend backend;
    ConnmanHelper helper(&backend);
    PrivateBus server;
    QString error;
    QVERIFY2(server.listen(socket, QStringLiteral("/helper"), &helper, false, &error), qPrintable(error));
    for (const QString &method : {QStringLiteral("List"), QStringLiteral("deleteLater")}) {
        QProcess caller;
        caller.start(QCoreApplication::applicationFilePath(), QStringList()
                     << QStringLiteral("--probe") << QStringLiteral("unix:path=") + socket
                     << QStringLiteral("peer") << method
                     << (method == QLatin1String("List") ? QStringLiteral("success")
                         : QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod")));
        QVERIFY(caller.waitForStarted());
        QTRY_COMPARE(caller.state(), QProcess::NotRunning);
        QCOMPARE(caller.exitCode(), 0);
    }
    QCOMPARE(backend.listCalls, 1);
    PrivateBus second;
    QVERIFY(!second.listen(socket, QStringLiteral("/helper"), &helper, false, &error));
}

void CommonTest::privateBusRejectsUnsafeSocket()
{
    QTemporaryDir directory;
    const QString socket = directory.path() + QStringLiteral("/peer");
    QFile file(socket);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("preserve"), qint64(8));
    file.close();
    QObject object;
    PrivateBus server;
    QVERIFY(!server.listen(socket, QStringLiteral("/helper"), &object, false, nullptr));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("preserve"));
}

class FakeSync : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.harbour.flotsam.Sync")
public:
    int changes = 0;
public slots:
    Q_SCRIPTABLE void ForgetEverywhere(const QString &id) {
        if (id == QLatin1String("fixture-only")) ++changes;
    }
    Q_SCRIPTABLE QVariantMap Status() {
        const QVariantMap result{{QStringLiteral("fixture"), true}};
        emit StatusChanged(result);
        return result;
    }
    Q_SCRIPTABLE QString Echo(const QString &text, const QVariantMap &map,
                              const QStringList &list, bool flag, int number) {
        return text == QLatin1String("fixture") && map.value(QStringLiteral("n")).toInt() == 7
                && list == (QStringList() << QStringLiteral("item")) && flag && number == 9
                ? QStringLiteral("ok") : QStringLiteral("wrong arguments");
    }
signals:
    Q_SCRIPTABLE void StatusChanged(const QVariantMap &status);
};

class FakeIdentification : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.sailfishos.sailjailed")
public slots:
    Q_SCRIPTABLE QVariantMap Identify() {
        return {{QStringLiteral("pid"), int(getppid())},
                {QStringLiteral("uid"), uint(getuid())},
                {QStringLiteral("exe"), QCoreApplication::applicationFilePath()}};
    }
};

void CommonTest::processIdentityChecks()
{
    const UiBusPolicy policy{getuid(), getegid(), getuid(),
                            QCoreApplication::applicationFilePath(), QStringLiteral("/nonexistent")};
    QVERIFY(processCredentials(getpid(), getuid(), getegid()));
    QVERIFY(!processCredentials(getpid(), getuid(), getegid() + 1));
    QVERIFY(!processCredentials(0, getuid(), getegid()));
    QVERIFY(processExecutable(getpid(), policy, policy.uiExecutable));
    QVERIFY(!processExecutable(getpid(), policy, QStringLiteral("/usr/bin/true")));
    UiBusPolicy wrongOwner = policy;
    wrongOwner.executableOwner = getuid() + 1;
    QVERIFY(!processExecutable(getpid(), wrongOwner, policy.uiExecutable));
    QTemporaryDir directory;
    const QString copy = directory.path() + QStringLiteral("sandbox-copy");
    QVERIFY(QFile::copy(policy.uiExecutable, copy));
    QVERIFY(!processExecutable(getpid(), policy, copy));
    QVERIFY(processExecutable(getpid(), policy, copy, true));
    QVERIFY(!processExecutable(getpid(), wrongOwner, copy, true));
    QFile changed(copy);
    QVERIFY(changed.open(QIODevice::ReadWrite));
    QVERIFY(changed.seek(8));
    QVERIFY(changed.write("not-the-ui") > 0);
    changed.close();
    QVERIFY(!processExecutable(getpid(), policy, copy, true));
}

void CommonTest::systemBusUiGate()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile policyFile(QFINDTESTDATA("../../data/dbus-1/system.d/org.harbour.flotsam.Sync.conf"));
    QVERIFY(policyFile.open(QIODevice::ReadOnly));
    QByteArray policyXml = policyFile.readAll();
    const QByteArray testUser = "user=\"" + QByteArray::number(getuid()) + "\"";
    policyXml.replace("user=\"defaultuser\"", testUser);
    policyXml.replace("user=\"nemo\"", testUser);
    QFile policyCopy(directory.path() + QStringLiteral("/flotsam.conf"));
    QVERIFY(policyCopy.open(QIODevice::WriteOnly));
    QCOMPARE(policyCopy.write(policyXml), qint64(policyXml.size()));
    policyCopy.close();
    QFile helperPolicy(QFINDTESTDATA("../../data/dbus-1/system.d/org.harbour.flotsam.Connman.conf"));
    QVERIFY(helperPolicy.open(QIODevice::ReadOnly));
    QByteArray helperXml = helperPolicy.readAll();
    // The isolated fixture runs both roles as the test user. Production keeps
    // helper name ownership root-only; never substitute this on the device.
    helperXml.replace("user=\"root\"", testUser);
    helperXml.replace("user=\"defaultuser\"", testUser);
    helperXml.replace("user=\"nemo\"", testUser);
    QFile helperCopy(directory.path() + QStringLiteral("/helper.conf"));
    QVERIFY(helperCopy.open(QIODevice::WriteOnly));
    QCOMPARE(helperCopy.write(helperXml), qint64(helperXml.size()));
    helperCopy.close();
    // Relevant defaults from Sailfish system.conf and sailjaild.conf, not the
    // permissive session bus. Include the actual packaged Flotsam policy.
    const QByteArray config = QByteArray(R"(<busconfig>
      <type>system</type><listen>unix:tmpdir=/tmp</listen><auth>EXTERNAL</auth>
      <policy context="default">
        <allow user="*"/><deny own="*"/><deny send_type="method_call"/>
        <allow send_type="signal"/>
        <allow send_requested_reply="true" send_type="method_return"/>
        <allow send_requested_reply="true" send_type="error"/>
        <allow receive_type="method_call"/><allow receive_type="method_return"/>
        <allow receive_type="error"/><allow receive_type="signal"/>
        <allow send_destination="org.freedesktop.DBus" send_interface="org.freedesktop.DBus"/>
        <allow send_destination="*" send_interface="org.sailfishos.sailjailed" send_member="Identify"/>
      </policy><include>)") + QFile::encodeName(policyCopy.fileName()) + "</include><include>"
            + QFile::encodeName(helperCopy.fileName()) + "</include></busconfig>";
    QFile configFile(directory.path() + QStringLiteral("/system.conf"));
    QVERIFY(configFile.open(QIODevice::WriteOnly));
    QCOMPARE(configFile.write(config), qint64(config.size()));
    configFile.close();
    QProcess bus;
    bus.start(QStringLiteral("dbus-daemon"), {QStringLiteral("--config-file=") + configFile.fileName(),
              QStringLiteral("--nofork"), QStringLiteral("--print-address=1")});
    QVERIFY(bus.waitForStarted());
    QVERIFY2(bus.waitForReadyRead(), bus.readAllStandardError().constData());
    const QString address = QString::fromUtf8(bus.readLine()).trimmed();
    QDBusConnection connection = QDBusConnection::connectToBus(address, QStringLiteral("ui-gate-test"));
    QVERIFY(connection.isConnected());
    QVERIFY(!connection.registerService(QStringLiteral("org.harbour.flotsam.NotAllowed")));
    QVERIFY(connection.registerService(QStringLiteral("org.harbour.flotsam.Sync")));
    QVERIFY(connection.registerService(QStringLiteral("org.harbour.flotsam.Connman")));
    DaemonIdentity identity(connection, getegid());
    QVERIFY(!identity.VerifyDaemon(connection.baseService(), getuid())); // Bus calls only.
    QVERIFY(connection.registerObject(QStringLiteral("/org/harbour/flotsam/Connman"), &identity,
                                      QDBusConnection::ExportScriptableSlots));
    const UiBusPolicy policy{getuid(), getegid(), getuid(),
                            QCoreApplication::applicationFilePath(), QStringLiteral("/nonexistent-proxy")};
    FakeSync api;
    UiBus gate(&api, connection, QString(), policy);
    connect(&api, &FakeSync::StatusChanged, &gate,
            [&gate](const QVariantMap &status) { gate.sendSignal(QStringLiteral("StatusChanged"), {status}); });
    QVERIFY(connection.registerVirtualObject(QStringLiteral("/org/harbour/flotsam/Sync"), &gate));
    const QString fake = directory.path() + QStringLiteral("pretend-ui");
    QVERIFY(QFile::copy(policy.uiExecutable, fake));
    // The copied executable exports a forged Identify pointing back at this
    // legitimate process. It must fail before that claimed identity is trusted.
    for (const QString &program : {policy.uiExecutable, fake}) {
        QProcess caller;
        caller.start(program, {QStringLiteral("--system-ui-probe"), address,
                     program == fake ? QStringLiteral("denied") : QStringLiteral("allowed")});
        QVERIFY(caller.waitForStarted());
        QTRY_COMPARE(caller.state(), QProcess::NotRunning);
        QCOMPARE(caller.exitCode(), 0);
    }
    connection.unregisterObject(QStringLiteral("/org/harbour/flotsam/Sync"));
    UiBusPolicy proxyPolicy = policy;
    proxyPolicy.proxyExecutable = fake;
    UiBus proxyGate(&api, connection, QString(), proxyPolicy);
    connect(&api, &FakeSync::StatusChanged, &proxyGate,
            [&proxyGate](const QVariantMap &status) { proxyGate.sendSignal(QStringLiteral("StatusChanged"), {status}); });
    QVERIFY(connection.registerVirtualObject(QStringLiteral("/org/harbour/flotsam/Sync"), &proxyGate));
    QProcess proxyCaller;
    proxyCaller.start(fake, {QStringLiteral("--system-ui-probe"), address, QStringLiteral("allowed")});
    QVERIFY(proxyCaller.waitForStarted());
    QTRY_COMPARE(proxyCaller.state(), QProcess::NotRunning);
    QCOMPARE(proxyCaller.exitCode(), 0);
    connection.unregisterObject(QStringLiteral("/org/harbour/flotsam/Sync"));
    UiBusPolicy wrongGroup = policy;
    wrongGroup.gid = getegid() + 1;
    UiBus deniedGate(&api, connection, QString(), wrongGroup);
    QVERIFY(connection.registerVirtualObject(QStringLiteral("/org/harbour/flotsam/Sync"), &deniedGate));
    QProcess unprivileged;
    unprivileged.start(policy.uiExecutable, {QStringLiteral("--system-ui-probe"), address, QStringLiteral("denied")});
    QVERIFY(unprivileged.waitForStarted());
    QTRY_COMPARE(unprivileged.state(), QProcess::NotRunning);
    QCOMPARE(unprivileged.exitCode(), 0);
    QCOMPARE(api.changes, 2); // Only the two authenticated callers reached it.
    connection.unregisterObject(QStringLiteral("/org/harbour/flotsam/Connman"));
    DaemonIdentity wrongIdentity(connection, getegid() + 1);
    QVERIFY(connection.registerObject(QStringLiteral("/org/harbour/flotsam/Connman"), &wrongIdentity,
                                      QDBusConnection::ExportScriptableSlots));
    QProcess wrongDaemon;
    wrongDaemon.start(policy.uiExecutable, {QStringLiteral("--system-ui-probe"), address,
                                           QStringLiteral("wrong-daemon-group")});
    QVERIFY(wrongDaemon.waitForStarted());
    QTRY_COMPARE(wrongDaemon.state(), QProcess::NotRunning);
    QCOMPARE(wrongDaemon.exitCode(), 0);
    QDBusConnection::disconnectFromBus(connection.name());
    bus.terminate();
    QVERIFY(bus.waitForFinished());
}

void CommonTest::uiProxyUsesPrivatePeer()
{
    QTemporaryDir directory;
    const QString socket = directory.path() + QStringLiteral("/peer");
    PrivateBus server;
    FakeSync sync;
    QVERIFY(server.listen(socket, QStringLiteral("/org/harbour/flotsam/Sync"), &sync, false, nullptr));
    QProcess caller;
    caller.start(QCoreApplication::applicationFilePath(), QStringList()
                 << QStringLiteral("--ui-probe") << QStringLiteral("unix:path=") + socket);
    QVERIFY(caller.waitForStarted());
    QTRY_COMPARE(caller.state(), QProcess::NotRunning);
    QCOMPARE(caller.exitCode(), 0);
}

void CommonTest::crossUidBoundary()
{
    if (geteuid() != 0) QSKIP("Run this test in the isolated root container fixture");
    const uid_t deviceUid = 100001;
    const gid_t trustedGid = 29995;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(chmod(QFile::encodeName(directory.path()).constData(), 0755) == 0);
    const QString guard = directory.path() + QStringLiteral("/guard");
    QVERIFY(QDir().mkdir(guard));
    QVERIFY(chown(QFile::encodeName(guard).constData(), 0, trustedGid) == 0);
    QVERIFY(chmod(QFile::encodeName(guard).constData(), 0750) == 0);
    const QString socket = guard + QStringLiteral("/peer");
    FakeBackend backend;
    ConnmanHelper helper(&backend);
    PrivateBus server;
    QVERIFY(server.listen(socket, QStringLiteral("/helper"), &helper, true, nullptr));
    QVERIFY(chown(QFile::encodeName(socket).constData(), 0, trustedGid) == 0);
    const QString statePath = guard + QStringLiteral("/state.json");
    AtomicState state(statePath);
    QVERIFY(state.save());
    // save() makes its test parent private; restore the production-style gate.
    QVERIFY(chmod(QFile::encodeName(guard).constData(), 0750) == 0);
    QVERIFY(chown(QFile::encodeName(statePath).constData(), deviceUid, trustedGid) == 0);
    for (const QString &role : {QStringLiteral("ordinary"), QStringLiteral("privileged")}) {
        QProcess caller;
        caller.start(QCoreApplication::applicationFilePath(), QStringList()
                     << QStringLiteral("--probe") << QStringLiteral("unix:path=") + socket
                     << QStringLiteral("peer") << QStringLiteral("List")
                     << (role == QLatin1String("privileged") ? QStringLiteral("success")
                         : QStringLiteral("org.freedesktop.DBus.Error.Disconnected")) << role);
        QVERIFY(caller.waitForStarted());
        QTRY_COMPARE(caller.state(), QProcess::NotRunning);
        QCOMPARE(caller.exitCode(), 0);
        QProcess reader;
        reader.start(QCoreApplication::applicationFilePath(), QStringList()
                     << QStringLiteral("--state-probe") << statePath << role);
        QVERIFY(reader.waitForStarted());
        QTRY_COMPARE(reader.state(), QProcess::NotRunning);
        QCOMPARE(reader.exitCode(), 0);
    }
    QCOMPARE(backend.listCalls, 1);
}

void CommonTest::helperCompareAndSwap()
{
    FakeBackend backend;
    ConnmanServiceData service;
    service.path = QStringLiteral("/net/connman/service/wifi_aabbccddeeff_436166653b4f6e65_managed_psk");
    service.properties = properties();
    backend.values.append(service);
    backend.passwords.insert(service.path, QStringLiteral("password1"));
    ConnmanHelper helper(&backend);
    NetworkRecord update = record();
    update.hidden = true;
    const QString response = helper.CompareAndApply(QString(64, QLatin1Char('0')),
                                                    QString::fromUtf8(update.toJsonData()), false);
    QCOMPARE(QJsonDocument::fromJson(response.toUtf8()).object().value("status").toString(),
             QStringLiteral("Conflict"));
    QCOMPARE(backend.createCalls, 0);
}

void CommonTest::helperRemovesMultipleAdapters()
{
    FakeBackend backend;
    const QStringList paths = QStringList()
            << QStringLiteral("/net/connman/service/wifi_aabbccddeeff_436166653b4f6e65_managed_psk")
            << QStringLiteral("/net/connman/service/wifi_112233445566_436166653b4f6e65_managed_sae");
    for (const QString &path : paths) {
        ConnmanServiceData service;
        service.path = path;
        service.properties = properties(path.endsWith(QStringLiteral("sae")) ? "sae" : "psk");
        backend.values.append(service);
        backend.passwords.insert(path, QStringLiteral("password1"));
    }
    ConnmanHelper helper(&backend);
    const NetworkRecord value = record();
    const QString response = helper.CompareAndRemove(value.networkId(),
                                                     QString::fromLatin1(value.contentFingerprint()));
    QCOMPARE(QJsonDocument::fromJson(response.toUtf8()).object().value("status").toString(),
             QStringLiteral("Ok"));
    QCOMPARE(backend.removed.size(), 2);
}

void CommonTest::helperRollsBack()
{
    FakeBackend backend;
    ConnmanServiceData service;
    service.path = QStringLiteral("/net/connman/service/wifi_aabbccddeeff_436166653b4f6e65_managed_psk");
    service.properties = properties();
    backend.values.append(service);
    backend.passwords.insert(service.path, QStringLiteral("password1"));
    backend.failSet = true;
    ConnmanHelper helper(&backend);
    NetworkRecord update = record();
    update.hidden = true;
    const QString response = helper.CompareAndApply(
            QString::fromLatin1(record().contentFingerprint()),
            QString::fromUtf8(update.toJsonData()), false);
    QCOMPARE(QJsonDocument::fromJson(response.toUtf8()).object().value("status").toString(),
             QStringLiteral("Failed"));
    QCOMPARE(backend.createCalls, 2);
}

void CommonTest::helperRemovesFailedNewService()
{
    FakeBackend backend;
    backend.failSet = true;
    ConnmanHelper helper(&backend);
    const NetworkRecord update = record();
    const QString response = helper.CompareAndApply(
            QString(), QString::fromUtf8(update.toJsonData()), false);
    QCOMPARE(QJsonDocument::fromJson(response.toUtf8()).object().value("status").toString(),
             QStringLiteral("Failed"));
    QCOMPARE(backend.removed, QStringList() << backend.createdPath);
}

void CommonTest::notificationTokensRejectStaleActions()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QJsonObject tokens;
    QJsonObject current;
    current.insert(QStringLiteral("networkId"), QStringLiteral("pending-id"));
    current.insert(QStringLiteral("kind"), QStringLiteral("new"));
    current.insert(QStringLiteral("createdAt"), now.toString(Qt::ISODate));
    tokens.insert(QStringLiteral("current"), current);

    NotificationTokenResult result = NotificationToken::take(
            &tokens, QStringLiteral("current"), QStringLiteral("sync"),
            QStringList() << QStringLiteral("pending-id"), now);
    QVERIFY(result.valid);
    QVERIFY(!tokens.contains(QStringLiteral("current")));

    QJsonObject old = current;
    old.insert(QStringLiteral("createdAt"), now.addSecs(-24 * 60 * 60 - 1).toString(Qt::ISODate));
    tokens.insert(QStringLiteral("old"), old);
    result = NotificationToken::take(&tokens, QStringLiteral("old"), QStringLiteral("sync"),
                                     QStringList() << QStringLiteral("pending-id"), now);
    QVERIFY(!result.valid);
    QVERIFY(!tokens.contains(QStringLiteral("old")));

    tokens.insert(QStringLiteral("resolved"), current);
    result = NotificationToken::take(&tokens, QStringLiteral("resolved"), QStringLiteral("keep"),
                                     QStringList(), now);
    QVERIFY(!result.valid);

    result = NotificationToken::take(&tokens, QStringLiteral("missing"), QStringLiteral("sync"),
                                     QStringList() << QStringLiteral("pending-id"), now);
    QVERIFY(!result.valid);
}

int main(int argc, char **argv)
{
    if ((argc == 7 && QByteArray(argv[1]) == "--probe")
            || (argc == 4 && QByteArray(argv[1]) == "--state-probe")) {
        const bool privileged = QByteArray(argv[argc - 1]) == "privileged";
        if (getuid() != 0 || setgroups(0, nullptr) != 0
                || setgid(privileged ? 29995 : 100001) != 0 || setuid(100001) != 0) return 4;
    }
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() == 4 && args.at(1) == QLatin1String("--system-ui-probe")) {
        QDBusConnection connection = QDBusConnection::connectToBus(args.at(2), QStringLiteral("system-ui-probe"));
        const QDBusReply<QString> owner = connection.interface()->serviceOwner(QStringLiteral("org.harbour.flotsam.Sync"));
        if (!owner.isValid()) return 18;
        for (int scenario = 0; scenario < 5; ++scenario) {
            QDBusMessage query = QDBusMessage::createMethodCall(QStringLiteral("org.harbour.flotsam.Connman"),
                    QStringLiteral("/org/harbour/flotsam/Connman"), QStringLiteral("org.harbour.flotsam.Identity1"),
                    QStringLiteral("VerifyDaemon"));
            query << (scenario == 2 ? QStringLiteral("org.harbour.flotsam.Sync")
                      : scenario == 3 ? connection.baseService()
                      : scenario == 4 ? QStringLiteral(":99999.99999") : owner.value())
                  << uint(scenario == 1 ? getuid() + 1 : getuid());
            QDBusPendingCallWatcher attest(connection.asyncCall(query));
            QSignalSpy done(&attest, &QDBusPendingCallWatcher::finished);
            if (!attest.isFinished() && !done.wait(3000)) return 19;
            const QDBusPendingReply<bool> verified = attest;
            if (verified.isError() || verified.value() != (scenario == 0
                    && args.at(3) != QLatin1String("wrong-daemon-group"))) return 20;
        }
        if (args.at(3) == QLatin1String("wrong-daemon-group")) return 0;
        FakeIdentification identity;
        connection.registerObject(QStringLiteral("/"), &identity, QDBusConnection::ExportScriptableSlots);
        SyncInterfaceProxy proxy(connection, nullptr, QStringLiteral("org.harbour.flotsam.Sync"));
        QSignalSpy statusSignals(&proxy, &SyncInterfaceProxy::StatusChanged);
        QDBusPendingCallWatcher call(proxy.asyncCall(QStringLiteral("Status")));
        QSignalSpy finished(&call, &QDBusPendingCallWatcher::finished);
        if (!call.isFinished() && !finished.wait(3000)) return 10;
        const QDBusPendingReply<QVariantMap> reply = call;
        QDBusPendingCallWatcher mutation(proxy.asyncCall(QStringLiteral("ForgetEverywhere"), QStringLiteral("fixture-only")));
        QSignalSpy mutationFinished(&mutation, &QDBusPendingCallWatcher::finished);
        if (!mutation.isFinished() && !mutationFinished.wait(4000)) return 16;
        if (args.at(3) == QLatin1String("denied"))
            return reply.isError() && reply.error().name() == QLatin1String("org.harbour.flotsam.Error.AccessDenied")
                    && mutation.error().name() == QLatin1String("org.harbour.flotsam.Error.AccessDenied")
                    && statusSignals.isEmpty() ? 0 : 11;
        if (mutation.isError()) return 17;
        if (reply.isError() || !reply.value().value(QStringLiteral("fixture")).toBool()) return 12;
        if (statusSignals.isEmpty()) statusSignals.wait(1000);
        if (statusSignals.size() != 1) return 13;
        QDBusPendingCallWatcher echoCall(proxy.asyncCall(QStringLiteral("Echo"), QStringLiteral("fixture"),
                QVariantMap{{QStringLiteral("n"), 7}}, QStringList() << QStringLiteral("item"), true, 9));
        QSignalSpy echoFinished(&echoCall, &QDBusPendingCallWatcher::finished);
        if (!echoCall.isFinished() && !echoFinished.wait(4000)) return 14;
        const QDBusPendingReply<QString> echo = echoCall;
        if (echo.isError() || echo.value() != QLatin1String("ok")) return 14;
        QDBusPendingCallWatcher inherited(proxy.asyncCall(QStringLiteral("deleteLater")));
        QSignalSpy inheritedFinished(&inherited, &QDBusPendingCallWatcher::finished);
        if (!inherited.isFinished() && !inheritedFinished.wait(4000)) return 15;
        return inherited.isError() ? 0 : 15;
    }
    if (args.size() == 4 && args.at(1) == QLatin1String("--state-probe")) {
        QFile file(args.at(2));
        const bool opened = file.open(QIODevice::ReadOnly);
        file.close();
        const bool writable = file.open(QIODevice::WriteOnly | QIODevice::Append);
        const bool expected = args.at(3) == QLatin1String("privileged");
        return opened == expected && writable == expected ? 0 : 5;
    }
    if (args.size() == 3 && args.at(1) == QLatin1String("--ui-probe")) {
        const QDBusConnection connection = QDBusConnection::connectToPeer(args.at(2), QStringLiteral("ui-probe"));
        SyncInterfaceProxy proxy(connection);
        QSignalSpy statusSignals(&proxy, &SyncInterfaceProxy::StatusChanged);
        QDBusPendingCallWatcher watcher(proxy.asyncCall(QStringLiteral("Status")));
        QSignalSpy finished(&watcher, &QDBusPendingCallWatcher::finished);
        if (!watcher.isFinished() && !finished.wait(3000)) return 6;
        QDBusPendingReply<QVariantMap> reply = watcher;
        if (statusSignals.isEmpty()) statusSignals.wait(3000);
        return !reply.isError() && reply.value().value(QStringLiteral("fixture")).toBool()
                && statusSignals.size() == 1 ? 0 : 7;
    }
    if ((args.size() == 6 || args.size() == 7) && args.at(1) == QLatin1String("--probe")) {
        const bool peer = args.at(3) == QLatin1String("peer");
        const QDBusConnection connection = peer
                ? QDBusConnection::connectToPeer(args.at(2), QStringLiteral("probe"))
                : QDBusConnection::connectToBus(args.at(2), QStringLiteral("probe"));
        QDBusMessage call = QDBusMessage::createMethodCall(
                peer ? QString() : QStringLiteral("org.harbour.flotsam.Test"),
                QStringLiteral("/helper"), QStringLiteral("org.harbour.flotsam.Connman"), args.at(4));
        if (args.at(4) == QLatin1String("Export")) call.setArguments(QVariantList() << QString());
        if (args.at(4) == QLatin1String("CompareAndApply")) call.setArguments(QVariantList() << QString() << QString() << false);
        if (args.at(4) == QLatin1String("CompareAndRemove")) call.setArguments(QVariantList() << QString() << QString());
        const QDBusMessage reply = connection.call(call, QDBus::Block, 3000);
        return args.at(5) == QLatin1String("success")
                ? (reply.type() == QDBusMessage::ReplyMessage && reply.arguments().value(0).toString() == QLatin1String("[]") ? 0 : 2)
                : (reply.errorName() == args.at(5) ? 0 : 3);
    }
    CommonTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_common.moc"
