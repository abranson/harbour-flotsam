/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FLOTSAM_ATOMICSTATE_H
#define FLOTSAM_ATOMICSTATE_H

#include <QJsonObject>
#include <QString>

namespace Flotsam {

class AtomicState
{
public:
    explicit AtomicState(const QString &path = QString());

    QString path() const;
    QJsonObject object() const;
    void setObject(const QJsonObject &object);
    bool load(QString *error = nullptr);
    bool save(QString *error = nullptr) const;

    static QString defaultPath();
    static QJsonObject initialObject();

private:
    QString m_path;
    QJsonObject m_object;
};

}

#endif
