#include "gui/GraphPanelModel.h"

#include <QCoreApplication>
#include <QFontMetrics>
#include <algorithm>
#include <limits>

#include "XCAssert.h"
#include "core/Frame.h"
#include "core/ObjectNode.h"
#include "core/TimeFormat.h"
#include "core/TimeKey.h"
#include "core/TimeLine.h"
#include "ctrl/GraphEditor.h"
#include "ctrl/TimeLineEditor.h"
#include "ctrl/graph/graph_Curve.h"
#include "ctrl/graph/graph_Geometry.h"
#include "ctrl/graph/graph_Scaler.h"
#include "gui/FrameRuler.h"
#include "gui/theme/Graph.h"

// The frame axis is the TIMELINE'S RULER (see THE AXES and LABELS in the header), so
// its metrics come from the timeline's own homes instead of being restated here: the
// band, the number row and the playhead pill are ctrl::TimeLineEditor's public
// constants, re-exported as gui::frameRuler and pinned to them below; the tick length,
// the 6px-per-character reserve and the 8px number margin are TimeRuler.cpp's local
// numbers, restated in gui::frameRuler because the model and the drawing layer both
// read them.
static_assert(gui::frameRuler::kBandHeight == ctrl::TimeLineEditor::kHeaderHeight,
              "the graph's ruler band must be the timeline's kHeaderHeight");
// The band is a SECTION the scaler reserves above the plot (Scaler::plotRect), so
// the rows the model publishes as the band and the rows the scaler keeps out of the
// plot are one number: a drift would put curve ink through the ruler or leave dead
// rows above the graph.
static_assert(ctrl::graph::Scaler::kRulerBandHeight == gui::frameRuler::kBandHeight,
              "the scaler's reserved band must be the ruler's kBandHeight");
static_assert(gui::frameRuler::kNumberTop == ctrl::TimeLineEditor::kNumberTop,
              "the graph's number row must start where the timeline's does");
static_assert(gui::frameRuler::kNumberHeight == ctrl::TimeLineEditor::kNumberHeight,
              "the graph's number row must be the timeline's kNumberHeight");
static_assert(gui::frameRuler::kPillPadding == ctrl::TimeLineEditor::kPillPadding,
              "the graph's playhead pill must use the timeline's padding");
static_assert(gui::frameRuler::kPillRadius == ctrl::TimeLineEditor::kPillRadius,
              "the graph's playhead pill must use the timeline's radius");

