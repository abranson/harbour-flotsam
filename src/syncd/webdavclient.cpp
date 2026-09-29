/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "webdavclient.h"

#include <QBuffer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSslError>
#include <QUrl>
#include <QXmlStreamReader>

namespace {

QByteArray formatDocument()
{
    QJsonObject object;
    object.insert(QStringLiteral("format"), QStringLiteral("org.harbour.flotsam.network-sync"));
    object.insert(QStringLiteral("schemaVersion"), 1);
    object.insert(QStringLiteral("minimumReaderVersion"), 1);
    return QJsonDocument(object).toJson(QJsonDocument::Indented);
}

int statusCode(QNetworkReply *reply)
{
    return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
}

QString normalizedEtag(const QByteArray &etag)
{
    return QString::fromLatin1(etag.trimmed());
}

}

namespace Flotsam {

WebDavClient::WebDavClient(QObject *parent)
    : QObject(parent)
{
    connect(&m_network, &QNetworkAccessManager::sslErrors,
            this, &WebDavClient::sslErrors);
}

void WebDavClient::configure(const WebDavCredentials &credentials)
{
    m_credentials = credentials;
}

QUrl WebDavClient::urlFor(const QString &relativePath) const
{
    QUrl url(m_credentials.serverAddress);
    QString davPath = m_credentials.webDavPath;
    if (!davPath.startsWith(QLatin1Char('/'))) {
        davPath.prepend(QLatin1Char('/'));
    }
    if (davPath.endsWith(QLatin1Char('/'))) {
        davPath.chop(1);
    }
    if (davPath.contains(QStringLiteral("%40"))) {
        davPath = QUrl::fromPercentEncoding(davPath.toUtf8());
    }
    QString child = relativePath;
    if (!child.startsWith(QLatin1Char('/'))) {
        child.prepend(QLatin1Char('/'));
    }
    // Sailfish's Nextcloud account stores webdav_path with any server-side
    // subdirectory already included.  Treat it as absolute, as the platform's
    // own NetworkRequestGenerator does, instead of duplicating url.path().
    url.setPath(davPath + child);
    url.setUserInfo(QString());
    return url;
}

QNetworkRequest WebDavClient::requestFor(const QString &relativePath) const
{
    QNetworkRequest request(urlFor(relativePath));
    request.setRawHeader("User-Agent", "harbour-flotsam/1");
    request.setRawHeader("Accept", "application/json, application/xml;q=0.9");
    // Reuse GET ETags in If-Match on PUT. Compression can give the downloaded
    // representation a different ETag from the stored WebDAV resource.
    request.setRawHeader("Accept-Encoding", "identity");
    if (!m_credentials.accessToken.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + m_credentials.accessToken.toUtf8());
    } else {
        const QByteArray basic = (m_credentials.username + QLatin1Char(':')
                                  + m_credentials.password).toUtf8().toBase64();
        request.setRawHeader("Authorization", "Basic " + basic);
    }
    return request;
}

QNetworkReply *WebDavClient::sendCustomRequest(const QNetworkRequest &request,
                                               const QByteArray &verb,
                                               const QByteArray &data)
{
    if (data.isEmpty()) {
        return m_network.sendCustomRequest(request, verb);
    }
    QBuffer *buffer = new QBuffer;
    buffer->setData(data);
    buffer->open(QIODevice::ReadOnly);
    QNetworkReply *reply = m_network.sendCustomRequest(request, verb, buffer);
    buffer->setParent(reply);
    return reply;
}

void WebDavClient::watchReply(QNetworkReply *reply, qint64 maximumBytes)
{
    reply->setReadBufferSize(maximumBytes + 1);
    connect(reply, &QNetworkReply::readyRead, this, [reply, maximumBytes]() {
        if (reply->bytesAvailable() > maximumBytes) {
            reply->setProperty("responseTooLarge", true);
            reply->abort();
        }
    });
}

void WebDavClient::createLayout()
{
    if (m_layoutStep != NoLayout) {
        return;
    }
    m_layoutStep = CreateSailfishDirectory;
    issueLayoutStep();
}

