/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "AnelloFirmwareUpgradeWorker.h"

#include "MAVLinkLib.h"
#include "QGC.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonValue>
#include <QtCore/QThread>
#include <QtCore/QtEndian>

namespace {

class CancelledError final : public std::exception
{
public:
    const char* what() const noexcept override { return "Firmware upgrade cancelled"; }
};

constexpr mavlink_channel_t kUpgradeMavlinkChannel = MAVLINK_COMM_15;
constexpr int kDefaultReadTimeoutMsecs = 2000;
constexpr qreal kMaxFlashProgramTimeSeconds = 0.001;

QByteArray byteArrayFromByte(quint8 byte)
{
    return QByteArray(1, static_cast<char>(byte));
}

} // namespace

AnelloFirmwareUpgradeWorker::AnelloFirmwareUpgradeWorker(const QString& portName,
                                                         qint32 bootloaderBaud,
                                                         qint32 flightstackBaud,
                                                         const QString& firmwarePath,
                                                         QObject* parent)
    : QObject(parent)
    , _portName(portName)
    , _bootloaderBaud(bootloaderBaud)
    , _flightstackBaud(flightstackBaud)
    , _firmwarePath(firmwarePath)
{
}

void AnelloFirmwareUpgradeWorker::cancel()
{
    _cancelled.store(true);
}

void AnelloFirmwareUpgradeWorker::run()
{
    bool success = false;
    int exitCode = 1;
    QString message;

    _elapsedTimer.start();

    try {
        emit output(tr("[INFO] Started native ANELLO firmware upgrade\n"));
        emit output(tr("[INFO] Port: %1, flightstack baud: %2, bootloader baud: %3\n")
                        .arg(_portName)
                        .arg(_flightstackBaud)
                        .arg(_bootloaderBaud));

        const FirmwareImage firmware = _loadFirmware();
        _enterBootloader();
        _waitForBootloader();
        _uploadFirmware(firmware);

        success = true;
        exitCode = 0;
        message = tr("Firmware upgrade completed.");
    } catch (const CancelledError&) {
        message = tr("Firmware upgrade cancelled.");
        emit output(tr("\n[INFO] %1\n").arg(message));
        exitCode = 1;
    } catch (const std::exception& error) {
        message = QString::fromUtf8(error.what());
        emit output(tr("\n[ERROR] %1\n").arg(message));
        exitCode = _failureExitCode;
    }

    if (_bootloaderPort) {
        if (_bootloaderPort->isOpen()) {
            _bootloaderPort->close();
        }
        delete _bootloaderPort;
        _bootloaderPort = nullptr;
    }

    emit finished(success, exitCode, message);
}

void AnelloFirmwareUpgradeWorker::_checkCancelled() const
{
    if (_cancelled.load()) {
        throw CancelledError();
    }
}

void AnelloFirmwareUpgradeWorker::_fail(const QString& message, int exitCode)
{
    _failureExitCode = exitCode;
    throw std::runtime_error(message.toUtf8().constData());
}

AnelloFirmwareUpgradeWorker::FirmwareImage AnelloFirmwareUpgradeWorker::_loadFirmware() const
{
    QFile firmwareFile(_firmwarePath);
    if (!firmwareFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        throw std::runtime_error(tr("Unable to open firmware file %1: %2")
                                     .arg(_firmwarePath, firmwareFile.errorString())
                                     .toUtf8()
                                     .constData());
    }

    const QByteArray jsonBytes = firmwareFile.readAll();
    firmwareFile.close();

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonBytes, &parseError);
    if (document.isNull() || !document.isObject()) {
        throw std::runtime_error(tr("Supplied firmware file is not valid JSON: %1")
                                     .arg(parseError.errorString())
                                     .toUtf8()
                                     .constData());
    }

    const QJsonObject jsonObject = document.object();
    const QStringList requiredKeys = {
        QStringLiteral("board_id"),
        QStringLiteral("board_revision"),
        QStringLiteral("image"),
        QStringLiteral("image_size"),
        QStringLiteral("image_maxsize"),
    };

    for (const QString& key : requiredKeys) {
        if (!jsonObject.contains(key)) {
            throw std::runtime_error(tr("Firmware file missing required key: %1")
                                         .arg(key)
                                         .toUtf8()
                                         .constData());
        }
    }

    FirmwareImage firmware;
    firmware.description = jsonObject;
    firmware.boardId = static_cast<quint32>(jsonObject.value(QStringLiteral("board_id")).toInt());
    firmware.boardRevision = static_cast<quint32>(jsonObject.value(QStringLiteral("board_revision")).toInt());
    firmware.imageSize = static_cast<quint32>(jsonObject.value(QStringLiteral("image_size")).toInt());
    firmware.imageMaxSize = static_cast<quint32>(jsonObject.value(QStringLiteral("image_maxsize")).toInt());
    firmware.image = _inflateJsonImage(jsonBytes, jsonObject);

    while ((firmware.image.size() % 4) != 0) {
        firmware.image.append(static_cast<char>(0xFF));
    }

    return firmware;
}

