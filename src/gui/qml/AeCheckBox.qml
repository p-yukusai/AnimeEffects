pragma Translator: "AnimeEffects.Dialogs"

import QtQuick
import QtQuick.Controls

// A checkbox with its words beside it: a raised box, an accent tick when on, and a
// label that is part of the click target.
//
// Qt Quick Controls' CheckBox with the app's OWN box, tick and words. Those are the
// `indicator` and `contentItem` delegates, so the design keeps exactly what it
// specifies - raised fill, accent fill when ticked, a hairline only hover and focus
// ever recolour - while Qt supplies the toggle itself, Space, the focus policy, hover,
// the checkbox role in the accessibility tree, and `checked`.
//
// The old header justified the hand-rolled control with "a QC2 indicator draws its own
// chrome, and the app's checkboxes are a raised box with a centred accent tick". That
// chrome IS the delegate replaced below - the same reasoning as AeButton's
// `background`: it is a place to put this design, not a fixed look to accept or reject.
//
// Measured on this Qt before converting (see docs/export-dialog-implementation-notes):
//   * `toggled()` EXISTS (AbstractButton's) and fires ONLY on activation - click(),
//     Space, a press anywhere on the row. Writing `checked` from a binding or directly
//     does NOT fire it, which is what the old control's hand-emitted `toggled()` did
//     and what every `onToggled: model.value = checked` on the form depends on: no
//     write-back when the model pushes a new value.
//   * Space toggles, on RELEASE, with no `Keys` filter involved - so the hand-written
//     Keys.onSpacePressed would have toggled twice (pressed by us, released by Qt).
//     Dropped, replaced by nothing.
//   * Return and Enter are neither consumed nor acted on (accepted=0, checked
//     unchanged), so nothing else in the dialog loses them and this control needs no
//     Return handler - the old one had none either.
//   * The control does NOT place an anchored `indicator`/`contentItem` in x: our
//     anchors win (box at the left, label at box + 8). It DOES write the delegate's
//     width and height from its own padding - measured offscreen, the label came out
//     556x12 where the hand-rolled Text was 17px tall - which is why the label below
//     is anchored by its TOP to its own line box rather than centred: centring a box
//     Qt has just shrunk moved every word 3px down (the pixel comparison caught it as
//     1205 differing pixels in an otherwise identical row). Cross-references BETWEEN
//     the two delegates do not work at all: `anchors.left: ind.right` from the label
//     threw "ReferenceError: ind is not defined" at creation, so the label's offset
//     comes from `boxSize` rather than from the box's own width.
//   * `focusPolicy` defaults to 0xB, which IS Qt.StrongFocus (the enum is
//     Tab|Click|0x8, not 3): Tab and Click, matching the widget QCheckBox.
//   * A press anywhere on the ROW toggles - the label is part of the target, across
//     the full 568px the Advanced rows occupy - and `hoverEnabled` defaults to FALSE,
//     so hover has to be asked for.
//
// THE FOCUS RING is contract line A1 ("the focused control shows its focus ring"),
// which the hand-rolled control had no provision for at all: 30 Tabs over the real
// form changed nothing in the box (captured; see docs/export-dialog-implementation-
// notes). The shape is not invented - `standard.ssa` already specifies it for the
// widget checkbox, `QCheckBox::indicator:focus { border: solid @focus@ }`, so the box
// border turns to theme.focus and nothing else moves, with focus ahead of hover in the
// cascade exactly as the stylesheet has them. `activeFocus` rather than `visualFocus`
// because the stylesheet says `:focus` and not a keyboard-only variant, and because
// QCheckBox is StrongFocus (measured): clicking a checkbox in the widget dialogs
// focuses it and lights this same border.
//
// NO `property bool checked`, NO `property string text`, NO `signal toggled`: CheckBox
// has all three. Redeclaring one shadows the base property (qmllint:
// [property-override]) and the control then reports a state it does not actually have
// to the accessibility tree - the failure AeButton's `enabled` note describes.
CheckBox {
    id: root

    // The box is 16px in this design (0.75em + hairline in the stylesheet's own
    // units), and three things must agree on it: the box, the implicit width, and the
    // label's offset. One property rather than the number three times - the label
    // cannot read the box's width, see the ReferenceError above.
    readonly property int boxSize: 16
    // Dialog controls keep 12px; the inspector passes the app font (8pt Geist).
    property font textFont: Qt.font({ pixelSize: 12 })

    implicitHeight: 24
    implicitWidth: root.boxSize + (root.text === "" ? 0 : 8 + label.implicitWidth)
    // Explicit rather than a style's default, because contract line A1 needs Tab
    // traversal and this is also the pair the widget QCheckBox has (StrongFocus,
    // measured): a click that leaves the PREVIOUS control focused is how a Space
    // pressed after ticking a box types a space into whatever field had focus.
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    // The checkbox carries the words it is labelled with, so a screen reader (and a
    // probe) addresses it by the same name the widget form gave the QCheckBox.
    Accessible.role: Accessible.CheckBox
    Accessible.name: root.text
    Accessible.description: root.checked ? qsTr("Checked") : qsTr("Unchecked")
    Accessible.onPressAction: if (root.enabled) root.click()

    indicator: Rectangle {
        id: box
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: root.boxSize
        height: root.boxSize
        radius: 3
        color: !root.enabled ? theme.recessed
             : root.down ? theme.active
             : root.checked ? theme.accent
             : theme.raised
        border.width: 1
        // This hairline is the control's entire state display, in the stylesheet's
        // cascade: @outline@ at rest, @hover@ on hover, @focus@ while focused.
        border.color: !root.enabled ? theme.outline
                    : root.activeFocus ? theme.focus
                    : root.hovered ? theme.hover
                    : root.checked ? theme.accent : theme.outline

        Canvas {
            anchors.fill: parent
            visible: root.checked
            opacity: root.enabled ? 1.0 : 0.45
            onPaint: {
                const c = getContext("2d");
                c.reset();
                c.strokeStyle = theme.accentText;
                c.lineWidth = 2;
                c.lineCap = "round";
                c.lineJoin = "round";
                c.beginPath();
                c.moveTo(width * 0.22, height * 0.52);
                c.lineTo(width * 0.43, height * 0.74);
                c.lineTo(width * 0.78, height * 0.28);
                c.stroke();
            }
        }
    }

    contentItem: Text {
        id: label
        // Anchored in BOTH axes, and by the TOP rather than by the centre: Qt writes
        // this delegate's height from the control's padding (17px became 12px), and a
        // vertical centre over a shrunken box drops the words 3px - measured against
        // the hand-rolled control, whose words sat at y=3 in the 24px row. The margin
        // is that same geometry stated in terms of the line box, so it holds for any
        // row height: floor((24 - 17) / 2) = 3, exactly where they were.
        anchors.left: parent.left
        anchors.leftMargin: root.boxSize + 8
        anchors.top: parent.top
        anchors.topMargin: Math.floor((parent.height - implicitHeight) / 2)
        visible: root.text !== ""
        text: root.text
        color: root.enabled ? theme.text : theme.textDisabled
        font: root.textFont
    }

    HoverHandler {
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
    }

    // Deliberately no Keys.onSpacePressed (Qt toggles on release - measured) and no
    // Keys.onReturnPressed (Qt consumes neither - measured, and the old control had
    // no such handler either).
}