void WebDavClient::issueLayoutStep()
{
    QString path;
    QByteArray verb;
    QByteArray body;
    switch (m_layoutStep) {
    case CreateSailfishDirectory:
        path = QStringLiteral("Sailfish OS");
        verb = QByteArrayLiteral("MKCOL");
        break;
    case CreateNetworkSyncDirectory:
        path = QStringLiteral("Sailfish OS/NetworkSync");
        verb = QByteArrayLiteral("MKCOL");
        break;
    case CreateNetworksDirectory:
        path = QStringLiteral("Sailfish OS/NetworkSync/networks");
        verb = QByteArrayLiteral("MKCOL");
        break;
    case CreateFormatFile:
        path = QStringLiteral("Sailfish OS/NetworkSync/format.json");
        verb = QByteArrayLiteral("PUT");
        body = formatDocument();
        break;
    case NoLayout:
        return;
    }
    QNetworkRequest request = requestFor(path);
    if (m_layoutStep == CreateFormatFile) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setRawHeader("If-None-Match", "*");
    }
    QNetworkReply *reply = sendCustomRequest(request, verb, body);
    reply->setProperty("operation", QStringLiteral("layout"));
    watchReply(reply, 64 * 1024);
    connect(reply, &QNetworkReply::finished, this, &WebDavClient::replyFinished);
}

void WebDavClient::listRecords()
{
    QNetworkRequest request = requestFor(QStringLiteral("Sailfish OS/NetworkSync/networks"));
    request.setRawHeader("Depth", "1");
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/xml; charset=utf-8"));
    const QByteArray body = QByteArrayLiteral(
            "<?xml version=\"1.0\"?><d:propfind xmlns:d=\"DAV:\"><d:prop><d:getetag/>"
            "<d:resourcetype/></d:prop></d:propfind>");
    QNetworkReply *reply = sendCustomRequest(request, QByteArrayLiteral("PROPFIND"), body);
    reply->setProperty("operation", QStringLiteral("list"));
    watchReply(reply, 1024 * 1024);
    connect(reply, &QNetworkReply::finished, this, &WebDavClient::replyFinished);
}

void WebDavClient::getFormat()
{
    QNetworkReply *reply = m_network.get(requestFor(
            QStringLiteral("Sailfish OS/NetworkSync/format.json")));
    reply->setProperty("operation", QStringLiteral("format"));
    watchReply(reply, 64 * 1024);
    connect(reply, &QNetworkReply::finished, this, &WebDavClient::replyFinished);
}

void WebDavClient::getRecord(const QString &networkId)
{
    QNetworkReply *reply = m_network.get(requestFor(
            QStringLiteral("Sailfish OS/NetworkSync/networks/%1.json").arg(networkId)));
    reply->setProperty("operation", QStringLiteral("get"));
    reply->setProperty("networkId", networkId);
    watchReply(reply, 64 * 1024);
    connect(reply, &QNetworkReply::finished, this, &WebDavClient::replyFinished);
}

void WebDavClient::putRecord(const QString &networkId, const QByteArray &data,
                             const QString &etag, bool createOnly)
{
    QNetworkRequest request = requestFor(
            QStringLiteral("Sailfish OS/NetworkSync/networks/%1.json").arg(networkId));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (createOnly) {
        request.setRawHeader("If-None-Match", "*");
    } else if (!etag.isEmpty()) {
        request.setRawHeader("If-Match", etag.toLatin1());
    } else {
        emit requestFailed(QStringLiteral("put"), networkId, 0,
                           QStringLiteral("Refusing an unconditional update"), false, false);
        return;
    }
    QNetworkReply *reply = m_network.put(request, data);
    reply->setProperty("operation", QStringLiteral("put"));
    reply->setProperty("networkId", networkId);
    watchReply(reply, 64 * 1024);
    connect(reply, &QNetworkReply::finished, this, &WebDavClient::replyFinished);
}

