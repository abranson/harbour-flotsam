/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "atomicstate.h"
#include "applicationactivation.h"
#include "connmanhelper.h"
#include "connmanutil.h"
#include "networkrecord.h"
#include "notificationtoken.h"
#include "reconciler.h"

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
    QStringList removed;

    QList<ConnmanServiceData> services(QString *) override { return values; }
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
    void helperCompareAndSwap();
    void helperRemovesMultipleAdapters();
    void helperRollsBack();
    void helperRemovesFailedNewService();
    void notificationTokensRejectStaleActions();
    void applicationActivationRoutesNetworks();
};

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

QTEST_APPLESS_MAIN(CommonTest)

#include "tst_common.moc"
