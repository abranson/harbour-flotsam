/* SPDX-License-Identifier: BSD-3-Clause */
#include "security.h"

#include <QFile>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <cerrno>

namespace Flotsam {
namespace Security {
namespace {
gid_t privilegedGroup()
{
    const group *entry = getgrnam("privileged");
    return entry ? entry->gr_gid : gid_t(-1);
}

bool socketOwnedBy(const QString &path, uid_t uid, mode_t mode)
{
    struct stat st;
    return lstat(QFile::encodeName(path).constData(), &st) == 0
            && S_ISSOCK(st.st_mode) && st.st_uid == uid
            && st.st_gid == privilegedGroup() && (st.st_mode & 07777) == mode;
}

int openDirectory(const QString &path)
{
    if (!path.startsWith(QLatin1Char('/'))) { errno = EINVAL; return -1; }
    int fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    for (const QString &part : path.split(QLatin1Char('/'), QString::SkipEmptyParts)) {
        if (fd < 0) return -1;
        if (part == QLatin1String(".") || part == QLatin1String("..")) {
            close(fd); errno = EINVAL; return -1;
        }
        const int next = openat(fd, QFile::encodeName(part).constData(),
                                O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        const int savedError = errno;
        close(fd);
        errno = savedError;
        fd = next;
    }
    return fd;
}
}

QString stateDirectory()
{
    const passwd *entry = getpwuid(getuid());
    return entry ? QString::fromLocal8Bit(entry->pw_dir)
                   + QStringLiteral("/.local/share/system/privileged/harbour-flotsam") : QString();
}

int openPrivilegedStateDirectory(const QString &home, uint owner, uint group, QString *error)
{
    // The platform creates the shared gate. Never create it, chmod it, or trust
    // HOME/XDG overrides. Open each component without following symlinks and
    // validate the gate on the descriptor used for all subsequent operations.
    const int gate = openDirectory(home + QStringLiteral("/.local/share/system/privileged"));
    struct stat st;
    if (gate >= 0 && fstat(gate, &st) == 0 && st.st_uid == owner && st.st_gid == group
            && (st.st_mode & 07777) == 0770) {
        if (mkdirat(gate, "harbour-flotsam", 0700) == 0 || errno == EEXIST) {
            const int fd = openat(gate, "harbour-flotsam",
                                  O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (fd >= 0 && fstat(fd, &st) == 0 && st.st_uid == getuid()
                    && st.st_gid == group && (st.st_mode & 07777) == 0700
                    && fsync(gate) == 0) {
                close(gate);
                return fd;
            }
            if (fd >= 0) close(fd);
        }
    }
    if (gate >= 0) close(gate);
    if (error) *error = QStringLiteral("Sailfish privileged user storage is unavailable or unsafe");
    return -1;
}

int openStateDirectory(QString *error)
{
    const passwd *user = getpwuid(getuid());
    const QString home = user ? QString::fromLocal8Bit(user->pw_dir) : QString();
    const passwd *owner = getpwnam("privileged");
    const gid_t group = privilegedGroup();
    if (!home.isEmpty() && owner && group != gid_t(-1))
        return openPrivilegedStateDirectory(home, owner->pw_uid, group, error);
    if (error) *error = QStringLiteral("Cannot resolve Sailfish privileged user storage");
    return -1;
}

int openPreviousStateDirectory(QString *error)
{
    const int gate = openDirectory(QStringLiteral("/var/lib/harbour-flotsam"));
    if (gate < 0 && errno == ENOENT) return -1;
    struct stat st;
    if (gate >= 0 && fstat(gate, &st) == 0 && st.st_uid == 0
            && st.st_gid == privilegedGroup() && (st.st_mode & 07777) == 0770) {
        const int fd = openat(gate, QByteArray::number(getuid()).constData(),
                              O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        const int savedError = errno;
        close(gate);
        if (fd < 0 && savedError == ENOENT) { errno = ENOENT; return -1; }
        if (fd >= 0 && fstat(fd, &st) == 0 && st.st_uid == getuid()
                && (st.st_mode & 07777) == 0700) return fd;
        if (fd >= 0) close(fd);
    } else if (gate >= 0) {
        close(gate);
    }
    if (error) *error = QStringLiteral("Previous Flotsam state directory is unsafe");
    errno = EACCES;
    return -1;
}

QString userDirectory(const QString &root)
{
    return root + QLatin1Char('/') + QString::number(getuid());
}

QString helperSocket()
{
    return QStringLiteral("/run/harbour-flotsam-helper/connman.socket");
}

bool trustedDirectory(const QString &path, bool writableByGroup)
{
    // Callers supply fixed paths under root-owned /run or /var/lib, never HOME.
    struct stat st;
    return privilegedGroup() != gid_t(-1)
            && lstat(QFile::encodeName(path).constData(), &st) == 0
            && S_ISDIR(st.st_mode) && st.st_uid == 0
            && st.st_gid == privilegedGroup()
            && (st.st_mode & 07777) == (writableByGroup ? 0770 : 0750);
}

bool validHelperEndpoint()
{
    return trustedDirectory(QStringLiteral("/run/harbour-flotsam-helper"), false)
            && socketOwnedBy(helperSocket(), 0, 0660);
}

bool initializeDaemon()
{
    // A setgid executable must not load caller-selected Qt plugins/configuration.
    // Do this before constructing Qt's application or creating worker threads.
    const passwd *entry = getpwuid(getuid());
    if (!entry || getuid() == 0 || geteuid() != getuid()
            || getegid() != privilegedGroup()) return false;
    const QByteArray home(entry->pw_dir);
    const QByteArray user(entry->pw_name);
    if (user != "defaultuser" && user != "nemo") return false;
    const QByteArray runtime = "/run/user/" + QByteArray::number(getuid());
    if (clearenv() != 0) return false;
    qputenv("HOME", home);
    qputenv("USER", user);
    qputenv("LOGNAME", user);
    qputenv("PATH", "/usr/bin:/bin");
    qputenv("LANG", "C.UTF-8");
    qputenv("XDG_RUNTIME_DIR", runtime);
    qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=" + runtime + "/dbus/user_bus_socket");
    qputenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:path=/run/dbus/system_bus_socket");
    umask(0077);
    const rlimit noCore = { 0, 0 };
    return setrlimit(RLIMIT_CORE, &noCore) == 0 && prctl(PR_SET_DUMPABLE, 0) == 0;
}
}
}
