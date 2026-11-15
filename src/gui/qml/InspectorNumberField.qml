pragma Translator: "AnimeEffects.Properties"

import QtQuick

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
    readonly property bool hovered: fieldHover.hovered
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
        reservePrefix: true
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

    Connections {
        target: root
        function onValueChanged() {
            if (!field.scrubbing && field.value !== root.value)
                field.value = root.value;
        }
    }
}
