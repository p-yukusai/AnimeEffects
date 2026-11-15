import QtQuick

// Hand-rolled Blender-style numeric input: drag horizontally to scrub the
// value, click to type a value directly. Not a QC2 SpinBox: a SpinBox could
// host the styling (it is delegate-based — its editor is SpinBox.contentItem, a
// TextInput with readOnly: !editable, measured in Basic/SpinBox.qml) but it
// assumes an always-present editor, which contradicts the design's
// display-until-clicked mode and the scrub shell's click/focus needs; the scrub
// is behaviour no toolkit ships as a delegate (measured inventory:
// docs/export-dialog-implementation-notes.md, "Control inventory", numeric field).
// Binds to the "theme" context property.
Item {
    id: root

    property real value: 0
    property real from: 0
    property real to: 100
    property real step: 1
    property int decimals: 0
    // Optional C++ host object with beginScrub()/endScrub(). The dialog uses it
    // so it knows a scrub is active and forwards the Qt::Popup re-targeted mouse
    // moves back to the scene with the held button restored — because inside a
    // Qt::Popup the QML scene stops scrubbing once the cursor leaves the popup
    // (Qt re-targets those moves with buttons()=NoButton). No grabMouse here.
    property var mouseGrabber: null
    // A caller may draw a WORD for a sentinel value instead of the number (the
    // export dialog shows the codec's "Auto" and the still writer's "Auto" this
    // way), in which case the number underneath must not show through it.
    property bool showValue: true
    // NOTE: the enabled state is Item.enabled itself, deliberately NOT redeclared:
    // a `property bool enabled` here would shadow the base property, the item would
    // never report disabled to the accessibility tree, and a probe could not tell a
    // frozen field from a live one. The fields below bind to root.enabled and the
    // scrub/edit handlers gate on it.

    // Our OWN scrub flag, set on press and cleared on release / popup hide. The
    // QML scrub must gate on this, not the MouseArea's `pressed`: Qt's popup path
    // can present an outside move with buttons()=NoButton, whose delivery can
    // drop the button/release signal that would normally keep `pressed` true.
    property bool scrubbing: false

    // Emitted on every live value change (each scrub tick, and on a typed
    // commit) so the model can track the field and the owner can re-sync while
    // the edit is still happening — NOT only on release.
    signal changed(real value)
    // Emitted when the value is finalized (drag released, Enter, or focus out).
    signal committed(real value)

    implicitWidth: 72
    implicitHeight: 24

    function fmt(v) { return Number(v).toFixed(root.decimals); }
    function clampNv(v) { return Math.max(root.from, Math.min(root.to, v)); }
    function commitText(t) {
        const n = parseFloat(t);
        if (!isNaN(n)) root.value = root.clampNv(n);
    }
    // Enter edit mode from OUTSIDE the pointer path. The accessibility layer's
    // "press" on the spin button lands here (see AeNumberField.qml's
    // Accessible.onPressAction): focusing the ScrubSpinBox Item alone does NOT give
    // the inner TextInput focus, so a screen reader's press - and a GUI probe that
    // drives the field through the accessibility action, because Qt reports a QML
    // item's screen extents in the wrong coordinate space - would type into nothing.
    // This is the same state a plain click enters, so the two routes agree.
    function beginEdit() {
        editInput.editStartValue = root.value;
        editInput.text = root.fmt(root.value);
        editInput.forceActiveFocus();
    }

    // Raised input field matching the app's dock input fields (propertywidget.ssa
    // QDoubleSpinBox): raised fill at rest, brightens to raisedHover + hover
    // border on hover, switches to the neutral "active" token (the one buttons
    // use) while pressed/scrubbing. The border is invisible at rest (same-as-fill)
    // so it only shows on hover/focus.
    Rectangle {
        id: field
        anchors.fill: parent
        radius: 4
        color: !root.enabled ? theme.recessed
              : scrub.pressed ? theme.active
              : (hoverH.hovered ? theme.raisedHover : theme.raised)
        border.color: !root.enabled ? theme.recessed
                      : editInput.activeFocus ? theme.focus
                      : (hoverH.hovered ? theme.hover : theme.raised)
        border.width: 1
    }

    // Read-only value display (below the MouseArea so the value text shows
    // through the transparent scrub layer; hidden while editing).
    Text {
        id: shown
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        verticalAlignment: Text.AlignVCenter
        color: root.enabled ? theme.text : theme.textDisabled
        font.pixelSize: 11
        text: root.fmt(root.value)
        visible: !editInput.activeFocus && root.showValue
    }

    // Hover feedback for the field (HoverHandler matches the checkbox/buttons).
    HoverHandler {
        id: hoverH
        enabled: !editInput.activeFocus
    }

    // Scrub + click-to-edit, ABOVE the value text so it reliably receives the
    // hover and pointer events. A click (press with no drag) enters edit mode;
    // a drag scrubs the value. Enabled only while not editing so the (on-top)
    // TextInput receives the pointer while typing. The cursor follows the mode:
    // the horizontal scrub hint over the scrub target (F32), an I-beam while the
    // field is text to select and edit (F31) — a constant SizeHor here was
    // measured (run.sh captures) painting the double arrow over the text even
    // after a hover re-evaluation, and the switch must land at the click, before
    // this area disables itself and the pointer has moved.
    MouseArea {
        id: scrub
        anchors.fill: parent
        enabled: !editInput.activeFocus && root.enabled
        cursorShape: editInput.activeFocus ? Qt.IBeamCursor : Qt.SizeHorCursor
        hoverEnabled: true

        property real pressX: 0
        property real pressValue: 0
        property real lastX: 0
        property bool dragged: false

        onPressed: {
            pressX = mouseX; pressValue = root.value; lastX = mouseX; dragged = false;
            scrubbing = true;
            if (root.mouseGrabber && root.mouseGrabber.beginScrub)
                root.mouseGrabber.beginScrub();
        }
        onPositionChanged: function(mouse) {
            if (!scrubbing) return;
            const dx = mouseX - pressX;
            if (!dragged && Math.abs(dx) > 2)
                dragged = true;
            if (dragged) {
                // Shift scrubs 10x finer, for precision (Blender-style). The
                // change is applied incrementally so toggling Shift mid-drag
                // just changes the rate without a jump.
                const fine = mouse.modifiers & Qt.ShiftModifier;
                const divisor = fine ? 40.0 : 4.0; // ~4px/step normally, ~40 with shift
                root.value = root.clampNv(root.value + (mouseX - lastX) / divisor * root.step);
                lastX = mouseX;
                root.changed(root.value);
            }
        }
        onReleased: {
            scrubbing = false;
            if (root.mouseGrabber && root.mouseGrabber.endScrub)
                root.mouseGrabber.endScrub();
            if (dragged) {
                // Scrub finished: finalize.
                root.committed(root.value);
            } else {
                // Plain click: enter edit mode, selecting the value for typing.
                root.beginEdit();
            }
        }
        onCanceled: {
            // The press was canceled (e.g. the popup closed, or another item
            // took the grab) — a release that would clear `scrubbing` may never
            // arrive. Reset the scrub state so the field isn't left "scrubbing"
            // on the next open.
            scrubbing = false;
            dragged = false;
            if (root.mouseGrabber && root.mouseGrabber.endScrub)
                root.mouseGrabber.endScrub();
        }
    }

    // Direct-entry field, on top while editing. Selects the whole value on
    // focus so the user can type over it; commits on Enter or focus out; Escape
    // cancels the edit (restores the value the edit BEGAN with - F34, decided
    // 2026-09-30) and blurs the field without letting the event close the
    // window.
    TextInput {
        id: editInput
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        verticalAlignment: Text.AlignVCenter
        color: theme.text
        // F33: the selection is the theme's selection pair, like AeTextField.
        selectionColor: theme.selection
        selectedTextColor: theme.selectionText
        font.pixelSize: 11
        visible: activeFocus
        enabled: root.enabled
        selectByMouse: true
        // F31: over the text the pointer is an I-beam. This handler owns the
        // cursor for a FRESH hover in edit mode (leaving and re-entering the
        // field); the mode switch at the click itself is the MouseArea's
        // cursorShape binding above.
        HoverHandler { cursorShape: Qt.IBeamCursor }
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        objectName: "scrubEdit"
        property bool cancelEdit: false
        // F34: the value this edit BEGAN with, captured where the edit starts
        // (both routes into an edit land in these two spots). Escape restores
        // THIS - not "whatever the property holds when the edit ends": the owner
        // can write `value` while an edit is open (the dialog re-syncs on aspect
        // locks and range clamps; the accessibility setter writes the same way),
        // and an undone edit must never keep a value the edit never saw.
        // Measured: without this snapshot, "keep the committed value" silently
        // keeps the re-sync.
        property real editStartValue: 0
        Keys.onEscapePressed: {
            cancelEdit = true;
            focus = false;
            event.accepted = true; // swallow: do not let the dialog close
        }
        onAccepted: focus = false // commit via onActiveFocusChanged
        onActiveFocusChanged: {
            if (activeFocus) {
                // F34: the edit starts HERE, so the snapshot is taken here too -
                // it covers any route that focuses the editor without beginEdit().
                editStartValue = root.value;
                selectAll();
            } else {
                if (cancelEdit) {
                    // Escape (F34): undo the edit back to where it began. The
                    // restore is the same kind of write the scrub path makes, so
                    // it re-syncs the owner the same way: changed() fires ONLY if
                    // the value actually moved (a cancelled edit that changes
                    // nothing reports nothing), and committed() never fires -
                    // a cancel finalizes nothing.
                    cancelEdit = false;
                    if (root.value !== editStartValue) {
                        root.value = editStartValue;
                        root.changed(root.value);
                    }
                } else {
                    root.commitText(text);
                    root.changed(root.value);
                    root.committed(root.value);
                }
            }
        }
    }
}
