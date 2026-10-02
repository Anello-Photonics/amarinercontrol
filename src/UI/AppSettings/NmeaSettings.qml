import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

SettingsPage {
    id: root

    property NmeaReceiveMonitor receiveMonitor: NmeaReceiveMonitor {}

    property string sendStatus: ""
    property var receiveStatus: QGroundControl.linkManager.nmeaReceiveStatus()
    property Timer receiveStatusTimer: Timer {
        interval: 500
        repeat: true
        running: root.visible
        triggeredOnStart: true
        onTriggered: root.receiveStatus = QGroundControl.linkManager.nmeaReceiveStatus()
    }
    property Settings savedSettings: Settings {
        category: "NmeaCommands"
        property alias destination: destination.text
        property alias destinationPort: destinationPort.text
        property alias manualSentence: manualSentence.text
        property alias addChecksum: checksum.checked
    }

    Component.onCompleted: {
        // Populate fields saved empty by earlier versions while retaining custom destinations.
        if (!destination.text.trim().length) destination.text = "192.168.0.3"
        if (!destinationPort.text.trim().length) destinationPort.text = "19551"
    }

    function sendSentence(sentence) {
        const error = QGroundControl.linkManager.sendNmeaSentence(sentence, checksum.checked,
                                                                 destination.text, Number(destinationPort.text))
        sendStatus = error.length ? error : qsTr("Sentence queued for transmission. Device acceptance is not confirmed.")
    }

    SettingsGroupLayout {
        heading: qsTr("NMEA connection")
        visible: QGroundControl.settingsManager.autoConnectSettings.autoConnectNmeaPort.visible && QGroundControl.settingsManager.autoConnectSettings.autoConnectNmeaBaud.visible

        LabelledComboBox {
            id: nmeaPortCombo
            label: qsTr("Device")

            model: ListModel {}

            onActivated: (index) => {
                if (index !== -1) {
                    QGroundControl.settingsManager.autoConnectSettings.autoConnectNmeaPort.value = comboBox.textAt(index);
                }
            }

            function refreshPorts() {
                let ports = ["Disabled", "UDP Port"]
                if (ScreenTools.isSerialAvailable) {
                    const serialPorts = QGroundControl.linkManager.serialPorts
                    for (let i = 0; i < serialPorts.length; ++i) {
                        ports.push(serialPorts[i])
                    }
                }
                const selected = QGroundControl.settingsManager.autoConnectSettings.autoConnectNmeaPort.valueString
                if (selected.length && ports.indexOf(selected) < 0) {
                    ports.push(selected)
                }
                model = ports
                currentIndex = Math.max(0, ports.indexOf(selected))
            }

            Component.onCompleted: refreshPorts()
        }

        QGCButton {
            text: qsTr("Refresh devices")
            onClicked: nmeaPortCombo.refreshPorts()
        }

        QGCButton {
            text: QGroundControl.linkManager.nmeaConnectionEnabled ? qsTr("Disconnect") : qsTr("Connect")
            enabled: nmeaPortCombo.currentIndex > 0
            onClicked: {
                QGroundControl.linkManager.setNmeaConnectionEnabled(!QGroundControl.linkManager.nmeaConnectionEnabled)
                root.receiveStatus = QGroundControl.linkManager.nmeaReceiveStatus()
                root.sendStatus = ""
            }
        }

        LabelledComboBox {
            id: nmeaBaudCombo
            visible: (nmeaPortCombo.currentText !== "UDP Port") && (nmeaPortCombo.currentText !== "Disabled")
            label: qsTr("Baudrate")
            model: ScreenTools.isSerialAvailable ? QGroundControl.linkManager.serialBaudRates : []

            onActivated: (index) => {
                if (index !== -1) {
                    QGroundControl.settingsManager.autoConnectSettings.autoConnectNmeaBaud.value = parseInt(comboBox.textAt(index));
                }
            }

            Component.onCompleted: {
                const index = nmeaBaudCombo.comboBox.find(QGroundControl.settingsManager.autoConnectSettings.autoConnectNmeaBaud.valueString);
                nmeaBaudCombo.currentIndex = index;
            }
        }

        LabelledFactTextField {
            visible: nmeaPortCombo.currentText === "UDP Port"
            label: qsTr("NMEA stream UDP port")
            fact: QGroundControl.settingsManager.autoConnectSettings.nmeaUdpPort
        }
        LabelledFactTextField {
            visible: nmeaPortCombo.currentText === "UDP Port"
            label: qsTr("NMEA receive multicast group")
            fact: QGroundControl.settingsManager.autoConnectSettings.nmeaMulticastGroup
        }
        QGCLabel {
            visible: nmeaPortCombo.currentText === "UDP Port"
            Layout.maximumWidth: ScreenTools.defaultFontPixelWidth * 65
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("For multicast output, enter the group IP configured on the INS and match its output port above. Leave blank for unicast/broadcast. The command destination below is separate.")
        }
    }

    SettingsGroupLayout {
        heading: qsTr("NMEA receive status")
        QGCButton {
            text: qsTr("Open receive monitor")
            onClicked: {
                root.receiveMonitor.show()
                root.receiveMonitor.raise()
                root.receiveMonitor.requestActivate()
            }
        }
        RowLayout {
            Rectangle {
                Layout.preferredWidth: ScreenTools.defaultFontPixelHeight
                Layout.preferredHeight: ScreenTools.defaultFontPixelHeight
                radius: width / 2
                color: root.receiveStatus.receiving ? "#38b86b" : "#c99b36"
            }
            QGCLabel { text: root.receiveStatus.text }
        }
        QGCLabel {
            Layout.maximumWidth: ScreenTools.defaultFontPixelWidth * 65
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Green means a correctly framed NMEA sentence with a valid checksum was received within 5 seconds. This does not require a GPS fix.")
        }
    }

    SettingsGroupLayout {
        heading: qsTr("Manual NMEA command")

        QGCLabel {
            Layout.fillWidth: true
            Layout.maximumWidth: ScreenTools.defaultFontPixelWidth * 65
            wrapMode: Text.WordWrap
            text: qsTr("Enter any NMEA sentence supported by the connected device. Each Send transmits one sentence on the selected NMEA connection. AMC adds CR/LF line endings.")
        }

        QGCLabel {
            visible: nmeaPortCombo.currentText === "UDP Port"
            text: qsTr("Device destination IP address")
        }
        QGCTextField {
            id: destination
            text: "192.168.0.3"
            Layout.fillWidth: true
            visible: nmeaPortCombo.currentText === "UDP Port"
            placeholderText: qsTr("Device destination IP address")
        }
        QGCLabel {
            visible: destination.visible
            text: qsTr("Device destination UDP port")
        }
        QGCTextField {
            id: destinationPort
            text: "19551"
            Layout.fillWidth: true
            visible: destination.visible
            placeholderText: qsTr("Device destination UDP port (1-65535)")
            validator: IntValidator { bottom: 1; top: 65535 }
        }
        QGCCheckBox {
            id: checksum
            text: qsTr("Calculate NMEA checksum (replace existing checksum)")
            checked: true
        }
        QGCLabel { text: qsTr("NMEA sentence") }
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 5
            contentWidth: availableWidth
            clip: true

            TextArea {
                id: manualSentence
                placeholderText: qsTr("Type or paste one NMEA sentence")
                wrapMode: TextEdit.WrapAnywhere
                selectByMouse: true
                color: QGroundControl.globalPalette.text
                font.family: ScreenTools.normalFontFamily
                font.pointSize: ScreenTools.defaultFontPointSize
                background: Rectangle {
                    color: QGroundControl.globalPalette.windowShade
                    border.color: QGroundControl.globalPalette.text
                    radius: 3
                }
            }
        }
        QGCButton {
            text: qsTr("Send NMEA command")
            enabled: QGroundControl.linkManager.nmeaConnectionEnabled && nmeaPortCombo.currentIndex > 0 && manualSentence.text.trim().length > 0
            onClicked: root.sendSentence(manualSentence.text)
        }
        QGCLabel {
            Layout.fillWidth: true
            Layout.maximumWidth: ScreenTools.defaultFontPixelWidth * 65
            wrapMode: Text.WordWrap
            text: root.sendStatus
            visible: text.length > 0
        }
    }
}
