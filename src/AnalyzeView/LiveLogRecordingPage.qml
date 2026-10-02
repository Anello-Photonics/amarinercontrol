import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

AnalyzePage {
    id: liveLogRecordingPage
    pageComponent: recordingComponent
    pageDescription: qsTr("Record live ULog data to the host computer or an external drive.")
    readonly property var _vehicle: QGroundControl.multiVehicleManager.activeVehicle
    readonly property var _logger: _vehicle ? _vehicle.mavlinkLogManager : null
    readonly property bool _recording: _logger ? _logger.logRunning : false

    Component {
        id: recordingComponent

        ColumnLayout {
            width: availableWidth
            spacing: ScreenTools.defaultFontPixelHeight

            QGCLabel {
                text: qsTr("Live ULog recording to computer / external drive")
            }

            QGCLabel {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Stream over the current vehicle connection to a .ulg file. Select a local folder or a mounted external drive. Warning: Before recording, increase MAV_x_RATE to 200000 for the MAVLink instance used by the current connection (replace x with the instance number).")
            }

            RowLayout {
                Layout.fillWidth: true
                QGCTextField {
                    id: hostLogFolder
                    Layout.fillWidth: true
                    text: QGroundControl.settingsManager.appSettings.logSavePath
                    enabled: !liveLogRecordingPage._recording
                }
                QGCButton {
                    text: qsTr("Browse…")
                    enabled: !liveLogRecordingPage._recording
                    onClicked: hostFolderDialog.openForLoad()
                }
                QGCButton {
                    text: liveLogRecordingPage._recording ? qsTr("Stop Recording") : qsTr("Start Recording")
                    enabled: liveLogRecordingPage._logger && (liveLogRecordingPage._recording ||
                             (!LogDownloadController.requestingList && !LogDownloadController.downloadingLogs))
                    onClicked: {
                        if (liveLogRecordingPage._recording) {
                            liveLogRecordingPage._logger.stopLogging()
                        } else {
                            liveLogRecordingPage._logger.startHostLogging(hostLogFolder.text)
                        }
                    }
                }
            }

            QGCFileDialog {
                id: hostFolderDialog
                title: qsTr("Select recording folder")
                folder: hostLogFolder.text
                selectFolder: true
                onAcceptedForLoad: (file) => { hostLogFolder.text = file }
            }

            QGCLabel {
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                text: liveLogRecordingPage._logger ? liveLogRecordingPage._logger.hostLogStatus : qsTr("Connect to a vehicle to record logs.")
                visible: text.length > 0
            }

        }
    }
}
