// Contract test for the ONE property of the animation graph that cannot be
// observed headless: the image-offset channel's step.
//
// tests/graph_curve.cpp pins the whole graph value model (Channel / Curve /
// Scaler) but has to SKIP the image staircase, because constructing a
// core::ImageKey needs a live GL context: ImageKey::Data holds a core::GridMesh
// whose LayerMesh::MeshBuffer::GLBinder calls gl::Global::makeCurrent() and whose
// gl::BufferObject constructor calls glGenBuffers on the global functions table,
// both of which are null under QT_QPA_PLATFORM=offscreen. With no ImageKey
// constructible there is no key to place on a timeline, so graph_curve has no way
// to prove that the graph draws the offset as steps.
//
// THIS FILE IS THAT PLACE. It stands up the same in-tree GL context the real GUI
// uses (QOpenGLWidget + gl::Global functions, exactly as tests/roundtrip_main.cpp
// and tests/pose_pull_crash.cpp do), then pushes two real core::ImageKeys through
// the production path (TimeLine::createPusher) and asserts the staircase:
//
//   * graph::Channel::isInterpolated() is false for both offset components, so
//     the sampler must NOT blend, even though the first key carries a Custom
//     bezier that would visibly bend any interpolation;
//   * graph::Curve::sample() equals EXACTLY the first key's value at every
//     interior frame and the second key's value at and past the second frame --
//     the exactness is the leak test: an implementation that eased between the
//     keys would leave the first key's value at any interior frame;
//   * graph::Curve::buildPolyline() emits the step shape, holding v0 across the
//     whole span and jumping to v1 at the arrival frame, where sample() reports
//     the second key;
//   * graph::Curve::segment(0).easing is still the authored Custom param, so it
//     is the STEP that is drawn, not the curve that param would describe.
//
// Headless: QApplication + a QOpenGLWidget for the context (runs under
// xvfb-run), no .anie fixture, no window-manager dependence. Exit 0 all checked,
// 1 a FAIL line, 2 no GL context could be created (an environment gap).
#include <QApplication>
#include <QOpenGLWidget>
#include <QOpenGLVersionFunctionsFactory>
#include <QOpenGLFunctions_4_0_Core>
#include <QSurfaceFormat>
#include <QPointF>
#include <QString>
#include <QVector2D>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#include "XCAssert.h"
#include "cmnd/Stack.h"
#include "gl/Global.h"
#include "gl/DeviceInfo.h"
#include "core/Animator.h"
#include "core/Frame.h"
#include "core/ImageKey.h"
#include "core/ObjectNode.h"
#include "core/Project.h"
#include "core/TimeKeyType.h"
#include "core/TimeLine.h"
#include "ctrl/graph/graph_Channel.h"
#include "ctrl/graph/graph_Curve.h"
#include "util/Easing.h"

// The same three GUI stubs graph_curve / creation_select_keys use: core's
// Project.cpp references AudioPlaybackWidget::deserialize (only for the .aemus
// sidecar this test never has), and ctrl references two ObjectTreeWidget symbols
// that only matter on paths this test does not drive. Stubbing them keeps the
// link to core/ctrl/gl enough -- no gui library.
#include "gui/AudioPlaybackWidget.h"
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