void WebDavClient::fail(QNetworkReply *reply, const QString &operation,
                        const QString &networkId)
{
    const int status = statusCode(reply);
    const bool precondition = status == 412;
    const bool interruptedSuccess = reply->error() != QNetworkReply::NoError
            && status > 0 && status < 300;
    const bool transient = status == 0 || interruptedSuccess || status == 408
            || status == 429 || status >= 500;
    QString message = reply->errorString();
    if (status == 404 && operation == QLatin1String("list")) {
        message = QStringLiteral("The NetworkSync directory was deleted");
    }
    emit requestFailed(operation, networkId, status, message, transient, precondition);
}

void WebDavClient::replyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        return;
    }
    reply->deleteLater();
    const QString operation = reply->property("operation").toString();
    const QString networkId = reply->property("networkId").toString();
    const int status = statusCode(reply);

    if (reply->property("responseTooLarge").toBool()) {
        if (operation == QLatin1String("layout")) {
            m_layoutStep = NoLayout;
        }
        emit requestFailed(operation, networkId, status,
                           QStringLiteral("WebDAV response exceeds its size limit"),
                           false, false);
        return;
    }

    if (operation == QLatin1String("layout")) {
        const bool collectionExists = m_layoutStep != CreateFormatFile && status == 405;
        const bool fileExists = m_layoutStep == CreateFormatFile && status == 412;
        if (reply->error() != QNetworkReply::NoError && !collectionExists && !fileExists) {
            m_layoutStep = NoLayout;
            fail(reply, operation, networkId);
            return;
        }
        if (m_layoutStep == CreateFormatFile) {
            m_layoutStep = NoLayout;
            emit layoutCreated();
        } else {
            m_layoutStep = LayoutStep(int(m_layoutStep) + 1);
            issueLayoutStep();
        }
        return;
    }

    if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
        fail(reply, operation, networkId);
        return;
    }
    if (operation == QLatin1String("format")) {
        emit formatReceived(reply->readAll());
    } else if (operation == QLatin1String("list")) {
        QString parseError;
        const QMap<QString, QString> result = parseListing(reply->readAll(), &parseError);
        if (!parseError.isEmpty()) {
            emit requestFailed(operation, networkId, status, parseError, false, false);
        } else {
            emit recordsListed(result);
        }
    } else if (operation == QLatin1String("get")) {
        emit recordReceived(networkId, reply->readAll(),
                            normalizedEtag(reply->rawHeader("ETag")));
    } else if (operation == QLatin1String("put")) {
        emit recordWritten(networkId, normalizedEtag(reply->rawHeader("ETag")));
    }
}

QMap<QString, QString> WebDavClient::parseListing(const QByteArray &xml, QString *error)
{
    QMap<QString, QString> result;
    QXmlStreamReader reader(xml);
    QString href;
    QString etag;
    bool inResponse = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QLatin1String("response")) {
            inResponse = true;
            href.clear();
            etag.clear();
        } else if (inResponse && reader.isStartElement()
                   && reader.name() == QLatin1String("href")) {
            href = reader.readElementText();
        } else if (inResponse && reader.isStartElement()
                   && reader.name() == QLatin1String("getetag")) {
            etag = reader.readElementText().trimmed();
        } else if (reader.isEndElement() && reader.name() == QLatin1String("response")) {
            inResponse = false;
            const QString fileName = QUrl::fromPercentEncoding(href.toUtf8()).section(QLatin1Char('/'), -1);
            if (fileName.endsWith(QStringLiteral(".json"))) {
                const QString id = fileName.left(fileName.size() - 5);
                if (id.size() == 64
                        && QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(id).hasMatch()) {
                    result.insert(id, etag);
                }
            }
        }
    }
    if (reader.hasError()) {
        if (error) {
            *error = QStringLiteral("Malformed WebDAV multistatus response");
        }
        result.clear();
    }
    return result;
}

void WebDavClient::sslErrors(QNetworkReply *reply, const QList<QSslError> &errors)
{
    Q_UNUSED(errors)
    if (m_credentials.ignoreSslErrors) {
        reply->ignoreSslErrors();
    }
}

}
