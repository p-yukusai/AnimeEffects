#ifndef CTRL_GRAPH_SCROLLBARGEOMETRY_H
#define CTRL_GRAPH_SCROLLBARGEOMETRY_H

#include <QRect>
#include <QtCore/qnamespace.h>

namespace ctrl {
namespace graph {

// THE APPLICATION'S SCROLL BAR, AS ONE DEFINITION: a 12px invisible lane, and a
// 4px pill with 2px caps drawn across the slider's run, centred in the lane.
//
// TWO LAYERS PAINT THAT PICTURE, and they have to paint the SAME one:
//   * gui::AppStyle's CC_ScrollBar (src/gui/AppStyle.cpp) - every Qt QScrollBar in
//     the application, the timeline's bar included. Qt hands it a widget rect and
//     a slider rect (SC_ScrollBarSlider) and it fills one rounded rect in a theme
//     token.
//   * gui::drawPanel's scroll-bar pass (src/gui/GraphPanelFrame.cpp) - the graph
//     panel's two lanes, whose track and run rects are the MODEL's
//     (ctrl::graph::Scaler::scrollBars publishes them as hTrack/hThumb and
//     vTrack/vThumb) and which fills the same rounded rect through ImDrawList.
//
// They used to state the numbers twice: AppStyle.cpp's
// kTrackThickness/kPillThickness/kPillRadius, and graph_Scaler.h's
// kScrollBarThickness/kScrollBarPillThickness/kScrollBarPillRadius, each side
// also carrying its own copy of the "(lane extent - pill thickness) / 2"
// centring. The panel's copy said only that "the lane and the run are the
// model's", which explains the INPUTS, not why the arithmetic was restated: a
// change to the lane or the pill could reach one painter and not the other, and
// the panel - measured against the app's bar pixel for pixel - would be the one
// that was wrong.
//
// WHY THIS LAYER: gui may include ctrl and ctrl may not include gui, so ctrl is
// the only layer the style and the model can share. HEADER-ONLY on purpose:
// src/gui/AppStyle.cpp is compiled straight into several test targets that do
// not link the ctrl library at all (the easing picker's layout/shots), so a
// definition in ctrl/graph/graph_Scaler.cpp would add a link dependency for
// geometry that is four lines long.
//
// THE GATE: tests/graph_panel_model.cpp asks pill() for a table of
// (lane, run, orientation) cases and measures gui::AppStyle's PAINTED pill
// against the answer - a QImage paint of a real QScrollBar through the app
// style, with the pill's band read off the pixels. tests/graph_panel_pixels.cpp
// measures the panel's DRAWN pill against the same answer, off the framebuffer,
// using the model's published rects. A divergence in either painter, or in this
// function, fails one of them.
namespace scrollBar {

// The lane: the scroll bar's 12px extent. It is AppStyle's PM_ScrollBarExtent,
// the height of graph_Scaler's hTrack and the width of its vTrack, and the
// reserve the graph's plot gives up at its right and bottom edges. THE TROUGH IS
// INVISIBLE: the lane is a hit area and a layout reserve, and only the pill is
// painted in it.
constexpr int kLaneThickness = 12;

// The pill across the run: 4px thick, centred in the lane.
constexpr int kPillThickness = 4;

// The pill's cap radius: a semicircle across its 4px thickness.
constexpr int kPillRadius = 2;

// One pill: where it is, and the radius its corners are rounded by. A QRect
// cannot carry the radius, so it is returned WITH the rect instead of being left
// for each painter to remember - a rect from here and a radius from somewhere
// else is the divergence this header exists to prevent.
struct Pill {
    QRect rect;
    int radius = kPillRadius;
};

// THE PILL for one lane and one run.
//
//   aLane   the whole scroll-bar rect: the QWidget's (AppStyle) or the model's
//           TRACK (the panel). It supplies the thickness axis, and the pill is
//           CENTRED in it - (lane extent - kPillThickness) / 2 from the lane's
//           leading edge. Integer division, so an odd lane puts the extra pixel
//           on the trailing side (13px lane -> 4px offset, 4px pill, 5px after
//           it).
//   aRun    the slider's rect (AppStyle: SC_ScrollBarSlider; the panel:
//           Scaler::hThumb / vThumb). The pill spans it EXACTLY along the scroll
//           axis - no inset and no minimum length - and inherits its offset, so a
//           run that starts mid-lane puts the pill there.
//   aOrientation
//           Qt::Vertical for a bar that scrolls vertically (the pill is
//           kPillThickness wide and as tall as the run), Qt::Horizontal for the
//           other axis (kPillThickness tall and as wide as the run).
//
// Both callers paint `pill.rect` rounded by `pill.radius`, and nothing else:
// gui::AppStyle with QPainter::drawRoundedRect, the panel with
// ImDrawList::AddRectFilled.
constexpr Pill pill(const QRect& aLane, const QRect& aRun, Qt::Orientation aOrientation) {
    if (aOrientation == Qt::Vertical) {
        const int offset = (aLane.width() - kPillThickness) / 2;
        return Pill{ QRect(aLane.left() + offset, aRun.top(), kPillThickness, aRun.height()), kPillRadius };
    }
    const int offset = (aLane.height() - kPillThickness) / 2;
    return Pill{ QRect(aRun.left(), aLane.top() + offset, aRun.width(), kPillThickness), kPillRadius };
}

} // namespace scrollBar
} // namespace graph
} // namespace ctrl

#endif // CTRL_GRAPH_SCROLLBARGEOMETRY_H