QByteArray AnelloFirmwareUpgradeWorker::_extractJsonStringValue(const QByteArray& jsonBytes, const QByteArray& key)
{
    const QByteArray needle = QByteArrayLiteral("\"") + key + QByteArrayLiteral("\"");
    const int keyIndex = jsonBytes.indexOf(needle);
    if (keyIndex < 0) {
        return {};
    }

    const int colonIndex = jsonBytes.indexOf(':', keyIndex + needle.size());
    if (colonIndex < 0) {
        return {};
    }

    const int quoteIndex = jsonBytes.indexOf('"', colonIndex + 1);
    if (quoteIndex < 0) {
        return {};
    }

    int endIndex = quoteIndex + 1;
    bool escaped = false;
    while (endIndex < jsonBytes.size()) {
        const char character = jsonBytes.at(endIndex);
        if (character == '"' && !escaped) {
            return jsonBytes.mid(quoteIndex + 1, endIndex - quoteIndex - 1);
        }
        escaped = (character == '\\' && !escaped);
        if (character != '\\') {
            escaped = false;
        }
        endIndex++;
    }

    return {};
}

QByteArray AnelloFirmwareUpgradeWorker::_inflateJsonImage(const QByteArray& jsonBytes, const QJsonObject& jsonObject)
{
    const int decompressedSize = jsonObject.value(QStringLiteral("image_size")).toInt();
    if (decompressedSize <= 0) {
        throw std::runtime_error("Firmware image_size is invalid");
    }

    const QByteArray base64Image = _extractJsonStringValue(jsonBytes, QByteArrayLiteral("image"));
    if (base64Image.isEmpty()) {
        throw std::runtime_error("Unable to locate compressed firmware image data");
    }

    QByteArray compressed;
    compressed.append(static_cast<char>((decompressedSize >> 24) & 0xFF));
    compressed.append(static_cast<char>((decompressedSize >> 16) & 0xFF));
    compressed.append(static_cast<char>((decompressedSize >> 8) & 0xFF));
    compressed.append(static_cast<char>(decompressedSize & 0xFF));
    compressed.append(QByteArray::fromBase64(base64Image));

    const QByteArray image = qUncompress(compressed);
    if (image.isEmpty()) {
        throw std::runtime_error("Firmware image decompression failed");
    }
    if (image.size() != decompressedSize) {
        throw std::runtime_error(QStringLiteral("Firmware image size mismatch: expected %1 bytes, got %2 bytes")
                                     .arg(decompressedSize)
                                     .arg(image.size())
                                     .toUtf8()
                                     .constData());
    }

    return image;
}

