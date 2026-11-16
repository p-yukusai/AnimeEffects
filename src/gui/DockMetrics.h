#ifndef GUI_DOCKMETRICS_H
#define GUI_DOCKMETRICS_H

namespace gui {

/**
 * One size range, shared by every panel - but applied per *area*, not per
 * panel.
 *
 * Two things went wrong when the panels each carried their own range. The
 * first is that a tab group is a single area, so its reachable sizes are the
 * intersection of its members': the widest minimum and the smallest maximum
 * win, and a panel whose content caps its own width leaves a gap beside itself
 * once the group is wider. That is what the three panels did - Tools was capped
 * at its design width of 172, Properties claimed 320..640, the timeline's
 * content asked for 380 - so no two of them could share an area sensibly, which
 * is the whole point of the tabs.
 *
 * The second is that the two axes are not the same kind of thing. A side area
 * is constrained in width and runs the full height of the window; a top or
 * bottom area is constrained in height and runs the full width. Giving every
 * panel both ranges meant the bottom panel took a maximum *width* of 640 and
 * could no longer span the window: the timeline was 640 wide with dead space
 * beside it. So the range is selected by the area the panel is in, and a
 * floating panel is constrained by neither (it is a window, and the compositor
 * resizes it).
 *
 * The minimum is also what a sash drag measures against: past it the panel
 * keeps shrinking to half of it, and only then collapses - the collapse is for
 * the deliberate "I want this gone" drag, not a hair past the usable size (see
 * DockSash::mouseMoveEvent).
 */
namespace dockMetrics {

/// Side areas (left, right): narrowest width before collapsing.
constexpr int kMinWidth = 200;
/// Side areas: widest, past which a sidebar is stretched empty space.
constexpr int kMaxWidth = 640;

/// Top/bottom areas: shortest height before collapsing.
constexpr int kMinHeight = 160;
/// Top/bottom areas: tallest.
constexpr int kMaxHeight = 640;

/// The floating window's frame, drawn inside the window: an unpainted grab
/// margin outside the painted line plus the ring the border is painted in. Both
/// are DockPanel's drawing, but the drop preview needs them too - a floating
/// panel's sizeHint includes the frame, and a preview built from it came out
/// exactly one frame too big on every axis (measured: 242 for a panel that Qt
/// then made 222 tall).
constexpr int kFrameMargin = 4;
constexpr int kFrameRing = 6;
/// What the frame adds to each axis of a floating panel.
constexpr int kFrameInset = kFrameMargin + kFrameRing;

/// Where a drag past the minimum turns into a collapse.
constexpr int kCollapseWidth = kMinWidth / 2;
constexpr int kCollapseHeight = kMinHeight / 2;

} // namespace dockMetrics

} // namespace gui

#endif // GUI_DOCKMETRICS_H
