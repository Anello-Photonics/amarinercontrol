/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "QGroundControlQmlGlobal.h"

#include "AnelloFirmwareUpgradeWorker.h"

#include "QGCApplication.h"
#include "QGCCorePlugin.h"
#include "LinkManager.h"
#include "MAVLinkProtocol.h"
#include "FirmwarePluginManager.h"
#include "AppSettings.h"
#include "FlightMapSettings.h"
#include "SettingsManager.h"
#include "PositionManager.h"
#include "QGCMapEngineManager.h"
#include "ADSBVehicleManager.h"
#include "MissionCommandTree.h"
#include "VideoManager.h"
#include "MultiVehicleManager.h"
#include "QGCLoggingCategory.h"
#ifndef QGC_NO_SERIAL_LINK
#include "GPSManager.h"
#include "GPSRtk.h"
#include "NTRIPManager.h"
#endif
#ifdef QT_DEBUG
#include "MockLink.h"
#endif
#ifndef QGC_AIRLINK_DISABLED
#include "AirLinkManager.h"
#endif
#ifdef QGC_UTM_ADAPTER
#include "UTMSPManager.h"
#endif

#include <QtCore/QSettings>
#include <QtCore/QLineF>
#include <QtCore/QFileInfo>
#include <QtCore/QUrl>
#include <QtCore/QThread>
#include <QtSerialPort/QSerialPortInfo>

QGC_LOGGING_CATEGORY(GuidedActionsControllerLog, "GuidedActionsControllerLog")

QGeoCoordinate QGroundControlQmlGlobal::_coord = QGeoCoordinate(0.0,0.0);
double QGroundControlQmlGlobal::_zoom = 2;

QGroundControlQmlGlobal::QGroundControlQmlGlobal(QObject *parent)
    : QObject(parent)
    , _mapEngineManager(QGCMapEngineManager::instance())
    , _adsbVehicleManager(ADSBVehicleManager::instance())
    , _qgcPositionManager(QGCPositionManager::instance())
    , _missionCommandTree(MissionCommandTree::instance())
    , _videoManager(VideoManager::instance())
    , _linkManager(LinkManager::instance())
    , _multiVehicleManager(MultiVehicleManager::instance())
    , _settingsManager(SettingsManager::instance())
    , _corePlugin(QGCCorePlugin::instance())
    , _globalPalette(new QGCPalette(this))
#ifndef QGC_NO_SERIAL_LINK
    , _gpsRtkFactGroup(GPSManager::instance()->gpsRtk()->gpsRtkFactGroup())
    , _ntripManager(GPSManager::instance()->ntripManager())
#endif
#ifndef QGC_AIRLINK_DISABLED
    , _airlinkManager(AirLinkManager::instance())
#endif
#ifdef QGC_UTM_ADAPTER
    , _utmspManager(UTMSPManager::instance())
#endif
{
    // We clear the parent on this object since we run into shutdown problems caused by hybrid qml app. Instead we let it leak on shutdown.
    // setParent(nullptr);

    // Load last coordinates and zoom from config file
    QSettings settings;
    settings.beginGroup(_flightMapPositionSettingsGroup);
    _coord.setLatitude(settings.value(_flightMapPositionLatitudeSettingsKey,    _coord.latitude()).toDouble());
    _coord.setLongitude(settings.value(_flightMapPositionLongitudeSettingsKey,  _coord.longitude()).toDouble());
    _zoom = settings.value(_flightMapZoomSettingsKey, _zoom).toDouble();
    _flightMapPositionSettledTimer.setSingleShot(true);
    _flightMapPositionSettledTimer.setInterval(1000);
    (void) connect(&_flightMapPositionSettledTimer, &QTimer::timeout, this, []() {
        // When they settle, save flightMapPosition and Zoom to the config file
        QSettings settings;
        settings.beginGroup(_flightMapPositionSettingsGroup);
        settings.setValue(_flightMapPositionLatitudeSettingsKey, _coord.latitude());
        settings.setValue(_flightMapPositionLongitudeSettingsKey, _coord.longitude());
        settings.setValue(_flightMapZoomSettingsKey, _zoom);
    });
    connect(this, &QGroundControlQmlGlobal::flightMapPositionChanged, this, [this](QGeoCoordinate){
        if (!_flightMapPositionSettledTimer.isActive()) {
            _flightMapPositionSettledTimer.start();
        }
    });
    connect(this, &QGroundControlQmlGlobal::flightMapZoomChanged, this, [this](double){
        if (!_flightMapPositionSettledTimer.isActive()) {
            _flightMapPositionSettledTimer.start();
        }
    });
    refreshAvailableSerialPorts();
}

