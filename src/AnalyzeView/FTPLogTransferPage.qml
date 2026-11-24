import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

AnalyzePage {
    id: page
    pageName: qsTr("FTP Log Transfer")
    pageDescription: qsTr("Browse and download INS logs using FTP • Port 21 • Anonymous • No encryption • Passive mode")
    pageComponent: content

    FTPLogController {
        id: ftp
        Component.onCompleted: refresh()
    }
    Component.onDestruction: ftp.cancel()

    Component {
        id: content
        ColumnLayout {
            width: page.availableWidth
            height: page.availableHeight
            spacing: ScreenTools.defaultFontPixelHeight * 0.5

            RowLayout {
                Layout.fillWidth: true
                QGCLabel { text: qsTr("IP address") }
                QGCTextField {
                    id: host
                    Layout.fillWidth: true
                    text: ftp.host
                    enabled: !ftp.busy
                    onEditingFinished: ftp.host = text
                    onAccepted: { ftp.host = text; ftp.refresh() }
                }
                QGCButton {
                    text: qsTr("Connect / Refresh")
                    enabled: !ftp.busy
                    onClicked: { ftp.host = host.text; ftp.refresh() }
                }
                QGCButton {
                    text: qsTr("Cancel")
                    enabled: ftp.busy
                    onClicked: ftp.cancel()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                QGCLabel { text: qsTr("Save to") }
                QGCTextField {
                    id: destination
                    Layout.fillWidth: true
                    text: QGroundControl.settingsManager.appSettings.logSavePath
                    enabled: !ftp.busy
                }
                QGCButton {
                    text: qsTr("Browse…")
                    enabled: !ftp.busy
                    onClicked: folderDialog.openForLoad()
                }
            }
            QGCFileDialog {
                id: folderDialog
                title: qsTr("Select log download folder")
                folder: destination.text
                selectFolder: true
                onAcceptedForLoad: (file) => { destination.text = file }
            }
            RowLayout {
                Layout.fillWidth: true
                QGCButton {
                    text: qsTr("Up")
                    enabled: !ftp.busy && ftp.path !== "/fs/microsd/log/"
                    onClicked: ftp.up()
                }
                QGCLabel {
                    Layout.fillWidth: true
                    text: ftp.path
                    wrapMode: Text.WrapAnywhere
                }
            }
            QGCLabel {
                Layout.fillWidth: true
                text: ftp.status + (ftp.busy && ftp.received > 0 ? qsTr(" — %1 KiB received").arg((ftp.received / 1024).toFixed(1)) : "")
                wrapMode: Text.WrapAnywhere
            }
            ProgressBar {
                Layout.fillWidth: true
                visible: ftp.downloading
                from: 0
                to: 1
                value: ftp.totalBytes > 0 ? Math.min(1, ftp.received / ftp.totalBytes) : 0
                indeterminate: ftp.totalBytes <= 0
            }
            QGCLabel {
                Layout.fillWidth: true
                visible: ftp.downloading
                text: ftp.totalBytes > 0
                      ? qsTr("%1% — %2 / %3 MiB").arg(Math.min(100, Math.floor(ftp.received * 100 / ftp.totalBytes))).arg((ftp.received / 1048576).toFixed(2)).arg((ftp.totalBytes / 1048576).toFixed(2))
                      : qsTr("%1 MiB received — total size unavailable").arg((ftp.received / 1048576).toFixed(2))
            }
            QGCLabel {
                Layout.fillWidth: true
                text: qsTr("Prefer completed logs. Existing local files are kept; duplicate downloads receive a numbered filename.")
                wrapMode: Text.WordWrap
            }
            ListView {
                id: files
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: ScreenTools.defaultFontPixelHeight * 0.3
                model: ftp.entries
                ScrollBar.vertical: ScrollBar {}
                delegate: RowLayout {
                    required property var modelData
                    required property int index
                    width: files.width
                    QGCLabel {
                        Layout.fillWidth: true
                        text: (modelData.directory ? qsTr("Folder: ") : "") + modelData.name
                        elide: Text.ElideMiddle
                    }
                    QGCLabel {
                        text: modelData.directory || modelData.size < 0 ? "" : qsTr("%1 MiB").arg((modelData.size / 1048576).toFixed(2))
                    }
                    QGCButton {
                        text: modelData.directory ? qsTr("Open") : qsTr("Download")
                        enabled: !ftp.busy
                        onClicked: {
                            if (modelData.directory) ftp.openDirectory(index)
                            else ftp.download(index, destination.text)
                        }
                    }
                }
                QGCLabel {
                    anchors.centerIn: parent
                    visible: !ftp.busy && files.count === 0
                    text: qsTr("No folders or .ulg logs displayed. Check the connection status above.")
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
