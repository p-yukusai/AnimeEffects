pragma Translator: "AnimeEffects.Tools"

import QtQuick
import QtQuick.Layouts

// ONE grid for every row in the Tools dock:
//
//   label 12..88 | content 90..width-16
//
// The same shape as the inspector's InspectorRow, at the tools dock's narrower width
// and without a key gutter (nothing here is a document channel). Keeping one grid is
// what lets a slider, a checkbox, a combo and a number field line up down the whole
// panel - the widget version laid each row out with its own arithmetic and the panels
// drifted from each other.
Item {
    id: root

    property string label: ""
    default property alias content: fields.data

    implicitHeight: 26
    width: parent ? parent.width : 0

    Text {
        x: 12
        width: 76
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
        anchors.leftMargin: 90
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        height: 23
        spacing: 6
    }
}
