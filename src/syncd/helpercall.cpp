/* SPDX-License-Identifier: BSD-3-Clause */
#include "helpercall.h"
#include "security.h"
#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QTimer>

namespace Flotsam {
HelperCall::HelperCall(const QString &method, const QVariantList &arguments, QObject *parent)
    : QObject(parent)
{
    QTimer::singleShot(0, this, [this, method, arguments]() {
        // The public bus only wakes the root service. It carries no secrets or
        // network operations. The private socket is validated after activation.
        const QDBusMessage ping = QDBusMessage::createMethodCall(
                QStringLiteral("org.harbour.flotsam.Connman"),
                QStringLiteral("/org/harbour/flotsam/Connman"),
                QStringLiteral("org.freedesktop.DBus.Peer"), QStringLiteral("Ping"));
        auto *activation = new QDBusPendingCallWatcher(
                QDBusConnection::systemBus().asyncCall(ping, 10000), this);
        connect(activation, &QDBusPendingCallWatcher::finished, this,
                [this, method, arguments](QDBusPendingCallWatcher *watcher) {
            const QDBusMessage reply = watcher->reply();
            watcher->deleteLater();
            if (reply.type() == QDBusMessage::ErrorMessage) {
                m_reply = reply;
                emit finished(this);
                return;
            }
            if (!Security::validHelperEndpoint()) {
                m_reply = QDBusMessage::createError(
                        QStringLiteral("org.harbour.flotsam.Error.AccessDenied"),
                        QStringLiteral("The protected ConnMan endpoint is unavailable"));
                emit finished(this);
                return;
            }
            const QString name = QStringLiteral("flotsam-helper-peer");
            QDBusConnection connection(name);
            if (!connection.isConnected()) {
                QDBusConnection::disconnectFromPeer(name);
                connection = QDBusConnection::connectToPeer(
                        QStringLiteral("unix:path=") + Security::helperSocket(), name);
            }
            QDBusMessage call = QDBusMessage::createMethodCall(QString(),
                    QStringLiteral("/org/harbour/flotsam/Connman"),
                    QStringLiteral("org.harbour.flotsam.Connman"), method);
            call.setArguments(arguments);
            auto *operation = new QDBusPendingCallWatcher(connection.asyncCall(call, 30000), this);
            connect(operation, &QDBusPendingCallWatcher::finished, this,
                    [this](QDBusPendingCallWatcher *watcher) {
                m_reply = watcher->reply();
                watcher->deleteLater();
                emit finished(this);
            });
        });
    });
}

QDBusMessage HelperCall::reply() const { return m_reply; }
}
