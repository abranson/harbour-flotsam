/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "atomicstate.h"
#include "security.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>

namespace {

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

bool safeFile(const struct stat &st)
{
    return S_ISREG(st.st_mode) && st.st_uid == getuid() && st.st_nlink == 1
            && (st.st_mode & 07777) == 0600 && st.st_size <= 1024 * 1024;
}

struct DirectoryFd {
    int value = -1;
    ~DirectoryFd() { if (value >= 0) close(value); }
};

int openDirectoryWithoutLinks(const QString &path)
{
    if (!path.startsWith(QLatin1Char('/'))) return -1;
    int fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    for (const QString &component : path.split(QLatin1Char('/'), QString::SkipEmptyParts)) {
        if (fd < 0) return -1;
        if (component == QLatin1String(".") || component == QLatin1String("..")) {
            close(fd);
            return -1;
        }
        const int next = openat(fd, QFile::encodeName(component).constData(),
                                O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        close(fd);
        fd = next;
    }
    return fd;
}

}

namespace Flotsam {

AtomicState::AtomicState(const QString &path, const QString &legacyPath)
    : m_path(path.isEmpty() ? defaultPath() : path)
    , m_protected(path.isEmpty())
    , m_legacyPath(path.isEmpty()
                   ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                     + QStringLiteral("/harbour-flotsam/state.json") : legacyPath)
    , m_object(initialObject())
{
}

QString AtomicState::defaultPath()
{
    return Security::userDirectory(QStringLiteral("/var/lib/harbour-flotsam"))
            + QStringLiteral("/state.json");
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
    if (m_protected && !Security::prepareUserDirectory(
                QStringLiteral("/var/lib/harbour-flotsam"), error)) return false;
    const QString legacy = m_legacyPath;
    struct stat existing;
    const bool exists = lstat(QFile::encodeName(m_path).constData(), &existing) == 0;
    if (!exists && errno != ENOENT) {
        setError(error, QStringLiteral("Cannot inspect protected state"));
        return false;
    }
    const bool legacyExists = !legacy.isEmpty()
            && lstat(QFile::encodeName(legacy).constData(), &existing) == 0;
    if (!legacy.isEmpty() && !legacyExists && errno != ENOENT) {
        setError(error, QStringLiteral("Cannot inspect legacy state"));
        return false;
    }
    if (exists && legacyExists) {
        setError(error, QStringLiteral("Legacy state remains outside protected storage; review and remove that copy before starting Flotsam"));
        return false;
    }
    if (!exists && !legacyExists) {
        m_object = initialObject();
        return true;
    }
    const QString source = exists ? m_path : legacy;
    // Anchor every path component with openat/O_NOFOLLOW. A concurrent rename
    // of a user-controlled legacy parent must not redirect privileged file I/O.
    const QFileInfo sourceInfo(source);
    DirectoryFd sourceDirectory;
    sourceDirectory.value = openDirectoryWithoutLinks(sourceInfo.absolutePath());
    if (sourceDirectory.value < 0) {
        setError(error, QStringLiteral("State path contains a symlink"));
        return false;
    }
    const QByteArray sourceName = QFile::encodeName(sourceInfo.fileName());
    const int fd = openat(sourceDirectory.value, sourceName.constData(),
                         O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0 || !safeFile(st)) {
        if (fd >= 0) close(fd);
        setError(error, QStringLiteral("State must be a private, single-link regular file of at most 1 MiB"));
        return false;
    }
    QFile file;
    if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        close(fd);
        setError(error, QStringLiteral("Cannot read state"));
        return false;
    }
    QJsonParseError parseError;
    const QByteArray data = file.read(1024 * 1024 + 1);
    if (data.size() > 1024 * 1024) {
        setError(error, QStringLiteral("State file exceeds 1 MiB"));
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
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
    if (!exists) {
        // Legacy state was writable by any unrestricted same-UID application.
        // Preserve it, but require a deliberate manual sync before replaying it.
        m_object.insert(QStringLiteral("migrationNeedsReview"), true);
        m_object.insert(QStringLiteral("notificationTokens"), QJsonObject());
        if (!save(error)) return false;
        struct stat current;
        if (fstatat(sourceDirectory.value, sourceName.constData(), &current, AT_SYMLINK_NOFOLLOW) != 0
                || current.st_dev != st.st_dev || current.st_ino != st.st_ino
                || unlinkat(sourceDirectory.value, sourceName.constData(), 0) != 0) {
            setError(error, QStringLiteral("State was migrated, but the legacy copy could not safely be removed"));
            return false;
        }
    }
    return true;
}

bool AtomicState::save(QString *error) const
{
    if (m_protected && !Security::prepareUserDirectory(
                QStringLiteral("/var/lib/harbour-flotsam"), error)) return false;
    const QFileInfo info(m_path);
    QDir parent = info.dir();
    if (!parent.exists() && !parent.mkpath(QStringLiteral("."))) {
        setError(error, QStringLiteral("Cannot create the state directory"));
        return false;
    }
    if (parent.canonicalPath() != parent.absolutePath()
            || !QFile::setPermissions(parent.absolutePath(), QFileDevice::ReadOwner
                          | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) {
        setError(error, QStringLiteral("Unsafe state directory"));
        return false;
    }
    struct stat st;
    if (lstat(QFile::encodeName(m_path).constData(), &st) == 0 && !safeFile(st)) {
        setError(error, QStringLiteral("Unsafe existing state file"));
        return false;
    }

    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, file.errorString());
        return false;
    }
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        setError(error, QStringLiteral("Cannot protect state permissions"));
        return false;
    }
    const QByteArray data = QJsonDocument(m_object).toJson(QJsonDocument::Compact);
    if (data.size() > 1024 * 1024) {
        setError(error, QStringLiteral("State file exceeds 1 MiB"));
        return false;
    }
    if (file.write(data) != data.size() || !file.commit()) {
        setError(error, file.errorString());
        return false;
    }
    QFile::setPermissions(m_path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

}