quint32 AnelloFirmwareUpgradeWorker::_firmwareCrc(const FirmwareImage& firmware, quint32 paddedFlashSize)
{
    quint32 state = QGC::crc32(reinterpret_cast<const quint8*>(firmware.image.constData()),
                               static_cast<unsigned>(firmware.image.size()),
                               0);

    const quint8 padding[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    quint32 bytesAccountedFor = static_cast<quint32>(firmware.image.size());
    while (bytesAccountedFor < paddedFlashSize) {
        state = QGC::crc32(padding, sizeof(padding), state);
        bytesAccountedFor += sizeof(padding);
    }

    return state;
}

void AnelloFirmwareUpgradeWorker::_enterBootloader()
{
    _checkCancelled();
    emit output(tr("Connecting to RS-232-1 MAVLink...\n"));

    QSerialPort mavlinkSerial;
    QString errorString;
    if (!_openSerial(mavlinkSerial, _portName, _flightstackBaud, &errorString)) {
        emit output(tr("Unable to open MAVLink serial connection: %1\n").arg(errorString));
        _sendNmeaBootloaderReboot();
        return;
    }

    _sendMavlinkHeartbeat(mavlinkSerial);
    emit output(tr("Waiting for heartbeat...\n"));

    const bool heartbeatReceived = _waitForMavlinkHeartbeat(mavlinkSerial, 5000);
    if (heartbeatReceived) {
        emit output(tr("Heartbeat received!\n"));
        _sendMavlinkSerialControl(mavlinkSerial, QByteArrayLiteral("reboot -b\n"),
                                  SERIAL_CONTROL_FLAG_EXCLUSIVE | SERIAL_CONTROL_FLAG_RESPOND);
        _sleepMs(500);
        _sendMavlinkSerialControl(mavlinkSerial, QByteArray(), 0);
        mavlinkSerial.close();
        emit output(tr("Waiting for bootloader...\n"));
        return;
    }

    emit output(tr("No heartbeat from MAVLink, trying NMEA0183...\n"));
    mavlinkSerial.close();
    _sleepMs(500);
    _sendNmeaBootloaderReboot();
}

bool AnelloFirmwareUpgradeWorker::_openSerial(QSerialPort& serial, const QString& portName, qint32 baudRate, QString* errorString)
{
    serial.setPortName(portName);
    serial.setBaudRate(baudRate);
    serial.setDataBits(QSerialPort::Data8);
    serial.setParity(QSerialPort::NoParity);
    serial.setStopBits(QSerialPort::OneStop);
    serial.setFlowControl(QSerialPort::NoFlowControl);

    if (!serial.open(QIODevice::ReadWrite)) {
        if (errorString) {
            *errorString = serial.errorString();
        }
        return false;
    }

    return true;
}

bool AnelloFirmwareUpgradeWorker::_waitForMavlinkHeartbeat(QSerialPort& serial, int timeoutMsecs)
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMsecs) {
        _checkCancelled();

        if (!serial.waitForReadyRead(50)) {
            continue;
        }

        const QByteArray bytes = serial.readAll();
        for (const char byte : bytes) {
            mavlink_message_t message{};
            mavlink_status_t status{};
            if (mavlink_parse_char(kUpgradeMavlinkChannel,
                                   static_cast<uint8_t>(byte),
                                   &message,
                                   &status) == MAVLINK_FRAMING_OK &&
                message.msgid == MAVLINK_MSG_ID_HEARTBEAT) {
                return true;
            }
        }
    }

    return false;
}

void AnelloFirmwareUpgradeWorker::_sendMavlinkHeartbeat(QSerialPort& serial)
{
    mavlink_message_t message{};
    mavlink_msg_heartbeat_pack_chan(kMavlinkSystemId,
                                    kMavlinkComponentId,
                                    kUpgradeMavlinkChannel,
                                    &message,
                                    MAV_TYPE_GENERIC,
                                    MAV_AUTOPILOT_INVALID,
                                    0,
                                    0,
                                    0);

    uint8_t buffer[MAVLINK_MAX_PACKET_LEN]{};
    const uint16_t length = mavlink_msg_to_send_buffer(buffer, &message);
    serial.write(reinterpret_cast<const char*>(buffer), length);
    serial.waitForBytesWritten(1000);
}

