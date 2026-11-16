pragma Translator: "AnimeEffects.Dialogs"

import QtQuick

// A numeric field with an optional icon-only reset beside it.
//
// The value field is ScrubSpinBox (drag to scrub, click to type) because that is
// what the rest of the app's numeric inputs are, and a dialog is a bad place to
// invent a second way to enter a number. The reset is an AeIconButton with no
// words on it: the widget form spelled it out ("Set to native") three times, so a
// form whose actual decisions are four numbers was mostly labels for buttons that
// undo an edit. The words live in `resetLabel`, where a screen reader and the
// tooltip still find them.
Item {
    id: root

    property real value: 0
    property real from: 0
    property real to: 100
    property real step: 1
    // Fractional values (the size cap is MB: 0.25 = 256 KiB). 0 keeps every
    // existing caller showing whole numbers.
    property int decimals: 0
    // The words a probe or a screen reader addresses this field by
    // ("Width", "Frame rate", ...). Required: a bare spin box announces nothing.
    property string label: ""
    // The icon-only reset: hidden when the words are empty.
    property string resetLabel: ""
    property bool resetVisible: true
    // The word this row reads when its value is NOT a number at all ("Auto" for the
    // codec's own knob and for the size cap). It is a STATE the owner sets, never a
    // comparison against a magic value: the old form compared the value to a sentinel
    // through Math.round, so a 0.25 MB cap - a real cap, 256 KiB - painted the word
    // "Auto" over itself (measured 2026-10-08).
    property string placeholder: ""
    property bool auto: false

    signal valueEdited(real value)
    signal resetRequested()

    // To the field's own decimals (0 keeps every existing caller at whole
    // numbers). See accessibleSetValue for the measured bug this replaces.
    function quantize(v) {
        const p = Math.pow(10, root.decimals);
        return Math.round(Number(v) * p) / p;
    }

    implicitHeight: 28
    // The field and its reset are one control on one row.
    implicitWidth: field.implicitWidth + (resetter.visible ? resetter.width + 4 : 0)

    // The accessible VALUE node.
    //
    // A hand-rolled QML spin box cannot expose AT-SPI's Value interface
    // declaratively - Qt Quick's Accessible attached type has no `value`,
    // `minimumValue` or `stepSize` - so the old widget form's QSpinBox had a
    // protocol this control did not: a screen reader could read AND SET the number
    // through the standard route, and so could scripts/gui/ae_ui.py. This childless
    // Item carries the identity, the range and a QML entry point, and the C++
    // factory in gui/qml/NumberFieldAccessibility turns it into a real
    // QAccessibleValueInterface (plus an ActionInterface whose press means "let me
    // edit", exactly as a mouse click does).
    //
    // Its geometry is the FIELD's, not the row's: that is what a probe clicks and a
    // magnifier frames. The label is a separate Item (AeFieldLabel) so it is not
    // swallowed here.
    Item {
        id: valueNode
        objectName: "aeAccessibleValue"
        x: field.x
        y: field.y
        width: field.width
        height: field.height
        enabled: root.enabled
        property string accessibleName: root.label
        property real accessibleValue: root.value
        property real accessibleMinimum: root.from
        property real accessibleMaximum: root.to
        property real accessibleStep: root.step
        // Called from C++ (QAccessibleValueInterface::setCurrentValue), so an
        // accessibility write takes the SAME path as a typed edit. Quantized to
        // the field's own precision: Math.round alone flattened the size cap's
        // 0.25 MB to 0 (measured 2026-09-30: probe_capped read back 0), and it
        // did the same to scrub drags on any decimals>0 field.
        function accessibleSetValue(value) {
            root.valueEdited(root.quantize(value));
        }
        function accessibleBeginEdit() { field.beginEdit(); }
        Accessible.role: Accessible.SpinBox
        Accessible.name: root.label
    }

    ScrubSpinBox {
        id: field
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width - (resetter.visible ? resetter.width + 4 : 0)
        height: 28
        value: root.value
        from: root.from
        to: root.to
        step: root.step
        decimals: root.decimals
        enabled: root.enabled
        // The word replaces the number: see the placeholder Text below.
        showValue: !root.auto
        // ScrubSpinBox writes `value` on a drag (which drops the caller's
        // binding), so the caller re-syncs it; changed() is what carries the value
        // back out.
        onChanged: (value) => { root.valueEdited(root.quantize(value)) }
    }
    AeIconButton {
        id: resetter
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        visible: root.resetLabel !== "" && root.resetVisible
        enabled: root.enabled
        iconName: "arrow-clockwise"
        label: root.resetLabel
        // E29's kept kind: the Auto/Reset affordance keeps its words as the
        // tooltip ("Reset to native" / "Reset to auto" - item 35's resetLabel).
        // Keyed on the words: a field with no resetLabel has no reset to hover.
        showTip: root.resetLabel !== ""
        onClicked: root.resetRequested()
    }
    // The word, over a hidden number: the field still carries a real value behind it
    // (where the next edit starts) but draws only the state's own name.
    Text {
        objectName: "aeAutoWord"
        anchors.left: parent.left
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        visible: root.auto
        text: root.placeholder
        color: theme.textMuted
        font.pixelSize: 11
    }
}
