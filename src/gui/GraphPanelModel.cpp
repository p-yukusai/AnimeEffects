#include "gui/GraphPanelModel.h"

#include <QCoreApplication>
#include <QFontMetrics>
#include <algorithm>

#include "core/Frame.h"
#include "core/ObjectNode.h"
#include "core/TimeKey.h"
#include "core/TimeLine.h"
#include "ctrl/GraphEditor.h"
#include "ctrl/graph/graph_Curve.h"
#include "ctrl/graph/graph_Geometry.h"
#include "ctrl/graph/graph_Scaler.h"
#include "gui/theme/Graph.h"

namespace {

// The panel's layout constants. They were the deleted QPainter renderer's own
// convention (ctrl/graph/graph_Renderer.cpp, removed in the ImGui cutover), and
// they are stated here because the model is now their one home - the renderer
// kept them private to its translation unit. The constants the rest of the code
// shares are read from their own homes instead: Scaler::kMargin, the label
// column's own size rule (Scaler::axisLabelWidth(), measured - see THE LABEL
// COLUMN in graph_Scaler.h) and Scaler::kAxisLabelPad from the scaler,
// ctrl::graph::kKeyRadius and kKeyDotRadius from graph_Geometry.h.
//
// The short tick marks replaced the renderer's full grid lines. Their length is
// the drawing layer's business (the model publishes positions); 4px is the
// convention this model documents, and the frame numbers are placed clear of it.
constexpr int kFrameTickLength = 4;
// Least gap between the right edge of one frame number's box and the left edge
// of the next. The renderer leaned on the ladder's 48px major spacing instead,
// which is a property of the axis rather than of the label; stating it on the
// boxes keeps two numbers from touching under ANY font metric.
constexpr int kFrameLabelGap = 8;
// Horizontal padding of a text box around its glyphs, the renderer's own
// `horizontalAdvance(text) + 4`.
constexpr int kLabelPad = 2;
// The legend swatch is a key square: side 2 * ctrl::graph::kKeyRadius.
constexpr int kLegendSwatchSide = 2 * ctrl::graph::kKeyRadius;
// The legend's insets from the plot's bottom-left corner: the left gap and the
// bottom gap are the renderer's caption offsets (the caption sat at
// plotLeft + 6, plotBottom - height - 3). The channel name is drawn beside the
// swatch by the drawing layer, whose font only it knows.
constexpr int kLegendLeftGap = 6;
constexpr int kLegendBottomGap = 3;
constexpr int kLegendRowPad = 2;

// The sampling budget of a curve: one point per ~2px of plot width, bounded so a
// dense key set does not explode the point count and a single long segment
// cannot ask for thousands of samples.
constexpr int kMinSamplesPerSegment = 2;
constexpr int kMaxSamplesPerSegment = 256;

// The value axis' number format, and the value labels' texts, are
// ctrl::graph::Scaler::valueAxisText: the label and the column's WIDTH are
// measured off the SAME strings, so the format must not be restated here.

// The frame axis: the ladder's own ticks, and a number on the majors.
//
// The frame window is the plot's edges through the frame mapping: a ladder can
// be far denser than the plot is wide, and only what is visible is published.
// Ticks are positions, never spans - there is no grid.
void buildFrameAxis(gui::GraphPanelModel& aModel, const ctrl::graph::Scaler& aScale, const QFontMetrics& aMetrics) {
    const QRect& plot = aModel.plot;
    const core::Frame first = aScale.frameAt(plot.left());
    const core::Frame last = aScale.frameAt(plot.right());
    const int textHeight = aMetrics.height();
    // The box sits below the tick marks, below the band the panel's top may use (see
    // THE OCCLUDED TOP in the header: a widget floating above the viewport moves the
    // band down, and the numbers with it). Without room for a whole text line under
    // the marks there is nothing to label.
    const int labelTop = aModel.rulerBandTop + kFrameTickLength + 1;
    const bool roomForText = (labelTop + textHeight - 1) <= plot.bottom();

    bool anyLabel = false;
    int lastLabelRight = 0;

    for (int frame = first.get(); frame <= last.get(); ++frame) {
        const ctrl::time::Scaler::TickLevel level = aScale.frameScale().attribute(frame).level;
        if (level == ctrl::time::Scaler::TickLevel::None)
            continue; // the ladder says this frame sits too close to its neighbours to mark

        const int x = aScale.pixelX(core::Frame(frame));
        // pixelX rounds, so a frame whose mapping falls a pixel outside the plot
        // would otherwise be ticked in the margin.
        if (x < plot.left() || plot.right() < x)
            continue;
        if (aModel.frameTickX.isEmpty() || aModel.frameTickX.last() != x)
            aModel.frameTickX.append(x);

        if (level != ctrl::time::Scaler::TickLevel::Major || !roomForText)
            continue;

        // Only Major frames are numbered. The box is centred on its tick and
        // then nudged inside the plot, so a number at the window's edge stays
        // readable instead of being cut off by the panel's border.
        const QString text = QString::number(frame);
        const int width = aMetrics.horizontalAdvance(text) + 2 * kLabelPad;
        if (width > plot.width())
            continue; // the number cannot fit inside the plot at all

        const int left = std::clamp(x - width / 2, plot.left(), plot.right() - width + 1);
        if (anyLabel && left < lastLabelRight + kFrameLabelGap)
            continue; // this number would touch the previous one

        gui::PanelLabel label;
        label.text = text;
        label.rect = QRect(left, labelTop, width, textHeight);
        aModel.frameLabels.append(label);
        lastLabelRight = left + width;
        anyLabel = true;
    }
}

// The value axis: the window's max / midpoint / min, right-aligned in the left
// column, each on the pixel its value maps to. Two of the three rounding to the
// same string would draw one number on top of itself, so the duplicate is
// dropped (the model's own rule).
//
// The column's width is the scaler's stable reserve for the FORMAT
// (Scaler::axisLabelWidth - the widest label 'g'/4 can write, measured once per
// font), so the numbers fit by construction while the column itself never moves
// when the window does; a number that still does not fit the box - reachable
// only once the column has hit its ceiling, which this format's widest label
// (62px at the UI font) cannot reach - is DROPPED rather than published
// overflowing, the same way the frame axis drops a number wider than the plot.
// Its tick is still published: the axis HAS a tick at that value whatever can be
// written beside it.
void buildValueAxis(gui::GraphPanelModel& aModel, const ctrl::graph::Scaler& aScale, const QFontMetrics& aMetrics) {
    const QRect& column = aModel.valueColumn;
    constexpr int kAxisLabelPad = ctrl::graph::Scaler::kAxisLabelPad;

    // The baseline exists only while 0 is inside the window: a line drawn for an
    // out-of-window zero would sit on (or outside) the plot edge and read as a
    // border rather than as the value origin.
    if (aScale.valueMin() <= 0.f && 0.f <= aScale.valueMax())
        aModel.zeroY = aScale.pixelY(0.f);

    const int textHeight = aMetrics.height();
    // The column the numbers may be placed in: the value column, with its bottom
    // shortened by one text line so a number centred on a value can still fit. The
    // occluded band moves the top of that window down (see THE OCCLUDED TOP), and when
    // the band has reached past the bottom there is no room for a number at all - the
    // same answer as a column shorter than a text line.
    const int columnTop = std::max(column.top(), aModel.rulerBandTop);
    const int columnBottom = column.bottom() - textHeight + 1;
    if (column.height() < textHeight || columnBottom < columnTop)
        return;

    const float values[3] = {aScale.valueMax(), 0.5f * (aScale.valueMin() + aScale.valueMax()), aScale.valueMin()};
    QString shown;
    for (const float value : values) {
        const QString text = ctrl::graph::Scaler::valueAxisText(value);
        if (text == shown)
            continue;
        shown = text;

        // The number is centred on its value and clamped into the column: the
        // extreme values sit on the plot's edges, where half a text height would
        // spill past the panel. The column starts at the plot's top edge, so the
        // topmost number is also clamped below the occluded band (the widget floating
        // above the viewport covers the value column too - see THE OCCLUDED TOP).
        const int y = aScale.pixelY(value);
        const int top = std::clamp(y - textHeight / 2, columnTop, columnBottom);

        aModel.valueTickY.append(y);

        // The box the drawing layer right-aligns the text in. The column was
        // MEASURED from these very strings, so this holds for every label the
        // value axis can write at the UI font; the guard is the honest floor
        // under a wider font (the column's own ceiling, see graph_Scaler.h) and
        // is the frame axis' rule: a number that cannot fit is not drawn
        // half-way off the panel.
        const int boxWidth = column.width() - kAxisLabelPad;
        if (aMetrics.horizontalAdvance(text) > boxWidth)
            continue;

        gui::PanelLabel label;
        label.text = text;
        label.rect = QRect(column.left(), top, boxWidth, textHeight);
        aModel.valueLabels.append(label);
    }
}

// The legend: one row per (target, channel) that has keys - the same set of
// channels the editor builds entries for (a type whose map is empty has no curve
// and no row), in target then channel order, hidden channels included. The row's
// colour is the curve's colour, so a swatch and its curve always read as one
// feature.
void buildLegend(
    gui::GraphPanelModel& aModel, ctrl::GraphEditor& aEditor, const theme::Graph& aTheme, const QFontMetrics& aMetrics
) {
    for (core::ObjectNode* node : aEditor.targets()) {
        if (!node)
            continue;
        const core::TimeLine* timeLine = node->timeLine();
        if (!timeLine) // a folder, or a node kind that holds no keys at all
            continue;

        for (core::TimeKeyType type : ctrl::graph::Channel::graphableTypes()) {
            if (timeLine->map(type).isEmpty())
                continue; // no keys: nothing to draw and nothing to toggle

            for (const ctrl::graph::Channel& channel : ctrl::graph::Channel::channelsOf(type)) {
                gui::PanelLegendRow row;
                row.node = node;
                row.channel = channel;
                row.label = node->name() + QStringLiteral(" : ") + channel.name();
                const QColor color = aTheme.keyColor(channel.type());
                row.color = color.isValid() ? color : aTheme.curve();
                row.visible = aEditor.isChannelVisible(*node, channel);
                aModel.legend.append(row);
            }
        }
    }

    // The swatches are stacked top to bottom in the plot's bottom-left corner,
    // the last row ending kLegendBottomGap above the plot's bottom edge. The
    // stack grows upward, so the first row is the one furthest from the edge.
    //
    // The stack is bounded by the plot: the top edge cuts it, and a plot
    // narrower than the left gap plus the swatch side has no room for a swatch
    // at all. A row that does not fit publishes NO swatch - an empty rect -
    // rather than one above the plot or in the margin. The row itself stays in
    // the legend with its identity, colour and label, because the user has to
    // be able to see that the channel exists and an assistive tool acts on a
    // row by index; the empty swatch is the model's own truthful signal that
    // the row has no geometry to draw or to hit (see the header's LEGEND).
    // Because the stack is anchored at the bottom, the rows left without a
    // swatch are always its first ones.
    const int rows = aModel.legend.size();
    if (rows <= 0)
        return;

    const int rowHeight = std::max(aMetrics.height() + 2 * kLegendRowPad, kLegendSwatchSide + 2 * kLegendRowPad);
    const int stackTop = aModel.plot.bottom() + 1 - kLegendBottomGap - rows * rowHeight;
    bool room = true;
    for (int i = rows - 1; 0 <= i; --i) {
        const int top = stackTop + i * rowHeight + (rowHeight - kLegendSwatchSide) / 2;
        const QRect swatch(aModel.plot.left() + kLegendLeftGap, top, kLegendSwatchSide, kLegendSwatchSide);
        // Walking upward from the bottom, so the first row that does not fit ends
        // the stack: a higher row cannot fit where a lower one did not.
        room = room && aModel.plot.contains(swatch);
        if (room)
            aModel.legend[i].swatch = swatch;
    }
}

// The curves: one per visible legend row, sampled and mapped through the scaler
// (Curve::buildPolyline over the plot's frame window, then Scaler::pixelXOfFrame
// / Scaler::pixelY), with the keys and the Custom tangent handles as the shared
// hit-test geometry (ctrl/graph/graph_Geometry.h).
void buildCurves(gui::GraphPanelModel& aModel, const ctrl::graph::Scaler& aScale) {
    for (int i = 0; i < aModel.legend.size(); ++i) {
        const gui::PanelLegendRow& row = aModel.legend[i];
        if (!row.visible)
            continue; // a hidden channel keeps its row and contributes no curve

        const core::TimeLine* timeLine = row.node ? row.node->timeLine() : nullptr;
        if (!timeLine)
            continue;

        const ctrl::graph::Curve curve(*timeLine, row.channel);

        gui::PanelCurve out;
        out.node = row.node;
        out.channel = row.channel;
        out.color = row.color;
        out.legendIndex = i;

        const core::Frame first = aScale.frameAt(aModel.plot.left());
        const core::Frame last = aScale.frameAt(aModel.plot.right());
        const int segments = std::max(1, curve.segmentCount());
        // One point per ~2px of plot width per segment (the drawing budget),
        // floored so the analytic easings keep their shape and capped so a long
        // segment cannot ask for thousands of samples.
        const int samples = std::clamp(aModel.plot.width() / (2 * segments), kMinSamplesPerSegment, kMaxSamplesPerSegment);

        QVector<QPointF> polyline;
        curve.buildPolyline(first, last, samples, polyline);
        out.points.reserve(polyline.size());
        for (const QPointF& sample : polyline) {
            out.points.append(QPointF(aScale.pixelXOfFrame(sample.x()), double(aScale.pixelY(float(sample.y())))));
        }

        // ctrl::graph::keyRect is the editor's HIT-TEST geometry: one rectangle,
        // so a marquee or a right-click lands on the key the editor agrees on. A
        // key straddling the plot's edge is cut rather than skipped - the drawing
        // layer clips a key over the panel's chrome.
        //
        // The DOT is the other half: keyDot() is the key marker's own centre and
        // the drawing layer paints a circle of radius kKeyDotRadius about it
        // (ctrl/graph/graph_Geometry.h's VISUAL/HIT split). It is published
        // rather than derived from the rect on purpose - the rect above is CUT
        // at the plot's edge, so its centre is not the key's centre for a key on
        // the border, and a dot reconstructed from it would sit off the key. Same
        // order and same length as `keys` and `keyFrames`: a key that publishes
        // no rect (one entirely outside the plot) publishes no dot either.
        for (const core::TimeKey* key : curve.keys()) {
            if (!key)
                continue;
            const core::Frame frame(key->frame());
            const float value = curve.valueOf(*key);
            const QRect rect = ctrl::graph::keyRect(aScale, frame, value).intersected(aModel.plot);
            if (!rect.isEmpty()) {
                out.keys.append(rect);
                out.keyFrames.append(frame);
                out.keyDots.append(ctrl::graph::keyDot(aScale, frame, value));
            }
        }

        // The Custom segments' tangent handles, in segment order: one pair per
        // segment, handle 0 leaving `from` and 1 arriving at `to`. The dot is
        // ctrl::graph::handlePoint - the editor's own grab mapping - and the base
        // is the key the handle belongs to, so the drawn stem starts on the key
        // it moves. Published whether or not the pair lies inside the plot: the
        // drawing layer clips, and a handle off the plot's edge is still one the
        // user can grab.
        const int handleSegments = curve.segmentCount();
        for (int s = 0; s < handleSegments; ++s) {
            const ctrl::graph::Curve::Segment segment = curve.segment(s);
            if (!curve.isCustom(segment))
                continue; // only a Custom easing has tangent handles to show

            const core::TimeKey* owners[2] = { segment.from, segment.to };
            const float ownerValues[2] = { segment.fromValue, segment.toValue };
            for (int h = 0; h < 2; ++h) {
                gui::PanelHandle handle;
                handle.segment = s;
                handle.index = h;
                handle.base = QPointF(
                    aScale.pixelXOfFrame(static_cast<double>(owners[h]->frame())),
                    static_cast<double>(aScale.pixelY(ownerValues[h]))
                );
                handle.dot = ctrl::graph::handlePoint(aScale, segment, h);
                handle.radius = ctrl::graph::kHandleRadius;
                out.handles.append(handle);
            }
        }

        aModel.curves.append(out);
    }
}

// Why an empty panel is empty. A panel with nothing in it has to say so: "no
// keys" and "all hidden" are different states with different fixes.
void buildEmptyState(gui::GraphPanelModel& aModel, ctrl::GraphEditor& aEditor) {
    if (!aModel.curves.isEmpty())
        return;

    bool anyTarget = false;
    for (core::ObjectNode* node : aEditor.targets()) {
        if (node)
            anyTarget = true;
    }

    if (aModel.legend.isEmpty()) {
        aModel.emptyState =
            anyTarget ? gui::graphPanelTr(QT_TRANSLATE_NOOP("GraphPanel", "No keys on the selected layers"))
                      : gui::graphPanelTr(QT_TRANSLATE_NOOP("GraphPanel", "Select a layer to show its curves"));
    } else {
        aModel.emptyState =
            gui::graphPanelTr(QT_TRANSLATE_NOOP("GraphPanel", "All channels of the selected layers are hidden"));
    }
}

} // namespace