void AnelloFirmwareUpgradeWorker::_sendMavlinkSerialControl(QSerialPort& serial, const QByteArray& data, quint8 flags)
{
    int offset = 0;
    do {
        const int bytesToSend = std::min(70, static_cast<int>(data.size() - offset));
        uint8_t serialData[70]{};
        if (bytesToSend > 0) {
            memcpy(serialData, data.constData() + offset, static_cast<size_t>(bytesToSend));
        }

        mavlink_message_t message{};
        mavlink_msg_serial_control_pack_chan(kMavlinkSystemId,
                                             kMavlinkComponentId,
                                             kUpgradeMavlinkChannel,
                                             &message,
                                             kMavlinkSerialDevice,
                                             flags,
                                             0,
                                             0,
                                             static_cast<uint8_t>(bytesToSend),
                                             serialData,
                                             0,
                                             0);

        uint8_t buffer[MAVLINK_MAX_PACKET_LEN]{};
        const uint16_t length = mavlink_msg_to_send_buffer(buffer, &message);
        serial.write(reinterpret_cast<const char*>(buffer), length);
        serial.waitForBytesWritten(1000);

        offset += bytesToSend;
    } while (offset < data.size());
}

void AnelloFirmwareUpgradeWorker::_sendNmeaBootloaderReboot()
{
    _checkCancelled();
    emit output(tr("Connecting to RS-232-1 NMEA0183...\n"));

    QSerialPort nmeaSerial;
    QString errorString;
    if (!_openSerial(nmeaSerial, _portName, _flightstackBaud, &errorString)) {
        _fail(tr("Unable to open NMEA0183 serial connection: %1").arg(errorString));
    }

    nmeaSerial.write(QByteArrayLiteral("$APRBL,1*50"));
    nmeaSerial.waitForBytesWritten(1000);
    _sleepMs(500);
    nmeaSerial.close();
    emit output(tr("Waiting for bootloader...\n"));
}

void AnelloFirmwareUpgradeWorker::_waitForBootloader()
{
    QElapsedTimer timer;
    timer.start();
    bool statusPrinted = false;

    while (timer.elapsed() < kBootloaderWaitMsecs) {
        _checkCancelled();

        try {
            _openBootloaderPort();
            _identifyBootloader();
            emit output(tr("\nFound board id: %1,%2 bootloader version: %3 on %4\n")
                            .arg(_boardType)
                            .arg(_boardRevision)
                            .arg(_bootloaderRevision)
                            .arg(_portName));
            return;
        } catch (const CancelledError&) {
            throw;
        } catch (const std::exception&) {
            if (_bootloaderPort) {
                _bootloaderPort->close();
                delete _bootloaderPort;
                _bootloaderPort = nullptr;
            }

            if (!statusPrinted && timer.elapsed() > 2000) {
                emit output(tr("[INFO] Still waiting for bootloader...\n"));
                statusPrinted = true;
            }

            _sleepMs(300);
        }
    }

    _fail(tr("Bootloader not found on %1 after %2 seconds.")
              .arg(_portName)
              .arg(kBootloaderWaitMsecs / 1000));
}

void AnelloFirmwareUpgradeWorker::_openBootloaderPort()
{
    if (_bootloaderPort) {
        if (_bootloaderPort->isOpen()) {
            return;
        }
        delete _bootloaderPort;
        _bootloaderPort = nullptr;
    }

    _bootloaderPort = new QSerialPort();
    QString errorString;
    if (!_openSerial(*_bootloaderPort, _portName, _bootloaderBaud, &errorString)) {
        delete _bootloaderPort;
        _bootloaderPort = nullptr;
        _fail(tr("Open failed on port %1: %2").arg(_portName, errorString));
    }
}

void AnelloFirmwareUpgradeWorker::_identifyBootloader()
{
    _determineInterface();
    _sync();

    _bootloaderRevision = _getInfo(kInfoBootloaderRevision);
    if (_bootloaderRevision < kBootloaderRevisionMin || _bootloaderRevision > kBootloaderRevisionMax) {
        _fail(tr("Bootloader protocol mismatch: %1").arg(_bootloaderRevision));
    }

    _boardType = _getInfo(kInfoBoardId);
    _boardRevision = _getInfo(kInfoBoardRevision);
    _flashSize = _getInfo(kInfoFlashSize);
}

