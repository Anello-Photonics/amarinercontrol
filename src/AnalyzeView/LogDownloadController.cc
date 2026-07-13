/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "LogDownloadController.h"
#include "AppSettings.h"
#include "LogEntry.h"
#include "GPSManager.h"
#include "NTRIPManager.h"
#include "MAVLinkProtocol.h"
#include "MultiVehicleManager.h"
#include "ParameterManager.h"
#include "QGCApplication.h"
#include "QGCLoggingCategory.h"
#include "QmlObjectListModel.h"
#include "SettingsManager.h"
#include "Vehicle.h"

#include <QtCore/QApplicationStatic>
#include <QtCore/QTimer>
#include <QtCore/QRegularExpression>

#include <algorithm>

namespace {
constexpr const char *kMavlinkShellLogRoot = "fs/microsd/log";
}

QGC_LOGGING_CATEGORY(LogDownloadControllerLog, "qgc.analyzeview.logdownloadcontroller")

LogDownloadController::LogDownloadController(QObject *parent)
    : QObject(parent)
    , _timer(new QTimer(this))
    , _logEntriesModel(new QmlObjectListModel(this))
{
    qCDebug(LogDownloadControllerLog) << this;

    (void) connect(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged, this, &LogDownloadController::_setActiveVehicle);
    (void) connect(_timer, &QTimer::timeout, this, &LogDownloadController::_processDownload);

    _timer->setSingleShot(false);

    _setActiveVehicle(MultiVehicleManager::instance()->activeVehicle());
}

LogDownloadController::~LogDownloadController()
{
    qCDebug(LogDownloadControllerLog) << this;
}

void LogDownloadController::download(const QString &path)
{
    const QString dir = path.isEmpty() ? SettingsManager::instance()->appSettings()->logSavePath() : path;
    _downloadToDirectory(dir);
}

void LogDownloadController::_downloadToDirectory(const QString &dir)
{
    _downloadData.reset();

    _downloadPath = dir;
    if (_downloadPath.isEmpty()) {
        return;
    }

    if (!_downloadPath.endsWith(QDir::separator())) {
        _downloadPath += QDir::separator();
    }

    _setDownloading(true);
    _receivedAllEntries();

    QGCLogEntry *const log = _getNextSelected();
    if (log) {
        log->setStatus(tr("Waiting"));
    }

    _receivedAllData();
}

void LogDownloadController::_processDownload()
{
    if (_requestingLogEntries) {
        _findMissingEntries();
    } else if (_downloadingLogs) {
        _findMissingData();
    }
}

void LogDownloadController::_findMissingEntries()
{
    if (_logListElapsed.isValid() && (_logListElapsed.elapsed() > kMaxLogListRequestMs)) {
        _abortLogList(tr("Timed out while retrieving log list. You can still erase logs from the vehicle."));
        return;
    }

    const int num_logs = _logEntriesModel->count();
    int start = -1;
    int end = -1;
    for (int i = 0; i < num_logs; i++) {
        const QGCLogEntry *const entry = _logEntriesModel->value<const QGCLogEntry*>(i);
        if (!entry) {
            continue;
        }

        if (!entry->received()) {
            if (start < 0) {
                start = i;
            } else {
                end = i;
            }
        } else if (start >= 0) {
            break;
        }
    }

    if (start < 0) {
        _receivedAllEntries();
        return;
    }

    if (_retries++ > 2) {
        for (int i = 0; i < num_logs; i++) {
            QGCLogEntry *const entry = _logEntriesModel->value<QGCLogEntry*>(i);
            if (entry && !entry->received()) {
                entry->setStatus(tr("Error"));
            }
        }

        _abortLogList(tr("Unable to retrieve the full log list. You can still erase logs from the vehicle."));
        qCWarning(LogDownloadControllerLog) << "Too many errors retrieving log list. Giving up.";
        return;
    }

    if (end < 0) {
        end = start;
    }

    start += _apmOffset;
    end += _apmOffset;

    _requestLogList(static_cast<uint32_t>(start), static_cast<uint32_t>(end));
}

void LogDownloadController::_setActiveVehicle(Vehicle *vehicle)
{
    if (vehicle == _vehicle) {
        return;
    }

    if (_vehicle) {
        _logEntriesModel->clearAndDeleteContents();
        (void) disconnect(_vehicle, &Vehicle::logEntry, this, &LogDownloadController::_logEntry);
        (void) disconnect(_vehicle, &Vehicle::logData,  this, &LogDownloadController::_logData);
    }

    _vehicle = vehicle;

    if (_vehicle) {
        (void) connect(_vehicle, &Vehicle::logEntry, this, &LogDownloadController::_logEntry);
        (void) connect(_vehicle, &Vehicle::logData,  this, &LogDownloadController::_logData);
    }
}

