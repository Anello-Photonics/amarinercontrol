/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "ParameterEditorController.h"
#include "QGCApplication.h"
#include "ParameterManager.h"
#include "AppSettings.h"
#include "ComponentInformationManager.h"
#include "CompInfoParam.h"
#include "Vehicle.h"
#include "QGCLoggingCategory.h"
#include "MAVLinkProtocol.h"
#include "MultiVehicleManager.h"
#include <QtCore/QRegularExpression>

QGC_LOGGING_CATEGORY(ParameterEditorControllerLog, "qgc.qmlcontrols.parametereditorcontroller")

ParameterTableModel::ParameterTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{

}

ParameterTableModel::~ParameterTableModel()
{
    
}

int ParameterTableModel::rowCount(const QModelIndex& /*parent*/) const
{
    return _tableData.count();
}

int ParameterTableModel::columnCount(const QModelIndex & /*parent*/) const
{
    return _tableViewColCount;
}

QVariant ParameterTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }
    
    if (index.row() < 0 || index.row() >= _tableData.count()) {
        return QVariant();
    }
    if (index.column() < 0 || index.column() >= _tableViewColCount) {
        return QVariant();
    }
    
    switch (role) {
        case Qt::DisplayRole:
            return QVariant::fromValue(_tableData[index.row()][index.column()]);
        case FactRole:
            return QVariant::fromValue(_tableData[index.row()][ValueColumn]);
        default:
            return QVariant();
    }
}

QHash<int, QByteArray> ParameterTableModel::roleNames() const
{
    return { 
        {Qt::DisplayRole, "display"},
        {FactRole, "fact"}
    };
}

void ParameterTableModel::clear()
{
    if (!_externalBeginResetModel) {
        beginResetModel();
    }
    _tableData.clear();
    if (!_externalBeginResetModel) {
        endResetModel();
        emit rowCountChanged(0);
    }
}

void ParameterTableModel::append(Fact* fact)
{
    insert(rowCount(), fact);
}

void ParameterTableModel::insert(int row, Fact* fact)
{
    if (row < 0 || row > rowCount()) {
        qWarning() << "Invalid row row:rowCount" << row << rowCount() << Q_FUNC_INFO;
        row = qMax(qMin(row, rowCount()), 0);
    }

    ColumnData colData(_tableViewColCount, QString());
    colData[NameColumn] = fact->name();
    colData[ValueColumn] = QVariant::fromValue(fact);
    colData[DescriptionColumn] = fact->shortDescription();

    beginInsertRows(QModelIndex(), row, row);
    _tableData.insert(row, colData);
    endInsertRows();

    emit rowCountChanged(rowCount());
}

void ParameterTableModel::beginReset()
{
    if (_externalBeginResetModel) {
        qWarning() << "ParameterTableModel::beginReset already set";
    }
    _externalBeginResetModel = true;
    beginResetModel();
}

void ParameterTableModel::endReset()
{
    if (!_externalBeginResetModel) {
        qWarning() << "ParameterTableModel::endReset begin not set";
    }
    _externalBeginResetModel = false;
    endResetModel();
}

Fact* ParameterTableModel::factAt(int row) const
{
    if (row < 0 || row >= _tableData.count()) {
        qWarning() << "Invalid row row:rowCount" << row << _tableData.count() << Q_FUNC_INFO;
        return nullptr;
    }

    return _tableData[row][ValueColumn].value<Fact*>();
}


ParameterEditorGroup::ParameterEditorGroup(QObject* parent) 
    : QObject(parent) 
{ 

}

ParameterEditorController::ParameterEditorController(QObject *parent)
    : FactPanelController(parent)
    , _parameterMgr(_vehicle->parameterManager())
{
    // qCDebug(ParameterEditorControllerLog) << Q_FUNC_INFO << this;

    _searchTimer.setSingleShot(true);
    _searchTimer.setInterval(300);

    connect(this, &ParameterEditorController::currentCategoryChanged,   this, &ParameterEditorController::_currentCategoryChanged);
    connect(this, &ParameterEditorController::currentGroupChanged,      this, &ParameterEditorController::_currentGroupChanged);
    connect(this, &ParameterEditorController::searchTextChanged,        this, &ParameterEditorController::_searchTextChanged);
    connect(this, &ParameterEditorController::showModifiedOnlyChanged,  this, &ParameterEditorController::_searchTextChanged);
    connect(&_searchTimer, &QTimer::timeout,                            this, &ParameterEditorController::_performSearch);
    connect(_parameterMgr, &ParameterManager::factAdded,                this, &ParameterEditorController::_factAdded);
    connect(_parameterMgr, &ParameterManager::parametersReadyChanged,   this, &ParameterEditorController::_buildListsIfReady);

    _presetResetTimer.setSingleShot(true);
    _presetResetTimer.setInterval(10000);
    connect(&_presetResetTimer, &QTimer::timeout, this, [this]() {
        _finishPresetReset(tr("Timed out waiting for the INS shell."));
    });
    connect(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged, this, [this](Vehicle *vehicle) {
        if (_presetResetTimer.isActive() && vehicle != _presetResetVehicle) {
            _finishPresetReset(tr("The active vehicle changed."));
        }
    });
    _buildListsIfReady(_parameterMgr->parametersReady());
}

