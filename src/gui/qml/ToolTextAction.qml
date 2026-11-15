pragma Translator: "AnimeEffects.Tools"

import QtQuick
import QtQuick.Controls

// A quiet text action: the words only, in the dock's muted ink, brightening on hover.
//
// The dock's small utilities ("Clear" on the mask's field row) are actions OF the thing
// they belong to, not the panel's primary decision. Drawn as a full-width raised button -
// which is what the Clear mask control was - they read as "press me first", which is
// exactly the wrong hierarchy in a panel whose job is painting. Text costs the layout
// nothing and cannot be mistaken for the main verb; the accessible name keeps it a real
// control for a reader and for a probe.
Item {
    id: root

    property string text: ""
    property string tip: ""
    signal clicked()

    implicitWidth: label.implicitWidth
    implicitHeight: 18

    Text {
        id: label
        anchors.verticalCenter: parent.verticalCenter
        text: root.text
        color: !root.enabled ? theme.textDisabled
             : mouse.containsMouse ? theme.text
             : theme.textMuted
        font: uiFont
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    Accessible.role: Accessible.Button
    Accessible.name: root.text
    Accessible.description: root.tip
    Accessible.onPressAction: if (root.enabled) root.clicked()

    ToolTip.visible: mouse.containsMouse && root.tip !== ""
    ToolTip.delay: 500
    ToolTip.text: root.tip

    activeFocusOnTab: true
    Keys.onSpacePressed: if (root.enabled) { root.clicked(); event.accepted = true }
    Keys.onReturnPressed: if (root.enabled) { root.clicked(); event.accepted = true }
}
