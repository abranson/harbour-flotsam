/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FLOTSAM_WEBDAVCREDENTIALS_H
#define FLOTSAM_WEBDAVCREDENTIALS_H

#include <QMetaType>
#include <QString>

namespace Flotsam {

struct WebDavCredentials {
    int accountId = 0;
    QString serviceName;
    QString serverAddress;
    QString webDavPath;
    QString username;
    QString password;
    QString accessToken;
    bool ignoreSslErrors = false;
};

}

Q_DECLARE_METATYPE(Flotsam::WebDavCredentials)

#endif
