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
#include <QStandardPaths>
#include <QUuid>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

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

struct TemporaryState {
    int directory = -1;
    QByteArray name;
    ~TemporaryState() { if (!name.isEmpty()) unlinkat(directory, name.constData(), 0); }
};

int openDirectoryWithoutLinks(const QString &path)
{
    if (!path.startsWith(QLatin1Char('/'))) return -1;
    int fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    for (const QString &component : path.split(QLatin1Char('/'), QString::SkipEmptyParts)) {
        if (fd < 0) return -1;
        if (component == QLatin1String(".") || component == QLatin1String("..")) {
            close(fd);
            errno = EINVAL;
            return -1;
        }
        const int next = openat(fd, QFile::encodeName(component).constData(),
                                O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        const int savedError = errno;
        close(fd);
        errno = savedError;
        fd = next;
    }
    return fd;
}

}

namespace Flotsam {

AtomicState::AtomicState(const QString &path, const QString &legacyPath,
                         const QString &previousProtectedPath)
    : m_path(path.isEmpty() ? defaultPath() : path)
    , m_protected(path.isEmpty())
    , m_legacyPath(path.isEmpty()
                   ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                     + QStringLiteral("/harbour-flotsam/state.json") : legacyPath)
    , m_previousProtectedPath(path.isEmpty()
                              ? Security::userDirectory(QStringLiteral("/var/lib/harbour-flotsam"))
                                + QStringLiteral("/state.json") : previousProtectedPath)
    , m_object(initialObject())
{
}

QString AtomicState::defaultPath()
{
    return Security::stateDirectory() + QStringLiteral("/state.json");
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
    // Inspect all supported locations through anchored descriptors. Never choose
    // between two copies by timestamps, and never treat inaccessible state as a
    // fresh installation. An interrupted migration therefore fails closed.
    DirectoryFd directories[3];
    const QString paths[] = {m_path, m_previousProtectedPath, m_legacyPath};
    int selected = -1;
    for (int i = 0; i < 3; ++i) {
        if (paths[i].isEmpty()) continue;
        if (m_protected && i == 0) {
            directories[i].value = Security::openStateDirectory(error);
            if (directories[i].value < 0) return false;
        } else if (m_protected && i == 1) {
            directories[i].value = Security::openPreviousStateDirectory(error);
        } else {
            directories[i].value = openDirectoryWithoutLinks(QFileInfo(paths[i]).absolutePath());
        }
        if (directories[i].value < 0) {
            if (errno == ENOENT) continue;
            setError(error, QStringLiteral("Cannot safely open a state directory"));
            return false;
        }
        struct stat existing;
        if (fstatat(directories[i].value, QFile::encodeName(QFileInfo(paths[i]).fileName()).constData(),
                    &existing, AT_SYMLINK_NOFOLLOW) != 0) {
            if (errno == ENOENT) continue;
            setError(error, QStringLiteral("Cannot inspect state"));
            return false;
        }
        if (selected >= 0) {
            setError(error, QStringLiteral("Multiple Flotsam state files exist; review the copies before starting Flotsam"));
            return false;
        }
        selected = i;
    }
    if (selected < 0) {
        m_object = initialObject();
        return true;
    }
    const int sourceDirectory = directories[selected].value;
    const QByteArray sourceName = QFile::encodeName(QFileInfo(paths[selected]).fileName());
    const int fd = openat(sourceDirectory, sourceName.constData(),
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
    if (selected != 0) {
        if (selected == 2) {
            // Only the original, unprotected store requires a new review.
            // Relocation of protected state preserves every decision and flag.
            m_object.insert(QStringLiteral("migrationNeedsReview"), true);
            m_object.insert(QStringLiteral("notificationTokens"), QJsonObject());
        }
        if (!save(error)) return false;
        struct stat current;
        if (fstatat(sourceDirectory, sourceName.constData(), &current, AT_SYMLINK_NOFOLLOW) != 0
                || current.st_dev != st.st_dev || current.st_ino != st.st_ino
                || unlinkat(sourceDirectory, sourceName.constData(), 0) != 0
                || fsync(sourceDirectory) != 0) {
            setError(error, QStringLiteral("State was migrated, but the previous copy could not safely be removed"));
            return false;
        }
    }
    return true;
}

bool AtomicState::save(QString *error) const
{
    const QFileInfo info(m_path);
    QDir parent = info.dir();
    DirectoryFd directory;
    if (m_protected) {
        directory.value = Security::openStateDirectory(error);
        if (directory.value < 0) return false;
    } else {
        if (!parent.exists() && !parent.mkpath(QStringLiteral("."))) {
            setError(error, QStringLiteral("Cannot create the state directory"));
            return false;
        }
        if (parent.canonicalPath() != parent.absolutePath()) {
            setError(error, QStringLiteral("Unsafe state directory"));
            return false;
        }
        directory.value = openDirectoryWithoutLinks(parent.absolutePath());
        if (directory.value >= 0 && fchmod(directory.value, 0700) != 0) {
            setError(error, QStringLiteral("Cannot protect state directory"));
            return false;
        }
    }
    if (directory.value < 0) {
        setError(error, QStringLiteral("Cannot open state directory without symlinks"));
        return false;
    }
    const QByteArray name = QFile::encodeName(info.fileName());
    struct stat st;
    const int inspected = fstatat(directory.value, name.constData(), &st, AT_SYMLINK_NOFOLLOW);
    if ((inspected == 0 && !safeFile(st)) || (inspected != 0 && errno != ENOENT)) {
        setError(error, QStringLiteral("Unsafe existing state file"));
        return false;
    }
    const QByteArray data = QJsonDocument(m_object).toJson(QJsonDocument::Compact);
    if (data.size() > 1024 * 1024) {
        setError(error, QStringLiteral("State file exceeds 1 MiB"));
        return false;
    }

    // Qt 5.6 QSaveFile checks access(W_OK) with the real GID, which cannot
    // traverse our privileged-group parent. Do not relax that boundary or
    // change process credentials: openat/renameat use the effective GID.
    // Anchor both temporary creation and atomic replacement to the same dir.
    TemporaryState temporary;
    temporary.directory = directory.value;
    const QByteArray candidate = ".state-" + QUuid::createUuid().toByteArray();
    const int fd = openat(directory.value, candidate.constData(),
                          O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) {
        setError(error, QStringLiteral("Cannot create temporary state: %1")
                 .arg(QString::fromLocal8Bit(strerror(errno))));
        return false;
    }
    temporary.name = candidate; // Only remove a temporary file we created.
    QFile file;
    if (!file.open(fd, QIODevice::WriteOnly, QFileDevice::AutoCloseHandle)) {
        close(fd);
        setError(error, file.errorString());
        return false;
    }
    if (fchmod(fd, 0600) != 0) {
        setError(error, QStringLiteral("Cannot protect state permissions"));
        return false;
    }
    if (file.write(data) != data.size() || !file.flush()) {
        setError(error, file.errorString());
        return false;
    }
    if (fsync(fd) != 0 || renameat(directory.value, temporary.name.constData(),
                                   directory.value, name.constData()) != 0) {
        setError(error, QStringLiteral("Cannot commit state: %1")
                 .arg(QString::fromLocal8Bit(strerror(errno))));
        return false;
    }
    temporary.name.clear();
    // Make the new directory entry durable before migration removes its source.
    if (fsync(directory.value) != 0) {
        setError(error, QStringLiteral("Cannot flush state directory"));
        return false;
    }
    return true;
}

}
