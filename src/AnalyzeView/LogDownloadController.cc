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
#include "FTPManager.h"
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
#include <QtCore/QFileInfo>
#include <QtCore/QTimer>
#include <QtCore/QTimeZone>
#include <QtCore/QRegularExpression>

#include <algorithm>
#include <limits>

namespace {
constexpr const char *kMavlinkShellLogRoot = "fs/microsd/log";
constexpr const char *kMavlinkFtpLogRoot = "@MAV_LOG";
constexpr const char *kMavlinkFtpLogRootFallback = "/fs/microsd/log";
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
    _legacyDownloadActive = true;
    _downloadToDirectory(dir);
}

void LogDownloadController::_downloadToDirectory(const QString &dir)
{
    _downloadData.reset();

    _downloadPath = dir;
    if (_downloadPath.isEmpty()) {
        _legacyDownloadActive = false;
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

        FTPManager *const ftp = _vehicle->ftpManager();
        (void) disconnect(ftp, &FTPManager::listDirectoryComplete, this, &LogDownloadController::_ftpListDirComplete);
        (void) disconnect(ftp, &FTPManager::downloadComplete,      this, &LogDownloadController::_ftpDownloadComplete);
        (void) disconnect(ftp, &FTPManager::deleteComplete,        this, &LogDownloadController::_ftpDeleteComplete);
        (void) disconnect(ftp, &FTPManager::removeDirectoryComplete, this, &LogDownloadController::_ftpRemoveDirectoryComplete);
        (void) disconnect(ftp, &FTPManager::commandProgress,       this, &LogDownloadController::_ftpCommandProgress);

        _ftpListState = FtpListState::Idle;
        _ftpDirsToList.clear();
        _ftpDownloadQueue.clear();
        _ftpDeleteQueue.clear();
        _ftpRemoveDirectoryQueue.clear();
        _ftpCurrentDownloadEntry = nullptr;
        _ftpCurrentDeleteEntry = nullptr;
        _ftpDeleting = false;
        _setTransport(Transport::Messages);
    }

    _vehicle = vehicle;

    if (_vehicle) {
        (void) connect(_vehicle, &Vehicle::logEntry, this, &LogDownloadController::_logEntry);
        (void) connect(_vehicle, &Vehicle::logData,  this, &LogDownloadController::_logData);

        FTPManager *const ftp = _vehicle->ftpManager();
        (void) connect(ftp, &FTPManager::listDirectoryComplete, this, &LogDownloadController::_ftpListDirComplete);
        (void) connect(ftp, &FTPManager::downloadComplete,      this, &LogDownloadController::_ftpDownloadComplete);
        (void) connect(ftp, &FTPManager::deleteComplete,        this, &LogDownloadController::_ftpDeleteComplete);
        (void) connect(ftp, &FTPManager::removeDirectoryComplete, this, &LogDownloadController::_ftpRemoveDirectoryComplete);
        (void) connect(ftp, &FTPManager::commandProgress,       this, &LogDownloadController::_ftpCommandProgress);
    }
}

