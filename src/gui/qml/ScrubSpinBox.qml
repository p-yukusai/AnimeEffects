import QtQuick

// Hand-rolled Blender-style numeric input: drag horizontally to scrub the
// value, click to type a value directly. Deliberately NOT Qt Quick Controls —
// a QC2 SpinBox cannot be restyled to match the app's raised-input look nor
// given scrub-drag behavior. Binds to the "theme" context property.
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

    // Raised input field matching the app's dock input fields (propertywidget.ssa
    // QDoubleSpinBox): raised fill at rest, brightens to raisedHover + hover
    // border on hover, switches to the neutral "active" token (the one buttons
    // use) while pressed/scrubbing. The border is invisible at rest (same-as-fill)
    // so it only shows on hover/focus.
    Rectangle {
        id: field
        anchors.fill: parent
        radius: 4
        color: scrub.pressed ? theme.active
              : (hoverH.hovered ? theme.raisedHover : theme.raised)
        border.color: editInput.activeFocus ? theme.focus
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
        color: theme.text
        font.pixelSize: 11
        text: root.fmt(root.value)
        visible: !editInput.activeFocus
    }

    // Hover feedback for the field (HoverHandler matches the checkbox/buttons).
    HoverHandler {
        id: hoverH
        enabled: !editInput.activeFocus
    }

    // Scrub + click-to-edit, ABOVE the value text so it reliably receives the
    // hover and pointer events. A click (press with no drag) enters edit mode;
    // a drag scrubs the value. Enabled only while not editing so the (on-top)
    // TextInput receives the pointer while typing. The cursor hints the
    // horizontal scrub (not a click-pointer).
    MouseArea {
        id: scrub
        anchors.fill: parent
        enabled: !editInput.activeFocus
        cursorShape: Qt.SizeHorCursor
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
                editInput.text = root.fmt(root.value);
                editInput.forceActiveFocus();
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
    // cancels the edit (reverts to the committed value) and blurs the field
    // without letting the event close the window.
    TextInput {
        id: editInput
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        verticalAlignment: Text.AlignVCenter
        color: theme.text
        font.pixelSize: 11
        visible: activeFocus
        selectByMouse: true
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        objectName: "scrubEdit"
        property bool cancelEdit: false
        Keys.onEscapePressed: {
            cancelEdit = true;
            focus = false;
            event.accepted = true; // swallow: do not let the dialog close
        }
        onAccepted: focus = false // commit via onActiveFocusChanged
        onActiveFocusChanged: {
            if (activeFocus) {
                selectAll();
            } else {
                if (cancelEdit) {
                    // Escape: discard the typed text, keep the committed value.
                    cancelEdit = false;
                } else {
                    root.commitText(text);
                    root.changed(root.value);
                    root.committed(root.value);
                }
            }
        }
    }
}