void LogDownloadController::_logEntry(uint32_t time_utc, uint32_t size, uint16_t id, uint16_t num_logs, uint16_t last_log_num)
{
    Q_UNUSED(last_log_num);

    if (!_requestingLogEntries) {
        return;
    }

    if (_logListElapsed.isValid() && (_logListElapsed.elapsed() > kMaxLogListRequestMs)) {
        _abortLogList(tr("Timed out while retrieving log list. You can still erase logs from the vehicle."));
        return;
    }

    if (num_logs > kMaxLogEntries) {
        _abortLogList(tr("Vehicle reported too many logs to display safely. Erase logs from the vehicle to recover."));
        qCWarning(LogDownloadControllerLog) << "Vehicle reported too many logs to display safely:" << num_logs;
        return;
    }

    if ((_logEntriesModel->count() == 0) && (num_logs > 0)) {
        if (_vehicle->firmwareType() == MAV_AUTOPILOT_ARDUPILOTMEGA) {
            // APM ID starts at 1
            _apmOffset = 1;
        }

        for (int i = 0; i < num_logs; i++) {
            QGCLogEntry *const entry = new QGCLogEntry(i);
            _logEntriesModel->append(entry);
        }
    }

    if (num_logs > 0) {
        if ((size > 0) || (_vehicle->firmwareType() != MAV_AUTOPILOT_ARDUPILOTMEGA)) {
            id -= _apmOffset;
            if (id < _logEntriesModel->count()) {
                QGCLogEntry *const entry = _logEntriesModel->value<QGCLogEntry*>(id);
                entry->setSize(size);
                entry->setTime(QDateTime::fromSecsSinceEpoch(time_utc));
                entry->setReceived(true);
                entry->setStatus(tr("Available"));
            } else {
                qCWarning(LogDownloadControllerLog) << "Received log entry for out-of-bound index:" << id;
            }
        }
    } else {
        _receivedAllEntries();
    }

    _retries = 0;

    if (_entriesComplete()) {
        _receivedAllEntries();
    } else {
        _timer->start(kTimeOutMs);
    }
}

void LogDownloadController::_receivedAllEntries()
{
    _timer->stop();
    _logListElapsed.invalidate();
    _setListing(false);
    emit logFoldersChanged();
}

void LogDownloadController::_abortLogList(const QString &message)
{
    _timer->stop();
    _requestLogEnd();

    if (_logEntriesModel->count() == 0) {
        QGCLogEntry *const entry = new QGCLogEntry(0);
        entry->setStatus(message);
        _logEntriesModel->append(entry);
    }

    _logListElapsed.invalidate();
    _setListing(false);
}

bool LogDownloadController::_entriesComplete() const
{
    const int num_logs = _logEntriesModel->count();
    for (int i = 0; i < num_logs; i++) {
        const QGCLogEntry *const entry = _logEntriesModel->value<const QGCLogEntry*>(i);
        if (!entry) {
            continue;
        }

        if (!entry->received()) {
            return false;
        }
    }

    return true;
}