namespace gui {

    //-------------------------------------------------------------------------------------------------
    QString graphPanelTr(const char* aSourceText) {
        // The context is spelled out on BOTH sides: the QT_TRANSLATE_NOOP at the
        // call site is what the extractor reads, and this is what Qt asks at
        // runtime. They are the same literal "GraphPanel" written once at each
        // call site, so a renamed context fails the extractor's check rather than
        // silently shipping English.
        return QCoreApplication::translate("GraphPanel", aSourceText);
    }

    //-------------------------------------------------------------------------------------------------
    GraphPanelModel buildGraphPanelModel(
        ctrl::GraphEditor& aEditor, const QSize& aViewport, const theme::Graph& aTheme, int aTopInset
    ) {
        GraphPanelModel model;

        // The panel rect is the editor's own, whatever it is: the fullscreen
        // graph view over the animation. Nothing here hardcodes a size, and
        // nothing here writes back into the editor's scaler.
        model.panel = ctrl::GraphEditor::panelRect(aViewport);

        ctrl::graph::Scaler scale = aEditor.scale();
        scale.setRect(model.panel);

        model.plot = scale.plotRect();
        // The band the panel's top content may use: the plot's own top, moved down by
        // the occluded inset (see THE OCCLUDED TOP in the header). A negative inset is
        // no inset at all, and the result never leaves the plot.
        const int inset = std::max(0, aTopInset);
        model.rulerBandTop = std::min(model.plot.top() + inset, model.plot.bottom() + 1);
        // The column's width is the scaler's STABLE reserve for the value format
        // (see THE LABEL COLUMN in graph_Scaler.h) - the width the plot was
        // already inset by - so the column and the plot are one geometry, a
        // number can never need more room than the column has, and a gesture that
        // changes which label is widest cannot move either one.
        model.valueColumn = QRect(
            model.panel.left() + ctrl::graph::Scaler::kMargin, model.plot.top(), scale.axisLabelWidth(),
            model.plot.height()
        );

        // The playhead belongs to the document, not to the geometry, so it is
        // read before the plot's own guards: a currentFrame() below 0 is "no
        // project", and there is no playhead to place (core frame numbers start
        // at 1, so a negative frame is never a real one).
        const core::Frame playhead = aEditor.currentFrame();
        if (playhead.get() >= 0)
            model.playheadX = scale.pixelX(playhead);

        // The live box-select rectangle, straight from the gesture that owns it: the
        // editor publishes an EMPTY rect while no drag is in flight (and for a press
        // with no movement), which is exactly "draw nothing". Normalised and clipped
        // to the plot by the editor, so this is a copy of one geometry rather than a
        // second derivation of it.
        model.marquee = aEditor.marqueeRect();

        if (model.plot.isEmpty()) {
            // Nothing to lay an axis on. The panel, the band, the playhead, the marquee
            // (the editor's own rect, empty unless a drag is live) and the reason are
            // still published: an empty panel must say why it is empty.
            model.rulerBandTop = model.plot.bottom() + 1;
            model.emptyState =
                graphPanelTr(QT_TRANSLATE_NOOP("GraphPanel", "No room for the graph in this viewport"));
            return model;
        }

        // Text is measured with the default UI font: the model has no painter by
        // design, and a label box is advisory geometry the drawing layer puts its
        // own glyphs in.
        const QFontMetrics metrics{QFont()};

        buildFrameAxis(model, scale, metrics);
        buildValueAxis(model, scale, metrics);
        buildLegend(model, aEditor, aTheme, metrics);
        buildCurves(model, scale);
        buildEmptyState(model, aEditor);

        return model;
    }

} // namespace gui
