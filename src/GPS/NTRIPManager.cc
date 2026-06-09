/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "NTRIPManager.h"

#include "Fact.h"
#include "GPSManager.h"
#include "MultiVehicleManager.h"
#include "NTRIPSettings.h"
#include "QGCLoggingCategory.h"
#include "RTCMMavlink.h"
#include "SettingsManager.h"
#include "Vehicle.h"

#include <cmath>

#include <QtCore/QDateTime>
#include <QtCore/QTime>
#include <QtNetwork/QTcpSocket>
#include <QtPositioning/QGeoCoordinate>

QGC_LOGGING_CATEGORY(NTRIPManagerLog, "qgc.gps.ntripmanager")

namespace {
constexpr int kReconnectIntervalMs = 5000;
constexpr int kHeaderMaxBytes = 16384;
constexpr int kDataRateIntervalMs = 1000;
constexpr const char *kUserAgent = "NTRIP QGroundControl";
}

NTRIPManager::NTRIPManager(QObject *parent)
    : QObject(parent)
    , _rtcmMavlink(new RTCMMavlink(this))
{
    _reconnectTimer.setSingleShot(true);
    (void) connect(&_reconnectTimer, &QTimer::timeout, this, &NTRIPManager::_connectToCaster);

    (void) connect(&_ggaTimer, &QTimer::timeout, this, &NTRIPManager::_sendGGA);

    _dataRateTimer.setInterval(kDataRateIntervalMs);
    (void) connect(&_dataRateTimer, &QTimer::timeout, this, &NTRIPManager::_updateDataRate);

    NTRIPSettings *settings = SettingsManager::instance()->ntripSettings();
    const QList<Fact*> facts = {
        settings->enabled(),
        settings->host(),
        settings->port(),
        settings->mountpoint(),
        settings->username(),
        settings->password(),
        settings->sendGGA(),
        settings->ggaInterval()
    };
    for (Fact *fact : facts) {
        (void) connect(fact, &Fact::rawValueChanged, this, &NTRIPManager::_settingsChanged);
    }

    _settingsChanged();
}

NTRIPManager::~NTRIPManager()
{
    _stop();
}

bool NTRIPManager::connected() const
{
    return _connected;
}

bool NTRIPManager::enabled() const
{
    return SettingsManager::instance()->ntripSettings()->enabled()->rawValue().toBool();
}

void NTRIPManager::reconnect()
{
    if (!enabled()) {
        _setStatusText(tr("NTRIP disabled"));
        return;
    }

    _stop(tr("Reconnecting to NTRIP caster"));
    _start();
}

void NTRIPManager::_settingsChanged()
{
    emit enabledChanged();

    if (enabled()) {
        reconnect();
    } else {
        _stop(tr("NTRIP disabled"));
    }
}

void NTRIPManager::_start()
{
    NTRIPSettings *settings = SettingsManager::instance()->ntripSettings();
    if (settings->host()->rawValue().toString().trimmed().isEmpty() ||
            settings->mountpoint()->rawValue().toString().trimmed().isEmpty()) {
        _setStatusText(tr("Enter NTRIP caster host and mountpoint"));
        return;
    }

    _bytesReceived = 0;
    _messagesReceived = 0;
    _dataRateBytesPerSecond = 0.0;
    _lastRateBytes = 0;
    _rateTimer.restart();
    emit statisticsChanged();

    _reconnectAttempt = 0;
    _connectToCaster();
}

void NTRIPManager::_stop(const QString &statusText)
{
    _reconnectTimer.stop();
    _ggaTimer.stop();
    _dataRateTimer.stop();
    _headerBuffer.clear();
    _headersComplete = false;

    if (_socket) {
        _socket->disconnect(this);
        _socket->abort();
        _socket->deleteLater();
        _socket = nullptr;
    }

    _setConnected(false);
    if (!statusText.isEmpty()) {
        _setStatusText(statusText);
    }
}

void NTRIPManager::_scheduleReconnect()
{
    if (!enabled()) {
        return;
    }

    _setConnected(false);
    ++_reconnectAttempt;
    _setStatusText(tr("NTRIP reconnecting in %1 seconds").arg(kReconnectIntervalMs / 1000));
    _reconnectTimer.start(kReconnectIntervalMs);
}