void LogDownloadController::_logData(uint32_t ofs, uint16_t id, uint8_t count, const uint8_t *data)
{
    if (!_downloadingLogs || !_downloadData) {
        return;
    }

    id -= _apmOffset;
    if (_downloadData->ID != id) {
        qCWarning(LogDownloadControllerLog) << "Received log data for wrong log";
        return;
    }

    if ((ofs % MAVLINK_MSG_LOG_DATA_FIELD_DATA_LEN) != 0) {
        qCWarning(LogDownloadControllerLog) << "Ignored misaligned incoming packet @" << ofs;
        return;
    }

    bool result = false;
    if (ofs <= _downloadData->entry->size()) {
        const uint32_t chunk = ofs / LogDownloadData::kChunkSize;
        // qCDebug(LogDownloadControllerLog) << "Received data - Offset:" << ofs << "Chunk:" << chunk;
        if (chunk != _downloadData->current_chunk) {
            qCWarning(LogDownloadControllerLog) << "Ignored packet for out of order chunk actual:expected" << chunk << _downloadData->current_chunk;
            return;
        }

        const uint16_t bin = (ofs - (chunk * LogDownloadData::kChunkSize)) / MAVLINK_MSG_LOG_DATA_FIELD_DATA_LEN;
        if (bin >= _downloadData->chunk_table.size()) {
            qCWarning(LogDownloadControllerLog) << "Out of range bin received";
        } else {
            _downloadData->chunk_table.setBit(bin);
        }

        if (_downloadData->file.pos() != ofs) {
            if (!_downloadData->file.seek(ofs)) {
                qCWarning(LogDownloadControllerLog) << "Error while seeking log file offset";
                return;
            }
        }

        if (_downloadData->file.write(reinterpret_cast<const char*>(data), count)) {
            _downloadData->written += count;
            _downloadData->rate_bytes += count;
            _updateDataRate();

            result = true;
            _retries = 0;

            _timer->start(kTimeOutMs);
            if (_logComplete()) {
                _downloadData->entry->setStatus(tr("Downloaded"));
                _receivedAllData();
            } else if (_chunkComplete()) {
                _downloadData->advanceChunk();
                _requestLogData(_downloadData->ID,
                                _downloadData->current_chunk * LogDownloadData::kChunkSize,
                                _downloadData->chunk_table.size() * MAVLINK_MSG_LOG_DATA_FIELD_DATA_LEN);
            } else if ((bin < (_downloadData->chunk_table.size() - 1)) && _downloadData->chunk_table.at(bin + 1)) {
                // Likely to be grabbing fragments and got to the end of a gap
                _findMissingData();
            }
        } else {
            qCWarning(LogDownloadControllerLog) << "Error while writing log file chunk";
        }
    } else {
        qCWarning(LogDownloadControllerLog) << "Received log offset greater than expected";
    }

    if (!result) {
        _downloadData->entry->setStatus(tr("Error"));
    }
}

void LogDownloadController::_findMissingData()
{
    if (_logComplete()) {
        _receivedAllData();
        return;
    }

    if (_chunkComplete()) {
        _downloadData->advanceChunk();
    }

    _retries++;

    _updateDataRate();

    uint16_t start = 0, end = 0;
    const int size = _downloadData->chunk_table.size();
    for (; start < size; start++) {
        if (!_downloadData->chunk_table.testBit(start)) {
            break;
        }
    }

    for (end = start; end < size; end++) {
        if (_downloadData->chunk_table.testBit(end)) {
            break;
        }
    }

    const uint32_t pos = (_downloadData->current_chunk * LogDownloadData::kChunkSize) + (start * MAVLINK_MSG_LOG_DATA_FIELD_DATA_LEN);
    const uint32_t len = (end - start) * MAVLINK_MSG_LOG_DATA_FIELD_DATA_LEN;
    _requestLogData(_downloadData->ID, pos, len, _retries);
}

void LogDownloadController::_updateDataRate()
{
    if (_downloadData->elapsed.elapsed() < kGUIRateMs) {
        return;
    }

    const qreal rate = _downloadData->rate_bytes / (_downloadData->elapsed.elapsed() / 1000.0);
    _downloadData->rate_avg = (_downloadData->rate_avg * 0.95) + (rate * 0.05);
    _downloadData->rate_bytes = 0;

    const QString status = QStringLiteral("%1 (%2/s)").arg(qgcApp()->bigSizeToString(_downloadData->written),
                                                           qgcApp()->bigSizeToString(_downloadData->rate_avg));

    _downloadData->entry->setStatus(status);
    _downloadData->elapsed.start();
}

bool LogDownloadController::_chunkComplete() const
{
    return _downloadData->chunkEquals(true);
}

bool LogDownloadController::_logComplete() const
{
    return (_chunkComplete() && ((_downloadData->current_chunk + 1) == _downloadData->numChunks()));
}

void LogDownloadController::_receivedAllData()
{
    _timer->stop();
    if (_prepareLogDownload()) {
        _requestLogData(_downloadData->ID, 0, _downloadData->chunk_table.size() * MAVLINK_MSG_LOG_DATA_FIELD_DATA_LEN);
        _timer->start(kTimeOutMs);
    } else {
        _resetSelection();
        _setDownloading(false);
    }
}

