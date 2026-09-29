/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "applicationactivation.h"
#include "appcontroller.h"

#include <QDBusConnection>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickView>
#include <QScopedPointer>
#include <sailfishapp.h>

int main(int argc, char **argv)
{
    QScopedPointer<QGuiApplication> application(SailfishApp::application(argc, argv));
    application->setOrganizationName(QStringLiteral("uk.co.nationalfantastic"));
    application->setApplicationName(QStringLiteral("harbour-flotsam"));

    Flotsam::ApplicationActivation activation;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(QStringLiteral("/uk/co/nationalfantastic/harbour_flotsam"),
                            &activation, QDBusConnection::ExportAllSlots)
            || !bus.registerService(
                    QStringLiteral("uk.co.nationalfantastic.harbour-flotsam"))) {
        return 1;
    }

    QScopedPointer<QQuickView> view(SailfishApp::createView());
    Flotsam::AppController controller;
    view->rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
    view->rootContext()->setContextProperty(QStringLiteral("applicationActivation"),
                                            &activation);
    view->setSource(SailfishApp::pathTo(QStringLiteral("qml/main.qml")));
    view->show();
    return application->exec();
}
