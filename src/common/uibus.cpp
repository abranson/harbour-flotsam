/* SPDX-License-Identifier: BSD-3-Clause */
#include "uibus.h"
#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusError>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QFile>
#include <QCryptographicHash>
#include <QMetaMethod>
#include <grp.h>
#include <unistd.h>
#include <sys/stat.h>

namespace Flotsam {
namespace {
const char interfaceName[] = "org.harbour.flotsam.Sync";
const char objectPath[] = "/org/harbour/flotsam/Sync";

QByteArray processFile(uint pid, const QString &file)
{
    if (!pid) return QByteArray();
    QFile input(QStringLiteral("/proc/%1/%2").arg(pid).arg(file));
    if (!input.open(QIODevice::ReadOnly)) return QByteArray();
    const QByteArray data = input.read(65537);
    return data.size() <= 65536 ? data : QByteArray();
}

QVariantMap connectionCredentials(const QDBusConnection &bus, const QString &sender)
{
    if (!sender.startsWith(QLatin1Char(':')) || sender.size() > 255) return QVariantMap();
    QDBusMessage query = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"),
            QStringLiteral("/org/freedesktop/DBus"), QStringLiteral("org.freedesktop.DBus"),
            QStringLiteral("GetConnectionCredentials"));
    query << sender;
    const QDBusReply<QVariantMap> reply = bus.call(query, QDBus::Block, 1000);
    if (!reply.isValid() || !reply.value().contains(QStringLiteral("UnixUserID"))
            || !reply.value().contains(QStringLiteral("ProcessID"))) return QVariantMap();
    return {{QStringLiteral("UnixUserID"), reply.value().value(QStringLiteral("UnixUserID"))},
            {QStringLiteral("ProcessID"), reply.value().value(QStringLiteral("ProcessID"))}};
}

QByteArray processStart(const QByteArray &stat)
{
    const int end = stat.lastIndexOf(')');
    return end < 0 ? QByteArray() : stat.mid(end + 2).split(' ').value(19);
}

QByteArray executableHash(const QByteArray &path)
{
    QFile file(QString::fromLocal8Bit(path));
    if (!file.open(QIODevice::ReadOnly)) return QByteArray();
    const QByteArray bytes = file.read(16 * 1024 * 1024 + 1);
    if (bytes.isEmpty() || bytes.size() > 16 * 1024 * 1024 || !file.atEnd()) return QByteArray();
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}
}

UiBusPolicy UiBusPolicy::platform()
{
    const group *privileged = getgrnam("privileged");
    return {getuid(), privileged ? privileged->gr_gid : gid_t(-1), 0,
            QStringLiteral("/usr/bin/harbour-flotsam"),
            QStringLiteral("/usr/bin/xdg-dbus-proxy")};
}

bool processCredentials(uint pid, uid_t uid, gid_t gid)
{
    if (gid == gid_t(-1)) return false;
    const QByteArray status = processFile(pid, QStringLiteral("status"));
    bool validUid = false, validGid = false;
    for (const QByteArray &line : status.split('\n')) {
        const QList<QByteArray> fields = line.simplified().split(' ');
        if (fields.size() != 5) continue;
        bool ok = false;
        const uint effective = fields.at(2).toUInt(&ok);
        if (fields.first() == "Uid:") validUid = ok && effective == uid;
        if (fields.first() == "Gid:") validGid = ok && effective == gid;
    }
    return validUid && validGid;
}

bool processExecutable(uint pid, const UiBusPolicy &policy, const QString &path, bool allowSandboxCopy)
{
    if (!processCredentials(pid, policy.uid, policy.gid)) return false;
    const QByteArray before = processFile(pid, QStringLiteral("stat"));
    struct stat installed, running;
    const QByteArray executable = QFile::encodeName(path);
    const QByteArray proc = QByteArray("/proc/") + QByteArray::number(pid) + "/exe";
    if (processStart(before).isEmpty() || lstat(executable.constData(), &installed) != 0
            || !S_ISREG(installed.st_mode) || installed.st_uid != policy.executableOwner
            || (installed.st_mode & 0022) || stat(proc.constData(), &running) != 0)
        return false;
    bool matches = installed.st_dev == running.st_dev && installed.st_ino == running.st_ino;
    // Sailjail private-bin copies the UI into root-owned tmpfs. Only permit
    // this fallback after authenticating its proxy, never for a direct caller.
    if (!matches && allowSandboxCopy && S_ISREG(running.st_mode)
            && running.st_uid == policy.executableOwner && !(running.st_mode & 0022)
            && running.st_size == installed.st_size) {
        const QByteArray expected = executableHash(executable);
        matches = !expected.isEmpty() && expected == executableHash(proc);
    }
    return matches
            && processCredentials(pid, policy.uid, policy.gid)
            // Compare stable process identity, not the changing CPU counters.
            && processStart(before) == processStart(processFile(pid, QStringLiteral("stat")));
}

bool authorizeUi(const QDBusConnection &bus, const QString &sender, const UiBusPolicy &policy)
{
    const QVariantMap credentials = connectionCredentials(bus, sender);
    if (!credentials.contains(QStringLiteral("UnixUserID"))
            || credentials.value(QStringLiteral("UnixUserID")).toUInt() != policy.uid)
        return false;
    const uint pid = credentials.value(QStringLiteral("ProcessID")).toUInt();
    if (processExecutable(pid, policy, policy.uiExecutable)) return true;
    if (!processExecutable(pid, policy, policy.proxyExecutable)) return false;

    // Only the verified privileged platform proxy may identify a sandboxed UI.
    QDBusMessage query = QDBusMessage::createMethodCall(sender, QStringLiteral("/"),
            QStringLiteral("org.sailfishos.sailjailed"), QStringLiteral("Identify"));
    const QDBusReply<QVariantMap> reply = bus.call(query, QDBus::Block, 1000);
    if (!reply.isValid()) return false;
    const QVariantMap client = reply.value();
    const qlonglong clientPid = client.value(QStringLiteral("pid")).toLongLong();
    return client.contains(QStringLiteral("uid"))
            && client.value(QStringLiteral("uid")).toUInt() == policy.uid
            && clientPid > 0 && clientPid <= 0x7fffffff
            && processExecutable(uint(clientPid), policy, policy.uiExecutable, true)
            && processExecutable(pid, policy, policy.proxyExecutable)
            && connectionCredentials(bus, sender) == credentials;
}