void NTRIPManager::_connectToCaster()
{
    if (!enabled()) {
        return;
    }

    NTRIPSettings *settings = SettingsManager::instance()->ntripSettings();
    const QString host = settings->host()->rawValue().toString().trimmed();
    const quint16 port = static_cast<quint16>(settings->port()->rawValue().toUInt());

    _headerBuffer.clear();
    _headersComplete = false;

    if (_socket) {
        _socket->disconnect(this);
        _socket->abort();
        _socket->deleteLater();
    }

    _socket = new QTcpSocket(this);
    (void) connect(_socket, &QTcpSocket::connected, this, &NTRIPManager::_socketConnected);
    (void) connect(_socket, &QTcpSocket::readyRead, this, &NTRIPManager::_socketReadyRead);
    (void) connect(_socket, &QTcpSocket::disconnected, this, &NTRIPManager::_socketDisconnected);
    (void) connect(_socket, &QTcpSocket::errorOccurred, this, &NTRIPManager::_socketError);

    _setStatusText(tr("Connecting to NTRIP caster %1:%2").arg(host).arg(port));
    _socket->connectToHost(host, port);
}

void NTRIPManager::_socketConnected()
{
    _sendRequest();
}

void NTRIPManager::_sendRequest()
{
    NTRIPSettings *settings = SettingsManager::instance()->ntripSettings();
    QString mountpoint = settings->mountpoint()->rawValue().toString().trimmed();
    if (!mountpoint.startsWith(QLatin1Char('/'))) {
        mountpoint.prepend(QLatin1Char('/'));
    }

    QByteArray request;
    request += QByteArrayLiteral("GET ") + mountpoint.toUtf8() + QByteArrayLiteral(" HTTP/1.0\r\n");
    request += "User-Agent: ";
    request += kUserAgent;
    request += "\r\n";
    request += QByteArrayLiteral("Host: ") + settings->host()->rawValue().toString().trimmed().toUtf8() + QByteArrayLiteral("\r\n");
    request += "Ntrip-Version: Ntrip/2.0\r\n";
    request += "Accept: */*\r\n";
    request += "Connection: close\r\n";

    const QString username = settings->username()->rawValue().toString();
    const QString password = settings->password()->rawValue().toString();
    if (!username.isEmpty() || !password.isEmpty()) {
        const QByteArray credentials = QStringLiteral("%1:%2").arg(username, password).toUtf8().toBase64();
        request += QByteArrayLiteral("Authorization: Basic ") + credentials + QByteArrayLiteral("\r\n");
    }

    request += "\r\n";
    _socket->write(request);
    _setStatusText(tr("Requesting NTRIP mountpoint %1").arg(mountpoint));
}

void NTRIPManager::_socketReadyRead()
{
    const QByteArray data = _socket->readAll();
    if (data.isEmpty()) {
        return;
    }

    _processIncomingData(data);
}

void NTRIPManager::_processIncomingData(const QByteArray &data)
{
    if (!_headersComplete) {
        _headerBuffer += data;
        if (_headerBuffer.size() > kHeaderMaxBytes) {
            _setStatusText(tr("Invalid NTRIP response"));
            _scheduleReconnect();
            return;
        }

        const int headerEnd = _headerBuffer.indexOf("\r\n\r\n");
        const int icyHeaderEnd = _headerBuffer.indexOf("ICY 200 OK");
        if (headerEnd < 0 && icyHeaderEnd != 0) {
            return;
        }

        QByteArray payload;
        QByteArray header;
        if (icyHeaderEnd == 0) {
            _headersComplete = true;
            const int icyEnd = _headerBuffer.indexOf("\r\n\r\n");
            if (icyEnd >= 0) {
                header = _headerBuffer.left(icyEnd);
                payload = _headerBuffer.mid(icyEnd + 4);
            } else {
                header = QByteArrayLiteral("ICY 200 OK");
            }
        } else {
            header = _headerBuffer.left(headerEnd);
            payload = _headerBuffer.mid(headerEnd + 4);
            _headersComplete = true;
        }

        if (!header.startsWith("ICY 200") && !header.startsWith("HTTP/1.0 200") && !header.startsWith("HTTP/1.1 200")) {
            const QString firstLine = QString::fromUtf8(header.left(header.indexOf('\n'))).trimmed();
            _setStatusText(tr("NTRIP caster rejected request: %1").arg(firstLine));
            _scheduleReconnect();
            return;
        }

        _headerBuffer.clear();
        _setConnected(true);
        _setStatusText(tr("NTRIP connected"));
        _dataRateTimer.start();
        _sendGGA();

        if (!payload.isEmpty()) {
            _processIncomingData(payload);
        }
        return;
    }

    _bytesReceived += static_cast<quint64>(data.size());
    ++_messagesReceived;
    emit statisticsChanged();
    _rtcmMavlink->RTCMDataUpdate(data);
}

