// Contract test for the engine-neutral graph panel model: gui::buildGraphPanelModel
// (src/gui/GraphPanelModel.h/.cpp).
//
// The model is what the overlay (Dear ImGui now, the QPainter renderer until
// then) draws the graph panel from, so what is pinned below is the geometry a
// drawing layer would consume: the panel/plot/value-column rectangles, the two
// SHORT tick axes with their label rects, the curves with their keys, the legend
// with its swatches, the playhead, and the empty state. Every assertion prints
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
//   2 no grid: the tick axes are POSITION lists (static_assert on their types),
//     no published rect spans the plot, every published frame tick is a frame the
//     ladder classifies and every frame the ladder classifies inside the plot has
//     its tick, the value ticks are exactly the published value labels' own
//     pixels, and every frame label box lies inside the plot
//   3 the ladder follows the scaler: a label's number is scale.frameAt(tick),
//     and neighbouring label boxes keep a minimum gap
//   4 curves: exactly one per shown (target, channel) - the same set and the
//     same order as the editor's own curves() - each with >1 point, key rects
//     clipped into the plot and equal to graph::keyRect, and the polyline's y
//     at every key frame equal to scale.pixelY(valueOf(key))
//   5 hidden channels: the row survives with visible == false and no curve
//   6 empty state: no targets -> a message and no curves
//   7 playhead: scale.pixelX(editor.currentFrame()) after moving the document's
//     frame, and -1 with no project
//   8 legend overflow: a row with no room inside the plot publishes NO swatch (an
//     empty rect) while keeping its identity, colour and label, and every swatch
//     that IS published is a key square inside the plot
//   9 the polyline is published unclipped and unreduced - point for point the
//     sampled curve mapped through the scaler, out-of-plot samples included, so
//     the drawing layer's own clip is the only clip (the header's CURVES)
//  10 tangent handles: one PanelHandle pair per Custom segment, in segment order,
//     with base/dot/radius exactly the shared geometry the editor's hit-test
//     grabs through (ctrl::graph::handlePoint) - and none on a curve with no
//     Custom segment
//  11 the reserved ruler band and the live marquee
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
// plus a read-only probe: building a model must not move the editor's scaler,
// its targets, the project's dirty flag or the document's keys.
//
// Headless: QApplication (translations + the font database the label metrics
// need), no widgets, no GL, no .anie fixture.
#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QPointF>
#include <QRectF>
#include <QString>
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
#include "core/Frame.h"
#include "core/MoveKey.h"
#include "core/ObjectNode.h"
#include "core/OpaKey.h"
#include "core/Project.h"
#include "core/RotateKey.h"
#include "core/TimeKeyType.h"
#include "core/TimeLine.h"
#include "ctrl/GraphEditor.h"
#include "ctrl/UILogger.h"
#include "ctrl/graph/graph_Channel.h"
#include "ctrl/graph/graph_Curve.h"
#include "ctrl/graph/graph_Geometry.h"
#include "ctrl/graph/graph_Scaler.h"
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

