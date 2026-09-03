/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "webdavclient.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

using namespace Flotsam;

class MockServer : public QTcpServer
{
    Q_OBJECT

public:
    QList<QByteArray> requests;
    QQueue<QByteArray> responses;
    bool disconnectNext = false;

protected:
    void incomingConnection(qintptr descriptor) override
    {
        QTcpSocket *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(descriptor);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            const QByteArray request = socket->readAll();
            requests.append(request);
            if (disconnectNext) {
                disconnectNext = false;
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n");
                socket->disconnectFromHost();
                return;
            }
            const QByteArray response = responses.isEmpty()
                    ? QByteArrayLiteral("HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n")
                    : responses.dequeue();
            socket->write(response);
            socket->disconnectFromHost();
        });
    }
};

class WebDavTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void createsFoldersAndFormat();
    void listsEtags();
    void conditionalPutRace();
    void malformedListing();
    void authenticationExpiry();
    void disconnectIsTransient();
    void oversizedRecordIsRejected();
    void usesAccountWebDavPathOnce();
    void formatPutMethodFailureIsNotSuccess();

private:
    MockServer server;
    WebDavClient *client = nullptr;
};

void WebDavTest::initTestCase()
{
    qRegisterMetaType<QMap<QString, QString> >();
}

void WebDavTest::init()
{
    if (server.isListening()) server.close();
    server.requests.clear();
    server.responses.clear();
    QVERIFY(server.listen(QHostAddress::LocalHost));
    delete client;
    client = new WebDavClient(this);
    WebDavCredentials credentials;
    credentials.serverAddress = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
    credentials.webDavPath = QStringLiteral("/dav");
    credentials.username = QStringLiteral("user");
    credentials.password = QStringLiteral("secret");
    client->configure(credentials);
}

void WebDavTest::createsFoldersAndFormat()
{
    QSignalSpy ready(client, &WebDavClient::layoutCreated);
    client->createLayout();
    QTRY_COMPARE(ready.count(), 1);
    QCOMPARE(server.requests.size(), 4);
    QVERIFY(server.requests.at(0).startsWith("MKCOL /dav/Sailfish%20OS "));
    QVERIFY(server.requests.at(2).contains("/NetworkSync/networks"));
    QVERIFY(server.requests.at(3).startsWith("PUT "));
    QVERIFY(server.requests.at(3).contains("If-None-Match: *"));
    QVERIFY(server.requests.at(3).contains("Authorization: Basic"));
}

void WebDavTest::listsEtags()
{
    const QByteArray id(64, 'a');
    const QByteArray xml = "<?xml version=\"1.0\"?><d:multistatus xmlns:d=\"DAV:\">"
            "<d:response><d:href>/dav/Sailfish%20OS/NetworkSync/networks/" + id
            + ".json</d:href><d:propstat><d:prop><d:getetag>\"one\"</d:getetag>"
              "</d:prop></d:propstat></d:response></d:multistatus>";
    server.responses.enqueue("HTTP/1.1 207 Multi-Status\r\nContent-Length: "
                             + QByteArray::number(xml.size()) + "\r\n\r\n" + xml);
    QSignalSpy listed(client, &WebDavClient::recordsListed);
    client->listRecords();
    QTRY_COMPARE(listed.count(), 1);
    const QMap<QString, QString> result = qvariant_cast<QMap<QString, QString> >(listed.takeFirst().at(0));
    QCOMPARE(result.value(QString::fromLatin1(id)), QStringLiteral("\"one\""));
    QVERIFY(server.requests.first().contains("Depth: 1"));
}

void WebDavTest::conditionalPutRace()
{
    server.responses.enqueue(QByteArrayLiteral(
            "HTTP/1.1 412 Precondition Failed\r\nContent-Length: 0\r\n\r\n"));
    QSignalSpy failed(client, &WebDavClient::requestFailed);
    client->putRecord(QString(64, QLatin1Char('b')), QByteArrayLiteral("{}"),
                      QStringLiteral("\"old\""), false);
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(failed.first().at(2).toInt(), 412);
    QVERIFY(failed.first().at(5).toBool());
    QVERIFY(server.requests.first().contains("If-Match: \"old\""));
}