QGroundControlQmlGlobal::~QGroundControlQmlGlobal()
{
    if (_firmwareUpgradeWorker) {
        _firmwareUpgradeWorker->cancel();
    }
    if (_firmwareUpgradeThread) {
        _firmwareUpgradeThread->quit();
        _firmwareUpgradeThread->wait(3000);
    }
}

void QGroundControlQmlGlobal::saveGlobalSetting (const QString& key, const QString& value)
{
    QSettings settings;
    settings.beginGroup(kQmlGlobalKeyName);
    settings.setValue(key, value);
}

QString QGroundControlQmlGlobal::loadGlobalSetting (const QString& key, const QString& defaultValue)
{
    QSettings settings;
    settings.beginGroup(kQmlGlobalKeyName);
    return settings.value(key, defaultValue).toString();
}

void QGroundControlQmlGlobal::saveBoolGlobalSetting (const QString& key, bool value)
{
    QSettings settings;
    settings.beginGroup(kQmlGlobalKeyName);
    settings.setValue(key, value);
}

bool QGroundControlQmlGlobal::loadBoolGlobalSetting (const QString& key, bool defaultValue)
{
    QSettings settings;
    settings.beginGroup(kQmlGlobalKeyName);
    return settings.value(key, defaultValue).toBool();
}

void QGroundControlQmlGlobal::startPX4MockLink(bool sendStatusText)
{
#ifdef QT_DEBUG
    MockLink::startPX4MockLink(sendStatusText);
#else
    Q_UNUSED(sendStatusText);
#endif
}

void QGroundControlQmlGlobal::startGenericMockLink(bool sendStatusText)
{
#ifdef QT_DEBUG
    MockLink::startGenericMockLink(sendStatusText);
#else
    Q_UNUSED(sendStatusText);
#endif
}

void QGroundControlQmlGlobal::startAPMArduCopterMockLink(bool sendStatusText)
{
#ifdef QT_DEBUG
    MockLink::startAPMArduCopterMockLink(sendStatusText);
#else
    Q_UNUSED(sendStatusText);
#endif
}

void QGroundControlQmlGlobal::startAPMArduPlaneMockLink(bool sendStatusText)
{
#ifdef QT_DEBUG
    MockLink::startAPMArduPlaneMockLink(sendStatusText);
#else
    Q_UNUSED(sendStatusText);
#endif
}

void QGroundControlQmlGlobal::startAPMArduSubMockLink(bool sendStatusText)
{
#ifdef QT_DEBUG
    MockLink::startAPMArduSubMockLink(sendStatusText);
#else
    Q_UNUSED(sendStatusText);
#endif
}

void QGroundControlQmlGlobal::startAPMArduRoverMockLink(bool sendStatusText)
{
#ifdef QT_DEBUG
    MockLink::startAPMArduRoverMockLink(sendStatusText);
#else
    Q_UNUSED(sendStatusText);
#endif
}

void QGroundControlQmlGlobal::stopOneMockLink(void)
{
#ifdef QT_DEBUG
    QList<SharedLinkInterfacePtr> sharedLinks = LinkManager::instance()->links();

    for (int i=0; i<sharedLinks.count(); i++) {
        LinkInterface* link = sharedLinks[i].get();
        MockLink* mockLink = qobject_cast<MockLink*>(link);
        if (mockLink) {
            mockLink->disconnect();
            return;
        }
    }
#endif
}

bool QGroundControlQmlGlobal::singleFirmwareSupport(void)
{
    return FirmwarePluginManager::instance()->supportedFirmwareClasses().count() == 1;
}

bool QGroundControlQmlGlobal::singleVehicleSupport(void)
{
    if (singleFirmwareSupport()) {
        return FirmwarePluginManager::instance()->supportedVehicleClasses(FirmwarePluginManager::instance()->supportedFirmwareClasses()[0]).count() == 1;
    }

    return false;
}

bool QGroundControlQmlGlobal::px4ProFirmwareSupported()
{
    return FirmwarePluginManager::instance()->supportedFirmwareClasses().contains(QGCMAVLink::FirmwareClassPX4);
}

bool QGroundControlQmlGlobal::apmFirmwareSupported()
{
    return FirmwarePluginManager::instance()->supportedFirmwareClasses().contains(QGCMAVLink::FirmwareClassArduPilot);
}

