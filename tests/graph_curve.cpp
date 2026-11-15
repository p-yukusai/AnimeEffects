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
//     sample() does, comes out as steps when the channel holds, and publishes a
//     non-looping channel's hold as a segment ACROSS a window that misses the
//     keys (a single point is dropped by the drawing layer, so the curve would
//     vanish)
//   8 the Scaler's two axes: the frame round trip, the zoom's anchor rule, and
//     the value window including a degenerate (constant) span
//  11 the easing param's on-disk form: the handle-link flag round-trips at 0.13
//     (the version that added it), and a file written before the field existed
//     still loads and reads FREE (the old file's key never moved its partner)
//  12 per-component easing: a Move key's x and y play and draw independently,
//     a 0.14 key round-trips two different components, and a 0.13 key's single
//     param is broadcast to every component
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
#include "core/Deserializer.h"
#include "core/FormatVersion.h"
#include "core/Frame.h"
#include "core/HsvKey.h"
#include "core/MoveKey.h"
#include "core/ObjectNode.h"
#include "core/OpaKey.h"
#include "core/RotateKey.h"
#include "core/ScaleKey.h"
#include "core/Serializer.h"
#include "core/TimeInfo.h"
#include "core/TimeKeyBlender.h"
#include "core/TimeKeyExpans.h"
#include "core/TimeKeyType.h"
#include "core/TimeLine.h"
#include "ctrl/graph/graph_Channel.h"
#include "ctrl/graph/graph_Curve.h"
#include "ctrl/graph/graph_Scaler.h"
#include "gl/DeviceInfo.h"
#include "util/Easing.h"
#include "util/IProgressReporter.h"
#include "util/StreamReader.h"
#include "util/StreamWriter.h"
#include <sstream>
#include <string>

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
        util::Easing::Param& e0 = k0->data().easing(0);
        e0.type = util::Easing::Type_Custom;
        e0.cubicBezier.x1 = 1.0f / 3.0f;
        e0.cubicBezier.y1 = 0.0f;
        e0.cubicBezier.x2 = 2.0f / 3.0f;
        e0.cubicBezier.y2 = 0.5f;
        k1->data().easing(0).type = util::Easing::Type_Expo;
        k1->data().easing(0).range = util::Easing::Range_Out;
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
        k0->data().easing(0).type = util::Easing::Type_Quad;
        k0->data().easing(0).range = util::Easing::Range_InOut;
        k1->data().easing(0).type = util::Easing::Type_Floor;
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
        // Every component carries the curve: each channel is sampled against
        // playback on its own, and per-component storage means component 1 would
        // otherwise be Linear.
        for (int component = 0; component < core::ScaleKey::kComponents; ++component) {
            k0->data().easing(component).type = util::Easing::Type_Back;
            k0->data().easing(component).range = util::Easing::Range_Out;
            k1->data().easing(component).type = util::Easing::Type_Cubic;
            k1->data().easing(component).range = util::Easing::Range_In;
        }
    }

    // Depth: Linear then Expo/InOut.
    {
        auto* k0 = pushKey<core::DepthKey>(aNode, aStack, core::TimeKeyType_Depth, 0);
        auto* k1 = pushKey<core::DepthKey>(aNode, aStack, core::TimeKeyType_Depth, 10);
        auto* k2 = pushKey<core::DepthKey>(aNode, aStack, core::TimeKeyType_Depth, 20);
        k0->data().setDepth(5.0f);
        k1->data().setDepth(25.0f);
        k2->data().setDepth(10.0f);
        k1->data().easing(0).type = util::Easing::Type_Expo;
        k1->data().easing(0).range = util::Easing::Range_InOut;
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
        for (int component = 0; component < core::BlurKey::kComponents; ++component) {
            k0->data().easing(component).type = util::Easing::Type_Elastic;
            k0->data().easing(component).range = util::Easing::Range_Out;
            k1->data().easing(component).type = util::Easing::Type_Bounce;
            k1->data().easing(component).range = util::Easing::Range_Out;
        }
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
        for (int component = 0; component < core::HSVKey::kComponents; ++component) {
            k1->data().easing(component).type = util::Easing::Type_Round;
        }
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
    // segment's samples untouched (playback reads k0->data().easing(0) for the
    // whole interval, whatever the arrival key carries).
    {
        const ctrl::graph::Curve rotate(*node.timeLine(), ctrl::graph::Channel(core::TimeKeyType_Rotate, 0));
        const float before = rotate.sample(core::Frame(5));
        auto* arrival = static_cast<core::RotateKey*>(node.timeLine()->timeKey(core::TimeKeyType_Rotate, 10));
        CHECK(arrival);
        arrival->data().easing(0).type = util::Easing::Type_Elastic;
        arrival->data().easing(0).range = util::Easing::Range_InOut;
        arrival->data().easing(0).weight = 0.25f;
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
    CHECK(k0->data().easing(0).type == util::Easing::Type_Linear);
    CHECK(curve.sample(core::Frame(5)) == 50.0f);
    CHECK(curve.sample(core::Frame(0)) == 0.0f);
    CHECK(curve.sample(core::Frame(10)) == 100.0f);

    // Floor: the rate is 0 across the whole interval, so every interior frame
    // holds the lower key's value; the next key's own frame is that key's value
    // (the gatherer's hasSameFrame path, not the easing).
    k0->data().easing(0).type = util::Easing::Type_Floor;
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
    util::Easing::Param& easing = k0->data().easing(0);
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
    CHECK(ctrl::graph::Curve::easingOf(*k0, core::TimeKeyType_Rotate, 0) == &k0->data().easing(0));
    CHECK(
        ctrl::graph::Curve::easingOf(static_cast<const core::RotateKey&>(*k0), core::TimeKeyType_Rotate, 0) ==
        &k0->data().easing(0)
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

    util::Easing::Param& easing = k0->data().easing(0);
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
    k0->data().easing(0).type = util::Easing::Type_Linear;
    CHECK(!curve.isCustom(curve.segment(0)));

    // The drag writes the document's own param (Curve::easingOf is that bridge),
    // so moving a handle moves the curve the panel draws. Going back to Custom
    // first, because that is the only type that has handles.
    util::Easing::Param* live = ctrl::graph::Curve::easingOf(*k0, core::TimeKeyType_Opa, 0);
    CHECK(live == &k0->data().easing(0));
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
    k0->data().easing(0).type = util::Easing::Type_Cubic;
    k0->data().easing(0).range = util::Easing::Range_InOut;

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

    // A window NARROWER than the segment still gets the whole budget: the samples land on the part
    // the window shows, so a zoomed-in view of a long segment is a curve and not one chord. (Sampling
    // over the segment's own span instead puts about one point in a view like this.)
    {
        const core::Frame nearFrom(0);
        const core::Frame nearTo(1);
        QVector<QPointF> zoomed;
        curve.buildPolyline(nearFrom, nearTo, 10, zoomed);
        int inside = 0;
        double lastX = -1.0;
        double widest = 0.0;
        for (const QPointF& point : zoomed) {
            if (point.x() < -1e-9 || 1.0 + 1e-9 < point.x())
                continue; // the segment's own ends, which the caller clips away
            ++inside;
            if (lastX >= 0.0)
                widest = std::max(widest, point.x() - lastX);
            lastX = point.x();
        }
        CHECK(inside >= 9);
        CHECK(widest <= 0.1 + 1e-6);
    }

    // With the project's LOOP on, the frames outside the keys are the eased segment from the LAST key
    // back to the first, wrapped by the frame range's span (core::TimeKeyGatherer's own rule) - not a
    // hold. The keys here sit on the range's own ends (0 and 10 of a 0..10 range, span 11), so the
    // wrapped segment is the last key's easing over the one frame that is left of the period.
    {
        k0->data().easing(0).type = util::Easing::Type_Linear; // linear, so the wrapped values are readable
        // A range ONE PERIOD wide with the keys inside it (0..20, keys at 0 and 10): past the last key
        // the wrap runs to the first key one span ahead, and past the RANGE the frame clamps, so the
        // curve is flat there rather than extrapolating.
        ctrl::graph::Curve looped(*node.timeLine(), ctrl::graph::Channel(core::TimeKeyType_Rotate, 0));
        looped.setLoop(true, util::Range(0, 20));
        CHECK(looped.isLooped());

        // The last key's own value at its own frame (the wrap starts there), and strict movement
        // toward the first key across the wrap - a property of the rule, not a restatement of the
        // easing's arithmetic.
        CHECK(nearly(looped.sample(core::Frame(10)), 100.0f));
        const float inside15 = looped.sample(core::Frame(15));
        const float atRangeEnd = looped.sample(core::Frame(20));
        CHECK(inside15 < 100.0f);          // the wrap moved off the last key...
        CHECK(0.0f < atRangeEnd);          // ...toward the first key without reaching it
        CHECK(atRangeEnd < inside15);
        CHECK(!nearly(inside15, 100.0f));  // a HOLD would sit at 100 for both of these

        // Past the frame range the frame clamps: FLAT at the range's edge value.
        CHECK(nearly(looped.sample(core::Frame(25)), atRangeEnd));
        CHECK(nearly(looped.sample(core::Frame(200)), atRangeEnd));
        // ...and flat before it, at the range's start value (the first key's).
        CHECK(nearly(looped.sample(core::Frame(-5)), looped.sample(core::Frame(0))));
        CHECK(nearly(looped.sample(core::Frame(-500)), looped.sample(core::Frame(0))));

        // A polyline over a window wider than the range follows the same rule everywhere: the samples
        // inside the range are the wrap, and past the range they are constant.
        QVector<QPointF> loopLine;
        looped.buildPolyline(core::Frame(-25), core::Frame(45), 10, loopLine);
        CHECK(!loopLine.isEmpty());
        CHECK(std::fabs(loopLine.first().x() + 25.0) <= 1e-9);
        CHECK(std::fabs(loopLine.last().x() - 45.0) <= 1e-9);
        for (const QPointF& point : loopLine) {
            const float at = std::min(20.0f, std::max(0.0f, static_cast<float>(point.x())));
            CHECK(nearly(static_cast<float>(point.y()), looped.sample(core::Frame::fromDecimal(at))));
            if (point.x() > 20.0 + 1e-6)
                CHECK(nearly(static_cast<float>(point.y()), atRangeEnd));
        }
        k0->data().easing(0).type = util::Easing::Type_Cubic;
    }

    // The value HOLDS outside the keys (what sample() reports there), so the polyline reaches the
    // WINDOW's own edges: the whole X range shows what playback plays, not only the span between the
    // first and last key.
    {
        QVector<QPointF> wide;
        curve.buildPolyline(core::Frame(-5), core::Frame(15), 10, wide);
        CHECK(!wide.isEmpty());
        CHECK(std::fabs(wide.first().x() + 5.0) <= 1e-9);
        CHECK(nearly(static_cast<float>(wide.first().y()), curve.sample(core::Frame(-5))));
        CHECK(std::fabs(wide.last().x() - 15.0) <= 1e-9);
        CHECK(nearly(static_cast<float>(wide.last().y()), curve.sample(core::Frame(15))));
        for (int i = 1; i < wide.size(); ++i)
            CHECK(wide.at(i).x() >= wide.at(i - 1).x() - 1e-9);
        // "Holds" stated as VALUES: the first key's before the keys, the last key's after them. A
        // curve with no loop range has no range to clamp to, so nothing moves these.
        CHECK(nearly(curve.sample(core::Frame(-5)), curve.sample(core::Frame(0))));
        CHECK(nearly(curve.sample(core::Frame(15)), curve.sample(core::Frame(10))));
        CHECK(nearly(curve.sample(core::Frame(-40)), curve.sample(core::Frame(0))));
    }

    // A step-held channel comes out as steps: with a Floor easing the rate is 0
    // across the whole interval, so every sample before the arrival key's frame
    // carries the lower key's value and the window end carries the arrival
    // key's. (The image channel is the other step-held one and cannot be built
    // headless -- section 5.)
    k0->data().easing(0).type = util::Easing::Type_Floor;
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
// 7b. A window that MISSES the keys still draws: the non-looping hold spans it.
//
// Panning puts the plot's frame window entirely before or after a channel's keys
// - and, because the plot's window is now the panel-wide shared one minus the
// value column, a key can sit at its left edge or just behind it. Playback HOLDS
// the nearest key's value across such a window, so the polyline must be a
// horizontal segment across it. ONE point is not a curve: the drawing layer
// drops anything shorter than two points (gui::drawPanel's size() < 2) and the
// curve disappears from the panel. The looping branch is deliberately untouched
// (section 7 pins the wrap), and the polyline stays UNCLIPPED - the drawing
// layer clips, the model does not.
// ---------------------------------------------------------------------------
int sectionPolylineHoldOutsideKeys() {
    core::TestNode node;
    cmnd::Stack stack;
    auto* k0 = pushKey<core::RotateKey>(node, stack, core::TimeKeyType_Rotate, 10);
    auto* k1 = pushKey<core::RotateKey>(node, stack, core::TimeKeyType_Rotate, 20);
    k0->data().setRotate(30.0f);
    k1->data().setRotate(70.0f);
    k0->data().easing(0).type = util::Easing::Type_Linear;

    const ctrl::graph::Curve curve(*node.timeLine(), ctrl::graph::Channel(core::TimeKeyType_Rotate, 0));
    CHECK(!curve.isLooped()); // loop OFF: the rule under test is the HOLD

    // The window the panel hands in (the plot's own frame window) is drawn as ONE horizontal
    // segment: both edges present, and every point at the one held value.
    auto holdsAcross = [](const ctrl::graph::Curve& aCurve, float aFrom, float aTo, float aValue,
                          const char* aWhat) {
        QVector<QPointF> line;
        aCurve.buildPolyline(core::Frame::fromDecimal(aFrom), core::Frame::fromDecimal(aTo), 10, line);
        if (line.size() < 2) {
            std::printf(
                "  %s: %d point(s), which the drawing layer drops\n", aWhat, static_cast<int>(line.size())
            );
            return false;
        }
        if (std::fabs(line.first().x() - aFrom) > 1e-6 || std::fabs(line.last().x() - aTo) > 1e-6)
            return false;
        for (const QPointF& point : line) {
            if (!nearly(static_cast<float>(point.y()), aValue))
                return false;
            if (point.x() < aFrom - 1e-6 || aTo + 1e-6 < point.x())
                return false;
        }
        return true;
    };

    // The keys are LEFT of the window (panned past them): the LAST key's value holds across it.
    {
        QVector<QPointF> line;
        curve.buildPolyline(core::Frame(30), core::Frame(40), 10, line);
        std::printf("[7b] keys left of window [30,40]: %d point(s)\n", static_cast<int>(line.size()));
        CHECK(line.size() >= 2);
        CHECK(std::fabs(line.first().x() - 30.0) <= 1e-9);
        CHECK(std::fabs(line.last().x() - 40.0) <= 1e-9);
        CHECK(nearly(static_cast<float>(line.first().y()), curve.sample(core::Frame(30))));
        CHECK(nearly(static_cast<float>(line.last().y()), curve.sample(core::Frame(30))));
        // ...and the held value really is the last key's (70), not the first's (30).
        CHECK(nearly(curve.sample(core::Frame(30)), curve.sample(core::Frame(20))));
        CHECK(nearly(curve.sample(core::Frame(20)), k1->data().rotate()));
        CHECK(holdsAcross(curve, 30.0f, 40.0f, curve.sample(core::Frame(30)), "keys left of the window"));
    }

    // The keys are RIGHT of the window: the FIRST key's value holds across it.
    {
        QVector<QPointF> line;
        curve.buildPolyline(core::Frame(0), core::Frame(5), 10, line);
        CHECK(line.size() >= 2);
        CHECK(std::fabs(line.first().x()) <= 1e-9);
        CHECK(std::fabs(line.last().x() - 5.0) <= 1e-9);
        CHECK(nearly(static_cast<float>(line.first().y()), curve.sample(core::Frame(0))));
        CHECK(nearly(static_cast<float>(line.last().y()), curve.sample(core::Frame(0))));
        CHECK(nearly(curve.sample(core::Frame(0)), curve.sample(core::Frame(10))));
        CHECK(nearly(curve.sample(core::Frame(10)), k0->data().rotate()));
        CHECK(holdsAcross(curve, 0.0f, 5.0f, curve.sample(core::Frame(0)), "keys right of the window"));
    }

    // The keys STRADDLE the window (the ordinary case): still the curve - and, being published
    // unclipped, the touched segment's own endpoints (10 and 20) are present even though both lie
    // outside the window, with the played values there.
    {
        QVector<QPointF> line;
        curve.buildPolyline(core::Frame(12), core::Frame(18), 10, line);
        CHECK(line.size() >= 2);
        // The published ends are the touched segment's own keys (30 and 70), not the window's -
        // that is the unclipped publication. The drawing layer cuts them at the plot edge.
        CHECK(nearly(static_cast<float>(line.first().y()), k0->data().rotate()));
        CHECK(nearly(static_cast<float>(line.last().y()), k1->data().rotate()));
        CHECK(line.last().y() > line.first().y()); // the Linear segment rises across the window
        CHECK(std::fabs(line.first().x() - 10.0) <= 1e-9); // outside the window: unclipped
        CHECK(std::fabs(line.last().x() - 20.0) <= 1e-9);
        CHECK(line.first().x() < 12.0 - 1e-6);
        CHECK(line.last().x() > 18.0 + 1e-6);
        // Inside the window the points are the played values frame by frame, as in section 7.
        for (const QPointF& point : line) {
            const double rounded = std::floor(point.x() + 0.5);
            if (std::fabs(point.x() - rounded) <= 1e-6 && 12.0 <= point.x() && point.x() <= 18.0)
                CHECK(nearly(
                    static_cast<float>(point.y()), curve.sample(core::Frame(static_cast<int>(rounded)))
                ));
        }
        // In frame order throughout, even across the hold-free window.
        for (int i = 1; i < line.size(); ++i)
            CHECK(line.at(i).x() >= line.at(i - 1).x() - 1e-9);
    }

    // A frame RANGE with the loop still off behaves the same: past the range the frame clamps to
    // the range's edge, whose value is the nearest key's, so the hold is the same flat segment.
    {
        ctrl::graph::Curve ranged(*node.timeLine(), ctrl::graph::Channel(core::TimeKeyType_Rotate, 0));
        ranged.setLoop(false, util::Range(0, 30));
        CHECK(!ranged.isLooped());
        CHECK(holdsAcross(ranged, 35.0f, 45.0f, ranged.sample(core::Frame(35)), "keys behind the range"));
        CHECK(nearly(ranged.sample(core::Frame(35)), k1->data().rotate()));
    }

    // ONE key: it holds across the whole window - before the key, through it, and after it.
    {
        core::TestNode single;
        cmnd::Stack singleStack;
        auto* only = pushKey<core::OpaKey>(single, singleStack, core::TimeKeyType_Opa, 10);
        only->data().setOpacity(0.25f);
        const ctrl::graph::Curve flat(*single.timeLine(), ctrl::graph::Channel(core::TimeKeyType_Opa, 0));
        CHECK(flat.keys().size() == 1);
        CHECK(holdsAcross(flat, 0.0f, 30.0f, flat.sample(core::Frame(0)), "one key, window around it"));
        CHECK(holdsAcross(flat, 40.0f, 60.0f, flat.sample(core::Frame(40)), "one key, window right of it"));
        CHECK(holdsAcross(flat, -30.0f, -10.0f, flat.sample(core::Frame(-20)), "one key, window left of it"));
        // The key sits ON a window edge (a panned plot puts it there): still a two-point hold,
        // because one point would be dropped and the key would look like an empty channel.
        CHECK(holdsAcross(flat, 10.0f, 30.0f, only->data().opacity(), "one key at the window's left edge"));
        CHECK(holdsAcross(flat, 0.0f, 10.0f, only->data().opacity(), "one key at the window's right edge"));
        // The value is the key's own everywhere in the window.
        CHECK(nearly(flat.sample(core::Frame(0)), only->data().opacity()));
        CHECK(nearly(flat.sample(core::Frame(10)), only->data().opacity()));
        CHECK(nearly(flat.sample(core::Frame(60)), only->data().opacity()));
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
    // The plot rectangle is a DESIGNED SECTION, not a fudge factor: the ruler is a
    // 22px strip ABOVE the graph (the timeline's own arrangement - its surface
    // starts below the TimeRuler), and Scaler::plotRect() reserves those rows at
    // the top. So the curve rectangle is:
    //   x      = kMargin + the measured axis-label column
    //   y      = kMargin + kRulerBandHeight     <- the band's bottom + 1
    //   width  = 600 - kMargin - axisLabelWidth() - kScrollBarReserve
    //   height = 400 - kMargin - kRulerBandHeight - kScrollBarReserve
    // and no curve/key/handle/chip can be clipped into the ruler's rows. The left
    // column is the scaler's OWN format-derived reserve, not a constant (margin 4,
    // then axisLabelWidth()'s column, then the plot, then the margin again); it is
    // written as the rule rather than the number the font happens to produce, so
    // this pins the inset arithmetic and not a magic 46. The RIGHT and BOTTOM insets
    // are the SCROLL BARS' LANES (the application's 12px lane, exactly - no gap, no
    // outer margin), which is why the plot's top-left corner is the only one that does
    // not move for them.
    CHECK(scaler.axisLabelWidth() >= Scaler::kAxisLabelWidth); // never below the floor
    CHECK(scaler.axisLabelWidth() <= Scaler::kMaxAxisLabelWidth); // never past the ceiling
    // The 22px band is the timeline's own kHeaderHeight (GraphPanelModel.cpp's
    // static_assert pins gui::frameRuler::kBandHeight to it and to this constant).
    CHECK(Scaler::kRulerBandHeight == 22);
    CHECK(
        scaler.plotRect()
        == QRect(
            Scaler::kMargin + scaler.axisLabelWidth(), Scaler::kMargin + Scaler::kRulerBandHeight,
            600 - Scaler::kMargin - scaler.axisLabelWidth() - Scaler::kScrollBarReserve,
            400 - Scaler::kMargin - Scaler::kRulerBandHeight - Scaler::kScrollBarReserve
        )
    );
    // ...and the two bars' lanes are the application's 12px scroll bar on the
    // TIMELINE'S edges - the reserve is exactly the lane (no gap, no outer margin),
    // and the horizontal lane is the panel's last 12 rows across its whole width.
    CHECK(Scaler::kScrollBarReserve == Scaler::kScrollBarThickness);
    // The dimensions are the application's own scroll bar (AppStyle.cpp's
    // CC_ScrollBar: a 12px invisible lane and a 4px, radius-2 pill), not a look of
    // this panel's invention.
    CHECK(Scaler::kScrollBarThickness == 12);
    CHECK(Scaler::kScrollBarPillThickness == 4);
    CHECK(Scaler::kScrollBarPillRadius == 2);
    CHECK(Scaler::kScrollBarStepPixels == 24); // the timeline's own kVerticalStep
    {
        // A separate scaler: the shared-window cases below move the frame origin,
        // and this section's later mapping assertions start from frame 0 at the
        // plot's left edge.
        Scaler barScale;
        barScale.setRect(QRect(0, 0, 600, 400));
        const QRect barPlot = barScale.plotRect();
        CHECK(barScale.scrollBars(1.0, 60.0, -10.0f, 10.0f).hTrack.top() == barPlot.bottom() + 1);
        CHECK(Scaler::kScrollBarTrailingMargin == 24); // ctrl::TimeLineEditor::kTimeLineMargin

        // THE HANDLE IS THE TIMELINE'S, BY HAND. gui::TimelineScrollBar::handleRect
        // maps the viewport's two edges over the UNION track and rounds each:
        //     fx(v) = int((v - trackMin)/(trackMax - trackMin) * trackWidth + 0.5)
        // The horizontal track's content end carries the timeline's trailing margin
        // (24px), expressed in frames at the axis' rate. Here the panel is 600x400,
        // so the horizontal lane is 600px wide and the vertical one 362px tall.
        //
        // Case A: content 0..100, window 0..50 (applyFrameWindow maps it to the
        // panel). ppf = 600/50 = 12, trailing margin = 24/12 = 2 frames, so the union
        // track is [0,102]; fx(0) = 0 and fx(50) = int(294.118 + 0.5) = 294.
        {
            CHECK(barScale.applyFrameWindow(0.0, 50.0));
            const Scaler::ScrollBars bars = barScale.scrollBars(0.0, 100.0, -10.0f, 10.0f);
            CHECK(!bars.isEmpty());
            CHECK(bars.hTrack.left() == barScale.rect().left());
            CHECK(bars.hTrack.width() == barScale.rect().width());
            CHECK(bars.hTrack.bottom() == barScale.rect().bottom()); // FLUSH
            CHECK(bars.hTrack.right() == barScale.rect().right());
            CHECK(bars.vTrack.left() == barPlot.right() + 1);
            CHECK(bars.vTrack.width() == Scaler::kScrollBarThickness);
            CHECK(bars.vTrack.bottom() + 1 == bars.hTrack.top()); // the corner is the horizontal bar's
            CHECK(bars.hThumb == QRect(0, bars.hTrack.top(), 294, bars.hTrack.height()));
            CHECK(bars.hHasRun());
            CHECK(std::fabs(bars.hContentPerPixel - 102.0 / 600.0) < 1e-12);
            std::printf(
                "[8] bars A: content 0..100 window 0..50 ppf 12 margin 2 -> union [0,102]; "
                "hThumb x=0 w=294 of 600 (hand: fx(50)=int(294.118+0.5)=294), %.6f frames/lane px\n",
                bars.hContentPerPixel
            );

            // Case B: the same track, window 25..75. fx(25) = int(147.059 + 0.5) = 147,
            // fx(75) = int(441.176 + 0.5) = 441, so the handle is 147..441 (294 wide).
            CHECK(barScale.applyFrameWindow(25.0, 75.0));
            const Scaler::ScrollBars mid = barScale.scrollBars(0.0, 100.0, -10.0f, 10.0f);
            CHECK(mid.hThumb == QRect(147, mid.hTrack.top(), 294, mid.hTrack.height()));
            std::printf("[8] bars B: window 25..75 -> hThumb x=147 w=294 (hand: 147..441)\n");

            // Case C: a window NARROWER than the visible range - 10..12 of 0..100.
            // ppf = 300, margin = 0.08, union [0,100.08]; fx(10) = int(59.952 + 0.5) =
            // 60 and fx(12) = int(71.942 + 0.5) = 72, so the handle is one 12px run.
            CHECK(barScale.applyFrameWindow(10.0, 12.0));
            const Scaler::ScrollBars narrow = barScale.scrollBars(0.0, 100.0, -10.0f, 10.0f);
            CHECK(narrow.hThumb == QRect(60, narrow.hTrack.top(), 12, narrow.hTrack.height()));
            CHECK(narrow.hHasRun());
            std::printf("[8] bars C: window 10..12 -> hThumb x=60 w=12 (hand: 60..72)\n");

            // Case D: the window covers the union track (content 0..40, window -10..60)
            // - the handle fills the lane. That is NOT a dead bar: the run still
            // exists, and its drag ratio is the union track's (the timeline reaches
            // beginScrollbarDrag with range == pageStep too).
            CHECK(barScale.applyFrameWindow(-10.0, 60.0));
            const Scaler::ScrollBars whole = barScale.scrollBars(0.0, 40.0, -10.0f, 10.0f);
            CHECK(whole.hThumb == whole.hTrack);
            CHECK(whole.hHasRun());
            CHECK(std::fabs(whole.hContentPerPixel - 70.0 / 600.0) < 1e-12);
        }

        // THE VALUE AXIS IS INVERTED ALONG ITS TRACK. content 0..100, window 25..75
        // reads in track order as content [-100,0], window [-75,-25]; fx(-75) =
        // int(90.5 + 0.5) = 91 and fx(-25) = int(271.5 + 0.5) = 272, so the handle is
        // rows 91..272 of the 362px lane (181px tall).
        barScale.setValueWindow(25.0f, 75.0f);
        const Scaler::ScrollBars vMid = barScale.scrollBars(0.0, 100.0, 0.0f, 100.0f);
        CHECK(vMid.vThumb == QRect(
                  vMid.vTrack.left(), vMid.vTrack.top() + 91, vMid.vTrack.width(), 181
              ));
        CHECK(vMid.vHasRun());
        std::printf("[8] bars V: value window 25..75 of 0..100 -> vThumb y=+91 h=181 of 362 (hand: 91..272)\n");

        // A window at the content's LOW end sits at the BOTTOM of the inverted lane
        // (rows 181..362); at its HIGH end it sits at the TOP (rows 0..181) with the
        // same length - the one thing a value bar must not get backwards.
        barScale.setValueWindow(0.0f, 50.0f);
        const Scaler::ScrollBars vLow = barScale.scrollBars(0.0, 100.0, 0.0f, 100.0f);
        barScale.setValueWindow(50.0f, 100.0f);
        const Scaler::ScrollBars vHigh = barScale.scrollBars(0.0, 100.0, 0.0f, 100.0f);
        CHECK(vLow.vThumb.height() == vHigh.vThumb.height());
        CHECK(vLow.vThumb.top() - vLow.vTrack.top() == 181);
        CHECK(vHigh.vThumb.top() == vHigh.vTrack.top());
        CHECK(vHigh.vThumb.top() < vLow.vThumb.top());

        // A window that covers its content: BOTH lanes draw the FULL-LENGTH pill (the
        // run fills the track) and BOTH runs are still GRABBABLE - the timeline's own
        // full-width handle is (its beginScrollbarDrag ratio is range/span, positive
        // at range == pageStep). The drag ratio is the union track's, so the run can
        // move the window even though the pill cannot slide within its lane. The
        // TRACKS stay laid out either way, so the plot's rect does not move.
        barScale.setValueWindow(-10.0f, 110.0f);
        const Scaler::ScrollBars whole = barScale.scrollBars(0.0, 40.0, 0.0f, 100.0f);
        CHECK(whole.vThumb == whole.vTrack);
        CHECK(whole.vHasRun());
        CHECK(std::fabs(whole.vContentPerPixel - 120.0 / 362.0) < 1e-12);
        CHECK(!whole.vTrack.isEmpty());
        // A degenerate CONTENT (no key at all) is still a window-shaped track: the run
        // exists (the window is its own track) and is grabbable, exactly as a content
        // whose extent the window covers is.
        const Scaler::ScrollBars none = barScale.scrollBars(0.0, 40.0, 0.0f, 0.0f); // no extent
        CHECK(none.vThumb == none.vTrack);
        CHECK(none.vHasRun());
        CHECK(none.vContentPerPixel > 0.0);
    }
    // The section boundary itself: the band's 22 rows sit immediately above the
    // plot, and the plot's first row is the graph's - the exact arithmetic a pixel
    // capture must show (band.bottom() + 1 == plot.top()). The band is as wide as the
    // PANEL (the model publishes it that way: the shared mapping puts frames on the
    // timeline's columns), which is why this rect is not derived from the plot.
    const QRect sectionBand(
        scaler.rect().left(), Scaler::kMargin, scaler.rect().width(), Scaler::kRulerBandHeight
    );
    CHECK(sectionBand.bottom() + 1 == scaler.plotRect().top());
    CHECK(scaler.plotRect().top() == Scaler::kMargin + Scaler::kRulerBandHeight);
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
    // exactly and only the final pixel is rounded), and the rate steps by a
    // constant factor wherever it is called from (THE LADDER in time_Scaler).
    const int anchorX = plot.left() + 250;
    const double anchorFrame = scaler.frameAtPixel(anchorX);
    const double beforeZoom = scaler.frameScale().pixelsPerFrame();
    CHECK(scaler.updateFrameZoom(-120, anchorX)); // one notch in
    CHECK(std::fabs(scaler.frameScale().pixelsPerFrame() / beforeZoom - 1.1) < 1e-6);
    CHECK(std::fabs(scaler.frameAtPixel(anchorX) - anchorFrame) <= 1e-6);
    // Zooming is not bounded inside the ladder's range: many notches in keep scaling by the same
    // factor and the anchor still holds, and the same call the other way keeps going out.
    CHECK(scaler.updateFrameZoom(-100000, anchorX));
    const double zoomedIn = scaler.frameScale().pixelsPerFrame();
    CHECK(zoomedIn > beforeZoom * 10.0);
    CHECK(std::fabs(scaler.frameAtPixel(anchorX) - anchorFrame) <= 1e-6);
    CHECK(scaler.updateFrameZoom(100000, anchorX));
    CHECK(scaler.frameScale().pixelsPerFrame() < zoomedIn / 10.0);
    CHECK(std::fabs(scaler.frameAtPixel(anchorX) - anchorFrame) <= 1.0);
    // A fit puts the whole span in the plot and lands on a ladder rung (the rate it picks is one the
    // ladder can actually reach - indexForRate inverts it exactly).
    scaler.fitFrameAxis(core::Frame(0), core::Frame(40));
    const QRect fitPlot = scaler.plotRect();
    const double fitted = scaler.frameScale().pixelsPerFrame();
    CHECK(std::fabs(scaler.frameScale().pixelsPerFrame() -
                    std::pow(1.1, scaler.frameScale().index())) < 1e-9);
    CHECK(scaler.frameAtPixel(fitPlot.left()) <= 1.0);
    CHECK(scaler.frameAtPixel(fitPlot.right()) >= 39.0);
    (void)fitted;

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

    // THE SHARED WINDOW IS THE TIMELINE'S MAPPING, pixel for pixel. The gui pushes
    // TimeLineWidget::visibleFrameWindow - the frames at the timeline viewport's two
    // edges, fractional - and the panel covers the same columns, so
    //
    //   graphPixelX(frame) == panel.left() + (frame - lo) * panel.width() / (hi - lo)
    //
    // must hold for the WHOLE window, with panel.left() (not the plot's left, and no
    // margin) as the origin. The right-hand side is a synthetic timeline: it is the
    // expression the timeline's own view transform evaluates, written here from the
    // window the gui hands over rather than from the scaler. If the scaler ever mapped
    // across the plot or inset the window again, the first/last-frame assertions below
    // would move by the label column's width.
    {
        const QRect panel = scaler.rect();
        const double lo = 12.0;
        const double hi = 88.0;
        CHECK(scaler.applyFrameWindow(lo, hi));
        const double timelinePpf = static_cast<double>(panel.width()) / (hi - lo);
        const auto timelinePixelX = [&panel, lo, timelinePpf](double aFrame) {
            return static_cast<double>(panel.left()) + (aFrame - lo) * timelinePpf;
        };
        for (const double frame : { lo, lo + 0.5, (lo + hi) / 2.0, hi - 0.25, hi })
            CHECK(std::fabs(scaler.pixelXOfFrame(frame) - timelinePixelX(frame)) <= 1e-9);
        CHECK(std::fabs(scaler.framePpf() - timelinePpf) <= 1e-12);
        // Both window edges are the timeline's own columns: the panel's first and one
        // past its last. pixelX rounds, and these are whole pixels by construction.
        CHECK(scaler.pixelX(core::Frame(int(lo))) == panel.left());
        CHECK(scaler.pixelX(core::Frame(int(hi))) == panel.left() + panel.width());
        // ...and NOT the plot's: the value column and the bar reserve live inside the
        // panel's columns now, and they must not shift the mapping.
        CHECK(scaler.pixelX(core::Frame(int(lo))) != plot.left());
        // Leaving the shared axis only drops the pushed RATE: the graph's own axis
        // writes its own origin on its next fit, zoom or pan (clearFrameWindow's
        // contract), so the rate - not the stale origin - is what this pins.
        scaler.clearFrameWindow();
        CHECK(std::fabs(scaler.framePpf() - static_cast<double>(scaler.frameScale().pixelsPerFrame())) <= 1e-12);
    }
    return 0;
}
// ---------------------------------------------------------------------------
// 11. PER-SIDE HANDLES: the derived curve is the played curve.
//
// A Custom curve's two control points are derived by their OWN HandleMode (see
// util::Easing::HandleMode). Two things can go wrong and both are pinned here:
// the derivation reads the VALUES OF THE COMPONENT BEING EVALUATED (a Move x
// curve reads x, its y curve reads y), and the graph draws what playback plays.
// The oracle comparison is the one section 2 runs.
//
// The four-key channel below is the user's question made concrete: P - A - B -
// C. A's OUTGOING side and B's INCOMING side are Auto. The derivation is per
// KEY (the reference's central difference): A's slope uses P and B, B's uses A
// and C. Changing P moves A's outgoing handle and leaves B's incoming one
// untouched; changing C does the reverse. That is what "an untouched Auto side
// keeps deriving while the other is dragged" means at the curve level; the
// gesture itself is tests/graph_drag.cpp's section.
// ---------------------------------------------------------------------------
int sectionAutoEasing() {
    using core::TimeKeyType;
    using core::TimeKeyType_Rotate;
    using core::TimeKeyType_Scale;
    using core::TimeKeyType_Opa;

    // P -50@-10, A 0@0, B 100@10, C 40@20. A Custom+Auto pair on A and B; every
    // other key's sides stay stored.
    core::TestNode node;
    cmnd::Stack stack;
    {
        auto* kp = pushKey<core::RotateKey>(node, stack, TimeKeyType_Rotate, -10);
        auto* k0 = pushKey<core::RotateKey>(node, stack, TimeKeyType_Rotate, 0);
        auto* k1 = pushKey<core::RotateKey>(node, stack, TimeKeyType_Rotate, 10);
        auto* k2 = pushKey<core::RotateKey>(node, stack, TimeKeyType_Rotate, 20);
        kp->data().setRotate(-50.0f);
        k0->data().setRotate(0.0f);
        k1->data().setRotate(100.0f);
        k2->data().setRotate(40.0f);
        k0->data().easing(0).type = util::Easing::Type_Custom;
        k0->data().easing(0).handleOut = util::Easing::HandleMode_Auto;
        k1->data().easing(0).type = util::Easing::Type_Custom;
        k1->data().easing(0).handleOut = util::Easing::HandleMode_AutoClamped;
        k1->data().easing(0).handleIn = util::Easing::HandleMode_Auto;
    }

    const ctrl::graph::Curve rotate(*node.timeLine(), ctrl::graph::Channel(TimeKeyType_Rotate, 0));
    CHECK(rotate.segmentCount() == 3);

    // A curve with a derived side is NOT "fully authored": the panel must read
    // its pair through effectiveHandles, not the stored fields.
    CHECK(!rotate.isCustom(rotate.segment(1))); // A -> B: A's outgoing is Auto
    // ...AND THE KIND IS PER SIDE, NOT PER SEGMENT. This fixture derives BOTH of
    // A -> B's sides (A's outgoing is Auto AND B's incoming is Auto - the
    // acceptance property below moves P and C independently), and only B -> C's
    // OUTGOING one (B's AutoClamped); C's handleIn is untouched and stays the
    // default Aligned, so B -> C is the mixed pair. Anything publishing one dot
    // at a time must ask per side: the stored side is the document's pair and the
    // derived one is a ghost. isCustom's both-sides answer is the wrong question
    // there.
    {
        const ctrl::graph::Curve::Segment ab = rotate.segment(1);
        const ctrl::graph::Curve::Segment bc = rotate.segment(2);
        // The DOCUMENT's modes first (the fixture's own state), then the modes the
        // segment carries - a segment that read the wrong key would otherwise agree
        // with derivesHandle in being wrong.
        const util::Easing::Param& a = static_cast<const core::RotateKey*>(ab.from)->data().easing(0);
        const util::Easing::Param& b = static_cast<const core::RotateKey*>(ab.to)->data().easing(0);
        const util::Easing::Param& c = static_cast<const core::RotateKey*>(bc.to)->data().easing(0);
        CHECK(a.handleOut == util::Easing::HandleMode_Auto);
        CHECK(b.handleOut == util::Easing::HandleMode_AutoClamped);
        CHECK(b.handleIn == util::Easing::HandleMode_Auto);    // set by this fixture
        CHECK(c.handleIn == util::Easing::HandleMode_Aligned); // untouched: the default
        CHECK(ab.easing.handleOut == a.handleOut);
        CHECK(ab.inMode == b.handleIn);
        CHECK(bc.easing.handleOut == b.handleOut);
        CHECK(bc.inMode == c.handleIn);
        CHECK(ctrl::graph::Curve::derivesHandle(ab, 0)); // A's outgoing: Auto
        CHECK(ctrl::graph::Curve::derivesHandle(ab, 1)); // B's incoming: Auto
        // The mixed pair: one derived side, one stored one.
        CHECK(ctrl::graph::Curve::derivesHandle(bc, 0));  // B's outgoing: AutoClamped
        CHECK(!ctrl::graph::Curve::derivesHandle(bc, 1)); // C's incoming: Aligned
    }
    {
        const QVector<QPointF> ghosts = rotate.ghostHandles(*rotate.segment(1).from);
        // Both of A -> B's sides derive here, so the "ghost" pair is the whole pair.
        CHECK(ghosts.size() == 2);
    }
    // The mirror image: a stored outgoing side with a derived incoming one.
    {
        core::TestNode mirrored;
        cmnd::Stack mirroredStack;
        auto* a = pushKey<core::RotateKey>(mirrored, mirroredStack, TimeKeyType_Rotate, 0);
        auto* b = pushKey<core::RotateKey>(mirrored, mirroredStack, TimeKeyType_Rotate, 10);
        a->data().setRotate(0.0f);
        b->data().setRotate(90.0f);
        a->data().easing(0).type = util::Easing::Type_Custom;
        a->data().easing(0).handleOut = util::Easing::HandleMode_Free;
        b->data().easing(0).handleIn = util::Easing::HandleMode_Auto;

        const ctrl::graph::Curve line(*mirrored.timeLine(), ctrl::graph::Channel(TimeKeyType_Rotate, 0));
        const ctrl::graph::Curve::Segment seg = line.segment(0);
        CHECK(!line.isCustom(seg)); // one side derives, so not fully authored
        CHECK(!ctrl::graph::Curve::derivesHandle(seg, 0)); // stored
        CHECK(ctrl::graph::Curve::derivesHandle(seg, 1));  // derived
        QPointF pair[2];
        CHECK(line.effectiveHandles(seg, pair));
        // The stored side is exactly the stored pair; only the other one moved.
        CHECK(pair[0] == ctrl::graph::Curve::handle(seg, 0));
    }

    // The graph equals playback over the whole span, key frames included.
    for (int frame = -10; frame <= 20; ++frame) {
        const BlenderOracle oracle(node, core::Frame(frame));
        CHECK(matchesPlayback(oracle, rotate, core::Frame(frame)));
    }

    const ctrl::graph::Curve::Segment ab = rotate.segment(1); // A -> B
    const ctrl::graph::Curve::Segment bc = rotate.segment(2); // B -> C

    // Hand-worked control points of A -> B (values 0 -> 100, span 10):
    //   outgoing at A: central difference over P(-10,-50) and B(10,100):
    //     px = -1, py = -0.5 -> m = (1 - (-0.5)) / (1 - (-1)) = 0.75
    //     -> y1 = 0.75/3 = 0.25.
    //   incoming at B: central difference over A(0,0) and C(20,40):
    //     nx = 2, ny = 0.4 -> m = (0.4 - 0) / (2 - 0) = 0.2
    //     -> y2 = 1 - 0.2/3 = 14/15.
    // (Auto is unclamped: A's 0.25 handle is the visible anticipation, and the
    // incoming side approaches B from above the segment.)
    {
        QPointF pair[2];
        CHECK(rotate.effectiveHandles(ab, pair));
        CHECK(nearly(static_cast<float>(pair[0].x()), 1.0f / 3.0f, 1e-6f));
        CHECK(nearly(static_cast<float>(pair[0].y()), 0.25f, 1e-5f));
        CHECK(nearly(static_cast<float>(pair[1].x()), 2.0f / 3.0f, 1e-6f));
        CHECK(nearly(static_cast<float>(pair[1].y()), 14.0f / 15.0f, 1e-5f));
        // A context-free evaluation of a Custom param with no derived side reads
        // the stored pair; the derived form reads these controls.
        const float atHalf = rotate.sample(core::Frame(5));
        CHECK(!nearly(atHalf, 50.0f, 0.01f)); // the four-key channel is shaped
    }

    // B -> C: B's outgoing is AutoClamped. Its central difference, over A and C
    // in this segment's unit (A = (-1, 5/3)), is
    // m = (1 - 5/3) / (1 - (-1)) = -1/3 -> y1 = -1/9, clamped into [0, 5/3] -> 0
    // (B is the local maximum, so the curve leaves it flat).
    // C's incoming side is stored (Aligned), so x2/y2 are the stored pair.
    {
        QPointF pair[2];
        CHECK(rotate.effectiveHandles(bc, pair));
        CHECK(nearly(static_cast<float>(pair[0].y()), 0.0f, 1e-6f));
        CHECK(nearly(static_cast<float>(pair[1].x()), static_cast<float>(bc.easing.cubicBezier.x2), 1e-6f));
        CHECK(nearly(static_cast<float>(pair[1].y()), static_cast<float>(bc.easing.cubicBezier.y2), 1e-6f));
    }

    // THE ACCEPTANCE PROPERTY. Change the PREVIOUS key's value (P): A's outgoing
    // derived handle moves, B's incoming derived handle does not (A's central
    // difference uses P, B's uses C). Then change the NEXT key's value (C): the
    // reverse.
    {
        const auto controlsOf = [&]() -> QPair<QPointF, QPointF> {
            QPointF pair[2];
            const ctrl::graph::Curve fresh(*node.timeLine(), ctrl::graph::Channel(TimeKeyType_Rotate, 0));
            fresh.effectiveHandles(fresh.segment(1), pair); // A -> B
            return QPair<QPointF, QPointF>(pair[0], pair[1]);
        };
        {
            QPointF probe[2];
            CHECK(rotate.effectiveHandles(ab, probe));
        }
        const auto before = controlsOf();

        auto* kp = static_cast<core::RotateKey*>(node.timeLine()->timeKey(TimeKeyType_Rotate, -10));
        CHECK(kp);
        kp->setRotate(-80.0f); // P changed: py = -0.8 -> slope 0.8 -> y1 = 0.2667
        const auto afterPrev = controlsOf();
        CHECK(afterPrev.first != before.first); // A's outgoing is STILL DERIVING
        CHECK(afterPrev.second == before.second); // ... and B's incoming did not move

        auto* k2 = static_cast<core::RotateKey*>(node.timeLine()->timeKey(TimeKeyType_Rotate, 20));
        CHECK(k2);
        k2->setRotate(10.0f); // C changed: ny = 0.1 -> slope -0.9 -> y2 = 1.3
        const auto afterNext = controlsOf();
        CHECK(afterNext.second != afterPrev.second); // B's incoming is STILL DERIVING
        CHECK(afterNext.first == afterPrev.first); // ... and A's outgoing did not move
    }

    // "DRAGGED": free A's outgoing side (what a drag writes) and CHANGE P again.
    // The dragged side now reads its stored value and stays put; B's incoming
    // side is still Auto and still follows C. This is the defect the retired
    // whole-key Auto type had: it converted BOTH tangents to manual.
    {
        auto* k0 = static_cast<core::RotateKey*>(node.timeLine()->timeKey(TimeKeyType_Rotate, 0));
        CHECK(k0);
        k0->data().easing(0).handleOut = util::Easing::HandleMode_Free;
        k0->data().easing(0).cubicBezier.x1 = 0.4f;
        k0->data().easing(0).cubicBezier.y1 = 0.25f;

        const ctrl::graph::Curve freed(*node.timeLine(), ctrl::graph::Channel(TimeKeyType_Rotate, 0));
        QPointF pair[2];
        CHECK(freed.effectiveHandles(freed.segment(1), pair));
        CHECK(pair[0] == QPointF(0.4f, 0.25f)); // the stored (dragged) value is the truth

        const QPointF incomingBefore = pair[1];
        auto* k2 = static_cast<core::RotateKey*>(node.timeLine()->timeKey(TimeKeyType_Rotate, 20));
        k2->setRotate(80.0f);
        const ctrl::graph::Curve after(*node.timeLine(), ctrl::graph::Channel(TimeKeyType_Rotate, 0));
        CHECK(after.effectiveHandles(after.segment(1), pair));
        CHECK(pair[0] == QPointF(0.4f, 0.25f)); // the freed side does not move
        CHECK(pair[1] != incomingBefore); // the OTHER side is still deriving
    }

    // Aligned and Free both read the stored pair; Vector is the chord (a straight
    // segment); AutoClamped flattens an extremum where Auto does not.
    {
        core::TestNode plain;
        cmnd::Stack plainStack;
        auto* a = pushKey<core::RotateKey>(plain, plainStack, TimeKeyType_Rotate, 0);
        auto* b = pushKey<core::RotateKey>(plain, plainStack, TimeKeyType_Rotate, 10);
        a->data().setRotate(0.0f);
        b->data().setRotate(90.0f);
        a->data().easing(0).type = util::Easing::Type_Custom;
        a->data().easing(0).handleOut = util::Easing::HandleMode_Free;
        a->data().easing(0).cubicBezier = util::Easing::CubicBezier{0.4f, 0.1f, 0.8f, 0.9f};
        b->data().easing(0).handleIn = util::Easing::HandleMode_Aligned;

        const ctrl::graph::Curve stored(*plain.timeLine(), ctrl::graph::Channel(TimeKeyType_Rotate, 0));
        QPointF pair[2];
        CHECK(stored.isCustom(stored.segment(0)));
        CHECK(stored.effectiveHandles(stored.segment(0), pair));
        CHECK(pair[0] == QPointF(0.4f, 0.1f));
        CHECK(pair[1] == QPointF(0.8f, 0.9f));
        CHECK(stored.ghostHandles(*stored.segment(0).from).isEmpty()); // both stored: nothing derived

        b->data().easing(0).handleIn = util::Easing::HandleMode_Vector;
        a->data().easing(0).handleOut = util::Easing::HandleMode_Vector;
        const ctrl::graph::Curve vec(*plain.timeLine(), ctrl::graph::Channel(TimeKeyType_Rotate, 0));
        for (int frame = 0; frame <= 10; ++frame) {
            const float t = float(frame) / 10.0f;
            CHECK(nearly(vec.sample(core::Frame(frame)), 90.0f * t, 0.01f));
        }
    }

    // The value basis is the COMPONENT'S OWN values, with the mode on each key of
    // that component's param.
    {
        core::TestNode scaleNode;
        cmnd::Stack scaleStack;
        auto* s0 = pushKey<core::ScaleKey>(scaleNode, scaleStack, TimeKeyType_Scale, 0);
        auto* s1 = pushKey<core::ScaleKey>(scaleNode, scaleStack, TimeKeyType_Scale, 10);
        auto* s2 = pushKey<core::ScaleKey>(scaleNode, scaleStack, TimeKeyType_Scale, 20);
        s0->data().setScale(QVector2D(1.0f, 1.0f));
        s1->data().setScale(QVector2D(1.0f, 3.0f));
        s2->data().setScale(QVector2D(1.0f, 2.0f));
        for (int component = 0; component < core::ScaleKey::kComponents; ++component) {
            s0->data().easing(component).type = util::Easing::Type_Custom;
            s0->data().easing(component).handleOut = util::Easing::HandleMode_Auto;
            s1->data().easing(component).type = util::Easing::Type_Custom;
            s1->data().easing(component).handleOut = util::Easing::HandleMode_AutoClamped;
            s1->data().easing(component).handleIn = util::Easing::HandleMode_Auto;
        }

        const ctrl::graph::Curve scaleY(*scaleNode.timeLine(), ctrl::graph::Channel(TimeKeyType_Scale, 1));
        const ctrl::graph::Curve scaleX(*scaleNode.timeLine(), ctrl::graph::Channel(TimeKeyType_Scale, 0));

        for (int frame = 0; frame <= 20; ++frame) {
            const BlenderOracle oracle(scaleNode, core::Frame(frame));
            CHECK(matchesPlayback(oracle, scaleY, core::Frame(frame)));
            CHECK(matchesPlayback(oracle, scaleX, core::Frame(frame)));
        }
        // The still component stays exactly still: a derived handle never invents
        // a change of its own.
        CHECK(nearly(scaleX.sample(core::Frame(15)), 1.0f));
    }

    // A two-key channel has no neighbours at all, so both Auto sides fall back to
    // the identity thirds and the segment is Linear.
    {
        core::TestNode solo;
        cmnd::Stack soloStack;
        auto* a = pushKey<core::OpaKey>(solo, soloStack, TimeKeyType_Opa, 0);
        auto* b = pushKey<core::OpaKey>(solo, soloStack, TimeKeyType_Opa, 8);
        a->data().setOpacity(0.2f);
        b->data().setOpacity(1.0f);
        a->data().easing(0).type = util::Easing::Type_Custom;
        a->data().easing(0).handleOut = util::Easing::HandleMode_Auto;
        b->data().easing(0).handleIn = util::Easing::HandleMode_Auto;
        const ctrl::graph::Curve line(*solo.timeLine(), ctrl::graph::Channel(TimeKeyType_Opa, 0));
        for (int frame = 0; frame <= 8; ++frame) {
            const float t = float(frame) / 8.0f;
            CHECK(nearly(line.sample(core::Frame(frame)), 0.2f + 0.8f * t));
        }
        const QVector<QPointF> soloGhosts = line.ghostHandles(*line.segment(0).from);
        CHECK(soloGhosts.size() == 2);
        CHECK(nearly(static_cast<float>(soloGhosts[0].x()), 1.0f / 3.0f, 1e-6f));
        CHECK(nearly(static_cast<float>(soloGhosts[0].y()), 1.0f / 3.0f, 1e-6f));
        CHECK(nearly(static_cast<float>(soloGhosts[1].x()), 2.0f / 3.0f, 1e-6f));
        CHECK(nearly(static_cast<float>(soloGhosts[1].y()), 2.0f / 3.0f, 1e-6f));
    }

    return 0;
}

// ---- 11 THE EASING PARAM'S ON-DISK FORM ----
//
// A key's HANDLE MODES (util::Easing::Param::handleOut/handleIn) are document
// state and travel with the param. The one write and the one read are
// core::Serializer::write(Param) / core::Deserializer::read(Param), so this
// section drives exactly those:
//
//   * a round trip at the CURRENT version (0.15) keeps both modes AND every
//     other field bit-identical;
//   * a 0.14 file carried ONE link flag where 0.15 writes two modes: the same
//     bytes with the trailing handleIn word removed. Reading it at 0.14 maps
//     the flag to BOTH sides (Aligned or Free), which is what it meant;
//   * a 0.12 file had no flag at all (both trailing words removed) and loads
//     FREE;
//   * THE RETIRED TYPE: id 14 was Type_Auto, which had no stored handles and
//     always clamped them. A 0.14 param of that type must load as Type_Custom
//     with BOTH sides AutoClamped (its exact shape, in the new vocabulary), and
//     the same id at 0.15 is invalid - the slot is not reused.
class ParamSerializationReporter : public util::IProgressReporter {
public:
    void setSection(const QString&) override {}
    void setMaximum(int) override {}
    void setProgress(int) override {}
    bool wasCanceled() const override { return false; }
};

bool readParam(const std::string& aBytes, const QVersionNumber& aVersion, util::Easing::Param& aOut) {
    std::istringstream stream(aBytes, std::ios::in | std::ios::binary);
    util::LEStreamReader reader(stream);
    core::Deserializer::IDSolverType solver;
    gl::DeviceInfo device;
    device.vender = "test";
    device.renderer = "test";
    device.version = "4";
    device.maxTextureSize = 16384;
    device.maxRenderBufferSize = 16384;
    ParamSerializationReporter reporter;
    core::Deserializer deserializer(
        reader, solver, aBytes.size(), aVersion, device, reporter, 0
    );
    return deserializer.read(aOut, false) && deserializer.checkStream();
}

int sectionEasingParamSerialization() {
    const auto param = [](util::Easing::HandleMode aOut, util::Easing::HandleMode aIn) {
        util::Easing::Param p;
        p.type = util::Easing::Type_Custom;
        p.range = util::Easing::Range_In;
        p.weight = 0.5f;
        p.cubicBezier.x1 = 0.4f;
        p.cubicBezier.y1 = 0.1f;
        p.cubicBezier.x2 = 0.8f;
        p.cubicBezier.y2 = 0.9f;
        p.handleOut = aOut;
        p.handleIn = aIn;
        return p;
    };
    const auto writeParam = [](const util::Easing::Param& aParam) {
        std::ostringstream stream(std::ios::out | std::ios::binary);
        util::StreamWriter writer(stream);
        core::Serializer serializer(writer);
        serializer.write(aParam); // an ostringstream cannot fail
        return stream.str();
    };

    // The fields are APPENDED: dropping four bytes gives the 0.14 layout (one
    // flag word), eight the 0.12 layout (none).
    constexpr std::size_t kModeBytes = sizeof(uint32);
    constexpr std::size_t kTwoModeBytes = 2 * sizeof(uint32);

    // (1) ROUND TRIP AT 0.15, every mode pair.
    const util::Easing::HandleMode pairs[][2] = {
        { util::Easing::HandleMode_Free, util::Easing::HandleMode_Aligned },
        { util::Easing::HandleMode_Auto, util::Easing::HandleMode_AutoClamped },
        { util::Easing::HandleMode_Vector, util::Easing::HandleMode_Free },
    };
    for (const auto& pair : pairs) {
        const util::Easing::Param written = param(pair[0], pair[1]);
        const std::string bytes = writeParam(written);
        util::Easing::Param readBack;
        readBack.handleOut = util::Easing::HandleMode_Auto; // stale: must be overwritten
        readBack.handleIn = util::Easing::HandleMode_Auto;
        const bool okRead = readParam(bytes, QVersionNumber(0, 15), readBack);
        std::printf("[11] param round trip out=%d in=%d: bytes=%zu read=%d equal=%d\n",
                    int(pair[0]), int(pair[1]), bytes.size(), int(okRead), int(readBack == written));
        CHECK(okRead);
        CHECK(readBack == written);
        CHECK(readBack.handleOut == pair[0]);
        CHECK(readBack.handleIn == pair[1]);
        CHECK(readBack.cubicBezier == written.cubicBezier);
    }

    // (2) 0.14 ONE FLAG -> BOTH SIDES. The remaining word is the old `aligned`
    // bool; a linked key becomes Aligned/Aligned, an unlinked one Free/Free.
    for (const bool linked : { true, false }) {
        const util::Easing::Param written = param(
            linked ? util::Easing::HandleMode_Aligned : util::Easing::HandleMode_Free,
            util::Easing::HandleMode_Vector); // the second word is the 0.15 addition
        const std::string current = writeParam(written);
        CHECK(current.size() > kModeBytes);
        const std::string oldFile = current.substr(0, current.size() - kModeBytes);

        util::Easing::Param oldRead;
        oldRead.handleOut = util::Easing::HandleMode_Auto; // must be overwritten
        oldRead.handleIn = util::Easing::HandleMode_Auto;
        CHECK(readParam(oldFile, QVersionNumber(0, 14), oldRead));
        CHECK(oldRead.handleOut == (linked ? util::Easing::HandleMode_Aligned : util::Easing::HandleMode_Free));
        CHECK(oldRead.handleIn == oldRead.handleOut);
        CHECK(oldRead.type == written.type);
        CHECK(oldRead.range == written.range);
        CHECK(oldRead.weight == written.weight);
        CHECK(oldRead.cubicBezier == written.cubicBezier);

        // The other direction of the gate: the SAME old bytes read at 0.15 are a
        // TRUNCATED param (the handleIn word is missing) and the read fails.
        util::Easing::Param truncated;
        CHECK(!readParam(oldFile, QVersionNumber(0, 15), truncated));
    }

    // (3) 0.12: no flag word at all -> both sides FREE.
    {
        const util::Easing::Param written = param(util::Easing::HandleMode_Aligned,
                                                  util::Easing::HandleMode_Aligned);
        const std::string current = writeParam(written);
        CHECK(current.size() > kTwoModeBytes);
        const std::string oldFile = current.substr(0, current.size() - kTwoModeBytes);
        util::Easing::Param oldRead;
        oldRead.handleOut = util::Easing::HandleMode_Auto;
        oldRead.handleIn = util::Easing::HandleMode_Auto;
        CHECK(readParam(oldFile, QVersionNumber(0, 12), oldRead));
        CHECK(oldRead.handleOut == util::Easing::HandleMode_Free);
        CHECK(oldRead.handleIn == util::Easing::HandleMode_Free);
        CHECK(oldRead.cubicBezier == written.cubicBezier);
    }

    // (4) THE RETIRED TYPE_AUTO ID. A 0.14 param whose type word is 14 loads as
    // Type_Custom with BOTH sides on the automatic CLAMPED mode: the old type
    // clamped unconditionally, so this is the lossless reading (unclamped Auto
    // would overshoot where the old curve did not).
    {
        util::Easing::Param legacy;
        legacy.type = static_cast<util::Easing::Type>(14); // Type_Auto's retired id
        legacy.range = util::Easing::Range_InOut;
        legacy.weight = 1.0f;
        const std::string current = writeParam(legacy);
        const std::string oldFile = current.substr(0, current.size() - kModeBytes);

        util::Easing::Param oldRead;
        CHECK(readParam(oldFile, QVersionNumber(0, 14), oldRead));
        CHECK(oldRead.type == util::Easing::Type_Custom);
        CHECK(oldRead.handleOut == util::Easing::kDefaultAutomaticHandleMode);
        CHECK(oldRead.handleIn == util::Easing::kDefaultAutomaticHandleMode);
        CHECK(oldRead.handleOut == util::Easing::HandleMode_AutoClamped);

        // ... and the id is NOT reused at 0.15: the same bytes (with both mode
        // words) are rejected rather than silently accepted as some new type.
        util::Easing::Param rejected;
        CHECK(!readParam(current, QVersionNumber(0, 15), rejected));
    }

    std::printf(
        "[S] easing param: both handle modes round-trip at 0.15; a 0.14 flag maps to both sides; "
        "a 0.12 param loads Free/Free; the retired type 14 loads at 0.14 as Custom with BOTH sides "
        "Auto Clamped and is invalid at 0.15\n"
    );
    return 0;
}

// ---- 12 PER-COMPONENT EASING: STORAGE, PLAYBACK AND THE FORMAT GATE ----
//
// A Move key now stores one util::Easing::Param per component. This section
// proves the three things that can go wrong independently:
//
//   * the two components are INDEPENDENT for playback - editing x's curve alone
//     leaves y's values on their own curve (core::TimeKeyBlender reads each),
//   * a 0.14 file round-trips two DIFFERENT components (the writer emits the
//     whole array),
//   * a 0.13 file wrote ONE param, and the reader BROADCASTS it to every
//     component - lossless, and the following fields still land where they
//     belong (which is what a wrong read would break).
//
// The old-format bytes are written by hand here: one param, then the MoveKey's
// own spline/pos/centroid. That is exactly the layout every key had at 0.13.
bool readMoveKey(const std::string& aBytes, const QVersionNumber& aVersion, core::MoveKey& aOut) {
    std::istringstream stream(aBytes, std::ios::in | std::ios::binary);
    util::LEStreamReader reader(stream);
    core::Deserializer::IDSolverType solver;
    gl::DeviceInfo device;
    device.vender = "test";
    device.renderer = "test";
    device.version = "4";
    device.maxTextureSize = 16384;
    device.maxRenderBufferSize = 16384;
    ParamSerializationReporter reporter;
    core::Deserializer deserializer(reader, solver, aBytes.size(), aVersion, device, reporter, 0);
    return aOut.deserialize(deserializer) && deserializer.checkStream();
}

int sectionPerComponentEasing() {
    using core::TimeKeyType;
    using ctrl::graph::Channel;

    util::Easing::Param xCurve;
    xCurve.type = util::Easing::Type_Quad;
    xCurve.range = util::Easing::Range_In;
    util::Easing::Param yCurve;
    yCurve.type = util::Easing::Type_Linear;
    yCurve.range = util::Easing::Range_InOut;
    CHECK(xCurve != yCurve);

    // (1) INDEPENDENT PLAYBACK. Two Move keys along the diagonal (a linear
    // spline path, so a rate maps straight to a coordinate), x on Quad/In and y
    // on Linear: at the midpoint x must be eased to 0.25 and y must stay 0.5.
    {
        core::TestNode node;
        cmnd::Stack stack;
        auto* k0 = pushKey<core::MoveKey>(node, stack, core::TimeKeyType_Move, 0);
        auto* k1 = pushKey<core::MoveKey>(node, stack, core::TimeKeyType_Move, 10);
        k0->setPos(QVector2D(0.0f, 0.0f));
        k1->setPos(QVector2D(100.0f, 100.0f));
        // Both components start on the same curve, then X ALONE is edited
        // through the graph's own write bridge (Curve::easingOf names the
        // component, so it cannot reach Y).
        k0->data().easing(0) = yCurve;
        k0->data().easing(1) = yCurve;
        util::Easing::Param* liveX = ctrl::graph::Curve::easingOf(*k0, core::TimeKeyType_Move, 0);
        CHECK(liveX == &k0->data().easing(0));
        *liveX = xCurve;
        CHECK(k0->data().easing(0) == xCurve);
        CHECK(k0->data().easing(1) == yCurve); // untouched by the X edit
        CHECK(k0->data().easing(0) != k0->data().easing(1));

        const BlenderOracle oracle(node, core::Frame(5));
        const float x = oracle.value(Channel(core::TimeKeyType_Move, 0));
        const float y = oracle.value(Channel(core::TimeKeyType_Move, 1));
        std::printf("[12] playback at frame 5: Move x %.4f (Quad/In), Move y %.4f (Linear)\n", x, y);
        CHECK(nearly(x, 25.0f, 0.01f));
        CHECK(nearly(y, 50.0f, 0.01f));
        CHECK(!nearly(x, y, 1.0f));

        // The graph draws the same split (its sampler reads the channel's own
        // component), so the two views cannot disagree about what plays.
        const ctrl::graph::Curve curveX(*node.timeLine(), Channel(core::TimeKeyType_Move, 0));
        const ctrl::graph::Curve curveY(*node.timeLine(), Channel(core::TimeKeyType_Move, 1));
        CHECK(nearly(curveX.sample(core::Frame(5)), x, 0.01f));
        CHECK(nearly(curveY.sample(core::Frame(5)), y, 0.01f));
        CHECK(curveX.segment(0).easing == xCurve);
        CHECK(curveY.segment(0).easing == yCurve);
    }

    // (2) CURRENT-FORMAT ROUND TRIP: two different components survive a write and a read.
    {
        core::MoveKey written;
        written.data().easing(0) = xCurve;
        written.data().easing(1) = yCurve;
        written.data().setSpline(core::MoveKey::SplineType_Linear);
        written.setPos(QVector2D(3.0f, -4.5f));
        written.setCentroid(QVector2D(1.5f, 2.5f));

        std::ostringstream stream(std::ios::out | std::ios::binary);
        util::StreamWriter writer(stream);
        core::Serializer serializer(writer);
        CHECK(written.serialize(serializer));
        const std::string bytes = stream.str();

        core::MoveKey readBack;
        CHECK(readMoveKey(bytes, QVersionNumber(0, AE_PROJECT_FORMAT_MINOR_VERSION), readBack));
        CHECK(readBack.data().easing(0) == xCurve);
        CHECK(readBack.data().easing(1) == yCurve);
        CHECK(readBack.data().easing(0) != readBack.data().easing(1));
        CHECK(readBack.pos() == written.pos());
        CHECK(readBack.centroid() == written.centroid());
        CHECK(readBack.data().spline() == written.data().spline());
        std::printf("[12] Move round trip at the current format: components differ and %zu bytes survive\n", bytes.size());
    }

    // (3) 0.13 GATE: ONE param on disk, broadcast to both components, and the
    // fields after it still read correctly.
    {
        std::ostringstream stream(std::ios::out | std::ios::binary);
        util::StreamWriter writer(stream);
        core::Serializer serializer(writer);
        // The single param a key wrote before 0.14, in its 0.13 ON-DISK form:
        // type (i32), range (i32), weight (f32), bezier x1/y1/x2/y2 (4 x f32)
        // and ONE handle-link flag (u32) - 32 bytes, followed by the key's own
        // spline (i32), pos (2 x f32) and centroid (2 x f32). It must be emitted
        // field by field, not with Serializer::write(Param): that writes the
        // 0.15 form (the same 28 bytes then TWO u32 handle modes, 8 extra
        // bytes), which shifts every following field by four bytes and makes
        // the fixture a file no 0.13 writer ever produced.
        serializer.write(static_cast<int>(xCurve.type));
        serializer.write(static_cast<int>(xCurve.range));
        serializer.write(xCurve.weight);
        serializer.write(xCurve.cubicBezier.x1);
        serializer.write(xCurve.cubicBezier.y1);
        serializer.write(xCurve.cubicBezier.x2);
        serializer.write(xCurve.cubicBezier.y2);
        serializer.write(true); // 0.13's single link flag: linked == Aligned
        serializer.write(static_cast<int>(core::MoveKey::SplineType_CatmullRom));
        serializer.write(QVector2D(7.0f, -8.0f));
        serializer.write(QVector2D(0.25f, 0.75f));
        const std::string oldBytes = stream.str();
        // 32-byte param + 4-byte spline + two 8-byte vectors: the exact size of
        // a 0.13 Move key. A serializer change that grows the param would make
        // this fixture stop being a 0.13 file, so the size is part of the gate.
        CHECK(oldBytes.size() == 52);

        core::MoveKey oldKey;
        oldKey.data().easing(1) = yCurve; // a stale in-memory value the gate must overwrite
        CHECK(readMoveKey(oldBytes, QVersionNumber(0, 13), oldKey));
        CHECK(oldKey.data().easing(0) == xCurve);
        CHECK(oldKey.data().easing(1) == xCurve); // broadcast, not left stale
        CHECK(oldKey.data().easing(0) == oldKey.data().easing(1));
        CHECK(oldKey.data().spline() == core::MoveKey::SplineType_CatmullRom);
        CHECK(oldKey.pos() == QVector2D(7.0f, -8.0f));
        CHECK(oldKey.centroid() == QVector2D(0.25f, 0.75f));

        // The other direction of the gate: the same one-param bytes read at 0.14
        // consume the spline word as a SECOND param, so the key is not silently
        // accepted as if the layouts were interchangeable.
        core::MoveKey mismatched;
        CHECK(!readMoveKey(oldBytes, QVersionNumber(0, AE_PROJECT_FORMAT_MINOR_VERSION), mismatched));
        std::printf("[12] 0.13 key: one param broadcast to both components; old bytes rejected at 0.14\n");
    }

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
    if (sectionPolylineHoldOutsideKeys())
        return 1;
    if (sectionScaler())
        return 1;
    if (sectionAutoEasing())
        return 1;
    if (sectionEasingParamSerialization())
        return 1;
    if (sectionPerComponentEasing())
        return 1;


    std::printf("graph_curve: all checks passed\n");
    return 0;
}
