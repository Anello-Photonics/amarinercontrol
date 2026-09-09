import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import QtPositioning

import QGroundControl
import QGroundControl.Controls
import "NmeaPositionInit.js" as NmeaPositionInit

QGCPopupDialog {
    id: root
    title: qsTr("NMEA position initialization")
    buttons: Dialog.Close

    property var coordinate: QtPositioning.coordinate()
    property string sendStatus: ""
    property var commandSettings: Settings {
        category: "NmeaCommands"
        property string destination: "192.168.0.3"
        property string destinationPort: "19551"
    }
    readonly property string sentence: NmeaPositionInit.sentenceFromFields([
        utcField.text, latitudeField.text, longitudeField.text,
        altitudeField.text, horizontalAccuracyField.text, verticalAccuracyField.text
    ])

    ColumnLayout {
        spacing: ScreenTools.defaultFontPixelHeight / 2

        GridLayout {
            columns: 2
            columnSpacing: ScreenTools.defaultFontPixelWidth
            rowSpacing: ScreenTools.defaultFontPixelHeight / 2
            Layout.fillWidth: true

            QGCLabel { text: qsTr("UTC time (optional)") }
            QGCTextField {
                id: utcField
                Layout.fillWidth: true
                placeholderText: qsTr("hhmmss.ss or leave blank")
            }
            QGCLabel { text: qsTr("Latitude (degrees)") }
            QGCTextField {
                id: latitudeField
                Layout.fillWidth: true
                text: root.coordinate.latitude.toFixed(6)
                placeholderText: qsTr("-90 to 90")
            }
            QGCLabel { text: qsTr("Longitude (degrees)") }
            QGCTextField {
                id: longitudeField
                Layout.fillWidth: true
                text: root.coordinate.longitude.toFixed(6)
                placeholderText: qsTr("-180 to 180")
            }
            QGCLabel { text: qsTr("Altitude above MSL (m)") }
            QGCTextField {
                id: altitudeField
                Layout.fillWidth: true
                placeholderText: qsTr("Altitude in meters")
            }
            QGCLabel { text: qsTr("Horizontal uncertainty (m)") }
            QGCTextField {
                id: horizontalAccuracyField
                Layout.fillWidth: true
                placeholderText: qsTr("Non-negative meters")
            }
            QGCLabel { text: qsTr("Vertical uncertainty (m)") }
            QGCTextField {
                id: verticalAccuracyField
                Layout.fillWidth: true
                placeholderText: qsTr("Non-negative meters")
            }
        }
        QGCLabel {
            Layout.maximumWidth: ScreenTools.defaultFontPixelWidth * 65
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Maritime INS uses latitude, longitude, altitude and horizontal/vertical uncertainty. UTC time may be blank. Latitude and longitude start at the clicked map location (north/east positive, south/west negative). AMC calculates the checksum automatically.")
        }
        QGCLabel {
            Layout.maximumWidth: ScreenTools.defaultFontPixelWidth * 65
            Layout.fillWidth: true
            wrapMode: Text.WrapAnywhere
            text: root.sentence.length ? root.sentence : qsTr("Enter valid coordinates, altitude and non-negative uncertainties. Optional UTC time must use hhmmss.ss.")
        }
        QGCButton {
            text: qsTr("Send position init")
            enabled: root.sentence.length > 0 && root.opened && mainWindow.active
            autoRepeat: true
            autoRepeatDelay: 500
            autoRepeatInterval: 500
            onClicked: {
                const error = QGroundControl.linkManager.sendNmeaSentence(
                    root.sentence, true,
                    root.commandSettings.destination.trim() || "192.168.0.3",
                    Number(root.commandSettings.destinationPort.trim() || "19551"))
                root.sendStatus = error.length ? error : qsTr("Position init queued for transmission. Device acceptance is not confirmed.")
            }
        }
        QGCLabel {
            text: qsTr("Click to send once. Hold to repeat every 0.5 seconds.")
        }
        QGCLabel {
            Layout.maximumWidth: ScreenTools.defaultFontPixelWidth * 65
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: root.sendStatus
            visible: text.length > 0
        }
    }
}