void AnelloFirmwareUpgradeWorker::_uploadFirmware(const FirmwareImage& firmware)
{
    const qreal percent = firmware.imageMaxSize == 0
                              ? 0.0
                              : (static_cast<qreal>(firmware.imageSize) / static_cast<qreal>(firmware.imageMaxSize)) * 100.0;

    emit output(tr("Loaded firmware for board id: %1,%2 size: %3 bytes (%4%)\n\n")
                    .arg(firmware.boardId)
                    .arg(firmware.boardRevision)
                    .arg(firmware.imageSize)
                    .arg(QString::number(percent, 'f', 2)));

    if (_boardType != firmware.boardId) {
        _fail(tr("Firmware not suitable for this board (Firmware board_type=%1 board_id=%2)")
                  .arg(_boardType)
                  .arg(firmware.boardId),
              2);
    }

    if (_flashSize < firmware.imageSize) {
        _fail(tr("Firmware image is too large for this board"));
    }

    _readOptionalBoardInfo(firmware);
    _erase();
    _program(firmware);
    _verify(firmware);

    emit output(tr("\nRebooting."));
    _reboot();

    if (_bootloaderPort) {
        _bootloaderPort->close();
    }

    emit output(tr(" Elapsed Time %1\n").arg(QString::number(_elapsedTimer.elapsed() / 1000.0, 'f', 3)));
}

void AnelloFirmwareUpgradeWorker::_readOptionalBoardInfo(const FirmwareImage& firmware)
{
    if (_bootloaderRevision >= 4) {
        try {
            emit output(tr("chip: %1\n").arg(QStringLiteral("%1").arg(_getChip(), 8, 16, QLatin1Char('0'))));
        } catch (const std::exception&) {
            // The Python uploader ignores bad optional identity reads; keep the same tolerance.
        }
    }

    if (_bootloaderRevision >= 5) {
        const QStringList description = _getChipDescription();
        if (description.size() == 2) {
            emit output(tr("family: %1\n").arg(description.at(0)));
            emit output(tr("revision: %1\n").arg(description.at(1)));
            emit output(tr("flash: %1 bytes\n").arg(_flashSize));

            if (_flashSize > firmware.imageMaxSize) {
                _fail(tr("Board can accept larger flash images (%1 bytes) than board config (%2 bytes). Please use the correct board configuration to avoid lacking critical functionality.")
                          .arg(_flashSize)
                          .arg(firmware.imageMaxSize));
            }
        }
    } else if (firmware.boardId == 9 && firmware.imageSize > 1032192) {
        _fail(tr("The board uses bootloader revision %1 and cannot determine whether flashing more than 1 MB is safe.")
                  .arg(_bootloaderRevision));
    }
}

void AnelloFirmwareUpgradeWorker::_erase()
{
    emit output(tr("Windowed mode: %1\n").arg(_ackWindowedMode ? QStringLiteral("True") : QStringLiteral("False")));
    emit output(QStringLiteral("\n"));

    _writeByte(kChipErase);
    _writeByte(kEoc);

    QElapsedTimer timer;
    timer.start();

    constexpr qreal usualEraseDurationSeconds = 15.0;
    constexpr int eraseTimeoutMsecs = 30000;

    while (timer.elapsed() < eraseTimeoutMsecs) {
        _checkCancelled();

        const qreal elapsedSeconds = timer.elapsed() / 1000.0;
        const qreal remainingSeconds = (eraseTimeoutMsecs - timer.elapsed()) / 1000.0;
        if (remainingSeconds >= usualEraseDurationSeconds) {
            _drawProgress(QStringLiteral("Erase  "), elapsedSeconds, usualEraseDurationSeconds);
        } else {
            _drawProgress(QStringLiteral("Erase  "), 10.0, 10.0);
            emit output(tr(" (timeout: %1 seconds) ").arg(static_cast<int>(remainingSeconds)));
        }

        if (_trySync()) {
            _drawProgress(QStringLiteral("Erase  "), 10.0, 10.0);
            emit output(QStringLiteral("\n"));
            return;
        }
    }

    _fail(tr("timed out waiting for erase"));
}