ParameterEditorController::~ParameterEditorController()
{
    if (_presetResetVehicle) { _sendPresetShell({}, true); }
    // qCDebug(ParameterEditorControllerLog) << Q_FUNC_INFO << this;
}

bool ParameterEditorController::shouldWarnAllMavlinkDisabled(Fact *editedFact, const QVariant &editedValue) const
{
    if (!editedFact || !_isMavlinkConfigFact(editedFact)) {
        return false;
    }

    static const QStringList mavlinkConfigParams {
        QStringLiteral("MAV_0_CONFIG"),
        QStringLiteral("MAV_2_CONFIG"),
    };

    for (const QString &parameterName : mavlinkConfigParams) {
        if (!_parameterMgr->parameterExists(_vehicle->defaultComponentId(), parameterName)) {
            continue;
        }

        Fact *const fact = _parameterMgr->getParameter(_vehicle->defaultComponentId(), parameterName);
        if (!_isMavlinkConfigFact(fact)) {
            continue;
        }

        const QVariant currentValue = (fact == editedFact) ? editedValue : fact->rawValue();
        if (!_isMavlinkConfigDisabled(fact, currentValue)) {
            return false;
        }
    }

    return true;
}

void ParameterEditorController::_buildListsForComponent(int compId)
{
    for (const QString& factName: _parameterMgr->parameterNames(compId)) {
        Fact* fact = _parameterMgr->getParameter(compId, factName);
        _refreshFactMetaData(compId, fact);
        _watchSecondaryUdpConfig(fact);

        if (_shouldHideFromGroupList(fact)) {
            continue;
        }

        const QString categoryName = _parameterDisplayCategory(fact);
        const QString groupName = _parameterDisplayGroup(fact);

        ParameterEditorCategory* category = nullptr;
        if (_mapCategoryName2Category.contains(categoryName)) {
            category = _mapCategoryName2Category[categoryName];
        } else {
            category        = new ParameterEditorCategory(this);
            category->name  = categoryName;
            _mapCategoryName2Category[categoryName] = category;
            _categories.append(category);
        }

        ParameterEditorGroup* group = nullptr;
        if (category->mapGroupName2Group.contains(groupName)) {
            group = category->mapGroupName2Group[groupName];
        } else {
            group               = new ParameterEditorGroup(this);
            group->componentId  = compId;
            group->name         = groupName;
            category->mapGroupName2Group[groupName] = group;
            category->groups.append(group);
        }

        group->facts.append(fact);
    }
}

void ParameterEditorController::_buildLists(void)
{
    if (_listsBuilt) {
        return;
    }

    _listsBuilt = true;

    // Autopilot component should always be first list
    _buildListsForComponent(MAV_COMP_ID_AUTOPILOT1);

    // "Standard" category should always be first
    for (int i=0; i<_categories.count(); i++) {
        ParameterEditorCategory* category = _categories.value<ParameterEditorCategory*>(i);
        if (category->name == "Standard" && i != 0) {
            _categories.removeAt(i);
            _categories.insert(0, category);
            break;
        }
    }

    // Default category should always be last
    for (int i=0; i<_categories.count(); i++) {
        ParameterEditorCategory* category = _categories.value<ParameterEditorCategory*>(i);
        if (category->name == FactMetaData::kDefaultCategory) {
            if (i != _categories.count() - 1) {
                _categories.removeAt(i);
                _categories.append(category);
            }
            break;
        }
    }

    // Now add other random components
    for (int compId: _parameterMgr->componentIds()) {
        if (compId != MAV_COMP_ID_AUTOPILOT1) {
            _buildListsForComponent(compId);
        }
    }

    // Default group should always be last
    for (int i=0; i<_categories.count(); i++) {
        ParameterEditorCategory* category = _categories.value<ParameterEditorCategory*>(i);
        for (int j=0; j<category->groups.count(); j++) {
            ParameterEditorGroup* group = category->groups.value<ParameterEditorGroup*>(j);
            if (group->name == FactMetaData::kDefaultGroup) {
                if (j != _categories.count() - 1) {
                    category->groups.removeAt(j);
                    category->groups.append(group);
                }
                break;
            }
        }
    }
}


void ParameterEditorController::_buildListsIfReady(bool parametersReady)
{
    if (!parametersReady || _listsBuilt) {
        return;
    }

    _buildLists();

    ParameterEditorCategory* category = _categories.count() ? _categories.value<ParameterEditorCategory*>(0) : nullptr;
    setCurrentCategory(category);
}

