#pragma once

#include <QtCore/QObject>
#include <QtCore/QElapsedTimer>
#include <QtCore/QSaveFile>
#include <QtCore/QTimer>
#include <QtCore/QVariantList>
#include <QtNetwork/QTcpSocket>
#include <QtQmlIntegration/QtQmlIntegration>
#include <memory>

// Plain FTP over TCP; independent of the vehicle's MAVLink FTP service.
class FTPLogController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString host READ host WRITE setHost NOTIFY changed)
    Q_PROPERTY(QString path READ path NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(qint64 received READ received NOTIFY progressChanged)
    Q_PROPERTY(qint64 totalBytes READ totalBytes NOTIFY progressChanged)
    Q_PROPERTY(bool downloading READ downloading NOTIFY changed)
public:
    explicit FTPLogController(QObject *parent = nullptr);
    ~FTPLogController() override;
    QString host() const { return _host; }
    void setHost(const QString &host);
    QString path() const { return _path; }
    QString status() const { return _status; }
    bool busy() const { return _busy; }
    QVariantList entries() const { return _entries; }
    qint64 received() const { return _received; }
    qint64 totalBytes() const { return _expectedSize; }
    bool downloading() const { return _busy && _download; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void openDirectory(int index);
    Q_INVOKABLE void up();
    Q_INVOKABLE void download(int index, const QString &folder);
    Q_INVOKABLE void cancel();
signals:
    void changed();
    void progressChanged();
    void entriesChanged();
private:
    enum class State { Greeting, User, Password, Type, Directory, Size, ExtendedPassive, Passive, Transfer };
    void start();
    void command(const QByteArray &command, State state);
    void readControl();
    void reply(int code, const QByteArray &line);
    void connectData(quint16 port);
    void readData();
    void finishTransfer();
    bool parseListing();
    void finish(const QString &status);
    static bool safeName(const QString &name);

    QTcpSocket _control;
    QTcpSocket _data;
    QTimer _timeout;
    QElapsedTimer _progressClock;
    State _state = State::Greeting;
    QString _host;
    QString _path = QStringLiteral("/fs/microsd/log/");
    QString _status;
    QString _phase;
    QVariantList _entries;
    QByteArray _controlBuffer, _listing;
    int _multilineCode = 0;
    bool _busy = false, _download = false, _useMlsd = true;
    bool _dataDone = false, _transferDone = false;
    qint64 _received = 0, _expectedSize = -1;
    QString _remoteName, _localPath;
    std::unique_ptr<QSaveFile> _file;
};