DaemonIdentity::DaemonIdentity(const QDBusConnection &bus, gid_t gid)
    : m_bus(bus), m_gid(gid)
{
}

bool DaemonIdentity::VerifyDaemon(const QString &owner, uint uid)
{
    if (!calledFromDBus() || connection().name() != m_bus.name()
            || !owner.startsWith(QLatin1Char(':')) || owner.size() > 255) return false;
    QDBusMessage query = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"),
            QStringLiteral("/org/freedesktop/DBus"), QStringLiteral("org.freedesktop.DBus"),
            QStringLiteral("GetNameOwner"));
    query << QStringLiteral("org.harbour.flotsam.Sync");
    const QDBusReply<QString> current = m_bus.call(query, QDBus::Block, 1000);
    if (!current.isValid() || current.value() != owner) return false;
    const QVariantMap credentials = connectionCredentials(m_bus, owner);
    return credentials.contains(QStringLiteral("UnixUserID"))
            && credentials.value(QStringLiteral("UnixUserID")).toUInt() == uid
            && processCredentials(credentials.value(QStringLiteral("ProcessID")).toUInt(), uid, m_gid)
            && connectionCredentials(m_bus, owner) == credentials;
}

UiBus::UiBus(QObject *api, const QDBusConnection &bus, const QString &xml,
             const UiBusPolicy &policy)
    : QDBusVirtualObject(api), m_api(api), m_bus(bus), m_xml(xml), m_policy(policy)
{
    auto *watcher = new QDBusServiceWatcher(QString(), bus,
            QDBusServiceWatcher::WatchForUnregistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this,
            [this, watcher](const QString &name) {
        m_clients.remove(name);
        watcher->removeWatchedService(name);
    });
}

QString UiBus::introspect(const QString &) const { return m_xml; }

bool UiBus::handleMessage(const QDBusMessage &message, const QDBusConnection &connection)
{
    if (message.type() != QDBusMessage::MethodCallMessage) return false;
    if (connection.name() != m_bus.name()
            || !authorizeUi(m_bus, message.service(), m_policy)) {
        connection.send(message.createErrorReply(QStringLiteral("org.harbour.flotsam.Error.AccessDenied"),
                                                 QStringLiteral("Only the privileged Flotsam UI may call this API")));
        return true;
    }
    const QMetaObject *meta = m_api->metaObject();
    if (message.interface() == QLatin1String(interfaceName)) {
        for (int i = meta->methodOffset(); i < meta->methodCount(); ++i) {
            const QMetaMethod method = meta->method(i);
            if (method.methodType() != QMetaMethod::Slot || !(method.attributes() & QMetaMethod::Scriptable)
                    || method.name() != message.member().toLatin1()
                    || method.parameterCount() != message.arguments().size()
                    || method.parameterCount() > 5) continue;
            QVariantList values = message.arguments();
            QGenericArgument args[5];
            bool valid = true;
            const QList<QByteArray> types = method.parameterTypes();
            for (int j = 0; j < values.size(); ++j) {
                const int type = QMetaType::type(types.at(j));
                if (values[j].userType() == qMetaTypeId<QDBusArgument>()) {
                    if (type == QMetaType::QVariantMap)
                        values[j] = qdbus_cast<QVariantMap>(values[j]);
                    else if (type == QMetaType::QStringList)
                        values[j] = qdbus_cast<QStringList>(values[j]);
                }
                if (values[j].userType() != type) { valid = false; break; }
                args[j] = QGenericArgument(types.at(j).constData(), values[j].constData());
            }
            if (!valid) break;
            if (!m_clients.contains(message.service())) {
                if (m_clients.size() >= 16) break;
                m_clients.insert(message.service());
                findChild<QDBusServiceWatcher *>()->addWatchedService(message.service());
            }
            QVariant result(method.returnType(), nullptr);
            QGenericReturnArgument output;
            if (method.returnType() != QMetaType::Void)
                output = QGenericReturnArgument(method.typeName(), result.data());
            if (method.invoke(m_api, Qt::DirectConnection, output,
                              args[0], args[1], args[2], args[3], args[4])) {
                QVariantList reply;
                if (method.returnType() != QMetaType::Void) reply << result;
                connection.send(message.createReply(reply));
                return true;
            }
            break;
        }
    }
    connection.send(message.createErrorReply(QDBusError::InvalidArgs,
                                             QStringLiteral("Unsupported method or arguments")));
    return true;
}

void UiBus::sendSignal(const QString &member, const QVariantList &arguments)
{
    for (const QString &client : m_clients.values()) {
        if (!authorizeUi(m_bus, client, m_policy)) {
            m_clients.remove(client);
            findChild<QDBusServiceWatcher *>()->removeWatchedService(client);
            continue;
        }
        QDBusMessage signal = QDBusMessage::createTargetedSignal(client,
                QString::fromLatin1(objectPath), QString::fromLatin1(interfaceName), member);
        signal.setArguments(arguments);
        m_bus.send(signal);
    }
}
}