void WebDavTest::malformedListing()
{
    const QByteArray xml = QByteArrayLiteral("<d:multistatus xmlns:d=\"DAV:\"><broken>");
    server.responses.enqueue("HTTP/1.1 207 Multi-Status\r\nContent-Length: "
                             + QByteArray::number(xml.size()) + "\r\n\r\n" + xml);
    QSignalSpy failed(client, &WebDavClient::requestFailed);
    client->listRecords();
    QTRY_COMPARE(failed.count(), 1);
    QVERIFY(!failed.first().at(4).toBool());
}

void WebDavTest::authenticationExpiry()
{
    server.responses.enqueue(QByteArrayLiteral(
            "HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n"));
    QSignalSpy failed(client, &WebDavClient::requestFailed);
    client->getFormat();
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(failed.first().at(2).toInt(), 401);
    QVERIFY(!failed.first().at(4).toBool());
}

void WebDavTest::disconnectIsTransient()
{
    server.disconnectNext = true;
    QSignalSpy failed(client, &WebDavClient::requestFailed);
    client->getFormat();
    QTRY_COMPARE(failed.count(), 1);
    QVERIFY(failed.first().at(4).toBool());
}

void WebDavTest::oversizedRecordIsRejected()
{
    const QByteArray body(70 * 1024, 'x');
    server.responses.enqueue("HTTP/1.1 200 OK\r\nContent-Length: "
                             + QByteArray::number(body.size()) + "\r\n\r\n" + body);
    QSignalSpy failed(client, &WebDavClient::requestFailed);
    client->getRecord(QString(64, QLatin1Char('c')));
    QTRY_COMPARE(failed.count(), 1);
    QVERIFY(failed.first().at(3).toString().contains(QStringLiteral("size limit")));
    QVERIFY(!failed.first().at(4).toBool());
}

void WebDavTest::usesAccountWebDavPathOnce()
{
    WebDavCredentials credentials;
    credentials.serverAddress = QStringLiteral("http://127.0.0.1:%1/nextcloud")
            .arg(server.serverPort());
    credentials.webDavPath = QStringLiteral("/nextcloud/remote.php/dav/files/user%40example.com");
    credentials.username = QStringLiteral("user");
    credentials.password = QStringLiteral("secret");
    client->configure(credentials);

    const QByteArray body = QByteArrayLiteral("{}");
    server.responses.enqueue("HTTP/1.1 200 OK\r\nContent-Length: "
                             + QByteArray::number(body.size()) + "\r\n\r\n" + body);
    QSignalSpy received(client, &WebDavClient::formatReceived);
    client->getFormat();
    QTRY_COMPARE(received.count(), 1);
    const QByteArray request = server.requests.first();
    const QByteArray suffix = QByteArrayLiteral(
            "/Sailfish%20OS/NetworkSync/format.json ");
    QVERIFY(request.startsWith(
            "GET /nextcloud/remote.php/dav/files/user@example.com" + suffix)
            || request.startsWith(
            "GET /nextcloud/remote.php/dav/files/user%40example.com" + suffix));
    QVERIFY(!request.contains("/nextcloud/nextcloud/"));
    QVERIFY(!request.contains("%2540"));
}

void WebDavTest::formatPutMethodFailureIsNotSuccess()
{
    for (int i = 0; i < 3; ++i) {
        server.responses.enqueue(QByteArrayLiteral(
                "HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n"));
    }
    server.responses.enqueue(QByteArrayLiteral(
            "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n"));
    QSignalSpy ready(client, &WebDavClient::layoutCreated);
    QSignalSpy failed(client, &WebDavClient::requestFailed);
    client->createLayout();
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(ready.count(), 0);
    QCOMPARE(failed.first().at(0).toString(), QStringLiteral("layout"));
    QCOMPARE(failed.first().at(2).toInt(), 405);
}

QTEST_MAIN(WebDavTest)

#include "tst_webdav.moc"