void AnelloFirmwareUpgradeWorker::_program(const FirmwareImage& firmware)
{
    emit output(QStringLiteral("\n"));

    const int groupCount = static_cast<int>(std::ceil(static_cast<qreal>(firmware.image.size()) / kProgMultiMax));
    _drawProgress(QStringLiteral("Program"), 0, groupCount);

    int uploadedGroups = 0;
    for (int offset = 0; offset < firmware.image.size(); offset += kProgMultiMax) {
        _checkCancelled();

        const QByteArray chunk = firmware.image.mid(offset, kProgMultiMax);
        _programMulti(chunk, _ackWindowedMode);
        if (_ackWindowedMode) {
            _ackWindowBytes += 2;
        }

        uploadedGroups++;
        if (uploadedGroups % 256 == 0) {
            _ackSyncWindow(_ackWindowBytes);
            _ackWindowBytes = 0;
            _drawProgress(QStringLiteral("Program"), uploadedGroups, groupCount);
        }
    }

    _ackSyncWindow(_ackWindowBytes);
    _ackWindowBytes = 0;
    _drawProgress(QStringLiteral("Program"), 100, 100);
    emit output(QStringLiteral("\n"));
}

void AnelloFirmwareUpgradeWorker::_verify(const FirmwareImage& firmware)
{
    if (_bootloaderRevision == 2) {
        _verifyV2(firmware);
    } else {
        _verifyV3(firmware);
    }
}

void AnelloFirmwareUpgradeWorker::_verifyV2(const FirmwareImage& firmware)
{
    emit output(QStringLiteral("\n"));
    _writeByte(kChipVerify);
    _writeByte(kEoc);
    _getSync();

    const int groupCount = static_cast<int>(std::ceil(static_cast<qreal>(firmware.image.size()) / kReadMultiMax));
    int verifiedGroups = 0;
    for (int offset = 0; offset < firmware.image.size(); offset += kReadMultiMax) {
        _checkCancelled();

        const QByteArray chunk = firmware.image.mid(offset, kReadMultiMax);
        verifiedGroups++;
        if (verifiedGroups % 256 == 0) {
            _drawProgress(QStringLiteral("Verify "), verifiedGroups, groupCount);
        }
        if (!_verifyMulti(chunk)) {
            _fail(tr("Verification failed"));
        }
    }

    _drawProgress(QStringLiteral("Verify "), 100, 100);
    emit output(QStringLiteral("\n"));
}

void AnelloFirmwareUpgradeWorker::_verifyV3(const FirmwareImage& firmware)
{
    emit output(QStringLiteral("\n"));
    _drawProgress(QStringLiteral("Verify "), 1, 100);

    const quint32 expectedCrc = _firmwareCrc(firmware, _flashSize);
    _writeByte(kGetCrc);
    _writeByte(kEoc);
    _sleepMs(500);

    const quint32 reportedCrc = _readUint32();
    _getSync();
    if (reportedCrc != expectedCrc) {
        emit output(tr("\nExpected 0x%1\n").arg(QStringLiteral("%1").arg(expectedCrc, 0, 16)));
        emit output(tr("Got      0x%1\n").arg(QStringLiteral("%1").arg(reportedCrc, 0, 16)));
        _fail(tr("Program CRC failed"));
    }

    _drawProgress(QStringLiteral("Verify "), 100, 100);
    emit output(QStringLiteral("\n"));
}

void AnelloFirmwareUpgradeWorker::_reboot()
{
    _writeByte(kReboot);
    _writeByte(kEoc);
    _flush();

    if (_bootloaderRevision >= 3) {
        _getSync();
    }
}

void AnelloFirmwareUpgradeWorker::_determineInterface()
{
    _clearInput();

#if defined(Q_OS_WIN)
    const qint32 oddBaud = static_cast<qint32>(_bootloaderBaud * 2.33);
    if (!_bootloaderPort->setBaudRate(oddBaud)) {
        emit output(tr("%1 -> could not check for FTDI device, assuming USB connection\n")
                        .arg(_bootloaderPort->errorString()));
        return;
    }

    _writeByte(kGetSync);
    _writeByte(kEoc);

    try {
        _getSync(false);
    } catch (const std::exception&) {
        emit output(tr("[INFO] Using per-block programming acknowledgements on this serial link.\n"));
        _ackWindowedMode = false;
    }

    _bootloaderPort->setBaudRate(_bootloaderBaud);
#endif
}