namespace {

class NullAnimator: public core::Animator {
public:
    core::Frame currentFrame() const override { return core::Frame(); }
    void stop() override {}
    void suspend() override {}
    void resume() override {}
    bool isSuspended() const override { return false; }
};

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

#define CHECK_MSG(cond, ...)                                                                           \
    do {                                                                                               \
        if (!(cond)) {                                                                                 \
            std::printf("FAIL %d: %s (", __LINE__, #cond);                                             \
            std::printf(__VA_ARGS__);                                                                  \
            std::printf(")\n");                                                                        \
            return 1;                                                                                  \
        }                                                                                              \
    } while (false)

// The linear value the drawn polyline holds at aFrame: the point is bracketed by
// the two consecutive emitted points, and a vertical run (the riser) reports its
// upper end. This is what a renderer would map onto the pixel at aFrame, so it is
// the drawn value -- not a second evaluation of the sampler.
double polylineValueAt(const QVector<QPointF>& aPoly, double aFrame) {
    if (aPoly.isEmpty())
        return 0.0;
    if (aFrame <= aPoly.first().x())
        return aPoly.first().y();
    for (int i = 0; i + 1 < aPoly.size(); ++i) {
        const QPointF& a = aPoly.at(i);
        const QPointF& b = aPoly.at(i + 1);
        if (a.x() <= aFrame && aFrame <= b.x()) {
            if (b.x() == a.x())
                return b.y();
            const double t = (aFrame - a.x()) / (b.x() - a.x());
            return a.y() + (b.y() - a.y()) * t;
        }
    }
    return aPoly.last().y();
}

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

namespace core {
// Minimal ObjectNode subclass owning a real TimeLine (ObjectNode::timeLine() is
// virtual and null by default). No meshes, no resources -- the image keys ride
// the timeline's own key maps. Same as graph_curve's TestNode.
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

static gl::DeviceInfo makeDeviceInfo() {
    gl::DeviceInfo dev;
    dev.vender = "harness";
    dev.renderer = "harness";
    dev.version = "5";
    dev.maxTextureSize = 16384;
    dev.maxRenderBufferSize = 16384;
    return dev;
}

// ---------------------------------------------------------------------------
// The staircase. Every assertion that follows needs a live GL context, because
// the two keys it rides on are constructed here and ImageKey construction binds
// GL (see the file header). A run that cannot make the context never reaches
// this function.
// ---------------------------------------------------------------------------
int sectionImageOffsetStaircase() {
    using core::TimeKeyType;
    using ctrl::graph::Channel;
    using ctrl::graph::Curve;

    NullAnimator animator;
    core::Project project(QStringLiteral("graph_image_step.anie"), animator, nullptr);
    cmnd::Stack stack;

    core::TestNode node;

    // Frame 0: offset (3, 6), and a Custom easing whose bezier is emphatically
    // non-linear so a leak is caught, not tolerated. The control points are
    // (x1,y1) = (0.9, 0.1) and (x2,y2) = (0.1, 0.9): the curve dips well below
    // the chord then rises well above it (at t=0.25 it is y=0.184 where the chord
    // is 0.409). The tempting (0.9,0.9)+(0.1,0.1) pair is NOT usable as a leak
    // probe -- with x1==y1 and x2==y2 the bezier is exactly y=x, i.e. an identity
    // rate, which a plain lerp would match. This pair does not.
    auto* k0 = new core::ImageKey();
    k0->setImageOffset(QVector2D(3.0f, 6.0f));
    util::Easing::Param& easing0 = k0->data().easing(0);
    easing0.type = util::Easing::Type_Custom;
    easing0.cubicBezier.x1 = 0.9f;
    easing0.cubicBezier.y1 = 0.1f;
    easing0.cubicBezier.x2 = 0.1f;
    easing0.cubicBezier.y2 = 0.9f;
    // BOTH components carry the authored param: the easing is per channel now, and
    // each channel's segment reads its OWN component's param (core::KeyEasing), so a
    // fixture that authored only component 0 would leave component 1's curve on the
    // default and its isCustom() false.
    k0->data().easing(1) = easing0;

    // Frame 24: offset (13, 26). Distinguished from the first on BOTH components
    // and both non-zero, so a sampler that fell back to the default (0,0) or read
    // the wrong component could not pass by symmetry.
    auto* k1 = new core::ImageKey();
    k1->setImageOffset(QVector2D(13.0f, 26.0f));

    // The production push path, twice, exactly as an edit installs keys.
    stack.push(node.timeLine()->createPusher(core::TimeKeyType_Image, 0, k0));
    stack.push(node.timeLine()->createPusher(core::TimeKeyType_Image, 24, k1));

    CHECK(node.timeLine()->map(core::TimeKeyType_Image).size() == 2);
    CHECK(ctrl::graph::Curve::easingOf(*k0, core::TimeKeyType_Image, 0) == &k0->data().easing(0));
    // EACH component, not just the first: the timeline's chip/picker read an Image
    // key through this same bridge (core::keyEasingOf), and a component the bridge
    // could not name would make its curve unreadable/unwritable there.
    CHECK(ctrl::graph::Curve::easingOf(*k0, core::TimeKeyType_Image, 1) == &k0->data().easing(1));

    const float expectedV0[2] = { 3.0f, 6.0f };
    const float expectedV1[2] = { 13.0f, 26.0f };

    for (int c = 0; c < 2; ++c) {
        const Channel channel(core::TimeKeyType_Image, c);
        const Curve curve(*node.timeLine(), channel);

        // Both components of the channel the graph draws are declared step-held.
        CHECK_MSG(!channel.isInterpolated(), "component %d must not interpolate the image offset", c);
        CHECK(curve.isEmpty() == false);
        CHECK(curve.keys().size() == 2);
        CHECK(curve.keys().at(0) == k0 && curve.keys().at(1) == k1);

        const float v0 = curve.valueOf(*k0);
        const float v1 = curve.valueOf(*k1);
        CHECK_MSG(v0 == expectedV0[c], "component %d: first key value %g, expected %g", c, v0, expectedV0[c]);
        CHECK_MSG(v1 == expectedV1[c], "component %d: second key value %g, expected %g", c, v1, expectedV1[c]);

        // At every interior frame the graph holds the FIRST key's value exactly:
        // no easing leak from the Custom bezier on k0 (an interpolating
        // implementation would be v0 + (v1-v0)*rate, which differs at frame 12
        // alone by the whole half-span). Frame 0 sits on the first key; frames 24
        // and 30 carry the SECOND key (30 is past the last key, held to the end).
        for (core::Frame f : { core::Frame(0), core::Frame(1), core::Frame(5), core::Frame(12), core::Frame(23) }) {
            CHECK_MSG(
                curve.sample(f) == v0,
                "component %d frame %d sampled %g, first key is %g",
                c,
                f.get(),
                curve.sample(f),
                v0
            );
        }
        CHECK_MSG(curve.sample(core::Frame(24)) == v1, "component %d frame 24 must be the second key %g", c, v1);
        CHECK_MSG(curve.sample(core::Frame(30)) == v1, "component %d frame 30 must hold the second key %g", c, v1);

        // The drawable segment still carries the authored Custom param, and it is
        // the step -- not that curve -- that is drawn. This is the whole point:
        // the easing is present in the document and playback/graph ignore it.
        CHECK(curve.segmentCount() == 1);
        const Curve::Segment segment = curve.segment(0);
        CHECK(segment.from == k0 && segment.to == k1);
        CHECK(segment.fromValue == v0 && segment.toValue == v1);
        CHECK(curve.isCustom(segment));
        CHECK(segment.easing.type == util::Easing::Type_Custom);
        CHECK(segment.easing.cubicBezier.x1 == 0.9f);
        CHECK(segment.easing.cubicBezier.y1 == 0.1f);
        CHECK(segment.easing.cubicBezier.x2 == 0.1f);
        CHECK(segment.easing.cubicBezier.y2 == 0.9f);
        // The authored param is the document's own (let a handle drag write it).
        CHECK(ctrl::graph::Curve::easingOf(*k0, core::TimeKeyType_Image, 0) == &k0->data().easing(0));

        // The polyline is a staircase: (0, v0), a tread holding v0 to frame 24,
        // then the riser to v1 at frame 24. Nothing is emitted strictly between
        // the value boundaries, and a point on the tread at an interior frame
        // carries v0.
        QVector<QPointF> poly;
        curve.buildPolyline(core::Frame(0), core::Frame(24), 8, poly);
        CHECK(poly.size() >= 3);
        CHECK_MSG(poly.first() == QPointF(0.0, v0), "component %d: first point must be (0, %g)", c, v0);
        CHECK_MSG(poly.last() == QPointF(24.0, v1), "component %d: last point must be (24, %g)", c, v1);
        for (const QPointF& p : poly) {
            if (p.x() < 24.0 - 1e-9)
                CHECK_MSG(p.y() == v0, "component %d: point (%g, %g) before frame 24 is not v0=%g", c, p.x(), p.y(), v0);
            // Nothing strictly between the two keys is ever drawn: the shape is a
            // staircase, not a curve.
            CHECK_MSG(p.y() == v0 || p.y() == v1, "component %d: point (%g, %g) is interpolated", c, p.x(), p.y());
        }
        bool treadAtArrival = false;
        bool riserAtArrival = false;
        for (const QPointF& p : poly) {
            if (p.x() == 24.0 && p.y() == v0)
                treadAtArrival = true;
            if (p.x() == 24.0 && p.y() == v1)
                riserAtArrival = true;
        }
        CHECK(treadAtArrival); // the tread reaches the arrival frame holding v0
        CHECK(riserAtArrival); // the jump to v1 happens exactly at frame 24
        // ...and the value drawn at an interior frame (the tread under frame 12)
        // is v0, matching sample(12).
        CHECK_MSG(polylineValueAt(poly, 12.0) == v0, "component %d: polyline value at frame 12 is not v0", c);
        CHECK(curve.sample(core::Frame(12)) == v0);
    }

    return 0;
}

} // namespace

int main(int argc, char** argv) {
    // QApplication, not QCoreApplication: the timeline and the key types are
    // QObject-based and QObject::tr is what the channel labels go through.
    QApplication app(argc, argv);

    // A live core-profile GL context, the same bootstrap roundtrip_main and
    // pose_pull_crash use. ImageKey construction needs it; nothing here draws,
    // so no VAO is bound (a core-profile context only requires one for a draw).
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setVersion(gl::Global::kVersion.first, gl::Global::kVersion.second);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);

    QOpenGLWidget glWidget;
    glWidget.resize(4, 4);
    glWidget.show();
    glWidget.makeCurrent();
    if (!glWidget.context()) {
        std::fprintf(stderr, "graph_image_step: no GL context could be created\n");
        return 2;
    }
    auto funcs = QOpenGLVersionFunctionsFactory::get<gl::Global::Functions>(glWidget.context());
    if (!funcs || !funcs->initializeOpenGLFunctions()) {
        std::fprintf(stderr, "graph_image_step: GL function table could not be initialized\n");
        return 2;
    }
    gl::Global::setContext(glWidget);
    gl::Global::setFunctions(*funcs);

    gl::DeviceInfo dev = makeDeviceInfo();
    gl::DeviceInfo::setInstance(&dev);

    if (sectionImageOffsetStaircase())
        return 1;

    std::printf(
        "graph_image_step: image-offset staircase proven on a live GL context -- two ImageKeys at "
        "frames 0/24, both components step-held (a Custom easing on the first key leaks nothing at "
        "frames 1/5/12/23), and buildPolyline draws the step ending on the second key at frame 24\n"
    );
    return 0;
}
