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

    function _formatDop(value) {
        return isNaN(value) ? "N/A" : value.toFixed(1)
    }

    function _pdopString(hdopValue, vdopValue) {
        if (isNaN(hdopValue) || isNaN(vdopValue)) {
            return "N/A"
        }

        return Math.sqrt((hdopValue * hdopValue) + (vdopValue * vdopValue)).toFixed(1)
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

        Column {
            id:                     gpsValuesColumn
            anchors.verticalCenter: parent.verticalCenter
            visible:                _activeVehicle
            spacing:                0

            QGCLabel {
                color: qgcPal.buttonText
                text: _activeVehicle
                    ? `ANT 1: SAT: ${_gps2SatCount}. PDOP: ${_pdopString(_activeVehicle.gps2.hdop.value, _activeVehicle.gps2.vdop.value)}. H_ACC: ${_formatDop(_activeVehicle.gps2.hdop.value)}. V_ACC: ${_formatDop(_activeVehicle.gps2.vdop.value)}.`
                    : ""
            }

            QGCLabel {
                color: qgcPal.buttonText
                text: _activeVehicle
                    ? `ANT 2: SAT: ${_gps1SatCount}. PDOP: ${_pdopString(_activeVehicle.gps.hdop.value, _activeVehicle.gps.vdop.value)}. H_ACC: ${_formatDop(_activeVehicle.gps.hdop.value)}. V_ACC: ${_formatDop(_activeVehicle.gps.vdop.value)}.`
                    : ""
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
