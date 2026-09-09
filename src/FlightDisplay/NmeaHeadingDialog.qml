import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore

import QGroundControl
import QGroundControl.Controls
import "NmeaHeading.js" as NmeaHeading

QGCPopupDialog {
    id: root
    title: qsTr("NMEA heading")
    buttons: Dialog.Close

    property string sendStatus: ""
    property var commandSettings: Settings {
        category: "NmeaCommands"
        property string destination: "192.168.0.3"
        property string destinationPort: "19551"
    }
    readonly property string sentence: NmeaHeading.sentenceFromFields([
        utcField.text, rollField.text, pitchField.text, headingField.text,
        rollAccuracyField.text, pitchAccuracyField.text, headingAccuracyField.text
    ])

    ColumnLayout {
        spacing: ScreenTools.defaultFontPixelHeight / 2

        RowLayout {
            spacing: ScreenTools.defaultFontPixelWidth * 2

            GridLayout {
                Layout.alignment: Qt.AlignTop
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
                QGCLabel { text: qsTr("Roll (degrees)") }
                QGCTextField {
                    id: rollField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Signed roll angle")
                }
                QGCLabel { text: qsTr("Pitch (degrees)") }
                QGCTextField {
                    id: pitchField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Signed pitch angle")
                }
                QGCLabel { text: qsTr("Heading / yaw (degrees)") }
                QGCTextField {
                    id: headingField
                    Layout.fillWidth: true
                    placeholderText: qsTr("0 to 360")
                }
                QGCLabel { text: qsTr("Roll uncertainty (degrees)") }
                QGCTextField {
                    id: rollAccuracyField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Non-negative degrees")
                }
                QGCLabel { text: qsTr("Pitch uncertainty (degrees)") }
                QGCTextField {
                    id: pitchAccuracyField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Non-negative degrees")
                }
                QGCLabel { text: qsTr("Heading uncertainty (degrees)") }
                QGCTextField {
                    id: headingAccuracyField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Non-negative degrees")
                }
            }
            NmeaAttitudeEditor {
                Layout.alignment: Qt.AlignTop
                dialSize: ScreenTools.defaultFontPixelHeight * 8
                textColor: root._qgcPal.text
                heading: isFinite(Number(headingField.text)) ? Number(headingField.text) : 0
                roll: isFinite(Number(rollField.text)) ? Number(rollField.text) : 0
                pitch: isFinite(Number(pitchField.text)) ? Number(pitchField.text) : 0
                onHeadingEdited: (value) => { headingField.text = (Math.round(value * 10) / 10 % 360).toFixed(1) }
                onAttitudeEdited: (roll, pitch) => {
                    rollField.text = roll.toFixed(1)
                    pitchField.text = pitch.toFixed(1)
                }
            }
        }
        QGCLabel {
            Layout.maximumWidth: ScreenTools.defaultFontPixelWidth * 65
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("PAPRPH supplies roll, pitch, heading and their uncertainties in degrees. UTC time may be blank. The supplied specification currently supports heading aiding; roll/pitch aiding is available on request. AMC calculates the checksum and uses the configured NMEA connection.")
        }
        QGCLabel {
            Layout.maximumWidth: ScreenTools.defaultFontPixelWidth * 65
            Layout.fillWidth: true
            wrapMode: Text.WrapAnywhere
            text: root.sentence.length ? root.sentence : qsTr("Enter roll, pitch, heading (0 to 360) and non-negative uncertainties. Optional UTC time must use hhmmss.ss.")
        }
        QGCButton {
            text: qsTr("Send NMEA heading")
            enabled: root.sentence.length > 0 && root.opened && mainWindow.active
            autoRepeat: true
            autoRepeatDelay: 500
            autoRepeatInterval: 500
            onClicked: {
                const error = QGroundControl.linkManager.sendNmeaSentence(
                    root.sentence, true,
                    root.commandSettings.destination.trim() || "192.168.0.3",
                    Number(root.commandSettings.destinationPort.trim() || "19551"))
                root.sendStatus = error.length ? error : qsTr("Heading queued for transmission. Device acceptance is not confirmed.")
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
