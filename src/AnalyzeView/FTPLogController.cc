#include "FTPLogController.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QRegularExpression>
#include <QtCore/QSettings>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkProxy>
#include <algorithm>

namespace {
const QString root = QStringLiteral("/fs/microsd/log/");
}

FTPLogController::FTPLogController(QObject *parent) : QObject(parent)
{
    _host = QSettings().value(QStringLiteral("FTPLogTransfer/host"), QStringLiteral("192.168.0.3")).toString();
    // Vehicle FTP must stay on the local network, independent of system proxies.
    _control.setProxy(QNetworkProxy::NoProxy);
    _data.setProxy(QNetworkProxy::NoProxy);
    _timeout.setSingleShot(true);
    _timeout.setInterval(30000);
    connect(&_timeout, &QTimer::timeout, this, [this] {
        finish(tr("FTP timed out during %1 after receiving %2 bytes. Check the INS/network load and retry.").arg(_phase).arg(_received));
    });
    connect(&_control, &QTcpSocket::readyRead, this, &FTPLogController::readControl);
    connect(&_control, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
        if (_busy && !(error == QAbstractSocket::RemoteHostClosedError && _transferDone))
            finish(tr("FTP connection failed during %1: %2").arg(_phase, _control.errorString()));
    });
    connect(&_control, &QTcpSocket::disconnected, this, [this] {
        if (_busy && !_transferDone) finish(tr("FTP server closed the connection during %1 (%2 bytes received).").arg(_phase).arg(_received));
    });
    connect(&_data, &QTcpSocket::connected, this, [this] {
        command(_download ? QByteArray("RETR ") + _remoteName.toUtf8() : (_useMlsd ? QByteArray("MLSD") : QByteArray("LIST")), State::Transfer);
    });
    connect(&_data, &QTcpSocket::readyRead, this, &FTPLogController::readData);
    connect(&_data, &QTcpSocket::disconnected, this, [this] {
        if (!_busy) return;
        readData();
        _dataDone = true;
        finishTransfer();
    });
    connect(&_data, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
        if (_busy && error != QAbstractSocket::RemoteHostClosedError)
            finish(tr("FTP data connection failed: %1").arg(_data.errorString()));
    });
}

FTPLogController::~FTPLogController()
{
    _busy = false;
    _control.abort();
    _data.abort();
}

void FTPLogController::setHost(const QString &host)
{
    if (_busy || host.trimmed() == _host) return;
    _host = host.trimmed();
    _path = root;
    _entries.clear();
    emit entriesChanged();
    emit changed();
}

bool FTPLogController::safeName(const QString &name)
{
    return !name.isEmpty() && name != "." && name != ".."
        && !name.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f/\\\\:]")));
}

void FTPLogController::refresh()
{
    if (_busy) return;
    _download = false;
    _expectedSize = -1;
    _entries.clear();
    emit entriesChanged();
    start();
}

void FTPLogController::openDirectory(int index)
{
    if (_busy || index < 0 || index >= _entries.size()) return;
    const auto entry = _entries[index].toMap();
    if (!entry.value("directory").toBool()) return;
    _path += entry.value("name").toString() + '/';
    refresh();
}

void FTPLogController::up()
{
    if (_busy || _path == root) return;
    _path.chop(1);
    _path = _path.left(_path.lastIndexOf('/') + 1);
    refresh();
}

void FTPLogController::download(int index, const QString &folder)
{
    if (_busy || index < 0 || index >= _entries.size()) return;
    const auto entry = _entries[index].toMap();
    if (entry.value("directory").toBool()) return;
    _remoteName = entry.value("name").toString();
    if (!_remoteName.endsWith(".ulg", Qt::CaseInsensitive)) return;
    const QUrl url(folder);
    const QDir dir(url.isLocalFile() ? url.toLocalFile() : folder);
    if (folder.isEmpty() || !dir.exists()) { finish(tr("Select an existing local download folder.")); return; }
    _localPath = dir.filePath(_remoteName);
    // Never silently replace an existing log.
    for (int n = 1; QFileInfo::exists(_localPath); ++n)
        _localPath = dir.filePath(QFileInfo(_remoteName).completeBaseName() + QStringLiteral(" (%1).ulg").arg(n));
    _file = std::make_unique<QSaveFile>(_localPath);
    if (!_file->open(QIODevice::WriteOnly)) { finish(tr("Cannot save log: %1").arg(_file->errorString())); return; }
    _download = true;
    _expectedSize = entry.value("size", -1).toLongLong();
    start();
}

void FTPLogController::start()
{
    if (_host.isEmpty() || _host.contains(QRegularExpression(QStringLiteral("[\\s/\\\\]")))) {
        finish(tr("Enter a valid IP address or hostname (without a URL or port)."));
        return;
    }
    QSettings().setValue(QStringLiteral("FTPLogTransfer/host"), _host);
    _controlBuffer.clear();
    _listing.clear();
    _multilineCode = 0;
    _received = 0;
    _progressClock.start();
    _useMlsd = true;
    _dataDone = _transferDone = false;
    _state = State::Greeting;
    _busy = true;
    _status = tr("Connecting to %1:21…").arg(_host);
    _phase = tr("connection/login");
    emit progressChanged();
    emit changed();
    _timeout.start();
    _control.connectToHost(_host, 21);
}

