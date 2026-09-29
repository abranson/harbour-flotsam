/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FLOTSAM_HELPERCALL_H
#define FLOTSAM_HELPERCALL_H
#include <QDBusMessage>
#include <QObject>

namespace Flotsam {
class HelperCall : public QObject
{
    Q_OBJECT
public:
    HelperCall(const QString &method, const QVariantList &arguments, QObject *parent);
    QDBusMessage reply() const;
signals:
    void finished(Flotsam::HelperCall *call);
private:
    QDBusMessage m_reply;
};
}
#endif
