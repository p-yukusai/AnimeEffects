pragma Translator: "AnimeEffects.Tools"

import QtQuick

// A block heading, at two levels.
//
// SECTION (the dock's blocks): the block's name on the left and - when the block has
// modes - the active mode's name on the right. The trailing word is the point of this
// component: a mode row is ICONS (seven of them in the bone editor), and the widget
// version explained them with tooltips only, so four unlabeled glyphs were the only way
// to tell which field the mask brush writes. The heading names the TOOL, the trailing
// text names the MODE: "Bone editor ... Envelope brush" is readable at a glance
// without hovering. It is the deleted QGroupBox title, in the dock's own vocabulary -
// the dock's face at semibold weight in `textMuted`, on the base surface, with no card
// and no rule.
//
// GROUP (a sub-section INSIDE a panel, e.g. "Brush"): a quiet 10px label, one step below
// the section heading. NOT upper case: the docks' headings are sentence case everywhere
// else (the tool name, the lane names, the inspector's section labels), and a shouting
// sub-label in a 240px panel read as chrome rather than as a heading - the reported
// "screaming case" tick.
Item {
    id: root

    property string title: ""
    property string detail: ""
    // a sub-section inside a panel rather than a block of the dock
    property bool group: false
    // CONTROLS at the heading's right end (the mask's field disclosure and its Clear).
    // A heading carries EITHER a detail string or actions, never both: they would share
    // the same corner. The mask is the only user today and it has no mode caption.
    default property alias actions: actionsRow.data

    implicitHeight: root.group ? 20 : 26
    width: parent ? parent.width : 0

    // The two levels are two items rather than one with conditional font sub-properties:
    // `font` is a value type, and a section heading needs the dock's whole face (family
    // AND point size, from the locale) while a group heading is a deliberate 10px step.
    Text {
        id: sectionTitle
        visible: !root.group
        x: 12
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(implicitWidth, detailLabel.visible ? detailLabel.x - x - 8 : root.width - x - 12)
        text: root.title
        color: theme.textMuted
        // The dock's whole face (family AND the locale's point/pixel size) plus the
        // heading's weight. QML forbids `font: uiFont` beside `font.weight:`, so the
        // parts are assigned - the one that the locale did not set stays -1, i.e. unset.
        font.family: uiFont.family
        font.pointSize: uiFont.pointSize
        font.pixelSize: uiFont.pixelSize
        font.weight: Font.DemiBold
        elide: Text.ElideRight
        Accessible.role: Accessible.Heading
        Accessible.name: root.title
        // The other level's item is still in the tree (QML does not remove an invisible
        // item), and an accessible node with the same words would make a screen reader
        // announce every heading twice.
        Accessible.ignored: root.group
    }
    Text {
        id: groupTitle
        visible: root.group
        x: 12
        anchors.verticalCenter: parent.verticalCenter
        width: root.width - 24
        text: root.title
        color: theme.textMutedDim
        font.family: uiFont.family
        font.pixelSize: 10
        font.weight: Font.DemiBold
        elide: Text.ElideRight
        Accessible.role: Accessible.Heading
        Accessible.name: root.title
        Accessible.ignored: !root.group
    }
    Row {
        id: actionsRow
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 10
    }
    Text {
        id: detailLabel
        anchors.right: actionsRow.width > 0 ? actionsRow.left : parent.right
        anchors.rightMargin: actionsRow.width > 0 ? 10 : 12
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(implicitWidth, root.width * 0.5)
        text: root.detail
        // A detail that repeats the title says nothing: the SR transform's own mode is
        // CALLED "Transform", and "Transform ........ Transform" is noise, not an answer
        // to "which mode am I in".
        visible: !root.group && text !== "" && text !== root.title
        color: theme.textMutedDim
        font.family: uiFont.family
        font.pixelSize: 10
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignRight
        Accessible.role: Accessible.StaticText
        Accessible.name: root.detail
        Accessible.ignored: root.group || root.detail === ""
    }
}
