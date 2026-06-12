/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls




// Used as the base class control for nboth VehicleGPSIndicator and RTKGPSIndicator

Item {
    id:             control
    width:          gpsIndicatorRow.width
    anchors.top:    parent.top
    anchors.bottom: parent.bottom

    property var    _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle
    property bool   _rtkConnected:  QGroundControl.gpsRtk.connected.value
    property string _gps1SatCount:  _activeVehicle ? _activeVehicle.gps.count.valueString : ""
    property string _gps2SatCount:  _activeVehicle ? _activeVehicle.gps2.count.valueString : ""
    property bool   _gpsInputAvailable: _activeVehicle ? _activeVehicle.gpsInput.telemetryAvailable : false
    property string _gpsInputSatCount: _gpsInputAvailable ? _activeVehicle.gpsInput.count.valueString : ""

    function _formatDop(value) {
        return isNaN(value) ? "N/A" : value.toFixed(1)
    }

    function _pdopString(hdopValue, vdopValue) {
        if (isNaN(hdopValue) || isNaN(vdopValue)) {
            return "N/A"
        }

        return Math.sqrt((hdopValue * hdopValue) + (vdopValue * vdopValue)).toFixed(1)
    }

    function _gpsInfoText(label, gpsGroup, satCount) {
        return `${label}: SAT: ${satCount}. PDOP: ${_pdopString(gpsGroup.hdop.value, gpsGroup.vdop.value)}. H_ACC: ${_formatDop(gpsGroup.hdop.value)}. V_ACC: ${_formatDop(gpsGroup.vdop.value)}.`
    }

    Row {
        id:             gpsIndicatorRow
        anchors.top:    parent.top
        anchors.bottom: parent.bottom
        spacing:        ScreenTools.defaultFontPixelWidth / 2

        Row {
            anchors.top:    parent.top
            anchors.bottom: parent.bottom
            spacing:        -ScreenTools.defaultFontPixelWidth / 2

            QGCLabel {
                id:                     gpsLabel
                rotation:               90
                text:                   qsTr("RTK")
                color:                  qgcPal.buttonText
                anchors.verticalCenter: parent.verticalCenter
                visible:                _rtkConnected
            }

            QGCColoredImage {
                id:                 gpsIcon
                width:              height
                anchors.top:        parent.top
                anchors.bottom:     parent.bottom
                source:             "/qmlimages/Gps.svg"
                fillMode:           Image.PreserveAspectFit
                sourceSize.height:  height
                opacity:            (_activeVehicle && _activeVehicle.gps.count.value >= 0) ? 1 : 0.5
                color:              qgcPal.buttonText
            }
        }

        Row {
            id:                     gpsValuesRow
            anchors.verticalCenter: parent.verticalCenter
            visible:                _activeVehicle
            spacing:                ScreenTools.defaultFontPixelWidth

            Column {
                id:                 gpsValuesColumn
                anchors.verticalCenter: parent.verticalCenter
                spacing:            0

                QGCLabel {
                    color: qgcPal.buttonText
                    text: _activeVehicle ? _gpsInfoText("ANT 1", _activeVehicle.gps2, _gps2SatCount) : ""
                }

                QGCLabel {
                    color: qgcPal.buttonText
                    text: _activeVehicle ? _gpsInfoText("ANT 2", _activeVehicle.gps, _gps1SatCount) : ""
                }
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                visible:            _gpsInputAvailable
                spacing:            0

                QGCLabel {
                    color: qgcPal.buttonText
                    text: _gpsInputAvailable ? _gpsInfoText("EXT", _activeVehicle.gpsInput, _gpsInputSatCount) : ""
                }
            }
        }
    }

    MouseArea {
        anchors.fill:   parent
        onClicked:      mainWindow.showIndicatorDrawer(gpsIndicatorPage, control)
    }

    Component {
        id: gpsIndicatorPage

        GPSIndicatorPage { }
    }
}
