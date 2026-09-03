/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "connmanbackend.h"

#include <QDBusArgument>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDBusVariant>

namespace {

const char ConnmanService[] = "net.connman";
const char ManagerInterface[] = "net.connman.Manager";
const char ServiceInterface[] = "net.connman.Service";

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}
bool basicCall(const QString &path, const QString &method, QString *error)
{
    QDBusInterface service(QString::fromLatin1(ConnmanService), path,
                           QString::fromLatin1(ServiceInterface), QDBusConnection::systemBus());
    const QDBusMessage reply = service.call(method);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        setError(error, reply.errorMessage());
        return false;
    }
    return true;
}

}

namespace Flotsam {

QDBusArgument &operator<<(QDBusArgument &argument, const ConnmanSetting &setting)
{
    argument.beginStructure();
    argument << setting.first << setting.second;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, ConnmanSetting &setting)
{
    argument.beginStructure();
    argument >> setting.first >> setting.second;
    argument.endStructure();
    return argument;
}

QList<ConnmanServiceData> RealConnmanBackend::services(QString *error)
{
    QList<ConnmanServiceData> result;
    QDBusInterface manager(QString::fromLatin1(ConnmanService), QStringLiteral("/"),
                           QString::fromLatin1(ManagerInterface), QDBusConnection::systemBus());
    const QDBusMessage reply = manager.call(QStringLiteral("GetServices"));
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) {
        setError(error, reply.errorMessage().isEmpty()
                 ? QStringLiteral("ConnMan returned no service list") : reply.errorMessage());
        return result;
    }

    const QDBusArgument array = reply.arguments().first().value<QDBusArgument>();
    array.beginArray();
    while (!array.atEnd()) {
        ConnmanServiceData item;
        QDBusObjectPath path;
        array.beginStructure();
        array >> path >> item.properties;
        array.endStructure();
        item.path = path.path();
        result.append(item);
    }
    array.endArray();
    return result;
}

QString RealConnmanBackend::getStringProperty(const QString &path, const QString &name,
                                              QString *error)
{
    QDBusInterface service(QString::fromLatin1(ConnmanService), path,
                           QString::fromLatin1(ServiceInterface), QDBusConnection::systemBus());
    const QDBusMessage reply = service.call(QStringLiteral("GetProperty"), name);
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) {
        setError(error, reply.errorMessage().isEmpty()
                 ? QStringLiteral("ConnMan did not return the property") : reply.errorMessage());
        return QString();
    }
    const QVariant value = reply.arguments().first();
    if (value.canConvert<QDBusVariant>()) {
        return value.value<QDBusVariant>().variant().toString();
    }
    return value.toString();
}

bool RealConnmanBackend::setProperty(const QString &path, const QString &name,
                                     const QVariant &value, QString *error)
{
    QDBusInterface service(QString::fromLatin1(ConnmanService), path,
                           QString::fromLatin1(ServiceInterface), QDBusConnection::systemBus());
    const QDBusMessage reply = service.call(QStringLiteral("SetProperty"), name,
                                             QVariant::fromValue(QDBusVariant(value)));
    if (reply.type() == QDBusMessage::ErrorMessage) {
        setError(error, reply.errorMessage());
        return false;
    }
    return true;
}

QString RealConnmanBackend::createService(const ConnmanSettings &settings, QString *error)
{
    static bool registered = false;
    if (!registered) {
        qDBusRegisterMetaType<ConnmanSetting>();
        qDBusRegisterMetaType<ConnmanSettings>();
        registered = true;
    }
    QDBusInterface manager(QString::fromLatin1(ConnmanService), QStringLiteral("/"),
                           QString::fromLatin1(ManagerInterface), QDBusConnection::systemBus());
    const QDBusMessage reply = manager.call(QStringLiteral("CreateService"),
                                             QStringLiteral("wifi"), QString(), QString(),
                                             QVariant::fromValue(settings));
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) {
        setError(error, reply.errorMessage().isEmpty()
                 ? QStringLiteral("ConnMan did not return the created service") : reply.errorMessage());
        return QString();
    }
    const QVariant value = reply.arguments().first();
    if (value.canConvert<QDBusObjectPath>()) {
        return value.value<QDBusObjectPath>().path();
    }
    return value.toString();
}

bool RealConnmanBackend::removeService(const QString &path, QString *error)
{
    return basicCall(path, QStringLiteral("Remove"), error);
}

bool RealConnmanBackend::disconnectService(const QString &path, QString *error)
{
    return basicCall(path, QStringLiteral("Disconnect"), error);
}

bool RealConnmanBackend::connectService(const QString &path, QString *error)
{
    return basicCall(path, QStringLiteral("Connect"), error);
}

}
