// Contract test for the animation graph's value model: ctrl::graph::Channel,
// ctrl::graph::Curve and ctrl::graph::Scaler (src/ctrl/graph/*).
//
// The graph overlay is only worth having if the curve it draws IS the number
// playback evaluates, so most of what is pinned below is an oracle comparison:
// the sampler is compared against the real core::TimeKeyBlender, driven over a
// real core::TimeLine and read out of the same core::TimeKeyExpans the renderer
// itself reads. A re-statement of the blend formula here would agree with any
// re-statement in the sampler, which is exactly the bug this test exists to
// catch -- so the sampler is never checked against arithmetic the test invented.
//
// Sections (each asserts the preconditions it needs, so a run that cannot
// observe its property reports that instead of passing quietly):
//   1 the channel table: component counts, which types are graphable at all,
//     which channel is step-held, and that value() reads the component the
//     blender blends for that channel
//   2 the sampler equals playback frame by frame over a span, plus the no-key,
//     default-key and one-key cases playback handles differently
//   3 the easing identities the graph is built on: a Linear midpoint is the
//     exact average, Floor holds the lower key, a Custom bezier passes through
//     a point stated in closed form
//   4 the Move channel goes through the same Catmull-Rom spline playback uses:
//     the neighbour key bends the path off the chord, and a Linear-spline
//     neighbour collapses it back onto the chord
//   5 the image offset is step-held (the staircase itself needs a GL context:
//     see the skip inside the section)
//   6 a Custom easing's handles are (x1,y1)/(x2,y2), x is clamped into [0,1]
//     while y stays free to overshoot, and the write lands on the document's
//     own param
//   7 buildPolyline emits the window's ends, samples on frames exactly as
//     sample() does, and comes out as steps when the channel holds
//   8 the Scaler's two axes: the frame round trip, the zoom's anchor rule, and
//     the value window including a degenerate (constant) span
//
// Headless: QApplication for the timeline's QObject/translation internals, no
// widgets, no GL, no .anie fixture. The one property that cannot be observed
// here is the image staircase, because a core::ImageKey binds GL when it is
// constructed; section 5 names that and asserts the GL-free half.
#include <QApplication>
#include <QPointF>
#include <QString>
#include <QVector2D>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "XCAssert.h"
#include "cmnd/Stack.h"
#include "core/BlurKey.h"
#include "core/DepthKey.h"
#include "core/Frame.h"
#include "core/HsvKey.h"
#include "core/MoveKey.h"
#include "core/ObjectNode.h"
#include "core/OpaKey.h"
#include "core/RotateKey.h"
#include "core/ScaleKey.h"
#include "core/TimeInfo.h"
#include "core/TimeKeyBlender.h"
#include "core/TimeKeyExpans.h"
#include "core/TimeKeyType.h"
#include "core/TimeLine.h"
#include "ctrl/graph/graph_Channel.h"
#include "ctrl/graph/graph_Curve.h"
#include "ctrl/graph/graph_Scaler.h"
#include "util/Easing.h"

// core::Project.cpp (pulled in by the core library) references the GUI
// AudioPlaybackWidget::deserialize only to load a .aemus sidecar, which this
// test never has; ctrl's timeline editor references two GUI symbols that only
// matter on the paths this test does not drive. Stub them (the same three stubs
// creation_select_keys / bonekey_regression use) so the link succeeds without
// pulling the whole gui library in.
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

