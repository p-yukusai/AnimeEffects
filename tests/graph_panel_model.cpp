// Contract test for the engine-neutral graph panel model: gui::buildGraphPanelModel
// (src/gui/GraphPanelModel.h/.cpp).
//
// The model is what the overlay (Dear ImGui now, the QPainter renderer until
// then) draws the graph panel from, so what is pinned below is the geometry a
// drawing layer would consume: the panel/plot/value-column rectangles, the two
// SHORT tick axes with their label rects, the curves with their keys, the channel
// list with its rows (grouped per layer and key type, each row carrying its
// channel hue from the one table both views read), the panel's one control (the
// channel button), the selection
// hull with its count, the playhead, and the empty state. Every assertion prints
// the value it measured.
//
// The oracle is the editor's own scaler (GraphEditor::scale(), geometrised with
// the same panelRect the model uses) and the editor's own curve set
// (GraphEditor::curves()) - never a re-statement of the model's arithmetic, which
// would agree with any bug that is in both.
//
// Coverage, in the order of the acceptance list:
//   1 consistency with the editor: panel == GraphEditor::panelRect(viewport),
//     plot inside the panel, value column left of and clear of the plot (at
//     1280x720 and at 320x200)
//   2 no grid: the tick axes are POSITION lists (static_assert on their types) and
//     every frame tick's TIER is a CLASSIFICATION of the same length and order
//     (static_assert too: an int per tick, no appearance),
//     no published rect spans the plot, every published frame tick is a frame the
//     ladder classifies and every frame the ladder classifies inside the plot has
//     its tick, the value ticks are exactly the published value labels' own
//     pixels, and every frame label box lies inside the RULER BAND above the plot
//     (the band is a section outside the plot, and no label reaches into the plot)
//   3 the ladder follows the scaler: a label's number is scale.frameAt(tick),
//     neighbouring label boxes keep a minimum gap, every published tier is one
//     of the three the ruler draws and is the ladder's own level for that tick's
//     frame (index-aligned with the positions), and every label box is the
//     TIMELINE'S slot - tick-centred, 6px per character, nudged inside the band -
//     at 1, 2 and 3 digits (the 1-digit case measured on a second window, 0..8,
//     because the fitted window's only one-digit numbers sit under the playhead
//     pill)
//   4 curves: exactly one per shown (target, channel) - the same set and the
//     same order as the editor's own curves() and as the channel list's rows -
//     each with >1 point, key rects clipped into the plot and equal to
//     graph::keyRect, the polyline's y at every key frame equal to
//     scale.pixelY(valueOf(key)), and a NAME CHIP whose box is derived from the
//     curve it names (its centre on the value the curve leaves the plot with) and
//     lies inside the plot
//   5 hidden channels: the row survives with visible == false and no curve
//   6 empty state: no targets -> a message and no curves
//   7 playhead: scale.pixelX(editor.currentFrame()) after moving the document's
//     frame, and -1 with no project
//   8 NO LEGEND, and the panel's one control: nothing publishes a swatch stack
//     (the legend is gone, not orphaned), the channel list still carries every
//     row whatever the viewport, a chip that cannot fit the plot publishes NO box
//     (an empty rect, never one in the margin), and a plot too small for the
//     "N channels" button publishes no button rect - a control that cannot be
//     drawn must not be hit
//   9 the polyline is published unclipped and unreduced - point for point the
//     sampled curve mapped through the scaler, out-of-plot samples included, so
//     the drawing layer's own clip is the only clip (the header's CURVES)
//  10 tangent handles: one PanelHandle per side that HAS one - a Custom
//     segment's STORED side (ghost == false) matching the editor's own grab
//     mapping, ctrl::graph::handlePoint, and a DERIVED side (ghost == true) whose
//     x IS the third/two thirds of the segment's span and whose dot is the same
//     mapping (ctrl::graph::handlePointAt). A segment's curve is its FROM key's
//     param, so an analytic segment publishes nothing at all (a derived incoming
//     mode on the TO key is inert there). The Auto preset applies to the segment
//     AFTER the key: applying it to a key writes that key's type and outgoing
//     side plus the next key's incoming side, leaving the previous key's param
//     byte-identical and the arriving segment with no ghost - all in one undo
//     step. The flags are gated against EVALUATION, not trusted: for every side
//     of a Custom segment the stored control point is perturbed and the segment
//     is re-evaluated, and a derived side must ignore it while a stored side
//     moves.
//  11 the ruler band as a section ABOVE the plot (its own reserved rows, the
//     numbers inside it, the playhead crossing from its pill column into the
//     plot), the occluded top that can only hide it, and the live marquee
//  12 the label column is a STABLE, FORMAT-DERIVED reserve: its width is the
//     widest label the value format can write (plus the box inset and text gap,
//     clamped into the scaler's floor and ceiling), so it is the SAME width for
//     every value window at several viewports - the plot rect stays
//     byte-identical across six windows, across a value-axis wheel step and a
//     value pan, and a 40px FRAME pan moves a frame's pixel by exactly 40 - while
//     every published label's box is still at least as wide as its own measured
//     text (checked with a wide value, 12345.6 -> "1.235e+04", a small negative
//     and the float extremes)
//  13 the key DOT is the marker's own geometry: circles of radius
//     ctrl::graph::kKeyDotRadius (== the TIMELINE's ctrl::keyRadius) centred on
//     each key, published beside the key rects, with the hit radius a separate,
//     larger constant
//  14 the SELECTION HULL is the editor's own outline in the model's own pixels -
//     EMPTY with nothing selected (and then no count label either) and
//     NON-EMPTY with a key selected, each point the same (frame, value) the editor
//     stated mapped through the same scaler the curves use - with the count the
//     editor reports and the label that writes it
//  15 the marker state: which marker is selected, and whether the key it belongs
//     to has a selected sibling - both raw answers from the editor's own queries
//  16 the ACTIVE-range recession (one inside/outside flag per tick and per frame
//     label, against the project range and against an active play window) and the
//     play window's overlay geometry (band, both stems, both arms, and the one
//     centred merged pill of a window narrower than its labels)
//  17 the SCROLL BAR'S PILL is one geometry: ctrl::graph::scrollBar::pill's table
//     of (lane, run, orientation) cases, the pill gui::AppStyle's CC_ScrollBar
//     PAINTS for them (read off a transparent QImage), and the pill the panel's
//     published lane/run rects produce - the same function both painters use
//  18 the FRAME RANGE'S GUIDES: the project's first and last frame as two COLUMNS
//     through the same scaler the ticks and the playhead use - PANEL columns (the
//     first frame of the range sits inside the value column and is still published),
//     ONE column when both ends round onto the same pixel, the recede flag following
//     the play window without moving either column, and none at all without a project
// plus a read-only probe: building a model must not move the editor's scaler,
// its targets, the project's dirty flag or the document's keys.
//
// Headless: QApplication (translations + the font database the label metrics
// need), no widgets shown, no GL, no .anie fixture.
#include <QApplication>
#include <QCommonStyle>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QScrollBar>
#include <QString>
#include <QStringList>
#include <QStyleOptionSlider>
#include <QVector2D>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <type_traits>

#include "XCAssert.h"
#include "cmnd/Stack.h"
#include "core/Animator.h"
#include "core/AutoEasing.h"
#include "core/Frame.h"
#include "core/MoveKey.h"
#include "core/ObjectNode.h"
#include "core/OpaKey.h"
#include "core/Project.h"
#include "core/RotateKey.h"
#include "core/TimeKeyType.h"
#include "core/TimeFormat.h"
#include "core/TimeLine.h"
#include "ctrl/GraphEditor.h"
#include "ctrl/TimeLineEditor.h"
#include "ctrl/UILogger.h"
#include "ctrl/graph/graph_Channel.h"
#include "ctrl/graph/graph_Curve.h"
#include "ctrl/graph/graph_Geometry.h"
#include "ctrl/graph/graph_Scaler.h"
// Section 17 paints the app's own scroll bar: gui::AppStyle's CC_ScrollBar is what
// draws the timeline's bar and the panel's lanes, and AppStyle.cpp is compiled into
// this target (see tests/CMakeLists.txt) so the measurement is of the real painter.
#include "ctrl/graph/graph_ScrollBarGeometry.h"
#include "gui/AppStyle.h"
// The timeline's own key marker (ctrl::keyRadius = 3.0 * ctrl::radiiFactor), the
// constant the panel's key dot is derived from - block [13] pins the two
// together instead of trusting a comment. Header-only: the constants are
// constexpr, so nothing from the timeline's renderer is linked.
#include "ctrl/time/time_Renderer.h"
#include "gui/GraphPanelModel.h"
#include "gui/theme/Colors.h"
#include "gui/theme/Graph.h"
#include "util/Range.h"

// core::Project.cpp references the GUI AudioPlaybackWidget::deserialize only to
// load a .aemus sidecar (this test never has one), and the object-tree symbols
// come in with ctrl's widgets. Stub them, exactly as graph_curve / graph_drag /
// creation_select_keys do, so the link succeeds without the whole gui library.
#include "gui/AudioPlaybackWidget.h"
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

