#ifndef GUI_GRAPHPANELFRAME_H
#define GUI_GRAPHPANELFRAME_H

#include <QColor>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QVector>
#include <QtCore/qnamespace.h>

#include "core/TimeKeyType.h"
#include "ctrl/graph/graph_Geometry.h"
#include "gui/GraphPanelModel.h"
#include "gui/Ink.h"
#include "gui/theme/Graph.h"

class QFont;
class QWidget;

namespace core {
class TimeLineEvent;
}

namespace gui {

// What the drawing layer reads off the editor: the STICKY FOCUS, and nothing else.
// Passed in rather than read here, so the drawing stays a pure function of its
// inputs (and a headless pixel gate can hand over a synthetic one).
//
// THE SELECTION IS NOT HERE, and that is deliberate: which marker is selected, and
// which marker's key has a selected sibling, are published per key by the MODEL
// (PanelCurve::keySelected / keySiblingSelected), straight from the editor's own
// two queries. A list of selected keys here would be a SECOND source of the same
// fact - one that had to be matched back to a marker by pixel x, and that could
// silently disagree with the flags the model just published.
//
// The one thing the focus still decides is EACH CURVE'S TANGENT HANDLES
// (node/channel/valid): the panel draws them for one channel at a time because they
// dominate the panel's per-frame cost, and the editor's hit-test focuses a channel on
// a handle press. The curve EMPHASIS is the selection's, and it comes from the model's
// marker flags.
struct PanelFocus {
    core::ObjectNode* node = nullptr;
    ctrl::graph::Channel channel;
    bool valid = false;
};

// THE PANEL'S INK, expressed in the backend's vocabulary (gui/Ink.h) - one implementation of
// every SHAPE and INK decision, drawn by whichever backend the host supplies (Canvas Painter
// on the shipping tree, Qt's rasteriser in the parity gate). The pointer is the HOST's: a
// backend reads no pointer, so the same function paints a headless gate with any hover state
// and a window with the real cursor.
//
// The RULER is a SECTION AT THE PANEL'S TOP ROW: its band, hairline, ticks and frame numbers
// are drawn first, outside the plot's clip (the scaler reserved their rows in plotRect()), so
// nothing the plot draws can reach them. A tick or a number OUTSIDE the model's active range
// is drawn at the timeline's 0.4 recession alpha. The temporary PLAYBACK WINDOW is painted
// over the ruler content, in the timeline's order: the translucent band, the two 2px bracket
// stems, the label pills (or one merged pill) - all in the ruler band, before the plot clip
// and before the playhead. Then, clipped to the plot: the value ticks, the zero rule, the
// curves (the channels that own a selected marker, or the SIBLING of one, at full colour and
// every other curve dimmed - see curveStrength), the key markers (a filled dot of radius
// ctrl::graph::kKeyDotRadius about each key's centre, ringed in theme::Graph::keyEdge(), in
// one of the THREE states the model classifies), every segment's tangent handles, the
// box-select marquee, the name CHIP of every curve in play, and the empty state. The PLAYHEAD
// is drawn last, outside the plot clip and over the chips, from the pill's row in the band
// through the PANEL's last row. The two SCROLL BARS are drawn after everything else and
// clipped to the PANEL: the application's scroll bar - an INVISIBLE 12px trough and a 4px,
// radius-2 pill across the slider's run, in theme::Colors::outline at rest and
// theme::Colors::hover while the pointer is on that bar or it is being dragged
// (src/gui/AppStyle.cpp's CC_ScrollBar; the geometry is ctrl::graph::scrollBar::pill).
// THE RUN IS ALWAYS THE SLIDER: at zero travel BOTH lanes paint a pill across their WHOLE
// length, exactly as the timeline's AlwaysOn horizontal bar does.
//
// It draws the MODEL and nothing else: every rect it uses comes out of aModel (or is derived
// from the host's font, for the chips' text boxes). No background and no grid are drawn - the
// model publishes none, and the panel must leave the artwork showing through (a plot area
// with nothing in it is therefore untouched by this function).
void drawPanel(
    ink::Ink& aInk, const GraphPanelModel& aModel, const theme::Graph& aTheme, const PanelFocus& aFocus,
    const ink::Pointer& aPointer
);

// WHICH SCROLL-BAR LANE THE POINTER IS OVER. One answer for the three things that
// must agree about it: the drawing layer's hot test (a lane's pill takes
// theme::Colors::hover while the pointer is on it), the host's hover-repaint rule
// (gui::GraphPanelWidget::updateHoverRepaint repaints when this answer CHANGES, so
// entering a lane, leaving it, or crossing from one lane to the other is painted),
// and a pixel gate that asserts the hover ink. Reading the model's published tracks
// - not a second copy of the lane geometry - is what keeps them on one rect.
//
// The order is the model's own: the tracks are disjoint (the corner belongs to the
// horizontal bar), so at most one can contain the point; Horizontal is asked first
// only because its rect is the wider one.
enum class PanelBar { None, Horizontal, Vertical };

inline PanelBar panelBarAt(const GraphPanelModel& aModel, const QPoint& aPointer) {
    if (!aModel.hTrack.isEmpty() && aModel.hTrack.contains(aPointer))
        return PanelBar::Horizontal;
    if (!aModel.vTrack.isEmpty() && aModel.vTrack.contains(aPointer))
        return PanelBar::Vertical;
    return PanelBar::None;
}

// THE LABEL INK'S OWN DECISIONS, here rather than in the .cpp because the pixel gate asserts
// against them.

// Whether a label is drawn with a halo ring. SHIPPED OFF: the panel's floor is opaque in the
// dock, and the ring existed for the viewport overlay's arbitrary artwork. The ink backends
// still implement the halo (a backend that cannot ring text still draws the run).
constexpr bool kDrawTextHalo = false;
constexpr int kHaloRadius = 2;
constexpr float kHaloAlpha = 0.85f;

// How far a right-aligned value label is pulled left of its box's right edge, never by more
// than the box's own spare width. The timeline's kNumberMargin for the same defect: without
// it the top value label and the plot's first frame number meet in the panel's top-left
// corner and read as one string.
constexpr int kValueLabelGap = 8;

// ---------------------------------------------------------------------------
// THE PANEL'S FRAME: what the one host composes each paint, and what the headless pixel gate
// drives directly.
//
// The panel lives in the TIMELINE DOCK (the Graph tab's page, gui::GraphPanelWidget). drawPanel
// is a pure function of (model, palette, focus, pointer); composePanelFrame() is the one place
// that builds those from the live editor, so every host composes the SAME value and paints it
// with its own backend. That is what keeps the two views - and the gate - from disagreeing
// about what the panel says.
struct PanelFrameState {
    // The frame the pixels were drawn from and the editor it was built from (null
    // while the panel is hidden). A host's OWN interactions - the channel button, a
    // right-click hit, the accessible items - are tested against THIS geometry, so the
    // clickable control is exactly the drawn control.
    GraphPanelModel model;
    ctrl::GraphEditor* editor = nullptr;
    // The palette that frame was built and drawn with: theme::Graph::from of the LIVE
    // theme::Colors::current(), derived once per frame here (the app changes palette at
    // runtime, so there is nothing to cache).
    theme::Graph palette;
    // How many LOGICAL pixels of this host's top another widget covers. The dock's page
    // leaves it at 0 - nothing floats over the dock, the tab names the page and the
    // header belongs to the dock - so the ruler band occupies the panel's top row. The
    // field and the clamp stay: the inset is the model's (GraphPanelModel's THE OCCLUDED
    // TOP), and a host that floats something over the panel sets it rather than the model
    // inferring one.
    int topInset = 0;
};

struct PanelFrame {
    GraphPanelModel model;
    PanelFocus focus;
    theme::Graph palette;
    bool drawn = false;
};

PanelFrame composePanelFrame(
    ctrl::GraphEditor* aEditor, bool aVisible, const QSize& aViewport, PanelFrameState& aState,
    const QFont& aMeasuredFont, GraphPanelHost aHostStyle
);

// THE PANEL'S ACCESSIBILITY PUBLICATION.
//
// gui::a11y keeps ONE installation for the process (one factory, one widget, one published
// model), so the panel can be published exactly once, from the widget that owns it. That
// widget is the panel's host (gui::GraphPanelWidget), which installed the factory for itself
// in its CONSTRUCTOR (installFactoryOnly - see gui/PanelA11y.h, "WHERE TO CALL IT": a widget
// shown or parented while accessibility is active caches its ordinary interface first, and no
// factory installed later is reached for it). This call is the second half of that pairing: a
// repeat install for the SAME widget, which keeps the interface and the id and only takes over
// the action callback.
//
// The panel is published while the host is ON SCREEN and the EMPTY model while it is not: the
// tree says "no panel" rather than showing the last frame of a hidden tab.
//
// aOnAction(aUser, aId) is the host's own handler for the ids below, called synchronously from
// the accessibility call that activated the item. aHost is the WIDGET whose interface is
// published - its rasteriser is irrelevant.
void publishPanelAccessibility(
    QWidget& aHost, PanelFrameState& aState, void (*aOnAction)(void* aUser, int aId), void* aUser
);

// The panel's accessible item ids. No item is a row index (the channel list and the key menu
// are the application's own QMenus, not in the tree), so every id is a fixed negative; the
// host dispatches on them.
constexpr int kPanelSummaryItemId = -1;
constexpr int kPanelFitItemId = -2;
constexpr int kPanelKeyMenuItemId = -3;

// The drawn marker of the one key aTargets names, in the model's own pixels: the
// (node, channel type, frame) triple - the timeline's own name for a key - looked up
// in PanelCurve::keyFrames/keyDots, which are published beside each other. False when
// the selection names no single key the model carries (a key outside the plot's
// window, or on a hidden channel). It is the rectangle of the key menu's accessible
// action and the position the picker opens at.
bool panelSelectedKeyDot(const GraphPanelModel& aModel, const core::TimeLineEvent& aTargets, QPoint& aDot);

// The key under aPos, by the EDITOR's own rule: the nearest key whose centre is within
// ctrl::graph::kKeyHitRadius of it - the same distance, and the same `<=`,
// ctrl::GraphEditor::hitTest grabs a key with - reported as the triple (node, channel,
// frame) the timeline names a key by. The centre is PanelCurve::keyDots, not the key's
// HIT rect and not the drawn marker: the rect is that rule's square form (too small at
// the diagonals), and the marker is the ink, deliberately smaller than the target.
//
// False when no key is that close. The HOVER is not this query: the pointer lights either a
// key or a tangent handle, and which one it names is the shared pick gui::panelPickAt makes
// for the drawing layer and the host alike (see PanelPick), because a right-click may name a
// key where a plain press at the same point selects a relatively nearer handle.
bool panelKeyAt(
    const GraphPanelModel& aModel, const QPoint& aPos, core::ObjectNode*& aNode,
    ctrl::graph::Channel& aChannel, core::Frame& aFrame
);

// WHAT THE POINTER'S POSITION NAMES ON THE PANEL - one answer, one rule, for the hover ink
// (drawPanel) and the hover-repaint scheduling (gui::GraphPanelWidget::updateHoverRepaint).
//
// This is the panel-level half of ctrl::graph::pickTarget: it finds the NEAREST key (over
// every curve the model publishes, PanelCurve::keyDots) and the NEAREST published handle
// (PanelCurve::handles - and the model publishes exactly the handles the drawing layer inks),
// then lets pickTarget() decide which class the point names. The press decides the same
// question from the same geometry through the same function (ctrl::GraphEditor::hitTest), so
// the marker or handle the pointer lights is exactly the target a press at that point selects.
//
// The tie-break inside each class is the one this file always used: the LAST candidate at the
// same distance wins (`<=`).
//
// A HANDLE THE DRAWING LAYER DOES NOT INK is not in the model, so it is not a hover candidate
// here: with a focus set, only that channel's row publishes handles. A press there still GRABS
// the handle - the editor's hit-test scans the document, not the picture - and a handle press
// focuses that channel, so the handle is published, drawn and highlighted from the next frame
// on. Keys are published for every curve, so they are always candidates.
struct PanelPick {
    ctrl::graph::PickKind kind = ctrl::graph::PickKind::None;
    // The key, as the timeline's own triple - set when kind == Key. A curve whose node is
    // null is not hoverable (there is no layer to name), exactly as gui::panelKeyAt answers
    // it.
    core::ObjectNode* node = nullptr;
    ctrl::graph::Channel channel;
    core::Frame frame;
    // The handle, as indices into aModel.curves and that curve's `handles` - set when
    // kind == Handle, -1 otherwise.
    int curve = -1;
    int handle = -1;
};

PanelPick panelPickAt(const GraphPanelModel& aModel, const QPoint& aPos);

// WHAT IS LEFT IN THIS FILE: the panel's ink (gui::drawPanel and its helpers), the model-level
// palette decisions the gate asserts against (kDrawTextHalo, kValueLabelGap), the one frame
// composition both hosts share (composePanelFrame), the accessibility publication, and the two
// hit tests the host and the accessible items share (panelKeyAt, panelSelectedKeyDot).
//
// Every user-visible string in the .cpp goes through QCoreApplication::translate with the
// "gui::ImGuiOverlay" context: that name is a CATALOG key the shipped .ts files already carry,
// so it is frozen (see the note beside kTranslationContext in the .cpp). It describes the
// panel, not the backend that used to draw it.

} // namespace gui

#endif // GUI_GRAPHPANELFRAME_H