namespace {

// The panel's layout constants. They were the deleted QPainter renderer's own
// convention (ctrl/graph/graph_Renderer.cpp, removed in the ImGui cutover), and
// they are stated here because the model is now their one home - the renderer
// kept them private to its translation unit. The constants the rest of the code
// shares are read from their own homes instead: Scaler::kMargin, the label
// column's own size rule (Scaler::axisLabelWidth(), measured - see THE LABEL
// COLUMN in graph_Scaler.h) and Scaler::kAxisLabelPad from the scaler,
// ctrl::graph::kKeyDotRadius from graph_Geometry.h.
//
// The chip that names a curve IN PLACE (see CHIPS in the header): the box's own
// padding around the text and its inset from the plot's right edge. The numbers
// are the reference's - mock-graph.js right-aligns the text 8px inside the plot
// and pads the chip's backing 6px left and right (its backdrop is the text box
// grown by (6, 8)).
constexpr int kChipRightGap = 8;
constexpr int kChipPadX = 6;

// The sampling budget of a curve: one point per ~2px of plot width, bounded so a
// dense key set does not explode the point count and a single long segment
// cannot ask for thousands of samples.
constexpr int kMinSamplesPerSegment = 2;
constexpr int kMaxSamplesPerSegment = 256;

// The value axis' number format, and the value labels' texts, are
// ctrl::graph::Scaler::valueAxisText: the label and the column's WIDTH are
// measured off the SAME strings, so the format must not be restated here.

// The frame axis: THE TIMELINE'S RULER (see THE AXES and LABELS in the header).
//
// The frame window is the RULER BAND's edges through the frame mapping (the band spans
// the panel, and the shared mapping puts frames on the timeline's own columns): a
// ladder can be far denser than the ruler is wide, and only what is visible is
// published. Ticks are
// positions, never spans - there is no grid. Each tick's TIER travels beside its
// position in frameTickLevel (same order, same length): the ladder that decided the
// tick also said which tier it is, so the classification is read here rather than
// re-derived by a drawing layer from the tick spacing - and what a tier looks like
// (its alpha; the length is one for every level) stays that layer's decision, the way
// the model publishes a position and not a span.
//
// ONE FORMATTER for the whole axis: every string this function writes - a ladder
// number, a boundary number and the merged "first-last" label - is
// core::TimeFormat::frameToString(frame, aFormatType). The timeline's own
// TimeRuler::renderHeader is the source of the rules below (its first/last boundary
// labels, its 6px-per-character reserve, its kNumberMargin, its pill-column
// suppression, and its "number every showNumber tier"), so the two rulers cannot
// disagree about which frames are numbered or what a frame is called.
//
// THE ACTIVE RANGE is the timeline's fade basis (ctrl::TimeLineEditor::playbackRange):
// the project's frame range, or the play window while it is active. Every tick and
// every frame label publishes whether its frame is INSIDE it, and no ink: the drawing
// layer multiplies an outside tick's tier alpha and an outside number's alpha by the
// timeline's fixed 0.4 (TimeRuler::renderHeader). A merged boundary label is inside
// only when BOTH of its frames are, which is the timeline's own rule.
void buildFrameAxis(
    gui::GraphPanelModel& aModel, const ctrl::graph::Scaler& aScale, const QFontMetrics& aMetrics, int aFps,
    core::TimeFormatType aFormatType, const util::Range& aFrameRange, const util::Range& aActiveRange,
    const ctrl::TimeLineEditor::PlayWindow& aWindow, int aPlayheadFrame
) {
    using namespace gui::frameRuler;
    const QRect& plot = aModel.plot;
    if (aModel.rulerBand.isEmpty()) {
        // No band, no ruler ink. The playhead still exists as a document fact; its
        // line falls back to the plot's top edge in the drawing layer.
        if (aModel.playheadX >= 0)
            aModel.playheadLineTop = plot.top();
        return;
    }
    // THE RULER'S OWN COLUMNS. The band spans the PANEL, and the frame mapping is the
    // timeline's (graph_Scaler.h: THE SHARED WINDOW'S MAPPING), so the axis is
    // enumerated over the band - not over the plot, whose left columns are the value
    // column's and whose right ones belong to the scroll bar's reserve.
    const QRect& band = aModel.rulerBand;

    // THE RULES ARE THE TIMELINE'S (gui/FrameRuler.h): which frames are ticked and at
    // which tier, which are numbered, how a number yields to the project's own first and
    // last frame, how those two collapse into one "first-last" label, how the playhead's
    // pill suppresses the number in its column, and the temporary window's band, stems
    // and pills. This function is the ADAPTER: it supplies the panel's geometry, the
    // panel's font measurements and the editor's facts, and copies the published model
    // into the panel's own fields. Nothing about the rules lives here any more.
    const core::TimeFormat format(aFrameRange, aFps);

    Inputs in;
    in.band = band;
    in.visible = band;
    in.labelTop = band.top() + kNumberTop;
    in.frameRange = aFrameRange;
    in.hasFrameRange = true;
    in.activeRange = aActiveRange;
    in.visibleFrames = util::Range(aScale.frameAt(band.left()).get(), aScale.frameAt(band.right()).get());
    in.fps = aFps;
    in.formatType = aFormatType;
    in.hasPlayhead = aPlayheadFrame != std::numeric_limits<int>::min();
    in.playheadFrame = aPlayheadFrame;
    in.playheadText = aModel.playheadText;
    in.playheadTextWidth = aMetrics.horizontalAdvance(aModel.playheadText);
    if (aWindow.active()) {
        in.windowStart = aWindow.start;
        in.windowEnd = aWindow.end;
        in.windowStartText = format.frameToString(aWindow.start, aFormatType);
        in.windowEndText = format.frameToString(aWindow.end, aFormatType);
        in.windowStartTextWidth = aMetrics.horizontalAdvance(in.windowStartText);
        in.windowEndTextWidth = aMetrics.horizontalAdvance(in.windowEndText);
        in.windowMergedTextWidth =
            aMetrics.horizontalAdvance(in.windowStartText + QString::fromUtf8("\u2013") + in.windowEndText);
    }

    Axis axis;
    axis.pixelOfFrame = [&aScale](int aFrame) { return aScale.pixelXOfFrame(double(aFrame)); };
    axis.frameOfPixel = [&aScale](int aPixel) { return aScale.frameAt(aPixel).get(); };
    axis.attributeAt = [&aScale](int aFrame, double aPpf) {
        return aScale.frameScale().attributeAt(aFrame, aPpf);
    };
    axis.pixelsPerFrame = aScale.framePpf();
    axis.tickStride = aScale.frameScale().tickStride(aScale.framePpf());

    const Model ruler = build(in, axis);

    // The playhead's pill and the top of its line: the panel draws both from the model.
    aModel.playheadPill = ruler.playhead.rect;
    aModel.playheadLineTop = ruler.playheadLineTop;

    // The ticks, with their tier beside their position (same order, same length, so the
    // two vectors cannot desync): the model classifies the tick, and how a tier LOOKS
    // stays the drawing layer's decision.
    aModel.frameTickX.clear();
    aModel.frameTickLevel.clear();
    aModel.frameTickInActiveRange.clear();
    for (const Tick& tick : ruler.ticks) {
        aModel.frameTickX.append(tick.x);
        aModel.frameTickLevel.append(tick.level);
        aModel.frameTickInActiveRange.append(tick.inActiveRange);
    }

    // The labels, in the panel's own published box convention: the shared model states
    // (left, top, width) because the two hosts form their text rects differently.
    aModel.frameLabels.clear();
    for (const Label& label : ruler.labels) {
        aModel.frameLabels.append(
            {QRect(label.left, label.top, label.width, kNumberHeight), label.text, label.frame, label.endFrame,
             label.inActiveRange}
        );
    }

    const PlayWindowOverlay& window = ruler.window;
    gui::PanelPlayWindow out;
    out.active = window.active;
    out.band = window.band;
    out.startStem = window.startStem;
    out.endStem = window.endStem;
    out.startPill = window.start.rect;
    out.endPill = window.end.rect;
    out.startText = window.start.text;
    out.endText = window.end.text;
    out.mergedPill = window.merged.rect;
    out.mergedText = window.merged.text;
    aModel.playWindow = out;
}

// THE FRAME RANGE'S TWO GUIDES, in the panel's own columns: the project's first and
// last frame mapped through the SAME scaler the frame ticks and the playhead use, so a
// guide lands on the tick of its frame (and on the timeline's column for it while the
// timeline owns the frame axis).
//
// THE RULE IS THE TIMELINE'S (ctrl/time/time_BodyModel.cpp's buildRangeMarkers): two
// columns, one for each end of the range, and a DEGENERATE range - both ends on one
// column - is ONE line rather than two overdrawn at the same pixel. A reversed range
// (min > max) marks nothing: it names no span.
void buildRangeGuides(
    gui::GraphPanelModel& aModel, const ctrl::graph::Scaler& aScale, const util::Range& aProjectRange,
    bool aPlayWindowActive
) {
    aModel.rangeGuideX.clear();
    aModel.rangeGuideRecede = aPlayWindowActive;
    if (aProjectRange.min() > aProjectRange.max())
        return;

    const int first = aScale.pixelX(core::Frame(aProjectRange.min()));
    const int last = aScale.pixelX(core::Frame(aProjectRange.max()));
    aModel.rangeGuideX.append(first);
    if (last != first)
        aModel.rangeGuideX.append(last);
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
    // column already begins at the plot's top (below the ruler band), and the
    // occluded-top line can only be at or above it now (THE OCCLUDED TOP: the band
    // never moves into the plot) - the clamp keeps that true for a hand-built model.
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
        // spill past the panel. The column starts at the plot's top edge - below the
        // ruler band, which owns the rows above it.
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

// The channel list: one row per (target, channel) that has keys - the same set of
// channels the editor builds entries for (a type whose map is empty has no curve
// and no row), in target then channel order, hidden channels included. The row's
// colour is the curve's colour, so the list's swatch and its curve always read as
// one feature. No row GEOMETRY is laid out: the list is the application's own
// QMenu (see the header's CHANNEL LIST), and the button that opens it is the DOCK
// HEADER's own control - the panel publishes no control of its own and no words for
// one.
void buildChannels(
    gui::GraphPanelModel& aModel, ctrl::GraphEditor& aEditor, const theme::Graph& aTheme
) {
    // focusedNode() hands out a CONST pointer (GraphEditor.h), and this model only
    // ever COMPARES it with the targets' own (non-const) pointers - it never mutates
    // through it - so the local keeps the const rather than casting it away.
    const core::ObjectNode* focusedNode = aEditor.hasFocus() ? aEditor.focusedNode() : nullptr;
    const ctrl::graph::Channel focusedChannel = aEditor.focusedChannel();

    for (core::ObjectNode* node : aEditor.targets()) {
        if (!node)
            continue;
        const core::TimeLine* timeLine = node->timeLine();
        if (!timeLine) // a folder, or a node kind that holds no keys at all
            continue;

        for (core::TimeKeyType type : ctrl::graph::Channel::graphableTypes()) {
            const bool trackStorage = core::TimeLine::usesChannelTracks(type);
            if (!trackStorage && timeLine->map(type).isEmpty())
                continue; // no keys: nothing to draw and nothing to toggle

            // The rows of one type are one LEGEND GROUP (design section 7): the
            // type names the property and each component gets its own bracket -
            // "Move [X][Y]"; a one-component type is simply "Opacity" or "Rotate".
            // Built from the same Channel table the rows are enumerated by, so the
            // heading and its entries cannot disagree, and written once per
            // (layer, type) as PanelChannelRow::group.
            const QVector<ctrl::graph::Channel> channels = ctrl::graph::Channel::channelsOf(type);
            const QString typeName = core::TimeLine::getTimeKeyDisplayName(type);
            // A multi-component property needs its components spelled out; a
            // one-component one is self-describing, so its group text IS its type
            // name ("Opacity", "Rotate") and the list omits the redundant heading.
            QString group = typeName;
            if (channels.size() > 1) {
                group += QLatin1Char(' ');
                for (int i = 0; i < channels.size(); ++i) {
                    group += QLatin1Char('[');
                    group += channels.at(i).componentName();
                    group += QLatin1Char(']');
                }
            }

            for (const ctrl::graph::Channel& channel : channels) {
                if (trackStorage && !timeLine->hasChannelKeys(type, channel.component()))
                    continue;
                gui::PanelChannelRow row;
                row.node = node;
                row.channel = channel;
                row.layer = node->name();
                row.group = group;
                // A one-component type has no sibling to be told apart from, so the
                // component word is noise: "Opacity", not "Opacity Opacity" (the reference
                // draws exactly this chip, mock-graph.js's _curveLabels).
                row.name = channels.size() > 1 ? channel.name() : typeName;
                // The one channel hue table: axis where the component lies on an
                // axis, the type's own hue otherwise. The curve, its key markers,
                // its chip and the list's swatch all read this one answer, and the
                // timeline reads the same table for the type's keys and lane label.
                const QColor color = aTheme.channelColor(channel.type(), channel.component());
                row.color = color.isValid() ? color : aTheme.curve();
                row.visible = aEditor.isChannelVisible(*node, channel);
                row.focused = node == focusedNode && channel == focusedChannel;
                aModel.channels.append(row);
            }
        }
    }
}

// The curves: one per visible channel row, sampled and mapped through the scaler
// (Curve::buildPolyline over the plot's frame window, then Scaler::pixelXOfFrame
// / Scaler::pixelY), with the keys and every segment's tangent handles as the
// shared hit-test geometry (ctrl/graph/graph_Geometry.h), the MARKER STATE of every
// key (the editor's own two queries - see PanelCurve::keySelected), the key's own
// PROPORTIONAL classification (PanelCurve::keyProportional), and the chip that names
// the curve in place.
void buildCurves(
    gui::GraphPanelModel& aModel, ctrl::GraphEditor& aEditor, const ctrl::graph::Scaler& aScale,
    const QFontMetrics& aMetrics
) {
    // The drawing layer inks a curve's tangent handles ONLY for the FOCUSED channel
    // (GraphPanelFrame's drawsHandles: with no focus it draws every curve's; with one it
    // draws that curve's alone), so a pair built for any other row is work nobody
    // reads - and half of it is the expensive kind: an Auto segment's pair is DERIVED
    // by Curve::ghostHandles from the key's neighbours. The rows already carry the
    // predicate (PanelChannelRow::focused, filled by buildChannels from the same editor
    // state the drawing layer reads), so it costs one walk of the rows to know whether
    // any of them is the focused one.
    //
    // WHY THE ROWS AND NOT aEditor.hasFocus(): they agree. buildChannels takes its
    // focused node as null when the editor has no focus, so no row can be focused while
    // the editor reports none - anyFocused implies the focus the drawing layer will call
    // valid, and with that focus it draws exactly the curve whose row is focused. The
    // converse is deliberately the SAFE side: an editor focus that matches no row (a
    // hidden channel, or a node that is not among the targets) leaves anyFocused false,
    // so every row publishes its pairs - which is more than the drawing layer uses,
    // never less.
    bool anyFocused = false;
    for (const gui::PanelChannelRow& row : aModel.channels) {
        if (row.focused) {
            anyFocused = true;
            break;
        }
    }

    for (int i = 0; i < aModel.channels.size(); ++i) {
        const gui::PanelChannelRow& row = aModel.channels[i];
        if (!row.visible)
            continue; // a hidden channel keeps its row and contributes no curve

        const core::TimeLine* timeLine = row.node ? row.node->timeLine() : nullptr;
        if (!timeLine)
            continue;

        const ctrl::graph::Curve curve = [&] {
            ctrl::graph::Curve made(*timeLine, row.channel);
            made.setLoop(aModel.projectLoop, aModel.projectLoopRange);
            return made;
        }();

        gui::PanelCurve out;
        out.node = row.node;
        out.channel = row.channel;
        out.loop = aModel.projectLoop;
        out.loopRange = aModel.projectLoopRange;
        out.color = row.color;
        out.channelRow = i;

        const core::Frame first = aScale.frameAt(aModel.plot.left());
        const core::Frame last = aScale.frameAt(aModel.plot.right());
        const int segments = std::max(1, curve.segmentCount());
        // One point per ~2px of plot width per segment (the drawing budget), floored so the
        // analytic easings keep their shape and capped so a long segment cannot ask for thousands
        // of samples. The budget is spent on the part of each segment the PLOT SHOWS (see
        // Curve::buildPolyline), which is what keeps a zoomed-in curve smooth.
        const int samples = std::clamp(aModel.plot.width() / (2 * segments), kMinSamplesPerSegment, kMaxSamplesPerSegment);

        QVector<QPointF> polyline;
        curve.buildPolyline(first, last, samples, polyline);
        out.points.reserve(polyline.size());
        for (const QPointF& sample : polyline) {
            out.points.append(
                QPointF(aScale.pixelXOfFrame(sample.x()), aScale.pixelYOfValue(float(sample.y())))
            );
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
                // THE MARKER STATE, in the same step as the geometry it belongs to, so
                // the three vectors cannot desync: the editor is asked about THIS key of
                // THIS curve - the node by identity, the channel's type and component,
                // and the frame just published - which is the same triple (node, channel,
                // frame) the drawing layer and the hit-test name a key by. Both answers
                // are the engine's own, unmodified: `keySelected` is
                // isMarkerSelected (an entry for this marker, or a whole-key entry that
                // covers it) and `keySiblingSelected` is hasSelectedSiblingMarker (an
                // entry naming a DIFFERENT component, or the key as a whole), which is
                // true even when this marker is selected too - see the header for the
                // composition that is deliberately left to the drawing layer.
                out.keySelected.append(aEditor.isMarkerSelected(*row.node, row.channel, frame));
                out.keySiblingSelected.append(aEditor.hasSelectedSiblingMarker(*row.node, row.channel, frame));
                // ...and the key's own PROPORTIONAL classification, in the same step.
                // The key is asked BY NAME (the channel's type and the frame just
                // published), which is the timeline's own identity for it; a key the
                // map does not answer for is not proportional. The model publishes the
                // flag unmodified - drawing a ring instead of a filled dot is the drawing
                // layer's decision, the same one the timeline's key pass makes.
                const core::TimeKey* publishedKey = core::TimeLine::usesChannelTracks(row.channel.type())
                    ? timeLine->channelKey(row.channel.type(), row.channel.component(), frame.get())
                    : timeLine->timeKey(row.channel.type(), frame.get());
                out.keyProportional.append(publishedKey && publishedKey->isProportional());
            }
        }

        // Every segment's tangent handles, in segment order: one pair per segment,
        // handle 0 leaving `from` and 1 arriving at `to`. The base is the key the
        // handle belongs to, so the drawn stem starts on the key it moves, and the
        // dot is the mapping the editor's own hit-test grabs through - a Custom
        // segment's STORED pair (handlePoint) or a DERIVED one (handlePointAt over
        // effectiveHandles, the same function handlePoint calls), so a stored pair
        // and a derived one cannot drift apart. `ghost` carries which kind it is:
        // the derived pair is not in the document, and the drawing layer draws it
        // as a smaller, solid dot of the channel's reduced-chroma hue (a drag on it
        // converts the key to Custom).
        //
        // THE KIND IS PER SIDE, never per segment. A Custom segment may derive one
        // side and store the other (the whole point of the per-side modes), so the
        // segment's two handles can be of DIFFERENT kinds. Asking isCustom() - "both
        // sides stored" - would publish a one-derived segment as two ghosts AND drop
        // its stored handle. Curve::derivesHandle is that per-side rule, and
        // effectiveHandles is the pair for both kinds (a stored side comes back
        // exactly as stored), so one mapping still serves every dot.
        //
        // THE SEGMENT'S CURVE IS THE FROM KEY'S PARAM, and that is the whole
        // existence test: a side is a handle only when evaluation of THIS segment
        // can read it. An analytic FROM param has no control points at all - its
        // `to` key's derived incoming mode is inert, because playback/drawing never
        // calls controlPoint for that segment - so publishing it would claim a
        // tangent no curve derives (the defect that showed an "automatic" handle on
        // a side whose curve was still Linear). The Auto preset makes the whole
        // segment AFTER a selected key Custom, so a real automatic side always
        // passes this gate.
        //
        // Published whether or not the pair lies inside the plot: the drawing layer
        // clips, and a handle off the plot's edge is still one the user can grab.
        //
        // Only for the curve the drawing layer INKS them on (see anyFocused above):
        // the pairs exist for the handle pass and its hover, both of which skip every
        // curve a focus excludes, so a pair built for another row would be a derived
        // tangent nobody reads - an empty `handles` is what the layer sees for those
        // rows. No focus means every row publishes, which is the layer's no-focus ink.
        if (!anyFocused || row.focused) {
            const int handleSegments = curve.segmentCount();
            for (int s = 0; s < handleSegments; ++s) {
                const ctrl::graph::Curve::Segment segment = curve.segment(s);
                if (!segment.from || !segment.to)
                    continue;
                if (segment.easing.type != util::Easing::Type_Custom)
                    continue; // an analytic segment has no tangent to publish

                // The effective pair: a stored side is its stored control point,
                // a derived side the one its neighbours produce. These are the dots
                // the editor's own hit-test grabs through.
                QPointF pair[2];
                if (!curve.effectiveHandles(segment, pair))
                    continue;

                const core::TimeKey* owners[2] = { segment.from, segment.to };
                const float ownerValues[2] = { segment.fromValue, segment.toValue };
                for (int h = 0; h < 2; ++h) {
                    // The kind is exactly what evaluation does for this side, from
                    // the one derivation query. A `true` here is a side that really
                    // derives (the pair holds its derived control point); a `false`
                    // is the document's stored one.
                    const bool derived = ctrl::graph::Curve::derivesHandle(segment, h);
                    gui::PanelHandle handle;
                    handle.segment = s;
                    handle.index = h;
                    handle.base = QPointF(
                        aScale.pixelXOfFrame(static_cast<double>(owners[h]->frame())),
                        static_cast<double>(aScale.pixelY(ownerValues[h]))
                    );
                    // Both mappings take the same pair; handlePoint() is the stored
                    // path and handlePointAt() the published dot, and the pair's
                    // stored side equals what handlePoint() would compute.
                    handle.dot = derived ? ctrl::graph::handlePointAt(aScale, segment, pair[h])
                                         : ctrl::graph::handlePoint(aScale, segment, h);
                    handle.radius = ctrl::graph::kHandleRadius;
                    handle.ghost = derived;
                    out.handles.append(handle);
                }
            }
        }

        // The chip that names this curve IN PLACE: at the plot's right edge, on the
        // value the curve LEAVES the window with - the last of the polyline
        // `points` above, so the chip is derived from the curve it names rather than
        // from a second evaluation of the curve at the edge. An empty box when the
        // chip would not fit inside the plot (see the header's CHIPS): a curve
        // leaving the window too near an edge, or a plot too narrow for the name,
        // publishes no chip rather than one in the margin or over another row.
        out.chipText = row.name;
        if (!out.points.isEmpty()) {
            const int width = aMetrics.horizontalAdvance(out.chipText) + 2 * kChipPadX;
            const int height = aMetrics.height();
            const double y = out.points.last().y();
            // Centre the box on the curve: round the CENTRE, not the top edge,
            // or an even-height box lands half a line off the value it names.
            const int top = qRound(y - (height - 1) / 2.0);
            const QRect chip(aModel.plot.right() + 1 - kChipRightGap - width, top, width, height);
            if (aModel.plot.contains(chip))
                out.chip = chip;
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

    if (aModel.channels.isEmpty()) {
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
        ctrl::GraphEditor& aEditor, const QSize& aViewport, const theme::Graph& aTheme, int aTopInset,
        GraphPanelHost aHost, const QFont& aMeasuredFont
    ) {
        GraphPanelModel model;

        // The floor this model is for: republished as the classification it is (see
        // GraphPanelHost). It places nothing - the panel's ONE control lives in the
        // dock header now.
        model.host = aHost;

        // The panel rect is the editor's own, whatever it is: the fullscreen
        // graph view over the animation. Nothing here hardcodes a size, and
        // nothing here writes back into the editor's scaler.
        model.panel = ctrl::GraphEditor::panelRect(aViewport);

        model.projectLoop = aEditor.isLooping();
        model.projectLoopRange = aEditor.frameRange();

        ctrl::graph::Scaler scale = aEditor.scale();
        scale.setRect(model.panel);

        model.plot = scale.plotRect();
        // THE RULER BAND IS ON THE TIMELINE'S ROW. The scaler reserves the band's 22
        // rows BELOW its top margin (plot.top() = panel.top() + kMargin + 22), and
        // the timeline's TimeRuler strip starts at its own page's top edge; both views
        // are pages of the same dock stack, so the band is published at the panel's
        // top row - the 4 margin rows stay BETWEEN the band's hairline and the plot
        // instead of pushing the ruler down by 4px (the measured divergence: the
        // timeline's strip was page.top()+9, the graph's band page.top()+13).
        //
        // THE OCCLUDED TOP (aTopInset): a widget floating over the panel covers the
        // viewport's first rows. The band may not begin above that line, but it may
        // NOT be pushed into the plot either - the plot's rows are the graph's, and
        // moving the band there would put curve ink through the ruler. So the band
        // stays only while its whole 22px slot is at or below the occluded line;
        // an occluder that reaches into the slot hides the strip for this frame
        // (rulerBandTop then names the plot's own top, where the panel's top content
        // may begin when there is no ruler). The plot itself never moves: it is the
        // editor's hit-test geometry too (see THE OCCLUDED TOP in the header).
        const int inset = std::max(0, aTopInset);
        const int bandTop = model.plot.top() - ctrl::graph::Scaler::kMargin - frameRuler::kBandHeight;
        if (!model.plot.isEmpty() && inset <= bandTop) {
            model.rulerBandTop = bandTop;
            // THE BAND SPANS THE PANEL, not the plot. The timeline's TimeRuler strip is
            // as wide as its viewport, and the shared frame mapping puts the graph's
            // frames on the timeline's own columns (graph_Scaler.h: THE SHARED WINDOW'S
            // MAPPING) - so the band must cover every column a frame tick can land on,
            // including the value column's top-left corner. A band as wide as the plot
            // left that corner bare and cut the ruler at the first frame the plot
            // showed, which is the divergence a tab switch made visible.
            model.rulerBand = QRect(model.panel.left(), bandTop, model.panel.width(), frameRuler::kBandHeight);
        } else if (!model.plot.isEmpty()) {
            model.rulerBandTop = model.plot.top();
        } else {
            model.rulerBandTop = model.plot.bottom() + 1;
        }
        // The column's width is the scaler's STABLE reserve for the value format
        // (see THE LABEL COLUMN in graph_Scaler.h) - the width the plot was
        // already inset by - so the column and the plot are one geometry, a
        // number can never need more room than the column has, and a gesture that
        // changes which label is widest cannot move either one.
        model.valueColumn = QRect(
            model.panel.left() + ctrl::graph::Scaler::kMargin, model.plot.top(), scale.axisLabelWidth(),
            model.plot.height()
        );

        // THE TWO SCROLL BARS, from the scaler's own layout (see the struct's SCROLL
        // BARS note) and the editor's own two contents: the project's frame range
        // (the horizontal bar's content) and the value extent of the SHOWN curves'
        // keys (the vertical one's) - the very range fitValueAxis fits, asked through
        // the editor so the fit and the bar cannot disagree. A value extent that does
        // not exist (no shown channel has a key) is passed on as an empty range, and
        // the layout reads that as "the thumb fills its track".
        //
        // An EMPTY plot publishes no bars at all: scrollBars() answers four empty
        // rects for a plot with no room, which is why this sits before the plot's own
        // guard as well as after it - the early return above cannot leave a stale bar
        // behind, there is none to leave.
        float valueContentLo = 0.0f;
        float valueContentHi = 0.0f;
        if (!aEditor.shownValueExtent(valueContentLo, valueContentHi))
            valueContentLo = valueContentHi = 0.0f;
        const util::Range frameContent = aEditor.frameRange();
        const ctrl::graph::Scaler::ScrollBars bars =
            scale.scrollBars(frameContent.min(), frameContent.max(), valueContentLo, valueContentHi);
        model.hTrack = bars.hTrack;
        model.hThumb = bars.hThumb;
        model.vTrack = bars.vTrack;
        model.vThumb = bars.vThumb;

        // The playhead belongs to the document, not to the geometry, so it is
        // read before the plot's own guards. NEGATIVE FRAMES ARE REAL FRAMES: a
        // project may start before 0, and the ruler seek reaches there like the
        // timeline's own, so the gate is "is a project attached", never the frame's
        // sign (currentFrame()'s -1 is only its no-project sentinel).
        //
        // Its NUMBER is written here, by the same formatter the ruler labels use and
        // with the same setting the timeline reads, so the pill and the label it
        // replaces name the frame identically.
        const core::TimeFormat format(aEditor.frameRange(), aEditor.fps());
        const core::Frame playhead = aEditor.currentFrame();
        if (aEditor.hasProject()) {
            model.playheadX = scale.pixelX(playhead);
            model.playheadText = format.frameToString(playhead.get(), aEditor.timeFormatType());
        }

        // The live box-select rectangle, straight from the gesture that owns it: the
        // editor publishes an EMPTY rect while no drag is in flight (and for a press
        // with no movement), which is exactly "draw nothing". Normalised and clipped
        // to the plot by the editor, so this is a copy of one geometry rather than a
        // second derivation of it.
        model.marquee = aEditor.marqueeRect();

        // The number of keys the live selection names - a DOCUMENT fact like the
        // playhead, so it is read before the plot's own guards: the panel publishes it
        // to the accessibility tree and draws nothing for it (see SELECTION COUNT).
        model.selectionCount = aEditor.selectionCount();

        if (model.plot.isEmpty()) {
            // Nothing to lay an axis on. The panel, the band, the playhead, the marquee
            // (the editor's own rect, empty unless a drag is live) and the reason are
            // still published: an empty panel must say why it is empty.
            model.rulerBandTop = model.plot.bottom() + 1;
            model.emptyState =
                graphPanelTr(QT_TRANSLATE_NOOP("GraphPanel", "No room for the graph in this viewport"));
            return model;
        }

        // Text is measured with the font the DRAWING HOST hands in: the model has no painter
        // by design, and a label box is advisory geometry the drawing layer puts its own
        // glyphs in. For the playhead pill that font is not advisory at all - it is the
        // timeline's own measurement (TimeRuler/TimeCursor call
        // QFontMetrics(aPainter.font())), so the pill's width and left edge come out equal
        // to TimeCursor's instead of 2px wider (QFont() is the application default font,
        // which the dock's stylesheet font size does not apply to).
        const QFontMetrics metrics{aMeasuredFont};

        buildFrameAxis(
            model, scale, metrics, aEditor.fps(), aEditor.timeFormatType(), aEditor.frameRange(),
            aEditor.activeRange(), aEditor.playWindow(),
            model.playheadX >= 0 ? aEditor.currentFrame().get() : std::numeric_limits<int>::min()
        );
        buildValueAxis(model, scale, metrics);
        // THE FRAME RANGE'S GUIDES are a DOCUMENT fact like the playhead (the project's own
        // range), so they are read whether or not the ruler band fits - they are plot-row,
        // panel-column geometry, not ruler ink. No project, no range, no guides.
        if (aEditor.hasProject())
            buildRangeGuides(model, scale, aEditor.frameRange(), aEditor.playWindow().active());
        buildChannels(model, aEditor, aTheme);
        buildCurves(model, aEditor, scale, metrics);
        buildEmptyState(model, aEditor);

        return model;
    }

} // namespace gui
