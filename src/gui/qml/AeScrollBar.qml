pragma Translator: "AnimeEffects.Dialogs"

import QtQuick
import QtQuick.Controls

// THE APPLICATION'S SCROLL BAR, in QML.
//
// One definition, because the same picture was being drawn four different ways: the
// widget bar (`gui::AppStyle`'s `CC_ScrollBar`, which every Qt scroll bar in the app
// uses - the object tree's and the timeline's included) paints a 12px invisible lane
// with a 4px pill in it, while the first QML copies invented an 8px lane with a 6px
// `textMuted` pill, and the two later ones copied the widget numbers but let the handle
// collapse to ONE pixel (measured 2026-10-26 on the blend list: a single 1px column of
// `outline` at the popup's edge, because the content item's `implicitWidth: 4` did not
// size the handle - Qt lays the handle out inside the control's own padding).
//
// The geometry is `ctrl::graph::scrollBar`'s, which is the ONE definition the widget
// style and the ImGui graph panel already share (`kLaneThickness` 12,
// `kPillThickness` 4, `kPillRadius` 2, the pill centred in the lane). The INK is the
// widget bar's: `theme.outline`, and `theme.hover` while the handle is hot - no fade,
// because the widget bar has none, and a bar that dims itself when idle reads as a
// different control beside one that does not.
//
// PADDING IS THE PILL. A vertical bar's lane is its `width`; `padding: 4` leaves the
// handle exactly `kPillThickness` wide and centres it, which is the same arithmetic as
// `ctrl::graph::scrollBar::pill`'s `(lane - pill) / 2` offset. Do not set the content
// item's implicit width instead - that is what collapsed to 1px.
//
// The trough is invisible (the surface shows through), the bar appears only when the
// content overflows a 28px minimum thumb, and the CALLER keeps deciding where the bar
// stands: every site parents it outside the clipping view (a bar parented to a
// Flickable is stacked into its content item and cut).
ScrollBar {
    id: root

    // The one thing the widget bar leaves to Qt: a floor on the handle's length, in the
    // fraction `minimumSize` takes (28px of the lane's own length).
    readonly property real laneLength: orientation === Qt.Vertical ? height : width

    // The THICKNESS of a vertical bar is its `width`, so every site passes
    // `width: 12`; a horizontal one would pass `height: 12` and leave the width to its
    // anchors. The component cannot pick the axis itself: a binding that named the other
    // axis would be self-referential.
    implicitWidth: 12
    implicitHeight: 12
    padding: 4
    policy: ScrollBar.AsNeeded
    visible: size < 1.0
    hoverEnabled: true
    minimumSize: laneLength > 0 ? 28 / laneLength : 0
    background: Item {}
    contentItem: Rectangle {
        radius: 2
        color: root.pressed || root.hovered ? theme.hover : theme.outline
    }
}