void LogDownloadController::_logEntry(uint32_t time_utc, uint32_t size, uint16_t id, uint16_t num_logs, uint16_t last_log_num)
{
    Q_UNUSED(last_log_num);

    if (!_requestingLogEntries || (_transport != Transport::Messages) || _ftpOverlayLegacyRows) {
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

    if (!_legacyDownloadActive && (_transport == Transport::Messages) && _vehicle && (_logEntriesModel->count() > 0)) {
        _ftpOverlayLegacyRows = true;
        _ftpStartListing();
    }
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
        _legacyDownloadActive = false;
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
    _setTransport(Transport::Messages);
    _ftpOverlayLegacyRows = false;
    _ftpDirsToList.clear();
    _ftpDownloadQueue.clear();
    _ftpDeleteQueue.clear();
    _ftpRemoveDirectoryQueue.clear();
    _ftpCurrentDownloadEntry = nullptr;
    _ftpCurrentDeleteEntry = nullptr;
    _ftpDeleting = false;
    emit logFoldersChanged();
    emit selectionChanged();

    _logListElapsed.start();
    _requestLogList(0, 0xffff);
}

QString LogDownloadController::transport() const
{
    return (_transport == Transport::Ftp) ? QStringLiteral("ftp") : QStringLiteral("messages");
}

void LogDownloadController::_setTransport(Transport transport)
{
    if (_transport != transport) {
        _transport = transport;
        emit transportChanged();
    }
}

void LogDownloadController::_ftpStartListing()
{
    _ftpDirsToList.clear();
    _ftpLogIdCounter = 0;
    _ftpLogRoot = QString::fromLatin1(kMavlinkFtpLogRoot);
    _ftpTriedFallbackRoot = false;

    _setTransport(Transport::Ftp);
    _setListing(true);
    _ftpListRoot();
}

void LogDownloadController::_ftpListRoot()
{
    _ftpListState = FtpListState::ListingRoot;

    qCDebug(LogDownloadControllerLog) << "ftp: listing root" << _ftpLogRoot;

    if (!_vehicle || !_vehicle->ftpManager()->listDirectory(MAV_COMP_ID_AUTOPILOT1, _ftpLogRoot)) {
        qCWarning(LogDownloadControllerLog) << "ftp: failed to start root listing for" << _ftpLogRoot;
        _ftpFallbackToMessages(tr("Unable to start MAVLink FTP log listing."));
    }
}

void LogDownloadController::_ftpListDirComplete(const QStringList &dirList, const QString &errorMsg)
{
    if (_ftpListState == FtpListState::Idle) {
        return;
    }

    if (!errorMsg.isEmpty()) {
        if ((_ftpListState == FtpListState::ListingRoot) && !_ftpTriedFallbackRoot) {
            qCDebug(LogDownloadControllerLog) << "ftp: root listing of" << _ftpLogRoot << "failed (" << errorMsg << "), falling back to" << kMavlinkFtpLogRootFallback;
            _ftpTriedFallbackRoot = true;
            _ftpLogRoot = QString::fromLatin1(kMavlinkFtpLogRootFallback);
            _ftpListRoot();
            return;
        }

        _ftpFallbackToMessages(errorMsg);
        return;
    }

    if (_ftpListState == FtpListState::ListingRoot) {
        (void) _ftpProcessFileEntries(dirList, QString());

        for (const QString &entry : dirList) {
            if (entry.startsWith(QLatin1Char('D'))) {
                QString dirName = entry.mid(1);
                const int tabIndex = dirName.indexOf(QLatin1Char('\t'));
                if (tabIndex >= 0) {
                    dirName = dirName.left(tabIndex);
                }
                dirName = dirName.trimmed();
                if (!dirName.isEmpty() && (dirName != QStringLiteral(".")) && (dirName != QStringLiteral(".."))) {
                    _ftpDirsToList.append(dirName);
                }
            }
        }

        _ftpDirsToList.sort();
        _ftpListState = FtpListState::ListingSubdir;
        _ftpListNextSubdir();
        return;
    }

    const QString currentDir = _ftpDirsToList.isEmpty() ? QString() : _ftpDirsToList.first();
    (void) _ftpProcessFileEntries(dirList, currentDir);

    if (!_ftpDirsToList.isEmpty()) {
        _ftpDirsToList.removeFirst();
    }

    _ftpListNextSubdir();
}

uint LogDownloadController::_ftpProcessFileEntries(const QStringList &dirList, const QString &subdir)
{
    const QDate dirDate = subdir.isEmpty() ? QDate() : QDate::fromString(subdir, QStringLiteral("yyyy-MM-dd"));
    uint logsFound = 0;

    for (const QString &entry : dirList) {
        if (!entry.startsWith(QLatin1Char('F'))) {
            continue;
        }

        const QString fileInfo = entry.mid(1);
        const int tabIdx = fileInfo.indexOf(QLatin1Char('\t'));
        if (tabIdx < 0) {
            continue;
        }

        const QString fileName = fileInfo.left(tabIdx).trimmed();
        const QString sizeStr = fileInfo.section(QLatin1Char('\t'), 1, 1);
        const QString mtimeStr = fileInfo.section(QLatin1Char('\t'), 2, 2);

        if (!fileName.endsWith(QStringLiteral(".ulg"), Qt::CaseInsensitive) &&
            !fileName.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive) &&
            !fileName.endsWith(QStringLiteral(".px4log"), Qt::CaseInsensitive)) {
            continue;
        }

        bool sizeOk = false;
        const uint fileSize = sizeStr.toUInt(&sizeOk);
        if (!sizeOk) {
            continue;
        }

        QDateTime dateTime;
        bool mtimeOk = false;
        const qint64 mtimeSecs = mtimeStr.toLongLong(&mtimeOk);
        if (mtimeOk && (mtimeSecs > 0)) {
            dateTime = QDateTime::fromSecsSinceEpoch(mtimeSecs, QTimeZone::UTC);
        }

        if (!dateTime.isValid() && dirDate.isValid()) {
            const QString baseName = QFileInfo(fileName).completeBaseName();
            const QTime fileTime = QTime::fromString(baseName, QStringLiteral("HH_mm_ss"));
            dateTime = QDateTime(dirDate, fileTime.isValid() ? fileTime : QTime(), QTimeZone::UTC);
        }

        const QString ftpPath = subdir.isEmpty()
            ? (_ftpLogRoot + QStringLiteral("/") + fileName)
            : (_ftpLogRoot + QStringLiteral("/") + subdir + QStringLiteral("/") + fileName);

        if (_ftpOverlayLegacyRows) {
            QGCLogEntry *const logEntry = _findLogEntryForFtpFile(dateTime, fileSize);
            if (!logEntry) {
                qCDebug(LogDownloadControllerLog) << "ftp: could not match log file to legacy LOG_ENTRY row" << ftpPath << fileSize << dateTime;
                continue;
            }

            logEntry->setFtpPath(ftpPath);
        } else {
            QGCLogEntry *const logEntry = new QGCLogEntry(_ftpLogIdCounter++, dateTime, fileSize, true);
            logEntry->setFtpPath(ftpPath);
            logEntry->setStatus(tr("Available"));
            (void) connect(logEntry, &QGCLogEntry::selectedChanged, this, &LogDownloadController::selectionChanged);
            _logEntriesModel->append(logEntry);
        }
        logsFound++;
    }

    return logsFound;
}

