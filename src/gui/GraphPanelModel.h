#ifndef GUI_GRAPHPANELMODEL_H
#define GUI_GRAPHPANELMODEL_H

#include <QColor>
#include <QFont>
#include <QPointF>
#include <QRect>
#include <QString>

#include "gui/FrameRuler.h"
#include <QVector>
#include "ctrl/graph/graph_Channel.h"
#include "util/Range.h"

namespace core {
class ObjectNode;
}
namespace ctrl {
class GraphEditor;
}
namespace theme {
class Graph;
}

namespace gui {

// The animation graph panel as an engine-neutral MODEL: everything the panel's
// host has to know to draw it, and nothing about how it is drawn.
//
// The panel was once a QPainter renderer (ctrl::graph::Renderer) and then a Dear
// ImGui spike (src/gui/ImGuiSpike.cpp, buildDocumentScene); both are deleted.
// This header is the durable form that replaced them: a pure function from the
// editor, a viewport size and a palette to a flat, POD-ish description of the
// panel. The drawing layer - gui::drawPanel, in src/gui/GraphPanelFrame.{h,cpp},
// the panel's one frame - walks the vectors below and emits primitives; it owns
// every appearance decision the model deliberately does not make (line widths,
// halos, fonts, alpha, emphasis).
//
// WHY IT EXISTS
//   * the panel's content is testable headlessly: no QPainter, no GL context, no
//     widget - tests/graph_panel_model.cpp asserts the geometry below against
//     the editor's own scaler;
//   * the drawing layer and the editor's hit-test read one geometry: the model
//     publishes what gui::drawPanel draws from the same functions the editor
//     picks with (ctrl/graph/graph_Geometry.h), so the drawn item and the
//     clickable item are one;
//   * the document is read, never written. See READ-ONLY below.
//
// WHAT IT IS NOT
//   * no background and no grid lines. The ONE piece of chrome the model describes
//     is the frame ruler's band (see THE OCCLUDED TOP and THE AXES below): a strip of
//     the timeline's own surface with a hairline under it, whose colours stay the
//     drawing layer's;
//   * no styled text: labels are RECTS. The halo/outline pass, the font, the
//     colour and the alignment are the drawing layer's;
//   * no interaction state. Hit-testing, the drag, the selection and the sticky
//     focus all live in the editor and in the drawing layer. Each PanelCurve
//     carries its (node, channel) identity so the drawing layer can emphasise
//     exactly one curve - the model never learns which one that is;
//   * no project mutation: the editor, its scaler, its selection and the
//     document come out of buildGraphPanelModel() exactly as they went in.

// The panel's empty-state strings, translatable.
//
// The model is not a QObject, so a plain tr() here would be resolved through a
// context this class does not own: lupdate would file the string under the class
// while Qt asked for it somewhere else, and the message would ship English in
// every language with nothing failing. The repository's precedent for a string
// that lives outside a QObject is an EXPLICIT context
// (src/ctrl/ExportTargetStrings.h): the literal carries
// QT_TRANSLATE_NOOP("GraphPanel", ...) so the extractor sees it, and this asks
// the SAME context at runtime. Both halves of that pair are written in one
// expression at each call site, so they cannot drift - which is what
// scripts/i18n/check_tr_contexts.py exists to police.
QString graphPanelTr(const char* aSourceText);

// One row of the CHANNEL LIST: a channel of one target, as the dock header's one
// control (the "N channels" button) lists it. A row exists for every (target,
// channel) that HAS KEYS, including a channel hidden through
// GraphEditor::setChannelVisible - a hidden channel keeps its row (visible ==
// false) and contributes no curve, so the user can see what is hidden and turn
// it back on.
//
// NO GEOMETRY IS PUBLISHED FOR A ROW, and that is the design: the list is the
// application's own QMenu (gui::MainDisplayWidget::showGraphChannelMenu), a
// native window this drawing layer neither draws nor measures - the same rule
// the key menu already follows. What a consumer gets here is the row's DATA: the
// layer it belongs to, its legend group (layer and key type), its name, its hue
// and its two states. A row is NEVER dropped for want of room: the menu scrolls,
// so every keyed channel is listed whatever the panel's size.
struct PanelChannelRow {
    // The target the row acts on. NON-CONST on purpose: the two actions a row
    // performs go through the editor, and both of those take a mutable node
    // (GraphEditor::setChannelVisible and setFocusedChannel). The targets the builder
    // walks carry non-const pointers (GraphEditor::targets() is a const method whose
    // ELEMENTS are ObjectNode*), so filling this needs no cast - only the sticky focus
    // it is compared against is const (focusedNode()), and that is a comparison.
    core::ObjectNode* node = nullptr;
    ctrl::graph::Channel channel;
    QString layer;      // the target's own name: a LAYER section heading in the list
    QString name;       // the channel's label for a list row or a chip ("Move X"), or
                        // the display type name for a one-component type ("Opacity")
    // THE LEGEND GROUP this row belongs to (design section 7): one group per
    // (layer, key type), naming the PROPERTY and its components - "Move [X][Y]";
    // a one-component type carries just its display name ("Opacity"), which the
    // list omits as a heading because the row repeats it. The group carries the
    // structure (which property); the row's
    // `color` carries only the axis, so two red X curves are told apart by their
    // group, not by a hue that cannot encode it. Built once per (layer,type) from
    // the same Channel table that names the components, so the heading and the rows
    // inside it cannot disagree. Rows of one group are CONTIGUOUS in `channels`.
    QString group;
    QColor color;       // theme::Graph::channelColor(channel.type(), channel.component()):
                        // the one hue table, theme::Colors (axis where there is an
                        // axis, type otherwise), theme::Graph::curve() when unnamed
    bool visible = true;  // == GraphEditor::isChannelVisible
    bool focused = false; // == the editor's sticky focus (focusedNode()/focusedChannel())
};

// One tangent handle of one segment, ready to stroke and to dot. A pair is
// published per segment that HAS handles: index 0 leaves the segment's `from`
// key, 1 arrives at its `to` key. Each handle is of one of TWO kinds, and they
// are NOT interchangeable (see PanelHandle::ghost) - and the kind is decided per
// SIDE, so one segment's two handles can differ:
//   * a STORED side (its mode Free/Aligned), what a drag writes; and
//   * a DERIVED side (Vector/Auto/AutoClamped), which the document does not hold
//     - it is recomputed from the key's neighbours, which is why it cannot be
//     stored - so the drawing layer can show that the side's tangent is implicit
//     and a drag would free it.
// Either way the drawing layer never has to know the easing model: only where to
// put a stem and a dot.
struct PanelHandle {
    int segment = -1; // index into Curve::segmentCount()
    int index = 0;    // 0 = leaving `from`, 1 = arriving at `to`
    QPointF base;     // where the stem starts (the key the handle belongs to)
    QPointF dot;      // where the user grabs (mapped through the scaler)
    int radius = 0;   // the dot's drawn radius
    // TRUE when THIS side is derived (Vector/Auto/AutoClamped) and false when it
    // is stored (Free/Aligned). The kind is PER SIDE: a Custom segment can derive
    // its outgoing handle and store the incoming one, so the segment's two
    // PanelHandles may differ. It is the whole of what distinguishes them: a
    // ghost handle is not in the document (dragging one frees that side), so the
    // drawing layer draws it as a SOLID dot of the channel's hue at a smaller
    // radius and a thinner-alpha ring, while a stored handle is the raised
    // surface - and a consumer that grabbed a handle knows which kind of write it
    // would start.
    bool ghost = false;
};

// One channel's curve, ready to stroke and to dot with keys.
struct PanelCurve {
    core::ObjectNode* node = nullptr;
    ctrl::graph::Channel channel;
    // The project's rule for the frames outside the keys (ctrl::GraphEditor::isLooping/frameRange):
    // the model rebuilds its own Curve from (node, channel), so the rule has to travel with them or
    // the drawn polyline would hold where playback wraps.
    bool loop = false;
    util::Range loopRange;
    QVector<QPointF> points; // (frame, value) mapped to panel pixels, in frame order
    QVector<QRect> keys;     // the key HIT squares, each clipped to the plot
    // The VISUAL of each key in `keys`: the centre of the marker its drawing
    // layer paints - a circle of radius ctrl::graph::kKeyDotRadius (see the
    // VISUAL/HIT split in ctrl/graph/graph_Geometry.h). Same order and same
    // length as `keys` and `keyFrames`, and published rather than recomputed
    // from the rect because the rect is CUT at the plot's edge: its centre is
    // not the key's centre for a key on the border, so a marker reconstructed
    // from the rect would sit off its key. A key that publishes no rect (one
    // entirely outside the plot) publishes no dot either.
    QVector<QPointF> keyDots;
    // The FRAME of each rect in `keys`, same order and same length: a rect is
    // where a key is HIT, which is all a hit-test needs, but an action needs to
    // NAME the key - and the triple (node, channel, frame) is the timeline's own
    // name for one. Parallel to `keys` (a) rather than a member of it because the
    // rects are the editor's shared geometry (ctrl::graph::keyRect) and a pair
    // would be a second copy of the same rule, and (b) because a key whose rect
    // comes out empty is DROPPED from `keys`: an index into `keys` is therefore
    // not an index into the curve's own key list, which is exactly the desync a
    // consumer must not be trusted to reconstruct for itself.
    QVector<core::Frame> keyFrames;
    // THE MARKER STATE of each of those keys, same order and same length as
    // `keys`/`keyDots`/`keyFrames`: whether THAT marker is selected, and whether the
    // key it belongs to has a selected SIBLING (another component of it, or the key as
    // a whole). They are the editor's own two queries, per key, published BESIDE the
    // geometry they describe so the drawn marker and the state it is drawn in come out
    // of one call: ctrl::GraphEditor::isMarkerSelected and
    // hasSelectedSiblingMarker - the SAME pair the editor's press reads to decide what
    // a selection is, and the same node/type/frame triple `keyFrames` names.
    //
    // A CLASSIFICATION, not ink: the model says WHICH of the three states a marker is
    // in, and the drawing layer decides what each state looks like (see the marker
    // rule in ctrl/GraphEditor.h's SELECTION note, and gui::drawPanel, which owns the
    // fill's brightening and both rings). The two flags are NOT mutually exclusive as
    // the engine answers them - a whole-key entry makes a key's every marker selected
    // AND every one of them see a sibling - so the states are:
    //   * SELECTED            : keySelected[i]                          (its own ring)
    //   * SIBLING IN PLAY     : !keySelected[i] && keySiblingSelected[i] (the faint ring)
    //   * NEITHER             : !keySelected[i] && !keySiblingSelected[i]
    // and a consumer that wants the second one to mean "the OTHER component is
    // selected" must AND the two flags exactly as written here. That composition is
    // the drawing layer's (it is the "which marker gets which ring" decision); what
    // the model owes it is the two facts the engine states, unmodified, so a consumer
    // can also ask the raw question ("is this key half in play?") without it being
    // pre-chewed.
    //
    // A key that publishes no dot (its rect lies entirely outside the plot) publishes
    // no entry here either, exactly as it publishes no keyFrame: the three vectors are
    // appended in one step.
    QVector<bool> keySelected;
    QVector<bool> keySiblingSelected;
    // WHETHER EACH KEY IS PROPORTIONAL, same order and same length as
    // `keys`/`keyDots`/`keyFrames`/`keySelected`: core::TimeKey::isProportional() of
    // the key `keyFrames[i]` names. It is the key's own classification, published as
    // the engine states it - the model does not decide what a proportional key looks
    // like. The drawing layer does: both views draw it as a RING (the timeline's
    // ctrl::time::kProportionalRingOuterInset / kProportionalHoleInset), leaving the radius,
    // the hue, the edge stroke and the selection ring untouched. Appended in the same step
    // as the geometry above, so a missing entry means "not proportional" exactly as a
    // missing marker state means "neither state".
    QVector<bool> keyProportional;
    QVector<PanelHandle> handles; // every segment's tangent handles, in segment order
    QColor color;            // == channels[channelRow].color; the drawing layer paints
                             // this curve's stroke, its key markers' FILL, its chip's
                             // text and its handle stems in it, so a channel is one
                             // colour everywhere
    int channelRow = -1;     // the row of `channels` this curve belongs to
    // The CHIP that names this curve IN PLACE: the box the name sits in, at the
    // plot's right edge, on the value the curve leaves the window with (see
    // CHIPS), and the text that goes in it. EMPTY when the chip would not fit
    // inside the plot - the drawing layer draws nothing for it and hit-tests
    // nothing derived from it.
    QRect chip;
    QString chipText;
};

// One piece of text and the box it goes in. The box is what the model decides;
// the text itself is anchored LEFT in it for frame numbers (the box is the
// timeline's reserved slot and TimeRuler draws with Qt's default left alignment)
// or right-aligned for value numbers, by the drawing layer.
struct PanelLabel {
    QRect rect;
    QString text;
    // The frame the text NAMES, or -1 for a label that names no frame (every value
    // axis number). The frame axis builds its strings with
    // core::TimeFormat::frameToString(frame, editor.timeFormatType()), so this is the
    // frame that call was given - the model states it rather than leaving a consumer
    // to recover it from the drawn pixels.
    int frame = -1;
    // The LAST frame of the merged boundary label ("first–last", drawn when the
    // project's own first and last frames are in view and their boxes collide); -1
    // for every ordinary number.
    int endFrame = -1;
    // TRUE when the frame this label NAMES lies inside the ACTIVE playback range -
    // the project's frame range, or the temporary play window while it is active
    // (ctrl::TimeLineEditor::playbackRange's rule). A CLASSIFICATION, never ink: the
    // timeline's own ruler recesses every number outside that range to an alpha of
    // 0.4 (TimeRuler::renderHeader), and the drawing layer applies that fixed factor
    // here. A label that names no frame (every value axis number) is always true:
    // the value axis is not a frame axis and has no active range.
    bool inActiveRange = true;
};

// THE TEMPORARY PLAYBACK WINDOW'S OVERLAY, exactly as TimeRuler::renderPlayWindow
// lays it out in the timeline's ruler. It is a SECTION OF THE RULER BAND, not plot
// content: the band rect is clipped to the visible band, the two 2px bracket stems
// run from the pill row to the band's last row, and the labels are the bracket's
// horizontal arms. The drawing layer paints the band first, then the stems, then the
// pills (a merged pill wider than the window must not hide the stems' lower half),
// with these tokens and order:
//   * band    -> theme::Colors::windowBand (translucent)
//   * stems   -> theme::Colors::windowEdge
//   * pills   -> theme::Colors::windowEdge, square on the face against each stem
//   * text    -> theme::Colors::windowText
// and the pills' rounding is gui::frameRuler::kPillRadius (the timeline's own).
//
// The model publishes GEOMETRY and TEXT only. `active` is the window's own
// classification; when it is false every rect is empty and the drawing layer draws
// nothing. The frames the window names are the timeline's window [start, end],
// mapped through the SAME scaler the frame ticks use, so a bracket sits on the tick
// of its frame.
struct PanelPlayWindow {
    bool active = false;
    // The translucent wash INSIDE the visible ruler band: [tick(start), tick(end)+1)
    // clipped to the band's columns, kBandHeight tall from the band's top.
    QRect band;
    // The 2px bracket stems, from the label row (kPillTop) to the band's bottom row.
    // [start] is attached to the window's first frame, [end] to its last.
    QRect startStem;
    QRect endStem;
    // The label arms, attached to the inner face of their stem. EMPTY when the two
    // would collide: the window is then narrower than its two labels and `merged` is
    // the one centred "a–b" pill.
    QRect startPill;
    QRect endPill;
    QString startText;
    QString endText;
    // The single centred label used when the two arms collide (window start–end in
    // the ruler's own formatter). Empty otherwise.
    QRect mergedPill;
    QString mergedText;
};

// THE FRAME RULER'S METRICS, ported from the timeline's ruler so the graph's band is
// its band and not a second convention:
//
//   * kBandHeight == ctrl::TimeLineEditor::kHeaderHeight (22px);
//   * kNumberTop / kNumberHeight == the timeline's own number row (1 / 14), measured
//     from the band's top;
//   * kPillPadding / kPillRadius == the timeline's playhead pill (5 / 3);
//   * kTickLength / kNumberCharWidth / kNumberMargin are TimeRuler.cpp's local
//     constants: every tier's tick is 5px long, a frame number RESERVES 6px per
//     character, and two numbers never come closer than 8px.
//
// The timeline's editor publishes the first five as public constants and
// GraphPanelModel.cpp static_asserts these against them, so the two cannot drift. The
// last three are the ruler's own file-local numbers there (TimeRuler.cpp:19, :108,
// :164); they are restated here because the model and the drawing layer both need
// them.
// THE RULER'S METRICS AND RULES live in gui/FrameRuler.h: one module for the strip above
// the timeline's lanes and this panel's band, so the two cannot disagree about a tick, a
// number or the temporary window's pills. gui::frameRuler is that module's namespace
// (included at the top of this header - it declares a namespace and must not be pulled
// in inside gui's).


// WHICH PLACEMENT THE MODEL PUBLISHES - the dock's Graph tab's, or the viewport
// overlay's. The panel is ONE component (design section 5: "one dock, two tabs",
// the Graph tab being the docked panel), and this flag names the floor the model was
// built for.
//
// WHAT IT NO LONGER DECIDES. The panel's ONE control - the button that opens the
// channel list - used to be placed by it (in the plot's corner for the dock, in the
// panel's own top-right corner for the overlay); the design moved that control into
// the DOCK HEADER (src/gui/TimeLineHeader.cpp, a Qt control the panel neither draws
// nor publishes), so nothing in this model is placed by the flag any more. It stays
// because it is part of the build the host asks for and is republished as the
// classification it is (see GraphPanelWidget, the dock's Graph tab, which asks for
// Dock; Overlay is the builder's default, i.e. "no host named").
//
// WHERE EACH ONE IS REACHED FROM, now that the application has ONE host. The
// docked page (gui::GraphPanelWidget, the dock's Graph tab) is the only host the
// application draws the panel in, and it asks for Dock
// (src/gui/GraphPanelWidget.cpp). Overlay is what every direct consumer of this
// model gets (the headless pixel gate, tests/graph_panel_pixels.cpp, builds its
// models that way).
enum class GraphPanelHost {
    Overlay,
    Dock,
};

// The panel's complete description for one frame.
//
// GEOMETRY
//   panel   == ctrl::GraphEditor::panelRect(aViewport). The panel covers the
//              whole viewport (it is the fullscreen graph view over the
//              animation), and this model never hardcodes a size: whatever that
//              function returns is the panel.
//   plot    == the editor's own scaler, given `panel`, i.e.
//              ctrl::graph::Scaler::plotRect() = panel minus kMargin on the left
//              and top, minus the MEASURED label column on the left
//              (Scaler::axisLabelWidth(); its floor is kAxisLabelWidth), minus
//              the RULER BAND's 22 rows at the top (Scaler::kRulerBandHeight)
//              and kScrollBarReserve at the right and bottom for the two bars'
//              lanes: the graph's own surface starts BELOW the ruler, exactly
//              as the timeline's surface starts below its TimeRuler strip. Empty
//              on a viewport too small for those insets; see emptyState.
//   valueColumn == the left column the value numbers sit in, the deleted painter
//              renderer's layout, kept as the panel's convention:
//              QRect(panel.left() + kMargin, plot.top(), scale.axisLabelWidth(),
//              plot.height()). Its width is the scaler's STABLE reserve for the
//              value format - the widest label that format can write, measured
//              once per font, NOT the widest label this window shows (see THE
//              LABEL COLUMN in ctrl/graph/graph_Scaler.h: the column insets the
//              plot, so a window-dependent width would move the plot's left edge
//              - and every frame coordinate - whenever a gesture changed the
//              numbers). It is the same width the plot was inset by, so the
//              column and the plot are one geometry. It never overlaps the plot.
//   hTrack/hThumb, vTrack/vThumb == the panel's two SCROLL BARS, outside the plot
//              and inside the panel: the frame axis' along the plot's bottom edge,
//              the value axis' along its right edge (see the struct's SCROLL BARS
//              note). The lanes are the application's scroll bars on the timeline's
//              own edges: the panel's last 12 rows (horizontal, its whole width) and
//              its last 12 columns (vertical, from the plot's first row) - flush with
//              the panel's bottom and right edges, no gap, no trough. The plot's
//              right/bottom inset IS that lane (Scaler::kScrollBarReserve == 12), so
//              the tracks touch neither the plot, the value column nor the ruler band,
//              and the plot's own top-left corner is where it always was.
//
// THE OCCLUDED TOP (rulerBandTop)
//   A widget may float ABOVE the viewport and cover the panel's first rows - the
//   project tab bar does, in the application's own layout - and the panel draws no
//   background, so anything under it is simply lost. buildGraphPanelModel therefore
//   takes the occluded height (aTopInset, logical pixels, 0 when nothing covers the
//   panel) and publishes rulerBandTop: the y where the panel's TOP CONTENT (the
//   ruler band) begins.
//
//   WHERE THE BAND IS, AND WHAT AN OCCLUDER MAY DO. The band is a SECTION at the
//   panel's OWN TOP ROW - the timeline's strip starts at its page's top edge, and
//   both docks are pages of the same stack, so the graph's rulers sit on the same
//   rows (measured: the timeline's strip top is page.top()+9, the graph's band must
//   be page.top()+9 too). The scaler's 22 reserved rows plus its 4px top margin put
//   plot.top() at panel.top()+26, so the band's slot is
//   plot.top() - kMargin - frameRuler::kBandHeight == panel.top(); the 4 margin rows
//   between the band's hairline (band.bottom()+1) and the plot stay the panel's
//   margin. The band may not begin above the occluded line, and it may NOT be pushed
//   down into the plot - those rows belong to the graph clip, and moving the ruler
//   into them is exactly the ink-over-the-ruler defect this section removes. So the
//   band is published only while its whole slot is at or below the occluder
//   (inset <= bandTop); an occluder that reaches into the slot hides the strip for
//   that frame, and rulerBandTop then names plot.top() - the y where the panel's top
//   content may begin when there is no ruler ink above the graph.
//
//   THE PLOT NEVER MOVES. It is ctrl::GraphEditor's own rect (the editor's
//   hit-test geometry, and the rect the editor's scaler is pinned to), so `plot`,
//   the curves, the keys, the handles, the playhead and the marquee stay exactly
//   where the editor acts on them. Moving them here would put the drawn key a tab
//   bar's height away from the key a press grabs.
//
// THE AXES (no grid, no rules)
//   frameTickX: the x of every frame the ladder classifies (time::Scaler's
//               TickLevel != None) inside the RULER BAND - the panel's columns, which
//               is where the shared mapping puts the timeline's frames, ascending -
//               plus the project's
//               forced BOUNDARY ticks (see LABELS below - the timeline's own rule that
//               the first and last frames own their slots). The drawing layer draws a
//               SHORT mark UP from the band's last row at each - frameRuler::kTickLength
//               (5px, the timeline's kTickLength) for every tier - and nothing else.
//   frameTickLevel: the tier of each of those ticks, one entry per frameTickX
//               entry in the same order, so index i is tick i's tier. It is the
//               ladder's own classification (ctrl::time::Scaler::TickLevel, carried
//               as its integer value; None never appears, because such a frame is
//               not a tick and frameTickX has no entry for it) and NOT a
//               measurement: the tier is what the ladder that decided the tick
//               said about it, and recovering it in the drawing layer from tick
//               spacing or label widths is the pixel measurement the timeline's own
//               ruler comment rejects. It is a CLASSIFICATION and not appearance -
//               the model names no tier's alpha or length, exactly as it publishes
//               a position and not a span, and the drawing layer decides how a tier
//               looks (the timeline's own table, 0.85 major / 0.60 intermediate /
//               0.20 minor, one length for every level). The two vectors are
//               appended in one step, so they are the same length by construction.
//               A forced boundary tick carries TickLevel::Major, the prominence the
//               timeline gives it.
//   valueTickY: the y of the value axis' own ticks, one per window value (max,
//               midpoint, min), in that order. A SHORT mark rightwards from
//               plot.left() at each. A value whose NUMBER the column could not
//               fit still gets its tick - the axis has a tick there whatever can
//               be written beside it, the frame axis' own rule - so a tick can
//               outnumber the valueLabels below (only past the column's ceiling,
//               unreachable at the UI font; see graph_Scaler.h).
//   The model publishes POSITIONS (ints) and CLASSIFICATIONS (ints), never
//   spans and never ink: a full-height or full-width line cannot be expressed in
//   this structure at all, which is the mechanical form of "no grid", and a
//   tick's tier is an index into the drawing layer's own appearance table rather
//   than a colour or a length.
//
// LABELS (rects only)
//   frameLabels: the frame NUMBERS of the ruler, written by ONE function as
//               core::TimeFormat::frameToString(frame, editor.timeFormatType()) - the
//               timeline editor's own formatter and format setting, so both rulers
//               name a frame identically. A number is drawn for EVERY ladder tick
//               whose Attribute::showNumber is set (Major always, the intermediate
//               tier once it has 64px of room), not only for majors. Every box
//               RESERVES frameRuler::kNumberCharWidth (6px) per character, centred on
//               its tick and nudged inside the BAND, and the number's own frame
//               travels beside the text in PanelLabel::frame. That box is
//               TimeRuler's own slot: its left edge is `tickX - (reserve >> 1)` and
//               the drawing layer anchors the run at that edge with Qt's default left
//               alignment - the box is NOT re-centred on the glyph's measured advance
//               (that subtraction is the 1px left shift of every digit count). The
//               project's FIRST and
//               LAST frames are forced in when they are visible (the timeline's own
//               "the range's first/last frame owns the end of the ruler"), and a
//               narrow window collapses them into one "first–last" label
//               (PanelLabel::endFrame). A label is dropped when it would come closer
//               than frameRuler::kNumberMargin (8px) to a boundary label's reserved
//               span, to the previous label's right edge, or when its centre falls
//               inside the playhead pill's half-width - the pill owns its column.
//               THE BOX'S TOP EDGE IS THE ROOM A NUMBER HAS: it sits at
//               rulerBandTop + frameRuler::kNumberTop, inside the band's number row,
//               and a drawing layer must never put a glyph ABOVE its box's top edge.
//               The glyph LINE BOX starts ON that edge, exactly as TimeRuler's
//               top-aligned drawText starts its own on the ruler's number row: a
//               vertical CENTRING of the box put the panel's numbers two rows below
//               the timeline's on one capture (same glyph, same height, shifted), and
//               the drawing layer's LabelVAlign::Top is the parity rule. A font
//               taller than the box then hangs BELOW the box, into the band's
//               remaining rows - the safe direction, since the top edge is the one
//               that borders the rows above the number row. That pair is what keeps
//               the top row of numbers off the plot's top edge and out of the plot
//               clip; the model measures the box with the ruler's fixed reserve and
//               cannot know the drawing layer's font, so the obligation is split
//               rather than assumed.
//   valueLabels: the window's max / midpoint / min, formatted 'g' with four
//               significant digits (the deleted painter renderer's valueText(),
//               now ctrl::graph::Scaler::valueAxisText - the SAME function the
//               column's width is measured from; two equal strings are
//               deduplicated). The box is the value column,
//               ctrl::graph::Scaler::kAxisLabelPad (4px) narrower on the right,
//               vertically centred on the value's own pixel and clamped into the
//               column - the drawing layer right-aligns the text in it.
//               THE COLUMN IS SIZED FOR THE WHOLE FORMAT, not for this window's
//               three strings: its width is the scaler's stable reserve (the
//               widest label 'g'/4 can write, plus the box's inset and a small
//               gap - Scaler::axisLabelWidth()), so every published box is at
//               least as wide as the text it carries, no number can spill past
//               the panel's own left margin, and NO gesture can move the column
//               (or the plot's left edge) by changing which label is widest.
//               That last part is why the reserve is format-derived: a
//               window-measured column moved the plot by a pixel on a 40px pan
//               (measured). A number that still would not fit (only past the
//               column's ceiling, which the format's widest label cannot reach
//               at the UI font) is DROPPED, not published overflowing, exactly as
//               the frame axis drops a number wider than the plot; its
//               valueTickY entry is still published.
//   The boxes are measured with the font the CALLER hands in (buildGraphPanelModel's
//   aMeasuredFont; QFont() when omitted): the model has no painter, and the rects are
//   advisory geometry, not a text layout. The drawing host passes its own widget font,
//   which is also the timeline's (both inherit the dock's stylesheet font), so a box the
//   two views compare - the playhead pill - is measured with the same font in both. A
//   drawing layer with its own font puts the glyphs in the box it was handed. The column
//   is measured with that same font (it is the scaler that measures it), so the split
//   obligation is still "put the glyphs in the box", not "make them fit".
//
// CURVES
//   One PanelCurve per VISIBLE channel of every target that has keys - the same
//   set GraphEditor::buildEntries() draws, rebuilt here from the public editor
//   surface (targets(), isChannelVisible(), the node's timeline) because a Curve
//   only names its timeline, not its node, and the drawing layer needs the
//   (node, channel) identity.
//   points: Curve::buildPolyline over the plot's own frame window, sampled at
//   qBound(2, plot.width() / (2 * segmentCount), 256) samples per segment - one
//   point per ~2px of plot width, clamped - then mapped through
//   Scaler::pixelXOfFrame / Scaler::pixelY. A curve whose keys all sit outside
//   the window has its identity and colour but no geometry.
//   THE POLYLINE IS NOT CLIPPED TO THE PLOT, and the model does not clip it:
//   Curve::buildPolyline emits every touched segment's two endpoints even when
//   they lie outside the sampled window - deliberately, so a polyline cut to the
//   window's interior would not fall a step short of the plot's edge - and the
//   model maps those samples through the scaler unchanged. A published point may
//   therefore lie outside `plot`: in x when a segment's endpoint frame is outside
//   the window, and in y whenever the value window is narrower than the curve's
//   own values (the user's zoom). CLIPPING IS THE CONSUMER'S OBLIGATION: a
//   drawing layer MUST clip the polyline to `plot` before stroking it (the
//   shipping overlay pushes a clip rect to the plot). A consumer that cannot
//   clip MUST NOT consume `points`. The key RECTS, by contrast, ARE clipped:
//   ctrl::graph::keyRect(scale, frame, valueOf(key)) clipped to the plot,
//   empties dropped - one geometry with the editor's hit-test. Nothing in
//   `points` is dropped or cut, so a curve's own samples can be reproduced from
//   them exactly.
//   keys: the key HIT squares, from ctrl::graph::keyRect - the editor's own
//   hit-test rectangle (and the panel's right-click menu's), clipped to the
//   plot, empties dropped. They are HIT geometry: the drawn marker is the
//   circle `keyDots` names (the VISUAL/HIT split in graph_Geometry.h), so a
//   consumer must not size its ink from these rects.
//   keyDots: the marker centre of each of those keys, from
//   ctrl::graph::keyDot - a circle of radius ctrl::graph::kKeyDotRadius (3.75px,
//   the timeline's own key-body radius ctrl::keyRadius) is drawn about it. Same
//   order and length as `keys` and `keyFrames`. Published because the hit rect
//   is cut at the plot's edge while the marker stays on the key: the rect's own
//   centre is the wrong centre for a key on the border.
//   keyFrames: the frame of each of those keys, same order, same length - the
//   half of a key's identity a point or a rect cannot carry, so a consumer that
//   HITS a key can name the key it hit (node, channel and frame are the
//   timeline's own triple). Published beside `keys` rather than reconstructed by
//   the consumer: an empty rect is dropped, so an index into `keys` is not an
//   index into the curve's key list.
//   keySelected/keySiblingSelected: the MARKER STATE of those same keys, one
//   bool each, same order and same length - the editor's own two queries
//   (isMarkerSelected, hasSelectedSiblingMarker) asked per key as the curve is
//   built. They are the classification the three marker states are drawn from
//   (see the struct's comment for the three and for why the sibling flag is
//   published raw). The model names the state and the drawing layer names the
//   ink: no colour, alpha or ring is published here.
//   handles: one PanelHandle pair per segment of the curve HAVE handles, in
//   segment order (segment 0's two handles, then segment 1's, ...). base is the
//   key the handle belongs to, mapped through the scaler (handle 0's base is the
//   segment's `from` key, handle 1's the `to` key - each control's stem leaves
//   the key it moves).
//   a Custom segment publishes a handle per side, each of ITS OWN KIND: a STORED
//   side (ghost == false) gets dot = ctrl::graph::handlePoint(scale, segment,
//   index) - the SAME mapping the editor's hit-test grabs through, so a grab at
//   the dot is the drag the editor performs - and a DERIVED side (ghost == true,
//   Curve::derivesHandle) gets dot = ctrl::graph::handlePointAt over the side's
//   effective control point (the third and two thirds of the span in x; the two
//   y values come from the key's neighbours), so a stored pair and a derived one
//   cannot drift apart. The document holds no derived pair - it is recomputed -
//   which is exactly what `ghost` tells a consumer: a solid dot of the channel's
//   hue (smaller and lighter than the raised, stored dot) is how the layer shows
//   a tangent the key does not store. The kind is PER SIDE, never per segment: a
//   segment with one derived side still publishes its stored side as stored.
//   Every other easing type (the analytic curves) publishes nothing for that
//   segment, so a curve with no Custom segment has an empty `handles`.
//   radius is ctrl::graph::kHandleRadius (the dot's drawn radius); the grab
//   radius is the larger ctrl::graph::kHandleHitRadius, and the two are
//   deliberately different (the same visual/hit split as a key's).
//   A CURVE CARRIES ITS PAIRS ONLY WHILE THE DRAWING LAYER WOULD INK THEM. The
//   layer draws tangent handles for the FOCUSED channel alone - and for every
//   channel while nothing is focused, which is the same rule it applies to a curve
//   crossing another - so with a focus set, only that row's curve publishes
//   `handles` and every other curve's vector is EMPTY. The predicate is the model's
//   own rows (PanelChannelRow::focused, taken from the same editor state the layer's
//   focus comes from) for a reason: an Auto segment's pair is DERIVED from the key's
//   neighbours, and deriving it for a row nobody draws is the expensive half of this
//   builder. A consumer that needs a handle's geometry for a curve outside that rule
//   reads it from ctrl::GraphEditor (its hit-test geometry) rather than from here.
//   A pair is published whether or not it lies inside the plot: the drawing
//   layer clips (the overlay's plot clip covers the handles like everything
//   else), and a handle the user can grab off the plot's edge must still be
//   published for the highlight to name it.
//   color: theme::Graph::channelColor(channel.type(), channel.component()) - the
//   one channel hue table (axis where there is an axis, type otherwise), which
//   falls back to theme::Graph::curve() for a type the palette does not name.
//   chip/chipText: the curve's own NAME, IN PLACE.
//   chip is the box the name sits in: it is anchored at the plot's RIGHT edge
//   (kChipRightGap = 8px inside it) and vertically centred on the value the
//   curve's polyline leaves the window with - the last of `points`, so the chip
//   is derived from the curve it names rather than from a second evaluation of
//   the curve at the edge. The box is the text's own advance plus
//   kChipPadX (6px) on each side, the reference's own backing padding. It is a
//   chip at the CURVE, not a legend in a corner,
//   so the name of a channel cannot be read off the wrong row; that is the whole
//   difference from the deleted legend.
//   chipText is "Move X", or the bare type name for a one-component type
//   ("Opacity", not "Opacity Opacity"): a channel whose type has one component has no
//   sibling to be told apart from, so the component word is noise.
//   chip is EMPTY when the box would not fit inside the plot - a curve leaving
//   the window too near the top or the bottom edge, or a plot too narrow for the
//   name - and the drawing layer MUST draw nothing for it and MUST NOT
//   hit-test any geometry derived from it (QRect().right() is -1 and its centre
//   is (0,0), so a box derived from an empty rect lands in the panel's corner).
//   WHICH chips are drawn is the drawing layer's: the model publishes one chip
//   per curve and the layer draws the ones IN PLAY - the channels that own a
//   selected key and whatever the pointer is over - and pushes the drawn ones
//   apart so two names cannot overlap. That subset is EMPHASIS, which is the
//   drawing layer's decision; the model publishes the candidate geometry only,
//   exactly as it publishes every curve and lets the layer dim the ones that own
//   no selected key.
//
// CHANNEL LIST
//   channels: one row per (target, channel) with keys, in target then channel
//   order - the same order as the curves, and the same set the editor builds
//   entries for. A hidden channel keeps its row with visible == false: that is
//   how the user sees what is hidden. The rows are GROUPED FOR THE LEGEND: one
//   group per (layer, key type) - PanelChannelRow::group, e.g. "Move [X][Y]" -
//   with one row per component inside it, and the rows of a group are
//   contiguous. The group names the property; the row's colour carries only the
//   axis (design section 7). The list is the application's own QMenu, so NO row
//   geometry is published (see PanelChannelRow): the menu is a native window the
//   drawing layer neither draws nor measures, exactly like the key menu, and it
//   can scroll, so no row is ever dropped for want of room.
//
// SELECTION COUNT
//   selectionCount is ctrl::GraphEditor::selectionCount(), the number of keys the
//   live selection names, 0 with nothing selected. It is published for the
//   ACCESSIBILITY tree, not for ink: the panel draws no count anywhere (see
//   gui::GraphPanelFrame's buildAccessibilityModel, which appends ", %n key(s)
//   selected" to the panel's summary so a screen reader still reads what the drawn
//   label used to say).
//
// PLAYHEAD
//   playheadX == Scaler::pixelX(aEditor.currentFrame()) whenever a project is
//   attached, or -1 when there is none (GraphEditor::hasProject). NEGATIVE
//   FRAMES ARE REAL: a project may start before 0 and the seek reaches them, so
//   the frame's sign never decides whether a playhead is placed - only whether
//   it is on screen (a pixel LEFT of the panel is negative and the drawing layer
//   skips it). It is read even when the plot is empty, because a playhead is a
//   property of the document and not of the geometry.
//   playheadText is that frame written with the ruler's own formatter; playheadPill
//   is the 14px rounded pill the timeline's TimeCursor draws about the column (null
//   when there is no frame or no band), and playheadLineTop is the row the playhead's
//   1px line starts on - under the pill, as TimeCursor's does. The line crosses the
//   whole PANEL (the timeline's playhead crosses its whole body), so it runs from that
//   row to panel.bottom() and through the chart's chrome; the two bar lanes are
//   painted after it. The pill's own number
//   suppresses every ruler label whose centre falls inside its half-width, so the
//   column carries one number.
//
// ACTIVE RANGE (recession)
//   The ruler recesses every tick and every frame number whose frame lies OUTSIDE the
//   ACTIVE playback range, exactly as the timeline's ruler does: the ACTIVE range is
//   the project's frame range, or the temporary play window while it is active
//   (ctrl::TimeLineEditor::playbackRange's rule, published by
//   ctrl::GraphEditor::activeRange()). The model publishes the classification, never
//   the ink: frameTickInActiveRange carries one flag per tick, PanelLabel::inActiveRange
//   one per frame label (a merged boundary label is in range only when BOTH of its
//   frames are), and the drawing layer applies the timeline's fixed 0.4 factor to the
//   tick's tier alpha / the number's alpha. This is what makes the active range read
//   at a glance in both views and cannot be recovered from the tick ladder.
//
// PLAYBACK WINDOW
//   playWindow publishes the temporary playback window's overlay geometry inside the
//   ruler band - the translucent band rect, the two 2px bracket stems, the two label
//   pills and their texts, or one centred merged pill when the arms would collide -
//   laid out exactly as TimeRuler::renderPlayWindow lays it out (see PanelPlayWindow).
//   It is geometry and text only; the drawing layer paints the band, the stems and the
//   pills in that order with theme::Colors::windowBand/windowEdge/windowText.
//
// MARQUEE
//   marquee == the editor's own box-select rectangle (GraphEditor::marqueeRect()),
//   or an EMPTY rect while no marquee drag is in flight - which is the model's
//   whole signal, since the editor publishes an empty rect whenever no drag is
//   live (a press and release with no movement included). It is the gesture's own
//   geometry, in the same viewport pixels as `plot`, and it is NOT clipped here:
//   the editor already clips it to the plot, and the drawing layer's plot clip
//   covers it whichever way the rectangle runs. The model draws nothing for it (it
//   publishes no ink of its own anywhere); what it adds is the identity of the
//   rectangle, so the gesture the editor is performing and the rectangle on screen
//   are one geometry.
//
// zeroY
//   The y of value 0 while the value window contains it, else -1. Published so
//   the drawing layer can place a zero rule without re-deriving the axis; the
//   model draws nothing for it (the panel has no grid).
//
// EMPTY STATE
//   emptyState is non-empty exactly when there is nothing to draw, and then NO
//   curve is produced (isEmpty() is the pair of those facts):
//     * "Select a layer to show its curves"  - no target selected;
//     * "No keys on the selected layers"     - targets selected, no channel has
//                                              a key;
//     * "All channels of the selected layers are hidden";
//     * "No room for the graph in this viewport" - the plot came out empty.
//   The strings are translatable: they are marked with
//   QT_TRANSLATE_NOOP("GraphPanel", ...) and resolved by graphPanelTr() through
//   that same context (see it above).
//   With targets but every shown channel hidden, the channel list still carries
//   the hidden rows and emptyState names the state: an empty panel must say why
//   it is empty.
//
// READ-ONLY
//   buildGraphPanelModel() takes the editor by non-const reference only because
//   GraphEditor's own accessors are split that way (curves(), targets() and
//   scale() are const-callable, isChannelVisible() is not). It mutates nothing:
//   it never writes the document, the editor, its scaler, its selection or its
//   hidden-channel set, and the scaler it maps through is a local copy.
struct GraphPanelModel {
    QRect panel;
    QRect plot;
    QRect valueColumn;
    // THE PANEL'S TWO SCROLL BARS, as four rects: the horizontal bar's track and
    // thumb, and the vertical bar's. They are the ONLY ink outside the plot that is
    // not the ruler's, and they are the panel's way of moving a WINDOW without
    // changing its scale (the wheel zooms, they scroll).
    //
    //   * hTrack/hThumb - along the plot's bottom edge: the FRAME axis' window. The
    //     content is the project's frame range (ctrl::GraphEditor::frameRange()) and
    //     the window is the visible frame window, so the thumb is where that window
    //     sits inside the range and how much of it is visible. While a
    //     timeline owns the frame axis (ctrl::GraphEditor::hasTimelineFrameAxis) this
    //     IS the timeline's window - one window, two views - and its span follows
    //     from the panel-wide mapping (Scaler::applyFrameWindow).
    //   * vTrack/vThumb - along the plot's right edge: the VALUE axis' window. The
    //     content is the value extent of every SHOWN channel's keys (exactly the
    //     range ctrl::GraphEditor::fitValueAxis fits, both read through the editor's
    //     one walk), and the window is the scaler's valueMin()..valueMax(), so the
    //     value axis is inverted along the track: a window at high values draws its
    //     thumb at the TOP.
    // The tracks are the APPLICATION's scroll bar lanes, on the TIMELINE'S edges: the
    // horizontal one is the panel's last 12 rows across its whole width, the vertical
    // one the panel's last 12 columns from the plot's first row down to the row above
    // the horizontal lane (ctrl::graph::Scaler::kScrollBarThickness ==
    // kScrollBarReserve == 12, no gap) - flush with the panel's bottom and right edges,
    // and the scaler takes exactly that lane out of the plot's right and bottom edges,
    // so these rects never overlap the plot, the value column or the ruler band.
    // hThumb/vThumb span the lane and name the SLIDER'S RUN: the drawing layer paints
    // the app's 4px, radius-2 pill centred in the lane across that run, and the trough
    // paints NOTHING (AppStyle.cpp's CC_ScrollBar). They are DISJOINT from each other
    // (the corner is the horizontal bar's).
    //
    // Run LENGTH and OFFSET are Qt's own QScrollBar arithmetic (length =
    // pageStep/content of the track, floored at QStyle::PM_ScrollBarSliderMin, offset =
    // the window's fraction of the travel over track-length), so a window that covers
    // the content (or a content with no extent at all - no key, or no frame span)
    // fills its track, and the run IS the track: the drawing layer paints a full-length
    // pill in BOTH lanes then (the vertical bar does not hide), while the travel flags
    // keep either full-length lane out of the press and wheel routes. The four
    // rects are EMPTY while the plot is empty: there is nowhere to lay a bar out, and
    // the drawing layer draws nothing for an empty rect.
    //
    // THE GEOMETRY IS ctrl::graph::Scaler::scrollBars()'s, computed here from this
    // model's own scaler - the SAME function ctrl::GraphEditor::updateCursor runs its
    // press routing through, so the bar a press grabs is the bar this model names.
    // No interaction state is published (hovered, dragged): that is the drawing
    // layer's, exactly as a key marker's hover is, and it reads the pointer for it.
    QRect hTrack;
    QRect hThumb;
    QRect vTrack;
    QRect vThumb;
    // Where the panel's top content begins: the ruler band's top, at the panel's own
    // top row (the timeline's strip starts at the page top too), i.e.
    // plot.top() - kMargin - frameRuler::kBandHeight. When a widget floating above
    // the viewport reaches into that slot the band is hidden and this names
    // plot.top() instead (see THE OCCLUDED TOP above); an empty plot publishes
    // plot.bottom() + 1.
    int rulerBandTop = -1;
    // THE RULER BAND: the PANEL's own width by frameRuler::kBandHeight rows from
    // rulerBandTop - the timeline's ruler strip, at the panel's top edge, with the
    // scaler's kMargin rows between its hairline and the plot. It is as wide as the
    // panel (not the plot) because the shared frame mapping puts frame ticks on the
    // timeline's own columns, including the value column's top-left corner: the
    // timeline's strip spans its viewport, and so does this one. The drawing layer fills
    // it with theme::Graph::rulerBand(), rules the row under it with
    // theme::Graph::rulerLine() and draws the ticks up from its last row; the band's
    // row is OUTSIDE the plot clip, so no curve, key, handle or chip can paint into
    // it. EMPTY when the strip does not fit in its reserved slot (a fully occluded
    // top), which is also the model's "no ruler ink" signal.
    QRect rulerBand;
    QVector<int> frameTickX;
    // The tier of each tick in frameTickX: same length, same order, so index i is
    // tick i's tier (the ladder's own ctrl::time::Scaler::TickLevel as an int; None
    // never appears). A CLASSIFICATION, not an appearance - the drawing layer maps a
    // tier to its own alpha (see THE AXES above).
    QVector<int> frameTickLevel;
    // TRUE when tick i's frame lies inside the ACTIVE playback range (the project's
    // frame range, or the play window while it is active). Same length and order as
    // frameTickX/frameTickLevel, appended in the same step. It is the second half of
    // the timeline's recession rule: the tier says HOW prominent a tick is, this says
    // whether it also recedes (the timeline multiplies an out-of-range tick's tier
    // alpha by 0.4 - TimeRuler::renderHeader). A classification, never ink: the 0.4
    // factor lives in the drawing layer.
    QVector<bool> frameTickInActiveRange;
    // THE FRAME RANGE'S TWO GUIDES: the COLUMNS of the project's first and last frame -
    // the ruler's boundary ticks continued down the body, exactly as the timeline draws
    // them (ctrl::time::buildRangeMarkers, the pass behind its lanes). Two entries, or
    // ONE when both ends round onto the same pixel (a one-frame range, or a zoom-out
    // coarse enough to collapse the span - the timeline's own degenerate-range rule,
    // which draws one line rather than two overdrawn on one column), and EMPTY when no
    // project is attached: there is no range to mark.
    //
    // THEY ARE PANEL COLUMNS, not plot columns, and that is the shared mapping's
    // consequence: a frame's x is the TIMELINE's x (graph_Scaler.h: THE SHARED
    // WINDOW'S MAPPING), so the project's first frame sits wherever the timeline puts
    // it - which, at the usual fit, is inside the value column the plot is inset by.
    // The guides are structural chrome like the playhead, so they are drawn over the
    // panel's columns (the value labels, painted last, cover them) and rise/fall inside
    // the plot's ROWS only: the ruler band above and the scroll bar's lane below stay
    // clear. A plot with no room publishes none (the early return above).
    QVector<int> rangeGuideX;
    // TRUE when a temporary playback window overrides the project's range: the
    // timeline drops its guides from 40% to 25% then, because the window's own brackets
    // carry the boundary reading. A classification, never ink - the two alphas live in
    // the drawing layer.
    bool rangeGuideRecede = false;
    // THE PLAYBACK WINDOW'S OVERLAY, as the timeline's ruler lays it out (see
    // PanelPlayWindow): empty/inactive unless the editor carries an active window.
    // It is ruler-band geometry, so the drawing layer paints it inside the band and
    // before the playhead - never inside the plot.
    PanelPlayWindow playWindow;
    QVector<int> valueTickY;
    QVector<PanelLabel> frameLabels;
    QVector<PanelLabel> valueLabels;
    int zeroY = -1;
    int playheadX = -1;
    // THE PLAYHEAD'S PILL: the frame number, written with the SAME formatter the ruler
    // numbers use (core::TimeFormat::frameToString(currentFrame, timeFormatType())) -
    // published as the string so the drawing layer writes the very characters the
    // suppression geometry was measured from. Empty when no project is attached (a
    // negative frame is a real frame and gets a number like any other).
    QString playheadText;
    // The 14px, 3px-radius pill gui::TimeCursor draws about the playhead's column, at
    // the band's number row (frameRuler::kPillTop). EMPTY when there is no frame or no
    // band to draw it in. The model suppresses every ruler number whose centre falls
    // inside half this pill's width, so the pill's own number is the only one read in
    // its column.
    QRect playheadPill;
    // The first row of the playhead's LINE: the pill covers the rows above it, exactly
    // as TimeCursor's line starts under its pill. -1 when there is no playhead.
    int playheadLineTop = -1;
    // The live box-select rectangle, or an empty rect while no marquee drag is in
    // flight (see the MARQUEE note above). The drawing layer paints it inside the
    // plot clip; nothing is drawn for an empty rect.
    QRect marquee;
    // The number of keys the live selection names (ctrl::GraphEditor::selectionCount()),
    // 0 with nothing selected. Published for the ACCESSIBILITY tree and not for ink
    // (see SELECTION COUNT).
    int selectionCount = 0;
    QVector<PanelCurve> curves;
    // The project's own loop rule, as the editor reported it, for every curve in this model.
    bool projectLoop = false;
    util::Range projectLoopRange;
    QVector<PanelChannelRow> channels;
    // The floor this model was built for, republished so a consumer can read the
    // placement it actually got (see GraphPanelHost). Nothing in the model is placed
    // by it: the panel's ONE control - the channels button - lives in the DOCK
    // HEADER, not on the panel.
    GraphPanelHost host = GraphPanelHost::Overlay;
    QString emptyState;
    bool isEmpty() const { return curves.isEmpty() && !emptyState.isEmpty(); }
};

// Builds the model. aViewport is the whole viewport the panel is pinned over;
// aTheme is the palette the curves and swatches take their colours from; aTopInset is
// how many LOGICAL pixels of that viewport's top a widget floating above it covers (0
// when nothing does), which can HIDE the ruler band but moves nothing else - see THE
// OCCLUDED TOP above; and aHost is the floor the panel is on, republished in the model and
// used for no geometry - see GraphPanelHost.
//
// aMeasuredFont is the font the model MEASURES its strings with. The drawing host must
// pass its own QWidget::font(): that is the font the timeline's ruler measures with
// (TimeRuler/TimeCursor call QFontMetrics(aPainter.font())), and the APPLICATION stylesheet
// gives every widget of the dock one font size (MainWindow's locale stylesheet), so the
// panel's playhead pill comes out exactly as wide as the timeline's TimeCursor pill. A
// default-constructed QFont() is the APPLICATION default font, which the stylesheet does
// NOT apply to the widgets - the shipped locale sheet sets QWidget { font-size: 8pt }
// while QFont() keeps the platform's 9pt: measuring the pill with QFont() made it 2px
// wider than the timeline's on a 3-digit frame (and shifted its left edge 1px). The
// default is kept for headless callers that only read the font-independent geometry
// (channels), so an existing call site is never silently measured with a widget font.
GraphPanelModel buildGraphPanelModel(
    ctrl::GraphEditor& aEditor, const QSize& aViewport, const theme::Graph& aTheme, int aTopInset = 0,
    GraphPanelHost aHost = GraphPanelHost::Overlay, const QFont& aMeasuredFont = QFont()
);

} // namespace gui

#endif // GUI_GRAPHPANELMODEL_H
