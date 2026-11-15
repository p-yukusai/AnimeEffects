pragma Translator: "AnimeEffects.Dialogs"

import QtQuick
import QtQuick.Controls

// A raised dropdown whose popup is an IN-SCENE list — by design, not by
// impossibility. This file's earlier comment claimed Controls' ComboBox cannot
// render its popup outside the hosting QQuickWidget; that claim was measured
// false on this Qt (6.11): the stock popup is `popupType: Popup.Item`, parented
// to the window overlay inside the widget's own window. A window-backed
// `Popup.Window` draws outside the scene — on xcb always; on Wayland whether it
// maps at all from a QQuickWidget host depends on the compositor's backend
// (measured 2026-10-03: weston's x11 backend maps it, its headless backend does
// not; notes, "The Wayland limitation is a backend limitation").
// The design keeps the list in-scene regardless —
// export-dialog-behavior G37: "the popup is the design's in-scene list" — and
// Qt's popup positioner performs the same dance `updatePopupGeometry()` did:
// below the field, flipped above when there is no room, clamped inside the
// window as the last resort (measured against the old control's geometry).
//
// `options` is a list of { label, value } maps (the shape the view model
// exposes), `index` is the current row and is written ONLY by the call sites'
// bindings (`index: exportModel.X`); `onActivated` is how every user choice
// reaches the model. Measured: stock emits `activated` exactly once for
// type-ahead on the closed field, for Enter's commit, and for a row click;
// a popup-open type-ahead moves only the highlight and emits nothing (the
// field does not change, so the model stays consistent). Up/Down are ours —
// see the Keys handler below.
//
// Type-ahead ships because Q6/G39 was decided: typing letters on the closed
// field selects the matching item. It needs `textRole: "label"` — with the
// raw {label,value} objects keySearch has no text to match (measured: idx
// never moved, and the style logged `Unable to assign QVariantMap to
// QString`); with the role, 'a'/'d' from row 2 select Alpha/Delta and the
// warning disappears.
//
// Escape and Space and Enter are the control's own (measured against the old
// hand-rolled file): Escape closes an open popup and otherwise PROPAGATES —
// the old control accepted it unconditionally and so failed design line A2
// ("Escape cancels") whenever the field was closed; that is a defect this
// conversion fixes. Space toggles on release (the AeDisclosure precedent).
// Enter with the popup open commits the highlighted row and dismisses (G37);
// with the popup closed it is swallowed, and nothing in the shell consumes a
// plain Return anyway (A2 scopes Return to the buttons and shortcuts).
//
// `flat` marks a field that reports a frozen fact: same shape, recessed fill.
// `overlay` is kept for the call sites — popup positioning belongs to Qt now.
ComboBox {
    id: root

    property var options: []
    property int index: -1
    // `flat` marks a field that reports a frozen fact (same shape, recessed fill)
    // — and it is Qt's OWN property on ComboBox (`Q_PROPERTY(bool flat … FINAL
    // REVISION(2,1))`), not ours: redeclaring it is the load error this file used
    // to hit (`Cannot override FINAL property`). The old control declared it
    // locally with this meaning, no call site has ever set it to anything, and
    // the background below is the only thing that reads it.
    // Kept so the call sites' `overlay: root` stays valid; the popup no longer
    // needs it — Qt's positioner places the list (below / flipped above /
    // clamped inside the window, measured).
    property Item overlay: null

    readonly property string value: (index >= 0 && index < options.length)
                                    ? String(options[index].value) : ""
    readonly property string text: (index >= 0 && index < options.length)
                                   ? String(options[index].label) : ""
    // The field's open state, in the old control's words (the background's
    // focus-coloured border reads it, as it read `popupOpen` before).
    readonly property bool popupOpen: popup.visible

    // index → currentIndex only. Every user path reaches the model through
    // `activated`, so the binding re-evaluates and comes back through here;
    // writing `index` from inside would destroy that binding, which is why
    // this control never does.
    onIndexChanged: if (index !== currentIndex) currentIndex = index

    model: options
    textRole: "label"
    focusPolicy: Qt.TabFocus        // the old control was Tab-only; stock's default
                                    // is StrongFocus and a click would TAKE focus
                                    // (measured) — it must not.
    hoverEnabled: true
    padding: 0                      // the field spans the full control, as the old
                                    // Rectangle did (stock padding would shrink it)
    // The style's own double-arrow: it lives OUTSIDE background/contentItem, so
    // overriding those still left it drawn (measured: 133 differing pixels, a second
    // caret beside ours). The old control had one caret; this is how it has none.
    // The Basic style's padding bindings null-check `indicator`, so null is safe.
    indicator: null
    implicitWidth: 160
    implicitHeight: 28

    // G37: Up/Down CHOOSE. Two measured facts shape this:
    //   * `highlightedIndex` is READ-ONLY from QML — writing it throws
    //     `Cannot assign to read-only property`, and the first version of this
    //     filter died on that line mid-handler, silently dropping the emission
    //     that came after it (the run_keys matrix caught it: closed worked only
    //     because stock rescues that case itself, open committed nothing);
    //   * stock's Down/Up with the popup OPEN moves only the highlight (no
    //     `activated`), while closed it moves the index AND emits exactly once.
    // So the filter only MARKS the key and lets stock run: stock moves the
    // highlight as it always did, and the highlight's change is mirrored to
    // `activated` only when a Down/Up put it there — one emission per press,
    // index and highlight ending on the same row, so Enter/Space then commit
    // what the eye sees. A type-ahead also moves the highlight while open; it
    // is not flagged, so it stays highlight-only until Enter commits it (the
    // decided closed-field type-ahead emits through stock itself, measured).
    property bool _keyNav: false

    Keys.onPressed: (event) => {
        event.accepted = false;
        _keyNav = event.key === Qt.Key_Down || event.key === Qt.Key_Up;
    }

    onHighlightedIndexChanged: {
        if (!_keyNav || highlightedIndex < 0 || highlightedIndex === currentIndex) return;
        _keyNav = false;
        root.activated(highlightedIndex);
    }

    // Toggle for the accessible node's press action (and the field's own
    // open/close), with the old control's guard: a disabled or empty combo
    // never opens.
    function togglePopup() {
        if (popup.visible) { popup.close(); return; }
        if (!root.enabled || root.options.length === 0) return;
        popup.open();
    }

    // The field points like every other control in this dialog: the old control's
    // field MouseArea carried `cursorShape: Qt.PointingHandCursor`, and every
    // sibling keeps it the same way (AeButton's HoverHandler — copied here). An
    // offscreen grab can never show this (no pointer in the pixels), so it is
    // verified by photographing the Xvfb screen with `ffmpeg -draw_mouse 1`.
    HoverHandler {
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
    }

    // ---- the field ----------------------------------------------------------
    background: Rectangle {
        radius: 4
        color: !root.enabled || root.flat ? theme.recessed
             : root.hovered ? theme.raisedHover : theme.raised
        border.width: 1
        border.color: root.popupOpen ? theme.focus
                    : root.hovered ? theme.hover
                    : (root.enabled && !root.flat ? theme.raised : theme.recessed)
    }

    contentItem: Item {
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 9
            anchors.right: caret.left
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root.enabled ? theme.text : theme.textDisabled
            font.pixelSize: 12
            elide: Text.ElideRight
        }
        Image {
            id: caret
            anchors.right: parent.right
            anchors.rightMargin: 7
            anchors.verticalCenter: parent.verticalCenter
            width: 12
            height: 12
            source: theme.iconUrl("caret-down")
            sourceSize.width: 64
            sourceSize.height: 64
            fillMode: Image.PreserveAspectFit
            opacity: root.enabled ? 0.85 : 0.4
        }
    }

    // The accessible COMBO node. Qt Quick's Accessible attached type cannot give
    // this control the protocols it needs (see gui/qml/QmlFormAccessibility): Qt
    // exposes a press action for Button/CheckBox/RadioButton/Switch roles, not for
    // a combo, and it reports a QML item's screen rectangle in the QQuickWidget's
    // coordinates. This childless Item carries the identity and the current value,
    // and the C++ factory gives it role ComboBox, a name that IS the current value
    // (Qt's own rule), the identity as its description, a press action that
    // opens/closes the list, and a screen-correct rectangle. Qt now also creates
    // its OWN ComboBox node for the control (measured: role ComboBox with an empty
    // name and description), which no probe can match by name — the marker stays
    // the addressable one.
    Item {
        id: accessibleCombo
        objectName: "aeAccessibleCombo"
        anchors.fill: parent
        enabled: root.enabled
        property string accessibleName: root.objectName
        property string accessibleValue: root.text
        function accessiblePress() { root.togglePopup(); }
        Accessible.role: Accessible.ComboBox
        Accessible.name: root.text
        Accessible.description: root.objectName
    }

    // ---- the popup ---------------------------------------------------------
    // NON-modal on purpose: the swallow the modal overlay used to give was a
    // DEFECT, not a feature. With `modal: true` the overlay consumed every press
    // that landed outside the list — and a combo's own field is outside its list —
    // so pressing the field (or its arrow) did not close the open list, and
    // pressing a SECOND combo only closed the first without opening it (reported
    // 2026-10-11; both symptoms are that one swallow). With `modal: false` Qt's
    // own control does the right thing on both: a press on the field toggles the
    // list, and a press on another combo closes this one and opens that one.
    // dim stays off: the old scrim was transparent.
    //
    // What is NOT a defect, and looks like one when two combos are stacked 36px
    // apart (the settings dialog): an OPEN LIST COVERS THE FIELDS BELOW IT, so a
    // press there lands on a list row and chooses it. That is what a dropdown
    // does; the row at the top of the list is usually the current value, which is
    // why it reads as "the list just closed". The pass-through is only visible
    // where the list does not cover the target.
    popup: Popup {
        modal: false
        dim: false
        // Below the field, with the old control's 2px gap: this is the same `y`
        // binding the Basic style ships for its own popup (`y: control.height`),
        // shifted by the gap `updatePopupGeometry()` used, and the 4px margins are
        // its clamp. (Without a `y` binding the positioner had no baseline and the
        // list opened AT the field instead of under it — measured: popup y=0/scene
        // 100 where the old control placed 130.) Qt's positioner keeps the
        // below → flip-above → clamp dance from there.
        y: root.height + 2
        topMargin: 4
        bottomMargin: 4
        width: root.width
        // The old control's own sizing rule, kept: a short list is its
        // contentHeight, a long one caps at 220.
        height: Math.min(220, list.contentHeight + 8)
        padding: 4                  // the old ListView's anchors.margins

        background: Rectangle {
            radius: 4
            color: theme.floaterBody
            border.width: 1
            border.color: theme.floaterEdge
        }

        // x/y are the positioner's (see the header): under the field, flipped
        // above when there is no room, clamped inside the window.
        //
        // …and the list always opens at the TOP, like the old control's: Qt's
        // ComboBox scrolls its popup ListView to the current row on open
        // (measured: contentY=48 with currentIndex=0 — row 2 pulled to the top)
        // while the old list never moved from contentY=0. No contract line asks
        // for the scroll, so the reference decides.
        onOpened: {
            list.contentY = 0;
            root._keyNav = false;
        }
        onClosed: root._keyNav = false
        contentItem: ListView {
            id: list
            model: root.options
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            delegate: Rectangle {
                required property int index
                required property var modelData
                width: list.width
                height: 24
                radius: 3
                // Each option is its own addressable node, AND it needs a press
                // action: Qt exposes one for Button/CheckBox/RadioButton/Switch
                // roles, not for a list item, so "open the list and pick an item"
                // was impossible for a screen reader - and for a probe - without
                // the C++ interface gui/qml/QmlFormAccessibility attaches to this
                // marker. Its rect() is screen-correct too, so a click lands on it.
                objectName: "aeAccessibleOption"
                property string accessibleName: modelData.label
                property string accessibleValue: modelData.label
                function accessibleChoose() {
                    root.popup.close();
                    root.activated(index);
                }
                Accessible.role: Accessible.ListItem
                Accessible.name: modelData.label
                color: rowMouse.containsMouse ? theme.raisedHover
                     : (index === root.index ? theme.active : "transparent")
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    anchors.right: tick.left
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.label
                    color: theme.text
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
                Image {
                    id: tick
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    width: 12
                    height: 12
                    visible: index === root.index
                    source: theme.iconUrl("check-bold")
                    sourceSize.width: 64
                    sourceSize.height: 64
                    fillMode: Image.PreserveAspectFit
                }
                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.popup.close();
                        root.activated(index);
                    }
                }
            }
        }
    }
}