void LogDownloadController::_ftpListNextSubdir()
{
    if (_ftpDirsToList.isEmpty()) {
        _ftpFinishListing();
        return;
    }

    const QString subdir = _ftpDirsToList.first();
    const QString path = _ftpLogRoot + QStringLiteral("/") + subdir;

    qCDebug(LogDownloadControllerLog) << "ftp: listing subdir" << path;

    if (!_vehicle || !_vehicle->ftpManager()->listDirectory(MAV_COMP_ID_AUTOPILOT1, path)) {
        qCWarning(LogDownloadControllerLog) << "ftp: failed to list subdir" << path;
        _ftpDirsToList.removeFirst();
        _ftpListNextSubdir();
    }
}

void LogDownloadController::_ftpFinishListing()
{
    _ftpListState = FtpListState::Idle;
    _logListElapsed.invalidate();
    if (_ftpOverlayLegacyRows) {
        _ftpOverlayLegacyRows = false;
        if (!_anyLogEntryHasFtpPath()) {
            _setTransport(Transport::Messages);
        }
    }
    _setListing(false);
    emit logFoldersChanged();
}

void LogDownloadController::_ftpFallbackToMessages(const QString &reason)
{
    qCWarning(LogDownloadControllerLog) << "ftp: falling back to message based log download" << reason;

    _ftpListState = FtpListState::Idle;
    _ftpDirsToList.clear();
    if (_ftpOverlayLegacyRows) {
        _ftpOverlayLegacyRows = false;
        if (!_anyLogEntryHasFtpPath()) {
            _setTransport(Transport::Messages);
        }
        _setListing(false);
        emit logFoldersChanged();
        return;
    }

    _setTransport(Transport::Messages);

    _logEntriesModel->clearAndDeleteContents();
    emit logFoldersChanged();
    emit selectionChanged();

    _logListElapsed.start();
    _requestLogList(0, 0xffff);
}

