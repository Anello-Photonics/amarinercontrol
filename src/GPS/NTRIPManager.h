/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtNetwork/QAbstractSocket>

class Fact;
class QTcpSocket;
class RTCMMavlink;

Q_DECLARE_LOGGING_CATEGORY(NTRIPManagerLog)

class NTRIPManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(quint64 bytesReceived READ bytesReceived NOTIFY statisticsChanged)
    Q_PROPERTY(quint32 messagesReceived READ messagesReceived NOTIFY statisticsChanged)
    Q_PROPERTY(double dataRateBytesPerSecond READ dataRateBytesPerSecond NOTIFY statisticsChanged)

public:
    explicit NTRIPManager(QObject *parent = nullptr);
    ~NTRIPManager() override;

    bool connected() const;
    bool enabled() const;
    bool paused() const { return _paused; }
    QString statusText() const { return _statusText; }
    quint64 bytesReceived() const { return _bytesReceived; }
    quint32 messagesReceived() const { return _messagesReceived; }
    double dataRateBytesPerSecond() const { return _dataRateBytesPerSecond; }

    Q_INVOKABLE void reconnect();
    void setPaused(bool paused);

signals:
    void connectedChanged();
    void enabledChanged();
    void pausedChanged();
    void statusTextChanged();
    void statisticsChanged();

private slots:
    void _settingsChanged();
    void _socketConnected();
    void _socketReadyRead();
    void _socketError(QAbstractSocket::SocketError socketError);
    void _socketDisconnected();
    void _sendGGA();
    void _updateDataRate();

private:
    void _start();
    void _stop(const QString &statusText = QString());
    void _scheduleReconnect();
    void _connectToCaster();
    void _sendRequest();
    void _processIncomingData(const QByteArray &data);
    void _setConnected(bool connected);
    void _setStatusText(const QString &statusText);
    QByteArray _buildGGA() const;
    static QByteArray _nmeaChecksumSentence(const QByteArray &sentenceWithoutDelimiters);

    QTcpSocket *_socket = nullptr;
    RTCMMavlink *_rtcmMavlink = nullptr;
    QTimer _reconnectTimer;
    QTimer _ggaTimer;
    QTimer _dataRateTimer;
    QByteArray _headerBuffer;
    bool _headersComplete = false;
    bool _connected = false;
    bool _paused = false;
    QString _statusText;
    int _reconnectAttempt = 0;
    quint64 _bytesReceived = 0;
    quint32 _messagesReceived = 0;
    double _dataRateBytesPerSecond = 0.0;
    quint64 _lastRateBytes = 0;
    QElapsedTimer _rateTimer;
};
