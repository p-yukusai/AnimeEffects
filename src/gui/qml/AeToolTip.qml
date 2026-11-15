pragma Translator: "AnimeEffects.Dialogs"

import QtQuick
// `Overlay` (the window's overlay item) is exported by QtQuick.Templates, not
// by QtQuick: Qt 6.12's QtQuick/Templates/plugins.qmltypes declares it as the
// attached type of QQuickOverlay, and the plain QtQuick module has no such
// type. The import exists for that one attached lookup below.
import QtQuick.Templates

// A hover label for an icon-only control.
//
// It labels an item named by ROLE, not by parentage: `ownerItem` is the control
// (or row) whose rectangle the bubble is placed against, and the bubble itself
// is mounted in the WINDOW OVERLAY while it is revealed:
//   AeToolTip { ownerItem: root; text: root.label; shown: root.showTip && mouse.containsMouse }
// (E29, decided 2026-09-30, keeps tooltips ONLY on the Auto/Reset affordances
// and on picker rows whose label truncates - see AeIconButton's `showTip`.)
//
// WHERE IT LIVES (decided 2026-10-26, from notes.md's "tooltips in the filter
// card items draw behind the cards so they're invisible"). It used to be a
// visual CHILD of the control it labels. A child's `z` orders it only among that
// control's own siblings, so anything painted after the control covered it: the
// filter card's expanded body is a LATER sibling of the header that owns the
// icon and is opaque (`theme.base`, InspectorFilterCard.qml), and every later
// card in the stack paints after that. The Properties panel is also one clipped
// `ScrollView` (PropertiesPanel.qml), so a bubble that had escaped the card
// would still have been cut at the viewport. The overlay is above all scene
// content, and is not inside any of those clips.
//
//   * `ownerItem` is REQUIRED at every call site and is never defaulted to
//     `parent`: `parent` is the overlay whenever the bubble is mounted, so
//     `parent` would chase the reparent instead of naming the labelled item.
//   * the ROOT stays an Item. A Popup root would break the
//     qobject_cast<QQuickItem*> discovery that tooltip_contract, form_layout and
//     picker_keyboard use to find the tip, and a Popup brings focus and close
//     policy - and, in its Window/Native popup types, a window of its own - to
//     a label that must have none of them.
//   * it is mounted ONLY while `revealed`. Qt's QQuickOverlay makes itself
//     visible as soon as it has any child item (qquickoverlay.cpp, on
//     ItemChildAddedChange: `setVisible(!allDrawers.isEmpty() ||
//     !childItems().isEmpty())`). A permanently attached hidden child would
//     leave a full-window item that accepts all mouse buttons (the overlay sits
//     at z 1000001) alive for the application's whole life, filtering every
//     press; the `parent` binding below is what prevents that.
//   * z: 1000000 is load-bearing, not a large number for its own sake. An
//     in-window Popup is parented to the same overlay (and
//     InspectorFilterStack.qml opens one). openPopup() in qquickpopup.cpp only
//     raises a newly opened popup to the current top POPUP's z
//     (`popupItem->setZ(qMax(topPopupItem->z(), popupItem->z()))`); it never
//     raises a popup above a non-popup sibling. A popup opened AFTER the tip
//     was inserted is later in the overlay's child order and would otherwise
//     paint over the bubble.
//   * enabled: false is E30's other half: a Rectangle and a Text are ordinary
//     items and would accept the left button. A label must not be a pointer
//     target, and the overlay's own press path ignores (does not consume) a
//     press while it has no popup, so the control underneath still gets it.
//
// WHERE THE BUBBLE LANDS is the contract's, not Qt's (E27/E28):
//   E27 - below its owner, or above when there is no room below (measured
//         2026-09-30 in host-spike: stock Controls.ToolTip places ABOVE always,
//         which is not the line's placement);
//   E28 - never clipped by the dialog's edge: shifted and flipped to stay
//         INSIDE the window (the host ruling of 2026-09-30; the measured defect
//         this replaces: a 324px tip overhung a 500px window by 114px and was
//         simply cut, `x: parent.width/2 - width/2` with no clamp at all).
// The owner's rectangle arrives as ownerX/Y/W/H in the OWNER's coordinate frame
// (default: the owner's whole rectangle). Keeping the rect in the owner's
// frame is what lets one call site - the picker's row tooltip - keep writing
// the row rect in its list area's coordinates, while the bubble is positioned
// in overlay coordinates; ownerRect() does the one conversion.
//
// Hand-rolled for the design's sake, not for want of Controls: a Popup would
// take a window of its own and the shared instance behind Controls' attached
// ToolTip cannot be positioned per site. (The old header gave a different
// reason - "a QQuickWidget cannot render a popup outside its own bounds" -
// which is false of Qt 6.11: a `Popup.Window` does draw outside the scene, and
// whether it maps from a QQuickWidget host on Wayland depends on the
// compositor's backend, measured 2026-10-03 - see the notes section titled
// "The Wayland limitation is a backend limitation".
// That is still not why this exists.)
//
// TIMING (contract line E26): it appears after a delay and never instantly, and it
// hides after a timeout or as soon as the pointer leaves.
//
// The delay is a CONSTANT of the design rather than a value read from Qt, and that
// is measured, not assumed: it was chosen on Qt 6.11.2, where there was nothing to
// read at all - that release ships no tooltip-delay API (the whole installed header
// tree matches no showDelay, toolTipDelay or hideDelay), Qt Quick Controls' ToolTip.delay
// defaults to 0, no installed style overrides it, and Qt's own QToolTip fires 1-2 ms
// after the hover (measured, this spike: ENTER 2846 ms / TOOLTIP 2848 ms). So "the
// platform's wake-up delay" here means the conventional desktop one: 500 ms, then 10 s
// before the tip retires itself. NOTE for whoever moves this next: the tree's floor is
// Qt 6.12 now, and 6.12 DOES have QStyleHints::toolTipWakeUpDelay (read-only) - the
// constant can become the platform's value, and that is a deliberate design change to
// make, not a rename.
Item {
    id: root

    // The item this label belongs to. REQUIRED at every call site - see "where
    // it lives" above for why this cannot default to `parent`.
    property Item ownerItem: null

    property string text: ""
    // Driven by the owner: true while the pointer is on the control.
    property bool shown: false

    // The two E26 timings. Named and at the top because the contract calls them
    // out, and because if the project moves to Qt 6.12 the first one should become
    // `Qt.styleHints.toolTipWakeUpDelay` (read-only there) rather than a constant.
    property int delay: 500
    property int timeout: 10000

    // Whether the label is actually on screen: `shown` only REQUESTS it, after
    // `delay`. Keeping the two apart is what makes "the pointer left while the
    // tooltip was still waiting" simply cancel, instead of showing a tooltip for
    // something the pointer has already left.
    property bool revealed: false
    visible: revealed && text !== ""
    // Mounted in the window overlay, and only while there is a bubble to draw.
    // See "where it lives": an attached-but-hidden child would keep the overlay
    // (a full-window, all-buttons item) visible forever.
    parent: (revealed && ownerItem) ? ownerItem.Overlay.overlay : null
    z: 1000000
    enabled: false
    implicitWidth: label.implicitWidth + 14
    // 22 is the single-line look as it stands; a multi-line tooltip (the picker
    // row's full label + identification) grows to fit rather than spilling out
    // of a fixed-height bubble - "shows it in full" (E29) and "never clipped"
    // (E28) both require the bubble to fit its text.
    implicitHeight: label.lineCount > 1 ? label.implicitHeight + 8 : 22

    // The owner's rectangle in the OWNER's coordinates (0-relative to ownerItem
    // unless a call site moves it). ownerW/H follow the owner's own size, which
    // is the common case; the picker overrides them with the hovered row's.
    property real ownerX: 0
    property real ownerY: 0
    property real ownerW: ownerItem ? ownerItem.width : 0
    property real ownerH: ownerItem ? ownerItem.height : 0

    // The owner's rectangle ONCE, converted into overlay coordinates - the frame
    // the x/y bindings below live in.
    //
    // mapToItem() cannot notify, so the geometry the mapping rests on is read
    // here ON PURPOSE: the owner's own x/y/width/height and every ancestor's
    // x/y/width/height/contentX/contentY. Those reads - not the map - are the
    // binding dependencies. Without them the bubble keeps whatever it computed
    // when it was created (measured: after a scroll under a stationary pointer
    // the bubble stayed at its pre-scroll y while the row moved;
    // picker_keyboard's scroll block reveals its tooltip FIRST and then scrolls,
    // which is the only way that stale binding shows - its 'gap %1 -> %2' check
    // is the assertion that catches this regression). form_layout's E28 block
    // measures the OTHER half of the same dependency: the clamp must narrow to
    // the scrolling viewport at all.
    function ownerRect() {
        if (!root.parent || !root.ownerItem) return { x: 0, y: 0, w: 0, h: 0 };
        var dep = 0;
        // NB: QML items walk by `.parent` (the item parent); `parentItem` is a
        // C++ getter and reads as undefined here, which silently ends the walk
        // after one hop - measured (form_layout's edge case caught it).
        for (var a = root.ownerItem; a; a = a.parent)
            dep += a.x + a.y + a.width + a.height
                 + (a.contentX !== undefined ? a.contentX + a.contentY : 0);
        var p = root.ownerItem.mapToItem(root.parent, ownerX, ownerY);
        return { x: p.x, y: p.y, w: ownerW, h: ownerH };
    }

    // The rectangle the bubble must FIT, in OVERLAY coordinates. Start from the
    // overlay's own bounds (the window) and narrow to the visible part of every
    // clipping ancestor of the OWNER: since D25 the form's content scrolls
    // inside a clipped Flickable, and a bubble clamped to the WINDOW alone would
    // still be cut by that viewport - the same E28 defect arriving through a new
    // door (measured class: a 324px tip overhanging its bounds by 114px and
    // simply cut). Each ancestor's rect is converted into overlay coordinates
    // because the bubble no longer lives in the ancestor's frame. Flickable-
    // derived views count as clipping even at clip:false: conservative, and the
    // cost is a bubble that stays a few pixels further inside than strictly
    // needed beside a non-clipping view. The reads below are as much for the
    // BINDING as for the math - a scroll changes only ancestors, and mapToItem
    // cannot notify. Called inside the x/y bindings so placement re-evaluates
    // with the geometry it rests on.
    function clampRect() {
        if (!root.parent || !root.ownerItem) return { l: -1e9, t: -1e9, r: 1e9, b: 1e9 };
        var l = 0, t = 0, r = root.parent.width, b = root.parent.height;
        for (var a = root.ownerItem; a; a = a.parent) {
            var dep = a.x + a.y + a.width + a.height
                    + (a.contentX !== undefined ? a.contentX + a.contentY : 0);
            if (a.clip || a.contentX !== undefined) {
                var o = a.mapToItem(root.parent, 0, 0);
                l = Math.max(l, o.x);
                t = Math.max(t, o.y);
                r = Math.min(r, o.x + a.width);
                b = Math.min(b, o.y + a.height);
            }
        }
        return { l: l, t: t, r: r, b: b };
    }
    function clamp(v, lo, hi) { return hi < lo ? lo : Math.max(lo, Math.min(hi, v)); }

    // E27: below the owner, flipping above only when there is no room below.
    // E28: then shifted vertically so the tip is never cut at either edge -
    // the flip chooses the side, the clamp guarantees the fit.
    y: {
        if (!root.parent || !root.ownerItem) return 0;
        var w = clampRect();
        var o = ownerRect();
        var below = o.y + o.h + 6;
        var v = (below + height <= w.b) ? below : o.y - height - 6;
        return Math.round(clamp(v, w.t, w.b - height));
    }
    // E27: centred on the owner, not at the pointer; E28: shifted inside.
    x: {
        if (!root.parent || !root.ownerItem) return 0;
        var w = clampRect();
        var o = ownerRect();
        return Math.round(clamp(o.x + o.w / 2 - width / 2, w.l, w.r - width));
    }

    // The owner going away (or a call site clearing the role) must not leave a
    // stale bubble mounted on the overlay.
    onOwnerItemChanged: if (!ownerItem) disarm()

    function disarm() {
        showTimer.stop();
        hideTimer.stop();
        revealed = false;
    }
    function arm() {
        disarm();
        showTimer.start();
    }
    onShownChanged: { if (shown) arm(); else disarm(); }
    // NEW TEXT WHILE STILL HOVERED IS A NEW TOOLTIP and gets its own delay. The
    // picker is the case that matters: its rows set `shown: hoveredRow >= 0`, which
    // stays true while the pointer scans from row to row and only `text` changes.
    // Without this the label would follow the pointer instantly - the same defect
    // E26 names, arriving through the back door. Re-arming also restarts the hide
    // timeout, so every tooltip gets the full 10 s.
    onTextChanged: { if (shown) arm(); }
    Component.onCompleted: { if (shown) arm(); }

    Timer {
        id: showTimer
        interval: root.delay
        // Re-checked here rather than trusted: `shown` may have gone false while the
        // delay ran (a fast pointer), and the text can be cleared the same way.
        onTriggered: {
            if (root.shown && root.text !== "" && root.ownerItem) {
                root.revealed = true;
                hideTimer.restart();
            }
        }
    }
    Timer {
        id: hideTimer
        interval: root.timeout
        onTriggered: root.revealed = false
    }

    Rectangle {
        anchors.fill: parent
        radius: 4
        color: theme.floaterBody
        border.width: 1
        border.color: theme.floaterEdge
    }
    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: theme.text
        font.pixelSize: 11
    }
}