bool LogDownloadController::_prepareLogDownload()
{
    _downloadData.reset();

    QGCLogEntry *const entry = _getNextSelected();
    if (!entry) {
        return false;
    }

    entry->setSelected(false);
    emit selectionChanged();

    const QString ftime = (entry->time().date().year() >= 2010) ? entry->time().toString(QStringLiteral("yyyy-M-d-hh-mm-ss")) : QStringLiteral("UnknownDate");

    _downloadData = std::make_unique<LogDownloadData>(entry);
    _downloadData->filename = QStringLiteral("log_") + QString::number(entry->id()) + "_" + ftime;

    if (_vehicle->firmwareType() == MAV_AUTOPILOT_PX4) {
        const QString loggerParam = QStringLiteral("SYS_LOGGER");
        ParameterManager *const parameterManager = _vehicle->parameterManager();
        if (parameterManager->parameterExists(ParameterManager::defaultComponentId, loggerParam) && parameterManager->getParameter(ParameterManager::defaultComponentId, loggerParam)->rawValue().toInt() == 0) {
            _downloadData->filename += ".px4log";
        } else {
            _downloadData->filename += ".ulg";
        }
    } else {
        _downloadData->filename += ".bin";
    }

    _downloadData->file.setFileName(_downloadPath + _downloadData->filename);

    if (_downloadData->file.exists()) {
        uint32_t numDups = 0;
        const QStringList filename_spl = _downloadData->filename.split('.');
        do {
            numDups += 1;
            const QString filename = filename_spl[0] + '_' + QString::number(numDups) + '.' + filename_spl[1];
            _downloadData->file.setFileName(filename);
        } while ( _downloadData->file.exists());
    }

    bool result = false;
    if (!_downloadData->file.open(QIODevice::WriteOnly)) {
        qCWarning(LogDownloadControllerLog) << "Failed to create log file:" <<  _downloadData->filename;
    } else if (!_downloadData->file.resize(entry->size())) {
        qCWarning(LogDownloadControllerLog) << "Failed to allocate space for log file:" <<  _downloadData->filename;
    } else {
        _downloadData->current_chunk = 0;
        _downloadData->chunk_table = QBitArray(_downloadData->chunkBins(), false);
        _downloadData->elapsed.start();
        result = true;
    }

    if (!result) {
        if (_downloadData->file.exists()) {
            (void) _downloadData->file.remove();
        }

        _downloadData->entry->setStatus(QStringLiteral("Error"));
        _downloadData.reset();
    }

    return result;
}

void LogDownloadController::refresh()
{
    _logEntriesModel->clearAndDeleteContents();
    emit logFoldersChanged();
    _logListElapsed.start();
    _requestLogList(0, 0xffff);
}

QGCLogEntry *LogDownloadController::_getNextSelected() const
{
    const int numLogs = _logEntriesModel->count();
    for (int i = 0; i < numLogs; i++) {
        QGCLogEntry *const entry = _logEntriesModel->value<QGCLogEntry*>(i);
        if (!entry) {
            continue;
        }

        if (entry->selected()) {
           return entry;
        }
    }

    return nullptr;
}

void LogDownloadController::cancel()
{
    _requestLogEnd();
    _receivedAllEntries();

    if (_downloadData) {
        _downloadData->entry->setStatus(QStringLiteral("Canceled"));
        if (_downloadData->file.exists()) {
            (void) _downloadData->file.remove();
        }

        _downloadData.reset();
    }

    _resetSelection(true);
    _setDownloading(false);
}

void LogDownloadController::_resetSelection(bool canceled)
{
    const int num_logs = _logEntriesModel->count();
    for (int i = 0; i < num_logs; i++) {
        QGCLogEntry *const entry = _logEntriesModel->value<QGCLogEntry*>(i);
        if (!entry) {
            continue;
        }

        if (entry->selected()) {
            if (canceled) {
                entry->setStatus(tr("Canceled"));
            }
            entry->setSelected(false);
        }
    }

    emit selectionChanged();
}

QStringList LogDownloadController::logFolders() const
{
    QStringList folders;
    const int numLogs = _logEntriesModel->count();
    for (int i = 0; i < numLogs; i++) {
        const QGCLogEntry *const entry = _logEntriesModel->value<const QGCLogEntry*>(i);
        if (!entry || !entry->received() || (entry->time().date().year() < 2010)) {
            continue;
        }

        const QString folder = entry->time().date().toString(QStringLiteral("yyyy-MM-dd"));
        if (!folders.contains(folder)) {
            folders.append(folder);
        }
    }

    folders.sort();
    return folders;
}

void LogDownloadController::eraseFolder(const QString &folder)
{
    eraseGroup(folder);
}

