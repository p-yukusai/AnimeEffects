pragma Translator: "AnimeEffects.Dialogs"

import QtQuick

// A hover label for an icon-only control.
//
// It is a CHILD of the control it labels, which is what gives it a position; the
// owner drives it:
//   AeToolTip { text: root.label; shown: root.showTip && mouse.containsMouse }
// (E29, decided 2026-09-30, keeps tooltips ONLY on the Auto/Reset affordances
// and on picker rows whose label truncates - see AeIconButton's `showTip`.)
//
// WHERE IT SITS is the contract's, not Qt's (E27/E28):
//   E27 - below its owner, or above when there is no room below (measured
//         2026-09-30 in host-spike: stock Controls.ToolTip places ABOVE always,
//         which is not the line's placement);
//   E28 - never clipped by the dialog's edge: shifted and flipped to stay
//         INSIDE the window (the host ruling of 2026-09-30; the measured defect
//         this replaces: a 324px tip overhung a 500px window by 114px and was
//         simply cut, `x: parent.width/2 - width/2` with no clamp at all).
// The owner's rectangle arrives as ownerX/Y/W/H in THIS item's coordinate frame
// (default: the whole parent), because one call site - the picker's row tooltip
// - parents the label to the list area while positioning it under a row.
//
// E30: it is drawn ABOVE its owner (the z, below) and does not intercept the
// pointer: it declares no pointer handlers, so hover and clicks pass through.
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
// is measured, not assumed: on Qt 6.11.2 there is nothing to read. Qt 6.11 ships no
// tooltip-delay API at all - the whole installed header tree matches no showDelay,
// toolTipDelay or hideDelay - Qt Quick Controls' ToolTip.delay defaults to 0, no
// installed style overrides it, and Qt's own QToolTip fires 1-2 ms after the hover
// (measured, this spike: ENTER 2846 ms / TOOLTIP 2848 ms). QStyleHints::
// toolTipWakeUpDelay only exists from 6.12. So "the platform's wake-up delay" here
// means the conventional desktop one: 500 ms here, 10 s before it retires itself.
Item {
    id: root

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
    z: 1000
    implicitWidth: label.implicitWidth + 14
    // 22 is the single-line look as it stands; a multi-line tooltip (the picker
    // row's full label + identification) grows to fit rather than spilling out
    // of a fixed-height bubble - "shows it in full" (E29) and "never clipped"
    // (E28) both require the bubble to fit its text.
    implicitHeight: label.lineCount > 1 ? label.implicitHeight + 8 : 22

    // The owner's rectangle in the tooltip's PARENT's coordinates. Defaults:
    // the whole parent (the common case - the tooltip is a child of its owner).
    property real ownerX: 0
    property real ownerY: 0
    property real ownerW: parent ? parent.width : 0
    property real ownerH: parent ? parent.height : 0

    // The rectangle the bubble must FIT, in the tooltip's PARENT's coordinates.
    // Start from the hosting window (its content item is the topmost ancestor of
    // any in-scene item) and narrow to the visible part of every clipping
    // ancestor: since D25 the form's content scrolls inside a clipped Flickable,
    // and a bubble clamped to the WINDOW alone would still be cut by that
    // viewport - the same E28 defect arriving through a new door (measured class:
    // a 324px tip overhanging its bounds by 114px and simply cut). Flickable-
    // derived views count as clipping even at clip:false: conservative, and the
    // cost is a bubble that stays a few pixels further inside than strictly
    // needed beside a non-clipping view. The walk reads each ancestor's
    // contentX/contentY/width/height ON PURPOSE - those reads are what make the
    // x/y bindings below re-evaluate when the content scrolls under a revealed
    // bubble. Called inside the x/y bindings so placement re-evaluates with the
    // geometry it rests on.
    function clampRect() {
        if (!root.parent) return { l: -1e9, t: -1e9, r: 1e9, b: 1e9 };
        var top = root.parent;
        while (top && top.parent) top = top.parent;
        var l = -1e9, t = -1e9, r = 1e9, b = 1e9;
        // NB: QML items walk by `.parent` (the item parent); `parentItem` is a
        // C++ getter and reads as undefined here, which silently ends the walk
        // after one hop - measured (form_layout's edge case caught it).
        for (var a = root.parent; a; a = a.parent) {
            // The reads below are as much for the BINDING as for the math:
            // mapToItem cannot notify, so without reading x/y/contentX/contentY
            // of every ancestor the bubble keeps whatever it computed at
            // creation (measured: tip.y frozen 30px past the viewport after a
            // scroll while clampRect() itself returned the right rect).
            var dep = a.x + a.y + a.width + a.height
                    + (a.contentX !== undefined ? a.contentX + a.contentY : 0);
            if (a === top || a.clip || a.contentX !== undefined) {
                var o = root.parent.mapToItem(a, 0, 0);
                l = Math.max(l, -o.x);
                t = Math.max(t, -o.y);
                r = Math.min(r, a.width - o.x);
                b = Math.min(b, a.height - o.y);
            }
        }
        return { l: l, t: t, r: r, b: b };
    }
    function clamp(v, lo, hi) { return hi < lo ? lo : Math.max(lo, Math.min(hi, v)); }

    // E27: below the owner, flipping above only when there is no room below.
    // E28: then shifted vertically so the tip is never cut at either edge -
    // the flip chooses the side, the clamp guarantees the fit.
    y: {
        if (!parent) return 0;
        var w = clampRect();
        var below = ownerY + ownerH + 6;
        var v = (below + height <= w.b) ? below : ownerY - height - 6;
        return Math.round(clamp(v, w.t, w.b - height));
    }
    // E27: centred on the owner, not at the pointer; E28: shifted inside.
    x: {
        if (!parent) return 0;
        var w = clampRect();
        return Math.round(clamp(ownerX + ownerW / 2 - width / 2, w.l, w.r - width));
    }

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
            if (root.shown && root.text !== "") {
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