void ParameterEditorController::_factAdded(int compId, Fact* fact)
{
    _watchSecondaryUdpConfig(fact);
    if (!_listsBuilt) {
        return;
    }

    _refreshFactMetaData(compId, fact);

    if (_shouldHideFromGroupList(fact)) {
        return;
    }

    const QString categoryName = _parameterDisplayCategory(fact);
    const QString groupName = _parameterDisplayGroup(fact);

    bool                        inserted = false;
    ParameterEditorCategory*    category = nullptr;

    if (_mapCategoryName2Category.contains(categoryName)) {
        category = _mapCategoryName2Category[categoryName];
    } else {
        category        = new ParameterEditorCategory(this);
        category->name  = categoryName;
        _mapCategoryName2Category[categoryName] = category;

        // Insert in sorted order
        inserted = false;
        for (int i=0; i<_categories.count(); i++) {
            if (_categories.value<ParameterEditorCategory*>(i)->name > category->name) {
                _categories.insert(i, category);
                inserted = true;
                break;
            }
        }
        if (!inserted) {
            _categories.append(category);
        }
    }

    ParameterEditorGroup* group = nullptr;
    if (category->mapGroupName2Group.contains(groupName)) {
        group = category->mapGroupName2Group[groupName];
    } else {
        group               = new ParameterEditorGroup(this);
        group->componentId  = compId;
        group->name         = groupName;
        category->mapGroupName2Group[groupName] = group;

        // Insert in sorted order
        QmlObjectListModel& groups = category->groups;
        inserted = false;
        for (int i=0; i<groups.count(); i++) {
            if (groups.value<ParameterEditorGroup*>(i)->name > group->name) {
                groups.insert(i, group);
                inserted = true;
                break;
            }
        }
        if (!inserted) {
            groups.append(group);
        }
    }

    // Insert in sorted order
    auto& facts = group->facts;
    for (int i=0; i<facts.rowCount(); i++) {
        if (facts.factAt(i)->name() > fact->name()) {
            facts.insert(i, fact);
            return;
        }
    }
    facts.append(fact);
}

void ParameterEditorController::saveToFile(const QString& filename)
{
    if (!filename.isEmpty()) {
        QString parameterFilename = filename;
        if (!QFileInfo(filename).fileName().contains(".")) {
            parameterFilename += QString(".%1").arg(AppSettings::parameterFileExtension);
        }

        QFile file(parameterFilename);

        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            qgcApp()->showAppMessage(tr("Unable to create file: %1").arg(parameterFilename));
            return;
        }

        QTextStream stream(&file);
        _parameterMgr->writeParametersToStream(stream);
        file.close();
    }
}

void ParameterEditorController::clearDiff(void)
{
    _diffList.clearAndDeleteContents();
    _diffOtherVehicle = false;
    _diffMultipleComponents = false;

    emit diffOtherVehicleChanged(_diffOtherVehicle);
    emit diffMultipleComponentsChanged(_diffMultipleComponents);
}

static bool isUnitIdentityParameter(const QString &name)
{
    return name == QStringLiteral("IMU_MB_C_SN") || name == QStringLiteral("IMU_MB_C_YR");
}

void ParameterEditorController::sendDiff(void)
{
    for (int i=0; i<_diffList.count(); i++) {
        ParameterEditorDiff* paramDiff = _diffList.value<ParameterEditorDiff*>(i);

        // Unit identity must never be copied from another unit's parameter file,
        // even if a protected entry somehow reaches the review model.
        if (paramDiff->load && !isUnitIdentityParameter(paramDiff->name)) {
            if (paramDiff->noVehicleValue) {
                _parameterMgr->_factRawValueUpdateWorker(paramDiff->componentId, paramDiff->name, paramDiff->valueType, paramDiff->fileValueVar);
            } else {
                Fact* fact = _parameterMgr->getParameter(paramDiff->componentId, paramDiff->name);
                fact->setRawValue(paramDiff->fileValueVar);
            }
        }
    }
}

bool ParameterEditorController::buildDiffFromFile(const QString& filename)
{
    QFile file(filename);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qgcApp()->showAppMessage(tr("Unable to open file: %1").arg(filename));
        return false;
    }

    clearDiff();

    QTextStream stream(&file);

    int firstComponentId = -1;
    while (!stream.atEnd()) {
        QString line = stream.readLine();
        if (!line.startsWith("#")) {
            QStringList wpParams = line.split("\t");
            if (wpParams.size() == 5) {
                int         vehicleId       = wpParams.at(0).toInt();
                int         componentId     = wpParams.at(1).toInt();
                QString     paramName       = wpParams.at(2);
                if (isUnitIdentityParameter(paramName)) {
                    continue;
                }
                QString     fileValueStr    = wpParams.at(3);
                int         mavParamType    = wpParams.at(4).toInt();
                QString     vehicleValueStr;
                QString     units;
                QVariant    fileValueVar    = fileValueStr;
                bool        noVehicleValue   = false;
                bool        readOnly         = false;

                if (_vehicle->id() != vehicleId) {
                    _diffOtherVehicle = true;
                }
                if (firstComponentId == -1) {
                    firstComponentId = componentId;
                } else if (firstComponentId != componentId) {
                    _diffMultipleComponents = true;
                }

                if (_parameterMgr->parameterExists(componentId, paramName)) {
                    Fact*           vehicleFact         = _parameterMgr->getParameter(componentId, paramName);
                    FactMetaData*   vehicleFactMetaData = vehicleFact->metaData();
                    Fact*           fileFact            = new Fact(vehicleFact->componentId(), vehicleFact->name(), vehicleFact->type(), this);

                    // Turn off reboot messaging before setting value in fileFact
                    bool vehicleRebootRequired = vehicleFactMetaData->vehicleRebootRequired();
                    vehicleFactMetaData->setVehicleRebootRequired(false);
                    fileFact->setMetaData(vehicleFact->metaData());
                    fileFact->setRawValue(fileValueStr);
                    vehicleFactMetaData->setVehicleRebootRequired(vehicleRebootRequired);
                    readOnly = vehicleFact->readOnly();

                    if (vehicleFact->rawValue() == fileFact->rawValue()) {
                        continue;
                    }
                    fileValueStr    = fileFact->enumOrValueString();
                    fileValueVar    = fileFact->rawValue();
                    vehicleValueStr = vehicleFact->enumOrValueString();
                    units           = vehicleFact->cookedUnits();
                } else {
                    noVehicleValue = true;
                }

                if (!readOnly) {
                    ParameterEditorDiff* paramDiff = new ParameterEditorDiff(this);

                    paramDiff->componentId      = componentId;
                    paramDiff->name             = paramName;
                    paramDiff->valueType        = ParameterManager::mavTypeToFactType(static_cast<MAV_PARAM_TYPE>(mavParamType));
                    paramDiff->fileValue        = fileValueStr;
                    paramDiff->fileValueVar     = fileValueVar;
                    paramDiff->vehicleValue     = vehicleValueStr;
                    paramDiff->noVehicleValue   = noVehicleValue;
                    paramDiff->units            = units;

                    _diffList.append(paramDiff);
                }
            }
        }
    }

    file.close();

    emit diffOtherVehicleChanged(_diffOtherVehicle);
    emit diffMultipleComponentsChanged(_diffMultipleComponents);

    return true;
}