bool QGroundControlQmlGlobal::linesIntersect(QPointF line1A, QPointF line1B, QPointF line2A, QPointF line2B)
{
    QPointF intersectPoint;

    auto intersect = QLineF(line1A, line1B).intersects(QLineF(line2A, line2B), &intersectPoint);

    return  intersect == QLineF::BoundedIntersection &&
            intersectPoint != line1A && intersectPoint != line1B;
}

void QGroundControlQmlGlobal::refreshAvailableSerialPorts()
{
    QStringList ports;
    const auto portInfos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo& info: portInfos) {
        if (!info.portName().isEmpty()) {
#if defined(Q_OS_WIN)
            ports << info.portName();
#else
            ports << (QStringLiteral("/dev/") + info.portName());
#endif
        }
    }

    ports.removeDuplicates();
    ports.sort();
    if (ports != _availableSerialPorts) {
        _availableSerialPorts = ports;
        emit availableSerialPortsChanged();
    }
}

bool QGroundControlQmlGlobal::launchFirmwareUpgrade(const QString& port, const QString& flightstackBaud, const QString& firmwarePath)
{
    const QString normalizedFirmwarePath = QUrl(firmwarePath).isLocalFile() ? QUrl(firmwarePath).toLocalFile() : firmwarePath;

    const QFileInfo firmwareInfo(normalizedFirmwarePath);
    if (!firmwareInfo.exists() || !firmwareInfo.isFile()) {
        qgcApp()->showAppMessage(tr("Firmware file not found: %1").arg(normalizedFirmwarePath));
        return false;
    }

    if (port.trimmed().isEmpty()) {
        qgcApp()->showAppMessage(tr("Serial port is required to launch firmware upgrade."));
        return false;
    }

    bool baudOk = false;
    const qint32 flightstackBaudValue = flightstackBaud.trimmed().toInt(&baudOk);
    if (!baudOk || flightstackBaudValue <= 0) {
        qgcApp()->showAppMessage(tr("Valid flightstack baud rate is required to launch firmware upgrade."));
        return false;
    }

    if (_firmwareUpgradeRunning) {
        qgcApp()->showAppMessage(tr("Firmware upgrade is already running."));
        return false;
    }

    _firmwareUpgradeOutput.clear();
    _firmwareUpgradeAnsiBuffer.clear();
    _firmwareUpgradeCarriageReturnPending = false;
    emit firmwareUpgradeOutputChanged();

    _firmwareUpgradeThread = new QThread(this);
    _firmwareUpgradeWorker = new AnelloFirmwareUpgradeWorker(port.trimmed(), 115200, flightstackBaudValue, firmwareInfo.absoluteFilePath());
    _firmwareUpgradeWorker->moveToThread(_firmwareUpgradeThread);

    connect(_firmwareUpgradeThread, &QThread::started, _firmwareUpgradeWorker, &AnelloFirmwareUpgradeWorker::run);
    connect(_firmwareUpgradeWorker, &AnelloFirmwareUpgradeWorker::output, this, [this](const QString& text) {
        _appendFirmwareUpgradeOutput(text);
    });
    connect(_firmwareUpgradeWorker, &AnelloFirmwareUpgradeWorker::finished, this, [this](bool success, int exitCode, const QString& message) {
        Q_UNUSED(message)
        _appendFirmwareUpgradeOutput(tr("\n[INFO] Firmware upgrade finished (exit code %1, status %2).\n")
                                     .arg(exitCode)
                                     .arg(success ? tr("Success") : tr("Failed")));
        _firmwareUpgradeRunning = false;
        _firmwareUpgradeWorker = nullptr;
        emit firmwareUpgradeRunningChanged();
    });
    connect(_firmwareUpgradeWorker, &AnelloFirmwareUpgradeWorker::finished, _firmwareUpgradeWorker, &QObject::deleteLater);
    connect(_firmwareUpgradeWorker, &AnelloFirmwareUpgradeWorker::finished, _firmwareUpgradeThread, &QThread::quit);

    QThread* firmwareUpgradeThread = _firmwareUpgradeThread;
    connect(firmwareUpgradeThread, &QThread::finished, this, [this, firmwareUpgradeThread]() {
        firmwareUpgradeThread->deleteLater();
        if (_firmwareUpgradeThread == firmwareUpgradeThread) {
            _firmwareUpgradeThread = nullptr;
        }
    });

    _firmwareUpgradeRunning = true;
    emit firmwareUpgradeRunningChanged();

    _firmwareUpgradeThread->start();
    return true;
}