#define CHECK(cond)                                                                                    \
    do {                                                                                               \
        if (!(cond)) {                                                                                 \
            std::printf("FAIL %d: %s\n", __LINE__, #cond);                                             \
            return 1;                                                                                  \
        }                                                                                              \
    } while (false)

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

namespace core {
// Minimal ObjectNode subclass owning a real TimeLine (ObjectNode::timeLine()
// is virtual and null by default). No meshes, no resources -- just key maps,
// so the sampler and the blender see a document and nothing else.
class TestNode: public ObjectNode {
    TimeLine mTimeLine;

public:
    ObjectType type() const override { return ObjectType_Layer; }
    void setName(const QString&) override {}
    const QString& name() const override {
        static QString s;
        return s;
    }
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

// The sampler and the blender run the same float arithmetic in the same order
// (the sampler mirrors the blender's own expression), so a few ULPs are all the
// room a correct implementation needs. The bound is tight enough that a missing
// int truncation on an HSV component (which costs up to a whole step, 0.1 or
// more on the values used below) cannot hide inside it.
constexpr float kSampleEps = 1e-4f;

bool nearly(float a, float b, float aEps = kSampleEps) { return std::fabs(a - b) <= aEps; }

// The real blender's evaluation of a node at one frame. Running it once per
// frame and reading every channel out of the same cache is what playback does:
// core::TimeCacheAccessor hands the renderer exactly this TimeKeyExpans after
// core::TimeKeyBlender::updateCurrents filled it. Nothing here is a
// re-implementation of the blend formula -- the values come from the production
// code path, which is the whole point of the oracle.
class BlenderOracle {
public:
    BlenderOracle(core::TestNode& aNode, core::Frame aFrame): mNode(aNode) {
        core::TimeInfo time;
        time.fps = 60;
        time.frame = aFrame;

        core::TimeKeyBlender blender(aNode, /*aUseWorking=*/true);
        // clearCaches is a MEMBER (it walks the node's ancestors' caches), not a
        // static, so it runs on the same blender that then fills them. The test
        // edits keys directly, so this is the app's own way of dropping a spline
        // cache a previous evaluation filled (Driver::updateKey does the same).
        blender.clearCaches(&aNode);
        blender.updateCurrents(&aNode, time);
    }

    // The value playback holds for one channel at this frame, read through the
    // same accessors the renderer uses for that channel.
    float value(const ctrl::graph::Channel& aChannel) const {
        const core::TimeKeyExpans& expans = mNode.timeLine()->working();
        const int comp = aChannel.component();
        switch (aChannel.type()) {
        case core::TimeKeyType_Move: return (comp == 0) ? expans.srt().pos().x() : expans.srt().pos().y();
        case core::TimeKeyType_Rotate: return expans.srt().rotate();
        case core::TimeKeyType_Scale: return (comp == 0) ? expans.srt().scale().x() : expans.srt().scale().y();
        case core::TimeKeyType_Depth: return expans.depth();
        case core::TimeKeyType_Opa: return expans.opa().opacity();
        case core::TimeKeyType_Blur:
            switch (comp) {
            case 0: return expans.blurX();
            case 1: return expans.blurY();
            default: return expans.angleDeg();
            }
        case core::TimeKeyType_HSV:
            // Playback blends the hue/saturation/value back through the int
            // setters, so the cache holds ints by the time it is read.
            return static_cast<float>(expans.hsv().hsv().at(comp));
        case core::TimeKeyType_Image: return (comp == 0) ? expans.imageOffset().x() : expans.imageOffset().y();
        default: return 0.0f;
        }
    }

private:
    core::TestNode& mNode;
};

// True when the sampler holds what the blender put in the cache for the same
// frame. A mismatch prints both numbers (the FAIL line that follows names the
// call site), so a divergence says which frame and which channel it is.
bool matchesPlayback(const BlenderOracle& aOracle, const ctrl::graph::Curve& aCurve, core::Frame aFrame) {
    const float expected = aOracle.value(aCurve.channel());
    const float sampled = aCurve.sample(aFrame);
    if (nearly(expected, sampled))
        return true;

    std::printf(
        "  mismatch at frame %d channel %s: blender=%g sampler=%g\n",
        aFrame.get(),
        qPrintable(aCurve.channel().name()),
        expected,
        sampled
    );
    return false;
}

// Push one key onto the node's timeline through the production push path
// (TimeLine::createPusher), which is where a real edit installs keys.
template<class tKey>
tKey* pushKey(core::TestNode& aNode, cmnd::Stack& aStack, core::TimeKeyType aType, int aFrame) {
    auto* key = new tKey();
    aStack.push(aNode.timeLine()->createPusher(aType, aFrame, key));
    return key;
}

// Every graphable channel of a timeline, in the key types' drawing order.
QVector<ctrl::graph::Curve> allCurves(const core::TimeLine& aLine) {
    QVector<ctrl::graph::Curve> curves;
    for (core::TimeKeyType type : ctrl::graph::Channel::graphableTypes()) {
        for (const ctrl::graph::Channel& channel : ctrl::graph::Channel::channelsOf(type)) {
            curves.append(ctrl::graph::Curve(aLine, channel));
        }
    }
    return curves;
}

// ---------------------------------------------------------------------------
// 1. The channel table.
//
// Which components exist, which types are graphable at all, which channel is
// step-held, and which value each channel reads -- all decided by Channel, and
// everything else in the graph (scaler, sampler, renderer, hit test) asks it.
// The keys below hold DISTINCT values per component, so reading the wrong
// component cannot pass by symmetry.
// ---------------------------------------------------------------------------
int sectionChannelTable() {
    using core::TimeKeyType;
    using ctrl::graph::Channel;

    // The count is also the component numbering value() reads.
    CHECK(Channel::componentCount(core::TimeKeyType_Move) == 2);
    CHECK(Channel::componentCount(core::TimeKeyType_Rotate) == 1);
    CHECK(Channel::componentCount(core::TimeKeyType_Scale) == 2);
    CHECK(Channel::componentCount(core::TimeKeyType_Depth) == 1);
    CHECK(Channel::componentCount(core::TimeKeyType_Opa) == 1);
    CHECK(Channel::componentCount(core::TimeKeyType_Blur) == 3);
    CHECK(Channel::componentCount(core::TimeKeyType_HSV) == 3);
    CHECK(Channel::componentCount(core::TimeKeyType_Image) == 2);
    // Bone, pose, mesh and FFD keys are structural: they carry no
    // util::Easing::Param, so they are not graphable (and TERM is not a type).
    CHECK(Channel::componentCount(core::TimeKeyType_Bone) == 0);
    CHECK(Channel::componentCount(core::TimeKeyType_Pose) == 0);
    CHECK(Channel::componentCount(core::TimeKeyType_Mesh) == 0);
    CHECK(Channel::componentCount(core::TimeKeyType_FFD) == 0);
    CHECK(Channel::componentCount(core::TimeKeyType_TERM) == 0);

    // channelsOf() enumerates exactly the count, in component order.
    const QVector<Channel> blurChannels = Channel::channelsOf(core::TimeKeyType_Blur);
    CHECK(blurChannels.size() == 3);
    CHECK(blurChannels.first() == Channel(core::TimeKeyType_Blur, 0));
    CHECK(blurChannels.at(2) == Channel(core::TimeKeyType_Blur, 2));
    CHECK(Channel::channelsOf(core::TimeKeyType_Move).size() == 2);
    CHECK(Channel::channelsOf(core::TimeKeyType_Bone).isEmpty());
    CHECK(Channel(core::TimeKeyType_Move, 1) != Channel(core::TimeKeyType_Move, 0));

    // graphableTypes() is the timeline's own key order with the non-graphable
    // types dropped: the image key leads (it renders first) and the easing types
    // follow in timeline order.
    const QVector<TimeKeyType> types = Channel::graphableTypes();
    CHECK(types.size() == 8);
    CHECK(types.at(0) == core::TimeKeyType_Image);
    CHECK(types.at(1) == core::TimeKeyType_Move);
    CHECK(types.at(2) == core::TimeKeyType_Rotate);
    CHECK(types.at(3) == core::TimeKeyType_Scale);
    CHECK(types.at(4) == core::TimeKeyType_Depth);
    CHECK(types.at(5) == core::TimeKeyType_Opa);
    CHECK(types.at(6) == core::TimeKeyType_HSV);
    CHECK(types.at(7) == core::TimeKeyType_Blur);

    // Exactly one graphable channel is step-held: the image offset, which
    // playback holds rather than blends (see section 5).
    for (int i = 0; i < core::TimeKeyType_TERM; ++i) {
        const TimeKeyType type = static_cast<TimeKeyType>(i);
        if (Channel::componentCount(type) == 0)
            continue;
        CHECK(Channel(type, 0).isInterpolated() == (type != core::TimeKeyType_Image));
    }
    CHECK(!Channel(core::TimeKeyType_Image, 1).isInterpolated());

    // value() reads the component the blender blends for that channel.
    core::MoveKey move;
    move.data().setPos(QVector2D(3.25f, -7.5f));
    CHECK(Channel(core::TimeKeyType_Move, 0).value(move) == 3.25f);
    CHECK(Channel(core::TimeKeyType_Move, 1).value(move) == -7.5f);

    core::ScaleKey scale;
    scale.data().setScale(QVector2D(2.5f, 0.75f));
    CHECK(Channel(core::TimeKeyType_Scale, 0).value(scale) == 2.5f);
    CHECK(Channel(core::TimeKeyType_Scale, 1).value(scale) == 0.75f);

    core::RotateKey rotate;
    rotate.data().setRotate(45.0f);
    CHECK(Channel(core::TimeKeyType_Rotate, 0).value(rotate) == 45.0f);

    core::DepthKey depth;
    depth.data().setDepth(12.0f);
    CHECK(Channel(core::TimeKeyType_Depth, 0).value(depth) == 12.0f);

    core::OpaKey opa;
    opa.data().setOpacity(0.375f);
    CHECK(Channel(core::TimeKeyType_Opa, 0).value(opa) == 0.375f);

    core::BlurKey blur;
    blur.data().setBlurX(5.0f);
    blur.data().setBlurY(6.0f);
    blur.data().setAngleDeg(7.0f);
    CHECK(Channel(core::TimeKeyType_Blur, 0).value(blur) == 5.0f);
    CHECK(Channel(core::TimeKeyType_Blur, 1).value(blur) == 6.0f);
    CHECK(Channel(core::TimeKeyType_Blur, 2).value(blur) == 7.0f);

    core::HSVKey hsv;
    hsv.data().setHSV(QList<int>{ 11, 22, 33, 0 });
    CHECK(Channel(core::TimeKeyType_HSV, 0).value(hsv) == 11.0f);
    CHECK(Channel(core::TimeKeyType_HSV, 1).value(hsv) == 22.0f);
    CHECK(Channel(core::TimeKeyType_HSV, 2).value(hsv) == 33.0f);

    // A hand-made channel outside the type's count is harmless (0) rather than
    // a read of a neighbouring component or past the end of a vector.
    CHECK(Channel(core::TimeKeyType_Blur, 5).value(blur) == 0.0f);
    CHECK(Channel(core::TimeKeyType_Move, -1).value(move) == 0.0f);

    // Labels: the component half is what a row caption is built from, and a
    // single-component channel carries no component half.
    CHECK(Channel(core::TimeKeyType_Move, 0).componentName() == QLatin1String("X"));
    CHECK(Channel(core::TimeKeyType_Move, 1).componentName() == QLatin1String("Y"));
    CHECK(Channel(core::TimeKeyType_Blur, 2).componentName() == QLatin1String("Angle"));
    CHECK(Channel(core::TimeKeyType_HSV, 1).componentName() == QLatin1String("Saturation"));
    CHECK(Channel(core::TimeKeyType_Move, 0).name() == QLatin1String("Move X"));
    CHECK(Channel(core::TimeKeyType_Rotate, 0).name() == QLatin1String("Rotate"));
    return 0;
}

// ---------------------------------------------------------------------------
// 2. The sampler equals playback.
//
// Three keys per channel (frames 0, 10, 20) so both segments are sampled and so
// the segment's easing can only be the FROM key's: every channel uses different
// easings on its two keys. All the frames compared are strictly inside a key
// span, which is the range the sampler promises to reproduce.
// ---------------------------------------------------------------------------
void buildSamplerNode(core::TestNode& aNode, cmnd::Stack& aStack) {
    using core::TimeKeyType;

    // Rotate: a Custom bezier into Expo/Out. The bezier's control x values are
    // 1/3 and 2/3, which make its x the normalized progress exactly (x(s)=s),
    // so its y is the rate at any frame without the test solving the curve.
    {
        auto* k0 = pushKey<core::RotateKey>(aNode, aStack, core::TimeKeyType_Rotate, 0);
        auto* k1 = pushKey<core::RotateKey>(aNode, aStack, core::TimeKeyType_Rotate, 10);
        auto* k2 = pushKey<core::RotateKey>(aNode, aStack, core::TimeKeyType_Rotate, 20);
        k0->data().setRotate(0.0f);
        k1->data().setRotate(100.0f);
        k2->data().setRotate(40.0f);
        util::Easing::Param& e0 = k0->data().easing();
        e0.type = util::Easing::Type_Custom;
        e0.cubicBezier.x1 = 1.0f / 3.0f;
        e0.cubicBezier.y1 = 0.0f;
        e0.cubicBezier.x2 = 2.0f / 3.0f;
        e0.cubicBezier.y2 = 0.5f;
        k1->data().easing().type = util::Easing::Type_Expo;
        k1->data().easing().range = util::Easing::Range_Out;
    }

    // Opa: Quad/InOut then Floor, so the second segment is a step-held one.
    // Both segments stay inside [0,1] on purpose: playback clamps the blended
    // opacity through OpaKey::Data::setOpacity, and this section is about the
    // easing and the lerp, not about that clamp.
    {
        auto* k0 = pushKey<core::OpaKey>(aNode, aStack, core::TimeKeyType_Opa, 0);
        auto* k1 = pushKey<core::OpaKey>(aNode, aStack, core::TimeKeyType_Opa, 10);
        auto* k2 = pushKey<core::OpaKey>(aNode, aStack, core::TimeKeyType_Opa, 20);
        k0->data().setOpacity(0.25f);
        k1->data().setOpacity(1.0f);
        k2->data().setOpacity(0.5f);
        k0->data().easing().type = util::Easing::Type_Quad;
        k0->data().easing().range = util::Easing::Range_InOut;
        k1->data().easing().type = util::Easing::Type_Floor;
    }

    // Scale: Back/Out overshoots the segment's value span (the scale setter
    // clamps at +-10000, so the overshoot reaches the cache unclipped), then
    // Cubic/In.
    {
        auto* k0 = pushKey<core::ScaleKey>(aNode, aStack, core::TimeKeyType_Scale, 0);
        auto* k1 = pushKey<core::ScaleKey>(aNode, aStack, core::TimeKeyType_Scale, 10);
        auto* k2 = pushKey<core::ScaleKey>(aNode, aStack, core::TimeKeyType_Scale, 20);
        k0->data().setScale(QVector2D(2.0f, 3.0f));
        k1->data().setScale(QVector2D(6.0f, 0.5f));
        k2->data().setScale(QVector2D(1.0f, 1.0f));
        k0->data().easing().type = util::Easing::Type_Back;
        k0->data().easing().range = util::Easing::Range_Out;
        k1->data().easing().type = util::Easing::Type_Cubic;
        k1->data().easing().range = util::Easing::Range_In;
    }

    // Depth: Linear then Expo/InOut.
    {
        auto* k0 = pushKey<core::DepthKey>(aNode, aStack, core::TimeKeyType_Depth, 0);
        auto* k1 = pushKey<core::DepthKey>(aNode, aStack, core::TimeKeyType_Depth, 10);
        auto* k2 = pushKey<core::DepthKey>(aNode, aStack, core::TimeKeyType_Depth, 20);
        k0->data().setDepth(5.0f);
        k1->data().setDepth(25.0f);
        k2->data().setDepth(10.0f);
        k1->data().easing().type = util::Easing::Type_Expo;
        k1->data().easing().range = util::Easing::Range_InOut;
    }

    // Blur: Elastic/Out (a rate that overshoots and oscillates, so the three
    // components cannot be right by accident) then Bounce/Out.
    {
        auto* k0 = pushKey<core::BlurKey>(aNode, aStack, core::TimeKeyType_Blur, 0);
        auto* k1 = pushKey<core::BlurKey>(aNode, aStack, core::TimeKeyType_Blur, 10);
        auto* k2 = pushKey<core::BlurKey>(aNode, aStack, core::TimeKeyType_Blur, 20);
        k0->data().setBlurX(4.0f);
        k0->data().setBlurY(8.0f);
        k0->data().setAngleDeg(30.0f);
        k1->data().setBlurX(12.0f);
        k1->data().setBlurY(2.0f);
        k1->data().setAngleDeg(150.0f);
        k2->data().setBlurX(6.0f);
        k2->data().setBlurY(6.0f);
        k2->data().setAngleDeg(-20.0f);
        k0->data().easing().type = util::Easing::Type_Elastic;
        k0->data().easing().range = util::Easing::Range_Out;
        k1->data().easing().type = util::Easing::Type_Bounce;
        k1->data().easing().range = util::Easing::Range_Out;
    }

    // HSV: Linear then Round. The values are chosen so the blended channel is
    // NOT an integer at most frames (101*0.1 = 10.1, 202*0.3 = 60.6, ...):
    // playback narrows the blend through the int setters, so a sampler that
    // keeps the fraction differs from playback by up to a whole step.
    {
        auto* k0 = pushKey<core::HSVKey>(aNode, aStack, core::TimeKeyType_HSV, 0);
        auto* k1 = pushKey<core::HSVKey>(aNode, aStack, core::TimeKeyType_HSV, 10);
        auto* k2 = pushKey<core::HSVKey>(aNode, aStack, core::TimeKeyType_HSV, 20);
        k0->data().setHSV(QList<int>{ 0, 0, 0, 0 });
        k1->data().setHSV(QList<int>{ 101, 202, 303, 0 });
        k2->data().setHSV(QList<int>{ 30, 60, 90, 0 });
        k1->data().easing().type = util::Easing::Type_Round;
    }

    // Move: three positions, so the first segment has a neighbour key that
    // bends the Catmull-Rom path (section 4 pins that on its own).
    {
        auto* k0 = pushKey<core::MoveKey>(aNode, aStack, core::TimeKeyType_Move, 0);
        auto* k1 = pushKey<core::MoveKey>(aNode, aStack, core::TimeKeyType_Move, 10);
        auto* k2 = pushKey<core::MoveKey>(aNode, aStack, core::TimeKeyType_Move, 20);
        k0->data().setPos(QVector2D(0.0f, 0.0f));
        k1->data().setPos(QVector2D(100.0f, 40.0f));
        k2->data().setPos(QVector2D(120.0f, -60.0f));
    }
}

int sectionSamplerMatchesPlayback() {
    core::TestNode node;
    cmnd::Stack stack;
    buildSamplerNode(node, stack);

    // The image channel has no keys here (a real one cannot be built headless,
    // see section 5), so its curve only participates through the no-key case.
    const QVector<ctrl::graph::Curve> curves = allCurves(*node.timeLine());
    CHECK(curves.size() == 15); // 2+2+1+2+1+1+3+3 components

    for (int frame = 1; frame <= 19; ++frame) {
        const BlenderOracle oracle(node, core::Frame(frame));
        for (const ctrl::graph::Curve& curve : curves) {
            CHECK(matchesPlayback(oracle, curve, core::Frame(frame)));
        }
    }

    // The easing that shapes a segment is the FROM key's, and the arrival key's
    // must not leak into it: restyling the second key's easing leaves the first
    // segment's samples untouched (playback reads k0->data().easing() for the
    // whole interval, whatever the arrival key carries).
    {
        const ctrl::graph::Curve rotate(*node.timeLine(), ctrl::graph::Channel(core::TimeKeyType_Rotate, 0));
        const float before = rotate.sample(core::Frame(5));
        auto* arrival = static_cast<core::RotateKey*>(node.timeLine()->timeKey(core::TimeKeyType_Rotate, 10));
        CHECK(arrival);
        arrival->data().easing().type = util::Easing::Type_Elastic;
        arrival->data().easing().range = util::Easing::Range_InOut;
        arrival->data().easing().weight = 0.25f;
        CHECK(rotate.sample(core::Frame(5)) == before);
    }
    return 0;
}

// 2b. An unkeyed channel reads what playback holds there. With no default key
// that is the key type's own default Data -- not zero: an unkeyed layer plays at
// scale 1, full opacity, HSV saturation 100. Created layers DO carry default
// keys, so this is the case the graph shows for a freshly added layer.
int sectionSamplerWithoutKeys() {
    using core::TimeKeyType;
    using ctrl::graph::Channel;

    core::TestNode node;
    cmnd::Stack stack;
    const QVector<ctrl::graph::Curve> curves = allCurves(*node.timeLine());
    for (const ctrl::graph::Curve& curve : curves) {
        CHECK(curve.isEmpty());
        CHECK(curve.keys().isEmpty());
        CHECK(curve.segmentCount() == 0);
    }

    {
        const BlenderOracle oracle(node, core::Frame(7));
        for (const ctrl::graph::Curve& curve : curves) {
            CHECK(matchesPlayback(oracle, curve, core::Frame(7)));
        }
    }

    // The concrete defaults, so a sampler that answers 0 for every empty channel
    // is caught even where the oracle's own default happens to be 0.
    for (const ctrl::graph::Curve& curve : curves) {
        const Channel channel = curve.channel();
        const float sampled = curve.sample(core::Frame(7));
        if (channel.type() == core::TimeKeyType_Scale)
            CHECK(sampled == 1.0f);
        if (channel.type() == core::TimeKeyType_Opa)
            CHECK(sampled == 1.0f);
        if (channel.type() == core::TimeKeyType_HSV && channel.component() > 0)
            CHECK(sampled == 100.0f);
        if (channel.type() == core::TimeKeyType_HSV && channel.component() == 0)
            CHECK(sampled == 0.0f);
        if (channel.type() == core::TimeKeyType_Rotate || channel.type() == core::TimeKeyType_Depth ||
            channel.type() == core::TimeKeyType_Blur || channel.type() == core::TimeKeyType_Move ||
            channel.type() == core::TimeKeyType_Image)
            CHECK(sampled == 0.0f);
    }

    // ...and once the timeline HAS a default key, that key is the value: it is
    // what the blender reads when the key map is empty (getDefaultKeyData).
    auto* defaultScale = new core::ScaleKey();
    defaultScale->data().setScale(QVector2D(5.0f, 7.0f));
    node.timeLine()->grabDefaultKey(core::TimeKeyType_Scale, defaultScale);
    CHECK(node.timeLine()->defaultKey(core::TimeKeyType_Scale) == defaultScale);

    auto* defaultOpa = new core::OpaKey();
    defaultOpa->data().setOpacity(0.4f);
    node.timeLine()->grabDefaultKey(core::TimeKeyType_Opa, defaultOpa);

    for (int frame = 0; frame <= 20; frame += 5) {
        const BlenderOracle oracle(node, core::Frame(frame));
        for (const ctrl::graph::Curve& curve : allCurves(*node.timeLine())) {
            CHECK(matchesPlayback(oracle, curve, core::Frame(frame)));
        }
    }

    bool sawScale = false;
    bool sawOpa = false;
    for (const ctrl::graph::Curve& curve : allCurves(*node.timeLine())) {
        const Channel channel = curve.channel();
        if (channel == Channel(core::TimeKeyType_Scale, 0)) {
            sawScale = true;
            CHECK(curve.sample(core::Frame(3)) == 5.0f);
        }
        if (channel == Channel(core::TimeKeyType_Scale, 1))
            CHECK(curve.sample(core::Frame(3)) == 7.0f);
        if (channel == Channel(core::TimeKeyType_Opa, 0)) {
            sawOpa = true;
            CHECK(curve.sample(core::Frame(3)) == 0.4f);
        }
    }
    CHECK(sawScale && sawOpa);
    return 0;
}

// 2c. A one-key channel follows that key everywhere, before and after it: the
// gatherer's "perfect following" case, which is what an animation with a single
// keyed frame looks like.
int sectionSamplerWithOneKey() {
    core::TestNode node;
    cmnd::Stack stack;
    auto* key = pushKey<core::OpaKey>(node, stack, core::TimeKeyType_Opa, 10);
    key->data().setOpacity(0.6f);

    const ctrl::graph::Curve curve(*node.timeLine(), ctrl::graph::Channel(core::TimeKeyType_Opa, 0));
    CHECK(curve.keys().size() == 1);
    CHECK(curve.segmentCount() == 0); // no segment to draw, but still a value
    for (int frame = 0; frame <= 20; frame += 5) {
        const BlenderOracle oracle(node, core::Frame(frame));
        CHECK(matchesPlayback(oracle, curve, core::Frame(frame)));
        CHECK(curve.sample(core::Frame(frame)) == 0.6f);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 3. The easing identities.
//
// Linear, Floor and Custom are the three shapes the graph draws as something
// other than a plain lerp, and each has a value the test can state in closed
// form -- so a sampler that is "close" is not enough.
// ---------------------------------------------------------------------------
int sectionEasingIdentities() {
    core::TestNode node;
    cmnd::Stack stack;
    auto* k0 = pushKey<core::RotateKey>(node, stack, core::TimeKeyType_Rotate, 0);
    auto* k1 = pushKey<core::RotateKey>(node, stack, core::TimeKeyType_Rotate, 10);
    k0->data().setRotate(0.0f);
    k1->data().setRotate(100.0f);

    const ctrl::graph::Curve curve(*node.timeLine(), ctrl::graph::Channel(core::TimeKeyType_Rotate, 0));

    // Linear: the rate at the midpoint is exactly 0.5, so the sample is the
    // exact average of the two keys (0 -> 100 is 50).
    CHECK(k0->data().easing().type == util::Easing::Type_Linear);
    CHECK(curve.sample(core::Frame(5)) == 50.0f);
    CHECK(curve.sample(core::Frame(0)) == 0.0f);
    CHECK(curve.sample(core::Frame(10)) == 100.0f);

    // Floor: the rate is 0 across the whole interval, so every interior frame
    // holds the lower key's value; the next key's own frame is that key's value
    // (the gatherer's hasSameFrame path, not the easing).
    k0->data().easing().type = util::Easing::Type_Floor;
    for (int frame = 1; frame <= 9; ++frame) {
        CHECK(curve.sample(core::Frame(frame)) == 0.0f);
    }
    CHECK(curve.sample(core::Frame(10)) == 100.0f);
    {
        const BlenderOracle oracle(node, core::Frame(5));
        CHECK(matchesPlayback(oracle, curve, core::Frame(5)));
    }

    // Custom: with x1=1/3 and x2=2/3 the bezier's x IS the normalized progress
    // (x(s)=s), so the rate at a frame is y(s) for the same s. For y1=0, y2=0.5
    // the curve is y(s) = 1.5s^2 - 0.5s^3: 0.0859375 at s=0.25 and 0.3125 at
    // s=0.5. Those are points ON the curve, not a second implementation of it.
    util::Easing::Param& easing = k0->data().easing();
    easing.type = util::Easing::Type_Custom;
    easing.cubicBezier.x1 = 1.0f / 3.0f;
    easing.cubicBezier.y1 = 0.0f;
    easing.cubicBezier.x2 = 2.0f / 3.0f;
    easing.cubicBezier.y2 = 0.5f;
    easing.range = util::Easing::Range_InOut; // Custom ignores range and weight
    easing.weight = 0.2f;

    CHECK(nearly(curve.sample(core::Frame(5)), 31.25f));
    CHECK(nearly(curve.sample(core::Frame(2)), 5.6f)); // 100 * y(0.2) = 100 * 0.056
    {
        // ...and playback agrees, so the closed form above is the document's own
        // value and not an approximation of it.
        for (int frame = 1; frame <= 9; ++frame) {
            const BlenderOracle oracle(node, core::Frame(frame));
            CHECK(matchesPlayback(oracle, curve, core::Frame(frame)));
        }
    }

    // The param a segment carries is the FROM key's -- the one the handle editor
    // writes and playback reads -- and it is present for every graphable type.
    const ctrl::graph::Curve::Segment segment = curve.segment(0);
    CHECK(segment.from == k0 && segment.to == k1);
    CHECK(segment.fromValue == 0.0f && segment.toValue == 100.0f);
    CHECK(curve.isCustom(segment));
    CHECK(segment.easing.type == util::Easing::Type_Custom);
    CHECK(ctrl::graph::Curve::easingOf(*k0, core::TimeKeyType_Rotate) == &k0->data().easing());
    CHECK(
        ctrl::graph::Curve::easingOf(static_cast<const core::RotateKey&>(*k0), core::TimeKeyType_Rotate) ==
        &k0->data().easing()
    );
    return 0;
}

// ---------------------------------------------------------------------------
// 4. The Move channel's spline.
//
// A spatial move path is interpolated by util::FergusonCoonsSpline with
// Catmull-Rom velocities taken from the neighbouring keys, so "the graph draws
// what plays" means the graph has to bow off the chord exactly where playback
// does. The deviation is the evidence: a plain lerp cannot leave the chord.
// ---------------------------------------------------------------------------
int sectionMoveSpline() {
    using core::MoveKey;

    core::TestNode node;
    cmnd::Stack stack;
    auto* a = pushKey<MoveKey>(node, stack, core::TimeKeyType_Move, 0);
    auto* b = pushKey<MoveKey>(node, stack, core::TimeKeyType_Move, 10);
    auto* c = pushKey<MoveKey>(node, stack, core::TimeKeyType_Move, 20);
    a->data().setPos(QVector2D(0.0f, 0.0f));
    b->data().setPos(QVector2D(100.0f, 0.0f));
    c->data().setPos(QVector2D(100.0f, 100.0f)); // the neighbour that bends the path
    // MoveKey's own default spline is Linear, so the Catmull-Rom case has to be
    // asked for explicitly -- which is also the point: the flag decides whether
    // the neighbour is used at all.
    a->data().setSpline(MoveKey::SplineType_CatmullRom);
    b->data().setSpline(MoveKey::SplineType_CatmullRom);
    c->data().setSpline(MoveKey::SplineType_CatmullRom);

    const ctrl::graph::Curve cx(*node.timeLine(), ctrl::graph::Channel(core::TimeKeyType_Move, 0));
    const ctrl::graph::Curve cy(*node.timeLine(), ctrl::graph::Channel(core::TimeKeyType_Move, 1));

    // The straight-line average of the two bounding keys at the midpoint is
    // (50, 0). The neighbour key makes the velocity at the arrival end
    // 0.5*(C-A) = (50,50) instead of the chord, and a Hermite whose end tangents
    // differ leaves the chord: y(t) = 50t(t-1) is strictly negative inside the
    // segment, so the sampled midpoint cannot be y = 0.
    const float catmullY = cy.sample(core::Frame(5));
    CHECK(std::fabs(catmullY) > 0.1f);
    CHECK(nearly(cx.sample(core::Frame(5)), 50.0f, 1.0f)); // x stays on the chord's side
    for (int frame = 1; frame <= 9; ++frame) {
        const BlenderOracle oracle(node, core::Frame(frame));
        CHECK(matchesPlayback(oracle, cx, core::Frame(frame)));
        CHECK(matchesPlayback(oracle, cy, core::Frame(frame)));
    }

    // A Linear spline on either end drops the neighbour from the Catmull-Rom
    // velocity (MoveKey::getCatmullRomVels nulls aKey3 when the arrival key is
    // Linear), so the segment is the straight chord again and the midpoint IS
    // the average. Playback must do the same, so the sampler is checked against
    // it, not against the chord formula.
    a->data().setSpline(MoveKey::SplineType_Linear);
    b->data().setSpline(MoveKey::SplineType_Linear);
    CHECK(nearly(cy.sample(core::Frame(5)), 0.0f, 1e-3f));
    CHECK(nearly(cx.sample(core::Frame(5)), 50.0f, 1e-3f));
    {
        const BlenderOracle oracle(node, core::Frame(5));
        CHECK(matchesPlayback(oracle, cx, core::Frame(5)));
        CHECK(matchesPlayback(oracle, cy, core::Frame(5)));
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 5. The image offset is a staircase.
//
// blendImageKey holds the last key at or before the frame (it never blends the
// offset), which is what makes the channel the one non-interpolated one. The
// two-interior-frames half of that needs two real core::ImageKeys.
// ---------------------------------------------------------------------------
int sectionImageOffsetStaircase() {
    using core::TimeKeyType;
    using ctrl::graph::Channel;

    CHECK(Channel::componentCount(core::TimeKeyType_Image) == 2);
    CHECK(!Channel(core::TimeKeyType_Image, 0).isInterpolated());
    CHECK(!Channel(core::TimeKeyType_Image, 1).isInterpolated());

    // The half that IS observable without a key: with neither an image key nor
    // an image default key, playback holds the (0,0) offset
    // (TimeKeyBlender::getImageOffset), and the sampler reads the same square.
    core::TestNode node;
    const ctrl::graph::Curve cx(*node.timeLine(), Channel(core::TimeKeyType_Image, 0));
    const ctrl::graph::Curve cy(*node.timeLine(), Channel(core::TimeKeyType_Image, 1));
    CHECK(cx.isEmpty() && cy.isEmpty());
    CHECK(cx.sample(core::Frame(3)) == 0.0f);
    CHECK(cy.sample(core::Frame(3)) == 0.0f);
    {
        core::TimeInfo time;
        time.fps = 60;
        time.frame = core::Frame(3);
        const QVector2D held = core::TimeKeyBlender::getImageOffset(node, time);
        CHECK(held.x() == cx.sample(core::Frame(3)));
        CHECK(held.y() == cy.sample(core::Frame(3)));
    }

    // The staircase itself is NOT observable here, and is reported as a skip
    // rather than weakened. The missing precondition is a live GL context:
    // constructing a core::ImageKey binds GL, because ImageKey::Data holds a
    // GridMesh whose mesh buffer is created by LayerMesh::MeshBuffer::GLBinder,
    // which calls gl::Global::makeCurrent() on the global widget -- null under
    // QT_QPA_PLATFORM=offscreen. With no ImageKey constructible there is no key
    // to place at frames 0 and 10, so asserting a step shape would assert
    // nothing. See tests/psd_relink_safety.cpp, which skips its ImageKey section
    // for the same reason.
    std::printf(
        "SKIP section 5 staircase (needs a live GL context: core::ImageKey binds a GridMesh "
        "buffer on construction and QT_QPA_PLATFORM=offscreen provides no GL)\n"
        "  -> the same property IS gated, with a context, by tests/graph_image_step.cpp\n"
    );
    return 0;
}

// ---------------------------------------------------------------------------
// 6. A Custom easing's tangent handles.
//
// The handles are drawn and dragged in NORMALIZED easing space (x = progress
// along the segment, y = value, free to overshoot), and that space is the one
// core::TimeKeyBlender consumes -- which is why the drag can write straight
// back into the key's param.
// ---------------------------------------------------------------------------
int sectionEasingHandles() {
    core::TestNode node;
    cmnd::Stack stack;
    auto* k0 = pushKey<core::OpaKey>(node, stack, core::TimeKeyType_Opa, 0);
    auto* k1 = pushKey<core::OpaKey>(node, stack, core::TimeKeyType_Opa, 10);
    k0->data().setOpacity(0.0f);
    k1->data().setOpacity(1.0f);

    util::Easing::Param& easing = k0->data().easing();
    easing.type = util::Easing::Type_Custom;
    easing.cubicBezier.x1 = 0.2f;
    easing.cubicBezier.y1 = -0.4f;
    easing.cubicBezier.x2 = 0.8f;
    easing.cubicBezier.y2 = 1.3f;

    const ctrl::graph::Curve curve(*node.timeLine(), ctrl::graph::Channel(core::TimeKeyType_Opa, 0));
    CHECK(curve.segmentCount() == 1);
    const ctrl::graph::Curve::Segment segment = curve.segment(0);
    CHECK(segment.from == k0 && segment.to == k1);
    CHECK(segment.fromValue == 0.0f && segment.toValue == 1.0f);
    CHECK(curve.isCustom(segment));
    CHECK(segment.easing.type == util::Easing::Type_Custom);

    // handle(0) is the tangent leaving `from` -- (x1,y1) -- and handle(1) the
    // one arriving at `to` -- (x2,y2) -- with no clamping on the way out.
    const QPointF h0 = ctrl::graph::Curve::handle(segment, 0);
    const QPointF h1 = ctrl::graph::Curve::handle(segment, 1);
    CHECK(std::fabs(h0.x() - 0.2) <= 1e-6);
    CHECK(std::fabs(h0.y() - (-0.4)) <= 1e-6);
    CHECK(std::fabs(h1.x() - 0.8) <= 1e-6);
    CHECK(std::fabs(h1.y() - 1.3) <= 1e-6);

    // setHandle clamps x into [0,1] (a cubic bezier requires it) and leaves y
    // alone, so an overshoot stays expressible: a drag may push a handle above
    // the segment's value span, and that is a legal curve playback will follow.
    util::Easing::Param editedParam = segment.easing;
    ctrl::graph::Curve::setHandle(editedParam, 0, QPointF(1.7, -0.75));
    CHECK(editedParam.cubicBezier.x1 == 1.0f);
    CHECK(editedParam.cubicBezier.y1 == -0.75f);
    ctrl::graph::Curve::setHandle(editedParam, 1, QPointF(-0.5, 2.5));
    CHECK(editedParam.cubicBezier.x2 == 0.0f);
    CHECK(editedParam.cubicBezier.y2 == 2.5f);

    // The write round-trips: what handle() reads back is what was written
    // (modulo the clamp).
    ctrl::graph::Curve::Segment roundTrip = segment;
    roundTrip.easing = editedParam;
    const QPointF r0 = ctrl::graph::Curve::handle(roundTrip, 0);
    CHECK(r0.x() == 1.0);
    CHECK(r0.y() == -0.75);

    // A segment that is not Custom exposes no handles to draw.
    k0->data().easing().type = util::Easing::Type_Linear;
    CHECK(!curve.isCustom(curve.segment(0)));

    // The drag writes the document's own param (Curve::easingOf is that bridge),
    // so moving a handle moves the curve the panel draws. Going back to Custom
    // first, because that is the only type that has handles.
    util::Easing::Param* live = ctrl::graph::Curve::easingOf(*k0, core::TimeKeyType_Opa);
    CHECK(live == &k0->data().easing());
    live->type = util::Easing::Type_Custom;
    ctrl::graph::Curve::setHandle(*live, 0, QPointF(0.25, 0.0));
    ctrl::graph::Curve::setHandle(*live, 1, QPointF(0.75, 0.5));
    // x(0.5) = 0.375*0.25 + 0.375*0.75 + 0.125 = 0.5 exactly, so the rate at
    // the midpoint is y(0.5) = 0.375*y1 + 0.375*y2 + 0.125 = 0.3125 -- and the
    // 0..1 opacity segment is 0.3125 there, not the 0.5 a plain lerp gives.
    CHECK(nearly(curve.sample(core::Frame(5)), 0.3125f));
    return 0;
}

// ---------------------------------------------------------------------------
// 7. The polyline the renderer draws.
// ---------------------------------------------------------------------------
int sectionPolyline() {
    core::TestNode node;
    cmnd::Stack stack;
    auto* k0 = pushKey<core::RotateKey>(node, stack, core::TimeKeyType_Rotate, 0);
    auto* k1 = pushKey<core::RotateKey>(node, stack, core::TimeKeyType_Rotate, 10);
    k0->data().setRotate(0.0f);
    k1->data().setRotate(100.0f);
    k0->data().easing().type = util::Easing::Type_Cubic;
    k0->data().easing().range = util::Easing::Range_InOut;

    const ctrl::graph::Curve curve(*node.timeLine(), ctrl::graph::Channel(core::TimeKeyType_Rotate, 0));
    const core::Frame from(0);
    const core::Frame to(10);

    QVector<QPointF> line;
    curve.buildPolyline(from, to, 10, line);
    CHECK(line.size() >= 2);
    // Both ends of the window are emitted, at the window's own frames.
    CHECK(std::fabs(line.first().x() - static_cast<double>(from.get())) <= 1e-9);
    CHECK(std::fabs(line.last().x() - static_cast<double>(to.get())) <= 1e-9);
    CHECK(nearly(static_cast<float>(line.first().y()), curve.sample(from)));
    CHECK(nearly(static_cast<float>(line.last().y()), curve.sample(to)));

    // Ordered in frame, inside the window, and every sample that lands ON a
    // frame carries what sample() holds there: the polyline is the same
    // evaluation, not a second interpolation of its own.
    int onFrameCount = 0;
    for (int i = 0; i < line.size(); ++i) {
        const QPointF& point = line.at(i);
        CHECK(point.x() >= -1e-9 && point.x() <= static_cast<double>(to.get()) + 1e-9);
        if (i > 0)
            CHECK(point.x() >= line.at(i - 1).x() - 1e-9);

        const double rounded = std::floor(point.x() + 0.5);
        if (std::fabs(point.x() - rounded) <= 1e-6) {
            CHECK(nearly(static_cast<float>(point.y()), curve.sample(core::Frame(static_cast<int>(rounded)))));
            ++onFrameCount;
        }
    }
    // The two window ends alone would not show that the curve was evaluated
    // inside the window.
    CHECK(onFrameCount >= 3);

    // A step-held channel comes out as steps: with a Floor easing the rate is 0
    // across the whole interval, so every sample before the arrival key's frame
    // carries the lower key's value and the window end carries the arrival
    // key's. (The image channel is the other step-held one and cannot be built
    // headless -- section 5.)
    k0->data().easing().type = util::Easing::Type_Floor;
    QVector<QPointF> steps;
    curve.buildPolyline(from, to, 10, steps);
    CHECK(steps.size() >= 2);
    CHECK(std::fabs(steps.last().x() - static_cast<double>(to.get())) <= 1e-9);
    CHECK(steps.last().y() == 100.0f);
    for (const QPointF& point : steps) {
        if (point.x() <= static_cast<double>(to.get()) - 1e-6)
            CHECK(point.y() == 0.0f);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 8. The Scaler's two axes.
//
// The frame axis delegates its ladder to ctrl::time::Scaler (the timeline's own
// convention) and adds the panel's origin; the value axis is a window with an
// inverted linear map. What a person depends on is that a frame stays under the
// pixel it was under, that a pixel means a pixel back, and that a constant
// channel still gets a usable axis.
// ---------------------------------------------------------------------------
int sectionScaler() {
    using ctrl::graph::Scaler;

    Scaler scaler;
    scaler.setRect(QRect(0, 0, 600, 400));
    // The left column is the scaler's OWN format-derived reserve, not a constant:
    // margin 4, then axisLabelWidth()'s column, then the plot, then the margin again.
    // Written as the rule rather than the number it happens to come to for the font
    // in force, so this pins the inset arithmetic and not a magic 46.
    CHECK(scaler.axisLabelWidth() >= Scaler::kAxisLabelWidth); // never below the floor
    CHECK(scaler.axisLabelWidth() <= Scaler::kMaxAxisLabelWidth); // never past the ceiling
    CHECK(
        scaler.plotRect()
        == QRect(
            Scaler::kMargin + scaler.axisLabelWidth(), Scaler::kMargin,
            600 - 2 * Scaler::kMargin - scaler.axisLabelWidth(), 400 - 2 * Scaler::kMargin
        )
    );
    CHECK(scaler.rect() == QRect(0, 0, 600, 400));
    // The value window is seeded non-degenerate: every mapping divides by its
    // span.
    CHECK(scaler.valueMin() == 0.0f);
    CHECK(scaler.valueMax() == 1.0f);

    const QRect plot = scaler.plotRect();
    // Frame 0 starts at the plot's left edge, and the offset is free.
    CHECK(scaler.pixelX(core::Frame(0)) == plot.left());
    CHECK(scaler.frameAt(plot.left()) == core::Frame(0));
    scaler.panFrame(10);
    CHECK(scaler.pixelX(core::Frame(0)) == plot.left() + 10);
    scaler.panFrame(-10);

    // frame <-> pixel: the frame axis rounds to whole frames, so one pixel is
    // the exact bound on the round trip; pixelXOfFrame/frameAtPixel are exact
    // inverses in doubles.
    for (int x = plot.left(); x < plot.right(); x += 37) {
        CHECK(std::abs(scaler.pixelX(scaler.frameAt(x)) - x) <= 1);
    }
    for (int frame = -20; frame <= 40; frame += 7) {
        CHECK(std::fabs(scaler.frameAtPixel(scaler.pixelXOfFrame(frame)) - frame) <= 1e-9);
    }

    // updateFrameZoom re-anchors the frame axis: the frame under the anchor
    // pixel is the same before and after (the content coordinate is scaled
    // exactly and only the final pixel is rounded).
    const int anchorX = plot.left() + 250;
    const double anchorFrame = scaler.frameAtPixel(anchorX);
    CHECK(scaler.updateFrameZoom(-120, anchorX)); // one notch in: 2 -> 3 px/frame
    CHECK(scaler.frameScale().pixelsPerFrame() == 3);
    CHECK(std::fabs(scaler.frameAtPixel(anchorX) - anchorFrame) <= 1e-6);
    // The ladder is clamped: at the ceiling a further zoom-in notch moves
    // nothing and says so.
    CHECK(scaler.updateFrameZoom(-100000, anchorX));
    CHECK(scaler.frameScale().pixelsPerFrame() == 16);
    CHECK(!scaler.updateFrameZoom(-120, anchorX));

    // value <-> pixel round trip, and the two ends of the window.
    scaler.setValueWindow(0.0f, 1.0f);
    CHECK(scaler.valueAt(plot.top()) == 1.0f);
    for (int y = plot.top(); y < plot.bottom(); y += 23) {
        CHECK(std::abs(scaler.pixelY(scaler.valueAt(y)) - y) <= 1);
    }

    // A constant channel still gets a usable window (a degenerate one would
    // leave pixelY with nothing to divide by) with the constant inside it.
    scaler.fitValueWindow(5.0f, 5.0f);
    CHECK(scaler.valueMax() > scaler.valueMin());
    CHECK(scaler.valueMin() < 5.0f);
    CHECK(5.0f < scaler.valueMax());
    CHECK(scaler.pixelY(5.0f) > plot.top());
    CHECK(scaler.pixelY(5.0f) < plot.bottom());
    // ...and a real span is padded by 5% at each end, so an extreme key is
    // drawn inside the plot instead of on its border.
    scaler.fitValueWindow(10.0f, 20.0f);
    CHECK(nearly(scaler.valueMin(), 9.5f));
    CHECK(nearly(scaler.valueMax(), 20.5f));

    // Value zoom holds the value under the anchor exactly and scales the span.
    scaler.setValueWindow(0.0f, 100.0f);
    const int anchorY = plot.top() + 100;
    const float anchorValue = scaler.valueAt(anchorY);
    CHECK(scaler.updateValueZoom(2.0f, anchorY));
    CHECK(nearly(scaler.valueAt(anchorY), anchorValue, 1e-3f));
    CHECK(nearly(scaler.valueMax() - scaler.valueMin(), 50.0f, 1e-2f));
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    // QApplication, not QCoreApplication: the timeline and the key types are
    // QObject-based and QObject::tr is what the channel labels go through.
    QApplication app(argc, argv);

    if (sectionChannelTable())
        return 1;
    if (sectionSamplerMatchesPlayback())
        return 1;
    if (sectionSamplerWithoutKeys())
        return 1;
    if (sectionSamplerWithOneKey())
        return 1;
    if (sectionEasingIdentities())
        return 1;
    if (sectionMoveSpline())
        return 1;
    if (sectionImageOffsetStaircase())
        return 1;
    if (sectionEasingHandles())
        return 1;
    if (sectionPolyline())
        return 1;
    if (sectionScaler())
        return 1;

    std::printf("graph_curve: all checks passed\n");
    return 0;
}
