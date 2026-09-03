/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "atomicstate.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace {

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

}

namespace Flotsam {

AtomicState::AtomicState(const QString &path)
    : m_path(path.isEmpty() ? defaultPath() : path)
    , m_object(initialObject())
{
}

QString AtomicState::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/harbour-flotsam/state.json");
}

QJsonObject AtomicState::initialObject()
{
    QJsonObject object;
    object.insert(QStringLiteral("schemaVersion"), 1);
    object.insert(QStringLiteral("deviceUuid"), QUuid::createUuid().toString());
    object.insert(QStringLiteral("deviceLabel"), QStringLiteral("Sailfish device"));
    object.insert(QStringLiteral("accountId"), 0);
    object.insert(QStringLiteral("setupComplete"), false);
    object.insert(QStringLiteral("setupStage"), QStringLiteral("account"));
    object.insert(QStringLiteral("blocks"), QJsonArray());
    object.insert(QStringLiteral("approvedLocal"), QJsonArray());
    object.insert(QStringLiteral("bases"), QJsonObject());
    object.insert(QStringLiteral("remoteEtags"), QJsonObject());
    object.insert(QStringLiteral("pending"), QJsonObject());
    object.insert(QStringLiteral("conflicts"), QJsonObject());
    object.insert(QStringLiteral("forgotten"), QJsonObject());
    object.insert(QStringLiteral("resolutions"), QJsonObject());
    object.insert(QStringLiteral("errors"), QJsonObject());
    object.insert(QStringLiteral("notificationTokens"), QJsonObject());
    object.insert(QStringLiteral("pendingTombstones"), QJsonObject());
    object.insert(QStringLiteral("retryAttempt"), 0);
    object.insert(QStringLiteral("lastResult"), QString());
    object.insert(QStringLiteral("lastError"), QString());
    object.insert(QStringLiteral("lastSync"), QString());
    return object;
}

QString AtomicState::path() const
{
    return m_path;
}

QJsonObject AtomicState::object() const
{
    return m_object;
}

void AtomicState::setObject(const QJsonObject &object)
{
    m_object = object;
}

bool AtomicState::load(QString *error)
{
    QFile file(m_path);
    if (!file.exists()) {
        m_object = initialObject();
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, file.errorString());
        return false;
    }
    if (file.size() > 1024 * 1024) {
        setError(error, QStringLiteral("State file exceeds 1 MiB"));
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QStringLiteral("State file is malformed"));
        return false;
    }
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("schemaVersion")).toInt() != 1) {
        setError(error, QStringLiteral("Unsupported state schema"));
        return false;
    }
    m_object = object;
    return true;
}

bool AtomicState::save(QString *error) const
{
    const QFileInfo info(m_path);
    QDir parent = info.dir();
    if (!parent.exists() && !parent.mkpath(QStringLiteral("."))) {
        setError(error, QStringLiteral("Cannot create the state directory"));
        return false;
    }
    QFile::setPermissions(parent.absolutePath(), QFileDevice::ReadOwner
                          | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, file.errorString());
        return false;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const QByteArray data = QJsonDocument(m_object).toJson(QJsonDocument::Compact);
    if (file.write(data) != data.size() || !file.commit()) {
        setError(error, file.errorString());
        return false;
    }
    QFile::setPermissions(m_path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

}
