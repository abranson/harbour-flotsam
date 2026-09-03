/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FLOTSAM_WEBDAVCLIENT_H
#define FLOTSAM_WEBDAVCLIENT_H

#include "webdavcredentials.h"

#include <QMap>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSslError>

class QNetworkReply;

namespace Flotsam {

class WebDavClient : public QObject
{
    Q_OBJECT

public:
    explicit WebDavClient(QObject *parent = nullptr);

    void configure(const WebDavCredentials &credentials);
    void createLayout();
    void getFormat();
    void listRecords();
    void getRecord(const QString &networkId);
    void putRecord(const QString &networkId, const QByteArray &data,
                   const QString &etag, bool createOnly);

signals:
    void layoutCreated();
    void formatReceived(const QByteArray &data);
    void recordsListed(const QMap<QString, QString> &etags);
    void recordReceived(const QString &networkId, const QByteArray &data,
                        const QString &etag);
    void recordWritten(const QString &networkId, const QString &etag);
    void requestFailed(const QString &operation, const QString &networkId,
                       int httpStatus, const QString &message,
                       bool transient, bool preconditionFailed);

private slots:
    void replyFinished();
    void sslErrors(QNetworkReply *reply, const QList<QSslError> &errors);

private:
    enum LayoutStep {
        NoLayout,
        CreateSailfishDirectory,
        CreateNetworkSyncDirectory,
        CreateNetworksDirectory,
        CreateFormatFile
    };

    QUrl urlFor(const QString &relativePath) const;
    QNetworkRequest requestFor(const QString &relativePath) const;
    QNetworkReply *sendCustomRequest(const QNetworkRequest &request,
                                     const QByteArray &verb,
                                     const QByteArray &data);
    void watchReply(QNetworkReply *reply, qint64 maximumBytes);
    void issueLayoutStep();
    void fail(QNetworkReply *reply, const QString &operation, const QString &networkId);
    static QMap<QString, QString> parseListing(const QByteArray &xml, QString *error);

    QNetworkAccessManager m_network;
    WebDavCredentials m_credentials;
    LayoutStep m_layoutStep = NoLayout;
};

}

#endif
