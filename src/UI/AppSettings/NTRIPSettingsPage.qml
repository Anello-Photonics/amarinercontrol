/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

SettingsPage {
    property var _ntripSettings: QGroundControl.settingsManager.ntripSettings
    property var _ntripManager:  QGroundControl.ntripManager

    SettingsGroupLayout {
        Layout.fillWidth: true
        heading:          qsTr("NTRIP / RTK")
        headingDescription: qsTr("Configure an NTRIP caster to receive RTCM corrections and forward them to connected vehicles.")

        FactCheckBoxSlider {
            Layout.fillWidth: true
            text:             qsTr("Enable NTRIP")
            fact:             _ntripSettings.enabled
        }

        LabelledFactTextField {
            label: _ntripSettings.host.shortDescription
            fact:  _ntripSettings.host
        }

        LabelledFactTextField {
            label: _ntripSettings.port.shortDescription
            fact:  _ntripSettings.port
        }

        LabelledFactTextField {
            label: _ntripSettings.mountpoint.shortDescription
            fact:  _ntripSettings.mountpoint
        }

        LabelledFactTextField {
            label: _ntripSettings.username.shortDescription
            fact:  _ntripSettings.username
        }

        LabelledFactTextField {
            label:              _ntripSettings.password.shortDescription
            fact:               _ntripSettings.password
            textField.echoMode: TextInput.Password
        }

        FactCheckBoxSlider {
            Layout.fillWidth: true
            text:             _ntripSettings.sendGGA.shortDescription
            fact:             _ntripSettings.sendGGA
        }

        LabelledFactTextField {
            label: _ntripSettings.ggaInterval.shortDescription
            fact:  _ntripSettings.ggaInterval
        }
    }

    SettingsGroupLayout {
        Layout.fillWidth: true
        heading:          qsTr("NTRIP Status")

        LabelledLabel {
            label:     qsTr("Connection")
            labelText: _ntripManager.connected ? qsTr("Connected") : qsTr("Disconnected")
        }

        LabelledLabel {
            label:     qsTr("Status")
            labelText: _ntripManager.statusText
        }

        LabelledLabel {
            label:     qsTr("Corrections")
            labelText: qsTr("%1 bytes, %2 messages, %3 B/s")
                .arg(_ntripManager.bytesReceived)
                .arg(_ntripManager.messagesReceived)
                .arg(_ntripManager.dataRateBytesPerSecond.toFixed(1))
        }

        LabelledButton {
            label:      qsTr("Connection")
            buttonText: qsTr("Reconnect")
            enabled:    _ntripSettings.enabled.rawValue
            onClicked:  _ntripManager.reconnect()
        }
    }
}
