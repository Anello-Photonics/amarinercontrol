/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

Item {
    id:             control
    implicitWidth:  mainLayout.width + (_toolsMargin * 2)
    implicitHeight: mainLayout.height + (_toolsMargin * 2)

    property var  _activeVehicle:       QGroundControl.multiVehicleManager.activeVehicle
    property Fact _speedSensorFact:     _activeVehicle ? _activeVehicle.getFact("groundSpeed") : null
    property Fact _gpsSpeedFact:        _activeVehicle && _activeVehicle.factExists("gps.vel") ? _activeVehicle.getFact("gps.vel") : null
    property bool _displayExpanded:     QGroundControl.settingsManager.flyViewSettings.showSpeedTelemetryDisplay.rawValue

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    function factValueText(fact) {
        if (!fact) {
            return qsTr("--.--")
        }

        return fact.enumOrValueString + (fact.units ? " " + fact.units : "")
    }

    Rectangle {
        id:         backgroundRect
        anchors.fill: parent
        color:      qgcPal.window
        radius:     ScreenTools.defaultFontPixelWidth / 2
        opacity:    0.75
    }

    ColumnLayout {
        id:                 mainLayout
        anchors.margins:    _toolsMargin
        anchors.bottom:     parent.bottom
        anchors.left:       parent.left
        spacing:            ScreenTools.defaultFontPixelWidth / 2

        RowLayout {
            Layout.fillWidth:   true
            visible:            _displayExpanded
            spacing:            ScreenTools.defaultFontPixelWidth

            QGCLabel {
                Layout.fillWidth:   true
                text:               qsTr("Speed")
                font.pointSize:     ScreenTools.smallFontPointSize
                color:              qgcPal.text
            }

            QGCButton {
                Layout.preferredWidth:  ScreenTools.minTouchPixels * 0.75
                Layout.preferredHeight: ScreenTools.minTouchPixels * 0.6
                text:                   qsTr("×")
                onClicked:              QGroundControl.settingsManager.flyViewSettings.showSpeedTelemetryDisplay.rawValue = false
            }
        }

        GridLayout {
            visible:        _displayExpanded
            rows:           2
            columns:        2
            rowSpacing:     0
            columnSpacing:  ScreenTools.defaultFontPixelWidth / 2
            flow:           GridLayout.LeftToRight

            QGCLabel {
                Layout.alignment:   Qt.AlignRight
                text:               qsTr("speed sensor")
                color:              qgcPal.text
            }

            QGCLabel {
                Layout.alignment:   Qt.AlignLeft
                text:               factValueText(_speedSensorFact)
                color:              qgcPal.text
                font.pointSize:     ScreenTools.largeFontPointSize
            }

            QGCLabel {
                Layout.alignment:   Qt.AlignRight
                text:               qsTr("GPS speed")
                color:              qgcPal.text
            }

            QGCLabel {
                Layout.alignment:   Qt.AlignLeft
                text:               factValueText(_gpsSpeedFact)
                color:              qgcPal.text
                font.pointSize:     ScreenTools.largeFontPointSize
            }
        }

        QGCButton {
            visible:                !_displayExpanded
            text:                   qsTr("Show speed")
            onClicked:              QGroundControl.settingsManager.flyViewSettings.showSpeedTelemetryDisplay.rawValue = true
        }
    }
}
