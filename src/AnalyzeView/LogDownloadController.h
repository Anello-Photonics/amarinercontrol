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
#include <QtCore/QQueue>
#include <QtCore/QStringList>
#include <QtQmlIntegration/QtQmlIntegration>

Q_DECLARE_LOGGING_CATEGORY(LogDownloadControllerLog)

struct LogDownloadData;
class QGCLogEntry;
class QmlObjectListModel;
class QTimer;
class QThread;
class Vehicle;
class LogDownloadTest;

class LogDownloadController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_MOC_INCLUDE("Vehicle.h")
    Q_MOC_INCLUDE("QmlObjectListModel.h")
    Q_PROPERTY(QmlObjectListModel *model          READ _getModel            CONSTANT)
    Q_PROPERTY(bool               requestingList  READ _getRequestingList   NOTIFY requestingListChanged)
    Q_PROPERTY(bool               downloadingLogs READ _getDownloadingLogs  NOTIFY downloadingLogsChanged)
    Q_PROPERTY(QStringList        logFolders      READ logFolders           NOTIFY logFoldersChanged)
    Q_PROPERTY(QString            transport       READ transport            NOTIFY transportChanged)

    friend class LogDownloadTest;

public:
    explicit LogDownloadController(QObject *parent = nullptr);
    ~LogDownloadController();

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void download(const QString &path = QString());
    Q_INVOKABLE void eraseAll();
    Q_INVOKABLE void eraseSelected();
    Q_INVOKABLE void eraseFolder(const QString &folder);
    Q_INVOKABLE void eraseGroup(const QString &group);
    QStringList logFolders() const;
    Q_INVOKABLE void cancel();
    QString transport() const;

signals:
    void requestingListChanged();
    void downloadingLogsChanged();
    void selectionChanged();
    void logFoldersChanged();
    void transportChanged();

private slots:
    void _setActiveVehicle(Vehicle *vehicle);
    void _logEntry(uint32_t time_utc, uint32_t size, uint16_t id, uint16_t num_logs, uint16_t last_log_num);
    void _logData(uint32_t ofs, uint16_t id, uint8_t count, const uint8_t *data);
    void _processDownload();
    void _ftpListDirComplete(const QStringList &dirList, const QString &errorMsg);
    void _ftpDownloadComplete(const QString &file, const QString &errorMsg);
    void _ftpDeleteComplete(const QString &file, const QString &errorMsg);
    void _ftpRemoveDirectoryComplete(const QString &directory, const QString &errorMsg);
    void _ftpCommandProgress(float value);

private:
    enum class Transport { Messages, Ftp };
    enum class FtpListState { Idle, ListingRoot, ListingSubdir };

    QmlObjectListModel *_getModel() const { return _logEntriesModel; }
    bool _getRequestingList() const { return _requestingLogEntries; }
    bool _getDownloadingLogs() const { return _downloadingLogs; }

    bool _chunkComplete() const;
    bool _entriesComplete() const;
    bool _logComplete() const;
    bool _prepareLogDownload();
    void _downloadToDirectory(const QString &dir);
    void _ftpStartListing();
    void _ftpListRoot();
    void _ftpListNextSubdir();
    uint _ftpProcessFileEntries(const QStringList &dirList, const QString &subdir);
    void _ftpFinishListing();
    void _ftpFallbackToMessages(const QString &reason = QString());
    void _ftpDownloadToDirectory(const QString &dir);
    void _ftpDownloadNext();
    void _ftpDeleteNext();
    void _ftpRemoveDirectoryCleanupNext();
    void _queueFtpDirectoryCleanup(const QString &filePath);
    void _setTransport(Transport transport);
    void _findMissingData();
    void _findMissingEntries();
    void _receivedAllData();
    void _receivedAllEntries();
    void _abortLogList(const QString &message);
    void _requestLogData(uint16_t id, uint32_t offset, uint32_t count, int retryCount = 0);
    void _requestLogList(uint32_t start, uint32_t end);
    void _requestLogEnd();
    bool _sendMavlinkShellCommand(const QString &command);
    void _resetSelection(bool canceled = false);
    void _setDownloading(bool active);
    void _setListing(bool active);
    void _updateDataRate();
    void _updateNTRIPPause();

    QGCLogEntry *_getNextSelected() const;
    QString _makeUniqueDownloadFileName(const QString &fileName) const;

    QTimer *_timer = nullptr;
    QmlObjectListModel *_logEntriesModel = nullptr;

    bool _downloadingLogs = false;
    bool _requestingLogEntries = false;
    int _apmOffset = 0;
    int _retries = 0;
    QElapsedTimer _logListElapsed;
    std::unique_ptr<LogDownloadData> _downloadData;
    QString _downloadPath;
    Vehicle *_vehicle = nullptr;
    Transport _transport = Transport::Messages;
    FtpListState _ftpListState = FtpListState::Idle;
    bool _ftpTriedFallbackRoot = false;
    bool _ftpDeleting = false;
    uint _ftpLogIdCounter = 0;
    QString _ftpLogRoot;
    QStringList _ftpDirsToList;
    QQueue<QGCLogEntry*> _ftpDownloadQueue;
    QQueue<QGCLogEntry*> _ftpDeleteQueue;
    QQueue<QString> _ftpRemoveDirectoryQueue;
    QGCLogEntry *_ftpCurrentDownloadEntry = nullptr;
    QGCLogEntry *_ftpCurrentDeleteEntry = nullptr;

    static constexpr uint32_t kTimeOutMs = 500;
    static constexpr uint32_t kGUIRateMs = 17; ///< 1000ms / 60fps
    static constexpr uint32_t kRequestLogListTimeoutMs = 5000;
    static constexpr uint32_t kMaxLogListRequestMs = 10000;
    static constexpr int kMaxLogEntries = 300;
};