void FTPLogController::command(const QByteArray &command, State state)
{
    _state = state;
    _phase = QString::fromLatin1(command.split(' ').first());
    _timeout.start();
    _control.write(command + "\r\n");
}

void FTPLogController::readControl()
{
    _controlBuffer += _control.readAll();
    if (_controlBuffer.size() > 1024 * 1024) { finish(tr("FTP response is too large.")); return; }
    while (_busy && _controlBuffer.contains('\n')) {
        const int end = _controlBuffer.indexOf('\n');
        const QByteArray line = _controlBuffer.left(end).trimmed();
        _controlBuffer.remove(0, end + 1);
        if (line.size() < 4) continue;
        bool ok = false;
        const int code = line.left(3).toInt(&ok);
        if (!ok) continue;
        if (_multilineCode) {
            if (code != _multilineCode || line[3] != ' ') continue;
            _multilineCode = 0;
        } else if (line[3] == '-') { _multilineCode = code; continue; }
        else if (line[3] != ' ') continue;
        reply(code, line);
    }
}

void FTPLogController::reply(int code, const QByteArray &line)
{
    // SIZE is optional. If unavailable, retain the directory listing's size.
    if (_state == State::Size && (code == 500 || code == 501 || code == 502 || code == 504 || code == 550)) {
        command("EPSV", State::ExtendedPassive);
        return;
    }
    if (_state == State::ExtendedPassive && code >= 400) { command("PASV", State::Passive); return; }
    if (_state == State::Transfer && !_download && _useMlsd && (code == 500 || code == 501 || code == 502 || code == 504)) {
        _data.blockSignals(true);
        _data.abort();
        _data.blockSignals(false);
        _useMlsd = false;
        _listing.clear();
        _dataDone = _transferDone = false;
        command("EPSV", State::ExtendedPassive);
        return;
    }
    if (code >= 400) { finish(tr("FTP error: %1").arg(QString::fromUtf8(line))); return; }
    switch (_state) {
    case State::Greeting:
        if (code == 220) command("USER anonymous", State::User);
        break;
    case State::User:
        if (code == 331) command("PASS ", State::Password);
        else if (code == 230) command("TYPE I", State::Type);
        break;
    case State::Password:
        if (code == 230) command("TYPE I", State::Type);
        break;
    case State::Type:
        if (code == 200) command("CWD " + _path.toUtf8(), State::Directory);
        break;
    case State::Directory:
        if (code == 250) {
            _status = _download ? tr("Downloading %1…").arg(_remoteName) : tr("Listing %1…").arg(_path);
            emit changed();
            if (_download) command("SIZE " + _remoteName.toUtf8(), State::Size);
            else command("EPSV", State::ExtendedPassive);
        }
        break;
    case State::Size:
        if (code == 213) {
            bool ok = false;
            const auto size = line.mid(4).trimmed().toLongLong(&ok);
            if (ok && size >= 0) _expectedSize = size;
            emit progressChanged();
            command("EPSV", State::ExtendedPassive);
        }
        break;
    case State::ExtendedPassive:
    case State::Passive: {
        if (code != 229 && code != 227) break;
        quint32 port = 0;
        if (_state == State::ExtendedPassive) {
            const int begin = line.indexOf('('), end = line.indexOf(')', begin);
            const auto parts = (begin >= 0 && end > begin + 1) ? line.mid(begin + 1, end - begin - 1).split(line[begin + 1]) : QList<QByteArray>();
            if (parts.size() == 5) port = parts[3].toUInt();
        } else {
            const auto match = QRegularExpression(QStringLiteral("\\((\\d+),(\\d+),(\\d+),(\\d+),(\\d+),(\\d+)\\)")).match(QString::fromLatin1(line));
            if (match.hasMatch() && match.captured(5).toUInt() <= 255 && match.captured(6).toUInt() <= 255)
                port = match.captured(5).toUInt() * 256 + match.captured(6).toUInt();
        }
        if (!port || port > 65535) { finish(tr("Invalid FTP passive port.")); return; }
        connectData(static_cast<quint16>(port));
        break;
    }
    case State::Transfer:
        if (code == 226 || code == 250) { _transferDone = true; finishTransfer(); }
        break;
    }
}

void FTPLogController::connectData(quint16 port)
{
    _timeout.start();
    _phase = tr("passive data connection on port %1").arg(port);
    // Use the control peer, avoiding incorrect/NAT-internal addresses in PASV replies.
    _data.connectToHost(_control.peerAddress(), port);
}

