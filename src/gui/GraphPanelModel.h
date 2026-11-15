#ifndef GUI_GRAPHPANELMODEL_H
#define GUI_GRAPHPANELMODEL_H

#include <QColor>
#include <QPointF>
#include <QRect>
#include <QString>
#include <QVector>
#include "ctrl/graph/graph_Channel.h"

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

// The animation graph panel as an engine-neutral MODEL: everything the overlay
// has to know to draw the panel, and nothing about how it is drawn.
//
// The panel was once a QPainter renderer (ctrl::graph::Renderer) and then a Dear
// ImGui spike (src/gui/ImGuiSpike.cpp, buildDocumentScene); both are deleted.
// This header is the durable form that replaced them: a pure function from the
// editor, a viewport size and a palette to a flat, POD-ish description of the
// panel. The drawing layer - gui::drawPanel, in the shipping ImGui overlay -
// walks the vectors below and emits primitives; it owns every appearance
// decision the model deliberately does not make (line widths, halos, fonts,
// alpha, emphasis).
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
//   * no background, no grid lines, no frame/value rules. The two axes are SHORT
//     tick marks only (see the axes below), so the viewport's animation stays
//     readable through the whole panel;
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

// One legend row: a channel of one target, its colour and where its clickable
// swatch is. A row exists for every (target, channel) that HAS KEYS, including a
// channel hidden through GraphEditor::setChannelVisible - a hidden channel keeps
// its row (visible == false) and contributes no curve, so the user can see what
// is hidden and click the swatch to bring it back.
struct PanelLegendRow {
    core::ObjectNode* node = nullptr;
    ctrl::graph::Channel channel;
    QString label;      // "<layer name> : <channel name>", the editor's own row label
    QColor color;       // theme::Graph::keyColor(channel.type()), theme::Graph::curve() when unnamed
    QRect swatch;       // the clickable colour chip, inside the plot; EMPTY when
                        // this row has no room to lay one out (see LEGEND)
    bool visible = true;
};

// One Custom segment's tangent handle, ready to stroke and to dot. A pair is
// published per Custom segment (index 0 leaves the segment's `from` key, 1
// arrives at its `to` key), so the drawing layer never has to know the easing
// model - only where to put a stem and a dot.
struct PanelHandle {
    int segment = -1; // index into Curve::segmentCount()
    int index = 0;    // 0 = leaving `from`, 1 = arriving at `to`
    QPointF base;     // where the stem starts (the key the handle belongs to)
    QPointF dot;      // where the user grabs (Curve::handle() mapped through the scaler)
    int radius = 0;   // the dot's drawn radius
};

// One channel's curve, ready to stroke and to dot with keys.
struct PanelCurve {
    core::ObjectNode* node = nullptr;
    ctrl::graph::Channel channel;
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
    QVector<PanelHandle> handles; // the Custom segments' tangent handles, in segment order
    QColor color;            // == legend[legendIndex].color; the drawing layer paints
                             // this curve's stroke, its key markers' FILL and its legend
                             // swatch in it, so a channel is one colour everywhere
    int legendIndex = -1;    // the row this curve belongs to
};

// One piece of text and the box it goes in. The box is what the model decides;
// the text itself may be drawn centred in it (frame numbers) or right-aligned
// (value numbers) by the drawing layer.
struct PanelLabel {
    QRect rect;
    QString text;
};

