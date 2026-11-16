import QtQuick
import QtQuick.Window

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
    // The inspector uses the app's 8pt Geist face, not the dialog controls'
    // fixed 11px. Callers that do not set this keep the dialog look.
    property font textFont: Qt.font({ pixelSize: 11 })
    // The property inspector shows 357.5, not 357.500. Off by default: the
    // dialogs that asked for fixed decimals keep their exact display.
    property bool trimTrailingZeros: false
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
    // In-field labels: Figma-style axis letters (X/Y/W/H), an optional leading
    // glyph, and a trailing unit (the inspector's angle °). Dialog fields leave
    // all three empty and keep their original geometry.
    property string prefix: ""
    property string prefixIcon: ""
    property string suffix: ""
    // Every inspector field reserves the same small lead-in, so an X/Y glyph (or
    // no glyph at all) cannot move the number horizontally. Dialog fields keep
    // their compact 8px inset.
    property bool reservePrefix: false
    readonly property int contentLeft: reservePrefix ? 24
                                              : (prefixIcon !== "" ? 24 : (prefix !== "" ? 18 : 8))
    readonly property int prefixSlotLeft: reservePrefix ? 4 : 8
    readonly property int prefixSlotWidth: reservePrefix ? 15 : Math.max(0, contentLeft - prefixSlotLeft)
    // NOTE: the enabled state is Item.enabled itself, deliberately NOT redeclared:
    // a `property bool enabled` here would shadow the base property, the item would
    // never report disabled to the accessibility tree, and a probe could not tell a
    // frozen field from a live one. The fields below bind to root.enabled and the
    // scrub/edit handlers gate on it.

    // In-field text editing. Exposed so a caller can keep a live preview (the
    // Properties panel's mesh overlay) up while the value is being typed.
    readonly property bool editing: editInput.activeFocus

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

    function fmt(v) {
        var text = Number(v).toFixed(root.decimals);
        if (root.trimTrailingZeros && text.indexOf(".") >= 0)
            text = text.replace(/0+$/, "").replace(/\.$/, "");
        return text === "-0" ? "0" : text;
    }
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

    // TYPE-STEPPING. The old widget fields were QAbstractSpinBoxes: Up/Down and
    // the wheel stepped the value (Shift x10), and their stepBy emitted
    // editingFinished. The QML replacement had neither, which is the reported
    // regression. While the text editor is open a step only moves the local
    // value/text - Enter or focus-out commits it, and Escape still restores the
    // value the edit began with. Outside the editor a wheel notch is a complete
    // edit and commits immediately.
    property real wheelAccumulator: 0

    function stepBy(aSteps, aModifiers) {
        if (!root.enabled || aSteps === 0)
            return;
        const rate = (aModifiers & Qt.ShiftModifier) ? 10 : 1;
        const next = root.clampNv(root.value + aSteps * rate * root.step);
        if (next === root.value)
            return;
        root.value = next;
        if (editInput.activeFocus) {
            editInput.text = root.fmt(next);
            editInput.selectAll();
        }
        root.changed(next);
        if (!editInput.activeFocus)
            root.committed(next);
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
        anchors.leftMargin: root.contentLeft
        anchors.rightMargin: 8
        verticalAlignment: Text.AlignVCenter
        color: root.enabled ? theme.text : theme.textDisabled
        font: root.textFont
        text: root.fmt(root.value) + (root.showValue && root.suffix !== "" ? root.suffix : "")
        visible: !editInput.activeFocus && root.showValue
    }

    Item {
        id: prefixSlot
        x: root.prefixSlotLeft
        width: root.prefixSlotWidth
        height: parent.height
        visible: root.showValue && (root.prefix !== "" || root.prefixIcon !== "")
        Text {
            anchors.centerIn: parent
            visible: root.prefix !== ""
            text: root.prefix
            color: theme.textMutedDim
            opacity: root.enabled ? 1.0 : 0.45
            font: root.textFont
        }
        Image {
            // 15px is the largest integer glyph that centers exactly in the
            // 23px field (4px above, 4px below); 14px left it on a half pixel.
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.floor((parent.height - height) / 2)
            visible: root.prefixIcon !== ""
            width: 15
            height: 15
            source: root.prefixIcon === "" ? "" : theme.iconUrl(root.prefixIcon)
            // SVG sourceSize is in PIXELS. Size the raster to the icon's
            // physical footprint so a high-DPI screen does not stretch a 14px
            // bitmap; at scale 1 this is the same 14px that removes the
            // four-times-downsample blur of the old 64px source.
            sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
            sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
            fillMode: Image.PreserveAspectFit
            smooth: true
            opacity: root.enabled ? 1.0 : 0.45
        }
    }

    // Hover feedback for the field (HoverHandler matches the checkbox/buttons).
    HoverHandler {
        id: hoverH
        enabled: !editInput.activeFocus
    }

    // THE WHEEL STEPS ONLY IN TEXT EDIT, as the old fields did: outside the editor
    // the wheel belongs to the surrounding ScrollView (a form full of fields must
    // still scroll). One notch is one step (Shift x10); high-resolution deltas are
    // accumulated so a touchpad cannot fire a step per pixel.
    WheelHandler {
        enabled: root.enabled && editInput.activeFocus
        onWheel: (wheel) => {
            const hasAngle = wheel.angleDelta.y !== 0;
            const delta = hasAngle ? wheel.angleDelta.y : wheel.pixelDelta.y;
            if (delta === 0)
                return;
            const unit = hasAngle ? 120.0 : 15.0;
            root.wheelAccumulator += delta;
            const steps = Math.trunc(root.wheelAccumulator / unit);
            if (steps !== 0) {
                root.wheelAccumulator -= steps * unit;
                root.stepBy(steps, wheel.modifiers);
            }
            wheel.accepted = true;
        }
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
        // A scrub IS a drag, and the form it usually sits in is a Flickable whose own
        // drag scrolls the page. A Flickable steals the mouse from a child MouseArea
        // once the drag passes its threshold, which is how editing a value also moved
        // the view (reported 2026-10-08: "when dragging the scrubbers, the drag also
        // propagates to the scroll area. This leads to the user accidentally scrolling
        // while changing values"). preventStealing is the documented way to say the
        // control owns the gesture that started on it; the steal was real enough that
        // the onCanceled handler below exists for it. A drag starting anywhere else
        // still scrolls the page; the wheel is the spin box's own (see stepBy).
        preventStealing: true

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
        anchors.leftMargin: root.contentLeft
        anchors.rightMargin: 8
        verticalAlignment: Text.AlignVCenter
        color: theme.text
        // F33: the selection is the theme's selection pair, like AeTextField.
        selectionColor: theme.selection
        selectedTextColor: theme.selectionText
        font: root.textFont
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
        // The dialog's Escape Shortcut (ExportDialog.qml:77) is matched
        // by the shortcut map BEFORE the key reaches this editor, and a
        // Keys handler cannot claim a key from a Shortcut: only an
        // accepted ShortcutOverride can (QQuickKeyEvent::reset() always
        // clears the override's acceptance, so nothing else accepts it).
        // Without this handler, Escape in an edit cancels the WHOLE
        // dialog instead of ending the edit (F33/F34) - measured
        // 2026-10-09 by driving the real dialog, and pinned by the
        // window-route case in tests/form_layout.cpp. This fires only
        // while THIS editor holds focus, so Escape still cancels the
        // dialog everywhere else (A2).
        Keys.onShortcutOverride: (event) => {
            if (event.key === Qt.Key_Escape)
                event.accepted = true;
        }
        Keys.onEscapePressed: {
            cancelEdit = true;
            focus = false;
            event.accepted = true; // swallow: do not let the dialog close
        }
        Keys.onUpPressed: (event) => {
            root.stepBy(1, event.modifiers);
            event.accepted = true;
        }
        Keys.onDownPressed: (event) => {
            root.stepBy(-1, event.modifiers);
            event.accepted = true;
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