void AnelloFirmwareUpgradeWorker::_sync()
{
    _clearInput();
    _writeByte(kGetSync);
    _writeByte(kEoc);
    _getSync();
}

bool AnelloFirmwareUpgradeWorker::_trySync()
{
    try {
        _flush();
        if (_readByte(500) != kInSync) {
            return false;
        }

        const quint8 response = _readByte(500);
        if (response == kBadSiliconRev) {
            _fail(tr("Programming not supported for this version of silicon"));
        }
        return response == kOk;
    } catch (const CancelledError&) {
        throw;
    } catch (const std::exception&) {
        return false;
    }
}

void AnelloFirmwareUpgradeWorker::_getSync(bool flushBeforeRead)
{
    if (flushBeforeRead) {
        _flush();
    }

    const quint8 sync = _readByte();
    if (sync != kInSync) {
        _fail(tr("unexpected 0x%1 instead of INSYNC").arg(sync, 2, 16, QLatin1Char('0')));
    }

    const quint8 response = _readByte();
    if (response == kInvalid) {
        _fail(tr("bootloader reports INVALID OPERATION"));
    }
    if (response == kFailed) {
        _fail(tr("bootloader reports OPERATION FAILED"));
    }
    if (response != kOk) {
        _fail(tr("unexpected response 0x%1 instead of OK").arg(response, 2, 16, QLatin1Char('0')));
    }
}

void AnelloFirmwareUpgradeWorker::_ackSyncWindow(int byteCount)
{
    if (byteCount <= 0) {
        return;
    }

    const QByteArray data = _readData(byteCount);
    if (data.size() != byteCount) {
        _fail(tr("Ack Window %1 not %2").arg(data.size()).arg(byteCount));
    }

    for (int i = 0; i < data.size(); i += 2) {
        const quint8 sync = static_cast<quint8>(data.at(i));
        const quint8 response = static_cast<quint8>(data.at(i + 1));
        if (sync != kInSync) {
            _fail(tr("unexpected 0x%1 instead of INSYNC").arg(sync, 2, 16, QLatin1Char('0')));
        }
        if (response == kInvalid) {
            _fail(tr("bootloader reports INVALID OPERATION"));
        }
        if (response == kFailed) {
            _fail(tr("bootloader reports OPERATION FAILED"));
        }
        if (response != kOk) {
            _fail(tr("unexpected response 0x%1 instead of OK").arg(response, 2, 16, QLatin1Char('0')));
        }
    }
}

quint32 AnelloFirmwareUpgradeWorker::_getInfo(quint8 parameter)
{
    _writeByte(kGetDevice);
    _writeByte(parameter);
    _writeByte(kEoc);

    const quint32 value = _readUint32();
    _getSync();
    return value;
}

quint32 AnelloFirmwareUpgradeWorker::_getChip()
{
    _writeByte(kGetChip);
    _writeByte(kEoc);

    const quint32 value = _readUint32();
    _getSync();
    return value;
}

QStringList AnelloFirmwareUpgradeWorker::_getChipDescription()
{
    _writeByte(kGetChipDescription);
    _writeByte(kEoc);

    const quint32 length = _readUint32();
    const QByteArray value = _readData(static_cast<int>(length));
    _getSync();

    QStringList pieces;
    const QList<QByteArray> rawPieces = value.split(',');
    for (const QByteArray& piece : rawPieces) {
        pieces.append(QString::fromLatin1(piece));
    }
    return pieces;
}

void AnelloFirmwareUpgradeWorker::_programMulti(const QByteArray& data, bool windowedMode)
{
    _writeByte(kProgMulti);
    _writeByte(static_cast<quint8>(data.size()));
    _writeData(data);
    _writeByte(kEoc);

    if (!windowedMode) {
        _getSync(false);
        return;
    }

    const qreal chartimeSeconds = 10.0 / static_cast<qreal>(_bootloaderBaud);
    const qreal sleepSeconds = (data.size() * chartimeSeconds) + kMaxFlashProgramTimeSeconds;
    QThread::usleep(static_cast<unsigned long>(sleepSeconds * 1000000.0));
}

