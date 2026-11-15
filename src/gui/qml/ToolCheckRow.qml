pragma Translator: "AnimeEffects.Tools"

import QtQuick
import QtQuick.Controls

// A dock-wide toggle row: the application's check box (AeCheckBox) with its words, at
// the dock's own face, spanning the FULL width rather than starting in the field
// column.
//
// Full width is the decision, not an oversight: a toggle has no separate label - the box
// and its words are one control - and the label column is 76px, which "Create move key"
// (88px at the dock's face) does not fit. The deleted widget panel stacked its toggles
// the same way, and the one that DID overflow ("Adjust centroid position", ~130px) is the
// reason the row is measured against the whole width instead of the field column.
Item {
    id: root

    property string text: ""
    property string tip: ""
    property bool value: false
    signal toggled(bool checked)

    implicitHeight: 24
    width: parent ? parent.width : 0

    AeCheckBox {
        id: box
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        height: 22
        text: root.text
        textFont: uiFont
        checked: root.value
        onToggled: root.toggled(checked)
        // the tooltip is the CHECK BOX's, which spans the row, so hovering anywhere along
        // it explains the toggle (the widget put the tooltip on the box alone, which left
        // a wide dead strip beside it)
        ToolTip.visible: box.hovered && root.tip !== ""
        ToolTip.delay: 500
        ToolTip.text: root.tip
    }
}