bool LogDownloadController::_anyLogEntryHasFtpPath() const
{
    const int numLogs = _logEntriesModel->count();
    for (int i = 0; i < numLogs; i++) {
        const QGCLogEntry *const entry = _logEntriesModel->value<const QGCLogEntry*>(i);
        if (entry && entry->hasFtpPath()) {
            return true;
        }
    }

    return false;
}

QGCLogEntry *LogDownloadController::_findLogEntryForFtpFile(const QDateTime &dateTime, uint fileSize) const
{
    QGCLogEntry *bestTimeMatch = nullptr;
    qint64 bestTimeDelta = std::numeric_limits<qint64>::max();

    const int numLogs = _logEntriesModel->count();
    for (int i = 0; i < numLogs; i++) {
        QGCLogEntry *const entry = _logEntriesModel->value<QGCLogEntry*>(i);
        if (!entry || entry->hasFtpPath() || !entry->received() || (entry->size() != fileSize)) {
            continue;
        }

        if (dateTime.isValid() && entry->time().isValid() && (entry->time().date().year() >= 2010)) {
            const qint64 delta = qAbs(entry->time().toSecsSinceEpoch() - dateTime.toSecsSinceEpoch());
            if (delta < bestTimeDelta) {
                bestTimeDelta = delta;
                bestTimeMatch = entry;
            }
        }
    }

    if (bestTimeMatch && (bestTimeDelta <= 2)) {
        return bestTimeMatch;
    }

    QGCLogEntry *uniqueSizeMatch = nullptr;
    int sizeMatches = 0;
    for (int i = 0; i < numLogs; i++) {
        QGCLogEntry *const entry = _logEntriesModel->value<QGCLogEntry*>(i);
        if (!entry || entry->hasFtpPath() || !entry->received() || (entry->size() != fileSize)) {
            continue;
        }

        uniqueSizeMatch = entry;
        sizeMatches++;
        if (sizeMatches > 1) {
            return nullptr;
        }
    }

    return uniqueSizeMatch;
}

void LogDownloadController::_ftpDownloadToDirectory(const QString &dir)
{
    _downloadPath = dir;
    if (_downloadPath.isEmpty()) {
        _legacyDownloadActive = false;
        return;
    }

    if (!_downloadPath.endsWith(QDir::separator())) {
        _downloadPath += QDir::separator();
    }

    _ftpDownloadQueue.clear();
    const int numLogs = _logEntriesModel->count();
    for (int i = 0; i < numLogs; i++) {
        QGCLogEntry *const entry = _logEntriesModel->value<QGCLogEntry*>(i);
        if (entry && entry->selected() && entry->hasFtpPath()) {
            entry->setStatus(tr("Waiting"));
            _ftpDownloadQueue.enqueue(entry);
        }
    }

    if (_ftpDownloadQueue.isEmpty()) {
        qCWarning(LogDownloadControllerLog) << "ftp: no selected logs have FTP paths for download";
        return;
    }

    _setDownloading(true);
    _ftpDownloadNext();
}

QString LogDownloadController::_makeUniqueDownloadFileName(const QString &fileName) const
{
    QFileInfo fileInfo(fileName);
    const QString baseName = fileInfo.completeBaseName();
    const QString suffix = fileInfo.suffix();

    QString candidate = fileName;
    uint duplicateIndex = 0;
    while (QFileInfo::exists(_downloadPath + candidate)) {
        duplicateIndex++;
        candidate = suffix.isEmpty()
            ? QStringLiteral("%1_%2").arg(baseName).arg(duplicateIndex)
            : QStringLiteral("%1_%2.%3").arg(baseName).arg(duplicateIndex).arg(suffix);
    }

    return candidate;
}

