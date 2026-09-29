/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "syncdaemon.h"
#include "security.h"
#include "uibus.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDebug>
#include <QFile>
#include <QLockFile>

int main(int argc, char **argv)
{
    if (!Flotsam::Security::initializeDaemon()) return 1;
    QCoreApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("harbour-flotsam-syncd"));

    Flotsam::SyncDaemon daemon;
    QString error;
    if (!Flotsam::Security::prepareUserDirectory(QStringLiteral("/var/lib/harbour-flotsam"), &error)) {
        qCritical() << "Unable to prepare Flotsam state:" << error;
        return 1;
    }
    QLockFile lock(Flotsam::Security::userDirectory(QStringLiteral("/var/lib/harbour-flotsam"))
                   + QStringLiteral("/daemon.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock() || !daemon.initialize(&error)) {
        qCritical() << "Unable to initialize Flotsam:" << error;
        return 1;
    }
    QDBusConnection system = QDBusConnection::systemBus();
    QFile xml(QStringLiteral("/usr/share/dbus-1/interfaces/org.harbour.flotsam.Sync.xml"));
    if (!xml.open(QIODevice::ReadOnly)) return 1;
    QString introspection = QString::fromUtf8(xml.readAll());
    const int begin = introspection.indexOf(QStringLiteral("<interface "));
    const int end = introspection.lastIndexOf(QStringLiteral("</interface>"));
    if (begin < 0 || end < begin) return 1;
    Flotsam::UiBus api(&daemon, system, introspection.mid(begin, end + 12 - begin));
    if (!system.registerVirtualObject(QStringLiteral("/org/harbour/flotsam/Sync"), &api)
            || !system.registerService(QStringLiteral("org.harbour.flotsam.Sync"))) {
        qCritical() << "Unable to register Flotsam on the system bus:" << system.lastError().message();
        return 1;
    }
    QObject::connect(&daemon, &Flotsam::SyncDaemon::StatusChanged, &api,
                     [&api](const QVariantMap &status) { api.sendSignal(QStringLiteral("StatusChanged"), {status}); });
    QObject::connect(&daemon, &Flotsam::SyncDaemon::NetworksChanged, &api,
                     [&api]() { api.sendSignal(QStringLiteral("NetworksChanged")); });
    QObject::connect(&daemon, &Flotsam::SyncDaemon::OperationFailed, &api,
                     [&api](const QString &error) { api.sendSignal(QStringLiteral("OperationFailed"), {error}); });
    QObject::connect(&daemon, &Flotsam::SyncDaemon::ImportFinished, &api,
                     [&api](bool ok, const QString &message) { api.sendSignal(QStringLiteral("ImportFinished"), {ok, message}); });
    // Retain activation only on the session bus for user-systemd supervision.
    QDBusConnection bus = QDBusConnection::sessionBus();
    QObject activation;
    if (!bus.registerObject(QStringLiteral("/org/harbour/flotsam/Sync"), &activation,
                            QDBusConnection::ExportScriptableSlots)
            || !bus.registerService(QStringLiteral("org.harbour.flotsam.Sync"))) {
        qCritical() << "Unable to register Flotsam on the session bus:"
                    << bus.lastError().message();
        return 1;
    }
    return application.exec();
}
