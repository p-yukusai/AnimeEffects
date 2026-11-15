pragma Translator: "AnimeEffects.Properties"

import QtQuick
import QtQuick.Layouts

// One field surface for an image asset: the filename and the browse action are
// the same control, not a read-only line beside a separate button.
Rectangle {
    id: root

    property string name: ""
    property string label: qsTr("Asset")
    signal clicked()

    implicitHeight: 23
    implicitWidth: 180
    radius: 4
    color: !root.enabled ? theme.recessed
         : hover.hovered ? theme.raisedHover : theme.raised
    border.width: 1
    border.color: !root.enabled ? theme.recessed
                : hover.hovered ? theme.hover : theme.raised

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 4
        spacing: 4
        Text {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: root.name !== "" ? root.name : qsTr("None")
            color: root.name !== "" && root.enabled ? theme.text : theme.textMuted
            font: uiFont
            elide: Text.ElideMiddle
        }
        Image {
            Layout.preferredWidth: 14
            Layout.preferredHeight: 14
            source: theme.iconUrl("file")
            sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
            sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
            smooth: true
            fillMode: Image.PreserveAspectFit
            opacity: root.enabled ? 0.9 : 0.45
        }
    }

    HoverHandler {
        id: hover
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.enabled
        onClicked: root.clicked()
    }

    Accessible.role: Accessible.Button
    Accessible.name: root.label
    Accessible.description: root.name
    Accessible.onPressAction: if (root.enabled) root.clicked()
}