void ParameterEditorController::refresh(void)
{
    _parameterMgr->refreshAllParameters();
}


void ParameterEditorController::resetToMaritimeDefaults()
{
    // Uses the Maritime reset script preset, with only four explicit reset exceptions.
    // Also applies the Ethernet defaults from Maritime_INS_CFG.py before saving.
    const QStringList commands = {
        QStringLiteral("param reset_all IMU_MB_C_SN IMU_MB_C_YR IMU_MB_C_FACTORY IMU_MB_C_FTOG"),
        QStringLiteral("param set IMU_MB_C_FACTORY 0"),
        QStringLiteral("param set IMU_MB_C_FTOG 1"),
        QStringLiteral("param set SYS_AUTOSTART 60009"),
        QStringLiteral("param set NM2K_CFG 1"),
        QStringLiteral("param set J1939_CFG 0"),
        QStringLiteral("param set NM2K_BITRATE 250000"),
        QStringLiteral("param set NM2K_127257_RATE 10"),
        QStringLiteral("param set CAN_TERM 1"),
        QStringLiteral("param set NM0183_CFG 2"),
        QStringLiteral("param set MAV_0_CONFIG 101"),
        QStringLiteral("param set SER_TEL1_BAUD 57600"),
        QStringLiteral("param set SER_TEL2_BAUD 921600"),
        QStringLiteral("param set GPS_SEP_BASE_X 0.0"),
        QStringLiteral("param set GPS_SEP_BASE_Y 0.0"),
        QStringLiteral("param set GPS_SEP_BASE_Z 0.0"),
        QStringLiteral("param set GPS_SEP_ROVER_X 0.0"),
        QStringLiteral("param set GPS_SEP_ROVER_Y 0.0"),
        QStringLiteral("param set GPS_SEP_ROVER_Z 0.0"),
        QStringLiteral("param set GPS_EXT_X 0.0"),
        QStringLiteral("param set GPS_EXT_Y 0.0"),
        QStringLiteral("param set GPS_EXT_Z 0.0"),
        QStringLiteral("param set EKF2_IMU_POS_X 0.0"),
        QStringLiteral("param set EKF2_IMU_POS_Y 0.0"),
        QStringLiteral("param set EKF2_IMU_POS_Z 0.0"),
        QStringLiteral("param set EKF2_WTSPD_POS_X 0.0"),
        QStringLiteral("param set EKF2_WTSPD_POS_Y 0.0"),
        QStringLiteral("param set EKF2_WTSPD_POS_Z 0.0"),
        QStringLiteral("param set SENS_BOARD_ROT 0"),
        QStringLiteral("param set NMUDP_EN 1"),
        QStringLiteral("param set NMUDP_ODR_GGA 0.0"),
        QStringLiteral("param set NMUDP_ODR_RMC 0.0"),
        QStringLiteral("param set NMUDP_ODR_APIMU 10.0"),
        QStringLiteral("param set NMUDP_ODR_APINS 0.0"),
        QStringLiteral("param set NMUDP_ODR_APACC 0.0"),
        QStringLiteral("param set NMUDP_ODR_HDT 0.0"),
        QStringLiteral("param set NMUDP_ODR_XDR 0.0"),
        QStringLiteral("param set NMUDP_ODR_ZDA 0.0"),
        QStringLiteral("param set NM0183_ODR_GGA 0.0"),
        QStringLiteral("param set NM0183_ODR_RMC 0.0"),
        QStringLiteral("param set NM0183_ODR_APIMU 10.0"),
        QStringLiteral("param set NM0183_ODR_APINS 0.0"),
        QStringLiteral("param set NM0183_ODR_APACC 0.0"),
        QStringLiteral("param set NM0183_ODR_HDT 0.0"),
        QStringLiteral("param set NM0183_ODR_XDR 0.0"),
        QStringLiteral("param set NM0183_ODR_ZDA 0.0"),
        QStringLiteral("param set NMUDP_MC_IP0 0"),
        QStringLiteral("param set NMUDP_MC_IP1 0"),
        QStringLiteral("param set NMUDP_MC_IP2 0"),
        QStringLiteral("param set NMUDP_MC_IP3 0"),
        QStringLiteral("param set NMUDP_UC_IP0 0"),
        QStringLiteral("param set NMUDP_UC_IP1 0"),
        QStringLiteral("param set NMUDP_UC_IP2 0"),
        QStringLiteral("param set NMUDP_UC_IP3 0"),
        QStringLiteral("param set MAV_2_BROADCAST 1"),
        QStringLiteral("param set MAV_2_CONFIG 1000"),
        QStringLiteral("param set EKF2_WTSPD_SF0 0.0"),
        QStringLiteral("param set EKF2_WTSPD_SF1 1.0"),
        QStringLiteral("param set EKF2_WTSPD_SF2 0.0"),
        QStringLiteral("param set EKF2_WTSPD_DBN -1.0"),
        QStringLiteral("param set EKF2_ENG_CTRL 0.0"),
        QStringLiteral("param set EKF2_ENG_STAT_L 0.0"),
        QStringLiteral("param set EKF2_ENG_STAT_H 0.0"),
        QStringLiteral("param set SDLOG_DIRS_MAX 200"),
        QStringLiteral("param set SDLOG_FILES_MAX 200"),
    };
    _startPresetReset(commands, tr("Maritime"));
}

