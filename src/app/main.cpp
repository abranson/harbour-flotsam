/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "appcontroller.h"

#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickView>
#include <QScopedPointer>
#include <sailfishapp.h>

int main(int argc, char **argv)
{
    QScopedPointer<QGuiApplication> application(SailfishApp::application(argc, argv));
    QScopedPointer<QQuickView> view(SailfishApp::createView());
    Flotsam::AppController controller;
    view->rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
    view->setSource(SailfishApp::pathTo(QStringLiteral("qml/main.qml")));
    view->show();
    return application->exec();
}
