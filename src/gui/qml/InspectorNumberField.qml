pragma Translator: "AnimeEffects.Properties"

import QtQuick
import QtQuick.Controls

// The inspector's numeric field: the existing ScrubSpinBox, at the dock's 23px
// field height. A scrub or typed edit commits ONCE, on release/Enter, so one
// gesture is one undo step; the field's own text still follows the pointer.
Item {
    id: root

    property real value: 0
    property real from: 0
    property real to: 100
    property real step: 1
    property int decimals: 3
    property string label: ""
    property string prefix: ""
    property string prefixIcon: ""
    property string suffix: ""
    // The inspector draws a line of X/Y/glyph prefixes, so it reserves one 24px lead-in
    // for every field and the numbers line up down the panel. A surface whose fields
    // carry no prefix must NOT reserve it: the number then starts 8px from the edge
    // instead of 24, which is the difference between a field that is mostly padding and
    // one that is mostly value (reported from the Tools dock, whose fields have no
    // prefix at all).
    property bool reservePrefix: true
    // Per-field keying: the row model and the channel this field names. A
    // right-click offers Add/Remove <channel> Key; -1 (or a row without a
    // channel key) leaves the field with no menu.
    property var row: null
    property int channel: -1
    readonly property string channelKeyAction:
        row && channel >= 0 ? row.channelKeyAction(channel) : ""
    // Read AGAIN when the menu opens: a Q_INVOKABLE call in a binding registers no
    // dependency, so the label above can still name the state from before the last
    // toggle. The open handler is the authority.
    property string openMenuAction: ""
    readonly property bool hovered: fieldHover.hovered
    // The field's two non-hover active states, for a live preview (the mesh
    // overlay): a scrub keeps the pointer grabbed, so it stays true even when
    // the pointer leaves the field's rectangle mid-drag.
    readonly property bool scrubbing: field.scrubbing
    readonly property bool editing: field.editing
    signal valueEdited(real value)

    implicitHeight: 23
    implicitWidth: 84
    function quantize(v) {
        const p = Math.pow(10, root.decimals);
        return Math.round(Number(v) * p) / p;
    }

    // The accessibility node sits BELOW the field so its full-area item can
    // never intercept the scrub. Its rectangle is the field's, which is what a
    // screen reader and scripts/gui/ae_ui.py click.
    Item {
        objectName: "aeAccessibleValue"
        anchors.fill: field
        enabled: root.enabled
        property string accessibleName: root.label
        property real accessibleValue: root.value
        property real accessibleMinimum: root.from
        property real accessibleMaximum: root.to
        property real accessibleStep: root.step
        function accessibleSetValue(value) { root.valueEdited(root.quantize(value)) }
        function accessibleBeginEdit() { field.beginEdit() }
        Accessible.role: Accessible.SpinBox
        Accessible.name: root.label
        Accessible.description: root.suffix === "°" ? qsTr("Degrees") : ""
    }

    ScrubSpinBox {
        id: field
        anchors.fill: parent
        value: root.value
        from: root.from
        to: root.to
        step: root.step
        decimals: root.decimals
        trimTrailingZeros: true
        reservePrefix: root.reservePrefix
        textFont: uiFont
        prefix: root.prefix
        prefixIcon: root.prefixIcon
        suffix: root.suffix
        enabled: root.enabled
        onCommitted: (value) => {
            root.valueEdited(root.quantize(value));
            // A model clamp is authoritative; re-read it after the write.
            Qt.callLater(function() {
                if (!field.scrubbing && field.value !== root.value)
                    field.value = root.value;
            });
        }
    }

    HoverHandler {
        id: fieldHover
        enabled: root.enabled
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        enabled: root.enabled && root.channel >= 0 && root.row
        onSingleTapped: {
            root.openMenuAction = root.row ? root.row.channelKeyAction(root.channel) : ""
            if (root.openMenuAction !== "")
                keyMenu.popup()
        }
    }

    Menu {
        id: keyMenu
        MenuItem {
            text: root.openMenuAction
            onTriggered: if (root.row) root.row.toggleChannelKey(root.channel)
        }
    }

    Connections {
        target: root
        function onValueChanged() {
            if (!field.scrubbing && field.value !== root.value)
                field.value = root.value;
        }
    }
}