void ParameterEditorController::resetToAerialDefaults()
{
    // Final_Configs_Aerial.py configuration, preserving the existing manufacturing
    // serial number/year. Other MB calibration resets; FACTORY/FTOG are set below.
    const QStringList commands = {
        QStringLiteral("param reset_all IMU_MB_C_SN IMU_MB_C_YR IMU_MB_C_FACTORY IMU_MB_C_FTOG"),
        QStringLiteral("param set IMU_MB_C_FACTORY 0"),
        QStringLiteral("param set IMU_MB_C_FTOG 1"),
        QStringLiteral("param set SYS_AUTOSTART 80000"),
        QStringLiteral("param set NM2K_CFG 0"),
        QStringLiteral("param set J1939_CFG 0"),
        QStringLiteral("param set QNODE_CFG 0"),
        QStringLiteral("param set CAN_TERM 1"),
        QStringLiteral("param set NM0183_CFG 2"),
        QStringLiteral("param set MAV_0_CONFIG 101"),
        QStringLiteral("param set SER_TEL1_BAUD 57600"),
        QStringLiteral("param set SER_TEL2_BAUD 921600"),
        QStringLiteral("param set GPS_SEP_BASE_X 0.0"),
        QStringLiteral("param set GPS_SEP_BASE_Y 0.0"),
        QStringLiteral("param set GPS_SEP_BASE_Z 0.0"),
        QStringLiteral("param set GPS_SEP_ROVER_X 0.0"),
        QStringLiteral("param set GPS_SEP_ROVER_Y 0.0"),
        QStringLiteral("param set GPS_SEP_ROVER_Z 0.0"),
        QStringLiteral("param set EKF2_IMU_POS_X 0.0"),
        QStringLiteral("param set EKF2_IMU_POS_Y 0.0"),
        QStringLiteral("param set EKF2_IMU_POS_Z 0.0"),
        QStringLiteral("param set SENS_BOARD_ROT 0"),
        QStringLiteral("param set NMUDP_EN 1"),
        QStringLiteral("param set NMUDP_ODR_GGA 0.0"),
        QStringLiteral("param set NMUDP_ODR_RMC 0.0"),
        QStringLiteral("param set NMUDP_ODR_VTG 0.0"),
        QStringLiteral("param set NMUDP_ODR_APIMU 10.0"),
        QStringLiteral("param set NMUDP_ODR_APINS 0.0"),
        QStringLiteral("param set NM0183_ODR_GGA 0.0"),
        QStringLiteral("param set NM0183_ODR_RMC 0.0"),
        QStringLiteral("param set NM0183_ODR_VTG 0.0"),
        QStringLiteral("param set NM0183_ODR_APIMU 10.0"),
        QStringLiteral("param set NM0183_ODR_APINS 0.0"),
        QStringLiteral("param set NMUDP_MC_IP0 0"),
        QStringLiteral("param set NMUDP_MC_IP1 0"),
        QStringLiteral("param set NMUDP_MC_IP2 0"),
        QStringLiteral("param set NMUDP_MC_IP3 0"),
        QStringLiteral("param set MAV_2_BROADCAST 1"),
        QStringLiteral("param set MAV_2_CONFIG 1000"),
        QStringLiteral("param set EKF2_WTSPD_SF0 0.0"),
        QStringLiteral("param set EKF2_WTSPD_SF1 1.0"),
        QStringLiteral("param set EKF2_WTSPD_SF2 0.0"),
        QStringLiteral("param set EKF2_WTSPD_DBN -1.0"),
        QStringLiteral("param set EKF2_ENG_CTRL 0.0"),
        QStringLiteral("param set EKF2_ENG_STAT_L 0.0"),
        QStringLiteral("param set EKF2_ENG_STAT_H 0.0"),
        QStringLiteral("param set SDLOG_DIRS_MAX 200"),
    };
    _startPresetReset(commands, tr("Aerial"));
}

