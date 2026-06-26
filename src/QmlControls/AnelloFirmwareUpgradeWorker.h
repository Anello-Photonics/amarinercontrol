/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <atomic>

#include <QtCore/QByteArray>
#include <QtCore/QElapsedTimer>
#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QString>

#ifdef Q_OS_ANDROID
#include "qserialport.h"
#else
#include <QtSerialPort/QSerialPort>
#endif

class AnelloFirmwareUpgradeWorker : public QObject
{
    Q_OBJECT

public:
    explicit AnelloFirmwareUpgradeWorker(const QString& portName,
                                         qint32 bootloaderBaud,
                                         qint32 flightstackBaud,
                                         const QString& firmwarePath,
                                         QObject* parent = nullptr);

    void cancel();

public slots:
    void run();

signals:
    void output(const QString& text);
    void finished(bool success, int exitCode, const QString& message);

private:
    struct FirmwareImage {
        QJsonObject description;
        QByteArray image;
        quint32 boardId = 0;
        quint32 boardRevision = 0;
        quint32 imageSize = 0;
        quint32 imageMaxSize = 0;
    };

    void _checkCancelled() const;
    [[noreturn]] void _fail(const QString& message, int exitCode = 1);

    FirmwareImage _loadFirmware() const;
    static QByteArray _extractJsonStringValue(const QByteArray& jsonBytes, const QByteArray& key);
    static QByteArray _inflateJsonImage(const QByteArray& jsonBytes, const QJsonObject& jsonObject);
    static quint32 _firmwareCrc(const FirmwareImage& firmware, quint32 paddedFlashSize);

    void _enterBootloader();
    bool _openSerial(QSerialPort& serial, const QString& portName, qint32 baudRate, QString* errorString);
    bool _waitForMavlinkHeartbeat(QSerialPort& serial, int timeoutMsecs);
    void _sendMavlinkHeartbeat(QSerialPort& serial);
    void _sendMavlinkSerialControl(QSerialPort& serial, const QByteArray& data, quint8 flags);
    void _sendNmeaBootloaderReboot();

    void _waitForBootloader();
    void _openBootloaderPort();
    void _identifyBootloader();
    void _uploadFirmware(const FirmwareImage& firmware);
    void _readOptionalBoardInfo(const FirmwareImage& firmware);
    void _erase();
    void _program(const FirmwareImage& firmware);
    void _verify(const FirmwareImage& firmware);
    void _verifyV2(const FirmwareImage& firmware);
    void _verifyV3(const FirmwareImage& firmware);
    void _reboot();

    void _determineInterface();
    void _sync();
    bool _trySync();
    void _getSync(bool flushBeforeRead = true);
    void _ackSyncWindow(int byteCount);
    quint32 _getInfo(quint8 parameter);
    quint32 _getChip();
    QStringList _getChipDescription();
    void _programMulti(const QByteArray& data, bool windowedMode);
    bool _verifyMulti(const QByteArray& data);

    void _clearInput();
    void _flush();
    void _writeByte(quint8 byte);
    void _writeData(const QByteArray& data);
    QByteArray _readData(int byteCount, int timeoutMsecs = 2000);
    quint8 _readByte(int timeoutMsecs = 2000);
    quint32 _readUint32(int timeoutMsecs = 2000);

    void _drawProgress(const QString& label, qreal progress, qreal maximum);
    void _sleepMs(int msecs);

    const QString _portName;
    const qint32 _bootloaderBaud = 115200;
    const qint32 _flightstackBaud = 57600;
    const QString _firmwarePath;

    std::atomic_bool _cancelled{false};
    QSerialPort* _bootloaderPort = nullptr;
    QElapsedTimer _elapsedTimer;

    quint32 _bootloaderRevision = 0;
    quint32 _boardType = 0;
    quint32 _boardRevision = 0;
    quint32 _flashSize = 0;
    bool _ackWindowedMode = false;
    int _ackWindowBytes = 0;
    int _failureExitCode = 1;

    static constexpr quint8 kInSync = 0x12;
    static constexpr quint8 kEoc = 0x20;
    static constexpr quint8 kOk = 0x10;
    static constexpr quint8 kFailed = 0x11;
    static constexpr quint8 kInvalid = 0x13;
    static constexpr quint8 kBadSiliconRev = 0x14;

    static constexpr quint8 kGetSync = 0x21;
    static constexpr quint8 kGetDevice = 0x22;
    static constexpr quint8 kChipErase = 0x23;
    static constexpr quint8 kChipVerify = 0x24;
    static constexpr quint8 kProgMulti = 0x27;
    static constexpr quint8 kReadMulti = 0x28;
    static constexpr quint8 kGetCrc = 0x29;
    static constexpr quint8 kGetChip = 0x2c;
    static constexpr quint8 kGetChipDescription = 0x2e;
    static constexpr quint8 kReboot = 0x30;

    static constexpr quint8 kInfoBootloaderRevision = 0x01;
    static constexpr quint8 kInfoBoardId = 0x02;
    static constexpr quint8 kInfoBoardRevision = 0x03;
    static constexpr quint8 kInfoFlashSize = 0x04;

    static constexpr quint32 kBootloaderRevisionMin = 2;
    static constexpr quint32 kBootloaderRevisionMax = 5;
    static constexpr int kProgMultiMax = 252;
    static constexpr int kReadMultiMax = 252;
    static constexpr int kMavlinkSerialDevice = 10;
    static constexpr int kMavlinkSystemId = 255;
    static constexpr int kMavlinkComponentId = 0;
    static constexpr int kBootloaderWaitMsecs = 10000;
};
