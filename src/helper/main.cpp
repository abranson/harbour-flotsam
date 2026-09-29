/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "connmanbackend.h"
#include "connmanhelper.h"
#include "privatebus.h"
#include "security.h"
#include "uibus.h"

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
    Flotsam::PrivateBus privateBus;
    QString error;
    if (!Flotsam::Security::trustedDirectory(
                QStringLiteral("/run/harbour-flotsam-helper"), false)
            || !privateBus.listen(Flotsam::Security::helperSocket(),
                QStringLiteral("/org/harbour/flotsam/Connman"), &helper, true, &error)) {
        qCritical() << "Cannot start protected ConnMan endpoint:" << error;
        return 1;
    }
    // Public identity attestation only; network operations remain private.
    QDBusConnection bus = QDBusConnection::systemBus();
    Flotsam::DaemonIdentity identity(bus);
    if (!bus.registerObject(QStringLiteral("/org/harbour/flotsam/Connman"), &identity,
                            QDBusConnection::ExportScriptableSlots)
            || !bus.registerService(QStringLiteral("org.harbour.flotsam.Connman"))) {
        qCritical() << "Unable to register the Flotsam ConnMan helper on the system bus:"
                    << bus.lastError().message();
        return 1;
    }
    return application.exec();
}
