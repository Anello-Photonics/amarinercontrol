/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/


import QtQuick
import QtQuick.Controls

import QGroundControl
import QGroundControl.Controls



Item {
    anchors.fill: parent
    anchors.margins: 10

    Rectangle {
        anchors.fill: parent
        color: "transparent" // Just for structure, no visible border

        ParameterEditor {
            anchors.fill: parent
            anchors.margins: 10
        }
    }
}
