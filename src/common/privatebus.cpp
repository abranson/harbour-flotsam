/* SPDX-License-Identifier: BSD-3-Clause */
#include "privatebus.h"
#include <QFile>
#include <sys/stat.h>
#include <unistd.h>

namespace Flotsam {
PrivateBus::PrivateBus(QObject *parent) : QObject(parent) {}

PrivateBus::~PrivateBus()
{
    for (const QDBusConnection &connection : m_connections)
        QDBusConnection::disconnectFromPeer(connection.name());
}

bool PrivateBus::listen(const QString &socket, const QString &objectPath,
                        QObject *object, bool rootHelper, QString *error)
{
    m_lock.reset(new QLockFile(socket + QStringLiteral(".lock")));
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock()) {
        if (error) *error = QStringLiteral("Another Flotsam process owns this endpoint");
        return false;
    }
    const QByteArray path = QFile::encodeName(socket);
    struct stat st;
    // Never unlink an arbitrary file or follow a planted symlink.
    if (lstat(path.constData(), &st) == 0
            && (!S_ISSOCK(st.st_mode) || st.st_uid != geteuid()
                || unlink(path.constData()) != 0)) {
        if (error) *error = QStringLiteral("Unsafe existing socket");
        return false;
    }
    m_server.reset(new QDBusServer(QStringLiteral("unix:path=") + socket));
    // Root and the device user have different UIDs. The kernel filesystem gate
    // (root:privileged 0750 directory, 0660 socket) authorizes helper peers.
    // Anonymous D-Bus auth never implies a world-accessible/abstract socket.
    m_server->setAnonymousAuthenticationAllowed(rootHelper);
    if (!m_server->isConnected() || chmod(path.constData(), rootHelper ? 0660 : 0600) != 0) {
        m_server.reset();
        if (error) *error = QStringLiteral("Cannot create protected D-Bus socket");
        return false;
    }
    connect(m_server.get(), &QDBusServer::newConnection, this,
            [this, objectPath, object](const QDBusConnection &peer) {
        for (int i = m_connections.size() - 1; i >= 0; --i) {
            if (!m_connections.at(i).isConnected()) {
                QDBusConnection::disconnectFromPeer(m_connections.at(i).name());
                m_connections.removeAt(i);
            }
        }
        QDBusConnection connection(peer);
        if (m_connections.size() >= 16
                || !connection.registerObject(objectPath, object,
                        QDBusConnection::ExportScriptableSlots
                        | QDBusConnection::ExportScriptableSignals)) {
            QDBusConnection::disconnectFromPeer(connection.name());
            return;
        }
        m_connections.append(connection);
    });
    return true;
}
}