void QGroundControlQmlGlobal::cancelFirmwareUpgrade()
{
    if (!_firmwareUpgradeRunning || !_firmwareUpgradeWorker) {
        return;
    }

    _appendFirmwareUpgradeOutput(tr("\n[INFO] Cancelling firmware upgrade.\n"));
    _firmwareUpgradeWorker->cancel();
}

QString QGroundControlQmlGlobal::_firmwareUpgradeProgressPhase(const QString& text, int startIndex)
{
    if (startIndex < 0 || startIndex >= text.length()) {
        return QString();
    }

    QString view = text.mid(startIndex).trimmed();
    if (view.startsWith(QStringLiteral("Erase"))) {
        return QStringLiteral("Erase");
    } else if (view.startsWith(QStringLiteral("Program"))) {
        return QStringLiteral("Program");
    } else if (view.startsWith(QStringLiteral("Verify"))) {
        return QStringLiteral("Verify");
    }

    return QString();
}

bool QGroundControlQmlGlobal::_truncateCurrentFirmwareUpgradeLineIfProgress(QString* currentPhase)
{
    const int lastNewlineIndex = _firmwareUpgradeOutput.lastIndexOf(QLatin1Char('\n'));
    const int lineStartIndex = (lastNewlineIndex < 0) ? 0 : (lastNewlineIndex + 1);
    const QString phase = _firmwareUpgradeProgressPhase(_firmwareUpgradeOutput, lineStartIndex);

    if (phase.isEmpty()) {
        return false;
    }

    if (currentPhase) {
        *currentPhase = phase;
    }

    _firmwareUpgradeOutput.truncate(lineStartIndex);
    return true;
}

void QGroundControlQmlGlobal::_resolveFirmwareUpgradeCarriageReturn(const QString& text, int startIndex)
{
    if (!_firmwareUpgradeCarriageReturnPending) {
        return;
    }

    QString currentPhase;
    const bool hasCurrentProgressLine = _truncateCurrentFirmwareUpgradeLineIfProgress(&currentPhase);
    if (!hasCurrentProgressLine) {
        _firmwareUpgradeCarriageReturnPending = false;
        return;
    }

    const QString nextPhase = _firmwareUpgradeProgressPhase(text, startIndex);
    if (!nextPhase.isEmpty() && nextPhase != currentPhase) {
        _firmwareUpgradeOutput += QLatin1Char('\n');
    }

    _firmwareUpgradeCarriageReturnPending = false;
}

void QGroundControlQmlGlobal::_appendFirmwareUpgradeOutput(const QString& text)
{
    if (text.isEmpty()) {
        return;
    }

    QString data = _firmwareUpgradeAnsiBuffer + text;
    _firmwareUpgradeAnsiBuffer.clear();

    bool updated = false;

    for (int index = 0; index < data.length(); ++index) {
        const QChar character = data.at(index);

        if (character == QLatin1Char('\x1b')) {
            if ((index + 1) >= data.length()) {
                _firmwareUpgradeAnsiBuffer = data.mid(index);
                break;
            }

            if (data.at(index + 1) == QLatin1Char('[')) {
                int sequenceIndex = index + 2;
                while (sequenceIndex < data.length() && !data.at(sequenceIndex).isLetter()) {
                    sequenceIndex++;
                }

                if (sequenceIndex >= data.length()) {
                    _firmwareUpgradeAnsiBuffer = data.mid(index);
                    break;
                }

                index = sequenceIndex;
                continue;
            }
        }

        if (character == QLatin1Char('\r')) {
            _firmwareUpgradeCarriageReturnPending = true;
            continue;
        }

        if (_firmwareUpgradeCarriageReturnPending && character == QLatin1Char('\n')) {
            _firmwareUpgradeCarriageReturnPending = false;
            _firmwareUpgradeOutput += character;
            updated = true;
            continue;
        }

        if (_firmwareUpgradeCarriageReturnPending) {
            _resolveFirmwareUpgradeCarriageReturn(data, index);
            updated = true;
        }

        if (character == QLatin1Char('\b')) {
            if (!_firmwareUpgradeOutput.isEmpty() && !_firmwareUpgradeOutput.endsWith(QLatin1Char('\n'))) {
                _firmwareUpgradeOutput.chop(1);
                updated = true;
            }
            continue;
        }

        _firmwareUpgradeOutput += character;
        updated = true;
    }

    if (updated) {
        emit firmwareUpgradeOutputChanged();
    }
}