void LogDownloadController::eraseGroup(const QString &group)
{
    if (!_vehicle) {
        qCWarning(LogDownloadControllerLog) << "Vehicle Unavailable";
        return;
    }

    static const QRegularExpression validLogGroupExpression(QStringLiteral("^[A-Za-z0-9._-]+$"));
    QString logGroup = group.trimmed();
    if (logGroup.startsWith(QLatin1Char('/'))) {
        logGroup.remove(0, 1);
    }
    if (logGroup.startsWith(QString::fromLatin1(kMavlinkShellLogRoot) + QLatin1Char('/'))) {
        logGroup.remove(0, QString::fromLatin1(kMavlinkShellLogRoot).size() + 1);
    }

    if (!validLogGroupExpression.match(logGroup).hasMatch() || (logGroup == QStringLiteral(".")) || (logGroup == QStringLiteral(".."))) {
        qCWarning(LogDownloadControllerLog) << "Invalid log group erase request" << group;
        return;
    }

    const QString folderPath = QStringLiteral("%1/%2").arg(QString::fromLatin1(kMavlinkShellLogRoot), logGroup);
    if (!_sendMavlinkShellCommand(QStringLiteral("rm -r %1").arg(folderPath))) {
        qCWarning(LogDownloadControllerLog) << "Failed to send log group erase command for" << folderPath;
        return;
    }

    _resetSelection();
    _logEntriesModel->clearAndDeleteContents();
    QGCLogEntry *const entry = new QGCLogEntry(0);
    entry->setStatus(tr("Erase command sent for %1. Refresh after the vehicle has deleted the log group.").arg(folderPath));
    _logEntriesModel->append(entry);
    emit logFoldersChanged();

    qCDebug(LogDownloadControllerLog) << "Sent log group erase command:" << folderPath;
}

void LogDownloadController::eraseAll()
{
    if (!_vehicle) {
        qCWarning(LogDownloadControllerLog) << "Vehicle Unavailable";
        return;
    }

    SharedLinkInterfacePtr sharedLink = _vehicle->vehicleLinkManager()->primaryLink().lock();
    if (!sharedLink) {
        qCWarning(LogDownloadControllerLog) << "Link Unavailable";
        return;
    }

    mavlink_message_t msg{};
    (void) mavlink_msg_log_erase_pack_chan(
        MAVLinkProtocol::instance()->getSystemId(),
        MAVLinkProtocol::getComponentId(),
        sharedLink->mavlinkChannel(),
        &msg,
        _vehicle->id(),
        _vehicle->defaultComponentId()
    );

    if (!_vehicle->sendMessageOnLinkThreadSafe(sharedLink.get(), msg)) {
        qCWarning(LogDownloadControllerLog) << "Failed to send";
        return;
    }

    _logEntriesModel->clearAndDeleteContents();
    QGCLogEntry *const entry = new QGCLogEntry(0);
    entry->setStatus(tr("Erase command sent. Refresh after the vehicle has deleted the logs."));
    _logEntriesModel->append(entry);
    emit logFoldersChanged();
}


bool LogDownloadController::_sendMavlinkShellCommand(const QString &command)
{
    SharedLinkInterfacePtr sharedLink = _vehicle->vehicleLinkManager()->primaryLink().lock();
    if (!sharedLink) {
        qCWarning(LogDownloadControllerLog) << "Link Unavailable";
        return false;
    }

    QByteArray data = command.toUtf8();
    data.append('\n');

    while (!data.isEmpty()) {
        QByteArray chunk = data.left(MAVLINK_MSG_SERIAL_CONTROL_FIELD_DATA_LEN);
        data.remove(0, chunk.size());

        const uint8_t count = static_cast<uint8_t>(chunk.size());
        chunk.append(MAVLINK_MSG_SERIAL_CONTROL_FIELD_DATA_LEN - chunk.size(), '\0');

        mavlink_message_t msg{};
        (void) mavlink_msg_serial_control_pack_chan(
            MAVLinkProtocol::instance()->getSystemId(),
            MAVLinkProtocol::getComponentId(),
            sharedLink->mavlinkChannel(),
            &msg,
            SERIAL_CONTROL_DEV_SHELL,
            SERIAL_CONTROL_FLAG_EXCLUSIVE | SERIAL_CONTROL_FLAG_RESPOND | SERIAL_CONTROL_FLAG_MULTI,
            0,
            0,
            count,
            reinterpret_cast<uint8_t*>(chunk.data()),
            _vehicle->id(),
            _vehicle->defaultComponentId()
        );

        if (!_vehicle->sendMessageOnLinkThreadSafe(sharedLink.get(), msg)) {
            qCWarning(LogDownloadControllerLog) << "Failed to send";
            return false;
        }
    }

    return true;
}