void LogDownloadController::_ftpDownloadNext()
{
    if (_ftpDownloadQueue.isEmpty()) {
        _ftpCurrentDownloadEntry = nullptr;
        _resetSelection();
        _setDownloading(false);
        return;
    }

    QGCLogEntry *const entry = _ftpDownloadQueue.dequeue();
    if (!entry || !_vehicle) {
        _ftpDownloadNext();
        return;
    }

    entry->setSelected(false);
    entry->setStatus(tr("Downloading"));
    _ftpCurrentDownloadEntry = entry;

    QString localFileName = entry->ftpPath().section(QLatin1Char('/'), -1);
    if (localFileName.isEmpty()) {
        localFileName = QStringLiteral("log_%1.ulg").arg(entry->id());
    }
    localFileName = _makeUniqueDownloadFileName(localFileName);

    if (!_vehicle->ftpManager()->download(MAV_COMP_ID_AUTOPILOT1, entry->ftpPath(), _downloadPath, localFileName)) {
        qCWarning(LogDownloadControllerLog) << "ftp: failed to start download for" << entry->ftpPath();
        entry->setStatus(tr("Error"));
        _ftpCurrentDownloadEntry = nullptr;
        _ftpDownloadNext();
    }
}

void LogDownloadController::_ftpDownloadComplete(const QString &file, const QString &errorMsg)
{
    Q_UNUSED(file);

    if (_transport != Transport::Ftp || !_ftpCurrentDownloadEntry) {
        return;
    }

    if (errorMsg.isEmpty()) {
        _ftpCurrentDownloadEntry->setStatus(tr("Downloaded"));
    } else {
        qCWarning(LogDownloadControllerLog) << "ftp: download failed" << _ftpCurrentDownloadEntry->ftpPath() << errorMsg;
        _ftpCurrentDownloadEntry->setStatus(tr("Error"));
    }

    _ftpCurrentDownloadEntry = nullptr;
    _ftpDownloadNext();
}