void QGroundControlQmlGlobal::setFlightMapPosition(QGeoCoordinate& coordinate)
{
    if (coordinate != flightMapPosition()) {
        _coord.setLatitude(coordinate.latitude());
        _coord.setLongitude(coordinate.longitude());
        emit flightMapPositionChanged(coordinate);
    }
}

void QGroundControlQmlGlobal::setFlightMapZoom(double zoom)
{
    if (zoom != flightMapZoom()) {
        _zoom = zoom;
        emit flightMapZoomChanged(zoom);
    }
}

QString QGroundControlQmlGlobal::qgcVersion(void)
{
    QString versionStr = QCoreApplication::applicationVersion();
    if(QSysInfo::buildAbi().contains("32"))
    {
        versionStr += QStringLiteral(" %1").arg(tr("32 bit"));
    }
    else if(QSysInfo::buildAbi().contains("64"))
    {
        versionStr += QStringLiteral(" %1").arg(tr("64 bit"));
    }
    return versionStr;
}

QString QGroundControlQmlGlobal::altitudeModeExtraUnits(AltMode altMode)
{
    switch (altMode) {
    case AltitudeModeNone:
        return QString();
    case AltitudeModeRelative:
        // Showing (Rel) all the time ends up being too noisy
        return QString();
    case AltitudeModeAbsolute:
        return tr("(AMSL)");
    case AltitudeModeCalcAboveTerrain:
        return tr("(CalcT)");
    case AltitudeModeTerrainFrame:
        return tr("(TerrF)");
    case AltitudeModeMixed:
        qWarning() << "Internal Error: QGroundControlQmlGlobal::altitudeModeExtraUnits called with altMode == AltitudeModeMixed";
        return QString();
    }

    // Should never get here but makes some compilers happy
    return QString();
}

QString QGroundControlQmlGlobal::altitudeModeShortDescription(AltMode altMode)
{
    switch (altMode) {
    case AltitudeModeNone:
        return QString();
    case AltitudeModeRelative:
        return tr("Relative To Launch");
    case AltitudeModeAbsolute:
        return tr("AMSL");
    case AltitudeModeCalcAboveTerrain:
        return tr("Calc Above Terrain");
    case AltitudeModeTerrainFrame:
        return tr("Terrain Frame");
    case AltitudeModeMixed:
        return tr("Mixed Modes");
    }

    // Should never get here but makes some compilers happy
    return QString();
}

QString QGroundControlQmlGlobal::elevationProviderName()
{
    return _settingsManager->flightMapSettings()->elevationMapProvider()->rawValue().toString();
}

QString QGroundControlQmlGlobal::elevationProviderNotice()
{
    return _settingsManager->flightMapSettings()->elevationMapProvider()->rawValue().toString();
}

QString QGroundControlQmlGlobal::parameterFileExtension() const
{
    return AppSettings::parameterFileExtension;
}

QString QGroundControlQmlGlobal::missionFileExtension() const
{
    return AppSettings::missionFileExtension;
}

QString QGroundControlQmlGlobal::telemetryFileExtension() const
{
    return AppSettings::telemetryFileExtension;
}

QString QGroundControlQmlGlobal::appName()
{
    return QCoreApplication::applicationName();
}

void QGroundControlQmlGlobal::deleteAllSettingsNextBoot()
{
    QGCApplication::deleteAllSettingsNextBoot();
}

void QGroundControlQmlGlobal::clearDeleteAllSettingsNextBoot()
{
    QGCApplication::clearDeleteAllSettingsNextBoot();
}

QStringList QGroundControlQmlGlobal::loggingCategories()
{
    return QGCLoggingCategoryRegister::instance()->registeredCategories();
}

void QGroundControlQmlGlobal::setCategoryLoggingOn(const QString &category, bool enable)
{
    QGCLoggingCategoryRegister::setCategoryLoggingOn(category, enable);
}

bool QGroundControlQmlGlobal::categoryLoggingOn(const QString &category)
{
    return QGCLoggingCategoryRegister::categoryLoggingOn(category);
}

void QGroundControlQmlGlobal::updateLoggingFilterRules()
{
    QGCLoggingCategoryRegister::instance()->setFilterRulesFromSettings(QString());
}