namespace {

class HarnessAssertHandler: public XCAssertHandler {
public:
    void failure() const override { std::abort(); }
};
class HarnessErrorHandler: public XCErrorHandler {
public:
    void critical(const QString&, const QString&, const QString&) const override { std::abort(); }
};
HarnessAssertHandler gAssertHandler;
HarnessErrorHandler gErrorHandler;

int gFailures = 0;

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

// The assertions count and continue: a run has to print every measurement it
// took, not stop at the first one that failed.
#define CHECK(cond)                                                                                    \
    do {                                                                                               \
        if (!(cond)) {                                                                                 \
            std::printf("  FAIL %d: %s\n", __LINE__, #cond);                                           \
            ++gFailures;                                                                               \
        }                                                                                              \
    } while (false)

namespace core {
// Minimal ObjectNode subclass owning a real TimeLine (ObjectNode::timeLine() is
// virtual and null by default), with a name so the channel list's rows (and the
// chips that name them in place) are distinct.
class TestNode: public ObjectNode {
    TimeLine mTimeLine;
    QString mName;

public:
    ObjectType type() const override { return ObjectType_Layer; }
    void setName(const QString& aName) override { mName = aName; }
    const QString& name() const override { return mName; }
    float initialDepth() const override { return 0; }
    void setVisibility(bool) override {}
    bool isVisible() const override { return true; }
    void setSlimDown(bool) override {}
    bool isSlimmedDown() const override { return false; }
    bool canHoldChild() const override { return false; }
    void setInitialRect(const QRect&) override {}
    QRect initialRect() const override { return QRect(); }
    bool serialize(Serializer&) const override { return true; }
    bool deserialize(Deserializer&) override { return true; }
    TimeLine* timeLine() override { return &mTimeLine; }
    const TimeLine* timeLine() const override { return &mTimeLine; }
};
} // namespace core

namespace {

// The document's clock, with a frame the test moves (core::Animator is an
// interface; the application's implementation is in the gui layer).
class TestAnimator: public core::Animator {
    core::Frame mFrame = core::Frame(1);

public:
    void setFrame(core::Frame aFrame) { mFrame = aFrame; }
    core::Frame currentFrame() const override { return mFrame; }
    void stop() override {}
    void suspend() override {}
    void resume() override {}
    bool isSuspended() const override { return false; }
};

class NullLogger: public ctrl::UILogger {
public:
    void pushLog(const QString&, ctrl::UILogType) override {}
};

using Pair = QPair<core::ObjectNode*, ctrl::graph::Channel>;

// Qt 6 containers report qsizetype; printf's %d (and the arithmetic below) want
// an int, and a mismatch there is undefined behaviour, not a formatting quirk.
template <class T>
int isz(const T& aContainer) {
    return int(aContainer.size());
}

bool samePair(const Pair& aLhs, const core::ObjectNode* aNode, const ctrl::graph::Channel& aChannel) {
    return aLhs.first == aNode && aLhs.second == aChannel;
}

// The (node, channel) pairs the editor is expected to show: the same walk
// GraphEditor::buildEntries() makes, expressed with the public Channel table.
QVector<Pair> expectedPairs(const QVector<core::ObjectNode*>& aTargets) {
    QVector<Pair> pairs;
    for (core::ObjectNode* node : aTargets) {
        if (!node)
            continue;
        const core::TimeLine* timeLine = node->timeLine();
        if (!timeLine)
            continue;
        for (core::TimeKeyType type : ctrl::graph::Channel::graphableTypes()) {
            if (timeLine->map(type).isEmpty())
                continue;
            for (const ctrl::graph::Channel& channel : ctrl::graph::Channel::channelsOf(type)) {
                pairs.append(Pair(node, channel));
            }
        }
    }
    return pairs;
}

QString channelText(const ctrl::graph::Channel& aChannel) {
    return QStringLiteral("%1/%2").arg(int(aChannel.type())).arg(aChannel.component());
}

// The value axis' number format, the renderer's own valueText(): 'g' with four
// significant digits. The model reads it from ctrl::graph::Scaler::valueAxisText
// (the function the column's width is measured from), so the test restates it
// here as the documented convention the published label text has to match.
QString valueTextOf(float aValue) {
    return QString::number(double(aValue), 'g', 4);
}

// The label column's reserve, restated from THE LABEL COLUMN in graph_Scaler.h:
// the widest label the value FORMAT can write - one candidate per shape 'g'/4
// can emit, both signs - plus the box's right inset and the text gap, clamped
// into the column's floor and ceiling. It is a function of the format and the
// font ALONE: it deliberately does not take a value window, because a reserve
// that read one would move the plot's left edge whenever a gesture changed which
// label is widest. Stated here so a column that stopped measuring (or that
// measured the window again) fails block [12].
int reserveAxisLabelWidth(const QFontMetrics& aMetrics) {
    const float extremes[] = {
        -std::numeric_limits<float>::max(),        // "-3.403e+38", the exponent form's widest
        -0.0009999f,                               // "-0.0009999", the plain form's widest
        -std::numeric_limits<float>::denorm_min(), // "-1.401e-45"
        std::numeric_limits<float>::max(),         // "3.403e+38"
        std::numeric_limits<float>::denorm_min(),  // "1.401e-45"
        12345.6f,                                  // "1.235e+04"
        100000.0f,                                 // "1e+05"
        -100000.0f,                                // "-1e+05"
        -1234.0f,                                  // "-1234"
    };
    int widest = 0;
    for (const float value : extremes)
        widest = std::max(widest, aMetrics.horizontalAdvance(valueTextOf(value)));
    return std::clamp(
        widest + ctrl::graph::Scaler::kAxisLabelPad + ctrl::graph::Scaler::kAxisLabelTextPad,
        ctrl::graph::Scaler::kAxisLabelWidth, ctrl::graph::Scaler::kMaxAxisLabelWidth
    );
}

// Every rect the model publishes, so "no grid" can be checked mechanically.
QVector<QRect> publishedRects(const gui::GraphPanelModel& aModel) {
    QVector<QRect> rects;
    for (const gui::PanelLabel& label : aModel.frameLabels)
        rects.append(label.rect);
    for (const gui::PanelLabel& label : aModel.valueLabels)
        rects.append(label.rect);
    for (const gui::PanelCurve& curve : aModel.curves) {
        for (const QRect& key : curve.keys)
            rects.append(key);
        // A curve's NAME CHIP: the panel has no swatch stack to publish any more (the
        // legend is gone), and this is the one box the model lays out per channel for
        // the drawing layer to write a name in.
        rects.append(curve.chip);
    }
    rects.append(aModel.marquee);
    return rects;
}

// The y of the polyline at a frame: the sample whose x is the key's own mapped
// x. Curve::buildPolyline emits every touched segment's endpoints, so a key
// inside the window is always a sample, and the nearest-x search is exact.
bool polylineYAt(const gui::PanelCurve& aCurve, double aX, double& aOutY, double& aOutDistance) {
    bool found = false;
    double best = std::numeric_limits<double>::max();
    for (const QPointF& point : aCurve.points) {
        const double distance = std::fabs(point.x() - aX);
        if (distance <= best) {
            best = distance;
            aOutY = point.y();
            found = true;
        }
    }
    aOutDistance = best;
    return found;
}

// ---------------------------------------------------------------------------
// THE SCROLL BAR'S PILL (section 17): the case table, the app style's paint, and
// the pixel reads that hold one to the other.
// ---------------------------------------------------------------------------

// One row of the geometry table: the rects both painters hand in, and the pill BY
// HAND - written out as the lane's offset from its own leading edge, the run's offset
// and its length - never re-derived from ctrl::graph::scrollBar::pill(), or the table
// would agree with any bug the function has.
struct PillCase {
    const char* what;
    QRect lane;
    QRect run;
    Qt::Orientation orientation;
    QRect expected;
};

// A first..last range along ONE axis; empty when nothing matched.
struct Span {
    int lo = 0;
    int hi = -1;
    bool isEmpty() const { return lo > hi; }
};

// The extent of the pixels of aProbe that satisfy aPredicate, along the axis
// aHorizontal selects (columns when true, rows when false). Every probe below is one
// pixel wide across the other axis, so this reports ONE measured span.
template <typename Predicate>
Span pixelSpan(const QImage& aImage, const QRect& aProbe, bool aHorizontal, Predicate aPredicate) {
    Span span;
    span.lo = std::numeric_limits<int>::max();
    span.hi = std::numeric_limits<int>::min();
    const QRect probe = aProbe.intersected(aImage.rect());
    for (int y = probe.top(); y <= probe.bottom(); ++y) {
        for (int x = probe.left(); x <= probe.right(); ++x) {
            if (!aPredicate(x, y))
                continue;
            const int at = aHorizontal ? x : y;
            span.lo = std::min(span.lo, at);
            span.hi = std::max(span.hi, at);
        }
    }
    return span.isEmpty() ? Span{} : span;
}

// gui::AppStyle's CC_ScrollBar painted into a TRANSPARENT image the size of the lane:
// the app's own scroll-bar painter with nothing behind it, so every inked pixel is the
// pill. aLane must start at (0,0) - the image's origin is the measurement's origin.
// aOutSlider receives the style's own SC_ScrollBarSlider rect, i.e. the run AppStyle
// itself paints the pill across, so the expectation below and the paint cannot be
// about two different runs.
QImage paintAppScrollBar(gui::AppStyle& aStyle, QScrollBar& aBar, const QRect& aLane, QRect& aOutSlider) {
    QStyleOptionSlider opt;
    opt.initFrom(&aBar);
    opt.rect = aLane;
    opt.orientation = aBar.orientation();
    opt.minimum = aBar.minimum();
    opt.maximum = aBar.maximum();
    opt.pageStep = aBar.pageStep();
    opt.sliderPosition = aBar.sliderPosition();
    opt.sliderValue = aBar.sliderPosition();
    aOutSlider = aStyle.subControlRect(QStyle::CC_ScrollBar, &opt, QStyle::SC_ScrollBarSlider, &aBar);

    QImage image(aLane.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    aStyle.drawComplexControl(QStyle::CC_ScrollBar, &opt, &painter, &aBar);
    painter.end();
    return image;
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    TestAnimator animator;
    core::Project project("graph_panel_model.anie", animator, nullptr);

    // The ladder is a function of the fps and the frame range, and it is seeded
    // when the editor is constructed - so the attribute goes in first.
    project.attribute().setFps(24);
    project.attribute().setFrameRange(util::Range(1, 120));

    // The keys go onto a stack this test owns, so the project's own stack stays
    // empty and the read-only probe below is measurable.
    cmnd::Stack setup;

    core::TestNode nodeA;
    nodeA.setName(QStringLiteral("A"));
    core::TestNode nodeB;
    nodeB.setName(QStringLiteral("B"));

    // A: Rotate (1 channel, 0 -> 90 deg) and Move (2 channels: x 0 -> 100, y 10 -> -30).
    core::RotateKey* rotate0 = new core::RotateKey();
    rotate0->setRotate(0.0f);
    setup.push(nodeA.timeLine()->createPusher(core::TimeKeyType_Rotate, 0, rotate0));
    core::RotateKey* rotate1 = new core::RotateKey();
    rotate1->setRotate(90.0f);
    setup.push(nodeA.timeLine()->createPusher(core::TimeKeyType_Rotate, 24, rotate1));

    core::MoveKey* move0 = new core::MoveKey();
    move0->setPos(QVector2D(0.0f, 10.0f));
    setup.push(nodeA.timeLine()->createPusher(core::TimeKeyType_Move, 0, move0));
    core::MoveKey* move1 = new core::MoveKey();
    move1->setPos(QVector2D(100.0f, -30.0f));
    setup.push(nodeA.timeLine()->createPusher(core::TimeKeyType_Move, 20, move1));

    // B: Opa (1 channel, 0.25 -> 0.75) - a second target, so "every channel of
    // every target" has something to be about.
    core::OpaKey* opa0 = new core::OpaKey();
    opa0->setOpacity(0.25f);
    setup.push(nodeB.timeLine()->createPusher(core::TimeKeyType_Opa, 6, opa0));
    core::OpaKey* opa1 = new core::OpaKey();
    opa1->setOpacity(0.75f);
    setup.push(nodeB.timeLine()->createPusher(core::TimeKeyType_Opa, 30, opa1));

    // C: a THREE-key Move channel whose outer keys are the ordinary Linear
    // default. It is not an editor target until the Auto-publication block asks
    // for it, so it cannot disturb any geometry measured below. The middle key
    // is the one the Auto action selects.
    core::TestNode nodeC;
    nodeC.setName(QStringLiteral("C"));
    core::MoveKey* moveC0 = new core::MoveKey();
    moveC0->setPos(QVector2D(0.0f, 0.0f));
    setup.push(nodeC.timeLine()->createPusher(core::TimeKeyType_Move, 0, moveC0));
    core::MoveKey* moveC1 = new core::MoveKey();
    moveC1->setPos(QVector2D(100.0f, 0.0f));
    setup.push(nodeC.timeLine()->createPusher(core::TimeKeyType_Move, 10, moveC1));
    core::MoveKey* moveC2 = new core::MoveKey();
    moveC2->setPos(QVector2D(40.0f, 0.0f));
    setup.push(nodeC.timeLine()->createPusher(core::TimeKeyType_Move, 20, moveC2));

    NullLogger logger;
    ctrl::GraphEditor editor(project, logger);
    // The show the whole file rides on is the surviving path the application uses:
    // the editor has no shown/hidden state any more (the dock's Graph page IS the
    // panel while its tab is current), and the fit that fills the value window is
    // one of the things setTargets() does - the application hands the panel its
    // targets when the document opens and whenever the tree selection changes, so
    // every model built below measures a FITTED editor. Its POST-CONDITION is
    // asserted right here: the window must leave the scaler's seed 0..1, which is
    // what a hand-in that did not take used to make a run fail loudly for.
    editor.setTargets({ &nodeA, &nodeB });
    CHECK(editor.scale().valueMin() != 0.0f || editor.scale().valueMax() != 1.0f);

    const QVector<core::ObjectNode*> targets = { &nodeA, &nodeB };
    const QVector<Pair> pairs = expectedPairs(targets);

    const QSize viewport(1280, 720);
    const theme::Graph palette = theme::Graph::from(theme::Colors::current());

    // THE RULER TOKENS ARE THE TIMELINE'S, token for token: the graph's band, hairline,
    // number ink and playhead-pill ink read the same palette cells the timeline's
    // TimeLineEditorWidget publishes (theme::TimeLine maps its headerBackgroundColor to
    // Colors::base, rulerLineColor to Colors::hairline and headerContentColor to
    // Colors::text - see src/gui/theme/TimeLine.cpp). Measured BEFORE the band's
    // geometry is checked, so a palette drift fails here rather than showing up as a
    // pixel difference between the two tabs. (The timeline theme's own class is not
    // linked into this target; its source cells are the contract.)
    {
        const theme::Colors& colors = theme::Colors::current();
        std::printf(
            "[0] tokens: band=%s line=%s numbers=%s pill=%s  timeline cells base=%s hairline=%s text=%s\n",
            qUtf8Printable(palette.rulerBand().name(QColor::HexRgb)),
            qUtf8Printable(palette.rulerLine().name(QColor::HexRgb)),
            qUtf8Printable(palette.axisText().name(QColor::HexRgb)),
            qUtf8Printable(palette.playheadText().name(QColor::HexRgb)),
            qUtf8Printable(colors.base.name(QColor::HexRgb)),
            qUtf8Printable(colors.hairline.name(QColor::HexRgb)),
            qUtf8Printable(colors.text.name(QColor::HexRgb))
        );
        CHECK(palette.rulerBand() == colors.base);       // TimeLine::headerBackgroundColor
        CHECK(palette.rulerLine() == colors.hairline);   // TimeLine::rulerLineColor
        CHECK(palette.axisText() == colors.text);        // TimeLine::headerContentColor
        // The pill's number is DARK on its accentBright fill - gui::TimeCursor's
        // own ink, measured on the running app: the ruler's text token there is
        // near-white and invisible on the violet (see the parity round).
        CHECK(palette.playheadText() == colors.accentBrightText);
    }

    // The oracle scaler: the editor's own axes, geometrised with the same panel
    // rect the model derives - built by the test, never read out of the model.
    const QRect panel = ctrl::GraphEditor::panelRect(viewport);
    ctrl::graph::Scaler scale = editor.scale();
    scale.setRect(panel);

    std::printf("viewport %dx%d  panel (%d,%d,%dx%d)  plot (%d,%d,%dx%d)\n", viewport.width(), viewport.height(),
                panel.x(), panel.y(), panel.width(), panel.height(), scale.plotRect().x(), scale.plotRect().y(),
                scale.plotRect().width(), scale.plotRect().height());
    std::printf("expected (target, channel) pairs: %d  [", isz(pairs));
    for (const Pair& pair : pairs)
        std::printf("%s%c", qUtf8Printable(channelText(pair.second)), &pair == &pairs.back() ? ']' : ' ');
    std::printf("\n");

    // -------------------------------------------------------------------------
    // 1. consistency with the editor
    gui::GraphPanelModel model = gui::buildGraphPanelModel(editor, viewport, palette);
    {
        std::printf("[1] panel (%d,%d,%dx%d) == panelRect(viewport): %d  plot (%d,%d,%dx%d)  valueColumn (%d,%d,%dx%d)\n",
                    model.panel.x(), model.panel.y(), model.panel.width(), model.panel.height(),
                    int(model.panel == ctrl::GraphEditor::panelRect(viewport)), model.plot.x(), model.plot.y(),
                    model.plot.width(), model.plot.height(), model.valueColumn.x(), model.valueColumn.y(),
                    model.valueColumn.width(), model.valueColumn.height());

        CHECK(model.panel == ctrl::GraphEditor::panelRect(viewport));
        CHECK(!model.plot.isEmpty());
        CHECK(model.panel.contains(model.plot));
        CHECK(model.valueColumn.right() < model.plot.left());
        CHECK(!model.valueColumn.intersects(model.plot));
        // The plot is inset by the MEASURED label column (graph_Scaler.h's THE
        // LABEL COLUMN), so this is the old statement with the measurement in
        // place of the fixed reserve - and the reserve is now its floor. The
        // oracle scaler is the test's own copy of the editor's axes, so the two
        // independent derivations of the column have to agree.
        CHECK(model.plot.left() == model.panel.left() + ctrl::graph::Scaler::kMargin + model.valueColumn.width());
        // THE ONLY RECT THE BAND MAY MOVE IS ITS OWN: the plot is the editor's own
        // scaler rect (the hit-test geometry), byte for byte, whatever the band's rows.
        CHECK(model.plot == scale.plotRect());
        CHECK(model.valueColumn.width() == scale.axisLabelWidth());
        CHECK(ctrl::graph::Scaler::kAxisLabelWidth <= model.valueColumn.width());
        CHECK(model.valueColumn.width() <= ctrl::graph::Scaler::kMaxAxisLabelWidth);
        // THE RULER IS A SECTION ABOVE THE GRAPH. The plot's top is the panel's
        // margin PLUS the band's 22 reserved rows, and its height loses exactly
        // those rows: the scaler's plot rect is the shared geometry, so every axis,
        // every curve point and the editor's hit-test start below the ruler.
        CHECK(model.plot.top()
              == model.panel.top() + ctrl::graph::Scaler::kMargin
                  + ctrl::graph::Scaler::kRulerBandHeight);
        // ...and its HEIGHT loses the top margin, the ruler band and the lane the scroll
        // bar sits in at the bottom edge (see THE SCROLL BARS' LANES in
        // graph_Scaler.h: the plot's right and bottom inset IS the app's 12px lane - no
        // gap, no extra margin - so the bar has a strip that is outside the plot and
        // flush with the panel's own edges).
        CHECK(model.plot.height()
              == model.panel.height() - ctrl::graph::Scaler::kMargin
                  - ctrl::graph::Scaler::kRulerBandHeight - ctrl::graph::Scaler::kScrollBarReserve);
        CHECK(model.plot.width()
              == model.panel.width() - ctrl::graph::Scaler::kMargin - model.valueColumn.width()
                  - ctrl::graph::Scaler::kScrollBarReserve);
        CHECK(model.plot.right()
              == model.panel.right() - ctrl::graph::Scaler::kScrollBarReserve);
        CHECK(model.plot.bottom()
              == model.panel.bottom() - ctrl::graph::Scaler::kScrollBarReserve);
        // The band is on the TIMELINE'S ROW: the timeline's TimeRuler strip starts at
        // its page's top edge, and both views are pages of the same dock stack, so the
        // band must start at the panel's top row - not at the scaler's top margin
        // (measured before this rule: timeline strip page.top()+9, graph band
        // page.top()+13). The scaler still reserves the band's 22 rows, but the
        // kMargin rows now sit BETWEEN the band's hairline and the plot, so the plot
        // (the editor's hit geometry) does not move.
        CHECK(model.rulerBand.top() == model.panel.top());
        // THE BAND SPANS THE PANEL: the shared frame mapping puts frames on the
        // timeline's own columns, so the ruler must cover all of them - including the
        // value column's top-left corner, which a plot-wide band left bare.
        CHECK(model.rulerBand.left() == model.panel.left());
        CHECK(model.rulerBand.width() == model.panel.width());
        CHECK(model.rulerBand.right() == model.panel.right());
        CHECK(model.rulerBand.bottom() + 1 + ctrl::graph::Scaler::kMargin == model.plot.top());
        CHECK(model.rulerBand.bottom() + 1 < model.plot.top());
        CHECK(!model.plot.intersects(model.rulerBand));

        // The small viewport, same invariants. What happens to the plot at this
        // size is reported rather than assumed: at 320x200 it is still non-empty,
        // and the curves survive (below).
        const QSize small(320, 200);
        const QRect smallPanel = ctrl::GraphEditor::panelRect(small);
        const gui::GraphPanelModel smallModel = gui::buildGraphPanelModel(editor, small, palette);
        std::printf("[1] small viewport %dx%d -> panel (%d,%d,%dx%d)  plot (%d,%d,%dx%d) empty=%d  curves=%d  "
                    "frameTicks=%d  frameLabels=%d\n",
                    small.width(), small.height(), smallModel.panel.x(), smallModel.panel.y(), smallModel.panel.width(),
                    smallModel.panel.height(), smallModel.plot.x(), smallModel.plot.y(), smallModel.plot.width(),
                    smallModel.plot.height(), int(smallModel.plot.isEmpty()), isz(smallModel.curves),
                    isz(smallModel.frameTickX), isz(smallModel.frameLabels));

        CHECK(smallModel.panel == smallPanel);
        CHECK(!smallModel.plot.isEmpty());
        CHECK(smallPanel.contains(smallModel.plot));
        CHECK(smallModel.valueColumn.right() < smallModel.plot.left());
        CHECK(!smallModel.valueColumn.intersects(smallModel.plot));
        CHECK(smallModel.curves.size() == model.curves.size());
        // The tier vector is index-aligned with the positions at every viewport: both
        // are appended in one step (buildFrameAxis), so this is a structural invariant
        // of the model rather than a property of this window.
        CHECK(smallModel.frameTickLevel.size() == smallModel.frameTickX.size());

        // A viewport too small for the label column and the margins has no plot
        // at all: the model says so instead of publishing geometry it cannot back.
        const QSize degenerate(40, 20);
        const gui::GraphPanelModel degenerateModel = gui::buildGraphPanelModel(editor, degenerate, palette);
        std::printf("[1] degenerate viewport %dx%d -> plot (%d,%d,%dx%d) empty=%d  emptyState=\"%s\"\n",
                    degenerate.width(), degenerate.height(), degenerateModel.plot.x(), degenerateModel.plot.y(),
                    degenerateModel.plot.width(), degenerateModel.plot.height(), int(degenerateModel.plot.isEmpty()),
                    qUtf8Printable(degenerateModel.emptyState));
        CHECK(degenerateModel.plot.isEmpty());
        CHECK(!degenerateModel.emptyState.isEmpty());
        CHECK(degenerateModel.curves.isEmpty());
    }

    // -------------------------------------------------------------------------
    // 2. no grid: the axes are position lists, and nothing published spans the plot
    {
        // The structural half: a full-height or full-width line cannot be
        // expressed in these members at all - they are ints, not rects.
        static_assert(std::is_same<decltype(gui::GraphPanelModel::frameTickX), QVector<int> >::value,
                      "frameTickX must be tick POSITIONS (ints): the model publishes no line geometry");
        static_assert(std::is_same<decltype(gui::GraphPanelModel::valueTickY), QVector<int> >::value,
                      "valueTickY must be tick POSITIONS (ints): the model publishes no line geometry");
        // The tier vector is the same KIND of statement about a tick as the position
        // vector is: an int per tick. It CLASSIFIES the tick - the ladder's own
        // ctrl::time::Scaler::TickLevel as its value - and it is not appearance: no
        // colour, no length, nothing a drawing layer would have to obey. The ladder
        // check in block 3 pins that every value is one of the three drawn tiers and
        // that the vector is index-aligned with frameTickX.
        static_assert(std::is_same<decltype(gui::GraphPanelModel::frameTickLevel), QVector<int> >::value,
                      "frameTickLevel must be a per-tick CLASSIFICATION (ints): the model publishes no appearance");

        const QVector<QRect> rects = publishedRects(model);
        int spanning = 0;
        for (const QRect& rect : rects) {
            if (rect.height() >= model.plot.height() || rect.width() >= model.plot.width())
                ++spanning;
        }
        std::printf("[2] frameTickX=%d valueTickY=%d published rects=%d spanning the plot=%d\n", isz(model.frameTickX),
                    isz(model.valueTickY), isz(rects), spanning);

        CHECK(!model.frameTickX.isEmpty());
        CHECK(!model.valueTickY.isEmpty());
        CHECK(!rects.isEmpty());
        CHECK(spanning == 0);

        // THE FRAME NUMBERS BELONG TO THE RULER, NOT THE GRAPH: their boxes are in
        // the band above the plot, and none of them may reach into the plot's rows
        // (the model's own section boundary).
        int outside = 0;
        int inPlotRows = 0;
        for (const gui::PanelLabel& label : model.frameLabels) {
            if (!model.rulerBand.contains(label.rect))
                ++outside;
            if (label.rect.intersects(model.plot))
                ++inPlotRows;
        }
        std::printf("[2] frameLabels=%d  outside the ruler band=%d  reaching into the plot=%d\n",
                    isz(model.frameLabels), outside, inPlotRows);
        CHECK(!model.frameLabels.isEmpty());
        CHECK(outside == 0);
        CHECK(inPlotRows == 0);

        int valuesOutside = 0;
        for (const gui::PanelLabel& label : model.valueLabels) {
            if (!model.valueColumn.contains(label.rect))
                ++valuesOutside;
        }
        std::printf("[2] valueLabels=%d  outside the value column=%d  first=\"%s\" at x=%d\n", isz(model.valueLabels),
                    valuesOutside, model.valueLabels.isEmpty() ? "" : qUtf8Printable(model.valueLabels.first().text),
                    model.valueLabels.isEmpty() ? -1 : model.valueLabels.first().rect.x());
        CHECK(!model.valueLabels.isEmpty());
        CHECK(valuesOutside == 0);
    }

    // -------------------------------------------------------------------------
    // 3. the ladder follows the scaler
    {
        // THE ONE FORMATTER the whole frame axis writes with: the test's own
        // core::TimeFormat, built from the EDITOR's frame range and rate, so every
        // label's text is checked against
        // core::TimeFormat::frameToString(frame, editor.timeFormatType()) for the
        // frame(s) the label publishes - never against the model's own string.
        const core::TimeFormat format(editor.frameRange(), editor.fps());
        const core::TimeFormatType formatType = editor.timeFormatType();
        const int numberReserve = gui::frameRuler::kNumberCharWidth;

        int sampled = 0;
        int wrongText = 0;
        int noFrame = 0;
        int noTick = 0;
        int wrongBoxWidth = 0;
        int wrongBoxLeft = 0;
        int timelineBoxes[4] = {0, 0, 0, 0}; // 1..3 digits verified as the timeline's slot
        int minGap = std::numeric_limits<int>::max();
        int previousRight = std::numeric_limits<int>::min();
        bool anyPrevious = false;

        for (const gui::PanelLabel& label : model.frameLabels) {
            ++sampled;
            // A merged boundary label names TWO frames ("first-last"); every other
            // frame label names exactly one, and that frame is published beside its
            // text in PanelLabel::frame.
            const bool merged = label.endFrame >= 0;
            const QString expected =
                merged ? format.frameToString(label.frame, formatType) + QString::fromUtf8("\u2013") +
                             format.frameToString(label.endFrame, formatType)
                       : format.frameToString(label.frame, formatType);
            if (!merged && label.frame < 0)
                ++noFrame;
            if (label.text != expected)
                ++wrongText;

            // The frame(s) the text names own a tick: an ordinary number's box contains
            // the tick of its own frame (the box is centred on it and nudged inside the
            // plot, so clamping can move the centre but never off the tick), and the
            // merged label's two boundary frames each have their tick somewhere.
            const auto tickInsideBox = [&model, &scale, &label](int aFrame) {
                for (const int x : model.frameTickX) {
                    if (x >= label.rect.left() && x <= label.rect.right() && scale.frameAt(x).get() == aFrame)
                        return true;
                }
                return false;
            };
            const auto tickExists = [&model, &scale](int aFrame) {
                for (const int x : model.frameTickX) {
                    if (scale.frameAt(x).get() == aFrame)
                        return true;
                }
                return false;
            };
            if (merged ? !(tickExists(label.frame) && tickExists(label.endFrame)) : !tickInsideBox(label.frame))
                ++noTick;

            if (anyPrevious) {
                const int gap = label.rect.left() - previousRight - 1;
                minGap = std::min(minGap, gap);
            }
            previousRight = label.rect.right();
            anyPrevious = true;

            if (sampled <= 4) {
                std::printf(
                    "[3] label \"%s\" frame=%d end=%d expected=\"%s\" box x=%d..%d y=%d..%d reserve=%dpx\n",
                    qUtf8Printable(label.text), label.frame, label.endFrame, qUtf8Printable(expected),
                    label.rect.left(), label.rect.right(), label.rect.top(), label.rect.bottom(),
                    numberReserve * int(label.text.size())
                );
            }
        }

        // THE TIMELINE'S SLOT, NOT A MEASURED BOX. The ruler reserves
        // kNumberCharWidth (6) per character and draws the number from
        // `left = tickX - (reserve >> 1)`. The panel must publish the same integer slot:
        // same width, same left edge, so the drawing layer can anchor the run at the
        // box's left instead of centring it on the drawing font's advance (that
        // subtraction is the 1px left shift this test exists to prevent).
        //
        // AND THE BOX IS NEVER NUDGED: a number that leaves the band is CLIPPED by the
        // host, not slid inward, because a shifted number would name a frame it is not
        // centred on. That rule is the shared model's (src/gui/FrameRuler.h) and was
        // signed off when the two rulers were unified - before it, this test allowed the
        // panel one nudge inside the band.
        const auto measureSlots = [&](const gui::GraphPanelModel& aModel, const ctrl::graph::Scaler& aScale,
                                      const char* aWindow) {
            for (const gui::PanelLabel& label : aModel.frameLabels) {
                // A merged boundary label names TWO frames in one box: it has no one
                // frame of its own to centre on.
                if (label.endFrame >= 0 || label.frame < 0)
                    continue;
                const int reserve = numberReserve * int(label.text.size());
                const int tickX = aScale.pixelX(core::Frame(label.frame));
                const int unclampedLeft = tickX - (reserve >> 1);
                const int expectedLeft = unclampedLeft;
                if (label.rect.width() != reserve)
                    ++wrongBoxWidth;
                if (label.rect.left() != expectedLeft)
                    ++wrongBoxLeft;
                if (label.rect.left() == unclampedLeft && label.text.size() >= 1 && label.text.size() <= 3) {
                    ++timelineBoxes[int(label.text.size())];
                    if (label.text.size() == 1) {
                        std::printf(
                            "[3] %s: one-digit label \"%s\" frame=%d tickX=%d unclampedLeft=%d"
                            " box x=%d..%d (band %d..%d)\n",
                            aWindow, qUtf8Printable(label.text), label.frame, tickX, unclampedLeft,
                            label.rect.left(), label.rect.right(), aModel.rulerBand.left(),
                            aModel.rulerBand.right()
                        );
                    }
                }
            }
        };
        measureSlots(model, scale, "fitted window");

        // A SECOND WINDOW BUILT FOR THE ONE-DIGIT CASE. The fitted window above is the
        // graph's own ladder at its seed rung (1.1 px/frame, a number every 96 frames).
        // Its only one-digit frame labels are the ladder's 0 and the project's first
        // frame, 1 - and both fall inside the playhead's pill (the document is on frame
        // 1, so the pill spans those columns), which is why no one-digit box survives
        // it. This window maps 0..8 across the panel (1280 / 8 = 160 px/frame): every
        // frame is then a numbered major tick, so frames 2..7 are one-digit labels well
        // inside the band and the identity is exercised where `reserve` (6px) is far
        // from the drawing font's advance. Frame 0 is clamped at the band's left edge
        // and frame 1 is the playhead's, which is why the printed list starts at 2. A
        // SECOND EDITOR: the main one's frame axis is the graph's own, and a shared
        // window applied to it could not be undone exactly (clearFrameWindow leaves the
        // offset and the ladder rung it rewrote).
        ctrl::GraphEditor digitsEditor(project, logger);
        digitsEditor.scale().setRect(panel);
        digitsEditor.setTargets({ &nodeA, &nodeB });
        digitsEditor.setFrameWindow(0.0, 8.0);
        const gui::GraphPanelModel digits = gui::buildGraphPanelModel(digitsEditor, viewport, palette);
        ctrl::graph::Scaler digitsScale = digitsEditor.scale();
        digitsScale.setRect(panel);
        measureSlots(digits, digitsScale, "one-digit window 0..8");
        int digitsLabels = 0;
        int digitsWrongText = 0;
        for (const gui::PanelLabel& label : digits.frameLabels) {
            if (label.endFrame >= 0 || label.text != format.frameToString(label.frame, formatType))
                ++digitsWrongText; // every box names the frame it publishes, the formatter's way
            digitsLabels += int(label.text.size() == 1);
        }
        std::printf(
            "[3] one-digit window 0..8: labels=%d (all one digit=%d, wrong text=%d), ticks=%d,"
            " pxPerFrame=%.3f, playhead frame=%d x=%d\n",
            isz(digits.frameLabels), digitsLabels, digitsWrongText, isz(digits.frameTickX),
            digitsScale.framePpf(), digitsEditor.currentFrame().get(), digits.playheadX
        );
        CHECK(!digits.frameLabels.isEmpty());
        CHECK(digitsWrongText == 0);
        CHECK(digitsLabels == isz(digits.frameLabels)); // the window's whole point: no 2/3-digit label in it
        CHECK(digitsScale.framePpf() == 160.0);         // 0..8 across a 1280px panel

        std::printf(
            "[3] sampled %d frame labels, %d not equal to frameToString(frame), %d without a frame,"
            " %d without the tick of their own frame, minimum gap between neighbouring boxes=%d px;"
            " TIMELINE SLOT: boxes with the wrong reserve=%d, with the wrong left edge=%d,"
            " 1/2/3-digit boxes exactly at the timeline's left edge=%d/%d/%d\n",
            sampled, wrongText, noFrame, noTick, sampled > 1 ? minGap : -1, wrongBoxWidth, wrongBoxLeft,
            timelineBoxes[1], timelineBoxes[2], timelineBoxes[3]
        );
        CHECK(sampled > 1);
        CHECK(wrongText == 0);
        CHECK(noFrame == 0);
        CHECK(noTick == 0);
        CHECK(minGap >= 8);
        // Every frame number is the timeline's slot, and BOTH windows carry the digit
        // lengths the identity needs: the fitted one has 2- and 3-digit numbers (its
        // only 1-digit candidate, frame 1, is the playhead's) and the 0..8 window has
        // nothing but 1-digit ones. The counts are summed over the two, so each length
        // is exercised where it actually occurs - not where the reserve happens to
        // equal the drawing font's advance.
        CHECK(wrongBoxWidth == 0);
        CHECK(wrongBoxLeft == 0);
        CHECK(timelineBoxes[1] > 0);
        CHECK(timelineBoxes[2] > 0);
        CHECK(timelineBoxes[3] > 0);

        // One tick, one number: the label rects are never the same box twice.
        int duplicates = 0;
        for (int i = 1; i < model.frameLabels.size(); ++i) {
            if (model.frameLabels[i].rect.intersects(model.frameLabels[i - 1].rect))
                ++duplicates;
        }
        std::printf("[3] overlapping neighbouring label boxes=%d\n", duplicates);
        CHECK(duplicates == 0);

        // The ticks ARE the ladder's own ticks (plus the ruler's forced boundaries
        // below), not a tick per pixel: deleting the `level == None` filter in
        // buildFrameAxis would turn the axis into the solid comb the design removed -
        // one tick per pixel at the 1px/frame floor. The measurements that catch that
        // run after the oracle, which knows which columns the ruler forced.

        // THE LADDER'S TICKS, PLUS THE RULER'S FORCED BOUNDARIES. The oracle is the
        // scaler's own classification, re-derived with the documented rule the model
        // follows: every None-filtered classified frame inside the plot is a tick, and
        // the PROJECT's first/last frames (the editor's own range) replace their tick
        // with a Major one when the ruler names them (their labels are active or the
        // two are merged) - the timeline's boundary rule (TimeRuler.cpp:139-182). The
        // model's own vectors are never used as their own oracle.
        // The axis is enumerated over the RULER BAND - the panel's columns - because
        // that is where the ticks are drawn now (the band spans the panel, and the
        // frame mapping is the timeline's).
        const core::Frame windowFirst = scale.frameAt(model.rulerBand.left());
        const core::Frame windowLast = scale.frameAt(model.rulerBand.right());
        QVector<int> ladderTicks;
        QVector<int> ladderLevels;
        int classified = 0;
        for (int frame = windowFirst.get(); frame <= windowLast.get(); ++frame) {
            const ctrl::time::Scaler::TickLevel level =
                scale.frameScale().attributeAt(frame, scale.framePpf()).level;
            if (level == ctrl::time::Scaler::TickLevel::None)
                continue;
            ++classified;
            const int x = scale.pixelX(core::Frame(frame));
            if (x < model.rulerBand.left() || model.rulerBand.right() < x)
                continue;
            if (ladderTicks.isEmpty() || ladderTicks.last() != x) {
                ladderTicks.append(x);
                ladderLevels.append(int(level));
            }
        }
        // The boundary frames, promoted to the ruler's Major tick when the model is
        // asked to name them - the same condition buildFrameAxis uses.
        const int boundaryFirstFrame = editor.frameRange().min();
        const int boundaryLastFrame = editor.frameRange().max();
        const bool hasBoundaryRange = boundaryFirstFrame < boundaryLastFrame;
        const int boundaryFirstX = hasBoundaryRange ? scale.pixelX(core::Frame(boundaryFirstFrame)) : -1;
        const int boundaryLastX = hasBoundaryRange ? scale.pixelX(core::Frame(boundaryLastFrame)) : -1;
        const auto inBand = [&model](int aX) {
            return model.rulerBand.left() <= aX && aX <= model.rulerBand.right();
        };
        const int majorLevel = int(ctrl::time::Scaler::TickLevel::Major);
        const auto levelOf = [&scale](int aFrame) {
            return int(scale.frameScale().attributeAt(aFrame, scale.framePpf()).level);
        };
        const bool firstInView = hasBoundaryRange && inBand(boundaryFirstX);
        const bool lastInView = hasBoundaryRange && inBand(boundaryLastX);
        const bool firstActive = firstInView && levelOf(boundaryFirstFrame) != majorLevel;
        const bool lastActive = lastInView && levelOf(boundaryLastFrame) != majorLevel;
        const QString firstBoundaryText =
            firstInView ? format.frameToString(boundaryFirstFrame, formatType) : QString();
        const QString lastBoundaryText =
            lastInView ? format.frameToString(boundaryLastFrame, formatType) : QString();
        const bool mergedBoundaries = firstInView && lastInView &&
            (boundaryFirstX + (numberReserve * int(firstBoundaryText.size())) / 2 +
                 gui::frameRuler::kNumberMargin >=
             boundaryLastX - (numberReserve * int(lastBoundaryText.size())) / 2);
        // The forced boundary ticks, by column: a tick the ladder does not classify is
        // allowed exactly here (that is what the ruler's boundary ink is).
        QVector<int> forcedX;
        const auto promote = [&ladderTicks, &ladderLevels, &forcedX](int aX) {
            for (int i = 0; i < ladderTicks.size(); ++i) {
                if (ladderTicks.at(i) == aX) {
                    ladderTicks.removeAt(i);
                    ladderLevels.removeAt(i);
                    break;
                }
            }
            ladderTicks.append(aX);
            ladderLevels.append(int(ctrl::time::Scaler::TickLevel::Major));
            forcedX.append(aX);
        };
        if (firstInView && (mergedBoundaries || firstActive))
            promote(boundaryFirstX);
        if (lastInView && (mergedBoundaries || lastActive))
            promote(boundaryLastX);
        // Ascending, one tick per column (the more prominent tier wins a shared one).
        {
            QVector<int> order;
            for (int i = 0; i < ladderTicks.size(); ++i)
                order.append(i);
            std::stable_sort(order.begin(), order.end(), [&ladderTicks, &ladderLevels](int a, int b) {
                return ladderTicks.at(a) != ladderTicks.at(b) ? ladderTicks.at(a) < ladderTicks.at(b)
                                                              : ladderLevels.at(a) > ladderLevels.at(b);
            });
            QVector<int> sortedTicks;
            QVector<int> sortedLevels;
            for (int i : order) {
                if (!sortedTicks.isEmpty() && sortedTicks.last() == ladderTicks.at(i))
                    continue;
                sortedTicks.append(ladderTicks.at(i));
                sortedLevels.append(ladderLevels.at(i));
            }
            ladderTicks = sortedTicks;
            ladderLevels = sortedLevels;
        }

        int unclassified = 0;
        int notRoundTrip = 0;
        int notAscending = 0;
        for (int i = 0; i < model.frameTickX.size(); ++i) {
            const int x = model.frameTickX.at(i);
            const core::Frame frame = scale.frameAt(x);
            const bool forced = forcedX.contains(x);
            if (!forced && scale.frameScale().attribute(frame.get()).level == ctrl::time::Scaler::TickLevel::None)
                ++unclassified;
            if (scale.pixelX(frame) != x)
                ++notRoundTrip;
            if (0 < i && x <= model.frameTickX.at(i - 1))
                ++notAscending;
        }

        std::printf("[3] frame ticks: published=%d, the ladder's own inside the plot=%d (%d classified frames in "
                    "the window)  unclassified ticks=%d  pixel round-trip mismatches=%d  out of order=%d\n",
                    isz(model.frameTickX), isz(ladderTicks), classified, unclassified, notRoundTrip, notAscending);
        CHECK(!model.frameTickX.isEmpty());
        CHECK(unclassified == 0);
        CHECK(notRoundTrip == 0);
        CHECK(notAscending == 0);
        CHECK(model.frameTickX == ladderTicks);

        // The TIERS: one per tick, index-aligned with the positions, each one of the
        // three tiers the ruler draws, and each the level the oracle derived for that
        // column. TickLevel::None reaches the model only through a FORCED boundary
        // tick (which the ruler promotes to Major), so a None entry elsewhere, a tier
        // for the wrong tick, or a count that does not match the positions, fails here.
        int notATier = 0;
        int tierHistogram[4] = {0, 0, 0, 0};
        for (int i = 0; i < model.frameTickLevel.size(); ++i) {
            const int level = model.frameTickLevel.at(i);
            const bool known = int(ctrl::time::Scaler::TickLevel::Minor) <= level &&
                level <= int(ctrl::time::Scaler::TickLevel::Major);
            if (!known)
                ++notATier;
            else
                ++tierHistogram[level];
        }
        std::printf("[3] frame tick tiers: major=%d intermediate=%d minor=%d  out of the three tiers=%d  "
                    "index-aligned with the positions=%d (ticks=%d levels=%d)  matching the ladder=%d\n",
                    tierHistogram[int(ctrl::time::Scaler::TickLevel::Major)],
                    tierHistogram[int(ctrl::time::Scaler::TickLevel::Intermediate)],
                    tierHistogram[int(ctrl::time::Scaler::TickLevel::Minor)], notATier,
                    int(model.frameTickLevel.size() == model.frameTickX.size()), isz(model.frameTickX),
                    isz(model.frameTickLevel), int(model.frameTickLevel == ladderLevels));
        CHECK(!model.frameTickLevel.isEmpty());
        CHECK(model.frameTickLevel.size() == model.frameTickX.size());
        CHECK(notATier == 0);
        CHECK(model.frameTickLevel == ladderLevels);

        // The value ticks are the value axis' own ticks: one per published value
        // label, in the labels' order, each on the pixel of the value its label
        // names (and the box is centred on that pixel, as far as the column's own
        // edges allow - the clamp moves it at the extremes). Two of the window's
        // three values rounding to the same string are deduplicated, which is the
        // renderer's own rule, so the expected pair of lists is derived from the
        // scaler's window with that rule applied.
        const float windowValues[3] = {
            scale.valueMax(), 0.5f * (scale.valueMin() + scale.valueMax()), scale.valueMin()
        };
        QVector<QString> expectedValueText;
        QVector<int> expectedValueTickY;
        QString shownValue;
        for (const float value : windowValues) {
            const QString text = valueTextOf(value);
            if (text == shownValue)
                continue;
            shownValue = text;
            expectedValueText.append(text);
            expectedValueTickY.append(scale.pixelY(value));
        }

        int valueTextMismatches = 0;
        int valueTicksOffCentre = 0;
        for (int i = 0; i < model.valueLabels.size() && i < expectedValueText.size(); ++i) {
            if (model.valueLabels.at(i).text != expectedValueText.at(i))
                ++valueTextMismatches;
            if (i < model.valueTickY.size()) {
                const int centre = model.valueLabels.at(i).rect.center().y();
                if (std::abs(centre - model.valueTickY.at(i)) > model.valueLabels.at(i).rect.height() / 2 + 1)
                    ++valueTicksOffCentre;
            }
        }

        std::printf("[3] value ticks: published=%d, expected=%d  labels=%d, text mismatches=%d, ticks not on their "
                    "label's own pixel=%d\n",
                    isz(model.valueTickY), isz(expectedValueTickY), isz(model.valueLabels), valueTextMismatches,
                    valueTicksOffCentre);
        CHECK(model.valueTickY == expectedValueTickY);
        CHECK(model.valueLabels.size() == expectedValueText.size());
        CHECK(valueTextMismatches == 0);
        CHECK(valueTicksOffCentre == 0);
    }

    // -------------------------------------------------------------------------
    // 4. curves
    {
        const QVector<ctrl::graph::Curve> editorCurves = editor.curves();
        std::printf("[4] curves=%d  editor.curves()=%d  channel rows=%d\n", isz(model.curves), isz(editorCurves),
                    isz(model.channels));

        // Exactly one curve per expected pair, in the editor's own order - and one
        // CHANNEL LIST row per pair too, in the same order, with the curve naming the
        // row it belongs to (so a consumer of one list can address the other).
        CHECK(model.curves.size() == pairs.size());
        CHECK(editorCurves.size() == pairs.size());
        CHECK(model.channels.size() == pairs.size());
        for (int i = 0; i < pairs.size() && i < model.curves.size(); ++i) {
            CHECK(model.curves[i].node == pairs[i].first);
            CHECK(model.curves[i].channel == pairs[i].second);
            CHECK(model.curves[i].channelRow == i);
            CHECK(0 <= model.curves[i].channelRow && model.curves[i].channelRow < model.channels.size());
            CHECK(model.channels[model.curves[i].channelRow].node == pairs[i].first);
            CHECK(model.channels[model.curves[i].channelRow].channel == pairs[i].second);
            CHECK(model.curves[i].color.isValid());
            CHECK(model.curves[i].color == model.channels[model.curves[i].channelRow].color);
        }
        for (int i = 0; i < editorCurves.size() && i < model.curves.size(); ++i) {
            CHECK(editorCurves[i].channel() == model.curves[i].channel); // same set, same order
        }

        int unique = 0;
        for (int i = 0; i < model.curves.size(); ++i) {
            bool duplicate = false;
            for (int j = 0; j < i; ++j) {
                if (model.curves[j].node == model.curves[i].node && model.curves[j].channel == model.curves[i].channel)
                    duplicate = true;
            }
            if (!duplicate)
                ++unique;
        }
        CHECK(unique == model.curves.size());

        int totalPoints = 0;
        int totalKeys = 0;
        double maxYDeviation = 0.0;
        double maxXDistance = 0.0;
        int badKeyRects = 0;
        int keysOutside = 0;
        int shortCurves = 0;

        for (const gui::PanelCurve& curve : model.curves) {
            totalPoints += curve.points.size();
            totalKeys += curve.keys.size();
            if (curve.points.size() <= 1)
                ++shortCurves;

            const ctrl::graph::Curve source(*curve.node->timeLine(), curve.channel);
            for (const core::TimeKey* key : source.keys()) {
                const core::Frame frame(key->frame());
                const float value = source.valueOf(*key);

                // The key rect is the hit-test's own rectangle, cut to the plot.
                const QRect expected =
                    ctrl::graph::keyRect(scale, frame, value).intersected(model.plot);
                if (!expected.isEmpty() && !curve.keys.contains(expected))
                    ++badKeyRects;
            }
            for (const QRect& key : curve.keys) {
                if (!model.plot.contains(key))
                    ++keysOutside;
            }

            // The polyline passes through its keys.
            for (const core::TimeKey* key : source.keys()) {
                double y = 0.0;
                double distance = 0.0;
                if (!polylineYAt(curve, scale.pixelXOfFrame(double(key->frame())), y, distance))
                    continue;
                maxXDistance = std::max(maxXDistance, distance);
                maxYDeviation = std::max(maxYDeviation, std::fabs(y - double(scale.pixelY(source.valueOf(*key)))));
            }
        }

        std::printf("[4] points=%d (min per curve=%d) keys=%d  key rects outside the plot=%d  key rects != "
                    "graph::keyRect=%d\n",
                    totalPoints,
                    [&model] {
                        int m = std::numeric_limits<int>::max();
                        for (const gui::PanelCurve& c : model.curves)
                            m = std::min(m, isz(c.points));
                        return m;
                    }(), totalKeys, keysOutside, badKeyRects);
        std::printf("[4] max |polyline.x - keyPixelX| = %.6f px,  max |polyline.y - pixelY(valueOf(key))| = %.6f px\n",
                    maxXDistance, maxYDeviation);

        CHECK(shortCurves == 0);
        CHECK(keysOutside == 0);
        CHECK(badKeyRects == 0);
        CHECK(maxXDistance <= 0.5);
        CHECK(maxYDeviation <= 2.0);

        // The channel list rows, and the NAME CHIP of every curve.
        //
        // A ROW is data only (the list is the application's own QMenu, so the model
        // lays out no row geometry - the header's CHANNEL LIST): it must name a real
        // target and channel, carry its layer, its name and a valid colour, and its
        // `visible` must be the editor's own answer.
        //
        // A CHIP is geometry, and it is derived from the curve it NAMES: the box's
        // right edge is at the plot's right edge, its vertical CENTRE is the value
        // that same curve leaves the plot with (asserted against the curve's own
        // published polyline, not against the model's arithmetic), and it lies
        // inside the plot. A curve that leaves the window too near an edge publishes
        // NO chip - an empty rect - rather than one in the margin.
        int badRows = 0;
        for (const gui::PanelChannelRow& row : model.channels) {
            const bool wellFormed = row.node != nullptr && row.node->timeLine() != nullptr && !row.name.isEmpty() &&
                !row.layer.isEmpty() && row.color.isValid() && row.visible == editor.isChannelVisible(*row.node, row.channel);
            if (!wellFormed)
                ++badRows;
        }
        std::printf("[4] channel rows=%d, malformed rows=%d, first row layer=\"%s\" name=\"%s\" visible=%d\n",
                    isz(model.channels), badRows, qUtf8Printable(model.channels.first().layer),
                    qUtf8Printable(model.channels.first().name), int(model.channels.first().visible));
        CHECK(model.channels.size() == pairs.size());
        CHECK(badRows == 0);

        // THE GROUPED LEGEND AND THE CHANNEL HUE TABLE (design section 7).
        //
        // The list is grouped one group per (layer, key type) - the group naming
        // the property and its components, with one row per component inside it -
        // and each row's colour is the channel's hue from the ONE table both views
        // read (theme::Colors, THE CHANNEL HUE TABLE): axis where the component
        // lies on an axis (X red, Y green), the type's own hue otherwise. This
        // pins the grouping and the per-component hue, so the panel's curve/marker/
        // chip ink cannot drift from the timeline's key and lane ink.
        {
            const theme::Colors& colors = theme::Colors::current();

            // The four keys this document has: A's Move (X, Y) and Rotate, then
            // B's Opa - target order, then the timeline's own type order.
            CHECK(model.channels.size() == 4);
            CHECK(model.channels.at(0).channel == ctrl::graph::Channel(core::TimeKeyType_Move, 0));
            CHECK(model.channels.at(1).channel == ctrl::graph::Channel(core::TimeKeyType_Move, 1));
            CHECK(model.channels.at(2).channel == ctrl::graph::Channel(core::TimeKeyType_Rotate, 0));
            CHECK(model.channels.at(3).channel == ctrl::graph::Channel(core::TimeKeyType_Opa, 0));

            // ONE GROUP PER (layer, key type), naming the property and every
            // component - "<type> [X][Y]"; a one-component type carries its own name
            // and needs no bracketed repeat. The TYPE NAME is asked of the same API
            // the model reads, so the shape is pinned without re-stating a display
            // string that another surface may rename ("Move" is "Position" now).
            const QString moveName = core::TimeLine::getTimeKeyDisplayName(core::TimeKeyType_Move);
            CHECK(model.channels.at(0).group == moveName + QStringLiteral(" [X][Y]"));
            CHECK(model.channels.at(1).group == moveName + QStringLiteral(" [X][Y]"));
            CHECK(model.channels.at(2).group ==
                  core::TimeLine::getTimeKeyDisplayName(core::TimeKeyType_Rotate));
            CHECK(model.channels.at(3).group ==
                  core::TimeLine::getTimeKeyDisplayName(core::TimeKeyType_Opa));
            // A group's rows are contiguous: the same (layer, type) never returns
            // after another group has started, or the menu's boundary walk would
            // open a second heading for one property.
            for (int i = 1; i < model.channels.size(); ++i) {
                const gui::PanelChannelRow& previous = model.channels.at(i - 1);
                const gui::PanelChannelRow& row = model.channels.at(i);
                const bool sameGroup = previous.node == row.node && previous.channel.type() == row.channel.type();
                CHECK(sameGroup ? previous.group == row.group : previous.group != row.group);
            }

            // THE PER-COMPONENT HUE. X and Y of one type differ; a channel with no
            // axis keeps its type's hue.
            CHECK(model.channels.at(0).color == colors.axisX);
            CHECK(model.channels.at(1).color == colors.axisY);
            CHECK(model.channels.at(0).color != model.channels.at(1).color);
            CHECK(model.channels.at(2).color == colors.rotateKey); // angle: no axis
            CHECK(model.channels.at(3).color == colors.opaKey);    // opacity: no axis

            // THE NO-AXIS GUARD: an axis must never be silently assigned to Depth
            // (the design leaves it on its type's hue) or to an angle, and HSV's
            // three values have no axis either. Both the classifier and the colour
            // are pinned, so a future axis added for one of them fails here.
            CHECK(theme::channelAxis(core::TimeKeyType_Depth, 0) < 0);
            CHECK(theme::channelAxis(core::TimeKeyType_Rotate, 0) < 0);
            CHECK(theme::channelAxis(core::TimeKeyType_Opa, 0) < 0);
            CHECK(theme::channelAxis(core::TimeKeyType_HSV, 0) < 0);
            CHECK(theme::channelAxis(core::TimeKeyType_HSV, 1) < 0);
            CHECK(theme::channelAxis(core::TimeKeyType_HSV, 2) < 0);
            CHECK(theme::channelAxis(core::TimeKeyType_Blur, 2) < 0); // Blur's angle
            // ...while Blur's two RADII are its X/Y axes, the interpretation that
            // makes its three channels distinguishable (the section's own defect
            // list names 'Blur's three' as sharing one hue).
            CHECK(theme::channelAxis(core::TimeKeyType_Blur, 0) == theme::kChannelAxisX);
            CHECK(theme::channelAxis(core::TimeKeyType_Blur, 1) == theme::kChannelAxisY);
            CHECK(theme::channelColor(colors, core::TimeKeyType_Depth, 0) == colors.depthKey);
            CHECK(theme::channelColor(colors, core::TimeKeyType_HSV, 0) == colors.HSVKey);
            CHECK(theme::channelColor(colors, core::TimeKeyType_HSV, 1) == colors.HSVKey);
            CHECK(theme::channelColor(colors, core::TimeKeyType_HSV, 2) == colors.HSVKey);
            CHECK(theme::channelColor(colors, core::TimeKeyType_Blur, 2) == colors.blurKey);
            // The panel and the timeline read the SAME answer: the model row's hue
            // is theme::channelColor, and the timeline's key/lane hue for the type
            // is channelColor(type, 0).
            CHECK(model.channels.at(0).color == theme::channelColor(colors, core::TimeKeyType_Move, 0));
            CHECK(model.channels.at(1).color == theme::channelColor(colors, core::TimeKeyType_Move, 1));
            CHECK(palette.keyColor(core::TimeKeyType_Move) == model.channels.at(0).color);
            CHECK(palette.keyColor(core::TimeKeyType_Opa) == model.channels.at(3).color);
            CHECK(palette.channelColor(core::TimeKeyType_Move, 1) == model.channels.at(1).color);

            // THE UNREACHABLE TYPES, proven rather than assumed: componentCount()
            // is 0 for all four, and graphableTypes() - the filter buildChannels
            // walks - never yields them, so no row and no curve can name them. This
            // is why their entries were deleted from the panel's table; the
            // timeline still draws their keys, so Colors keeps their tokens.
            CHECK(ctrl::graph::Channel::componentCount(core::TimeKeyType_Bone) == 0);
            CHECK(ctrl::graph::Channel::componentCount(core::TimeKeyType_Pose) == 0);
            CHECK(ctrl::graph::Channel::componentCount(core::TimeKeyType_Mesh) == 0);
            CHECK(ctrl::graph::Channel::componentCount(core::TimeKeyType_FFD) == 0);
            CHECK(!ctrl::graph::Channel::graphableTypes().contains(core::TimeKeyType_Bone));
            CHECK(!ctrl::graph::Channel::graphableTypes().contains(core::TimeKeyType_Pose));
            CHECK(!ctrl::graph::Channel::graphableTypes().contains(core::TimeKeyType_Mesh));
            CHECK(!ctrl::graph::Channel::graphableTypes().contains(core::TimeKeyType_FFD));
            for (const gui::PanelChannelRow& row : model.channels)
                CHECK(ctrl::graph::Channel::componentCount(row.channel.type()) > 0);
        }

        int chips = 0;
        int emptyChips = 0;
        int badChipText = 0;
        int emptyChipText = 0;
        int outsideChips = 0;
        int offCurveChips = 0;
        double maxChipCentreError = 0.0;
        for (const gui::PanelCurve& curve : model.curves) {
            if (curve.chipText.isEmpty()) {
                ++emptyChipText;
                continue;
            }
            if (curve.channelRow < 0 || curve.channelRow >= model.channels.size()) {
                ++badChipText;
                continue;
            }
            // The chip names the CURVE: its text is the name of the row this curve
            // belongs to (not some other channel's).
            if (curve.chipText != model.channels.at(curve.channelRow).name)
                ++badChipText;
            if (curve.chip.isEmpty()) {
                ++emptyChips;
                continue;
            }
            ++chips;
            if (!model.plot.contains(curve.chip))
                ++outsideChips;
            // The chip is anchored at the plot's right edge: its box ends inside the
            // plot and starts left of it, so it is a chip AT the curve's own exit
            // point rather than a label parked somewhere else.
            if (curve.chip.right() > model.plot.right() || curve.chip.left() < model.plot.left())
                ++outsideChips;
            // ...and it is centred on the value the curve LEAVES the window with: the
            // curve's own published polyline at the plot's right edge.
            double y = 0.0;
            double distance = 0.0;
            if (polylineYAt(curve, double(model.plot.right()), y, distance)) {
                const double error = std::fabs(double(curve.chip.center().y()) - y);
                maxChipCentreError = std::max(maxChipCentreError, error);
                if (error > 1.0)
                    ++offCurveChips;
            }
        }
        std::printf("[4] chips=%d  curves whose chip is empty=%d  chips outside the plot=%d  chips off their own "
                    "curve=%d (max |centre - polyline y| = %.3f px)  empty chip texts=%d  chips naming another row=%d\n",
                    chips, emptyChips, outsideChips, offCurveChips, maxChipCentreError, emptyChipText, badChipText);
        CHECK(chips > 0);          // the fixture's premise: some curve leaves the window inside the plot's rows
        CHECK(emptyChipText == 0); // every curve names itself
        CHECK(badChipText == 0);
        CHECK(outsideChips == 0);
        CHECK(offCurveChips == 0);
    }

    // -------------------------------------------------------------------------
    // 8. NO LEGEND: the names live at their curves. Two things are pinned, and
    // neither is cosmetic:
    //   1. nothing names a channel in the plot's bottom-left corner any more - the
    //      legend stack is gone and not orphaned, and every chip the model publishes
    //      is anchored at the plot's RIGHT edge (so not one of them can sit where the
    //      stack did, over the value numbers and the zero rule);
    //   2. a cramped viewport changes the LIST not at all - every row is still
    //      published, because the list is the application's own QMenu and a menu
    //      scrolls - while the geometry the panel DRAWS gives up rather than leaving
    //      the plot: a chip with no room is an empty rect;
    //   3. every chip the cramped model does publish is still inside the plot, so a
    //      box that exists is always a box the drawing layer can draw.
    {
        int chipsLeftOfCentre = 0;
        int chipsOutsidePlot = 0;
        for (const gui::PanelCurve& curve : model.curves) {
            if (curve.chip.isEmpty())
                continue;
            if (curve.chip.left() < model.plot.center().x())
                ++chipsLeftOfCentre;
            if (!model.plot.contains(curve.chip))
                ++chipsOutsidePlot;
        }
        std::printf("[8] full viewport: chips=%d  chips left of the plot's centre (where the legend was)=%d  "
                    "chips outside the plot=%d\n",
                    [&model] {
                        int n = 0;
                        for (const gui::PanelCurve& c : model.curves)
                            n += !c.chip.isEmpty();
                        return n;
                    }(), chipsLeftOfCentre, chipsOutsidePlot);
        CHECK(chipsLeftOfCentre == 0);
        CHECK(chipsOutsidePlot == 0);

        const QSize cramped(160, 48);
        const gui::GraphPanelModel crampedModel = gui::buildGraphPanelModel(editor, cramped, palette);

        int identityLost = 0;
        for (int i = 0; i < crampedModel.channels.size(); ++i) {
            const gui::PanelChannelRow& row = crampedModel.channels.at(i);
            const gui::PanelChannelRow& full = model.channels.at(i);
            if (row.node != full.node || row.channel != full.channel || row.name != full.name ||
                row.layer != full.layer || row.group != full.group || row.color != full.color ||
                row.visible != full.visible || row.focused != full.focused)
                ++identityLost; // a row the panel cannot DRAW is still the same row
        }

        int crampedChips = 0;
        int crampedEmptyChips = 0;
        int crampedOutsideChips = 0;
        int dishonestEmptyChips = 0;
        for (const gui::PanelCurve& curve : crampedModel.curves) {
            if (curve.chip.isEmpty()) {
                ++crampedEmptyChips;
                // An empty chip must be TRUTHFUL: the box would not have fitted the
                // plot. (A chip dropped for no reason would be a name the user can
                // never see on a panel that had room for it.)
                if (!curve.points.isEmpty()) {
                    const QFontMetrics metrics{QFont()};
                    const int width = metrics.horizontalAdvance(curve.chipText) + 12; // the model's own padding: 2 x 6
                    const int height = metrics.height();
                    const QRect box(
                        crampedModel.plot.right() + 1 - 8 - width, qRound(curve.points.last().y()) - height / 2,
                        width, height
                    );
                    if (crampedModel.plot.contains(box))
                        ++dishonestEmptyChips;
                }
                continue;
            }
            ++crampedChips;
            if (!crampedModel.plot.contains(curve.chip))
                ++crampedOutsideChips;
        }

        std::printf("[8] cramped viewport %dx%d -> plot (%d,%d,%dx%d)  channel rows=%d (identity mismatches=%d)  "
                    "published chips=%d  empty chips=%d (of those, ones that WOULD have fitted=%d)  chips outside "
                    "the plot=%d\n",
                    cramped.width(), cramped.height(), crampedModel.plot.x(), crampedModel.plot.y(),
                    crampedModel.plot.width(), crampedModel.plot.height(), isz(crampedModel.channels), identityLost,
                    crampedChips, crampedEmptyChips, dishonestEmptyChips, crampedOutsideChips);

        CHECK(!crampedModel.plot.isEmpty());
        CHECK(crampedModel.channels.size() == model.channels.size());
        CHECK(identityLost == 0);
        CHECK(crampedOutsideChips == 0);
        CHECK(dishonestEmptyChips == 0);

        // A plot too small for anything at all: every chip is empty. The viewport has
        // to leave a NON-EMPTY plot first: the label column's reserve is the value
        // format's own width (Scaler::axisLabelWidth(), 68px at the UI font rather
        // than the old 46), so the plot starts 22px further right; and the right
        // edge reserves the scroll bar lane (Scaler::kScrollBarReserve == the
        // app's 12px lane, with no gap), so this fixture stays a viewport whose
        // plot exists and is narrower than 16px.
        const QSize tooNarrow(94, 200);
        const gui::GraphPanelModel narrowModel = gui::buildGraphPanelModel(editor, tooNarrow, palette);
        int narrowChips = 0;
        for (const gui::PanelCurve& curve : narrowModel.curves) {
            if (!curve.chip.isEmpty())
                ++narrowChips;
        }
        std::printf("[8] narrow viewport %dx%d -> plot width=%d  channel rows=%d  published chips=%d  "
                    "chip text=\"%s\"\n",
                    tooNarrow.width(), tooNarrow.height(), narrowModel.plot.width(), isz(narrowModel.channels),
                    narrowChips,
                    narrowModel.curves.isEmpty() ? "" : qUtf8Printable(narrowModel.curves.first().chipText));
        CHECK(narrowModel.plot.width() < 16); // no room for a 12px-chip-plus-text: see the header's CHIPS
        CHECK(narrowModel.channels.size() == model.channels.size());
        CHECK(narrowChips == 0);
    }

    // -------------------------------------------------------------------------
    // 9. the polyline is published unclipped: the drawing layer owns the clip
    {
        // The model hands the drawing layer the sampled curve mapped through the
        // scaler, point for point and in order, and it does NOT cut the line to the
        // plot - the header's CURVES states the clipping as the CONSUMER's own
        // obligation (the ImGui overlay pushes a clip rect to the plot, and the
        // QPainter renderer cut every segment against it). The value window is
        // narrowed here, through the editor's own scaler - the object a wheel zoom
        // moves - so the fixture is guaranteed to hold samples that leave the plot.
        const float windowMin = editor.scale().valueMin();
        const float windowMax = editor.scale().valueMax();
        editor.scale().setValueWindow(0.0f, 50.0f);

        ctrl::graph::Scaler zoomed = editor.scale();
        zoomed.setRect(panel);
        const gui::GraphPanelModel zoomedModel = gui::buildGraphPanelModel(editor, viewport, palette);

        editor.scale().setValueWindow(windowMin, windowMax); // as the editor was found

        int mismatchedCurves = 0;
        int totalPoints = 0;
        int outsideX = 0;
        int outsideY = 0;
        double worstDelta = 0.0;

        for (const gui::PanelCurve& curve : zoomedModel.curves) {
            const ctrl::graph::Curve source(*curve.node->timeLine(), curve.channel);
            const core::Frame first = zoomed.frameAt(zoomedModel.plot.left());
            const core::Frame last = zoomed.frameAt(zoomedModel.plot.right());
            const int segments = std::max(1, source.segmentCount());
            // The header's own sampling budget (the renderer's): one point per ~2px
            // of plot width per segment, floored and capped.
            const int samples = std::clamp(zoomedModel.plot.width() / (2 * segments), 2, 256);

            QVector<QPointF> polyline;
            source.buildPolyline(first, last, samples, polyline);

            bool same = (polyline.size() == curve.points.size());
            for (int i = 0; same && i < polyline.size(); ++i) {
                // Both axes through the SCALER's exact mapping: the curve's own coordinates are the
                // mapping's, not a rounded copy of it (a whole-pixel y is what makes a flat curve's
                // edges hard).
                const QPointF expected(
                    zoomed.pixelXOfFrame(polyline.at(i).x()), zoomed.pixelYOfValue(float(polyline.at(i).y()))
                );
                const double dx = std::fabs(expected.x() - curve.points.at(i).x());
                const double dy = std::fabs(expected.y() - curve.points.at(i).y());
                worstDelta = std::max(worstDelta, std::max(dx, dy));
                if (dx > 0.0 || dy > 0.0)
                    same = false; // a cut or dropped sample would show up right here
            }
            if (!same)
                ++mismatchedCurves;

            // A curve's own coordinates are the EXACT mapping's on both axes. The frame axis always
            // was (pixelXOfFrame); the value axis was rounded to whole pixels, which locks a flat
            // curve's edges to the pixel grid - a nearly-horizontal line then has no antialiasing at
            // all while a steep one, whose edge phase runs along x, stays smooth. This window's
            // mapping is fractional, so an integer y here means the rounding is back.
            {
                bool subPixelY = false;
                for (const QPointF& point : curve.points)
                    subPixelY = subPixelY || std::fabs(point.y() - std::floor(point.y())) > 1e-6;
                CHECK(subPixelY);
            }

            // The value HOLDS outside the keys, so every curve reaches the PLOT's edges: the whole X
            // range shows what playback plays. A polyline that stopped at its first and last key
            // would leave the rest of the plot empty (and a one-key channel would draw nothing).
            CHECK(curve.points.size() >= 2);
            CHECK(curve.points.first().x() <= zoomedModel.plot.left() + 1);
            CHECK(curve.points.last().x() >= zoomedModel.plot.right() - 1);

            for (const QPointF& point : curve.points) {
                ++totalPoints;
                if (point.x() < zoomedModel.plot.left() || double(zoomedModel.plot.right()) < point.x())
                    ++outsideX;
                if (point.y() < zoomedModel.plot.top() || double(zoomedModel.plot.bottom()) < point.y())
                    ++outsideY;
            }
        }

        std::printf("[9] window 0..50 (was %g..%g)  curves=%d  points=%d  curves != the sampled curve mapped through "
                    "the scaler=%d  worst coordinate delta=%.9f  samples outside the plot: x=%d y=%d\n",
                    double(windowMin), double(windowMax), isz(zoomedModel.curves), totalPoints, mismatchedCurves,
                    worstDelta, outsideX, outsideY);

        CHECK(!zoomedModel.curves.isEmpty());
        CHECK(totalPoints > 0);
        CHECK(mismatchedCurves == 0);
        // The fixture's premise, and the documented contract: samples the narrowed
        // window pushed out of the plot are published anyway.
        CHECK(outsideY > 0);
    }

    // -------------------------------------------------------------------------
    // 4b. the project's LOOP reaches the drawn polyline
    //
    // The model rebuilds its own Curve from each entry's (node, channel), so the editor's loop rule
    // has to travel with them: with the loop on, the frames outside the keys continue the easing from
    // the last key back to the first, and with it off they hold. The assertion is the DIFFERENCE -
    // a hold cannot make the extension vary.
    {
        project.attribute().setLoop(true);
        const gui::GraphPanelModel loopedModel = gui::buildGraphPanelModel(editor, viewport, palette);
        CHECK(loopedModel.projectLoop);
        CHECK(loopedModel.projectLoopRange == project.attribute().frameRange());

        int varying = 0;
        int held = 0;
        for (const gui::PanelCurve& curve : loopedModel.curves) {
            if (curve.points.size() < 3)
                continue;
            // The last key's own pixel column: everything past it is the extension.
            const double lastKeyX = curve.keys.isEmpty() ? 0.0 : double(curve.keys.last().right());
            double lo = 1e30;
            double hi = -1e30;
            for (const QPointF& point : curve.points) {
                if (point.x() < lastKeyX)
                    continue;
                lo = std::min(lo, point.y());
                hi = std::max(hi, point.y());
            }
            if (hi - lo > 0.5)
                ++varying;
            else if (hi > -1e30)
                ++held;
        }
        std::printf(
            "[4b] loop on: curves=%d extension varying=%d holding=%d range=%d..%d\n",
            isz(loopedModel.curves), varying, held, loopedModel.projectLoopRange.min(),
            loopedModel.projectLoopRange.max()
        );
        CHECK(varying > 0);

        project.attribute().setLoop(false);
        const gui::GraphPanelModel heldModel = gui::buildGraphPanelModel(editor, viewport, palette);
        CHECK(!heldModel.projectLoop);
    }

    // -------------------------------------------------------------------------
    // 5. hidden channels
    {
        const ctrl::graph::Channel hidden(core::TimeKeyType_Move, 1); // Move Y of target A
        editor.setChannelVisible(nodeA, hidden, false);
        const gui::GraphPanelModel hiddenModel = gui::buildGraphPanelModel(editor, viewport, palette);

        int rowIndex = -1;
        for (int i = 0; i < hiddenModel.channels.size(); ++i) {
            if (samePair(Pair(hiddenModel.channels[i].node, hiddenModel.channels[i].channel), &nodeA, hidden))
                rowIndex = i;
        }
        int curvesForHidden = 0;
        for (const gui::PanelCurve& curve : hiddenModel.curves) {
            if (curve.node == &nodeA && curve.channel == hidden)
                ++curvesForHidden;
        }

        std::printf("[5] after hiding Move/Y of A: channel rows=%d (row found=%d, visible=%d) curves=%d (was %d) "
                    "curves for the hidden pair=%d\n",
                    isz(hiddenModel.channels), rowIndex,
                    rowIndex >= 0 ? int(hiddenModel.channels[rowIndex].visible) : -1, isz(hiddenModel.curves),
                    isz(model.curves), curvesForHidden);

        CHECK(rowIndex >= 0);
        CHECK(hiddenModel.channels[rowIndex].visible == false);
        CHECK(curvesForHidden == 0);
        CHECK(hiddenModel.curves.size() == model.curves.size() - 1);
        CHECK(hiddenModel.channels.size() == model.channels.size());
        CHECK(hiddenModel.emptyState.isEmpty());

        editor.setChannelVisible(nodeA, hidden, true);
        const gui::GraphPanelModel restored = gui::buildGraphPanelModel(editor, viewport, palette);
        std::printf("[5] restored: curves=%d channel rows=%d\n", isz(restored.curves), isz(restored.channels));
        CHECK(restored.curves.size() == model.curves.size());
        CHECK(restored.channels.size() == model.channels.size());
    }

    // -------------------------------------------------------------------------
    // 6. empty state
    {
        editor.setTargets({});
        const gui::GraphPanelModel emptyModel = gui::buildGraphPanelModel(editor, viewport, palette);
        std::printf("[6] no targets: emptyState=\"%s\" curves=%d channel rows=%d isEmpty=%d\n",
                    qUtf8Printable(emptyModel.emptyState),
                    isz(emptyModel.curves), isz(emptyModel.channels), int(emptyModel.isEmpty()));

        CHECK(!emptyModel.emptyState.isEmpty());
        CHECK(emptyModel.curves.isEmpty());
        CHECK(emptyModel.channels.isEmpty());
        CHECK(emptyModel.isEmpty());
        CHECK(emptyModel.selectionCount == 0);

        editor.setTargets({ &nodeA, &nodeB });
        const gui::GraphPanelModel again = gui::buildGraphPanelModel(editor, viewport, palette);
        std::printf("[6] targets restored: emptyState=\"%s\" curves=%d\n", qUtf8Printable(again.emptyState),
                    isz(again.curves));
        CHECK(again.emptyState.isEmpty());
        CHECK(again.curves.size() == pairs.size());
    }

    // -------------------------------------------------------------------------
    // 7. playhead
    {
        animator.setFrame(core::Frame(37));
        const gui::GraphPanelModel moved = gui::buildGraphPanelModel(editor, viewport, palette);
        const int expectedX = scale.pixelX(editor.currentFrame());

        std::printf("[7] document frame=%d  editor.currentFrame()=%d  project.currentTimeInfo().frame=%d  "
                    "playheadX=%d  scale.pixelX(currentFrame)=%d\n",
                    animator.currentFrame().get(), editor.currentFrame().get(),
                    project.currentTimeInfo().frame.get(), moved.playheadX, expectedX);

        CHECK(editor.currentFrame().get() == 37);
        CHECK(project.currentTimeInfo().frame.get() == 37);
        CHECK(moved.playheadX == expectedX);
        CHECK(moved.playheadX == scale.pixelX(core::Frame(37)));
        CHECK(model.plot.left() <= moved.playheadX && moved.playheadX <= model.plot.right());

        // THE PILL'S TEXT IS THE RULER'S OWN STRING: the frame written with
        // core::TimeFormat::frameToString and the editor's own format setting - the
        // same call every frame label above is checked against, so the two views
        // cannot name one frame two ways.
        const core::TimeFormat format(editor.frameRange(), editor.fps());
        const QString expectedText = format.frameToString(37, editor.timeFormatType());
        std::printf(
            "[7] playheadText=\"%s\" expected=\"%s\"  playheadPill=(%d,%d %dx%d)  rectangleBand=(%d,%d %dx%d)"
            "  playheadLineTop=%d\n",
            qUtf8Printable(moved.playheadText), qUtf8Printable(expectedText), moved.playheadPill.left(),
            moved.playheadPill.top(), moved.playheadPill.width(), moved.playheadPill.height(),
            moved.rulerBand.left(), moved.rulerBand.top(), moved.rulerBand.width(), moved.rulerBand.height(),
            moved.playheadLineTop
        );
        CHECK(moved.playheadText == expectedText);
        CHECK(!moved.playheadPill.isEmpty());
        CHECK(moved.playheadPill.top() == moved.rulerBand.top() + gui::frameRuler::kPillTop);
        CHECK(moved.playheadPill.height() == gui::frameRuler::kNumberHeight);
        CHECK(moved.playheadPill.bottom() < moved.playheadLineTop);
        CHECK(moved.playheadLineTop == moved.rulerBand.top() + gui::frameRuler::kLineTop);
        // THE PLAYHEAD SPANS BOTH SECTIONS: its line begins in the ruler's pill column
        // (under the pill) and crosses the section boundary into the plot, which
        // starts BELOW the band. The drawing layer inks the band's rows outside the
        // curve clip and the plot's rows inside it; this is the geometry both halves
        // share.
        CHECK(moved.playheadLineTop > moved.rulerBand.top());
        CHECK(moved.playheadLineTop < moved.plot.top());
        // The band's hairline is the last band row + 1; the scaler's margin rows sit
        // between it and the plot (see the band alignment rule in block [1]).
        CHECK(moved.plot.top() == moved.rulerBand.bottom() + 1 + ctrl::graph::Scaler::kMargin);

        // THE PILL IS MEASURED WITH THE FONT THE HOST HANDS IN. That is the timeline's
        // own measurement: TimeCursor builds its pill from
        // `QFontMetrics(font()).horizontalAdvance(mFrameText) + 2 * kPillPadding`
        // (TimeLineEditorWidget.cpp:54, :96), and the dock's stylesheet gives the
        // timeline's ruler and the panel's host the same widget font. QFont() is the
        // APPLICATION default font, which the stylesheet does not apply to the widgets
        // - the shipped locale sheet is 8pt while QFont() keeps the platform's 9pt, so
        // a QFont()-measured pill came out 2px wider than TimeCursor's on a 3-digit
        // frame and its left edge 1px left (the parity probe's 283+30 against 282+32).
        // The same integer rule the timeline uses places it: `left = x - width / 2`.
        {
            QFont hostFont;
            hostFont.setPixelSize(17); // deliberately not QFont()'s size: the reading must follow it
            const QFontMetrics hostMetrics(hostFont);
            const gui::GraphPanelModel measured =
                gui::buildGraphPanelModel(editor, viewport, palette, 0, gui::GraphPanelHost::Dock, hostFont);
            const int hostWidth =
                hostMetrics.horizontalAdvance(measured.playheadText) + 2 * gui::frameRuler::kPillPadding;
            const int defaultWidth =
                QFontMetrics(QFont()).horizontalAdvance(moved.playheadText) + 2 * gui::frameRuler::kPillPadding;
            std::printf(
                "[7] pill measured with a 17px host font: width=%d (QFontMetrics=%d, left=%d, playheadX=%d,"
                " x - width/2=%d); the default QFont() box is %d wide (QFontMetrics=%d)\n",
                measured.playheadPill.width(), hostWidth, measured.playheadPill.left(), measured.playheadX,
                measured.playheadX - hostWidth / 2, moved.playheadPill.width(), defaultWidth
            );
            CHECK(measured.playheadText == expectedText);
            CHECK(measured.playheadPill.width() == hostWidth);
            CHECK(measured.playheadPill.left() == measured.playheadX - hostWidth / 2);
            CHECK(measured.playheadPill.height() == gui::frameRuler::kNumberHeight);
            // The no-font call keeps its documented default, so a headless caller's
            // geometry is unchanged by the new parameter.
            CHECK(moved.playheadPill.width() == defaultWidth);
            CHECK(moved.playheadPill.left() == moved.playheadX - defaultWidth / 2);
        }

        // ...AND NO RULER NUMBER IS DRAWN UNDER IT: every label whose centre falls
        // inside the pill's half-width is dropped, so the pill's number is the only
        // one read in its column (the timeline's own rule).
        int underPill = 0;
        for (const gui::PanelLabel& label : moved.frameLabels) {
            if (qAbs(label.rect.center().x() - moved.playheadX) < moved.playheadPill.width() / 2)
                ++underPill;
        }
        std::printf("[7] frame labels under the pill=%d\n", underPill);
        CHECK(underPill == 0);

        // A NEGATIVE FRAME IS A REAL FRAME, not the no-project sentinel. A
        // project's range may start before 0 and the ruler seek reaches there
        // exactly as the timeline's own does, so the model must place the
        // playhead at the pointer's negative frame - its gate is "a project is
        // attached", never the frame's sign.
        //
        // AN EDITOR OF ITS OWN, as the 1-digit-label fixture below does: this
        // window is deliberately biased to the negative side, and the shared
        // `editor` is the fixture every other block in this file measures through.
        // Pushing a shared window into it (and then clearing it) would leave its
        // ladder-seeded axis different from the test's `scale` oracle, so the
        // window never touches `editor` at all.
        {
            ctrl::GraphEditor negativeEditor(project, logger);
            negativeEditor.scale().setRect(panel);
            negativeEditor.setTargets({ &nodeA, &nodeB });
            negativeEditor.setFrameWindow(-12.0, 12.0); // a window that really spans frames before 0

            animator.setFrame(core::Frame(-3));
            ctrl::graph::Scaler negativeScale = negativeEditor.scale();
            const gui::GraphPanelModel negative =
                gui::buildGraphPanelModel(negativeEditor, viewport, palette);
            const core::TimeFormat negativeFormat(negativeEditor.frameRange(), negativeEditor.fps());
            const QString negativeText = negativeFormat.frameToString(-3, negativeEditor.timeFormatType());
            const int negativeX = negativeScale.pixelX(core::Frame(-3));
            std::printf("[7] negative frame: currentFrame()=%d  scale.pixelX(-3)=%d  playheadX=%d  "
                        "playheadText=\"%s\"  pill=%dx%d\n",
                        negativeEditor.currentFrame().get(), negativeX, negative.playheadX,
                        qUtf8Printable(negative.playheadText), negative.playheadPill.width(),
                        negative.playheadPill.height());
            CHECK(negativeEditor.currentFrame().get() == -3);
            CHECK(negative.playheadX == negativeX);
            CHECK(negative.playheadX > 0); // inside the panel: this is a PLACED playhead
            CHECK(negative.playheadText == negativeText);
            CHECK(!negative.playheadText.isEmpty());
            CHECK(!negative.playheadPill.isEmpty());
            CHECK(negative.playheadLineTop >= negative.rulerBand.top());

            // ...and it MOVES with the frame, the seek's whole contract: a
            // further-left frame is placed further left, still outside the
            // sentinel's reach.
            animator.setFrame(core::Frame(-8));
            const gui::GraphPanelModel further =
                gui::buildGraphPanelModel(negativeEditor, viewport, palette);
            CHECK(further.playheadX == negativeScale.pixelX(core::Frame(-8)));
            CHECK(further.playheadX < negative.playheadX);

            animator.setFrame(core::Frame(37)); // the document's frame is shared state
        }

        // No project: no frame to place, no pill, no text.
        editor.setProject(nullptr);
        const gui::GraphPanelModel orphaned = gui::buildGraphPanelModel(editor, viewport, palette);
        std::printf("[7] no project: editor.currentFrame()=%d  playheadX=%d  playheadText=\"%s\"  emptyState=\"%s\"\n",
                    editor.currentFrame().get(), orphaned.playheadX, qUtf8Printable(orphaned.playheadText),
                    qUtf8Printable(orphaned.emptyState));
        CHECK(editor.currentFrame().get() == -1);
        CHECK(orphaned.playheadX == -1);
        CHECK(orphaned.playheadText.isEmpty());
        CHECK(orphaned.playheadPill.isEmpty());
        CHECK(!orphaned.emptyState.isEmpty());
        CHECK(orphaned.curves.isEmpty());
    }

    // -------------------------------------------------------------------------
    // 16. THE ACTIVE-RANGE RECESSION AND THE TEMPORARY PLAYBACK WINDOW OVERLAY.
    // The timeline's ruler recesses every tick and number outside its ACTIVE range
    // (the project frame range, or the play window while it is active) and overlays
    // the window as a translucent band + bracket stems + label pills. The graph's
    // ruler is the same ruler, so the model must publish (a) inside/outside per tick
    // and per frame label - a CLASSIFICATION, no ink - and (b) the overlay's exact
    // geometry in the ruler band. The oracle is the editor's own scalar and
    // ctrl::TimeLineEditor's playbackRange rule.
    {
        editor.setProject(&project);
        editor.setTargets({ &nodeA, &nodeB });
        animator.setFrame(core::Frame(37));

        const core::TimeFormat timeFormat(editor.frameRange(), editor.fps());
        const QFontMetrics metrics{QFont()};

        // ---- (a) no window: the project frame range is the active range ----
        editor.setPlayWindow(ctrl::TimeLineEditor::PlayWindow());
        const gui::GraphPanelModel plain = gui::buildGraphPanelModel(editor, viewport, palette);
        const util::Range projectRange = editor.frameRange();
        CHECK(!plain.playWindow.active);
        CHECK(plain.playWindow.band.isEmpty());
        CHECK(plain.playWindow.startStem.isEmpty() && plain.playWindow.endStem.isEmpty());
        CHECK(plain.playWindow.startPill.isEmpty() && plain.playWindow.endPill.isEmpty());
        CHECK(plain.playWindow.mergedPill.isEmpty());

        CHECK(plain.frameTickInActiveRange.size() == plain.frameTickX.size());
        int plainOutsideTicks = 0;
        int plainTickMismatch = 0;
        for (int i = 0; i < plain.frameTickX.size(); ++i) {
            const bool expected = projectRange.contains(scale.frameAt(plain.frameTickX.at(i)).get());
            if (!expected)
                ++plainOutsideTicks;
            if (plain.frameTickInActiveRange.at(i) != expected)
                ++plainTickMismatch;
        }
        int plainLabelMismatch = 0;
        for (const gui::PanelLabel& label : plain.frameLabels) {
            const bool expected = label.endFrame >= 0
                ? projectRange.contains(label.frame) && projectRange.contains(label.endFrame)
                : projectRange.contains(label.frame);
            if (label.inActiveRange != expected)
                ++plainLabelMismatch;
        }
        std::printf("[16] no window: active=%d..%d ticks=%d outside=%d tick_mismatches=%d label_mismatches=%d\n",
                    projectRange.min(), projectRange.max(), isz(plain.frameTickX), plainOutsideTicks,
                    plainTickMismatch, plainLabelMismatch);
        CHECK(plainTickMismatch == 0);
        CHECK(plainLabelMismatch == 0);
        CHECK(plainOutsideTicks > 0); // the window of this viewport reaches outside 1..120

        // ---- (b) an active window: the overlay and a window-based active range ----
        ctrl::TimeLineEditor::PlayWindow window;
        window.set(10, 100);
        editor.setPlayWindow(window);
        const gui::GraphPanelModel withWindow = gui::buildGraphPanelModel(editor, viewport, palette);
        const int x0 = scale.pixelX(core::Frame(window.start));
        const int x1 = scale.pixelX(core::Frame(window.end));
        const int pillTop = withWindow.rulerBand.top() + gui::frameRuler::kPillTop;
        const int stemHeight = gui::frameRuler::kBandHeight - gui::frameRuler::kPillTop;
        std::printf(
            "[16] window %d..%d: x=%d..%d band=(%d,%d %dx%d) stems=(%d,%d) pills=(%d,%d %dx%d)+(%d,%d %dx%d) "
            "texts=\"%s\"/\"%s\"\n",
            window.start, window.end, x0, x1, withWindow.playWindow.band.left(),
            withWindow.playWindow.band.top(), withWindow.playWindow.band.width(),
            withWindow.playWindow.band.height(), withWindow.playWindow.startStem.left(),
            withWindow.playWindow.endStem.left(), withWindow.playWindow.startPill.left(),
            withWindow.playWindow.startPill.top(), withWindow.playWindow.startPill.width(),
            withWindow.playWindow.startPill.height(), withWindow.playWindow.endPill.left(),
            withWindow.playWindow.endPill.top(), withWindow.playWindow.endPill.width(),
            withWindow.playWindow.endPill.height(), qUtf8Printable(withWindow.playWindow.startText),
            qUtf8Printable(withWindow.playWindow.endText)
        );
        CHECK(withWindow.playWindow.active);
        // The band is INSIDE the ruler band and exactly the band's height.
        CHECK(withWindow.playWindow.band.top() == withWindow.rulerBand.top());
        CHECK(withWindow.playWindow.band.height() == gui::frameRuler::kBandHeight);
        CHECK(withWindow.playWindow.band.left() == std::max(x0, withWindow.rulerBand.left()));
        CHECK(withWindow.playWindow.band.right() == std::min(x1, withWindow.rulerBand.right()));
        // The two stems: 2px, from the label row to the band's last row, on the ticks
        // of the window's frames (the end bracket on frame `end`'s own tick).
        CHECK(withWindow.playWindow.startStem.left() == x0);
        CHECK(withWindow.playWindow.startStem.width() == 2);
        CHECK(withWindow.playWindow.startStem.top() == pillTop);
        CHECK(withWindow.playWindow.startStem.height() == stemHeight);
        CHECK(withWindow.playWindow.endStem.left() == x1 - 1);
        CHECK(withWindow.playWindow.endStem.width() == 2);
        CHECK(withWindow.playWindow.endStem.top() == pillTop);
        CHECK(withWindow.playWindow.endStem.height() == stemHeight);
        // The label arms ride the stems' inner faces with the ruler's own text.
        CHECK(withWindow.playWindow.startPill.left() == x0 + 2);
        CHECK(withWindow.playWindow.endPill.right() == x1 - 2);
        CHECK(withWindow.playWindow.startPill.top() == pillTop);
        CHECK(withWindow.playWindow.endPill.top() == pillTop);
        CHECK(withWindow.playWindow.startPill.height() == gui::frameRuler::kNumberHeight);
        CHECK(withWindow.playWindow.endPill.height() == gui::frameRuler::kNumberHeight);
        CHECK(withWindow.playWindow.startText == timeFormat.frameToString(window.start, editor.timeFormatType()));
        CHECK(withWindow.playWindow.endText == timeFormat.frameToString(window.end, editor.timeFormatType()));
        CHECK(withWindow.playWindow.mergedPill.isEmpty());
        CHECK(withWindow.playWindow.mergedText.isEmpty());

        // The active range is now the window: both vectors classify against it.
        CHECK(withWindow.frameTickInActiveRange.size() == withWindow.frameTickX.size());
        int windowOutsideTicks = 0;
        int windowInsideTicks = 0;
        int windowTickMismatch = 0;
        for (int i = 0; i < withWindow.frameTickX.size(); ++i) {
            const int frame = scale.frameAt(withWindow.frameTickX.at(i)).get();
            const bool expected = window.start <= frame && frame <= window.end;
            if (expected)
                ++windowInsideTicks;
            else
                ++windowOutsideTicks;
            if (withWindow.frameTickInActiveRange.at(i) != expected)
                ++windowTickMismatch;
        }
        int windowLabelMismatch = 0;
        for (const gui::PanelLabel& label : withWindow.frameLabels) {
            const bool expected = label.endFrame >= 0
                ? window.start <= label.frame && label.frame <= window.end && window.start <= label.endFrame
                    && label.endFrame <= window.end
                : window.start <= label.frame && label.frame <= window.end;
            if (label.inActiveRange != expected)
                ++windowLabelMismatch;
        }
        std::printf("[16] window active range: inside_ticks=%d outside_ticks=%d tick_mismatches=%d"
                    " label_mismatches=%d\n",
                    windowInsideTicks, windowOutsideTicks, windowTickMismatch, windowLabelMismatch);
        CHECK(windowTickMismatch == 0);
        CHECK(windowLabelMismatch == 0);
        CHECK(windowInsideTicks > 0);
        CHECK(windowOutsideTicks > 0);

        // ---- (c) a narrow window: the two arms collide into ONE centred label ----
        window.set(20, 21);
        editor.setPlayWindow(window);
        const gui::GraphPanelModel merged = gui::buildGraphPanelModel(editor, viewport, palette);
        const int mx0 = scale.pixelX(core::Frame(window.start));
        const int mx1 = scale.pixelX(core::Frame(window.end));
        const QString expectedMerged =
            timeFormat.frameToString(window.start, editor.timeFormatType()) + QString::fromUtf8("\u2013")
            + timeFormat.frameToString(window.end, editor.timeFormatType());
        const int expectedMergedWidth = metrics.horizontalAdvance(expectedMerged) + 2 * gui::frameRuler::kPillPadding;
        std::printf("[16] merged window %d..%d: x=%d..%d merged=(%d,%d %dx%d) text=\"%s\"\n", window.start,
                    window.end, mx0, mx1, merged.playWindow.mergedPill.left(), merged.playWindow.mergedPill.top(),
                    merged.playWindow.mergedPill.width(), merged.playWindow.mergedPill.height(),
                    qUtf8Printable(merged.playWindow.mergedText));
        CHECK(merged.playWindow.active);
        CHECK(!merged.playWindow.mergedPill.isEmpty());
        CHECK(merged.playWindow.mergedText == expectedMerged);
        CHECK(merged.playWindow.mergedPill.width() == expectedMergedWidth);
        CHECK(merged.playWindow.mergedPill.top() == merged.rulerBand.top() + gui::frameRuler::kPillTop);
        CHECK(merged.playWindow.mergedPill.height() == gui::frameRuler::kNumberHeight);
        CHECK(merged.playWindow.mergedPill.left() == (mx0 + mx1) / 2 - expectedMergedWidth / 2);
        CHECK(merged.playWindow.startPill.isEmpty() && merged.playWindow.endPill.isEmpty());
        // The brackets are still there: a merged label covers the arms, not the stems.
        CHECK(!merged.playWindow.startStem.isEmpty() && !merged.playWindow.endStem.isEmpty());

        // The editor goes back to its no-window state for the blocks after this one.
        editor.setPlayWindow(ctrl::TimeLineEditor::PlayWindow());
        CHECK(!editor.playWindow().active());
        CHECK(editor.activeRange() == editor.frameRange());
    }


    // -------------------------------------------------------------------------
    // 10. tangent handles: one PanelHandle pair per Custom segment, in segment
    // order, matching the editor's own grab mapping (Curve::handle through the
    // scaler) - so a grab at a published dot is the drag the editor performs.
    {
        // Block [7] left the editor with no project (the no-frame case); the
        // document and the targets come back here, because a model needs both.
        editor.setProject(&project);
        editor.setTargets({ &nodeA, &nodeB });

        // A Custom easing on the first Move key: its one segment (frame 0 -> 20)
        // then exposes two tangent handles, on BOTH Move components (x and y).
        util::Easing::Param custom;
        custom.type = util::Easing::Type_Custom;
        custom.cubicBezier.x1 = 0.4f;
        custom.cubicBezier.y1 = 0.1f;
        custom.cubicBezier.x2 = 0.6f;
        custom.cubicBezier.y2 = 0.9f;
        for (int component = 0; component < core::MoveKey::kComponents; ++component) {
            move0->data().easing(component) = custom;
        }

        const gui::GraphPanelModel handled = gui::buildGraphPanelModel(editor, viewport, palette);
        ctrl::graph::Scaler oracle = editor.scale();
        oracle.setRect(panel);

        int totalHandles = 0;
        int customSegments = 0;
        int badHandles = 0;
        int misplacedDots = 0;
        for (const gui::PanelCurve& curve : handled.curves) {
            const ctrl::graph::Curve source(*curve.node->timeLine(), curve.channel);
            const int segments = source.segmentCount();
            int expected = 0;
            for (int s = 0; s < segments; ++s) {
                if (source.isCustom(source.segment(s)))
                    expected += 2;
            }
            if (expected > 0)
                ++customSegments;
            totalHandles += curve.handles.size();
            if (curve.handles.size() != expected)
                ++badHandles;

            int index = 0;
            for (int s = 0; s < segments; ++s) {
                const ctrl::graph::Curve::Segment segment = source.segment(s);
                if (!source.isCustom(segment))
                    continue;
                const core::TimeKey* owners[2] = { segment.from, segment.to };
                const float ownerValues[2] = { segment.fromValue, segment.toValue };
                for (int h = 0; h < 2 && index < curve.handles.size(); ++h, ++index) {
                    const gui::PanelHandle& handle = curve.handles.at(index);
                    const QPointF expectedDot = ctrl::graph::handlePoint(oracle, segment, h);
                    const QPointF expectedBase(
                        oracle.pixelXOfFrame(double(owners[h]->frame())), double(oracle.pixelY(ownerValues[h]))
                    );
                    if (handle.segment != s || handle.index != h || handle.radius != ctrl::graph::kHandleRadius)
                        ++badHandles;
                    if (handle.dot != expectedDot || handle.base != expectedBase)
                        ++misplacedDots;
                }
            }
        }
        std::printf("[10] curves=%d with_custom_segments=%d handles=%d  handle mismatches=%d  misplaced dots/bases=%d\n",
                    isz(handled.curves), customSegments, totalHandles, badHandles, misplacedDots);
        // Move x and Move y both carry the one Custom segment (the easing lives on
        // the key, and a key shapes every component of its type at once).
        CHECK(customSegments == 2);
        CHECK(totalHandles == 4);
        CHECK(badHandles == 0);
        CHECK(misplacedDots == 0);
        // A STORED pair is not a ghost pair: `ghost` is the whole distinction, and a
        // Custom handle drawn as a derived (smaller, channel-hue) dot would claim the
        // document holds no tangent.
        int storedGhosts = 0;
        for (const gui::PanelCurve& curve : handled.curves) {
            const ctrl::graph::Curve source(*curve.node->timeLine(), curve.channel);
            for (const gui::PanelHandle& handle : curve.handles) {
                if (0 <= handle.segment && handle.segment < source.segmentCount() &&
                    source.isCustom(source.segment(handle.segment)) && handle.ghost)
                    ++storedGhosts;
            }
        }
        CHECK(storedGhosts == 0);

        // A curve with NO Custom and NO Auto segment publishes no handle at all: the
        // pairs are keyed to the easing, not to "a curve". (The other channels here
        // carry the default Linear easing, which exposes no control point to drag.)
        int nonHandleCurves = 0;
        int handlesOnNonHandleCurves = 0;
        for (const gui::PanelCurve& curve : handled.curves) {
            const ctrl::graph::Curve source(*curve.node->timeLine(), curve.channel);
            bool anyEditable = false;
            for (int s = 0; s < source.segmentCount(); ++s) {
                const ctrl::graph::Curve::Segment segment = source.segment(s);
                if (source.isCustom(segment) || !source.ghostHandles(*segment.from).isEmpty())
                    anyEditable = true;
            }
            if (!anyEditable) {
                ++nonHandleCurves;
                handlesOnNonHandleCurves += curve.handles.size();
            }
        }
        std::printf("[10] curves with no Custom and no Auto segment=%d, handles they published=%d\n",
                    nonHandleCurves, handlesOnNonHandleCurves);
        CHECK(nonHandleCurves > 0);
        CHECK(handlesOnNonHandleCurves == 0);

        // ---- and now the AUTO segment: the DERIVED pair, which the document does
        // NOT hold. It is published as ghost handles, and its x IS the third and two
        // thirds of the segment's own frame span (the design's automatic handle, the
        // whole reason the pair can be derived at all) while only the two y values
        // come from the key's neighbours. The dot goes through the SAME mapping a
        // stored one does - ctrl::graph::handlePointAt - so the two can never drift
        // apart, and a drag on a ghost dot would convert the key (the editor's own
        // job; the model only has to say WHICH kind of pair this is).
        util::Easing::Param automatic;
        automatic.type = util::Easing::Type_Custom;
        automatic.handleIn = util::Easing::HandleMode_Auto;
        automatic.handleOut = util::Easing::HandleMode_Auto;
        for (int component = 0; component < core::MoveKey::kComponents; ++component) {
            move0->data().easing(component) = automatic;
            move1->data().easing(component) = automatic;
        }

        const gui::GraphPanelModel ghosted = gui::buildGraphPanelModel(editor, viewport, palette);
        ctrl::graph::Scaler ghostOracle = editor.scale();
        ghostOracle.setRect(panel);

        int ghostPairs = 0;
        int ghostHandles = 0;
        int badGhosts = 0;
        int badGhostX = 0;
        int ghostDotsOffMapping = 0;
        int nonGhostOnAuto = 0;
        for (const gui::PanelCurve& curve : ghosted.curves) {
            const ctrl::graph::Curve source(*curve.node->timeLine(), curve.channel);
            if (!source.isCustom(source.segment(0)) && source.ghostHandles(*source.segment(0).from).isEmpty())
                continue; // not one of the Auto channels
            if (curve.handles.size() != 2) {
                ++badGhosts;
                continue;
            }
            ++ghostPairs;
            const ctrl::graph::Curve::Segment segment = source.segment(0);
            const QVector<QPointF> derived = source.ghostHandles(*segment.from);
            const core::TimeKey* owners[2] = { segment.from, segment.to };
            for (int h = 0; h < 2 && h < curve.handles.size(); ++h) {
                const gui::PanelHandle& handle = curve.handles.at(h);
                ++ghostHandles;
                if (!handle.ghost || handle.segment != 0 || handle.index != h ||
                    handle.radius != ctrl::graph::kHandleRadius)
                    ++badGhosts;
                if (!handle.ghost)
                    ++nonGhostOnAuto;
                // The base is STILL the key the control belongs to: control 0 leaves
                // the from key, control 1 arrives at the to key.
                const QPointF expectedBase(
                    ghostOracle.pixelXOfFrame(double(owners[h]->frame())),
                    double(ghostOracle.pixelY(h == 0 ? segment.fromValue : segment.toValue))
                );
                if (handle.base != expectedBase)
                    ++badGhosts;
                // The derived x is the THIRD POINT of the span, exactly.
                const double expectedX = h == 0 ? 1.0 / 3.0 : 2.0 / 3.0;
                if (std::fabs(derived.at(h).x() - expectedX) > 1e-9)
                    ++badGhostX;
                if (std::fabs(handle.dot.x() - ghostOracle.pixelXOfFrame(
                        double(segment.from->frame()) + expectedX * double(segment.to->frame() - segment.from->frame())
                    )) > 0.5)
                    ++badGhostX;
                // ...and the dot is that normalized point through the shared mapping.
                if (handle.dot != ctrl::graph::handlePointAt(ghostOracle, segment, derived.at(h)))
                    ++ghostDotsOffMapping;
            }
        }
        std::printf("[10] Auto segments: ghost pairs=%d handles=%d  mismatches=%d  x off the third point=%d  "
                    "dots off handlePointAt=%d  stored-looking ghosts=%d\n",
                    ghostPairs, ghostHandles, badGhosts, badGhostX, ghostDotsOffMapping, nonGhostOnAuto);
        CHECK(ghostPairs == 2); // Move x and Move y again: one key, every component
        CHECK(ghostHandles == 4);
        CHECK(badGhosts == 0);
        CHECK(badGhostX == 0);
        CHECK(ghostDotsOffMapping == 0);
        CHECK(nonGhostOnAuto == 0);

        // ---- 10c. A SEGMENT WITH ONE DERIVED SIDE MUST STILL PUBLISH ITS STORED
        // SIDE AS STORED. The kind is a per-SIDE fact: the model used to ask
        // Curve::isCustom ("both sides stored"), so a mixed segment fell into the
        // ghost branch wholesale - BOTH handles were marked derived AND the stored
        // pair was never read. Publish per side instead: the derived side is the
        // effective control point (a ghost), the stored one is handlePoint().
        // Component 0 derives its OUTGOING side, component 1 its INCOMING one, so
        // both directions are pinned by one build.
        {
            // The FROM key carries the outgoing mode and the stored x1/y1; the TO
            // key carries the incoming mode (its x2/y2 still lives in the FROM
            // param). Component 0 derives the OUTGOING side, component 1 the
            // INCOMING one.
            util::Easing::Param outgoingDerived = custom;
            outgoingDerived.type = util::Easing::Type_Custom;
            outgoingDerived.handleOut = util::Easing::HandleMode_Auto;
            outgoingDerived.handleIn = util::Easing::HandleMode_Aligned;
            util::Easing::Param storedOutgoing = custom;
            storedOutgoing.type = util::Easing::Type_Custom;
            storedOutgoing.handleOut = util::Easing::HandleMode_Free;
            storedOutgoing.handleIn = util::Easing::HandleMode_Aligned;
            util::Easing::Param incomingDerived = custom;
            incomingDerived.type = util::Easing::Type_Custom;
            incomingDerived.handleOut = util::Easing::HandleMode_Aligned;
            incomingDerived.handleIn = util::Easing::HandleMode_Auto;

            move0->data().easing(0) = outgoingDerived;
            move1->data().easing(0) = custom; // incoming side stored (Aligned)
            move0->data().easing(1) = storedOutgoing;
            move1->data().easing(1) = incomingDerived;

            const gui::GraphPanelModel mixed = gui::buildGraphPanelModel(editor, viewport, palette);
            ctrl::graph::Scaler mixedOracle = editor.scale();
            mixedOracle.setRect(panel);

            int mixedPairs = 0;
            int ghostSides = 0;
            int storedSides = 0;
            int badKind = 0;
            int badStoredDot = 0;
            int badDerivedDot = 0;
            int badSize = 0;
            for (const gui::PanelCurve& curve : mixed.curves) {
                const ctrl::graph::Curve source(*curve.node->timeLine(), curve.channel);
                if (curve.channel.type() != core::TimeKeyType_Move || source.isEmpty())
                    continue;
                const ctrl::graph::Curve::Segment seg = source.segment(0);
                if (seg.from != move0)
                    continue; // another layer's Move channel: not the edited segment
                if (curve.handles.size() != 2) {
                    ++badSize;
                    continue;
                }
                ++mixedPairs;
                QPointF pair[2];
                if (!source.effectiveHandles(seg, pair))
                    ++badKind;
                for (int h = 0; h < 2; ++h) {
                    const gui::PanelHandle& handle = curve.handles.at(h);
                    const bool derived = ctrl::graph::Curve::derivesHandle(seg, h);
                    if (handle.ghost != derived || handle.index != h || handle.segment != 0)
                        ++badKind;
                    if (derived) {
                        ++ghostSides;
                        if (handle.dot != ctrl::graph::handlePointAt(mixedOracle, seg, pair[h]))
                            ++badDerivedDot;
                    } else {
                        ++storedSides;
                        if (handle.dot != ctrl::graph::handlePoint(mixedOracle, seg, h))
                            ++badStoredDot;
                    }
                }
            }
            std::printf("[10c] mixed-side segments: pairs=%d ghost sides=%d stored sides=%d  "
                        "wrong pair size=%d  wrong kind=%d  stored dot off handlePoint=%d  "
                        "ghost dot off handlePointAt=%d\n",
                        mixedPairs, ghostSides, storedSides, badSize, badKind, badStoredDot,
                        badDerivedDot);
            CHECK(mixedPairs == 2);   // Move x and Move y
            CHECK(ghostSides == 2);   // exactly one derived side per segment
            CHECK(storedSides == 2);  // ... and the stored side is NOT dropped
            CHECK(badSize == 0);
            CHECK(badKind == 0);
            CHECK(badStoredDot == 0);
            CHECK(badDerivedDot == 0);
        }

        // Back to the Custom pair, so the blocks after this one measure the geometry
        // they were written against (the trace handle is the one thing that moved).
        for (int component = 0; component < core::MoveKey::kComponents; ++component) {
            move0->data().easing(component) = custom;
            move1->data().easing(component) = custom;
        }
        const gui::GraphPanelModel restoredHandles = gui::buildGraphPanelModel(editor, viewport, palette);
        int restoredGhosts = 0;
        for (const gui::PanelCurve& curve : restoredHandles.curves) {
            for (const gui::PanelHandle& handle : curve.handles)
                restoredGhosts += int(handle.ghost);
        }
        std::printf("[10] after restoring Custom: handles=%d  of them ghosts=%d\n",
                    [&restoredHandles] {
                        int n = 0;
                        for (const gui::PanelCurve& c : restoredHandles.curves)
                            n += isz(c.handles);
                        return n;
                    }(), restoredGhosts);
        CHECK(restoredGhosts == 0);
    }

    // -------------------------------------------------------------------------
    // 10b. AND ONLY THE DRAWING LAYER'S OWN ROW CARRIES THEM. The pairs exist to be
    // INKED, and the panel inks a curve's tangent handles for the FOCUSED channel
    // alone (with no focus, for every curve) - so a pair built for any other row is
    // work nobody reads, and half of that work is the expensive kind: an Auto
    // segment's pair is DERIVED by Curve::ghostHandles from the key's neighbours.
    // This pins the publication rule on the model's own rows (PanelChannelRow::
    // focused): with a focus, the focused row keeps its derived pair and every other
    // row publishes NO handle; clearing the focus puts them all back. Nothing else
    // moves - not the curves, not the keys.
    {
        editor.setProject(&project);
        editor.setTargets({ &nodeA, &nodeB });

        // The Auto easing again: its pair is the DERIVED one, so the rows whose
        // handles are dropped are exactly the rows that would have recomputed a
        // tangent nobody inks.
        util::Easing::Param automatic;
        automatic.type = util::Easing::Type_Custom;
        automatic.handleIn = util::Easing::HandleMode_Auto;
        automatic.handleOut = util::Easing::HandleMode_Auto;
        for (int component = 0; component < core::MoveKey::kComponents; ++component) {
            move0->data().easing(component) = automatic;
            move1->data().easing(component) = automatic;
        }

        const gui::GraphPanelModel unfocused = gui::buildGraphPanelModel(editor, viewport, palette);
        int unfocusedPairs = 0;
        for (const gui::PanelCurve& curve : unfocused.curves)
            unfocusedPairs += isz(curve.handles);
        CHECK(unfocusedPairs == 4); // no focus: the two Auto rows publish their pairs (as in [10])

        const ctrl::graph::Channel moveX(core::TimeKeyType_Move, 0); // Move X of target A
        editor.setFocusedChannel(nodeA, moveX);
        CHECK(editor.hasFocus());

        const gui::GraphPanelModel focused = gui::buildGraphPanelModel(editor, viewport, palette);
        int focusedRows = 0;
        for (const gui::PanelChannelRow& row : focused.channels)
            focusedRows += int(row.focused);

        int focusedCurveHandles = 0;
        int focusedCurveGhosts = 0;
        int otherCurves = 0;
        int handlesOnOtherCurves = 0;
        for (const gui::PanelCurve& curve : focused.curves) {
            if (curve.node == &nodeA && curve.channel == moveX) {
                focusedCurveHandles = isz(curve.handles);
                for (const gui::PanelHandle& handle : curve.handles)
                    focusedCurveGhosts += int(handle.ghost);
                continue;
            }
            ++otherCurves;
            handlesOnOtherCurves += isz(curve.handles);
        }
        std::printf(
            "[10b] focus on A/Move-x: rows_focused=%d  focused curve handles=%d (ghosts=%d)  other curves=%d"
            "  handles they published=%d  (with no focus: %d handles over %d curves)\n",
            focusedRows, focusedCurveHandles, focusedCurveGhosts, otherCurves, handlesOnOtherCurves,
            unfocusedPairs, isz(focused.curves)
        );
        CHECK(focusedRows == 1);                // the focus is on the panel's own rows
        CHECK(otherCurves > 0);                 // the fixture's premise: there are other rows
        CHECK(focusedCurveHandles == 2);        // the row the layer inks keeps its pair...
        CHECK(focusedCurveGhosts == 2);
        CHECK(handlesOnOtherCurves == 0);       // ...and no other row pays for a pair nobody inks
        CHECK(focused.curves.size() == unfocused.curves.size()); // the curves themselves are untouched

        // Clearing the focus is the no-focus ink: every handle-bearing row publishes
        // again, and the count is exactly the one measured before the focus.
        editor.clearFocus();
        const gui::GraphPanelModel cleared = gui::buildGraphPanelModel(editor, viewport, palette);
        int clearedPairs = 0;
        for (const gui::PanelCurve& curve : cleared.curves)
            clearedPairs += isz(curve.handles);
        CHECK(!editor.hasFocus());
        CHECK(clearedPairs == unfocusedPairs);

        // Leave the document as section 10 left it: the trace handle is the only
        // thing this block moved.
        util::Easing::Param custom;
        custom.type = util::Easing::Type_Custom;
        custom.cubicBezier.x1 = 0.4f;
        custom.cubicBezier.y1 = 0.1f;
        custom.cubicBezier.x2 = 0.6f;
        custom.cubicBezier.y2 = 0.9f;
        for (int component = 0; component < core::MoveKey::kComponents; ++component) {
            move0->data().easing(component) = custom;
            move1->data().easing(component) = custom;
        }
    }

    // -------------------------------------------------------------------------
    // 11. the ruler's reserved band, and the live box-select marquee. Both are two
    // halves of one story the drawing layer cannot finish alone: the model RESERVES
    // the space a frame number is allowed to occupy, and it publishes the marquee a
    // drag is drawing. Neither is a rect the drawing layer may invent, so both are
    // pinned here on the editor's own numbers.
    {
        editor.setProject(&project);
        editor.setTargets({ &nodeA, &nodeB });
        const gui::GraphPanelModel ruled = gui::buildGraphPanelModel(editor, viewport, palette);

        // THE RULER'S BAND is the timeline's: a 22px strip at rulerBandTop whose
        // numbers sit in its top 14 rows and its 5px ticks rise from its last row. The
        // model must carry it, the band must sit ABOVE the plot (the scaler reserved
        // those rows, so the plot's own clip begins below them), and every
        // number's box must be in the band's number row - ABOVE the tick rows, as the
        // timeline draws them (the opposite of the old panel, which hung its numbers
        // below the ticks).
        const int kBandHeight = gui::frameRuler::kBandHeight;
        const int kTickLength = gui::frameRuler::kTickLength;
        int labels = 0;
        int insideBand = 0;
        int inNumberRow = 0;
        for (const gui::PanelLabel& label : ruled.frameLabels) {
            ++labels;
            if (ruled.rulerBand.contains(label.rect))
                ++insideBand;
            if (label.rect.top() == ruled.rulerBand.top() + gui::frameRuler::kNumberTop &&
                label.rect.bottom() < ruled.rulerBand.top() + kBandHeight - kTickLength)
                ++inNumberRow;
        }
        std::printf(
            "[11] plot=(%d,%d %dx%d) rulerBand=(%d,%d %dx%d) bandHeight=%d tickLength=%d frame_labels=%d"
            " inside_band=%d in_number_row=%d\n",
            ruled.plot.left(), ruled.plot.top(), ruled.plot.width(), ruled.plot.height(), ruled.rulerBand.left(),
            ruled.rulerBand.top(), ruled.rulerBand.width(), ruled.rulerBand.height(), kBandHeight, kTickLength,
            labels, insideBand, inNumberRow
        );
        // The precondition of the pixel gate's assertion 16 (which measures the same
        // rule on the drawn glyphs): the model has to carry labels at all, or the
        // reservation is unobservable. THE BAND IS ABOVE THE PLOT and the numbers are
        // inside the BAND (they used to be inside the plot): the plot's clip now
        // starts on the graph's first row, so a number that reached it would be cut.
        CHECK(!ruled.rulerBand.isEmpty());
        CHECK(ruled.rulerBand.top() == ruled.rulerBandTop);
        CHECK(ruled.rulerBand.top() == ruled.panel.top());
        // The band is as wide as the PANEL, not the plot: the frame mapping is the
        // timeline's, so the ruler covers the value column's top corner too.
        CHECK(ruled.rulerBand.left() == ruled.panel.left());
        CHECK(ruled.rulerBand.width() == ruled.panel.width());
        CHECK(ruled.rulerBand.height() == kBandHeight);
        CHECK(ruled.rulerBand.bottom() + 1 + ctrl::graph::Scaler::kMargin == ruled.plot.top());
        CHECK(!ruled.plot.intersects(ruled.rulerBand));
        // EVERY TICK IS THE SCALER'S PIXEL FOR ITS FRAME, and the ticks reach the
        // panel's own columns: the band is the ruler's whole width, so a tick at the
        // value column's top (x < plot.left()) is published, not dropped.
        int ticksLeftOfPlot = 0;
        int tickMismatch = 0;
        for (const int x : ruled.frameTickX) {
            if (scale.pixelX(scale.frameAt(x)) != x)
                ++tickMismatch;
            if (x < ruled.plot.left())
                ++ticksLeftOfPlot;
        }
        std::printf(
            "[11] ticks=%d  left of the plot=%d  pixel mismatches=%d\n", isz(ruled.frameTickX), ticksLeftOfPlot,
            tickMismatch
        );
        CHECK(tickMismatch == 0);
        CHECK(ruled.rulerBand.contains(ruled.valueColumn.left(), ruled.rulerBand.top()));
        CHECK(ruled.rulerBand.contains(ruled.valueColumn.right(), ruled.rulerBand.top()));
        CHECK(labels > 0);
        CHECK(insideBand == labels);
        CHECK(inNumberRow == labels);

        // No drag is live, so the marquee is EMPTY - which is the drawing layer's
        // whole signal to draw nothing.
        std::printf("[11] marquee with no drag live: (%d,%d %dx%d) empty=%d\n", ruled.marquee.left(),
                    ruled.marquee.top(), ruled.marquee.width(), ruled.marquee.height(), int(ruled.marquee.isEmpty()));
        CHECK(ruled.marquee.isEmpty());

        // AND WITH ONE LIVE: a press on empty plot space (no key, no handle - the
        // panel's own plot, far from every published key) that is dragged. The model
        // must publish the EDITOR's rectangle, so the gesture and the rectangle on
        // screen are one geometry rather than two derivations of one.
        core::CameraInfo camera;
        camera.setScreenSize(viewport);
        core::AbstractCursor cursor;
        const QPoint from(ruled.plot.center().x() + 80, ruled.plot.center().y() + 40);
        const QPoint to(from.x() + 60, from.y() + 50);
        CHECK(ruled.plot.contains(from) && ruled.plot.contains(to));
        QMouseEvent press(QEvent::MouseButtonPress, from, from, from, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent move(QEvent::MouseMove, to, to, to, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        // The marquee is the editor's CTRL-press gesture (a plain press in the plot is
        // left to the viewport tools - GraphEditor::ownsCursor's own routing), and the
        // modifier is read from the editor, exactly as the widget feeds it.
        editor.setModifiers(Qt::ControlModifier);
        const bool pressed = cursor.setMousePress(&press, camera) && editor.updateCursor(camera, cursor);
        editor.setModifiers(Qt::NoModifier);
        const bool dragged = cursor.setMouseMove(&move, camera) && editor.updateCursor(camera, cursor);
        const gui::GraphPanelModel draggedModel = gui::buildGraphPanelModel(editor, viewport, palette);
        const QRect published = draggedModel.marquee;
        std::printf(
            "[11] press=%d dragged=%d editor_marquee=(%d,%d %dx%d) model_marquee=(%d,%d %dx%d) equal=%d"
            " inside_plot=%d\n",
            int(pressed), int(dragged), editor.marqueeRect().left(), editor.marqueeRect().top(),
            editor.marqueeRect().width(), editor.marqueeRect().height(), published.left(), published.top(),
            published.width(), published.height(), int(published == editor.marqueeRect()),
            int(ruled.plot.contains(published))
        );
        CHECK(pressed);
        CHECK(dragged);
        CHECK(!published.isEmpty());
        CHECK(published == editor.marqueeRect());
        CHECK(ruled.plot.contains(published));

        // The release ends the gesture, and the model goes back to publishing nothing.
        QMouseEvent release(
            QEvent::MouseButtonRelease, to, to, to, Qt::LeftButton, Qt::NoButton, Qt::NoModifier
        );
        cursor.setMouseRelease(&release, camera);
        editor.updateCursor(camera, cursor);
        const gui::GraphPanelModel afterRelease = gui::buildGraphPanelModel(editor, viewport, palette);
        CHECK(afterRelease.marquee.isEmpty());

        // ---- AND THE OCCLUDED TOP: a widget floating above the viewport ----
        // The project tab bar covers the panel's first rows. The ruler band is a
        // SECTION ABOVE THE GRAPH now, so the only two answers an occluder can get
        // are:
        //   * the same band, when its whole slot is still at or below the occluded
        //     line (the inset cannot push it UP, so the strip does not move); and
        //   * NO band, when the occluder reaches into its slot - the band is never
        //     pushed down into the plot, because the plot's rows are the graph's and
        //     the whole point of the section is that curve ink cannot enter the ruler.
        // In BOTH cases the plot does not move: it is the editor's hit-test geometry,
        // so the drawn key stays the same key a press grabs.
        const int shallowInset = ruled.rulerBand.top() - ruled.panel.top(); // reaches the band's first row at most
        const gui::GraphPanelModel shallow =
            gui::buildGraphPanelModel(editor, viewport, palette, shallowInset);
        std::printf(
            "[11] occluded_shallow inset=%d band=(%d,%d %dx%d) frame_labels=%d plot_moved=%d\n",
            shallowInset, shallow.rulerBand.left(), shallow.rulerBand.top(), shallow.rulerBand.width(),
            shallow.rulerBand.height(), isz(shallow.frameLabels), int(shallow.plot != ruled.plot)
        );
        CHECK(shallow.rulerBand == ruled.rulerBand);
        CHECK(shallow.rulerBandTop == ruled.rulerBandTop);
        CHECK(shallow.frameLabels.size() == ruled.frameLabels.size());
        CHECK(shallow.plot == ruled.plot); // the interactive geometry stays put

        // An inset that reaches INTO the band's slot hides the strip for this frame:
        // no band, no ruler numbers, and - the invariant - no ruler ink inside the
        // plot. The graph itself is untouched.
        const int kInset = 24;
        const gui::GraphPanelModel occluded = gui::buildGraphPanelModel(editor, viewport, palette, kInset);
        std::printf(
            "[11] occluded_top inset=%d rulerBandTop=%d plot.top()=%d band_empty=%d frame_labels=%d"
            " plot_moved=%d value_labels=%d\n",
            kInset, occluded.rulerBandTop, ruled.plot.top(), int(occluded.rulerBand.isEmpty()),
            isz(occluded.frameLabels), int(occluded.plot != ruled.plot), isz(occluded.valueLabels)
        );
        CHECK(occluded.rulerBand.isEmpty());          // the strip is hidden, not moved into the graph
        CHECK(occluded.rulerBandTop == ruled.plot.top());
        CHECK(occluded.plot == ruled.plot);           // the interactive geometry stays put
        CHECK(occluded.panel == ruled.panel);
        CHECK(occluded.curves.size() == ruled.curves.size());
        CHECK(occluded.playheadX == ruled.playheadX);
        CHECK(occluded.frameLabels.isEmpty());
        CHECK(occluded.valueLabels.size() == ruled.valueLabels.size());

        // An inset that swallows the whole panel has the same answer - there is still
        // no ruler ink inside the plot, and still no label under the widget.
        const gui::GraphPanelModel swallowed =
            gui::buildGraphPanelModel(editor, viewport, palette, ruled.plot.bottom() + 40);
        std::printf(
            "[11] occluded_all inset=%d rulerBandTop=%d plot.top()=%d band_empty=%d frame_labels=%d\n",
            ruled.plot.bottom() + 40, swallowed.rulerBandTop, ruled.plot.top(),
            int(swallowed.rulerBand.isEmpty()), isz(swallowed.frameLabels)
        );
        CHECK(swallowed.rulerBand.isEmpty());
        CHECK(swallowed.rulerBandTop == ruled.plot.top());
        CHECK(swallowed.frameLabels.isEmpty());
        CHECK(swallowed.plot == ruled.plot);
    }

    // -------------------------------------------------------------------------
    // 12. THE LABEL COLUMN IS A STABLE, FORMAT-DERIVED RESERVE, AND NO GESTURE
    // MOVES IT. Two things have to hold at once:
    //   (a) the column is a function of the value FORMAT and the UI font, not of
    //       the values currently visible - so it is the SAME width for every
    //       window, and the plot's left edge (and every frame coordinate in the
    //       panel) stays put while the user pans, wheels or adds a key. This is
    //       the defect the first version of this rule had: a reserve measured
    //       from the visible labels moved plot.left by 1px on a 40px pan, so the
    //       frame coordinate moved 41px instead of 40 (measured on this tree).
    //   (b) it still COVERS the labels: it is the widest label 'g'/4 can write,
    //       so no published label exceeds its box, and the format's own sweep
    //       (62px at the UI font) sits inside the ceiling, which is a
    //       panel-protection bound rather than a clipping rule.
    // The reserve rule itself is restated by reserveAxisLabelWidth() above - the
    // test's own format and font metrics, never the model's own numbers.
    {
        const QFontMetrics metrics{QFont()};
        const float savedMin = editor.scale().valueMin();
        const float savedMax = editor.scale().valueMax();
        editor.setProject(&project);
        editor.setTargets({ &nodeA, &nodeB });

        const int reserve = reserveAxisLabelWidth(metrics);

        struct Window {
            const char* what;
            float lo;
            float hi;
        };
        const Window windows[] = {
            { "the seed window", 0.0f, 1.0f },
            { "a wide value, 12345.6", 0.0f, 12345.6f },
            { "a small negative, -1234", -1234.0f, 0.0f },
            { "both float extremes", -std::numeric_limits<float>::max(), std::numeric_limits<float>::max() },
            { "a mid-size window", 1000.0f, 2000.0f },
            { "a narrow window", 0.4f, 0.6f },
        };
        const QSize viewports[] = { QSize(1280, 720), QSize(800, 480), QSize(320, 200) };

        int wrongReserve = 0;     // a column that is not the format's reserve
        int movedPlot = 0;        // a plot rect that changed with the window
        int movedColumn = 0;      // a column that changed with the window
        int tooNarrowBox = 0;     // a label box narrower than its own measured text
        int tooNarrowColumn = 0;  // a column that cannot hold a label plus its right inset
        int labels = 0;

        for (const QSize& vp : viewports) {
            // One reference geometry per viewport: the plot depends on the
            // viewport, but it must NOT depend on the value window.
            QRect plotRef;
            QRect columnRef;
            bool haveRef = false;

            for (const Window& window : windows) {
                editor.scale().setValueWindow(window.lo, window.hi);
                const gui::GraphPanelModel measuredModel = gui::buildGraphPanelModel(editor, vp, palette);
                if (!haveRef) {
                    plotRef = measuredModel.plot;
                    columnRef = measuredModel.valueColumn;
                    haveRef = true;
                }

                if (measuredModel.valueColumn.width() != reserve)
                    ++wrongReserve;
                if (measuredModel.plot != plotRef)
                    ++movedPlot;
                if (measuredModel.valueColumn != columnRef)
                    ++movedColumn;

                int widestShown = 0;
                QString widestShownText;
                for (const gui::PanelLabel& label : measuredModel.valueLabels) {
                    const int advance = metrics.horizontalAdvance(label.text);
                    ++labels;
                    if (advance > widestShown) {
                        widestShown = advance;
                        widestShownText = label.text;
                    }
                    if (label.rect.width() < advance)
                        ++tooNarrowBox;
                    if (measuredModel.valueColumn.width() < advance + ctrl::graph::Scaler::kAxisLabelPad)
                        ++tooNarrowColumn;
                }
                std::printf(
                    "[12] viewport %4dx%-4d  window %-24s [%g, %g]  column=%3d reserve=%3d  widest label "
                    "\"%s\"=%2dpx box=%2dpx  plot=(%d,%d %dx%d)\n",
                    vp.width(), vp.height(), window.what, double(window.lo), double(window.hi),
                    measuredModel.valueColumn.width(), reserve, qUtf8Printable(widestShownText), widestShown,
                    measuredModel.valueLabels.isEmpty() ? -1 : measuredModel.valueLabels.first().rect.width(),
                    measuredModel.plot.x(), measuredModel.plot.y(), measuredModel.plot.width(),
                    measuredModel.plot.height()
                );
            }
        }
        editor.scale().setValueWindow(savedMin, savedMax); // as the editor was found

        std::printf(
            "[12] labels=%d  boxes narrower than their text=%d  columns too narrow for a label+inset=%d  "
            "columns != the format's reserve (%d)=%d  plots moved by a window change=%d  columns moved=%d\n",
            labels, tooNarrowBox, tooNarrowColumn, reserve, wrongReserve, movedPlot, movedColumn
        );
        CHECK(labels > 0);
        CHECK(wrongReserve == 0);
        CHECK(movedPlot == 0);
        CHECK(movedColumn == 0);
        CHECK(tooNarrowBox == 0);
        CHECK(tooNarrowColumn == 0);

        // ---- THE GESTURES A USER ACTUALLY MAKES, on the editor's own scaler ----
        // A wheel step on the value axis and a value PAN both change the value
        // window (that is what they are for) and must leave the panel's chrome
        // exactly where it was: the plot rect byte-identical to the reference.
        // The column is what makes that possible - a window-measured reserve made
        // the plot's left edge follow the numbers.
        const QSize vp = viewports[0];
        const gui::GraphPanelModel reference = gui::buildGraphPanelModel(editor, vp, palette);
        const QRect plotRef = reference.plot;
        const QRect columnRef = reference.valueColumn;

        const int anchorY = plotRef.center().y();
        const bool zoomed = editor.scale().updateValueZoom(1.25f, anchorY);
        const gui::GraphPanelModel afterZoom = gui::buildGraphPanelModel(editor, vp, palette);
        editor.scale().panValue(40);
        const gui::GraphPanelModel afterPan = gui::buildGraphPanelModel(editor, vp, palette);
        editor.scale().setValueWindow(savedMin, savedMax);

        // ...and a FRAME pan moves a frame's pixel by EXACTLY the pan delta: no
        // pixel of it may be eaten by a column that re-measured on the way.
        const int framePixelBefore = editor.scale().pixelX(core::Frame(0));
        editor.scale().panFrame(40);
        const int framePixelAfter = editor.scale().pixelX(core::Frame(0));
        editor.scale().panFrame(-40);
        const gui::GraphPanelModel afterFramePan = gui::buildGraphPanelModel(editor, vp, palette);

        std::printf(
            "[12] wheel step (anchor y=%d) applied=%d  pan 40px:  plot before=(%d,%d %dx%d) after_zoom=(%d,%d %dx%d) "
            "after_pan=(%d,%d %dx%d)  column before=%d after_zoom=%d after_pan=%d  px(0) frame pan: %d -> %d "
            "(delta %d, expected 40)\n",
            anchorY, int(zoomed), plotRef.x(), plotRef.y(), plotRef.width(), plotRef.height(), afterZoom.plot.x(),
            afterZoom.plot.y(), afterZoom.plot.width(), afterZoom.plot.height(), afterPan.plot.x(), afterPan.plot.y(),
            afterPan.plot.width(), afterPan.plot.height(), columnRef.width(), afterZoom.valueColumn.width(),
            afterPan.valueColumn.width(), framePixelBefore, framePixelAfter, framePixelAfter - framePixelBefore
        );
        CHECK(zoomed); // the fixture's premise: there was a window left to zoom
        CHECK(afterZoom.plot == plotRef);
        CHECK(afterPan.plot == plotRef);
        CHECK(afterFramePan.plot == plotRef);
        CHECK(afterZoom.valueColumn == columnRef);
        CHECK(afterPan.valueColumn == columnRef);
        CHECK(framePixelAfter - framePixelBefore == 40);
        CHECK(editor.scale().pixelX(core::Frame(0)) == framePixelBefore); // panned back

        // The reserve is inside the ceiling and covers the format's widest label:
        // the clamp is panel protection, NOT a rule that clips numbers, and this
        // is the number that keeps it one.
        int widestExtreme = 0;
        QString widestText;
        const float extremes[] = {
            -std::numeric_limits<float>::max(),
            -0.0009999f,
            -std::numeric_limits<float>::denorm_min(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::denorm_min(),
            12345.6f,
            100000.0f,
            -100000.0f,
            -1234.0f,
        };
        for (const float value : extremes) {
            const QString text = valueTextOf(value);
            const int advance = metrics.horizontalAdvance(text);
            if (advance > widestExtreme) {
                widestExtreme = advance;
                widestText = text;
            }
        }
        std::printf(
            "[12] reserve=%d = widest 'g'/4 label \"%s\" %dpx + inset %d + gap %d  ceiling=%d (cleared by %d)  "
            "floor=%d\n",
            reserve, qUtf8Printable(widestText), widestExtreme, ctrl::graph::Scaler::kAxisLabelPad,
            ctrl::graph::Scaler::kAxisLabelTextPad, ctrl::graph::Scaler::kMaxAxisLabelWidth,
            ctrl::graph::Scaler::kMaxAxisLabelWidth - reserve, ctrl::graph::Scaler::kAxisLabelWidth
        );
        CHECK(widestExtreme > 0);
        CHECK(reserve >= widestExtreme + ctrl::graph::Scaler::kAxisLabelPad);
        CHECK(reserve <= ctrl::graph::Scaler::kMaxAxisLabelWidth);
        // The reserve IS the measurement: pinning the column to the old 46px
        // reserve leaves 42px of box for a 62px label - this fails then.
        CHECK(ctrl::graph::Scaler::kAxisLabelWidth < reserve);
    }

    // -------------------------------------------------------------------------
    // 13. THE KEY MARKER IS A CIRCLE, AND THE HIT GEOMETRY IS A SEPARATE, LARGER
    // THING. The drawn marker is a circle of radius ctrl::graph::kKeyDotRadius
    // centred on the key's mapped point - and that radius is the APPLICATION'S
    // OWN key marker, the timeline's (ctrl::keyRadius = 3.0 * ctrl::radiiFactor =
    // 3.75px, src/ctrl/time/time_Renderer.h, the radius drawKeys() paints a key
    // body with). The rects in `keys` stay: they are the editor's hit geometry,
    // a different size on purpose.
    {
        // The tie to the timeline, as a compile-time identity rather than a
        // comment: this fails to build if either side moves.
        static_assert(
            ctrl::graph::kKeyDotRadius == ctrl::keyRadius,
            "the panel's key marker radius must be the timeline's own key-body radius "
            "(ctrl::keyRadius = 3.0 * ctrl::radiiFactor, src/ctrl/time/time_Renderer.h)"
        );
        CHECK(ctrl::graph::kKeyDotRadius < double(ctrl::graph::kKeyHitRadius)); // drawn small, grabbed large
        CHECK(ctrl::graph::kHandleRadius < ctrl::graph::kHandleHitRadius);      // ...for a handle too
        CHECK(ctrl::graph::kKeyDotRadius < double(ctrl::graph::kKeyRadius));    // the marker is not the hit square

        const gui::GraphPanelModel dotted = gui::buildGraphPanelModel(editor, viewport, palette);
        // A fresh copy of the editor's own axes: earlier blocks moved the value
        // window and the targets, and the key mapping under test is the CURRENT
        // one (block [10] refreshes its oracle for the same reason).
        ctrl::graph::Scaler oracle = editor.scale();
        oracle.setRect(panel);
        int keys = 0;
        int dots = 0;
        int badCounts = 0;
        int badCentres = 0;
        int offHitCentre = 0;
        int outsideOwnRect = 0;
        for (const gui::PanelCurve& curve : dotted.curves) {
            if (curve.keyDots.size() != curve.keys.size() || curve.keyDots.size() != curve.keyFrames.size())
                ++badCounts;

            const ctrl::graph::Curve source(*curve.node->timeLine(), curve.channel);
            int published = 0;
            for (const core::TimeKey* key : source.keys()) {
                const core::Frame frame(key->frame());
                const float value = source.valueOf(*key);
                const QRect hit = ctrl::graph::keyRect(oracle, frame, value);
                if (hit.intersected(dotted.plot).isEmpty())
                    continue; // entirely outside the plot: no rect published, so no dot
                if (published >= curve.keyDots.size()) {
                    ++badCounts;
                    break;
                }

                const QPointF dot = curve.keyDots.at(published);
                ++dots;
                // The dot is the key's mapped centre, derived from the scaler
                // rather than from the model's own call to keyDot().
                if (dot != QPointF(oracle.pixelX(frame), oracle.pixelY(value)))
                    ++badCentres;
                // ...and it is concentric with the HIT square: QRectF's centre is
                // the geometric one (left + width/2), so an uncut rect's centre is
                // exactly the key's point.
                if (QRectF(hit).center() != dot)
                    ++offHitCentre;
                if (curve.keys.at(published) == hit) {
                    // The rect is uncut, so the marker it belongs to must sit
                    // inside the geometry the editor picks with - a click on the
                    // dot's own pixel lands on that key.
                    if (!curve.keys.at(published).contains(dot.toPoint()))
                        ++outsideOwnRect;
                }
                ++published;
            }
            if (published != curve.keyDots.size())
                ++badCounts;
            keys += curve.keys.size();
        }

        std::printf(
            "[13] key rects=%d  key dots=%d  radius counts mismatched=%d  dots off the key's own pixel=%d  "
            "dots not concentric with the hit square=%d  uncut dots outside their own square=%d  "
            "kKeyDotRadius=%.2f (timeline ctrl::keyRadius=%.2f)  kKeyHitRadius=%d  kKeyRadius=%d\n",
            keys, dots, badCounts, badCentres, offHitCentre, outsideOwnRect, ctrl::graph::kKeyDotRadius,
            ctrl::keyRadius, ctrl::graph::kKeyHitRadius, ctrl::graph::kKeyRadius
        );
        CHECK(keys > 0);
        CHECK(dots == keys);
        CHECK(badCounts == 0);
        CHECK(badCentres == 0);
        CHECK(offHitCentre == 0);
        CHECK(outsideOwnRect == 0);
    }

    // -------------------------------------------------------------------------
    // 14. THE SELECTION COUNT, which the model still publishes and nothing draws.
    // Its one consumer now is the panel's accessibility summary ("N keys selected",
    // built by gui::GraphPanelFrame from this field), so what is pinned here is that
    // the field is the EDITOR's own number and follows the selection: the model is
    // read against editor.selectionCount(), the file's usual oracle, with nothing
    // selected, with one key selected, and after the selection is cleared again.
    {
        // Nothing before this block selects a key, and that is stated rather than
        // assumed: "the count is 0" is only an assertion if nothing is selected.
        editor.clearSelection();
        const gui::GraphPanelModel none = gui::buildGraphPanelModel(editor, viewport, palette);
        std::printf("[14] no selection: count=%d\n", none.selectionCount);
        CHECK(none.selectionCount == 0);

        // ONE key: the Move X key at frame 0 of target A, selected by the panel's own
        // rule (a key is named by node, type and frame - the timeline's triple).
        const ctrl::graph::Channel moveX(core::TimeKeyType_Move, 0);
        editor.selectKey(nodeA, moveX, core::Frame(0));
        const gui::GraphPanelModel one = gui::buildGraphPanelModel(editor, viewport, palette);
        std::printf("[14] one key selected: count=%d (editor states %d)\n", one.selectionCount,
                    editor.selectionCount());
        CHECK(one.selectionCount == editor.selectionCount());
        CHECK(one.selectionCount == 1);

        editor.clearSelection();
        const gui::GraphPanelModel cleared = gui::buildGraphPanelModel(editor, viewport, palette);
        std::printf("[14] cleared again: count=%d\n", cleared.selectionCount);
        CHECK(cleared.selectionCount == 0);
    }

    // -------------------------------------------------------------------------
    // 15. THE MARKER STATE: which marker is selected, and whether the key it belongs
    // to has a selected SIBLING. The drawing layer's three marker states are drawn
    // from exactly these two flags (see gui::drawPanel's marker pass), and the pair is
    // the design's "one key, two channels, both in play" rule: selecting ONE component
    // of a two-component key leaves the other component's marker unselected - its curve
    // still at full strength, its marker ringed faintly - and never selected.
    //
    // The editor is the oracle and the model is read: every flag below is compared
    // against editor.isMarkerSelected/hasSelectedSiblingMarker asked on the key the
    // model published (node, channel type and component, frame), and the two state
    // vectors are asserted parallel to the geometry they classify.
    {
        // THE VIEW, set to a known window. Blocks [11] and [12] drive the editor's own
        // scaler (a wheel zoom, pans, cursor gestures), and this block needs every key of
        // BOTH components of the Move key inside the plot at once - the two markers of one
        // key are what the sibling flag is about. The editor's own fit is the way back to
        // a window that covers the keys (nothing here sets a window by hand); the panel
        // rect goes into the scaler first, because a fit maps into the scaler's PLOT and
        // this file's own panel rect is the geometry every other block uses.
        editor.setTargets({ &nodeA, &nodeB });
        editor.scale().setRect(panel);
        editor.clearSelection();
        editor.fitAll();

        // One curve of the model, by the identity a channel is named by, or nullptr.
        const auto curveFor = [](const gui::GraphPanelModel& aModel, core::ObjectNode* aNode,
                                 const ctrl::graph::Channel& aChannel) -> const gui::PanelCurve* {
            for (const gui::PanelCurve& curve : aModel.curves) {
                if (curve.node == aNode && curve.channel == aChannel)
                    return &curve;
            }
            return nullptr;
        };
        // One flag of the key at aFrame, with aFound false when the model publishes no
        // marker for that key at all (it is outside the plot's window) - the honest
        // answer, and the one that keeps a malformed index from being read as a state.
        const auto flagOf = [](const QVector<bool>& aFlags, const gui::PanelCurve& aCurve, core::Frame aFrame,
                               bool& aFound) {
            const int i = aCurve.keyFrames.indexOf(aFrame);
            aFound = 0 <= i && i < aFlags.size();
            return aFound && aFlags.at(i);
        };
        // One curve's whole state, key by key: 'S' selected, 's' the SIBLING state
        // (not selected, its key selected on another component), '.' neither - the three
        // states the drawing layer draws, composed exactly as its marker pass composes
        // them from the two published flags.
        const auto statesOf = [](const gui::PanelCurve& aCurve) {
            QString out;
            for (int i = 0; i < aCurve.keySelected.size() && i < aCurve.keySiblingSelected.size(); ++i) {
                out += aCurve.keySelected.at(i) ? QLatin1Char('S')
                                                : aCurve.keySiblingSelected.at(i) ? QLatin1Char('s')
                                                                                  : QLatin1Char('.');
            }
            return out;
        };
        // The model against the editor, cell for cell, over every curve it published.
        struct MarkerTally {
            int keys = 0;           // markers the model classified
            int wider = 0;          // curves whose state vectors are not parallel to the geometry
            int wrongSelected = 0;  // cells where the model and the editor disagree
            int wrongSibling = 0;
        };
        const auto tally = [](const gui::GraphPanelModel& aModel, ctrl::GraphEditor& aEditor) {
            MarkerTally out;
            for (const gui::PanelCurve& curve : aModel.curves) {
                if (curve.keySelected.size() != curve.keyDots.size()
                    || curve.keySiblingSelected.size() != curve.keyDots.size()
                    || curve.keyDots.size() != curve.keyFrames.size()) {
                    ++out.wider;
                }
                for (int i = 0; i < curve.keyFrames.size(); ++i) {
                    const core::Frame frame = curve.keyFrames.at(i);
                    const bool selected = aEditor.isMarkerSelected(*curve.node, curve.channel, frame);
                    const bool sibling = aEditor.hasSelectedSiblingMarker(*curve.node, curve.channel, frame);
                    ++out.keys;
                    if (i >= curve.keySelected.size() || curve.keySelected.at(i) != selected)
                        ++out.wrongSelected;
                    if (i >= curve.keySiblingSelected.size() || curve.keySiblingSelected.at(i) != sibling)
                        ++out.wrongSibling;
                }
            }
            return out;
        };
        // How many cells a model flags at all, either way round: the "nothing is flagged"
        // half of the pair below.
        const auto flaggedCount = [](const gui::GraphPanelModel& aModel) {
            int on = 0;
            for (const gui::PanelCurve& curve : aModel.curves) {
                for (const bool flag : curve.keySelected)
                    on += flag ? 1 : 0;
                for (const bool flag : curve.keySiblingSelected)
                    on += flag ? 1 : 0;
            }
            return on;
        };

        const ctrl::graph::Channel moveX(core::TimeKeyType_Move, 0);
        const ctrl::graph::Channel moveY(core::TimeKeyType_Move, 1);
        const ctrl::graph::Channel rotate(core::TimeKeyType_Rotate, 0);
        const ctrl::graph::Channel opa(core::TimeKeyType_Opa, 0);

        // ---- NOTHING SELECTED: neither flag on any marker of any curve.
        const gui::GraphPanelModel none = gui::buildGraphPanelModel(editor, viewport, palette);
        const MarkerTally noneTally = tally(none, editor);
        std::printf(
            "[15] no selection: markers=%d parallel_mismatches=%d flag_mismatches=%d/%d flagged=%d\n",
            noneTally.keys, noneTally.wider, noneTally.wrongSelected, noneTally.wrongSibling,
            flaggedCount(none)
        );
        CHECK(noneTally.keys > 0); // the fixture's premise: there are markers to classify
        CHECK(noneTally.wider == 0);
        CHECK(noneTally.wrongSelected == 0);
        CHECK(noneTally.wrongSibling == 0);
        CHECK(flaggedCount(none) == 0); // and that is what the assertion is about

        // ---- ONE MARKER of a TWO-component key: Move X at frame 0. The other component
        // of the same key is NOT selected and carries the sibling flag; the key at frame
        // 20 of that same type, and the other target's key, are neither.
        CHECK(editor.selectMarker(nodeA, moveX, core::Frame(0)));
        const gui::GraphPanelModel one = gui::buildGraphPanelModel(editor, viewport, palette);
        const MarkerTally oneTally = tally(one, editor);
        const gui::PanelCurve* oneX = curveFor(one, &nodeA, moveX);
        const gui::PanelCurve* oneY = curveFor(one, &nodeA, moveY);
        const gui::PanelCurve* oneRotate = curveFor(one, &nodeA, rotate);
        const gui::PanelCurve* oneOpa = curveFor(one, &nodeB, opa);
        bool x0 = false;
        bool y0 = false;
        bool x20 = false;
        bool y20 = false;
        const bool oneX0 = oneX && flagOf(oneX->keySelected, *oneX, core::Frame(0), x0);
        const bool oneY0 = oneY && flagOf(oneY->keySelected, *oneY, core::Frame(0), y0);
        const bool oneXSib0 = oneX && flagOf(oneX->keySiblingSelected, *oneX, core::Frame(0), x0);
        const bool oneYSib0 = oneY && flagOf(oneY->keySiblingSelected, *oneY, core::Frame(0), y0);
        const bool oneX20 = oneX && flagOf(oneX->keySelected, *oneX, core::Frame(20), x20);
        const bool oneY20 = oneY && flagOf(oneY->keySelected, *oneY, core::Frame(20), y20);
        const bool oneXSib20 = oneX && flagOf(oneX->keySiblingSelected, *oneX, core::Frame(20), x20);
        const bool oneYSib20 = oneY && flagOf(oneY->keySiblingSelected, *oneY, core::Frame(20), y20);
        std::printf(
            "[15] Move X at frame 0 selected: markers=%d parallel_mismatches=%d flag_mismatches=%d/%d"
            "  Move X=[%s]  Move Y=[%s]  Rotate=[%s]  Opa=[%s]  (frame0 published X/Y=%d/%d,"
            " frame20 X/Y=%d/%d)\n",
            oneTally.keys, oneTally.wider, oneTally.wrongSelected, oneTally.wrongSibling,
            oneX ? qUtf8Printable(statesOf(*oneX)) : "<missing>",
            oneY ? qUtf8Printable(statesOf(*oneY)) : "<missing>",
            oneRotate ? qUtf8Printable(statesOf(*oneRotate)) : "<missing>",
            oneOpa ? qUtf8Printable(statesOf(*oneOpa)) : "<missing>", int(x0), int(y0), int(x20), int(y20)
        );
        CHECK(oneX && oneY && oneRotate && oneOpa); // the model draws every channel of both targets
        CHECK(oneTally.wider == 0);
        CHECK(oneTally.wrongSelected == 0); // the model IS the editor's answer, cell for cell
        CHECK(oneTally.wrongSibling == 0);
        CHECK(x0 && y0 && x20 && y20); // every Move key is inside the fitted window
        // The shape of that answer, stated on the two components of one key:
        CHECK(oneX0);        // the marker the press named is selected
        CHECK(!oneXSib0);    // ... with no sibling of its own
        CHECK(!oneY0);       // the sibling NEVER takes the selection
        CHECK(oneYSib0);     // ... and it is exactly what the sibling flag is for
        // The second key of the same type is nobody's: neither of its components.
        CHECK(!oneX20 && !oneXSib20);
        CHECK(!oneY20 && !oneYSib20);
        // A ONE-component key can have no sibling at all (the engine's own rule: another
        // COMPONENT of the same key, which Rotate has none of), and a Move selection does
        // not reach another target's Opa key either.
        CHECK(oneRotate->keySiblingSelected.count(true) == 0);
        CHECK(oneOpa->keySelected.count(true) == 0 && oneOpa->keySiblingSelected.count(true) == 0);
        CHECK(flaggedCount(one) == 2); // exactly the selected marker and its one sibling

        // ---- THE WHOLE KEY: the timeline's own gesture (and the panel's marquee and
        // ctrl+A) selects every component of a key, so BOTH markers are selected. The
        // engine's sibling query is the RAW one - an entry naming a different marker
        // counts, and a key-level entry names all of them - so both report a sibling too;
        // the drawing layer reaches the faint-ring state by ANDing the two flags
        // (`!selected && sibling`), which is false here. That composition is why the flags
        // are published as the engine states them and not pre-combined.
        CHECK(editor.selectKey(nodeA, moveX, core::Frame(0)));
        const gui::GraphPanelModel whole = gui::buildGraphPanelModel(editor, viewport, palette);
        const gui::PanelCurve* wholeX = curveFor(whole, &nodeA, moveX);
        const gui::PanelCurve* wholeY = curveFor(whole, &nodeA, moveY);
        std::printf(
            "[15] whole key selected: Move X=[%s]  Move Y=[%s]  editor answers X=%d/%d  Y=%d/%d"
            "  (selected/sibling)  faint_ring_cells=%d\n",
            wholeX ? qUtf8Printable(statesOf(*wholeX)) : "<missing>",
            wholeY ? qUtf8Printable(statesOf(*wholeY)) : "<missing>",
            int(editor.isMarkerSelected(nodeA, moveX, core::Frame(0))),
            int(editor.hasSelectedSiblingMarker(nodeA, moveX, core::Frame(0))),
            int(editor.isMarkerSelected(nodeA, moveY, core::Frame(0))),
            int(editor.hasSelectedSiblingMarker(nodeA, moveY, core::Frame(0))),
            [&whole] {
                int faint = 0;
                for (const gui::PanelCurve& curve : whole.curves) {
                    for (int i = 0; i < curve.keySelected.size() && i < curve.keySiblingSelected.size(); ++i) {
                        if (!curve.keySelected.at(i) && curve.keySiblingSelected.at(i))
                            ++faint;
                    }
                }
                return faint;
            }()
        );
        CHECK(wholeX && wholeY);
        bool wx0 = false;
        bool wy0 = false;
        CHECK(wholeX && flagOf(wholeX->keySelected, *wholeX, core::Frame(0), wx0));
        CHECK(wholeY && flagOf(wholeY->keySelected, *wholeY, core::Frame(0), wy0));
        CHECK(wx0 && wy0); // one key lights BOTH its markers
        CHECK(!wholeX->keySiblingSelected.isEmpty() && !wholeY->keySiblingSelected.isEmpty());
        CHECK(tally(whole, editor).wrongSelected == 0);

        // ---- AND CLEARED AGAIN: the flags follow the selection rather than latch it.
        editor.clearSelection();
        const gui::GraphPanelModel cleared = gui::buildGraphPanelModel(editor, viewport, palette);
        const MarkerTally clearedTally = tally(cleared, editor);
        std::printf(
            "[15] cleared again: parallel_mismatches=%d flag_mismatches=%d/%d flagged=%d\n", clearedTally.wider,
            clearedTally.wrongSelected, clearedTally.wrongSibling, flaggedCount(cleared)
        );
        CHECK(clearedTally.wider == 0);
        CHECK(clearedTally.wrongSelected == 0);
        CHECK(clearedTally.wrongSibling == 0);
        CHECK(flaggedCount(cleared) == 0);

        // ---- THE PROPORTIONAL FLAG: published parallel to the geometry, and it IS the
        // document's answer. The pair is the Move channel's two keys - frame 0 stays
        // absolute, frame 20 is made proportional - so the model must report exactly
        // one true entry and name the right frame for it. The flag is read from the KEY
        // the model's own `keyFrames` names, through core::TimeKey::isProportional();
        // nothing about the model's geometry may change with it (the model is read-only).
        const int absoluteFrame = 0;
        const int proportionalFrame = 20;
        move1->setProportional(true);
        CHECK(nodeA.timeLine()->timeKey(core::TimeKeyType_Move, proportionalFrame) == move1);

        const gui::GraphPanelModel proportionalModel = gui::buildGraphPanelModel(editor, viewport, palette);
        int widerProportional = 0;
        int wrongProportional = 0;
        for (const gui::PanelCurve& curve : proportionalModel.curves) {
            if (curve.keyProportional.size() != curve.keys.size()
                || curve.keyProportional.size() != curve.keyDots.size()
                || curve.keyProportional.size() != curve.keyFrames.size()) {
                ++widerProportional;
            }
            for (int i = 0; i < curve.keyFrames.size(); ++i) {
                const core::TimeKey* key =
                    curve.node->timeLine()->timeKey(curve.channel.type(), curve.keyFrames.at(i).get());
                const bool expected = key && key->isProportional();
                if (i >= curve.keyProportional.size() || curve.keyProportional.at(i) != expected)
                    ++wrongProportional;
            }
        }
        CHECK(widerProportional == 0);

        const gui::PanelCurve* proportionalX = curveFor(proportionalModel, &nodeA, moveX);
        CHECK(proportionalX != nullptr);
        bool absolutePublished = false;
        bool proportionalPublished = false;
        const bool absoluteFlag =
            proportionalX && flagOf(proportionalX->keyProportional, *proportionalX, core::Frame(absoluteFrame),
                                    absolutePublished);
        const bool proportionalFlag =
            proportionalX && flagOf(proportionalX->keyProportional, *proportionalX,
                                    core::Frame(proportionalFrame), proportionalPublished);
        QString proportionalFlags;
        if (proportionalX) {
            for (const bool flag : proportionalX->keyProportional)
                proportionalFlags += flag ? QLatin1Char('P') : QLatin1Char('.');
        } else {
            proportionalFlags = QStringLiteral("<missing>");
        }
        std::printf(
            "[15] proportional flag: parallel_mismatches=%d flag_mismatches=%d  Move X=[%s]  abs@%d=%d"
            " prop@%d=%d\n",
            widerProportional, wrongProportional, qUtf8Printable(proportionalFlags), absoluteFrame,
            int(absoluteFlag), proportionalFrame, int(proportionalFlag)
        );
        CHECK(absolutePublished && proportionalPublished);
        CHECK(!absoluteFlag);      // the absolute neighbour keeps its flag off
        CHECK(proportionalFlag);   // ...and the proportional key turns exactly its own on
        CHECK(wrongProportional == 0);

        // ...and CLEARED AGAIN: the flag follows the document rather than latching.
        move1->setProportional(false);
        const gui::GraphPanelModel afterClear = gui::buildGraphPanelModel(editor, viewport, palette);
        const gui::PanelCurve* clearedX = curveFor(afterClear, &nodeA, moveX);
        CHECK(clearedX != nullptr);
        CHECK(clearedX && clearedX->keyProportional.count(true) == 0);

        // The editor is left as the blocks before this one left it: no selection, the
        // two targets, the panel's own rect in the scaler.
        editor.clearSelection();
    }

    // -------------------------------------------------------------------------
    // 16: THE SCROLL BARS - the four rects the panel publishes, and what a thumb's
    // LENGTH means.
    //
    // The bars are chrome the user DRAGS: the horizontal one along the plot's bottom
    // edge moves the frame axis' window (the timeline's own window while a timeline
    // owns that axis), the vertical one along the plot's right edge moves the panel's
    // value window. Both live in the margin the scaler reserves for them, so the
    // four rects are outside the plot, inside the panel and disjoint from everything
    // already published - the value column included.
    {
        editor.setTargets({ &nodeA, &nodeB });
        editor.scale().setRect(panel);
        editor.fitAll();

        float valueLo = 0.0f;
        float valueHi = 0.0f;
        CHECK(editor.shownValueExtent(valueLo, valueHi)); // the keys of both layers
        CHECK(valueHi > valueLo);
        const util::Range frames = editor.frameRange(); // the horizontal bar's content
        CHECK(frames.max() > frames.min());

        // THE VALUE AXIS IS ZOOMED INTO THE KEYS on purpose: fitAll fits the value
        // window TO the key extent, i.e. the window would equal its own content and the
        // v-thumb would fill its track (a full-length pill, nothing to scroll). A
        // quarter-to-three-quarters window is a real thumb, so the assertions below
        // measure a bar that travels. The frame axis keeps fitAll's fit: its CONTENT is
        // the project's frame range, which is wider than the keys' own span either way.
        editor.scale().setValueWindow(
            valueLo + 0.25f * (valueHi - valueLo), valueLo + 0.75f * (valueHi - valueLo)
        );

        const gui::GraphPanelModel barsModel = gui::buildGraphPanelModel(editor, viewport, palette);
        const QRect plot = barsModel.plot;
        CHECK(!plot.isEmpty());
        CHECK(barsModel.panel == panel);

        // THE LANES, hand-built from the scaler's own numbers and the published panel:
        // the application's 12px lane FLUSH with the panel's bottom and right edges -
        // the horizontal one the panel's last 12 rows across its whole width (the
        // timeline's own bar is flush the same way), the vertical one the panel's last
        // 12 columns from the plot's first row to the row above the horizontal lane.
        const int lane = ctrl::graph::Scaler::kScrollBarThickness;
        CHECK(lane == 12);
        CHECK(ctrl::graph::Scaler::kScrollBarReserve == lane); // the plot's inset IS the lane
        CHECK(barsModel.hTrack == QRect(panel.left(), panel.bottom() - lane + 1, panel.width(), lane));
        CHECK(barsModel.vTrack
              == QRect(panel.right() - lane + 1, plot.top(), lane, panel.bottom() - lane - plot.top() + 1));
        // FLUSH: the horizontal lane touches the panel's bottom AND right edges; the
        // vertical lane touches its right edge and ends on the row above the horizontal
        // one, so the corner belongs to the single bar the timeline has.
        CHECK(barsModel.hTrack.bottom() == panel.bottom());
        CHECK(barsModel.hTrack.right() == panel.right());
        CHECK(barsModel.hTrack.left() == panel.left());
        CHECK(barsModel.vTrack.right() == panel.right());
        CHECK(barsModel.vTrack.bottom() + 1 == barsModel.hTrack.top());
        CHECK(!barsModel.hTrack.intersects(barsModel.vTrack));
        // ...and the plot's own insets are the lane and nothing else.
        CHECK(plot.bottom() + 1 == barsModel.hTrack.top());
        CHECK(plot.right() + 1 == barsModel.vTrack.left());
        CHECK(barsModel.panel.contains(barsModel.hTrack));
        CHECK(barsModel.panel.contains(barsModel.vTrack));
        CHECK(!barsModel.hTrack.intersects(plot));
        CHECK(!barsModel.vTrack.intersects(plot));
        CHECK(!barsModel.hTrack.intersects(barsModel.rulerBand)); // the ruler's rows are above the plot
        CHECK(!barsModel.vTrack.intersects(barsModel.rulerBand));
        CHECK(!barsModel.valueColumn.intersects(barsModel.hTrack));
        CHECK(!barsModel.valueColumn.intersects(barsModel.vTrack));
        CHECK(barsModel.hTrack.top() > barsModel.valueColumn.bottom());
        CHECK(barsModel.vTrack.left() > plot.right());

        // THE THUMBS ARE THE TIMELINE'S HANDLE, not a private fraction. The
        // reference is gui::TimelineScrollBar::handleRect: the viewport's two edges
        // over the UNION track, each rounded; the frame axis' track also carries the
        // timeline's trailing kTimeLineMargin (24px, in frames at the axis' rate).
        // There is no style minimum and no track-minus-one cap.
        //
        // The window the thumb describes is the editor's own: the frames the plot
        // covers (the mapping's inverse at its two edges) inside the project's range.
        // The oracle is a copy of the editor's LIVE scaler pinned to this panel - the
        // same state the model's own copy was built from - so the ratio below is
        // measured against the mapping the plot is drawn with.
        ctrl::graph::Scaler live = editor.scale();
        live.setRect(panel);
        const auto spans = [](double aLo, double aHi) {
            return std::max(aLo, aHi) - std::min(aLo, aHi);
        };
        // The timeline's handleRect, written out (edge rounding, union track, 1px
        // floor) - the formula graph_Scaler.cpp's timelineHandle implements.
        const auto timelineThumb = [](const QRect& aTrack, bool aHorizontal, bool aInverted,
                                      double aContentLo, double aContentHi, double aWindowLo,
                                      double aWindowHi) {
            const int trackLength = aHorizontal ? aTrack.width() : aTrack.height();
            if (trackLength <= 0)
                return QRect();
            const double windowLo = aInverted ? -std::max(aWindowLo, aWindowHi) : std::min(aWindowLo, aWindowHi);
            const double windowHi = aInverted ? -std::min(aWindowLo, aWindowHi) : std::max(aWindowLo, aWindowHi);
            const double contentLo = aInverted ? -std::max(aContentLo, aContentHi) : std::min(aContentLo, aContentHi);
            const double contentHi = aInverted ? -std::min(aContentLo, aContentHi) : std::max(aContentLo, aContentHi);
            const double trackLo = std::min(contentLo, windowLo);
            const double trackHi = std::max(contentHi, windowHi);
            if (!(trackHi > trackLo))
                return QRect();
            const auto edge = [&](double aValue) {
                return int((aValue - trackLo) / (trackHi - trackLo) * trackLength + 0.5);
            };
            const int start = edge(windowLo);
            const int length = std::max(edge(windowHi) - start, 1);
            return aHorizontal ? QRect(aTrack.left() + start, aTrack.top(), length, aTrack.height())
                               : QRect(aTrack.left(), aTrack.top() + start, aTrack.width(), length);
        };
        const double frameWindowLo = live.frameAtPixel(double(plot.left()));
        const double frameWindowHi = live.frameAtPixel(double(plot.right() + 1));
        const double frameContentLo = double(frames.min());
        const double frameContentHi = double(frames.max());
        const double frameMargin =
            double(ctrl::graph::Scaler::kScrollBarTrailingMargin) / live.framePpf();
        const QRect expectedH = timelineThumb(
            barsModel.hTrack, true, false, frameContentLo, frameContentHi + frameMargin, frameWindowLo,
            frameWindowHi
        );
        const double hTrackLo = std::min(frameContentLo, frameWindowLo);
        const double hTrackHi = std::max(frameContentHi + frameMargin, frameWindowHi);
        const bool expectedHTravel =
            hTrackHi - hTrackLo > spans(frameWindowLo, frameWindowHi);
        const QRect expectedV =
            timelineThumb(barsModel.vTrack, false, true, double(valueLo), double(valueHi), live.valueMin(), live.valueMax());
        const double vTrackLo = std::min(double(valueLo), double(live.valueMin()));
        const double vTrackHi = std::max(double(valueHi), double(live.valueMax()));
        const bool expectedVTravel =
            vTrackHi - vTrackLo > (double(live.valueMax()) - double(live.valueMin()));
        CHECK(barsModel.hThumb == expectedH);
        if (!expectedHTravel)
            CHECK(barsModel.hThumb == barsModel.hTrack);
        // BOTH lanes publish the handle in every state: at zero travel the run IS the
        // track (the panel draws a full-length pill), and that full run is still a
        // grabbable bar - the timeline's full-width handle is (see THE RUN RULE in
        // GraphEditor.h; the model only publishes the geometry).
        CHECK(barsModel.vThumb == expectedV);
        if (!expectedVTravel)
            CHECK(barsModel.vThumb == barsModel.vTrack);
        CHECK(!barsModel.vThumb.isEmpty());
        CHECK(barsModel.hTrack.contains(barsModel.hThumb));
        CHECK(barsModel.vTrack.contains(barsModel.vThumb) || barsModel.vThumb.isEmpty());
        std::printf(
            "[16] bars: plot (%d,%d %dx%d)  hTrack (%d,%d %dx%d) hThumb %d px at +%d (expected %d at +%d)  "
            "vTrack (%d,%d %dx%d) vThumb %d px at +%d  value content %.4f..%.4f window %.4f..%.4f  frames %d..%d "
            "window %.2f..%.2f\n",
            plot.x(), plot.y(), plot.width(), plot.height(), barsModel.hTrack.x(), barsModel.hTrack.y(),
            barsModel.hTrack.width(), barsModel.hTrack.height(), barsModel.hThumb.width(),
            barsModel.hThumb.left() - barsModel.hTrack.left(), expectedH.width(),
            expectedH.left() - barsModel.hTrack.left(),
            barsModel.vTrack.x(), barsModel.vTrack.y(), barsModel.vTrack.width(), barsModel.vTrack.height(),
            barsModel.vThumb.height(), barsModel.vThumb.top() - barsModel.vTrack.top(), valueLo, valueHi,
            editor.scale().valueMin(), editor.scale().valueMax(), frames.min(), frames.max(), frameWindowLo,
            frameWindowHi
        );
        CHECK(barsModel.hThumb.left() >= barsModel.hTrack.left());
        CHECK(barsModel.hThumb.right() <= barsModel.hTrack.right());
        CHECK(barsModel.vThumb.top() >= barsModel.vTrack.top());
        CHECK(barsModel.vThumb.bottom() <= barsModel.vTrack.bottom());

        // THE DIRECTION, both axes: the pill moves WITH the window. A frame window at
        // the project's START puts the horizontal pill at the lane's left. At the
        // range's END it is NOT flush with the lane: the track carries the timeline's
        // trailing margin past the last frame, so a window ending on the last frame
        // sits a margin short of the track's end (flush right needs viewportEnd >=
        // frameEnd, as the timeline's own handle does). The value axis is inverted
        // along its lane, so a window at HIGH values puts the pill at the TOP and a
        // window at LOW values at the BOTTOM. The frame window is PLACED exactly
        // (setFrameWindow maps a pushed window with no ladder rounding).
        editor.setFrameWindow(double(frames.min()), double(frames.min()) + 5.0);
        const gui::GraphPanelModel atStart = gui::buildGraphPanelModel(editor, viewport, palette);
        CHECK(atStart.hThumb.left() == atStart.hTrack.left());
        editor.setFrameWindow(double(frames.max()) - 5.0, double(frames.max()));
        const gui::GraphPanelModel atEnd = gui::buildGraphPanelModel(editor, viewport, palette);
        {
            const double endMargin =
                double(ctrl::graph::Scaler::kScrollBarTrailingMargin) / editor.scale().framePpf();
            const QRect expectedEnd = timelineThumb(
                atEnd.hTrack, true, false, double(frames.min()), double(frames.max()) + endMargin,
                double(frames.max()) - 5.0, double(frames.max())
            );
            CHECK(atEnd.hThumb == expectedEnd);
            CHECK(atEnd.hThumb.right() < atEnd.hTrack.right());
            std::printf(
                "[16] direction: at the range end the pill is x %d..%d of the lane %d..%d (hand: %d..%d)\n",
                atEnd.hThumb.left(), atEnd.hThumb.right(), atEnd.hTrack.left(), atEnd.hTrack.right(),
                expectedEnd.left(), expectedEnd.right()
            );
        }
        editor.clearFrameAxis(); // the frame axis is the panel's own again
        editor.scale().setRect(panel);
        editor.scale().setValueWindow(valueHi - 0.1f * (valueHi - valueLo), valueHi);
        const gui::GraphPanelModel atTop = gui::buildGraphPanelModel(editor, viewport, palette);
        CHECK(atTop.vThumb.top() == atTop.vTrack.top());
        editor.scale().setValueWindow(valueLo, valueLo + 0.1f * (valueHi - valueLo));
        const gui::GraphPanelModel atBottom = gui::buildGraphPanelModel(editor, viewport, palette);
        CHECK(atBottom.vThumb.bottom() == atBottom.vTrack.bottom());
        std::printf(
            "[16] direction: frame window at the range start -> pill x %d..%d; value window at the top -> pill y "
            "%d..%d; at the bottom -> %d..%d (lane %d..%d)\n",
            atStart.hThumb.left(), atStart.hThumb.right(), atTop.vThumb.top(), atTop.vThumb.bottom(),
            atBottom.vThumb.top(), atBottom.vThumb.bottom(), atTop.vTrack.top(), atTop.vTrack.bottom()
        );

        // A FULL-CONTENT WINDOW DRAWS A FULL-LENGTH SLIDER IN BOTH LANES. Qt's slider
        // fills its groove when the range is empty, and that is what the timeline's
        // AlwaysOn horizontal bar shows (measured at zero travel: 638 of its 638px
        // lane); the vertical lane is the panel's own and must do the same - a vertical
        // bar that VANISHED with the content in range left the user with one lane that
        // disappears. Neither lane loses its RUN at zero travel: the model publishes
        // the full pill and the editor still claims a press on it (hHasRun - THE RUN
        // RULE in GraphEditor.h). The TRACK is laid out either way, which is what keeps
        // the plot's rect - and every frame coordinate - independent of content.
        editor.scale().fitFrameAxis(core::Frame(-500), core::Frame(500));
        editor.scale().setValueWindow(-1000.0f, 1000.0f);
        const gui::GraphPanelModel full = gui::buildGraphPanelModel(editor, viewport, palette);
        std::printf(
            "[16] full-content window: hThumb %d px of a %d px lane  vThumb %d px of a %d px lane  tracks laid "
            "out %d/%d\n",
            full.hThumb.width(), full.hTrack.width(), full.vThumb.height(), full.vTrack.height(),
            int(!full.hTrack.isEmpty()), int(!full.vTrack.isEmpty())
        );
        CHECK(!full.hThumb.isEmpty());
        CHECK(full.hThumb == full.hTrack); // the slider fills its groove: nowhere to go
        CHECK(full.vThumb == full.vTrack); // the VERTICAL lane is never hidden in this state
        CHECK(!full.vTrack.isEmpty());
        CHECK(full.plot == barsModel.plot);

        // A NARROW WINDOW GETS THE HANDLE THE TIMELINE WOULD DRAW - the visible
        // fraction, edge-rounded, floored at one pixel. The frame axis is given a
        // HALF-FRAME window (about 11px of the frame track) and the value axis a
        // thousandth of the key extent (far under one track pixel, so the 1px floor);
        // both are asserted against handleRect written out.
        {
            editor.setFrameWindow(10.0, 10.5);
            editor.scale().setValueWindow(50.0f, 50.001f);
            const gui::GraphPanelModel narrow = gui::buildGraphPanelModel(editor, viewport, palette);
            const double narrowMargin =
                double(ctrl::graph::Scaler::kScrollBarTrailingMargin) / editor.scale().framePpf();
            const QRect narrowH = timelineThumb(
                narrow.hTrack, true, false, double(frames.min()), double(frames.max()) + narrowMargin, 10.0, 10.5
            );
            const QRect narrowV = timelineThumb(
                narrow.vTrack, false, true, double(valueLo), double(valueHi),
                double(editor.scale().valueMin()), double(editor.scale().valueMax())
            );
            std::printf(
                "[16] narrow windows: frame window 10..10.5 -> hThumb %d px (hand %d, lane %d); value window "
                "%.4f..%.4f -> vThumb %d px (hand %d, lane %d)\n",
                narrow.hThumb.width(), narrowH.width(), narrow.hTrack.width(), editor.scale().valueMin(),
                editor.scale().valueMax(), narrow.vThumb.height(), narrowV.height(), narrow.vTrack.height()
            );
            CHECK(narrow.hThumb == narrowH);
            CHECK(narrow.hThumb.width() >= 1);
            CHECK(narrow.vThumb == narrowV);
            CHECK(narrow.vThumb.height() >= 1);
        }

        // A SHARED WINDOW NARROWER THAN THE CONTENT PUBLISHES A RUN - the delta this
        // block exists for, stated as the timeline's own rule. The window below is the
        // MIDDLE THIRD of the project's frame range, pushed through setFrameWindow
        // (the live dock's path: the timeline hands its visible window to the editor),
        // and the panel publishes the handle handleRect draws for it: the union track
        // is [rangeStart, rangeEnd + 24px/ppf], and the span's edges are rounded.
        // An empty run is correct ONLY while the window covers that union.
        {
            const double contentSpan = double(frames.max() - frames.min());
            CHECK(contentSpan > 0.0);
            const double lo = double(frames.min()) + contentSpan / 3.0;
            const double hi = double(frames.min()) + 2.0 * contentSpan / 3.0;
            editor.setFrameWindow(lo, hi);
            const gui::GraphPanelModel third = gui::buildGraphPanelModel(editor, viewport, palette);
            const double thirdMargin =
                double(ctrl::graph::Scaler::kScrollBarTrailingMargin) / editor.scale().framePpf();
            const QRect thirdExpected = timelineThumb(
                third.hTrack, true, false, double(frames.min()), double(frames.max()) + thirdMargin, lo, hi
            );
            std::printf(
                "[16] shared window %.3f..%.3f of %d..%d -> hThumb %d px at +%d of a %d px lane (hand: %d at +%d)\n",
                lo, hi, frames.min(), frames.max(), third.hThumb.width(),
                third.hThumb.left() - third.hTrack.left(), third.hTrack.width(), thirdExpected.width(),
                thirdExpected.left() - third.hTrack.left()
            );
            CHECK(third.hThumb == thirdExpected);
            CHECK(third.hThumb.width() < third.hTrack.width()); // TRAVEL: the run does not fill its lane
            CHECK(third.hTrack.contains(third.hThumb));

            // THE WINDOW IS THE MAPPING'S, NOT THE CALLER'S RECT. The scaler fits the
            // pushed window over ITS OWN rect; the model publishes the panel it is
            // asked for and sets that rect afterwards. A bar that measures the window
            // over the NEW rect scales its span by the two widths' ratio - and a
            // narrower fit (here 400px against the model's 1280px) then reads as "the
            // content fits" and the run is REMOVED while the panel is plainly showing a
            // window under a third of the range. That is the user-visible "the
            // horizontal bar is never there"; scrollBars must read mFrameWindowRect.
            editor.scale().setRect(QRect(QPoint(0, 0), QSize(400, 720)));
            editor.setFrameWindow(lo, hi);
            const gui::GraphPanelModel mapped = gui::buildGraphPanelModel(editor, viewport, palette);
            const double mappedMargin =
                double(ctrl::graph::Scaler::kScrollBarTrailingMargin) / editor.scale().framePpf();
            const QRect mappedExpected = timelineThumb(
                mapped.hTrack, true, false, double(frames.min()), double(frames.max()) + mappedMargin, lo, hi
            );
            std::printf(
                "[16] window fitted over a 400px rect, model built for the %dx%d panel: hThumb %d px of a %d px "
                "lane (hand %d)\n",
                viewport.width(), viewport.height(), mapped.hThumb.width(), mapped.hTrack.width(),
                mappedExpected.width()
            );
            CHECK(mapped.hThumb == mappedExpected);
            CHECK(mapped.hThumb.width() < mapped.hTrack.width());
            editor.scale().setRect(panel);

            // A WINDOW ALMOST THE FULL RANGE STILL DRAWS THE HANDLE THE TIMELINE
            // DRAWS: 1.7% short is a run just under the track, and a tenth of a frame
            // short is still short by the trailing margin (24px) - the margin is part
            // of the track, which is why the timeline's own full-range pill does not
            // fill its lane.
            {
                editor.setFrameWindow(
                    double(frames.min()) + 0.01 * contentSpan, double(frames.min()) + 0.993 * contentSpan
                );
                const gui::GraphPanelModel near = gui::buildGraphPanelModel(editor, viewport, palette);
                const double nearMargin =
                    double(ctrl::graph::Scaler::kScrollBarTrailingMargin) / editor.scale().framePpf();
                const QRect nearExpected = timelineThumb(
                    near.hTrack, true, false, double(frames.min()), double(frames.max()) + nearMargin,
                    double(frames.min()) + 0.01 * contentSpan, double(frames.min()) + 0.993 * contentSpan
                );
                std::printf(
                    "[16] near-full window (%.3f of the range): hThumb %d px of a %d px lane (hand %d)\n",
                    0.983, near.hThumb.width(), near.hTrack.width(), nearExpected.width()
                );
                CHECK(near.hThumb == nearExpected);
                CHECK(near.hThumb.width() < near.hTrack.width());

                editor.setFrameWindow(double(frames.min()), double(frames.max()) - 0.1);
                const gui::GraphPanelModel sliver = gui::buildGraphPanelModel(editor, viewport, palette);
                const double sliverMargin =
                    double(ctrl::graph::Scaler::kScrollBarTrailingMargin) / editor.scale().framePpf();
                const QRect sliverExpected = timelineThumb(
                    sliver.hTrack, true, false, double(frames.min()), double(frames.max()) + sliverMargin,
                    double(frames.min()), double(frames.max()) - 0.1
                );
                std::printf(
                    "[16] tenth-of-a-frame window: hThumb %d px of a %d px lane (hand %d)\n",
                    sliver.hThumb.width(), sliver.hTrack.width(), sliverExpected.width()
                );
                CHECK(sliver.hThumb == sliverExpected);
            }

            // ...and the window that COVERS the union track draws the FULL handle (not
            // a removed bar): the zero-travel state is the one the user sees beside a
            // timeline that always shows its own, so it must not be the one state with
            // no bar at all.
            editor.setFrameWindow(double(frames.min()), double(frames.max()) + contentSpan);
            const gui::GraphPanelModel whole = gui::buildGraphPanelModel(editor, viewport, palette);
            std::printf("[16] covering shared window: hThumb %d px of a %d px lane, lane laid out %d\n",
                        whole.hThumb.width(), whole.hTrack.width(), int(!whole.hTrack.isEmpty()));
            CHECK(whole.hThumb == whole.hTrack);
            CHECK(!whole.hTrack.isEmpty());

            // THE PROJECT'S OWN RANGE IS NOT A FULL HANDLE: the track reaches one
            // kTimeLineMargin past the last frame, exactly as the timeline's does, so a
            // window ending on the last frame leaves that margin of run.
            editor.setFrameWindow(double(frames.min()), double(frames.max()));
            const gui::GraphPanelModel range = gui::buildGraphPanelModel(editor, viewport, palette);
            const double rangeMargin =
                double(ctrl::graph::Scaler::kScrollBarTrailingMargin) / editor.scale().framePpf();
            const QRect rangeExpected = timelineThumb(
                range.hTrack, true, false, double(frames.min()), double(frames.max()) + rangeMargin,
                double(frames.min()), double(frames.max())
            );
            std::printf(
                "[16] full-range shared window: hThumb %d px of a %d px lane (hand %d; the trailing margin is "
                "the difference)\n",
                range.hThumb.width(), range.hTrack.width(), rangeExpected.width()
            );
            CHECK(range.hThumb == rangeExpected);
            CHECK(range.hThumb.width() < range.hTrack.width());
        }
        editor.clearFrameAxis(); // the frame axis is the panel's own again for what follows

        // AN EMPTY PLOT PUBLISHES NO BARS AT ALL (there is nowhere to lay one out), and
        // that is also what keeps the panel's margin clean in that state.
        const QSize degenerate(40, 20);
        const gui::GraphPanelModel degenerateModel = gui::buildGraphPanelModel(editor, degenerate, palette);
        CHECK(degenerateModel.plot.isEmpty());
        CHECK(degenerateModel.hTrack.isEmpty());
        CHECK(degenerateModel.hThumb.isEmpty());
        CHECK(degenerateModel.vTrack.isEmpty());
        CHECK(degenerateModel.vThumb.isEmpty());

        // The editor is left as the blocks around this one expect it.
        editor.scale().setRect(panel);
        editor.fitAll();
    }

    // -------------------------------------------------------------------------
    // THE AUTO PRESET APPLIES TO THE SEGMENT AFTER THE SELECTED KEY.
    //
    // A tangent PAIR belongs to one segment, so a key's OUTGOING side is the
    // start tangent of the segment leaving it and the TO key's INCOMING side its
    // end tangent. Applying Auto to the MIDDLE key of a three-key channel
    // therefore writes TWO params in ONE undo step:
    //   * the middle key's own TYPE (Custom, so its outgoing mode is consulted)
    //     and its OUTGOING side;
    //   * the last key's INCOMING side (its type is left alone: it shapes the
    //     segment after it, which the action does not own).
    // The PREVIOUS key's param is BYTE-IDENTICAL afterwards, and the middle key's
    // INCOMING side is untouched: both belong to the segment ARRIVING at the
    // middle key, which this action does not touch. So the segment AFTER the key
    // derives both sides and the segment BEFORE it derives neither.
    {
        editor.setTargets({ &nodeC });
        editor.clearFocus(); // no-focus ink: every row publishes its pairs
        editor.fitAll();

        const util::Easing::Param firstBefore = moveC0->data().easing(0);
        const util::Easing::Param middleBefore = moveC1->data().easing(0);
        const util::Easing::Param lastBefore = moveC2->data().easing(0);

        const bool selected =
            editor.selectMarker(nodeC, ctrl::graph::Channel(core::TimeKeyType_Move, 0), core::Frame(10));
        const bool acted = selected && editor.setSelectedKeysAutoHandles();

        const util::Easing::Param first = moveC0->data().easing(0);
        const util::Easing::Param middle = moveC1->data().easing(0);
        const util::Easing::Param last = moveC2->data().easing(0);
        // The SELECTED key: type Custom and the OUTGOING side automatic. Its
        // incoming side belongs to the arriving segment and is left exactly as it
        // was, as are the stored cubic, range and weight.
        const bool ownOutgoing = middle.type == util::Easing::Type_Custom
            && middle.handleOut == util::Easing::kAutoPresetHandleMode
            && middle.handleOut != util::Easing::HandleMode_Auto
            && middle.handleIn == middleBefore.handleIn
            && middle.cubicBezier == middleBefore.cubicBezier
            && middle.range == middleBefore.range
            && middle.weight == middleBefore.weight;
        // THE PREVIOUS KEY: not a single byte of its param moves.
        const bool prevUntouched = first == firstBefore;
        // The LEAVING segment's end tangent: the last key's incoming side only.
        const bool nextIncoming = last.handleIn == util::Easing::kAutoPresetHandleMode
            && last.handleIn != util::Easing::HandleMode_Auto
            && last.type == lastBefore.type
            && last.handleOut == lastBefore.handleOut
            && last.cubicBezier == lastBefore.cubicBezier
            && last.range == lastBefore.range
            && last.weight == lastBefore.weight;

        const gui::GraphPanelModel autoModel = gui::buildGraphPanelModel(editor, viewport, palette);
        ctrl::graph::Scaler oracle = editor.scale();
        oracle.setRect(ctrl::GraphEditor::panelRect(viewport));
        ctrl::graph::Curve source(*nodeC.timeLine(), ctrl::graph::Channel(core::TimeKeyType_Move, 0));
        const ctrl::graph::Curve::Segment firstSeg = source.segment(0);  // C0 -> C1, BEFORE the key
        const ctrl::graph::Curve::Segment secondSeg = source.segment(1); // C1 -> C2, AFTER the key
        // The semantics, stated directly: BOTH sides of the segment AFTER the key
        // derive (its curve is the middle key's Custom param and its end tangent
        // the last key's incoming mode), and NEITHER side of the segment BEFORE it
        // does (that curve is still the first key's analytic param).
        const bool afterAutomatic = ctrl::graph::Curve::derivesHandle(secondSeg, 0)
            && ctrl::graph::Curve::derivesHandle(secondSeg, 1);
        const bool beforePlain = !ctrl::graph::Curve::derivesHandle(firstSeg, 0)
            && !ctrl::graph::Curve::derivesHandle(firstSeg, 1);

        const gui::PanelCurve* curve = nullptr;
        for (const gui::PanelCurve& candidate : autoModel.curves) {
            if (candidate.node == &nodeC
                && candidate.channel == ctrl::graph::Channel(core::TimeKeyType_Move, 0)) {
                curve = &candidate;
                break;
            }
        }

        QStringList kinds;
        // ONLY the segment after the key is Custom, so only ITS two sides are
        // published - as ghosts (the arriving segment's param is analytic and
        // exposes no tangent at all, which is why its pair is not published).
        QPointF secondPair[2];
        const bool secondPairKnown = source.effectiveHandles(secondSeg, secondPair);
        bool oneSegmentGhosted = curve != nullptr && curve->handles.size() == 2 && secondPairKnown;
        bool dotsOk = curve != nullptr && curve->handles.size() == 2;
        if (curve) {
            for (const gui::PanelHandle& h : curve->handles) {
                kinds << QStringLiteral("%1:%2:%3").arg(h.segment).arg(h.index)
                             .arg(h.ghost ? QStringLiteral("ghost") : QStringLiteral("stored"));
                oneSegmentGhosted = oneSegmentGhosted && h.ghost && h.segment == 1;
                if (h.segment == 1)
                    dotsOk = dotsOk && h.dot == ctrl::graph::handlePointAt(oracle, secondSeg, secondPair[h.index]);
            }
        }
        const bool ok = acted && ownOutgoing && prevUntouched && nextIncoming && afterAutomatic
            && beforePlain && oneSegmentGhosted && dotsOk;
        std::printf("[10d] Auto middle key: selected=%d acted=%d  own outgoing=AutoClamped:%d  "
                    "prev untouched=%d  next incoming=%d  after segment automatic=%d  "
                    "before segment plain=%d  handles=[%s]  one segment ghosted=%d  dots=%d -> %s\n",
                    int(selected), int(acted), int(ownOutgoing), int(prevUntouched), int(nextIncoming),
                    int(afterAutomatic), int(beforePlain), qUtf8Printable(kinds.join(QLatin1Char(','))),
                    int(oneSegmentGhosted), int(dotsOk), ok ? "OK" : "FAIL");
        CHECK(ok);

        // ONE UNDO, THE WHOLE WRITE SET. The batch is one macro: a single undo
        // must put every param back (an action that pushed one command per param
        // would only revert the last write).
        project.commandStack().undo();
        const bool undone = moveC0->data().easing(0) == firstBefore
            && moveC1->data().easing(0) == middleBefore
            && moveC2->data().easing(0) == lastBefore;
        std::printf("[10d] one undo restores the written params (and leaves the untouched one equal)=%d\n",
                    int(undone));
        CHECK(undone);

        // Leave the editor as the blocks around this one expect it (nodeC is a
        // local fixture).
        editor.clearSelection();
        editor.setTargets({ &nodeA, &nodeB });
    }

    // -------------------------------------------------------------------------
    // 10e. PUBLICATION AND EVALUATION AGREE, PER SIDE.
    //
    // `PanelHandle::ghost` is a claim about the DRAWN and PLAYED curve: this side's
    // control point is DERIVED from the keys around the segment instead of being
    // read from the document's stored cubic. Assert the claim behaviourally instead
    // of trusting the flag: for every side of every Custom segment, evaluate the
    // segment the way playback does (util::Easing::calculate over the segment's
    // param and its core::autoEasingContext) and then change ONLY that side's
    // STORED control point. A derived side never reads it, so the value is
    // bit-identical; a stored side's value moves. The published flag must match
    // that answer exactly, and every derived side must be published as a ghost.
    //
    // The fixture also holds the divergence the old publication had: the LAST
    // segment's FROM key is still an analytic (Linear) curve while its TO key's
    // incoming mode is AutoClamped. Evaluation of that segment is Linear and never
    // calls controlPoint, so its TO side is not derived - and nothing may be
    // published for it. (The old model published that arriving side as a ghost,
    // which is why the panel drew an "automatic" handle on a curve that had none.)
    {
        core::TestNode nodeD;
        nodeD.setName(QStringLiteral("D"));
        const int dFrames[5] = { 0, 10, 20, 30, 40 };
        const float dValues[5] = { 0.0f, 10.0f, 40.0f, 70.0f, 100.0f };
        core::RotateKey* dKeys[5] = {};
        for (int i = 0; i < 5; ++i) {
            dKeys[i] = new core::RotateKey();
            dKeys[i]->setRotate(dValues[i]);
            setup.push(nodeD.timeLine()->createPusher(core::TimeKeyType_Rotate, dFrames[i], dKeys[i]));
        }
        // K0..K2 shape the segment leaving them; K2's incoming side is STORED
        // (Aligned) so one published handle is of the other kind. K3 is analytic:
        // its outgoing mode means nothing, and its arrival at K4 must not publish.
        for (int i = 0; i < 3; ++i) {
            dKeys[i]->data().easing(0).type = util::Easing::Type_Custom;
            dKeys[i]->data().easing(0).handleOut = util::Easing::HandleMode_AutoClamped;
        }
        dKeys[0]->data().easing(0).handleIn = util::Easing::HandleMode_Aligned;
        dKeys[1]->data().easing(0).handleIn = util::Easing::HandleMode_AutoClamped;
        dKeys[2]->data().easing(0).handleIn = util::Easing::HandleMode_Aligned;
        dKeys[3]->data().easing(0).type = util::Easing::Type_Linear;
        dKeys[3]->data().easing(0).handleIn = util::Easing::HandleMode_AutoClamped;
        dKeys[4]->data().easing(0).type = util::Easing::Type_Custom;
        dKeys[4]->data().easing(0).handleIn = util::Easing::HandleMode_AutoClamped;

        editor.setTargets({ &nodeD });
        editor.clearSelection();
        editor.clearFocus(); // no-focus ink: every row publishes its pairs
        editor.fitAll();
        const gui::GraphPanelModel gateModel = gui::buildGraphPanelModel(editor, viewport, palette);
        ctrl::graph::Scaler gateScale = editor.scale();
        gateScale.setRect(ctrl::GraphEditor::panelRect(viewport));

        const ctrl::graph::Curve gateCurve(*nodeD.timeLine(),
                                           ctrl::graph::Channel(core::TimeKeyType_Rotate, 0));
        const core::TimeLine::MapType& dMap =
            nodeD.timeLine()->map(core::TimeKeyType_Rotate);

        int published = 0;
        int badKind = 0;
        int missing = 0;
        int analyticPublished = 0;
        int ghostDotOffMapping = 0;
        int derivedSides = 0;
        int storedSides = 0;

        for (const gui::PanelCurve& modelCurve : gateModel.curves) {
            if (modelCurve.node != &nodeD
                || modelCurve.channel != ctrl::graph::Channel(core::TimeKeyType_Rotate, 0))
                continue;

            for (int s = 0; s < gateCurve.segmentCount(); ++s) {
                const ctrl::graph::Curve::Segment seg = gateCurve.segment(s);
                if (!seg.from || !seg.to)
                    continue;

                // Every published handle must belong to a Custom FROM param: an
                // analytic segment's curve has no control point to draw or grab.
                int onSegment = 0;
                for (const gui::PanelHandle& handle : modelCurve.handles) {
                    if (handle.segment == s)
                        ++onSegment;
                }
                published += onSegment;
                if (seg.easing.type != util::Easing::Type_Custom) {
                    analyticPublished += onSegment;
                    continue; // nothing on an analytic segment can be a handle
                }

                for (const gui::PanelHandle& handle : modelCurve.handles) {
                    if (handle.segment != s || !handle.ghost)
                        continue;
                    // The dot is the side's EFFECTIVE control point through the
                    // editor's own mapping.
                    QPointF pair[2];
                    if (gateCurve.effectiveHandles(seg, pair)
                        && handle.dot != ctrl::graph::handlePointAt(gateScale, seg, pair[handle.index]))
                        ++ghostDotOffMapping;
                }
                if (onSegment != 2)
                    ++missing; // every Custom segment has two sides

                // The evaluation oracle, exactly as playback builds it.
                const core::TimeKey* prev = nullptr;
                const core::TimeKey* next = nullptr;
                auto self = dMap.constFind(seg.from->frame());
                if (self != dMap.constEnd()) {
                    if (self != dMap.constBegin()) {
                        auto before = self;
                        --before;
                        prev = before.value();
                    }
                    auto after = self;
                    ++after;
                    if (after != dMap.constEnd())
                        next = after.value();
                }
                util::Easing::AutoContext ctx =
                    core::autoEasingContext(*seg.from, *seg.to, 0, prev, next); // Rotate: one component
                ctx.inMode = seg.inMode;
                const float d = static_cast<float>(seg.to->frame() - seg.from->frame());
                const float t = 0.5f * d;

                for (int h = 0; h < 2; ++h) {
                    const float base = util::Easing::calculate(seg.easing, ctx, t, 0.0f, 1.0f, d);
                    util::Easing::Param perturbed = seg.easing;
                    ctrl::graph::Curve::setHandle(perturbed, h, QPointF(0.05f, 0.93f));
                    const float changed = util::Easing::calculate(perturbed, ctx, t, 0.0f, 1.0f, d);
                    const bool derivedByEvaluation = (changed == base);
                    if (derivedByEvaluation)
                        ++derivedSides;
                    else
                        ++storedSides;

                    const gui::PanelHandle* found = nullptr;
                    for (const gui::PanelHandle& handle : modelCurve.handles) {
                        if (handle.segment == s && handle.index == h)
                            found = &handle;
                    }
                    if (!found) {
                        ++missing;
                        continue;
                    }
                    if (found->ghost != derivedByEvaluation)
                        ++badKind;
                }
            }
        }
        std::printf("[10e] publication vs evaluation: published=%d derived sides=%d stored sides=%d  "
                    "kind mismatches=%d  missing sides=%d  analytic published=%d  ghost dots off mapping=%d\n",
                    published, derivedSides, storedSides, badKind, missing, analyticPublished,
                    ghostDotOffMapping);
        // 6 = three Custom segments (0,1,2) x two sides; the analytic K3 -> K4
        // segment publishes nothing at all.
        CHECK(published == 6);
        CHECK(derivedSides == 5); // K2's incoming side is the one stored side
        CHECK(storedSides == 1);
        CHECK(badKind == 0);
        CHECK(missing == 0);
        CHECK(analyticPublished == 0);
        CHECK(ghostDotOffMapping == 0);

        editor.clearSelection();
        editor.setTargets({ &nodeA, &nodeB });
    }

    // -------------------------------------------------------------------------
    // 10f. THE TIMELINE'S OWN SELECTION ROUTE WRITES THE SAME SEGMENT RULE.
    //
    // The quick easings and the picker's Auto tile commit through
    // ctrl::TimeLineEditor::assignSelectionAutoHandles, not through the graph
    // panel's action, so the rule must live in the shared appendAutoPresetWrites.
    // One key selected on a three-key channel: the segment AFTER it - its own
    // type and outgoing side plus the next key's incoming side, two params, one
    // undo - and the previous key's param is byte-identical.
    {
        core::TestNode nodeE;
        nodeE.setName(QStringLiteral("E"));
        const int eFrames[3] = { 0, 10, 20 };
        const float eValues[3] = { 0.0f, 10.0f, 40.0f };
        core::RotateKey* eKeys[3] = {};
        for (int i = 0; i < 3; ++i) {
            eKeys[i] = new core::RotateKey();
            eKeys[i]->setRotate(eValues[i]);
            setup.push(nodeE.timeLine()->createPusher(core::TimeKeyType_Rotate, eFrames[i], eKeys[i]));
        }

        ctrl::TimeLineEditor timeline;
        timeline.setProject(&project);

        const util::Easing::Param eFirstBefore = eKeys[0]->data().easing(0);
        const util::Easing::Param eMiddleBefore = eKeys[1]->data().easing(0);
        const util::Easing::Param eLastBefore = eKeys[2]->data().easing(0);

        core::TimeLineEvent created;
        created.setType(core::TimeLineEvent::Type_PushKey);
        created.pushTarget(nodeE, core::TimeKeyType_Rotate, 10);
        timeline.selectCreatedKeys(created);
        const bool acted = timeline.assignSelectionAutoHandles();

        const bool wrote = acted
            && eKeys[0]->data().easing(0) == eFirstBefore // previous key: byte-identical
            && eKeys[1]->data().easing(0).type == util::Easing::Type_Custom
            && eKeys[1]->data().easing(0).handleOut == util::Easing::kAutoPresetHandleMode
            && eKeys[1]->data().easing(0).handleIn == eMiddleBefore.handleIn // arriving side untouched
            && eKeys[2]->data().easing(0).handleIn == util::Easing::kAutoPresetHandleMode
            && eKeys[2]->data().easing(0).type == eLastBefore.type // its own curve is not ours
            && eKeys[2]->data().easing(0).handleOut == eLastBefore.handleOut;

        project.commandStack().undo();
        const bool undone = eKeys[0]->data().easing(0) == eFirstBefore
            && eKeys[1]->data().easing(0) == eMiddleBefore
            && eKeys[2]->data().easing(0) == eLastBefore;

        std::printf("[10f] timeline Auto on one key: acted=%d  two params written=%d  "
                    "one undo restores all=%d\n",
                    int(acted), int(wrote), int(undone));
        CHECK(acted);
        CHECK(wrote);
        CHECK(undone);

        // TWO ADJACENT SELECTED KEYS: each writes the segment after ITSELF, so
        // the shared middle key is named by its own entry (type + outgoing) and
        // by the first key's (incoming) - the merged write keeps BOTH of its
        // sides automatic, and the first key's incoming side is still untouched
        // (it belongs to the segment arriving at it). One undo restores all.
        core::TimeLineEvent adjacent;
        adjacent.setType(core::TimeLineEvent::Type_PushKey);
        adjacent.pushTarget(nodeE, core::TimeKeyType_Rotate, 0);
        adjacent.pushTarget(nodeE, core::TimeKeyType_Rotate, 10);
        timeline.selectCreatedKeys(adjacent);
        const bool actedBoth = timeline.assignSelectionAutoHandles();
        const bool sharedKept = actedBoth
            && eKeys[0]->data().easing(0).type == util::Easing::Type_Custom
            && eKeys[0]->data().easing(0).handleOut == util::Easing::kAutoPresetHandleMode
            && eKeys[0]->data().easing(0).handleIn == eFirstBefore.handleIn
            && eKeys[1]->data().easing(0).handleIn == util::Easing::kAutoPresetHandleMode
            && eKeys[1]->data().easing(0).handleOut == util::Easing::kAutoPresetHandleMode
            && eKeys[2]->data().easing(0).handleIn == util::Easing::kAutoPresetHandleMode
            && eKeys[2]->data().easing(0).type == eLastBefore.type;
        project.commandStack().undo();
        const bool bothUndone = eKeys[0]->data().easing(0) == eFirstBefore
            && eKeys[1]->data().easing(0) == eMiddleBefore
            && eKeys[2]->data().easing(0) == eLastBefore;
        std::printf("[10f] timeline Auto on two adjacent keys: acted=%d  shared key keeps both sides=%d  "
                    "one undo restores all=%d\n",
                    int(actedBoth), int(sharedKept), int(bothUndone));
        CHECK(actedBoth);
        CHECK(sharedKept);
        CHECK(bothUndone);

        // A LAST KEY HAS NO SEGMENT AFTER IT YET, so the preset writes the key's
        // OWN pair: its type becomes Custom and BOTH sides become automatic, so
        // the segments that appear when a neighbour is added derive on both
        // sides. Nothing else is named - the two earlier params stay
        // byte-identical - and one undo restores the key.
        core::TimeLineEvent lastOnly;
        lastOnly.setType(core::TimeLineEvent::Type_PushKey);
        lastOnly.pushTarget(nodeE, core::TimeKeyType_Rotate, 20);
        timeline.selectCreatedKeys(lastOnly);
        const bool actedLast = timeline.assignSelectionAutoHandles();
        const util::Easing::Param lastAfter = eKeys[2]->data().easing(0);
        const bool lastPair = lastAfter.type == util::Easing::Type_Custom
            && lastAfter.handleIn == util::Easing::kAutoPresetHandleMode
            && lastAfter.handleOut == util::Easing::kAutoPresetHandleMode
            && lastAfter.cubicBezier == eLastBefore.cubicBezier
            && lastAfter.range == eLastBefore.range
            && lastAfter.weight == eLastBefore.weight;
        const bool lastOnlyWrote = actedLast && lastPair
            && eKeys[0]->data().easing(0) == eFirstBefore // no other param was named
            && eKeys[1]->data().easing(0) == eMiddleBefore;
        project.commandStack().undo();
        const bool lastUndone = eKeys[0]->data().easing(0) == eFirstBefore
            && eKeys[1]->data().easing(0) == eMiddleBefore
            && eKeys[2]->data().easing(0) == eLastBefore;
        std::printf("[10f] timeline Auto on the LAST key: acted=%d  own pair automatic=%d  "
                    "nothing else written=%d  one undo restores it=%d\n",
                    int(actedLast), int(lastPair), int(lastOnlyWrote), int(lastUndone));
        CHECK(actedLast);
        CHECK(lastPair);
        CHECK(lastOnlyWrote);
        CHECK(lastUndone);

        // THE SINGLE-KEY CHANNEL: a property with only ONE key. The same rule
        // applies - Auto must MARK that key automatic (both sides + Custom), not
        // silently do nothing because there is no next key yet. The key is the
        // only param in the document, and its stored cubic, range and weight
        // ride along untouched.
        core::TestNode soloNode;
        soloNode.setName(QStringLiteral("Solo"));
        core::RotateKey* soloKey = new core::RotateKey();
        soloKey->setRotate(25.0f);
        setup.push(soloNode.timeLine()->createPusher(core::TimeKeyType_Rotate, 5, soloKey));
        CHECK(soloNode.timeLine()->map(core::TimeKeyType_Rotate).size() == 1);

        core::TimeLineEvent soloCreated;
        soloCreated.setType(core::TimeLineEvent::Type_PushKey);
        soloCreated.pushTarget(soloNode, core::TimeKeyType_Rotate, 5);
        timeline.selectCreatedKeys(soloCreated);
        const util::Easing::Param soloBefore = soloKey->data().easing(0);
        const bool soloActed = timeline.assignSelectionAutoHandles();
        const util::Easing::Param soloAfter = soloKey->data().easing(0);
        const bool soloPair = soloAfter.type == util::Easing::Type_Custom
            && soloAfter.handleIn == util::Easing::kAutoPresetHandleMode
            && soloAfter.handleOut == util::Easing::kAutoPresetHandleMode
            && soloAfter.cubicBezier == soloBefore.cubicBezier
            && soloAfter.range == soloBefore.range
            && soloAfter.weight == soloBefore.weight
            && soloNode.timeLine()->map(core::TimeKeyType_Rotate).size() == 1;
        project.commandStack().undo();
        const bool soloUndone = soloKey->data().easing(0) == soloBefore;
        std::printf("[10f] timeline Auto on the ONLY key: acted=%d  both sides automatic=%d  "
                    "one undo restores it=%d\n",
                    int(soloActed), int(soloPair), int(soloUndone));
        CHECK(soloActed);
        CHECK(soloPair);
        CHECK(soloUndone);
    }

    // -------------------------------------------------------------------------
    // 17. THE SCROLL BAR'S PILL IS ONE GEOMETRY, and both painters paint it.
    //
    // The application's scroll bar is gui::AppStyle's CC_ScrollBar (the timeline's
    // bar, and the ink the graph panel's two lanes copy) and the panel paints
    // scrollBar::pill(track, thumb, axis) (src/gui/GraphPanelFrame.cpp). Both read
    // the lane, the pill and its centring from
    // src/ctrl/graph/graph_ScrollBarGeometry.h, and this section holds each of them
    // to a measurement of that one function:
    //   * the geometry TABLE - (lane, run, orientation) -> pill, written out by
    //     hand, with the cases integer division and a non-zero origin make
    //     interesting (an odd lane, an offset lane, a zero-travel full run);
    //   * AppStyle PAINTED - a real QScrollBar through the real style into a
    //     transparent QImage, then the pill read off the pixels: its band's exact
    //     rows/columns, its span, no ink outside the rect, and the OPAQUE token
    //     held back by exactly the radius at each cap;
    //   * the panel's PUBLISHED lane/run rects, through the same function - its
    //     drawn pixels are measured in tests/graph_panel_pixels.cpp.
    {
        using ctrl::graph::scrollBar::kLaneThickness;
        using ctrl::graph::scrollBar::kPillRadius;
        using ctrl::graph::scrollBar::kPillThickness;
        using ctrl::graph::scrollBar::Pill;
        using ctrl::graph::scrollBar::pill;

        const PillCase cases[] = {
            // The panel's horizontal lane at 800x600 with a run 60px in:
            // 588 + (12 - 4) / 2 = 592.
            { "panel horizontal", QRect(0, 588, 800, 12), QRect(60, 588, 90, 12), Qt::Horizontal,
              QRect(60, 592, 90, 4) },
            // The panel's vertical lane, a 70px run 40px down: 788 + (12 - 4) / 2 = 792.
            { "panel vertical", QRect(788, 26, 12, 562), QRect(788, 66, 12, 70), Qt::Vertical,
              QRect(792, 66, 4, 70) },
            // A ZERO-TRAVEL lane: the run IS the lane, and the pill spans it whole.
            { "full lane", QRect(20, 0, 12, 200), QRect(20, 0, 12, 200), Qt::Vertical,
              QRect(24, 0, 4, 200) },
            // An ODD lane: the 9px difference is floored to 4, so the extra pixel
            // stays on the TRAILING side (4px before the pill, 5px after it).
            { "odd lane vertical", QRect(5, 0, 13, 100), QRect(5, 10, 13, 60), Qt::Vertical,
              QRect(9, 10, 4, 60) },
            { "odd lane horizontal", QRect(0, 7, 300, 13), QRect(11, 7, 100, 13), Qt::Horizontal,
              QRect(11, 11, 100, 4) },
            // A lane whose ORIGIN is not (0,0): both painters are given widget rects,
            // and the pill must follow the lane it was handed, not the image origin.
            { "offset lane horizontal", QRect(30, 90, 120, 12), QRect(40, 90, 50, 12), Qt::Horizontal,
              QRect(40, 94, 50, 4) },
        };
        for (const PillCase& c : cases) {
            const Pill got = pill(c.lane, c.run, c.orientation);
            const bool vertical = c.orientation == Qt::Vertical;
            const int capExtent = vertical ? got.rect.width() : got.rect.height();
            const int runSpan = vertical ? c.run.height() : c.run.width();
            std::printf(
                "[17] pill %s: lane(%d,%d %dx%d) run(%d,%d %dx%d) %s -> (%d,%d %dx%d) r%d  by hand"
                " (%d,%d %dx%d), cap axis %dpx, run span %dpx\n",
                c.what, c.lane.left(), c.lane.top(), c.lane.width(), c.lane.height(), c.run.left(),
                c.run.top(), c.run.width(), c.run.height(), vertical ? "vertical" : "horizontal",
                got.rect.left(), got.rect.top(), got.rect.width(), got.rect.height(), got.radius,
                c.expected.left(), c.expected.top(), c.expected.width(), c.expected.height(), capExtent,
                runSpan
            );
            CHECK(got.rect == c.expected);
            CHECK(capExtent == kPillThickness);
            CHECK(got.radius == kPillRadius);
            CHECK((vertical ? got.rect.height() : got.rect.width()) == runSpan);
            CHECK(c.lane.contains(got.rect));
        }

        // --- AppStyle, PAINTED ---
        // A QCommonStyle base: the proxy needs one, and nothing here asks the base to
        // paint - only AppStyle's own CC_ScrollBar branch runs.
        // QProxyStyle TAKES OWNERSHIP of the base style it is handed and deletes it,
        // so the base must outlive nothing and the stack must not own it: a stack
        // QCommonStyle here was freed twice (measured: "double free or corruption
        // (out)" in AppStyle::~AppStyle, via QObject::~QObject deleting children).
        QCommonStyle* baseStyle = new QCommonStyle;
        gui::AppStyle appStyle(baseStyle);
        QScrollBar bar;
        const theme::Colors& colors = theme::Colors::current();
        struct PaintCase {
            const char* what;
            QSize lane;
            int maximum;
            int pageStep;
            int value;
        };
        const PaintCase paintCases[] = {
            { "horizontal", QSize(200, kLaneThickness), 100, 30, 10 },
            { "vertical", QSize(kLaneThickness, 200), 100, 30, 10 },
            { "horizontal zero travel", QSize(200, kLaneThickness), 100, 100, 0 },
            { "vertical window at the end", QSize(kLaneThickness, 200), 100, 25, 75 },
        };
        for (const PaintCase& pc : paintCases) {
            const Qt::Orientation orientation =
                pc.lane.height() > pc.lane.width() ? Qt::Vertical : Qt::Horizontal;
            const bool horizontal = orientation == Qt::Horizontal;
            bar.setOrientation(orientation);
            bar.resize(pc.lane);
            bar.setRange(0, pc.maximum);
            bar.setPageStep(pc.pageStep);
            bar.setValue(pc.value);

            const QRect lane(QPoint(0, 0), pc.lane);
            QRect slider;
            const QImage image = paintAppScrollBar(appStyle, bar, lane, slider);
            const Pill expected = pill(lane, slider, orientation);
            const QRect rect = expected.rect;

            // No ink anywhere but inside the shared rect...
            int inked = 0;
            int outside = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    if (qAlpha(image.pixel(x, y)) == 0)
                        continue;
                    ++inked;
                    if (!rect.contains(x, y))
                        ++outside;
                }
            }
            // ...the band and the span, read through the pill's MIDDLE (its straight
            // section, where coverage is full, so the extent is unambiguous)...
            const QRect alongProbe = horizontal ? QRect(rect.left(), rect.center().y(), rect.width(), 1)
                                                : QRect(rect.center().x(), rect.top(), 1, rect.height());
            const QRect bandProbe = horizontal ? QRect(rect.center().x(), rect.top(), 1, rect.height())
                                               : QRect(rect.left(), rect.center().y(), rect.width(), 1);
            const auto inkedAt = [&image](int x, int y) { return qAlpha(image.pixel(x, y)) > 0; };
            const Span along = pixelSpan(image, alongProbe, horizontal, inkedAt);
            const Span band = pixelSpan(image, bandProbe, !horizontal, inkedAt);
            // ...and the CAPS: the opaque token stops one radius short at each end of
            // the run (a square-capped rect would run to the rect's edges, and a
            // different rounding would stop somewhere else).
            const auto tokenAt = [&image, &colors](int x, int y) {
                const QRgb p = image.pixel(x, y);
                return qAlpha(p) == 255 && std::abs(qRed(p) - colors.outline.red()) <= 1
                    && std::abs(qGreen(p) - colors.outline.green()) <= 1
                    && std::abs(qBlue(p) - colors.outline.blue()) <= 1;
            };
            const Span tokenAlong = pixelSpan(image, alongProbe, horizontal, tokenAt);

            const int alongLo = horizontal ? rect.left() : rect.top();
            const int alongHi = horizontal ? rect.right() : rect.bottom();
            const int bandLo = horizontal ? rect.top() : rect.left();
            const int bandHi = horizontal ? rect.bottom() : rect.right();
            std::printf(
                "[17] appstyle %s: lane(%d,%d %dx%d) slider(%d,%d %dx%d) -> pill(%d,%d %dx%d) r%d  inked=%d"
                " outside=%d  span=%d..%d band=%d..%d token=%d..%d  (expected span %d..%d, band %d..%d,"
                " token %d..%d)\n",
                pc.what, lane.left(), lane.top(), lane.width(), lane.height(), slider.left(), slider.top(),
                slider.width(), slider.height(), rect.left(), rect.top(), rect.width(), rect.height(),
                expected.radius, inked, outside, along.lo, along.hi, band.lo, band.hi, tokenAlong.lo,
                tokenAlong.hi, alongLo, alongHi, bandLo, bandHi, alongLo + kPillRadius, alongHi - kPillRadius
            );
            CHECK(inked > 0);
            CHECK(outside == 0);
            CHECK(expected.radius == kPillRadius);
            CHECK((horizontal ? rect.height() : rect.width()) == kPillThickness);
            CHECK(along.lo == alongLo && along.hi == alongHi);
            CHECK(band.lo == bandLo && band.hi == bandHi);
            // The EXACT token run stops one pixel short of the radius at each cap: the
            // cap's last pixel is an antialiased blend, not the token, so the run of
            // exact-token pixels is [lo + radius - 1, hi - radius + 1] (measured on all
            // four cases: 27..64 for a pill 26..65 at radius 2). The full SPAN above is
            // still asserted exactly - the ink reaches the pill's own edges.
            CHECK(tokenAlong.lo >= alongLo + kPillRadius - 1 && tokenAlong.lo <= alongLo + kPillRadius);
            CHECK(tokenAlong.hi <= alongHi - kPillRadius + 1 && tokenAlong.hi >= alongHi - kPillRadius);
        }

        // --- THE PANEL'S PUBLISHED RECTS ---
        // The model publishes Scaler::scrollBars' lanes and runs (hTrack/hThumb,
        // vTrack/vThumb) and the drawing layer paints scrollBar::pill of them. What the
        // model can be held to here is the geometry: the app's 4px band, centred in a
        // 12px lane, spanning its run and inside it - at the shipping panel size and at
        // a short one.
        const QSize panelSizes[] = { QSize(800, 600), QSize(320, 200) };
        for (const QSize panelSize : panelSizes) {
            ctrl::graph::Scaler panelScale;
            panelScale.setRect(QRect(QPoint(0, 0), panelSize));
            const ctrl::graph::Scaler::ScrollBars bars = panelScale.scrollBars(0.0, 100.0, -10.0f, 10.0f);
            const Pill hPill = pill(bars.hTrack, bars.hThumb, Qt::Horizontal);
            const Pill vPill = pill(bars.vTrack, bars.vThumb, Qt::Vertical);
            std::printf(
                "[17] published panel %dx%d: hTrack(%d,%d %dx%d) hThumb(%d,%d %dx%d) -> hPill(%d,%d %dx%d)"
                "  vTrack(%d,%d %dx%d) vThumb(%d,%d %dx%d) -> vPill(%d,%d %dx%d)\n", panelSize.width(),
                panelSize.height(), bars.hTrack.left(), bars.hTrack.top(), bars.hTrack.width(),
                bars.hTrack.height(), bars.hThumb.left(), bars.hThumb.top(), bars.hThumb.width(),
                bars.hThumb.height(), hPill.rect.left(), hPill.rect.top(), hPill.rect.width(),
                hPill.rect.height(), bars.vTrack.left(), bars.vTrack.top(), bars.vTrack.width(),
                bars.vTrack.height(), bars.vThumb.left(), bars.vThumb.top(), bars.vThumb.width(),
                bars.vThumb.height(), vPill.rect.left(), vPill.rect.top(), vPill.rect.width(),
                vPill.rect.height()
            );
            CHECK(!bars.hTrack.isEmpty() && !bars.vTrack.isEmpty());
            CHECK(bars.hTrack.height() == kLaneThickness && bars.vTrack.width() == kLaneThickness);
            CHECK(hPill.rect.height() == kPillThickness && vPill.rect.width() == kPillThickness);
            CHECK(hPill.radius == kPillRadius && vPill.radius == kPillRadius);
            // Centred in the lane: the even (12px) lane's pill shares its centre row /
            // column with the lane.
            CHECK(hPill.rect.center().y() == bars.hTrack.center().y());
            CHECK(vPill.rect.center().x() == bars.vTrack.center().x());
            // Spanning the run exactly, and inside the lane.
            CHECK(hPill.rect.left() == bars.hThumb.left() && hPill.rect.right() == bars.hThumb.right());
            CHECK(vPill.rect.top() == bars.vThumb.top() && vPill.rect.bottom() == bars.vThumb.bottom());
            CHECK(bars.hTrack.contains(hPill.rect) && bars.vTrack.contains(vPill.rect));
        }
    }

    // -------------------------------------------------------------------------
    // 18. THE FRAME RANGE'S GUIDES. The timeline draws two vertical hairlines at the
    // project's first and last frame, continuing the ruler's boundary ticks down the
    // body (ctrl/time/time_BodyModel.cpp's buildRangeMarkers); the graph publishes the
    // same two marks as COLUMNS. What the model can be held to is their mapping and
    // their classification: each column is the frame's pixel through the SAME scaler the
    // ticks and the playhead use, they are PANEL columns (the shared mapping puts the
    // project's first frame inside the value column, so a guide clipped to the plot
    // would vanish exactly at the start of the range), a range whose two ends round onto
    // one column is ONE guide (the timeline's degenerate-range rule), and the recede
    // flag follows the play window. The rows, the ink and the two alphas are the drawing
    // layer's (asserted in tests/graph_panel_pixels.cpp).
    {
        editor.setProject(&project);
        editor.setTargets({ &nodeA, &nodeB });
        editor.setPlayWindow(ctrl::TimeLineEditor::PlayWindow());

        // (a) the mapping: the two ends of the project's range, in the project's order.
        editor.setFrameWindow(0.0, 130.0);
        editor.syncFrameWindowFor(viewport);
        ctrl::graph::Scaler guideScale = editor.scale();
        guideScale.setRect(panel);
        const gui::GraphPanelModel ranged = gui::buildGraphPanelModel(editor, viewport, palette);
        const int firstX = guideScale.pixelX(core::Frame(project.attribute().frameRange().min()));
        const int lastX = guideScale.pixelX(core::Frame(project.attribute().frameRange().max()));
        std::printf(
            "[18] range %d..%d -> guides=%d [%d, %d]  oracle (%d, %d)  plot.left=%d  panel.left=%d\n",
            project.attribute().frameRange().min(), project.attribute().frameRange().max(),
            isz(ranged.rangeGuideX), ranged.rangeGuideX.value(0), ranged.rangeGuideX.value(1), firstX, lastX,
            ranged.plot.left(), ranged.panel.left()
        );
        CHECK(ranged.rangeGuideX.size() == 2);
        CHECK(ranged.rangeGuideX.value(0) == firstX);
        CHECK(ranged.rangeGuideX.value(1) == lastX);
        // PANEL COLUMNS: the first frame of the range sits at the panel's left edge here -
        // left of the plot, inside the value column - and the model still publishes it.
        CHECK(ranged.rangeGuideX.value(0) < ranged.plot.left());
        CHECK(ranged.rangeGuideX.value(0) >= ranged.panel.left());
        CHECK(ranged.plot.contains(ranged.rangeGuideX.value(1), ranged.plot.top()));

        // (b) the classification: an active play window is what makes the guides recede
        // (the timeline drops them from 40% to 25% then), and it moves neither column -
        // the guides mark the PROJECT's range, which a temporary window does not change.
        ctrl::TimeLineEditor::PlayWindow window;
        window.set(project.attribute().frameRange().min() + 5, project.attribute().frameRange().max() - 5);
        editor.setPlayWindow(window);
        const gui::GraphPanelModel windowed = gui::buildGraphPanelModel(editor, viewport, palette);
        std::printf(
            "[18] play window %d..%d -> recede=%d  guides=[%d, %d]\n", window.start, window.end,
            int(windowed.rangeGuideRecede), windowed.rangeGuideX.value(0), windowed.rangeGuideX.value(1)
        );
        CHECK(!ranged.rangeGuideRecede);
        CHECK(windowed.rangeGuideRecede);
        CHECK(windowed.rangeGuideX == ranged.rangeGuideX);
        editor.setPlayWindow(ctrl::TimeLineEditor::PlayWindow());

        // (c) a range wider than the panel: the columns leave the panel, and the model
        // still states them (the drawing layer clips; a mark whose frame is off-screen is
        // a coordinate, not an omission).
        editor.setFrameWindow(0.0, 60.0);
        editor.syncFrameWindowFor(viewport);
        ctrl::graph::Scaler wideScale = editor.scale();
        wideScale.setRect(panel);
        const gui::GraphPanelModel wide = gui::buildGraphPanelModel(editor, viewport, palette);
        std::printf(
            "[18] window 0..60: guides=[%d, %d]  oracle (%d, %d)  panel.right=%d\n",
            wide.rangeGuideX.value(0), wide.rangeGuideX.value(1),
            wideScale.pixelX(core::Frame(project.attribute().frameRange().min())),
            wideScale.pixelX(core::Frame(project.attribute().frameRange().max())), wide.panel.right()
        );
        CHECK(wide.rangeGuideX.value(1) == wideScale.pixelX(core::Frame(project.attribute().frameRange().max())));
        CHECK(wide.rangeGuideX.value(1) > wide.panel.right());

        // (d) the DEGENERATE range: a window so wide that both ends round onto one column
        // is ONE guide - the timeline's own rule, so nothing overdraws one pixel.
        editor.setFrameWindow(0.0, 1.0e6);
        editor.syncFrameWindowFor(viewport);
        ctrl::graph::Scaler degenerateScale = editor.scale();
        degenerateScale.setRect(panel);
        const gui::GraphPanelModel degenerate = gui::buildGraphPanelModel(editor, viewport, palette);
        std::printf(
            "[18] window 0..1e6: guides=%d [%d]  oracle (%d, %d)\n", isz(degenerate.rangeGuideX),
            degenerate.rangeGuideX.value(0),
            degenerateScale.pixelX(core::Frame(project.attribute().frameRange().min())),
            degenerateScale.pixelX(core::Frame(project.attribute().frameRange().max()))
        );
        CHECK(degenerateScale.pixelX(core::Frame(project.attribute().frameRange().min()))
              == degenerateScale.pixelX(core::Frame(project.attribute().frameRange().max())));
        CHECK(degenerate.rangeGuideX.size() == 1);
        CHECK(degenerate.rangeGuideX.value(0) == degenerateScale.pixelX(core::Frame(project.attribute().frameRange().min())));

        // (e) no project: no range, no guides - and no stale column left from (d).
        editor.setProject(nullptr);
        const gui::GraphPanelModel orphaned = gui::buildGraphPanelModel(editor, viewport, palette);
        std::printf("[18] no project: guides=%d\n", isz(orphaned.rangeGuideX));
        CHECK(orphaned.rangeGuideX.isEmpty());
        editor.setProject(&project);
        editor.setTargets({ &nodeA, &nodeB });
        editor.clearFrameAxis();
    }

    // -------------------------------------------------------------------------
    // read-only probe: a build must not move the editor or the document
    {
        editor.setProject(&project);
        editor.setTargets({ &nodeA, &nodeB });
        animator.setFrame(core::Frame(12));

        const QRect rectBefore = editor.scale().rect();
        const float minBefore = editor.scale().valueMin();
        const float maxBefore = editor.scale().valueMax();
        const int targetsBefore = editor.targets().size();
        const int rotateKeysBefore = nodeA.timeLine()->map(core::TimeKeyType_Rotate).size();
        const bool modifiedBefore = project.isModified();
        const int frameBefore = editor.currentFrame().get();

        const gui::GraphPanelModel probe = gui::buildGraphPanelModel(editor, viewport, palette);
        const gui::GraphPanelModel probeAgain = gui::buildGraphPanelModel(editor, viewport, palette);

        std::printf("[R] scale rect %d  window %.6f..%.6f (before %.6f..%.6f)  targets %d  rotate keys %d  "
                    "project.isModified=%d  currentFrame=%d  rebuild identical=%d\n",
                    int(editor.scale().rect() == rectBefore), editor.scale().valueMin(), editor.scale().valueMax(),
                    minBefore, maxBefore, isz(editor.targets()), isz(nodeA.timeLine()->map(core::TimeKeyType_Rotate)),
                    int(project.isModified()), editor.currentFrame().get(),
                    int(probe.curves.size() == probeAgain.curves.size() &&
                        probe.frameTickX == probeAgain.frameTickX && probe.playheadX == probeAgain.playheadX));

        CHECK(editor.scale().rect() == rectBefore);
        CHECK(editor.scale().valueMin() == minBefore);
        CHECK(editor.scale().valueMax() == maxBefore);
        CHECK(editor.targets().size() == targetsBefore);
        CHECK(nodeA.timeLine()->map(core::TimeKeyType_Rotate).size() == rotateKeysBefore);
        CHECK(project.isModified() == modifiedBefore);
        CHECK(editor.currentFrame().get() == frameBefore);
        CHECK(probe.playheadX == probeAgain.playheadX);
        CHECK(probe.frameTickX == probeAgain.frameTickX);
        // The tiers are part of the same pure function: the ladder does not move
        // between two builds either, so the classification is reproducible too.
        CHECK(probe.frameTickLevel == probeAgain.frameTickLevel);
        CHECK(probe.curves.size() == probeAgain.curves.size());
    }

    std::printf("%s: %d failed assertion(s)\n", gFailures == 0 ? "PASS" : "FAIL", gFailures);
    return gFailures == 0 ? 0 : 1;
}