void NTRIPManager::_socketError(QAbstractSocket::SocketError socketError)
{
    Q_UNUSED(socketError)
    if (!enabled()) {
        return;
    }

    const QString errorString = _socket ? _socket->errorString() : tr("Unknown socket error");
    qCWarning(NTRIPManagerLog) << "NTRIP socket error" << errorString;
    _setStatusText(tr("NTRIP error: %1").arg(errorString));
    _scheduleReconnect();
}

void NTRIPManager::_socketDisconnected()
{
    if (_connected) {
        _setStatusText(tr("NTRIP caster disconnected"));
        _scheduleReconnect();
    }
}

void NTRIPManager::_sendGGA()
{
    if (!_socket || !_connected || !SettingsManager::instance()->ntripSettings()->sendGGA()->rawValue().toBool()) {
        return;
    }

    const QByteArray gga = _buildGGA();
    if (!gga.isEmpty()) {
        _socket->write(gga);
    }

    const int intervalMs = SettingsManager::instance()->ntripSettings()->ggaInterval()->rawValue().toInt() * 1000;
    _ggaTimer.start(intervalMs);
}

void NTRIPManager::_updateDataRate()
{
    const qint64 elapsedMs = _rateTimer.restart();
    if (elapsedMs <= 0) {
        return;
    }

    _dataRateBytesPerSecond = static_cast<double>(_bytesReceived - _lastRateBytes) * 1000.0 / static_cast<double>(elapsedMs);
    _lastRateBytes = _bytesReceived;
    emit statisticsChanged();
}

QByteArray NTRIPManager::_buildGGA() const
{
    Vehicle *vehicle = MultiVehicleManager::instance()->activeVehicle();
    if (!vehicle || !vehicle->coordinate().isValid()) {
        return QByteArray();
    }

    const QGeoCoordinate coordinate = vehicle->coordinate();
    const double lat = coordinate.latitude();
    const double lon = coordinate.longitude();
    const double altitude = std::isfinite(coordinate.altitude()) ? coordinate.altitude() : 0.0;

    const int latDegrees = static_cast<int>(qAbs(lat));
    const double latMinutes = (qAbs(lat) - latDegrees) * 60.0;
    const int lonDegrees = static_cast<int>(qAbs(lon));
    const double lonMinutes = (qAbs(lon) - lonDegrees) * 60.0;

    const QTime time = QDateTime::currentDateTimeUtc().time();
    const QString sentence = QStringLiteral("GPGGA,%1,%2%3,%4,%5%6,%7,1,12,1.0,%8,M,0.0,M,,")
        .arg(time.toString(QStringLiteral("hhmmss.zzz")))
        .arg(latDegrees, 2, 10, QLatin1Char('0'))
        .arg(latMinutes, 7, 'f', 4, QLatin1Char('0'))
        .arg(lat >= 0.0 ? QLatin1Char('N') : QLatin1Char('S'))
        .arg(lonDegrees, 3, 10, QLatin1Char('0'))
        .arg(lonMinutes, 7, 'f', 4, QLatin1Char('0'))
        .arg(lon >= 0.0 ? QLatin1Char('E') : QLatin1Char('W'))
        .arg(altitude, 0, 'f', 1);

    return _nmeaChecksumSentence(sentence.toLatin1());
}

QByteArray NTRIPManager::_nmeaChecksumSentence(const QByteArray &sentenceWithoutDelimiters)
{
    quint8 checksum = 0;
    for (const char ch : sentenceWithoutDelimiters) {
        checksum ^= static_cast<quint8>(ch);
    }

    QByteArray sentence = QByteArrayLiteral("$") + sentenceWithoutDelimiters + QByteArrayLiteral("*");
    sentence += QByteArray::number(checksum, 16).rightJustified(2, '0').toUpper();
    sentence += "\r\n";
    return sentence;
}

void NTRIPManager::_setConnected(bool connected)
{
    if (_connected != connected) {
        _connected = connected;
        emit connectedChanged();
    }
}

void NTRIPManager::_setStatusText(const QString &statusText)
{
    if (_statusText != statusText) {
        _statusText = statusText;
        emit statusTextChanged();
    }
}