void LogDownloadController::_requestLogList(uint32_t start, uint32_t end)
{
    if (!_vehicle) {
        qCWarning(LogDownloadControllerLog) << "Vehicle Unavailable";
        return;
    }

    SharedLinkInterfacePtr sharedLink = _vehicle->vehicleLinkManager()->primaryLink().lock();
    if (!sharedLink) {
        qCWarning(LogDownloadControllerLog) << "Link Unavailable";
        return;
    }

    _setListing(true);

    mavlink_message_t msg{};
    (void) mavlink_msg_log_request_list_pack_chan(
        MAVLinkProtocol::instance()->getSystemId(),
        MAVLinkProtocol::getComponentId(),
        sharedLink->mavlinkChannel(),
        &msg,
        _vehicle->id(),
        _vehicle->defaultComponentId(),
        start,
        end
    );

    if (!_vehicle->sendMessageOnLinkThreadSafe(sharedLink.get(), msg)) {
        qCWarning(LogDownloadControllerLog) << "Failed to send";
        _setListing(false);
        return;
    }

    qCDebug(LogDownloadControllerLog) << "Request log entry list (" << start << "through" << end << ")";
    _timer->start(kRequestLogListTimeoutMs);
}

void LogDownloadController::_requestLogData(uint16_t id, uint32_t offset, uint32_t count, int retryCount)
{
    if (!_vehicle) {
        qCWarning(LogDownloadControllerLog) << "Vehicle Unavailable";
        return;
    }

    SharedLinkInterfacePtr sharedLink = _vehicle->vehicleLinkManager()->primaryLink().lock();
    if (!sharedLink) {
        qCWarning(LogDownloadControllerLog) << "Link Unavailable";
        return;
    }

    id += _apmOffset;
    qCDebug(LogDownloadControllerLog) << "Request log data (id:" << id << "offset:" << offset << "size:" << count << "retryCount" << retryCount << ")";

    mavlink_message_t msg{};
    (void) mavlink_msg_log_request_data_pack_chan(
        MAVLinkProtocol::instance()->getSystemId(),
        MAVLinkProtocol::getComponentId(),
        sharedLink->mavlinkChannel(),
        &msg,
        _vehicle->id(),
        _vehicle->defaultComponentId(),
        id,
        offset,
        count
    );

    if (!_vehicle->sendMessageOnLinkThreadSafe(sharedLink.get(), msg)) {
        qCWarning(LogDownloadControllerLog) << "Failed to send";
    }
}

void LogDownloadController::_requestLogEnd()
{
    if (!_vehicle) {
        qCWarning(LogDownloadControllerLog) << "Vehicle Unavailable";
        return;
    }

    SharedLinkInterfacePtr sharedLink = _vehicle->vehicleLinkManager()->primaryLink().lock();
    if (!sharedLink) {
        qCWarning(LogDownloadControllerLog) << "Link Unavailable";
        return;
    }

    mavlink_message_t msg{};
    (void) mavlink_msg_log_request_end_pack_chan(
        MAVLinkProtocol::instance()->getSystemId(),
        MAVLinkProtocol::getComponentId(),
        sharedLink->mavlinkChannel(),
        &msg,
        _vehicle->id(),
        _vehicle->defaultComponentId()
    );

    if (!_vehicle->sendMessageOnLinkThreadSafe(sharedLink.get(), msg)) {
        qCWarning(LogDownloadControllerLog) << "Failed to send";
    }
}

void LogDownloadController::_setDownloading(bool active)
{
    if (_downloadingLogs != active) {
        _downloadingLogs = active;
        _vehicle->vehicleLinkManager()->setCommunicationLostEnabled(!active);
        _updateNTRIPPause();
        emit downloadingLogsChanged();
    }
}

void LogDownloadController::_setListing(bool active)
{
    if (_requestingLogEntries != active) {
        _requestingLogEntries = active;
        _vehicle->vehicleLinkManager()->setCommunicationLostEnabled(!active);
        _updateNTRIPPause();
        emit requestingListChanged();
    }
}

void LogDownloadController::_updateNTRIPPause()
{
    GPSManager::instance()->ntripManager()->setPaused(_requestingLogEntries || _downloadingLogs);
}
