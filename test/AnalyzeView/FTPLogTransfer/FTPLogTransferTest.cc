#include "FTPLogController.h"
#include <QtNetwork/QTcpServer>
#include <QtCore/QTemporaryDir>
#include <QtCore/QSettings>
#include <QtCore/QFile>
#include <QtTest/QTest>
#include <QtTest/QSignalSpy>

class FTPLogTransferTest : public QObject {
    Q_OBJECT
private slots:
    void transfer_data() {
        QTest::addColumn<bool>("legacy");
        QTest::addColumn<bool>("truncated");
        QTest::newRow("MLSD-EPSV-binary") << false << false;
        QTest::newRow("LIST-PASV-binary") << true << false;
        QTest::newRow("incomplete-download") << false << true;
    }
    void transfer() {
        QFETCH(bool, legacy);
        QFETCH(bool, truncated);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
        QTcpServer server, passive;
        QVERIFY2(server.listen(QHostAddress::LocalHost, 21), qPrintable(server.errorString()));
        QVERIFY(passive.listen(QHostAddress::LocalHost));
        QByteArray payload("ULog\0\r\n\xff", 9);
        QStringList commands;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            auto socket = server.nextPendingConnection();
            socket->setParent(&server);
            socket->write("220-Welcome\r\nThis is a multiline greeting\r\n220 Ready\r\n");
            connect(socket, &QTcpSocket::readyRead, this, [&, socket] {
                while (socket->canReadLine()) {
                    auto command = socket->readLine();
                    command.chop(2);
                    commands.append(QString::fromUtf8(command));
                    if (command == "USER anonymous") socket->write("331 Password\r\n");
                    else if (command == "PASS ") socket->write("230 Logged in\r\n");
                    else if (command == "TYPE I") socket->write("200 Binary\r\n");
                    else if (command.startsWith("CWD ")) socket->write("250 Directory changed\r\n");
                    else if (command.startsWith("SIZE ")) socket->write(legacy ? "502 Unsupported\r\n" : "213 9\r\n");
                    else if (command == "EPSV") {
                        socket->write(legacy ? QByteArray("500 Unsupported\r\n") : "229 Passive (|||" + QByteArray::number(passive.serverPort()) + "|)\r\n");
                    } else if (command == "PASV") {
                        socket->write("227 Passive (127,0,0,1," + QByteArray::number(passive.serverPort() / 256) + "," + QByteArray::number(passive.serverPort() % 256) + ")\r\n");
                    } else if (command == "MLSD" && legacy) {
                        socket->write("502 Unsupported\r\n");
                        if (auto data = passive.nextPendingConnection()) data->deleteLater();
                    } else if (command == "MLSD" || command == "LIST" || command.startsWith("RETR ")) {
                        auto data = passive.nextPendingConnection();
                        QVERIFY(data);
                        socket->write("150 Opening data\r\n");
                        if (command.startsWith("RETR ")) {
                            // Deliberately split/delay the stream to exercise visible progress.
                            QTimer::singleShot(150, data, [&, data, socket] {
                                data->write(payload.left(3));
                                QTimer::singleShot(150, data, [&, data, socket] {
                                    if (!truncated) data->write(payload.mid(3));
                                    data->disconnectFromHost();
                                    socket->write("226 Completed\r\n");
                                });
                            });
                            connect(data, &QTcpSocket::disconnected, data, &QObject::deleteLater);
                            continue;
                        }
                        else if (legacy) data->write("drwxr-xr-x 1 ftp ftp 0 Nov 23 12:00 session\r\n-rw-r--r-- 1 ftp ftp 9 Nov 23 12:00 test log.ulg\r\n-rw-r--r-- 1 ftp ftp 2 Nov 23 12:00 ignored.txt\r\n");
                        else data->write("type=cdir; .\r\ntype=dir; session\r\ntype=file;size=8; test log.ulg\r\ntype=file;size=2; ignored.txt\r\ntype=dir; ../escape\r\n");
                        data->disconnectFromHost();
                        connect(data, &QTcpSocket::disconnected, data, &QObject::deleteLater);
                        socket->write("226 Completed\r\n");
                    } else QFAIL(qPrintable("Unexpected command: " + command));
                }
            });
        });
        FTPLogController controller;
        QCOMPARE(controller.host(), QString("192.168.0.3"));
        controller.setHost("127.0.0.1");
        controller.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QCOMPARE(controller.entries().size(), 2);
        QCOMPARE(controller.entries()[0].toMap()["name"].toString(), QString("session"));
        QVERIFY(commands.contains("PASS "));
        QVERIFY(commands.contains("CWD /fs/microsd/log/"));
        controller.openDirectory(0);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QCOMPARE(controller.path(), QString("/fs/microsd/log/session/"));
        controller.up();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QCOMPARE(controller.path(), QString("/fs/microsd/log/"));
        QSignalSpy entriesChanged(&controller, &FTPLogController::entriesChanged);
        QList<qint64> progress;
        connect(&controller, &FTPLogController::progressChanged, this, [&] {
            if (controller.downloading()) progress.append(controller.received());
        });
        controller.download(1, temp.path());
        QVERIFY(controller.downloading());
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY(!controller.downloading());
        QCOMPARE(controller.totalBytes(), 9);
        QCOMPARE(entriesChanged.count(), 0);
        QVERIFY(progress.contains(3));
        QCOMPARE(controller.received(), truncated ? 3 : 9);
        QFile file(temp.filePath("test log.ulg"));
        if (truncated) {
            QVERIFY(!file.exists());
            QVERIFY(controller.status().contains("incomplete"));
        } else {
            QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(controller.status()));
            QCOMPARE(file.readAll(), payload);
            file.close();
            controller.download(1, QUrl::fromLocalFile(temp.path()).toString());
            QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
            QVERIFY(QFile::exists(temp.filePath("test log (1).ulg")));
        }
        controller.refresh();
        controller.cancel();
        QVERIFY(!controller.busy());
        QVERIFY(controller.status().contains("cancelled"));
        FTPLogController remembered;
        QCOMPARE(remembered.host(), QString("127.0.0.1"));
        QSettings().clear();
    }
};
QTEST_GUILESS_MAIN(FTPLogTransferTest)
#include "FTPLogTransferTest.moc"
