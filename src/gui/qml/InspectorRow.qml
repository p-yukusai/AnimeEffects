pragma Translator: "AnimeEffects.Properties"

import QtQuick
import QtQuick.Layouts

// ONE geometry for every property row, keyable or not:
//   gutter 16..36 | label 42..114 | field column 124..right-16
// The empty gutter is deliberate on Constant rows: it is what keeps their
// labels and fields on the same grid as Position and Opacity.
Item {
    id: root

    property string label: ""
    property string keyState: ""
    property color keyColor: "transparent"
    property string keyLabel: label
    property bool keyEnabled: true
    signal toggleKey()

    default property alias content: fields.data

    implicitHeight: 27
    height: 27
    width: parent ? parent.width : 0
    Accessible.ignored: !root.visible

    Item {
        id: gutter
        x: 16
        width: 20
        height: 27
        InspectorKeyDot {
            anchors.centerIn: parent
            visible: root.keyState !== ""
            enabled: root.keyEnabled
            state: root.keyState
            ink: root.keyColor
            label: root.keyLabel
            onClicked: root.toggleKey()
        }
    }

    Text {
        x: 42
        width: 72
        anchors.verticalCenter: parent.verticalCenter
        text: root.label
        color: theme.text
        font: uiFont
        elide: Text.ElideRight
        Accessible.role: Accessible.StaticText
        Accessible.name: root.label
    }

    RowLayout {
        id: fields
        anchors.left: parent.left
        anchors.leftMargin: 124
        anchors.right: parent.right
        anchors.rightMargin: 16
        height: 23
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6
    }
}