bool AnelloFirmwareUpgradeWorker::_verifyMulti(const QByteArray& data)
{
    _writeByte(kReadMulti);
    _writeByte(static_cast<quint8>(data.size()));
    _writeByte(kEoc);
    _flush();

    const QByteArray programmed = _readData(data.size());
    if (programmed != data) {
        emit output(tr("got    %1\n").arg(QString::fromLatin1(programmed.toHex())));
        emit output(tr("expect %1\n").arg(QString::fromLatin1(data.toHex())));
        return false;
    }

    _getSync();
    return true;
}

void AnelloFirmwareUpgradeWorker::_clearInput()
{
    _checkCancelled();
    _bootloaderPort->clear(QSerialPort::Input);
}

void AnelloFirmwareUpgradeWorker::_flush()
{
    _checkCancelled();
    _bootloaderPort->flush();

    QElapsedTimer timer;
    timer.start();
    while (_bootloaderPort->bytesToWrite() > 0) {
        _checkCancelled();
        const int remaining = 2000 - static_cast<int>(timer.elapsed());
        if (remaining <= 0) {
            _fail(tr("Write timed out: %1").arg(_bootloaderPort->errorString()));
        }
        _bootloaderPort->waitForBytesWritten(std::min(remaining, 100));
    }
}

void AnelloFirmwareUpgradeWorker::_writeByte(quint8 byte)
{
    _writeData(byteArrayFromByte(byte));
}

void AnelloFirmwareUpgradeWorker::_writeData(const QByteArray& data)
{
    _checkCancelled();

    int offset = 0;
    while (offset < data.size()) {
        const qint64 written = _bootloaderPort->write(data.constData() + offset, data.size() - offset);
        if (written < 0) {
            _fail(tr("Write failed: %1").arg(_bootloaderPort->errorString()));
        }
        if (written == 0 && !_bootloaderPort->waitForBytesWritten(1000)) {
            _fail(tr("Write timed out: %1").arg(_bootloaderPort->errorString()));
        }
        if (written > 0) {
            offset += static_cast<int>(written);
        }
    }
}

QByteArray AnelloFirmwareUpgradeWorker::_readData(int byteCount, int timeoutMsecs)
{
    _checkCancelled();

    QByteArray result;
    result.reserve(byteCount);

    QElapsedTimer timer;
    timer.start();

    while (result.size() < byteCount) {
        _checkCancelled();

        const qint64 available = _bootloaderPort->bytesAvailable();
        if (available > 0) {
            result.append(_bootloaderPort->read(byteCount - result.size()));
            continue;
        }

        const int remaining = timeoutMsecs - static_cast<int>(timer.elapsed());
        if (remaining <= 0) {
            _fail(tr("timeout waiting for data (%1 bytes)").arg(byteCount));
        }

        _bootloaderPort->waitForReadyRead(std::min(remaining, 100));
    }

    return result;
}

quint8 AnelloFirmwareUpgradeWorker::_readByte(int timeoutMsecs)
{
    const QByteArray data = _readData(1, timeoutMsecs);
    return static_cast<quint8>(data.at(0));
}

quint32 AnelloFirmwareUpgradeWorker::_readUint32(int timeoutMsecs)
{
    const QByteArray data = _readData(4, timeoutMsecs);
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(data.constData()));
}

void AnelloFirmwareUpgradeWorker::_drawProgress(const QString& label, qreal progress, qreal maximum)
{
    if (maximum <= 0) {
        maximum = 1;
    }
    if (progress > maximum) {
        progress = maximum;
    }

    const qreal percent = (progress / maximum) * 100.0;
    QString bar;
    bar.fill(QLatin1Char('='), static_cast<int>(percent / 5.0));
    const QString paddedBar = bar.leftJustified(20, QLatin1Char(' '), true);

    emit output(QStringLiteral("\r%1: [%2] %3%")
                    .arg(label, paddedBar, QString::number(percent, 'f', 1)));
}

void AnelloFirmwareUpgradeWorker::_sleepMs(int msecs)
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < msecs) {
        _checkCancelled();
        const int remaining = msecs - static_cast<int>(timer.elapsed());
        QThread::msleep(static_cast<unsigned long>(std::min(remaining, 50)));
    }
}
