/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FLOTSAM_PRIVATEBUS_H
#define FLOTSAM_PRIVATEBUS_H

#include <QDBusConnection>
#include <QDBusServer>
#include <QList>
#include <QLockFile>
#include <memory>

namespace Flotsam {
// The containing directory is the authorization boundary. It must already be
// validated and inaccessible to ordinary applications before constructing this.
class PrivateBus : public QObject
{
    Q_OBJECT
public:
    explicit PrivateBus(QObject *parent = nullptr);
    ~PrivateBus();
    bool listen(const QString &socket, const QString &objectPath, QObject *object,
                bool rootHelper, QString *error);
private:
    QList<QDBusConnection> m_connections;
    std::unique_ptr<QLockFile> m_lock;
    std::unique_ptr<QDBusServer> m_server;
};
}
#endif