void ParameterEditorController::_startPresetReset(const QStringList &commands, const QString &presetName)
{
    if (presetResetInProgress()) { return; }
    if (!_vehicle || _vehicle != MultiVehicleManager::instance()->activeVehicle()
        || !_vehicle->px4Firmware() || _vehicle->isOfflineEditingVehicle() || _vehicle->armed()
        || !_vehicle->vehicleLinkManager()->primaryLink().lock()) {
        emit presetResetFinished(tr("Connect to a disarmed PX4 INS before resetting."));
        return;
    }
    _presetName = presetName;
    _presetCommands = commands;
    // Shared Ethernet defaults from Maritime_INS_CFG.py and Final_Configs_Aerial.py.
    _presetCommands.append(QStringList {
        // Static Ethernet defaults; IPv4 values use the script's signed int32 encoding.
        QStringLiteral("param set NET_CFG_PROTO 1"), // 1
        QStringLiteral("param set NET_CFG_IPADDR -1062731773"), // 192.168.0.3
        QStringLiteral("param set NET_CFG_NETMASK -256"), // 255.255.255.0
        QStringLiteral("param set NET_CFG_ROUTER -1062731522"), // 192.168.0.254
        QStringLiteral("param set NET_CFG_DNS -1062731522"), // 192.168.0.254
        QStringLiteral("param save"),
    });
    _presetResetVehicle = _vehicle;
    _presetCommandIndex = -1;
    _presetOutput.clear();
    _presetResetStatus = tr("Connecting to INS shell...");
    _presetReceiveConnection = connect(_vehicle, &Vehicle::mavlinkSerialControl, this,
        [this](uint8_t device, uint8_t, uint16_t, uint32_t, const QByteArray &data) {
            if (device != SERIAL_CONTROL_DEV_SHELL || !presetResetInProgress()) { return; }
            _presetOutput.append(data);
            if (!_presetOutput.contains("nsh> ")) { return; }
            const QString output = QString::fromUtf8(_presetOutput);
            static const QRegularExpression failure(QStringLiteral("error|failed|not found|invalid|unknown command"),
                                                      QRegularExpression::CaseInsensitiveOption);
            if (_presetCommandIndex >= 0 && output.contains(failure)) {
                _finishPresetReset(tr("Command failed: %1\n%2").arg(_presetCommands[_presetCommandIndex], output));
                return;
            }
            _nextPresetCommand();
        });
    _presetResetTimer.start();
    emit presetResetChanged();
    if (!_sendPresetShell("\n")) { _finishPresetReset(tr("Unable to reach the INS shell.")); }
}

void ParameterEditorController::rebootViaShell()
{
    if (presetResetInProgress()) {
        return;
    }
    if (!_vehicle || _vehicle != MultiVehicleManager::instance()->activeVehicle()
        || _vehicle->isOfflineEditingVehicle() || _vehicle->armed()) {
        emit shellRebootError(tr("Connect to a disarmed INS before rebooting."));
        return;
    }
    // Send the same command as the MAVLink console. The leading newline wakes
    // the shell; reboot does not return a prompt or a MAV_CMD acknowledgement.
    if (!_sendShell(_vehicle, "\nreboot\n")) {
        emit shellRebootError(tr("Unable to send reboot to the INS shell."));
        return;
    }
    _shellRebootPending = true;
    _presetResetStatus = tr("Reboot command sent. Waiting for the INS to restart...");
    emit presetResetChanged();
    const QPointer<Vehicle> target = _vehicle;
    QTimer::singleShot(1000, this, [this, target]() {
        if (target) {
            _sendShell(target.data(), {}, true);
        }
        _shellRebootPending = false;
        _presetResetStatus.clear();
        emit presetResetChanged();
    });
}

bool ParameterEditorController::_sendPresetShell(const QByteArray &data, bool close)
{
    return _sendShell(_presetResetVehicle.data(), data, close);
}

bool ParameterEditorController::_sendShell(Vehicle *vehicle, const QByteArray &data, bool close)
{
    if (!vehicle) { return false; }
    auto link = vehicle->vehicleLinkManager()->primaryLink().lock();
    if (!link) { return false; }
    // Shell commands can span SERIAL_CONTROL packets. Preserve the newline at
    // the end, and also send a zero-length packet when releasing the shell.
    qsizetype offset = 0;
    do {
        QByteArray payload = data.mid(offset, MAVLINK_MSG_SERIAL_CONTROL_FIELD_DATA_LEN);
        const uint8_t count = static_cast<uint8_t>(payload.size());
        payload.append(MAVLINK_MSG_SERIAL_CONTROL_FIELD_DATA_LEN - payload.size(), '\0');
        mavlink_message_t message;
        mavlink_msg_serial_control_pack_chan(
            MAVLinkProtocol::instance()->getSystemId(), MAVLinkProtocol::getComponentId(),
            link->mavlinkChannel(), &message, SERIAL_CONTROL_DEV_SHELL,
            close ? 0 : SERIAL_CONTROL_FLAG_EXCLUSIVE | SERIAL_CONTROL_FLAG_RESPOND | SERIAL_CONTROL_FLAG_MULTI,
            0, 0, count, reinterpret_cast<const uint8_t *>(payload.constData()),
            vehicle->id(), vehicle->defaultComponentId());
        if (!vehicle->sendMessageOnLinkThreadSafe(link.get(), message)) {
            return false;
        }
        offset += count;
    } while (offset < data.size());
    return true;
}

