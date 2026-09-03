/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "connmanbackend.h"
#include "connmanhelper.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDebug>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("harbour-flotsam-connman-helper"));

    Flotsam::RealConnmanBackend backend;
    Flotsam::ConnmanHelper helper(&backend);
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.registerObject(QStringLiteral("/org/harbour/flotsam/Connman"), &helper,
                            QDBusConnection::ExportAllSlots)
            || !bus.registerService(QStringLiteral("org.harbour.flotsam.Connman"))) {
        qCritical() << "Unable to register the Flotsam ConnMan helper on the system bus:"
                    << bus.lastError().message();
        return 1;
    }
    return application.exec();
}
