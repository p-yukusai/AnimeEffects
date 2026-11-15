pragma Translator: "AnimeEffects.Dialogs"

import QtQuick
import QtQuick.Controls

// A full-width disclosure header: a caret, a label, and a clickable row.
//
// Qt Quick Controls' ToolButton with this design's OWN caret, words and hover row -
// the `contentItem` and `background` delegates - so the look is the design's and Qt
// supplies activation, Space, hover, the focus policy and the button role.
//
// The old header's argument stands, and is now an argument about ONE property rather
// than about the toolkit: the widget form used QToolButton with setArrowType, and
// `QToolButton:checked` paints the checked state with the QSS `@focus@` colour, so
// expanding "Advanced" filled the header with the accent - the one thing the user did
// NOT select. That is why `checkable` stays OFF here: `open` is the model's word, and
// this control draws nothing when open except the caret turning and the label
// brightening, with the content below as the feedback.
//
// Measured on this Qt before converting (see docs/export-dialog-implementation-notes):
//   * `toggled()` EXISTS on AbstractButton and can be emitted from QML - which is what
//     makes `onClicked: root.toggled()` possible while the control is NOT checkable,
//     so the call sites' `onToggled` keeps working untouched.
//   * Space fires `clicked` on RELEASE (press only sets `down`), so the hand-written
//     Keys.onSpacePressed would have toggled twice. Dropped, replaced by nothing.
//   * Return and Enter are neither consumed nor acted on (accepted=0, no click), so
//     the design's own Keys.onReturnPressed below still receives them - kept, because
//     this control, unlike the checkbox, has always handled Return.
//   * `hoverEnabled` defaults to FALSE and `focusPolicy` to 0xB (StrongFocus). The
//     widget QToolButton is **TabFocus** (measured: click does not focus it), which is
//     also what the hand-rolled control did - `activeFocusOnTab` plus a MouseArea that
//     takes no focus - so TabFocus is stated rather than inherited.
//   * `click()` exists, for Accessible.onPressAction.
//
// THE FOCUS RING is contract line A1: this header declares `activeFocusOnTab`, so the
// focused control must show its ring, and it drew none. `standard.ssa` has no
// `QToolButton:focus` rule to borrow (it has `QPushButton:focus { border-color:
// @focus@ }`), and Qt's own Basic ToolButton draws a border on `visualFocus`, so the
// ring is the same one AeButton draws: 1px of theme.focus around the row, the only
// border this header ever has.
ToolButton {
    id: root

    // NO `property string text`: ToolButton has it. Redeclaring shadows the base
    // property (qmllint: [property-override]) - see AeButton's note on `enabled`.
    // `open` is ours: ToolButton has no such property, and it is the model's state
    // rather than Qt's `checked`, which is deliberately unused (see the header).
    property bool open: false

    implicitHeight: 30
    focusPolicy: Qt.TabFocus
    hoverEnabled: true
    // The header carries its own words, so a screen reader (and a probe) addresses it
    // by the same name the widget form gave the QToolButton, and is told whether it is
    // open.
    Accessible.role: Accessible.Button
    Accessible.name: root.text
    Accessible.description: root.open ? qsTr("Expanded") : qsTr("Collapsed")
    Accessible.onPressAction: if (root.enabled) root.click()

    background: Rectangle {
        radius: 4
        color: root.hovered ? theme.raised : "transparent"
        // The ring is the only border this row ever draws, and only for keyboard
        // focus (`visualFocus`), as on AeButton: a ring left behind by the pointer is
        // not a focus ring.
        border.width: root.visualFocus ? 1 : 0
        border.color: theme.focus
    }

    // Our caret and words rather than Qt's IconLabel+Text pair: the design fixes a
    // 14px caret at x=4 with the words 6px after it, left-aligned in a full-width row.
    // The row is anchored to the LEFT of the content item - Qt places `contentItem`
    // inside its padding, so anchoring it here is what puts the caret at x=4 rather
    // than at x=padding (measured: the same control's label came out 6px off in the
    // checkbox until its position was stated explicitly).
    contentItem: Item {
        Row {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Image {
                anchors.verticalCenter: parent.verticalCenter
                width: 14
                height: 14
                source: theme.iconUrl(root.open ? "caret-down" : "caret-right")
                sourceSize.width: 64
                sourceSize.height: 64
                fillMode: Image.PreserveAspectFit
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.text
                color: root.hovered || root.open ? theme.text : theme.textMuted
                font.pixelSize: 12
            }
        }
        anchors.left: parent.left
        anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
    }

    HoverHandler {
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
    }

    // Activation, in one place: Qt's own Space (on release - measured) and the pointer
    // both arrive here, and both become the design's `toggled`. Return is NOT handled
    // by Qt (measured), so the design's own handling of it follows.
    onClicked: root.toggled()
    Keys.onReturnPressed: (event) => {
        if (!root.enabled) return;
        root.toggled();
        event.accepted = true;
    }
    // Deliberately no Keys.onSpacePressed: Qt toggles on release, so this would fire
    // twice.
}