void ParameterEditorController::_nextPresetCommand()
{
    if (!_presetResetVehicle || _presetResetVehicle->armed()) {
        _finishPresetReset(tr("The INS disconnected or became armed."));
        return;
    }
    if (++_presetCommandIndex == _presetCommands.size()) {
        _finishPresetReset();
        return;
    }
    _presetOutput.clear();
    _presetResetStatus = tr("%1/%2: %3").arg(_presetCommandIndex + 1)
        .arg(_presetCommands.size()).arg(_presetCommands[_presetCommandIndex]);
    emit presetResetChanged();
    _presetResetTimer.start();
    if (!_sendPresetShell(_presetCommands[_presetCommandIndex].toUtf8() + '\n')) {
        _finishPresetReset(tr("Unable to send the command to the INS."));
    }
}

void ParameterEditorController::_finishPresetReset(const QString &error)
{
    _presetResetTimer.stop();
    disconnect(_presetReceiveConnection);
    _sendPresetShell({}, true);
    const bool canRefresh = _presetResetVehicle && _presetResetVehicle == MultiVehicleManager::instance()->activeVehicle();
    _presetResetVehicle.clear();
    _presetResetStatus.clear();
    emit presetResetChanged();
    if (canRefresh) { refresh(); }
    emit presetResetFinished(error.isEmpty()
        ? tr("%1 INS defaults saved. Reboot the INS for all settings to take effect.").arg(_presetName)
        : tr("Reset stopped. Some settings may already have changed.\n%1").arg(error));
}

void ParameterEditorController::resetAllToDefaults(void)
{
    _parameterMgr->resetAllParametersToDefaults();
    refresh();
}

void ParameterEditorController::resetAllToVehicleConfiguration(void)
{
    _parameterMgr->resetAllToVehicleConfiguration();
    refresh();
}


void ParameterEditorController::_refreshFactMetaData(int compId, Fact *fact) const
{
    if (!fact || !_vehicle || !_vehicle->compInfoManager()) {
        return;
    }

    CompInfoParam *const compInfoParam = _vehicle->compInfoManager()->compInfoParam(static_cast<uint8_t>(compId));
    if (!compInfoParam) {
        return;
    }

    FactMetaData *const factMetaData = compInfoParam->factMetaDataForName(fact->name(), fact->type());
    if (factMetaData && factMetaData != fact->metaData()) {
        fact->setMetaData(factMetaData);
    }
}


QString ParameterEditorController::_parameterDisplayCategory(Fact *fact) const
{
    if (!fact) {
        return QString();
    }

    const QString name = fact->name();
    if ((name == QStringLiteral("CAN_TERM")) || name.startsWith(QStringLiteral("NM2K_")) || name.startsWith(QStringLiteral("NMEA")) || name.startsWith(QStringLiteral("NM0183_")) || name.startsWith(QStringLiteral("NMUDP_")) || name.startsWith(QStringLiteral("NM_GNSS_"))) {
        return QStringLiteral("Standard");
    }

    return fact->category();
}

QString ParameterEditorController::_parameterDisplayGroup(Fact *fact) const
{
    if (!fact) {
        return QString();
    }

    const QString name = fact->name();
    if ((name == QStringLiteral("CAN_TERM")) || name.startsWith(QStringLiteral("NM2K_"))) {
        return QStringLiteral("NMEA2000");
    }
    if (name.startsWith(QStringLiteral("NM0183_")) || name.startsWith(QStringLiteral("NMUDP_")) || name.startsWith(QStringLiteral("NM_GNSS_"))) {
        return QStringLiteral("NMEA0183");
    }

    return fact->group();
}

bool ParameterEditorController::_secondaryUdpSettingsVisible(Fact *fact) const
{
    if (!fact) {
        return false;
    }
    const QString configName = QStringLiteral("NMUDP_2_EN");
    if (!fact->name().startsWith(QStringLiteral("NMUDP_2_")) || fact->name() == configName) {
        return true;
    }
    return _parameterMgr->parameterExists(fact->componentId(), configName)
        && _parameterMgr->getParameter(fact->componentId(), configName)->rawValue().toInt() == 1;
}

