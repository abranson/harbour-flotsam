/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "syncdaemon.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDebug>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("harbour-flotsam-syncd"));

    Flotsam::SyncDaemon daemon;
    QString error;
    if (!daemon.initialize(&error)) {
        qCritical() << "Unable to initialize Flotsam:" << error;
        return 1;
    }
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(QStringLiteral("/org/harbour/flotsam/Sync"), &daemon,
                            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)
            || !bus.registerService(QStringLiteral("org.harbour.flotsam.Sync"))) {
        qCritical() << "Unable to register Flotsam on the session bus:"
                    << bus.lastError().message();
        return 1;
    }
    return application.exec();
}