void LogDownloadController::_ftpCommandProgress(float value)
{
    if ((_transport == Transport::Ftp) && _ftpCurrentDownloadEntry) {
        _ftpCurrentDownloadEntry->setStatus(tr("Downloading %1%").arg(qBound(0, qRound(value * 100.0f), 100)));
    }
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
    if (_legacyDownloadActive || _downloadData) {
        _requestLogEnd();
        if (_transport == Transport::Messages) {
            _receivedAllEntries();
        }
    } else if (_transport == Transport::Ftp) {
        if (_requestingLogEntries) {
            // AMC's FTP manager has no cancel-list operation. Ignore the eventual completion.
            _ftpListState = FtpListState::Idle;
            _ftpDirsToList.clear();
            _setListing(false);
        }

        if (_downloadingLogs) {
            if (_ftpCurrentDownloadEntry) {
                _ftpCurrentDownloadEntry->setStatus(QStringLiteral("Canceled"));
                _ftpCurrentDownloadEntry = nullptr;
            }
            _ftpDownloadQueue.clear();
        }

        if (_ftpDeleting) {
            _ftpDeleteQueue.clear();
            _ftpRemoveDirectoryQueue.clear();
            _ftpCurrentDeleteEntry = nullptr;
        }
    } else {
        _requestLogEnd();
        _receivedAllEntries();
    }

    if (_downloadData) {
        _downloadData->entry->setStatus(QStringLiteral("Canceled"));
        if (_downloadData->file.exists()) {
            (void) _downloadData->file.remove();
        }

        _downloadData.reset();
    }

    _legacyDownloadActive = false;
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

void LogDownloadController::eraseSelected()
{
    if (!_vehicle) {
        qCWarning(LogDownloadControllerLog) << "Vehicle Unavailable";
        return;
    }

    if (_transport != Transport::Ftp) {
        qCWarning(LogDownloadControllerLog) << "Erase Selected requires MAVLink FTP log listing";
        return;
    }

    _ftpDeleteQueue.clear();
    _ftpRemoveDirectoryQueue.clear();
    const int numLogs = _logEntriesModel->count();
    for (int i = 0; i < numLogs; i++) {
        QGCLogEntry *const entry = _logEntriesModel->value<QGCLogEntry*>(i);
        if (entry && entry->selected() && entry->hasFtpPath()) {
            _ftpDeleteQueue.enqueue(entry);
        }
    }

    if (_ftpDeleteQueue.isEmpty()) {
        qCWarning(LogDownloadControllerLog) << "ftp: no selected logs have FTP paths for erase";
        return;
    }

    _ftpDeleting = true;
    _setDownloading(true);
    _ftpDeleteNext();
}

void LogDownloadController::_ftpDeleteNext()
{
    if (_ftpDeleteQueue.isEmpty()) {
        _ftpCurrentDeleteEntry = nullptr;
        _ftpRemoveDirectoryCleanupNext();
        return;
    }

    QGCLogEntry *const entry = _ftpDeleteQueue.dequeue();
    if (!entry || !_vehicle) {
        _ftpDeleteNext();
        return;
    }

    entry->setSelected(false);
    entry->setStatus(tr("Erasing"));
    _ftpCurrentDeleteEntry = entry;

    qCDebug(LogDownloadControllerLog) << "ftp: deleting" << entry->ftpPath();

    if (!_vehicle->ftpManager()->deleteFile(MAV_COMP_ID_AUTOPILOT1, entry->ftpPath())) {
        qCWarning(LogDownloadControllerLog) << "ftp: failed to start delete for" << entry->ftpPath();
        entry->setStatus(tr("Error"));
        _ftpCurrentDeleteEntry = nullptr;
        _ftpDeleteNext();
    }
}

void LogDownloadController::_ftpDeleteComplete(const QString &file, const QString &errorMsg)
{
    if (!_ftpDeleting) {
        return;
    }

    if (_ftpCurrentDeleteEntry) {
        if (errorMsg.isEmpty()) {
            _ftpCurrentDeleteEntry->setStatus(tr("Erased"));
            _queueFtpDirectoryCleanup(file);
        } else {
            qCWarning(LogDownloadControllerLog) << "ftp: delete error:" << file << errorMsg;
            _ftpCurrentDeleteEntry->setStatus(tr("Error"));
        }
        _ftpCurrentDeleteEntry = nullptr;
    }

    _ftpDeleteNext();
}

void LogDownloadController::_queueFtpDirectoryCleanup(const QString &filePath)
{
    const int slashIndex = filePath.lastIndexOf(QLatin1Char('/'));
    if (slashIndex <= 0) {
        return;
    }

    const QString directory = filePath.left(slashIndex);
    if ((directory == _ftpLogRoot) || _ftpRemoveDirectoryQueue.contains(directory)) {
        return;
    }

    _ftpRemoveDirectoryQueue.enqueue(directory);
}

void LogDownloadController::_ftpRemoveDirectoryCleanupNext()
{
    if (_ftpRemoveDirectoryQueue.isEmpty()) {
        _ftpDeleting = false;
        _setDownloading(false);
        refresh();
        return;
    }

    const QString directory = _ftpRemoveDirectoryQueue.dequeue();
    qCDebug(LogDownloadControllerLog) << "ftp: best-effort remove directory" << directory;

    if (!_vehicle || !_vehicle->ftpManager()->removeDirectory(MAV_COMP_ID_AUTOPILOT1, directory)) {
        qCWarning(LogDownloadControllerLog) << "ftp: failed to start directory cleanup for" << directory;
        _ftpRemoveDirectoryCleanupNext();
    }
}

void LogDownloadController::_ftpRemoveDirectoryComplete(const QString &directory, const QString &errorMsg)
{
    if (!_ftpDeleting) {
        return;
    }

    if (!errorMsg.isEmpty()) {
        qCDebug(LogDownloadControllerLog) << "ftp: directory cleanup skipped/failed" << directory << errorMsg;
    }

    _ftpRemoveDirectoryCleanupNext();
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