#include "gui/ObjectTreeWidget.h"
bool gui::obj::Item::isTopNode() const { return false; }
std::pair<core::TimeKey*, core::LayerMesh*> gui::ObjectTreeWidget::getAreaMeshImpl(core::ObjectNode&, int) {
    return { nullptr, nullptr };
}

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
// virtual and null by default), with a name so the legend labels are distinct.
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
    }
    for (const gui::PanelLegendRow& row : aModel.legend)
        rects.append(row.swatch);
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

    NullLogger logger;
    ctrl::GraphEditor editor(project, logger);
    // The show the whole file rides on, with its POST-CONDITION asserted: every
    // model built below assumes the panel is up (and the fit that fills the value
    // window is one of the things setVisible(true) does), so a setVisible() that
    // silently did not take would leave this file measuring a hidden editor while
    // the application routes nothing to it at all. Asserted here, at the one call
    // site that shows it.
    editor.setVisible(true);
    CHECK(editor.isVisible());
    editor.setTargets({ &nodeA, &nodeB });

    const QVector<core::ObjectNode*> targets = { &nodeA, &nodeB };
    const QVector<Pair> pairs = expectedPairs(targets);

    const QSize viewport(1280, 720);
    const theme::Graph palette = theme::Graph::from(theme::Colors::current());

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
        CHECK(model.valueColumn.width() == scale.axisLabelWidth());
        CHECK(ctrl::graph::Scaler::kAxisLabelWidth <= model.valueColumn.width());
        CHECK(model.valueColumn.width() <= ctrl::graph::Scaler::kMaxAxisLabelWidth);
        CHECK(model.plot.height() == model.panel.height() - 2 * ctrl::graph::Scaler::kMargin);

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

        int outside = 0;
        for (const gui::PanelLabel& label : model.frameLabels) {
            if (label.rect.left() < model.plot.left() || model.plot.right() < label.rect.right() ||
                label.rect.top() < model.plot.top() || model.plot.bottom() < label.rect.bottom())
                ++outside;
        }
        std::printf("[2] frameLabels=%d  with an x outside the plot=%d\n", isz(model.frameLabels), outside);
        CHECK(!model.frameLabels.isEmpty());
        CHECK(outside == 0);

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
        int sampled = 0;
        int wrongNumber = 0;
        int noTick = 0;
        int minGap = std::numeric_limits<int>::max();
        int previousRight = std::numeric_limits<int>::min();
        bool anyPrevious = false;

        for (const gui::PanelLabel& label : model.frameLabels) {
            bool ok = false;
            const int number = label.text.toInt(&ok);

            // The number is the frame of a tick inside its own box (the box is
            // centred on the tick and nudged inside the plot, so clamping can
            // move the centre but never off the tick).
            bool found = false;
            int matchedX = -1;
            for (int x : model.frameTickX) {
                if (label.rect.left() <= x && x <= label.rect.right() && scale.frameAt(x).get() == number) {
                    found = true;
                    matchedX = x;
                }
            }
            if (!found)
                ++noTick;
            if (!ok)
                ++wrongNumber;
            ++sampled;

            if (anyPrevious) {
                const int gap = label.rect.left() - previousRight - 1;
                minGap = std::min(minGap, gap);
            }
            previousRight = label.rect.right();
            anyPrevious = true;

            if (sampled <= 4) {
                std::printf("[3] label \"%s\" <- tick x=%d, scale.frameAt(x)=%d, box x=%d..%d y=%d..%d\n",
                            qUtf8Printable(label.text), matchedX,
                            matchedX >= 0 ? scale.frameAt(matchedX).get() : -1, label.rect.left(), label.rect.right(),
                            label.rect.top(), label.rect.bottom());
            }
        }

        std::printf("[3] sampled %d frame labels, %d with no tick carrying their number, minimum gap between "
                    "neighbouring boxes=%d px\n",
                    sampled, noTick, sampled > 1 ? minGap : -1);
        CHECK(sampled > 1);
        CHECK(noTick == 0);
        CHECK(wrongNumber == 0);
        CHECK(minGap >= 8);

        // One tick, one number: the label rects are never the same box twice.
        int duplicates = 0;
        for (int i = 1; i < model.frameLabels.size(); ++i) {
            if (model.frameLabels[i].rect.intersects(model.frameLabels[i - 1].rect))
                ++duplicates;
        }
        std::printf("[3] overlapping neighbouring label boxes=%d\n", duplicates);
        CHECK(duplicates == 0);

        // The ticks ARE the ladder's own ticks, not a tick per pixel: every
        // published x maps to a frame the ladder classifies (TickLevel != None)
        // and back to that same x, every classified frame inside the plot has its
        // tick, and there are no others. Deleting the `level == None` filter in
        // buildFrameAxis turns the axis into the solid comb the design removed -
        // one tick per pixel at the 1px/frame floor - while every assertion above
        // still passes; these are the ones that fail then.
        int unclassified = 0;
        int notRoundTrip = 0;
        int notAscending = 0;
        for (int i = 0; i < model.frameTickX.size(); ++i) {
            const int x = model.frameTickX.at(i);
            const core::Frame frame = scale.frameAt(x);
            if (scale.frameScale().attribute(frame.get()).level == ctrl::time::Scaler::TickLevel::None)
                ++unclassified;
            if (scale.pixelX(frame) != x)
                ++notRoundTrip;
            if (0 < i && x <= model.frameTickX.at(i - 1))
                ++notAscending;
        }

        const core::Frame windowFirst = scale.frameAt(model.plot.left());
        const core::Frame windowLast = scale.frameAt(model.plot.right());
        QVector<int> ladderTicks;
        int classified = 0;
        for (int frame = windowFirst.get(); frame <= windowLast.get(); ++frame) {
            if (scale.frameScale().attribute(frame).level == ctrl::time::Scaler::TickLevel::None)
                continue;
            ++classified;
            const int x = scale.pixelX(core::Frame(frame));
            if (x < model.plot.left() || model.plot.right() < x)
                continue;
            if (ladderTicks.isEmpty() || ladderTicks.last() != x)
                ladderTicks.append(x);
        }

        std::printf("[3] frame ticks: published=%d, the ladder's own inside the plot=%d (%d classified frames in "
                    "the window)  unclassified ticks=%d  pixel round-trip mismatches=%d  out of order=%d\n",
                    isz(model.frameTickX), isz(ladderTicks), classified, unclassified, notRoundTrip, notAscending);
        CHECK(!model.frameTickX.isEmpty());
        CHECK(unclassified == 0);
        CHECK(notRoundTrip == 0);
        CHECK(notAscending == 0);
        CHECK(model.frameTickX == ladderTicks);

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
        std::printf("[4] curves=%d  editor.curves()=%d  legend rows=%d\n", isz(model.curves), isz(editorCurves),
                    isz(model.legend));

        // Exactly one curve per expected pair, in the editor's own order.
        CHECK(model.curves.size() == pairs.size());
        CHECK(editorCurves.size() == pairs.size());
        for (int i = 0; i < pairs.size() && i < model.curves.size(); ++i) {
            CHECK(model.curves[i].node == pairs[i].first);
            CHECK(model.curves[i].channel == pairs[i].second);
            CHECK(0 <= model.curves[i].legendIndex && model.curves[i].legendIndex < model.legend.size());
            CHECK(model.legend[model.curves[i].legendIndex].channel == pairs[i].second);
            CHECK(model.curves[i].color.isValid());
            CHECK(model.curves[i].color == model.legend[model.curves[i].legendIndex].color);
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

        // Every legend row is a channel with keys, and every swatch the model
        // publishes is a key square INSIDE the plot. A row with no room for one
        // publishes no rect at all - an empty swatch, the model's own signal that
        // the row carries no geometry (the header's LEGEND; the overflow fixtures
        // are block [8]).
        int badSwatches = 0;
        int rowsWithoutSwatch = 0;
        for (const gui::PanelLegendRow& row : model.legend) {
            const bool wellFormed =
                row.node != nullptr && row.node->timeLine() != nullptr && !row.label.isEmpty() && row.color.isValid();
            if (!wellFormed) {
                ++badSwatches;
                continue;
            }
            if (row.swatch.isEmpty()) {
                ++rowsWithoutSwatch;
                continue;
            }
            if (row.swatch.width() != 2 * ctrl::graph::kKeyRadius ||
                row.swatch.height() != 2 * ctrl::graph::kKeyRadius || !model.plot.contains(row.swatch))
                ++badSwatches;
        }
        std::printf("[4] legend rows=%d, malformed rows=%d, rows without a swatch=%d, first swatch (%d,%d,%dx%d) "
                    "label=\"%s\"\n",
                    isz(model.legend), badSwatches, rowsWithoutSwatch, model.legend.first().swatch.x(),
                    model.legend.first().swatch.y(), model.legend.first().swatch.width(),
                    model.legend.first().swatch.height(), qUtf8Printable(model.legend.first().label));
        CHECK(model.legend.size() == pairs.size());
        CHECK(badSwatches == 0);
        // At this viewport the legend fits the plot, so no row is left without a
        // swatch: an empty one here would mean the layout gave up on a row it had
        // room for.
        CHECK(rowsWithoutSwatch == 0);

        int rowOverlaps = 0;
        for (int i = 1; i < model.legend.size(); ++i) {
            if (model.legend[i].swatch.intersects(model.legend[i - 1].swatch))
                ++rowOverlaps;
        }
        std::printf("[4] overlapping legend swatches=%d  legendIndex set on curves=%d\n", rowOverlaps,
                    [&model] {
                        int n = 0;
                        for (const gui::PanelCurve& c : model.curves)
                            n += (c.legendIndex >= 0);
                        return n;
                    }());
        CHECK(rowOverlaps == 0);
    }

    // -------------------------------------------------------------------------
    // 8. legend overflow: a row with no room inside the plot publishes no swatch
    {
        // The stack is anchored at the plot's bottom corner and grows upward, so a
        // legend taller than the plot (or a plot narrower than the inset + a swatch)
        // cannot lay every row out. The model keeps every row - identity, colour and
        // label - and publishes no swatch for the rows it cannot place: an empty
        // rect, the model's signal that the row has no geometry to draw or to hit
        // (the header's LEGEND). What it must never do is publish a swatch outside
        // the plot, which is what the unbounded stack did.
        const QSize cramped(160, 48);
        const gui::GraphPanelModel crampedModel = gui::buildGraphPanelModel(editor, cramped, palette);

        int emptyRows = 0;
        int publishedSwatches = 0;
        int outsideSwatches = 0;
        int malformedSwatches = 0;
        int emptyAfterPublished = 0;
        int identityLost = 0;
        bool seenPublished = false;
        for (int i = 0; i < crampedModel.legend.size(); ++i) {
            const gui::PanelLegendRow& row = crampedModel.legend.at(i);
            const gui::PanelLegendRow& full = model.legend.at(i);
            if (row.node != full.node || row.channel != full.channel || row.label != full.label ||
                row.color != full.color || row.visible != full.visible)
                ++identityLost; // a row the layout could not place is still the same row
            if (row.swatch.isEmpty()) {
                ++emptyRows;
                // The stack is bottom-anchored, so the rows without a swatch are the
                // FIRST rows of the legend: an empty row BELOW a published one is a
                // hole in the middle of the stack, which is not a layout at all.
                if (seenPublished)
                    ++emptyAfterPublished;
                continue;
            }
            seenPublished = true;
            ++publishedSwatches;
            if (row.swatch.width() != 2 * ctrl::graph::kKeyRadius ||
                row.swatch.height() != 2 * ctrl::graph::kKeyRadius)
                ++malformedSwatches;
            if (!crampedModel.plot.contains(row.swatch))
                ++outsideSwatches;
        }

        std::printf("[8] cramped viewport %dx%d -> plot (%d,%d,%dx%d)  legend rows=%d  rows with no swatch=%d  "
                    "published swatches=%d  empty rows below a published one=%d  swatches outside the plot=%d  "
                    "malformed=%d  identity mismatches=%d\n",
                    cramped.width(), cramped.height(), crampedModel.plot.x(), crampedModel.plot.y(),
                    crampedModel.plot.width(), crampedModel.plot.height(), isz(crampedModel.legend), emptyRows,
                    publishedSwatches, emptyAfterPublished, outsideSwatches, malformedSwatches, identityLost);

        CHECK(!crampedModel.plot.isEmpty());
        CHECK(crampedModel.legend.size() == model.legend.size());
        CHECK(emptyRows > 0);           // the fixture's premise: this stack does not fit
        CHECK(publishedSwatches > 0);   // ... while the rows that DO fit keep their swatch
        CHECK(emptyAfterPublished == 0); // the rows left without one are always the FIRST
        CHECK(outsideSwatches == 0);
        CHECK(malformedSwatches == 0);
        CHECK(identityLost == 0);

        // A plot narrower than the legend's own left inset (6px) plus a swatch has
        // no room for a swatch at all: every row is left empty rather than published
        // in the margin, and the legend still lists every row. The viewport has to
        // leave a NON-EMPTY plot first: the label column's reserve is now the value
        // format's own width (Scaler::axisLabelWidth(), 68px at the UI font rather
        // than the old 46), so the plot starts 22px further right and this fixture
        // grew by exactly that much (64 -> 80) to keep the same premise - a plot
        // that exists and is narrower than 16px.
        const QSize tooNarrow(80, 200);
        const gui::GraphPanelModel narrowModel = gui::buildGraphPanelModel(editor, tooNarrow, palette);
        int narrowPublished = 0;
        for (const gui::PanelLegendRow& row : narrowModel.legend) {
            if (!row.swatch.isEmpty())
                ++narrowPublished;
        }
        std::printf("[8] narrow viewport %dx%d -> plot width=%d  legend rows=%d  published swatches=%d\n",
                    tooNarrow.width(), tooNarrow.height(), narrowModel.plot.width(), isz(narrowModel.legend),
                    narrowPublished);
        CHECK(narrowModel.plot.width() < 6 + 10); // no room for one: see the header's LEGEND insets
        CHECK(narrowModel.legend.size() == model.legend.size());
        CHECK(narrowPublished == 0);
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
                const QPointF expected(
                    zoomed.pixelXOfFrame(polyline.at(i).x()), double(zoomed.pixelY(float(polyline.at(i).y())))
                );
                const double dx = std::fabs(expected.x() - curve.points.at(i).x());
                const double dy = std::fabs(expected.y() - curve.points.at(i).y());
                worstDelta = std::max(worstDelta, std::max(dx, dy));
                if (dx > 0.0 || dy > 0.0)
                    same = false; // a cut or dropped sample would show up right here
            }
            if (!same)
                ++mismatchedCurves;

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
    // 5. hidden channels
    {
        const ctrl::graph::Channel hidden(core::TimeKeyType_Move, 1); // Move Y of target A
        editor.setChannelVisible(nodeA, hidden, false);
        const gui::GraphPanelModel hiddenModel = gui::buildGraphPanelModel(editor, viewport, palette);

        int rowIndex = -1;
        for (int i = 0; i < hiddenModel.legend.size(); ++i) {
            if (samePair(Pair(hiddenModel.legend[i].node, hiddenModel.legend[i].channel), &nodeA, hidden))
                rowIndex = i;
        }
        int curvesForHidden = 0;
        for (const gui::PanelCurve& curve : hiddenModel.curves) {
            if (curve.node == &nodeA && curve.channel == hidden)
                ++curvesForHidden;
        }

        std::printf("[5] after hiding Move/Y of A: legend rows=%d (row found=%d, visible=%d) curves=%d (was %d) "
                    "curves for the hidden pair=%d\n",
                    isz(hiddenModel.legend), rowIndex, rowIndex >= 0 ? int(hiddenModel.legend[rowIndex].visible) : -1,
                    isz(hiddenModel.curves), isz(model.curves), curvesForHidden);

        CHECK(rowIndex >= 0);
        CHECK(hiddenModel.legend[rowIndex].visible == false);
        CHECK(curvesForHidden == 0);
        CHECK(hiddenModel.curves.size() == model.curves.size() - 1);
        CHECK(hiddenModel.legend.size() == model.legend.size());
        CHECK(hiddenModel.emptyState.isEmpty());

        editor.setChannelVisible(nodeA, hidden, true);
        const gui::GraphPanelModel restored = gui::buildGraphPanelModel(editor, viewport, palette);
        std::printf("[5] restored: curves=%d legend=%d\n", isz(restored.curves), isz(restored.legend));
        CHECK(restored.curves.size() == model.curves.size());
        CHECK(restored.legend.size() == model.legend.size());
    }

    // -------------------------------------------------------------------------
    // 6. empty state
    {
        editor.setTargets({});
        const gui::GraphPanelModel emptyModel = gui::buildGraphPanelModel(editor, viewport, palette);
        std::printf("[6] no targets: emptyState=\"%s\" curves=%d legend=%d isEmpty=%d\n",
                    qUtf8Printable(emptyModel.emptyState),
                    isz(emptyModel.curves), isz(emptyModel.legend), int(emptyModel.isEmpty()));

        CHECK(!emptyModel.emptyState.isEmpty());
        CHECK(emptyModel.curves.isEmpty());
        CHECK(emptyModel.legend.isEmpty());
        CHECK(emptyModel.isEmpty());

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

        // No project: no frame to place.
        editor.setProject(nullptr);
        const gui::GraphPanelModel orphaned = gui::buildGraphPanelModel(editor, viewport, palette);
        std::printf("[7] no project: editor.currentFrame()=%d  playheadX=%d  emptyState=\"%s\"\n",
                    editor.currentFrame().get(), orphaned.playheadX, qUtf8Printable(orphaned.emptyState));
        CHECK(editor.currentFrame().get() == -1);
        CHECK(orphaned.playheadX == -1);
        CHECK(!orphaned.emptyState.isEmpty());
        CHECK(orphaned.curves.isEmpty());
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
        move0->data().easing() = custom;

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

        // A curve with no Custom segment publishes NO handle, so the pairs are
        // keyed to the easing and not to "a curve".
        int nonCustomCurves = 0;
        int handlesOnNonCustom = 0;
        for (const gui::PanelCurve& curve : handled.curves) {
            const ctrl::graph::Curve source(*curve.node->timeLine(), curve.channel);
            bool anyCustom = false;
            for (int s = 0; s < source.segmentCount(); ++s) {
                if (source.isCustom(source.segment(s)))
                    anyCustom = true;
            }
            if (!anyCustom) {
                ++nonCustomCurves;
                handlesOnNonCustom += curve.handles.size();
            }
        }
        std::printf("[10] curves without a Custom segment=%d, handles they published=%d\n", nonCustomCurves,
                    handlesOnNonCustom);
        CHECK(nonCustomCurves > 0);
        CHECK(handlesOnNonCustom == 0);
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

        // The tick length the panel draws (kAxisTickLength in
        // src/gui/ImGuiOverlay.cpp, the drawing layer's half of the same convention
        // the model documents as kFrameTickLength).
        constexpr int kDrawnTickLength = 4;
        int labels = 0;
        int insidePlot = 0;
        int clearOfTicks = 0;
        for (const gui::PanelLabel& label : ruled.frameLabels) {
            ++labels;
            if (ruled.plot.contains(label.rect))
                ++insidePlot;
            if (label.rect.top() >= ruled.plot.top() + kDrawnTickLength)
                ++clearOfTicks;
        }
        std::printf(
            "[11] plot=(%d,%d %dx%d) frame_labels=%d inside_plot=%d clear_of_%dpx_tick_band=%d\n", ruled.plot.left(),
            ruled.plot.top(), ruled.plot.width(), ruled.plot.height(), labels, insidePlot, kDrawnTickLength,
            clearOfTicks
        );
        // The precondition of the pixel gate's assertion 16 (which measures the same
        // rule on the drawn glyphs): the model has to carry labels at all, or the
        // reservation is unobservable.
        CHECK(labels > 0);
        CHECK(insidePlot == labels);
        CHECK(clearOfTicks == labels);

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
        // The project tab bar covers the panel's first rows, so the model is asked for
        // the occluded height and must move the RULER BAND - and nothing else. The two
        // halves are what this checks: the band (and the labels below it) moves down,
        // and the interactive geometry (the plot, and with it every curve, key, handle
        // and the playhead) does NOT, which is what keeps the drawn key the same key a
        // press grabs.
        const int kInset = 24;
        const gui::GraphPanelModel occluded = gui::buildGraphPanelModel(editor, viewport, palette, kInset);
        int bandLabels = 0;
        int bandClearOfTicks = 0;
        int valueLabelsBelowBand = 0;
        for (const gui::PanelLabel& label : occluded.frameLabels) {
            ++bandLabels;
            if (label.rect.top() >= occluded.rulerBandTop + 4) {
                ++bandClearOfTicks;
            }
        }
        for (const gui::PanelLabel& label : occluded.valueLabels) {
            if (label.rect.top() >= occluded.rulerBandTop) {
                ++valueLabelsBelowBand;
            }
        }
        std::printf(
            "[11] occluded_top inset=%d rulerBandTop=%d (plot.top()=%d) plot_moved=%d frame_labels=%d"
            " clear_of_tick_band=%d value_labels=%d below_band=%d ruler_vtx_unaffected=%d\n",
            kInset, occluded.rulerBandTop, ruled.plot.top(), int(occluded.plot != ruled.plot), bandLabels,
            bandClearOfTicks, isz(occluded.valueLabels), valueLabelsBelowBand,
            int(occluded.curves.size() == ruled.curves.size())
        );
        CHECK(occluded.rulerBandTop == ruled.plot.top() + kInset);
        CHECK(occluded.plot == ruled.plot);          // the interactive geometry stays put
        CHECK(occluded.panel == ruled.panel);
        CHECK(occluded.curves.size() == ruled.curves.size());
        CHECK(occluded.playheadX == ruled.playheadX);
        CHECK(bandLabels > 0);
        CHECK(bandClearOfTicks == bandLabels);
        CHECK(valueLabelsBelowBand == isz(occluded.valueLabels));

        // An inset that swallows the plot leaves no band to draw in, and then there is
        // nothing to label - the model says so by publishing no frame label rather than
        // by placing one under the widget.
        const gui::GraphPanelModel swallowed =
            gui::buildGraphPanelModel(editor, viewport, palette, ruled.plot.height() + 40);
        std::printf(
            "[11] occluded_all inset=%d rulerBandTop=%d plot.bottom()+1=%d frame_labels=%d value_labels=%d\n",
            ruled.plot.height() + 40, swallowed.rulerBandTop, ruled.plot.bottom() + 1,
            isz(swallowed.frameLabels), isz(swallowed.valueLabels)
        );
        CHECK(swallowed.rulerBandTop == ruled.plot.bottom() + 1);
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
        CHECK(probe.curves.size() == probeAgain.curves.size());
    }

    std::printf("%s: %d failed assertion(s)\n", gFailures == 0 ? "PASS" : "FAIL", gFailures);
    return gFailures == 0 ? 0 : 1;
}
