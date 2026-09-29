/* SPDX-License-Identifier: BSD-3-Clause */
#include "security.h"

#include <QFile>
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

bool privateUserDirectory(const QString &path)
{
    struct stat st;
    return lstat(QFile::encodeName(path).constData(), &st) == 0
            && S_ISDIR(st.st_mode) && st.st_uid == getuid()
            && (st.st_mode & 07777) == 0700;
}

bool socketOwnedBy(const QString &path, uid_t uid, mode_t mode)
{
    struct stat st;
    return lstat(QFile::encodeName(path).constData(), &st) == 0
            && S_ISSOCK(st.st_mode) && st.st_uid == uid
            && st.st_gid == privilegedGroup() && (st.st_mode & 07777) == mode;
}
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

bool prepareUserDirectory(const QString &root, QString *error)
{
    const QByteArray path = QFile::encodeName(userDirectory(root));
    if (trustedDirectory(root, true)
            && (mkdir(path.constData(), 0700) == 0 || errno == EEXIST)
            && privateUserDirectory(QString::fromLocal8Bit(path))) {
        return true;
    }
    if (error) *error = QStringLiteral("Protected Flotsam directory is unavailable or unsafe");
    return false;
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
