/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FLOTSAM_UIBUS_H
#define FLOTSAM_UIBUS_H

#include <QDBusVirtualObject>
#include <QDBusConnection>
#include <QDBusContext>
#include <QSet>
#include <sys/types.h>

namespace Flotsam {
struct UiBusPolicy {
    uid_t uid;
    gid_t gid;
    uid_t executableOwner;
    QString uiExecutable;
    QString proxyExecutable;
    static UiBusPolicy platform();
};

// Reads kernel process credentials, like ConnMan's libdbusaccess. An executable
// check is additional application access control, not protection from root or
// code already executing inside a trusted privileged process.
bool processCredentials(uint pid, uid_t uid, gid_t gid);
bool processExecutable(uint pid, const UiBusPolicy &policy, const QString &path,
                       bool allowSandboxCopy = false);
bool authorizeUi(const QDBusConnection &bus, const QString &sender,
                 const UiBusPolicy &policy);

// Public, read-only attestation on the helper's root-only system-bus name.
// The sandboxed UI cannot inspect host PIDs in its own /proc namespace.
class DaemonIdentity : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.harbour.flotsam.Identity1")
public:
    explicit DaemonIdentity(const QDBusConnection &bus,
                            gid_t gid = UiBusPolicy::platform().gid);
public slots:
    Q_SCRIPTABLE bool VerifyDaemon(const QString &owner, uint uid);
private:
    QDBusConnection m_bus;
    gid_t m_gid;
};

class UiBus : public QDBusVirtualObject
{
    Q_OBJECT
public:
    UiBus(QObject *api, const QDBusConnection &bus, const QString &xml,
          const UiBusPolicy &policy = UiBusPolicy::platform());
    QString introspect(const QString &path) const override;
    bool handleMessage(const QDBusMessage &message,
                       const QDBusConnection &connection) override;
    void sendSignal(const QString &member, const QVariantList &arguments = QVariantList());
private:
    QObject *m_api;
    QDBusConnection m_bus;
    QString m_xml;
    UiBusPolicy m_policy;
    QSet<QString> m_clients;
};
}
#endif