void ParameterEditorController::_watchSecondaryUdpConfig(Fact *fact)
{
    if (fact && fact->name() == QStringLiteral("NMUDP_2_EN")) {
        // Defer model changes until the parameter editor has handled the value change.
        connect(fact, &Fact::rawValueChanged, this, &ParameterEditorController::_secondaryUdpConfigChanged,
                static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
        QMetaObject::invokeMethod(this, &ParameterEditorController::_secondaryUdpConfigChanged, Qt::QueuedConnection);
    }
}

void ParameterEditorController::_secondaryUdpConfigChanged()
{
    if (!_listsBuilt) {
        return;
    }
    // Keep the existing groups and selection; only repopulate their visible rows.
    for (int i = 0; i < _categories.count(); ++i) {
        auto *category = _categories.value<ParameterEditorCategory*>(i);
        for (int j = 0; j < category->groups.count(); ++j) {
            auto *group = category->groups.value<ParameterEditorGroup*>(j);
            if (group->name != QStringLiteral("NMEA0183")) {
                continue;
            }
            group->facts.beginReset();
            group->facts.clear();
            for (const QString &name : _parameterMgr->parameterNames(group->componentId)) {
                Fact *fact = _parameterMgr->getParameter(group->componentId, name);
                if (!_shouldHideFromGroupList(fact)
                    && _parameterDisplayCategory(fact) == category->name
                    && _parameterDisplayGroup(fact) == group->name) {
                    group->facts.append(fact);
                }
            }
            group->facts.endReset();
        }
    }
    if (!_searchText.isEmpty() || _showModifiedOnly) {
        _performSearch();
    }
}

bool ParameterEditorController::_shouldHideFromGroupList(Fact *fact) const
{
    return !_secondaryUdpSettingsVisible(fact) || (_parameterDisplayGroup(fact) == QStringLiteral("Battery Calibration"));
}

bool ParameterEditorController::_shouldShow(Fact* fact) const
{
    if (!_secondaryUdpSettingsVisible(fact)) {
        return false;
    }

    if (!_showModifiedOnly) {
        return true;
    }

    return fact->defaultValueAvailable() && !fact->valueEqualsDefault();
}

void ParameterEditorController::_searchTextChanged(void)
{
    _searchTimer.start();
}

void ParameterEditorController::_performSearch(void)
{
    QObjectList newParameterList;

    QStringList rgSearchStrings = _searchText.split(' ', Qt::SkipEmptyParts);

    if (rgSearchStrings.isEmpty() && !_showModifiedOnly) {
        ParameterEditorCategory* category = _categories.count() ? _categories.value<ParameterEditorCategory*>(0) : nullptr;
        setCurrentCategory(category);
        _searchParameters.clear();
    } else {
        QVector<QRegularExpression> regexList;
        regexList.reserve(rgSearchStrings.size());
        for (const QString &searchItem : rgSearchStrings) {
            QRegularExpression re(searchItem, QRegularExpression::CaseInsensitiveOption);
            regexList.append(re.isValid() ? re : QRegularExpression());
        }

        _searchParameters.beginReset();
        _searchParameters.clear();

        for (const QString &paraName: _parameterMgr->parameterNames(_vehicle->defaultComponentId())) {
            Fact* fact = _parameterMgr->getParameter(_vehicle->defaultComponentId(), paraName);
            bool matched = _shouldShow(fact);
            // All of the search items must match in order for the parameter to be added to the list
            if (matched) {
                for (int i = 0; i < rgSearchStrings.size(); ++i) {
                    const QRegularExpression &re = regexList.at(i);
                    if (re.isValid()) {
                        if (!fact->name().contains(re) &&
                                !fact->shortDescription().contains(re) &&
                                !fact->longDescription().contains(re)) {
                            matched = false;
                        }
                    } else {
                        const QString &searchItem = rgSearchStrings.at(i);
                        if (!fact->name().contains(searchItem, Qt::CaseInsensitive) &&
                                !fact->shortDescription().contains(searchItem, Qt::CaseInsensitive) &&
                                !fact->longDescription().contains(searchItem, Qt::CaseInsensitive)) {
                            matched = false;
                        }                    
                    }
                }
            }
            if (matched) {
                _searchParameters.append(fact);
            }
        }

        _searchParameters.endReset();

        if (_parameters != &_searchParameters) {
            _parameters = &_searchParameters;
            emit parametersChanged();

            _currentCategory    = nullptr;
            _currentGroup       = nullptr;
        }
    }
}

void ParameterEditorController::_currentCategoryChanged(void)
{
    ParameterEditorGroup* group = nullptr;
    if (_currentCategory) {
        // Select first group when category changes
        group = _currentCategory->groups.value<ParameterEditorGroup*>(0);
    } else {
        group = nullptr;
    }
    setCurrentGroup(group);
}

void ParameterEditorController::_currentGroupChanged(void)
{
    _parameters = _currentGroup ? &_currentGroup->facts : nullptr;
    emit parametersChanged();
}

void ParameterEditorController::setCurrentCategory(QObject* currentCategory)
{
    ParameterEditorCategory* category = qobject_cast<ParameterEditorCategory*>(currentCategory);
    if (category != _currentCategory) {
        _currentCategory = category;
        emit currentCategoryChanged();
    }
}

void ParameterEditorController::setCurrentGroup(QObject* currentGroup)
{
    ParameterEditorGroup* group = qobject_cast<ParameterEditorGroup*>(currentGroup);
    if (group != _currentGroup) {
        _currentGroup = group;
        emit currentGroupChanged();
    }
}

bool ParameterEditorController::_isMavlinkConfigFact(Fact *fact) const
{
    return fact && ((fact->name() == QStringLiteral("MAV_0_CONFIG")) || (fact->name() == QStringLiteral("MAV_2_CONFIG")));
}

bool ParameterEditorController::_isMavlinkConfigDisabled(Fact *fact, const QVariant &value) const
{
    if (!fact) {
        return false;
    }

    const QString targetValue = value.toString();
    const QStringList enumStrings = fact->enumStrings();
    const QVariantList enumValues = fact->enumValues();
    const int enumCount = qMin(enumStrings.size(), enumValues.size());

    for (int i = 0; i < enumCount; i++) {
        if (enumValues[i].toString() == targetValue) {
            return enumStrings[i].contains(QStringLiteral("Disabled"), Qt::CaseInsensitive) ||
                   enumStrings[i].contains(QStringLiteral("Disable"), Qt::CaseInsensitive);
        }
    }

    // Fallback for non-enum metadata formats.
    return targetValue == QStringLiteral("0");
}
