import QtQuick
import QtQuick.Layouts

import QGroundControl.Controls
import QGroundControl.FlightMap

ColumnLayout {
    id: root
    property real heading: 0
    property real roll: 0
    property real pitch: 0
    property color textColor: "white"
    property real dialSize: 160
    signal headingEdited(real value)
    signal attitudeEdited(real roll, real pitch)

    function headingAt(x, y, size) {
        return (Math.atan2(x - size / 2, size / 2 - y) * 180 / Math.PI + 360) % 360
    }
    function attitudeAt(x, y, size) {
        return Qt.point(Math.max(-180, Math.min(180, (x / size - 0.5) * 360)),
                        Math.max(-90, Math.min(90, (0.5 - y / size) * 180)))
    }

    spacing: 4
    QGCLabel { text: qsTr("Artificial horizon"); color: root.textColor; Layout.alignment: Qt.AlignHCenter }
    QGCAttitudeWidget {
        size: root.dialSize
        Layout.alignment: Qt.AlignHCenter
        Layout.preferredWidth: size
        Layout.preferredHeight: size
        _rollAngle: root.roll
        _pitchAngle: root.pitch

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SizeAllCursor
            preventStealing: true
            function updateValue(mouse) {
                const attitude = root.attitudeAt(mouse.x, mouse.y, width)
                root.attitudeEdited(attitude.x, attitude.y)
            }
            onPressed: (mouse) => updateValue(mouse)
            onPositionChanged: (mouse) => { if (pressed) updateValue(mouse) }
            onDoubleClicked: root.attitudeEdited(0, 0)
        }
    }
    QGCLabel {
        text: qsTr("Roll %1° · Pitch %2°").arg(root.roll.toFixed(1)).arg(root.pitch.toFixed(1))
        color: root.textColor; Layout.alignment: Qt.AlignHCenter
    }
    QGCLabel {
        text: qsTr("Left/right: roll · Up/down: pitch\nDouble-click to level")
        color: root.textColor; font.pointSize: ScreenTools.smallFontPointSize
        horizontalAlignment: Text.AlignHCenter; Layout.alignment: Qt.AlignHCenter
    }

    QGCLabel { text: qsTr("Compass"); color: root.textColor; Layout.topMargin: 8; Layout.alignment: Qt.AlignHCenter }
    QGCCompassWidget {
        size: root.dialSize
        Layout.alignment: Qt.AlignHCenter
        Layout.preferredWidth: size
        Layout.preferredHeight: size
        _heading: root.heading
        // Keep cardinal directions fixed so clicking north/east selects 0/90 degrees.
        _lockNoseUpCompass: false
        _showAdditionalIndicators: false

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            preventStealing: true
            function updateValue(mouse) {
                const dx = mouse.x - width / 2, dy = mouse.y - height / 2
                if (dx * dx + dy * dy > 16) root.headingEdited(root.headingAt(mouse.x, mouse.y, width))
            }
            onPressed: (mouse) => updateValue(mouse)
            onPositionChanged: (mouse) => { if (pressed) updateValue(mouse) }
        }
    }
    QGCLabel {
        text: qsTr("Heading %1°").arg(root.heading.toFixed(1))
        color: root.textColor; Layout.alignment: Qt.AlignHCenter
    }
    QGCLabel {
        text: qsTr("Click or drag to point heading")
        color: root.textColor; font.pointSize: ScreenTools.smallFontPointSize
        Layout.alignment: Qt.AlignHCenter
    }
}
