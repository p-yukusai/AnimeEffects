pragma Translator: "AnimeEffects.Dialogs"

import QtQuick

// ONE frame range: [first] [last] and the trash that removes THIS row.
//
// The widget form had a single pair of buttons under the whole list ("Insert
// range" / "Remove range") and removeRange() deleted the LAST pair whatever was
// on screen - so with two ranges, pressing the button under the first one deleted
// the second. A row that owns its own removal cannot have that bug: the trash is
// in the row, and it is disabled while this is the only range.
//
// The row's own properties are NOT named first/last/firstName/lastName. They were,
// and a Repeater delegate that declares a `required property int first` to receive
// the model's `first` role then OVERRODE the row's property instead of binding it
// (qmllint: property-override), so no value ever reached the row and the range rows
// rendered as empty space. A delegate may not share a name with the row it fills.
Item {
    id: root

    property int firstFrame: 0
    property int lastFrame: 0
    // The per-row words a screen reader announces and a probe addresses: the widget
    // form's rows had no name of their own beyond the two column headings.
    property string firstLabel: ""
    property string lastLabel: ""
    // The row's number in the list (1-based), so this row's trash is a DIFFERENT
    // control from the next row's: two buttons labelled "Remove this range" cannot
    // be told apart by a screen reader or by a probe, which is the whole point of a
    // per-row trash.
    property int ordinal: 1
    property bool removable: true

    signal firstEdited(int value)
    signal lastEdited(int value)
    signal removeRequested()

    implicitHeight: 28
    implicitWidth: 320

    AeNumberField {
        id: firstField
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(64, (parent.width - 10 - 4 - trash.width) / 2)
        // Projects may start before frame 0: a negative start is a normal frame in
        // this codebase, not an error to reject.
        from: -2147483648
        // F36: the frame-range fields clamp against each other so a range can never
        // invert. The RULE belongs to the model - ExportRangeModel::setRangeFirst/
        // setRangeLast refuse a value that would pass the partner - and this row
        // owns only what it OFFERS: First stops at Last, so the edit that would
        // invert is never offered in the first place, and the pair stays consistent
        // because `to` follows root.lastFrame, which follows the model role, whatever
        // changed it. The two are not two implementations of the clamp; one clamps,
        // the other hides the values it would refuse.
        to: root.lastFrame
        value: root.firstFrame
        label: root.firstLabel
        onValueEdited: (value) => root.firstEdited(value)
    }
    AeNumberField {
        id: lastField
        anchors.left: firstField.right
        anchors.leftMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        width: firstField.width
        // Last starts at First - the other half of F36, stated once here and
        // clamped once in the model (see firstField's note).
        from: root.firstFrame
        to: 2147483647
        value: root.lastFrame
        label: root.lastLabel
        onValueEdited: (value) => root.lastEdited(value)
    }
    AeIconButton {
        id: trash
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        iconName: "trash"
        label: qsTr("Remove range %1").arg(root.ordinal)
        kind: "danger"
        enabled: root.removable
        onClicked: root.removeRequested()
    }
}