void FTPLogController::readData()
{
    if (!_busy) return;
    const auto bytes = _data.readAll();
    _timeout.start();
    _received += bytes.size();
    if (_download) {
        if (_file->write(bytes) != bytes.size()) { finish(tr("Could not write log: %1").arg(_file->errorString())); return; }
    } else {
        _listing += bytes;
        if (_listing.size() > 16 * 1024 * 1024) { finish(tr("FTP directory listing is too large.")); return; }
    }
    // Progress must not rebuild the directory model or saturate the UI while
    // MAVLink telemetry and a large FTP stream are being processed together.
    if (_progressClock.elapsed() >= 100) {
        _progressClock.restart();
        emit progressChanged();
    }
}

void FTPLogController::finishTransfer()
{
    if (!_busy || !_dataDone || !_transferDone) return;
    if (_download) {
        if (_expectedSize >= 0 && _received != _expectedSize) {
            finish(_received > _expectedSize
                ? tr("Log is still growing: expected %1 bytes, received %2. Select an older, completed log and retry.").arg(_expectedSize).arg(_received)
                : tr("FTP download incomplete: expected %1 bytes, received %2. Retry a completed log.").arg(_expectedSize).arg(_received));
            return;
        }
        if (!_file->commit()) { finish(tr("Could not finalize log: %1").arg(_file->errorString())); return; }
        _file.reset();
        finish(tr("Saved %1 (%2 bytes)").arg(_localPath).arg(_received));
    } else if (parseListing()) {
        finish(tr("%1 folders and log files. Open a date/session folder to find .ulg logs.").arg(_entries.size()));
    } else {
        finish(tr("The server returned an unsupported directory listing format."));
    }
}

bool FTPLogController::parseListing()
{
    QVariantList entries;
    const QRegularExpression unixLine(QStringLiteral("^([d-])[rwxstST-]{9}[+@.]?\\s+\\d+\\s+\\S+\\s+\\S+\\s+(\\d+)\\s+\\S+\\s+\\d+\\s+\\S+\\s+(.+)$"));
    const QRegularExpression dosLine(QStringLiteral("^\\d{2}-\\d{2}-\\d{2,4}\\s+\\d{1,2}:\\d{2}[AP]M\\s+(<DIR>|\\d+)\\s+(.+)$"), QRegularExpression::CaseInsensitiveOption);
    bool recognized = _listing.trimmed().isEmpty();
    for (const auto &raw : _listing.split('\n')) {
        QString line = QString::fromUtf8(raw);
        if (line.endsWith('\r')) line.chop(1);
        if (line.isEmpty()) continue;
        if (!_useMlsd && line.startsWith("total ")) { recognized = true; continue; }
        QString name;
        bool directory = false;
        qint64 size = -1;
        if (_useMlsd) {
            const int split = line.indexOf(' ');
            if (split < 0) continue;
            QString type;
            for (const auto &fact : line.left(split).split(';', Qt::SkipEmptyParts)) {
                const int equals = fact.indexOf('=');
                const auto key = fact.left(equals).toLower(), value = fact.mid(equals + 1);
                if (key == "type") type = value.toLower();
                if (key == "size") { bool ok; const auto parsed = value.toLongLong(&ok); if (ok) size = parsed; }
            }
            if (type.isEmpty()) continue;
            recognized = true;
            if (type != "dir" && type != "file") continue;
            directory = type == "dir";
            name = line.mid(split + 1);
        } else {
            const auto unixMatch = unixLine.match(line), dosMatch = dosLine.match(line);
            if (unixMatch.hasMatch()) {
                directory = unixMatch.captured(1) == "d";
                size = unixMatch.captured(2).toLongLong();
                name = unixMatch.captured(3);
            } else if (dosMatch.hasMatch()) {
                directory = dosMatch.captured(1).compare("<DIR>", Qt::CaseInsensitive) == 0;
                size = directory ? -1 : dosMatch.captured(1).toLongLong();
                name = dosMatch.captured(2);
            } else continue;
            recognized = true;
        }
        if (!safeName(name) || (!directory && !name.endsWith(".ulg", Qt::CaseInsensitive))) continue;
        entries.append(QVariantMap{{"name", name}, {"directory", directory}, {"size", size}});
    }
    std::sort(entries.begin(), entries.end(), [](const QVariant &a, const QVariant &b) {
        const auto left = a.toMap(), right = b.toMap();
        if (left.value("directory") != right.value("directory")) return left.value("directory").toBool();
        return left.value("name").toString() < right.value("name").toString();
    });
    _entries = entries;
    emit entriesChanged();
    return recognized;
}

void FTPLogController::finish(const QString &status)
{
    _busy = false;
    _timeout.stop();
    _control.abort();
    _data.abort();
    _file.reset(); // QSaveFile removes any incomplete temporary download.
    _status = status;
    emit progressChanged();
    emit changed();
}

void FTPLogController::cancel()
{
    if (_busy) finish(tr("Transfer cancelled."));
}
