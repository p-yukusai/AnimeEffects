pragma Translator: "AnimeEffects.Tools"

import QtQuick
import QtQuick.Controls

// A tool parameter as a SLIDER and a TYPED FIELD, on the dock's row grid.
//
// The widget version showed "Hardness:  50" on its own 16px label line above a
// QPainter slider: the value was readable but not editable, and the punctuation was
// hand-assembled. Two controls now share one value:
//   * the gauge for the feel (drag, click-to-position, wheel);
//   * the field for the exact number - a ScrubSpinBox, the dock's own numeric field,
//     which also takes a typed value and a drag-scrub and commits once on release/Enter.
// Either may write; both follow the model. The gauge's value is therefore SYNCED
// IMPERATIVELY rather than bound: a Slider writes its own `value` on every drag, which
// destroys a binding, so a bound gauge would stop following the field after the first
// drag. The `syncing` flag keeps the round trip from writing the model twice.
//
// The gauge's drawing is the deleted gui::tool::ToolSlider's, token for token: a 4px
// recessed pill, the filled part in the accent, and a solid accent head that travels
// the track MINUS its own diameter. A QSS QSlider cannot round a handle (Qt's
// subcontrol rendering caps border-radius below half the box), which is why that C++
// class existed at all; here the shapes are delegates and the problem is gone. It
// takes NO keyboard focus: a slider that did would swallow the arrow keys from the
// viewport after one click, which is what Qt::NoFocus was for there.
Item {
    id: root

    property string label: ""
    property real value: 0
    property real from: 0
    property real to: 100
    property real step: 1
    property int decimals: 0
    property string suffix: ""
    // The row's hover text. EVERY extra word this dock has to say lives here rather than
    // in a line of its own under the controls: a visible hint per row made the panel
    // busy, and the information is the same word for word.
    property string tip: ""
    signal valueEdited(real value)

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
        HoverHandler { id: labelHover }
        ToolTip.visible: labelHover.hovered && root.tip !== ""
        ToolTip.delay: 500
        ToolTip.text: root.tip
    }

    InspectorNumberField {
        id: field
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        width: 58
        height: 23
        // no prefix glyph in these fields, so no 24px lead-in either
        reservePrefix: false
        value: root.value
        from: root.from
        to: root.to
        step: root.step
        decimals: root.decimals
        suffix: root.suffix
        label: root.label
        onValueEdited: (value) => root.valueEdited(value)
    }

    Slider {
        id: gauge
        anchors.left: parent.left
        anchors.leftMargin: 90
        anchors.right: field.left
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        height: 18
        from: root.from
        to: root.to
        stepSize: root.step
        focusPolicy: Qt.NoFocus
        // The Controls style pads a slider by half a handle on each side, which pushed
        // the track's left end 6px right of the field column the rows above use -
        // MEASURED: the combo in a ToolRow starts at x=90 and the track's first inked
        // column was x=96, so the sliders read as misaligned with the dropdowns and 6px
        // short at both ends. The padding is zero here, so the track spans exactly the
        // column.
        leftPadding: 0
        rightPadding: 0

        // handle diameter, and the one number both shapes and the fill below agree on
        readonly property int headSize: 15
        // The head's centre, in the control's own coordinates: it travels the track
        // minus its own diameter, so the value is exact at BOTH ends (the widget's
        // headCenter() was the same function, which is what made its click mapping
        // land where its head was drawn).
        readonly property real headCentre: visualPosition * (availableWidth - headSize) + headSize / 2
        // True while this control is the writer, so the round trip below cannot write
        // the model a second time.
        property bool syncing: false

        handle: Rectangle {
            x: Math.round(gauge.leftPadding + gauge.headCentre - width / 2)
            y: Math.round((gauge.height - height) / 2)
            width: gauge.headSize
            height: gauge.headSize
            radius: width / 2
            color: gauge.enabled ? theme.accent : theme.textDisabled
        }
        background: Item {
            x: gauge.leftPadding
            y: Math.round((gauge.height - height) / 2)
            width: gauge.availableWidth
            height: 4

            Rectangle {
                anchors.fill: parent
                radius: height / 2
                // `raised`, not `recessed`: MEASURED in the light theme the empty part of
                // the track was #f5f5f5 on a #fafafa surface (1.07:1) so a slider sitting
                // at 0 read as a head and a field with nothing between them, while the
                // dark theme's recessed is darker than its base and read fine. `raised` is
                // the fill every field in the app uses (#e5e5e5 / #262626) and is visible
                // in both.
                color: gauge.enabled ? theme.outline : theme.hairline
            }
            Rectangle {
                width: Math.max(0, Math.round(gauge.headCentre))
                height: parent.height
                radius: height / 2
                color: gauge.enabled ? theme.accent : theme.textDisabled
            }
        }

        Component.onCompleted: {
            syncing = true;
            value = root.value;
            syncing = false;
        }
        // Every way the gauge can move - drag, press on the track, wheel - is a user
        // edit. `syncing` excludes the one write that is not.
        onValueChanged: if (!syncing) root.valueEdited(value)
        Connections {
            target: root
            function onValueChanged() {
                if (gauge.value === root.value)
                    return;
                gauge.syncing = true;
                gauge.value = root.value;
                gauge.syncing = false;
            }
        }
    }
}