// The panel's complete description for one frame.
//
// GEOMETRY
//   panel   == ctrl::GraphEditor::panelRect(aViewport). The panel covers the
//              whole viewport (it is the fullscreen graph view over the
//              animation), and this model never hardcodes a size: whatever that
//              function returns is the panel.
//   plot    == the editor's own scaler, given `panel`, i.e.
//              ctrl::graph::Scaler::plotRect() = panel minus kMargin on all
//              sides and minus the MEASURED label column on the left
//              (Scaler::axisLabelWidth(); its floor is kAxisLabelWidth). Empty
//              on a viewport too small for that inset; see emptyState.
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
//
// THE OCCLUDED TOP (rulerBandTop)
//   A widget may float ABOVE the viewport and cover the panel's first rows - the
//   project tab bar does, in the application's own layout - and the panel draws no
//   background, so anything under it is simply lost. buildGraphPanelModel therefore
//   takes the occluded height (aTopInset, logical pixels, 0 when nothing covers the
//   panel) and publishes rulerBandTop: the y where the panel's TOP CONTENT may
//   begin, i.e. plot.top() normally and the occluded line when the inset reaches
//   past it (clamped to the plot's bottom, so a viewport whose whole top is covered
//   simply has no room for a ruler band).
//   WHAT MOVES AND WHAT DOES NOT. The RULER BAND moves: the tick marks are drawn
//   down from rulerBandTop (not from plot.top()) and every frame label's box starts
//   below it, and a value label whose own row would fall under the occluded band is
//   nudged down to it - all of that is non-interactive ink, and it is the ink the
//   user cannot see otherwise. The PLOT does not move: it is ctrl::GraphEditor's own
//   rect (the editor's hit-test geometry, and the rect the editor's scaler is pinned
//   to), so `plot`, the curves, the keys, the handles, the playhead and the marquee
//   stay exactly where the editor acts on them. Moving them here would put the drawn
//   key a tab bar's height away from the key a press grabs.
//
// THE AXES (no grid, no rules)
//   frameTickX: the x of every frame the ladder classifies (time::Scaler's
//               TickLevel != None) inside the plot, ascending. The drawing layer
//               draws a SHORT mark down from rulerBandTop at each - 4px is the
//               convention (kFrameTickLength) - and nothing else.
//   valueTickY: the y of the value axis' own ticks, one per window value (max,
//               midpoint, min), in that order. A SHORT mark rightwards from
//               plot.left() at each. A value whose NUMBER the column could not
//               fit still gets its tick - the axis has a tick there whatever can
//               be written beside it, the frame axis' own rule - so a tick can
//               outnumber the valueLabels below (only past the column's ceiling,
//               unreachable at the UI font; see graph_Scaler.h).
//   The model publishes POSITIONS (ints), never spans: a full-height or
//   full-width line cannot be expressed in this structure at all, which is the
//   mechanical form of "no grid".
//
// LABELS (rects only)
//   frameLabels: the frame NUMBER of a Major tick, in a box centred on that tick
//               and always inside the plot (a label near an edge is nudged in).
//               A label is dropped when it would come closer than kFrameLabelGap
//               (8px) to the previous label's right edge - a rule stated on the
//               boxes, not on their centres, so it holds for any font.
//               THE BOX'S TOP EDGE IS THE ROOM A NUMBER HAS: it sits at
//               plot.top() + kFrameTickLength + 1, i.e. under the tick band, and a
//               drawing layer must never put a glyph ABOVE its box's top edge - a
//               font taller than the box is clamped into it (drawLabel in
//               src/gui/ImGuiOverlay.cpp). That pair is what keeps the top row of
//               numbers off the plot's top edge and out of the plot clip; the model
//               measures the box with the default UI font and cannot know the
//               drawing layer's, so the obligation is split rather than assumed.
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
//   The boxes are measured with the default UI font (QFontMetrics(QFont())):
//   the model has no painter, and the rects are advisory geometry, not a text
//   layout. A drawing layer with its own font puts the glyphs in the box it was
//   handed. The column is measured with that same font (it is the scaler that
//   measures it), so the split obligation is still "put the glyphs in the box",
//   not "make them fit".
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
//   handles: one PanelHandle pair per Custom segment of the curve, in segment
//   order (segment 0's two handles, then segment 1's, ...). base is the key the
//   handle belongs to, mapped through the scaler; dot is
//   ctrl::graph::handlePoint(scale, segment, index) - the SAME mapping the
//   editor's hit-test grabs through, so a grab at the dot is the drag the editor
//   performs. radius is ctrl::graph::kHandleRadius (the dot's drawn radius); the
//   grab radius is the larger ctrl::graph::kHandleHitRadius, and the two are
//   deliberately different (the same visual/hit split as a key's).
//   A pair is published whether or not it lies inside the plot: the drawing
//   layer clips (the overlay's plot clip covers the handles like everything
//   else), and a handle the user can grab off the plot's edge must still be
//   published for the highlight to name it. A non-Custom segment publishes no
//   handle, so a curve with no Custom segment has an empty `handles`.
//   color: theme::Graph::keyColor(channel.type()), which falls back to
//   theme::Graph::curve() for a type the palette does not name.
//
// LEGEND
//   legend: one row per (target, channel) with keys, in target then channel
//   order - the same order as the curves. Layout: the swatches are stacked
//   top-to-bottom in legend order in the plot's bottom-left corner, the last row
//   ending kLegendBottomGap (3px) above plot.bottom(), each swatch
//   kLegendSwatchSide (10px - the side of the key HIT square,
//   2 * graph::kKeyRadius; a square colour chip, not the round key marker,
//   which is kKeyDotRadius across)
//   and left-aligned kLegendLeftGap (6px) inside the plot, clear of the value
//   column. The model publishes the swatch and not a row rect: the row is where
//   its swatch is, and the label is drawn beside it for as long as the drawing
//   layer's font needs.
//   The stack never leaves the plot: it is anchored at the bottom and bounded by
//   plot.top(), and by plot.right() for a plot narrower than
//   kLegendLeftGap + kLegendSwatchSide. A row that cannot be laid out inside the
//   plot publishes NO swatch - swatch.isEmpty() - and its identity, colour and
//   label are still published, so the user can still see that the channel exists.
//   Because the stack is anchored at the bottom, the rows left without a swatch
//   are always its FIRST ones; the published swatches are the tail of the
//   legend, in legend order. An empty swatch is not a rectangle at the origin:
//   the drawing layer MUST draw nothing for that row and MUST NOT hit-test it,
//   and no geometry derived from the empty rect is a control either (a label box
//   computed from it is not the row's label), so a consumer lays out a row only
//   while its swatch is non-empty - which is what keeps a row with no room from
//   leaving a phantom control at the panel's corner. Rows are NEVER dropped from
//   the legend for want of room, so the published row count is the number of
//   keyed channels whatever the viewport, and an assistive tool that acts on a
//   row by its index can still toggle a row the pointer cannot reach.
//
// PLAYHEAD
//   playheadX == Scaler::pixelX(aEditor.currentFrame()), or -1 when the editor
//   reports no frame (currentFrame() < 0, i.e. no project is set). It is read
//   even when the plot is empty, because a playhead is a property of the
//   document and not of the geometry.
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
//   With targets but every shown channel hidden, the legend still lists the
//   hidden rows and emptyState names the state: an empty panel must say why it
//   is empty.
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
    // Where the panel's top content may begin: plot.top() unless a widget floating
    // above the viewport covers part of it (see THE OCCLUDED TOP above). Always
    // >= plot.top() and never past the plot's bottom edge.
    int rulerBandTop = -1;
    QVector<int> frameTickX;
    QVector<int> valueTickY;
    QVector<PanelLabel> frameLabels;
    QVector<PanelLabel> valueLabels;
    int zeroY = -1;
    int playheadX = -1;
    // The live box-select rectangle, or an empty rect while no marquee drag is in
    // flight (see the MARQUEE note above). The drawing layer paints it inside the
    // plot clip; nothing is drawn for an empty rect.
    QRect marquee;
    QVector<PanelCurve> curves;
    QVector<PanelLegendRow> legend;
    QString emptyState;
    bool isEmpty() const { return curves.isEmpty() && !emptyState.isEmpty(); }
};

// Builds the model. aViewport is the whole viewport the panel is pinned over;
// aTheme is the palette the curves and swatches take their colours from; aTopInset is
// how many LOGICAL pixels of that viewport's top a widget floating above it covers (0
// when nothing does), which moves the ruler band and nothing else - see THE OCCLUDED
// TOP above.
GraphPanelModel buildGraphPanelModel(
    ctrl::GraphEditor& aEditor, const QSize& aViewport, const theme::Graph& aTheme, int aTopInset = 0
);

} // namespace gui

#endif // GUI_GRAPHPANELMODEL_H
