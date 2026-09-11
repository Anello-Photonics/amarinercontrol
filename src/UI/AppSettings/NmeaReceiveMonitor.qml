import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

import QGroundControl
import QGroundControl.Controls

Window {
    id: root
    title: qsTr("NMEA receive monitor")
    width: 850
    height: 500
    minimumWidth: 480
    minimumHeight: 280
    color: QGroundControl.globalPalette.window

    property bool paused: false
    property string pendingText: ""
    readonly property int characterLimit: 65536
    property var receiveStatus: ({text: ""})

    Connections {
        target: QGroundControl.linkManager
        enabled: root.visible && !root.paused
        function onNmeaBytesReceived(text) {
            root.pendingText = (root.pendingText + text).slice(-root.characterLimit)
        }
    }

    Timer {
        interval: 100
        repeat: true
        running: root.visible
        triggeredOnStart: true
        onTriggered: {
            root.receiveStatus = QGroundControl.linkManager.nmeaReceiveStatus()
            if (!root.paused && root.pendingText.length) {
                stream.text = (stream.text + root.pendingText).slice(-root.characterLimit)
                root.pendingText = ""
                if (follow.checked) stream.cursorPosition = stream.length
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            QGCButton {
                text: root.paused ? qsTr("Resume") : qsTr("Pause")
                onClicked: {
                    root.paused = !root.paused
                    root.pendingText = ""
                }
            }
            QGCButton {
                text: qsTr("Clear")
                onClicked: {
                    root.pendingText = ""
                    stream.clear()
                }
            }
            QGCCheckBox {
                id: follow
                text: qsTr("Auto-scroll")
                checked: true
            }
            Item { Layout.fillWidth: true }
        }
        QGCLabel {
            Layout.fillWidth: true
            text: root.receiveStatus.text
            wrapMode: Text.WordWrap
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            TextArea {
                id: stream
                readOnly: true
                selectByMouse: true
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.NoWrap
                font.family: "Consolas"
                color: QGroundControl.globalPalette.text
                placeholderText: qsTr("Waiting for incoming NMEA data...")
                background: Rectangle { color: QGroundControl.globalPalette.windowShade }
            }
        }
        QGCLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: root.paused
                ? qsTr("Display paused. New data is omitted; NMEA reception continues.")
                : qsTr("Raw received data, including invalid sentences. Keeps the latest 64 KiB while this window is open.")
        }
    }
}
