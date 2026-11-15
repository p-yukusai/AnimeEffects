// Gate for the WAVE DEFORM filter (core/WaveKey.h, core/WavePass.h,
// core/FilterFrame.cpp's wave pass, core/LayerNode.cpp + core/FolderNode.cpp):
//
//   [1] the key's data contract - a default key is inert, the preset is the
//       visible starter a new filter gets, and every setter keeps the range the
//       shader and the UI assume;
//   [2] the five graph channels read and write the same five fields playback
//       blends (one read table, one write table - ctrl/graph/graph_Channel.cpp);
//   [3] playback: a two-key segment interpolates each channel with its OWN
//       easing while the shape fields (speed, crest, the dual wave, the deform
//       mode) are STEP-HELD from the earlier key, and a lane with no explicit
//       key plays its default key;
//   [4] the 0.18 record: every field, the easings and the filter stack survive
//       TimeLine serialize -> deserialize;
//   [5] the PASS MAPPING: content pixels -> composite pixels is the exact path
//       the layer shader draws with (image origin at the layer's on-screen
//       corner, y inverted into the framebuffer, rotation included), and its
//       inverse round-trips - which is what keeps the wave glued to the artwork
//       instead of to the screen;
//   [6] the 0.19 painted-mask record: both fields, their content rectangles and
//       their neutral starts survive TimeLine serialize -> deserialize;
//   [7] filter ACTIVITY: a value living on a lane's DEFAULT key (no explicit key
//       at all) is live, editing it back to the identity makes it not live, and
//       a key at a later frame is live BEFORE that frame too - the renderer tests
//       the BLENDED value, not the key map (core/FilterActivity.h), so a static
//       blur/HSV renders exactly like a static wave.
//
// Headless: no widgets, no GL context. The mapping is pure matrix math and the
// blend is the production core::TimeKeyBlender.
#include <QApplication>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QSizeF>
#include <QVector2D>
#include <QVersionNumber>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "XC.h"
#include "cmnd/Stack.h"
#include "core/CameraInfo.h"
#include "core/Deserializer.h"
#include "core/FormatVersion.h"
#include "core/KeyEasing.h"
#include "core/Serializer.h"
#include "core/BlurKey.h"
#include "core/FilterActivity.h"
#include "core/FilterMask.h"
#include "core/HsvKey.h"
#include "core/TimeKeyBlender.h"
#include "core/TimeKeyExpans.h"
#include "core/TimeCacheAccessor.h"
#include "core/TimeCacheLock.h"
#include "core/TimeLine.h"
#include "core/WaveKey.h"
#include "core/WavePass.h"
#include "ctrl/graph/graph_Channel.h"
#include "gl/DeviceInfo.h"
#include "util/IDSolver.h"
#include "util/IProgressReporter.h"
#include "util/StreamReader.h"
#include "util/StreamWriter.h"

// core::Project.cpp's audio sidecar loader is not part of this test.
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
class NullReporter: public util::IProgressReporter {
public:
    void setSection(const QString&) override {}
    void setMaximum(int) override {}
    void setProgress(int) override {}
    bool wasCanceled() const override { return false; }
};

HarnessAssertHandler gAssertHandler;
HarnessErrorHandler gErrorHandler;

int failures = 0;
void check(bool aOk, const char* aWhat) {
    if (!aOk) {
        std::printf("FAIL  %s\n", aWhat);
        ++failures;
    }
}
bool nearly(float a, float b, float aEpsilon = 1.0e-4f) { return std::fabs(a - b) <= aEpsilon; }

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

namespace core {
// A node with a real TimeLine and nothing else: the blender walks the tree but
// a wave key owns no meshes, no images and no bones.
class TestNode: public ObjectNode {
    TimeLine mTimeLine;

public:
    ObjectType type() const override { return ObjectType_Layer; }
    void setName(const QString&) override {}
    const QString& name() const override {
        static QString s(QStringLiteral("node"));
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

core::WaveKey* pushWave(core::TimeLine& aLine, cmnd::Stack& aStack, int aFrame, const core::WaveKey::Data& aData) {
    auto* key = new core::WaveKey();
    key->data() = aData;
    aStack.push(aLine.createPusher(core::TimeKeyType_Wave, aFrame, key));
    return key;
}

// Linear easing on every channel, so a halfway frame is exactly halfway.
void linearEasing(core::WaveKey& aKey) {
    util::Easing::Param param;
    param.type = util::Easing::Type_Linear;
    for (int c = 0; c < core::WaveKey::kComponents; ++c)
        aKey.data().easing(c) = param;
}

QVector2D mapPoint(const QMatrix4x4& aMatrix, const QVector2D& aPoint) {
    const QVector4D mapped = aMatrix * QVector4D(aPoint.x(), aPoint.y(), 0.0f, 1.0f);
    return QVector2D(mapped.x(), mapped.y());
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // ---------------------------------------------------------------- [1] data
    {
        core::WaveKey::Data data;
        check(!data.isActive(), "a default wave key is inert (strength 0)");
        check(data.isIdentity(), "the default data is the identity");
        check(data.wavelength() > 0.0f, "the default wavelength is renderable");

        core::WaveKey::Data preset = core::WaveKey::Data::presetData();
        check(preset.isActive(), "the preset deforms");
        check(nearly(preset.strength(), core::WaveKey::Preset::strength), "the preset carries its strength");
        check(nearly(preset.wavelength(), core::WaveKey::Preset::wavelength), "the preset carries its wavelength");

        preset.setWavelength(0.0f);
        check(preset.wavelength() == 1.0f, "a wavelength never reaches zero (the field divides by it)");
        preset.setMix(2.0f);
        check(preset.mix() == 1.0f, "mix clamps to 1");
        preset.setMix(-3.0f);
        check(preset.mix() == 0.0f, "mix clamps to 0");
        preset.setCrest(0.0f);
        check(preset.crest() == 0.25f, "crest clamps at its floor");
        preset.setCrest(99.0f);
        check(preset.crest() == 4.0f, "crest clamps at its ceiling");
        preset.setDualScale(0.01f);
        check(preset.dualScale() == 0.1f, "the dual scale clamps at its floor");
        preset.setDualScale(99.0f);
        check(preset.dualScale() == 4.0f, "the dual scale clamps at its ceiling");
        preset.setDeform(core::WaveKey::Deform_Sway);
        check(preset.deform() == core::WaveKey::Deform_Sway, "the deform mode is stored");

        check(core::easingComponentCount(core::TimeKeyType_Wave) == 5, "a wave key stores five easing params");
        check(core::isFilterType(core::TimeKeyType_Wave), "wave is a stackable filter type");
        check(
            core::TimeLine::getTimeKeyName(core::TimeKeyType_Wave) == QStringLiteral("Wave"),
            "the serialized token is \"Wave\" (<= 8 bytes)"
        );
        check(
            core::TimeLine::getTimeKeyType(QStringLiteral("Wave")) == core::TimeKeyType_Wave,
            "the token resolves back to the type"
        );

        if (failures == 0)
            std::printf("[1] wave key data contract\n");
    }

    // ------------------------------------------------------------- [2] channels
    {
        const int before = failures;
        const QVector<ctrl::graph::Channel> channels = ctrl::graph::Channel::channelsOf(core::TimeKeyType_Wave);
        check(channels.size() == 5, "the graph publishes five wave channels");

        core::WaveKey key;
        key.data() = core::WaveKey::Data::presetData();
        key.setDeform(core::WaveKey::Deform_Ripple);

        // One write per channel that stays inside every clamp, then a read back:
        // the pair is the graph's whole value contract.
        const float targets[5] = {7.5f, 220.0f, 33.0f, 0.75f, 0.6f};
        bool written = true;
        for (int c = 0; c < channels.size(); ++c)
            written = channels.at(c).writeValue(key, targets[c]) && written;
        check(written, "every wave channel writes");
        for (int c = 0; c < channels.size(); ++c) {
            check(
                nearly(channels.at(c).value(key), targets[c]),
                "each wave channel reads back what it wrote"
            );
            check(!channels.at(c).componentName().isEmpty(), "each wave channel has a name");
            check(channels.at(c).isInterpolated(), "every wave channel is interpolated (playback blends it)");
        }
        // The non-channel shape fields must be untouched by channel writes.
        check(key.deform() == core::WaveKey::Deform_Ripple, "a channel write leaves the deform mode alone");
        check(
            nearly(key.speed(), core::WaveKey::Preset::speed),
            "a channel write leaves the step-held speed alone"
        );

        if (failures == before)
            std::printf("[2] graph channels read and write the blended fields\n");
    }

    // -------------------------------------------------------------- [3] playback
    {
        const int before = failures;
        core::TestNode node;
        core::TimeLine& line = *node.timeLine();
        cmnd::Stack stack;

        core::WaveKey::Data first = core::WaveKey::Data::presetData();
        first.setStrength(10.0f);
        first.setWavelength(100.0f);
        first.setAngleDeg(0.0f);
        first.setPhase(0.0f);
        first.setMix(0.0f);
        first.setSpeed(0.5f);
        first.setCrest(2.0f);
        first.setDualAngleDeg(70.0f);
        first.setDualScale(0.5f);
        first.setDeform(core::WaveKey::Deform_Sway);

        core::WaveKey::Data second = core::WaveKey::Data::presetData();
        second.setStrength(30.0f);
        second.setWavelength(300.0f);
        second.setAngleDeg(90.0f);
        second.setPhase(1.0f);
        second.setMix(1.0f);
        second.setSpeed(-2.0f);
        second.setCrest(1.0f);
        second.setDualAngleDeg(-20.0f);
        second.setDualScale(2.0f);
        second.setDeform(core::WaveKey::Deform_Ripple);

        core::WaveKey* k0 = pushWave(line, stack, 0, first);
        linearEasing(*k0);
        pushWave(line, stack, 10, second);

        core::TimeInfo info;
        info.fps = 60;
        info.frameRange = util::Range(0, 10);
        info.loop = false;
        info.frame = core::Frame(5);

        core::TimeKeyBlender blender(node, false);
        blender.updateCurrents(&node, info);
        const core::WaveKey::Data middle = line.current().wave();

        check(nearly(middle.strength(), 20.0f), "the halfway frame is halfway in strength");
        check(nearly(middle.wavelength(), 200.0f), "the halfway frame is halfway in wavelength");
        check(nearly(middle.angleDeg(), 45.0f), "the direction interpolates linearly");
        check(nearly(middle.phase(), 0.5f), "the phase interpolates linearly");
        check(nearly(middle.mix(), 0.5f), "the dual mix interpolates linearly");

        // Step-held shape: the earlier key's values play through the whole
        // segment, because a smooth `speed` would make the phase (speed * time)
        // jump at every key.
        check(nearly(middle.speed(), first.speed()), "speed is step-held from the earlier key");
        check(nearly(middle.crest(), first.crest()), "crest is step-held");
        check(nearly(middle.dualAngleDeg(), first.dualAngleDeg()), "the dual angle is step-held");
        check(nearly(middle.dualScale(), first.dualScale()), "the dual scale is step-held");
        check(middle.deform() == first.deform(), "the deform mode is step-held");

        // On the key itself the values are exact.
        info.frame = core::Frame(10);
        blender.updateCurrents(&node, info);
        const core::WaveKey::Data onKey = line.current().wave();
        check(nearly(onKey.strength(), 30.0f), "a frame with its own key plays that key");
        check(nearly(onKey.crest(), 1.0f), "the later key's own shape plays on its frame");
        check(onKey.deform() == core::WaveKey::Deform_Ripple, "the later key's own deform plays");

        // A lane with only a DEFAULT key must play it: that is what makes a
        // filter added from the stack deform before it is keyed at all.
        core::TestNode defaulted;
        core::WaveKey::Data only = core::WaveKey::Data::presetData();
        only.setStrength(7.0f);
        auto* defaultKey = new core::WaveKey();
        defaultKey->data() = only;
        defaulted.timeLine()->grabDefaultKey(core::TimeKeyType_Wave, defaultKey);
        info.frame = core::Frame(3);
        core::TimeKeyBlender defaultBlender(defaulted, false);
        defaultBlender.updateCurrents(&defaulted, info);
        check(
            nearly(defaulted.timeLine()->current().wave().strength(), 7.0f),
            "a wave with no explicit key plays its default key"
        );

        if (failures == before)
            std::printf("[3] playback blends the channels and holds the shape\n");
    }

    // ------------------------------------------------------------ [4] on disk
    {
        const int before = failures;
        core::TimeLine line;
        cmnd::Stack stack;

        // A stack entry plus an explicit, PROPORTIONAL key: both halves of the
        // wave's record have to survive, and a proportional wave key exercises
        // all five component weights (kMaxProportionalComponents).
        QVector<core::FilterEntry> entries;
        entries.append(core::FilterEntry(core::TimeKeyType_Wave, true));
        line.setFilterEntries(entries);

        core::WaveKey::Data data = core::WaveKey::Data::presetData();
        data.setStrength(-13.5f);
        data.setWavelength(271.0f);
        data.setAngleDeg(137.0f);
        data.setPhase(-0.375f);
        data.setMix(0.8125f);
        data.setSpeed(-1.25f);
        data.setCrest(3.5f);
        data.setDualAngleDeg(-47.0f);
        data.setDualScale(1.75f);
        data.setDeform(core::WaveKey::Deform_Sway);

        auto* key = new core::WaveKey();
        key->data() = data;
        util::Easing::Param eased;
        eased.type = util::Easing::Type_Custom;
        eased.cubicBezier.x1 = 0.25f;
        eased.cubicBezier.y1 = 0.5f;
        eased.cubicBezier.x2 = 0.75f;
        eased.cubicBezier.y2 = 0.875f;
        for (int c = 0; c < core::WaveKey::kComponents; ++c)
            key->data().easing(c) = eased;
        stack.push(line.createPusher(core::TimeKeyType_Wave, 4, key));

        core::TimeKey::Proportional proportional;
        proportional.timeValid = true;
        proportional.timeWeight = 0.375f;
        for (int c = 0; c < core::TimeKey::kMaxProportionalComponents; ++c) {
            proportional.valueValid[c] = true;
            proportional.valueWeight[c] = 0.1f * float(c + 1);
        }
        key->setProportionalState(proportional);

        std::ostringstream bytes;
        util::StreamWriter writer(bytes);
        core::Serializer out(writer);
        check(line.serialize(out), "the timeline serializes");
        const std::string stored = bytes.str();
        check(!stored.empty(), "the stream carries the timeline block");

        std::istringstream input(stored);
        util::LEStreamReader reader(input);
        util::IDSolver<void*> solver;
        NullReporter reporter;
        gl::DeviceInfo device;
        core::Deserializer in(
            reader, solver, stored.size() + 1, QVersionNumber(0, AE_PROJECT_FORMAT_MINOR_VERSION), device,
            reporter, 0
        );
        core::TimeLine loaded;
        check(loaded.deserialize(in), "the timeline deserializes at the current format");

        check(loaded.filters().size() == 1, "the filter stack survives");
        if (loaded.filters().size() == 1)
            check(loaded.filters().first().type == core::TimeKeyType_Wave, "the wave entry survives");
        check(loaded.map(core::TimeKeyType_Wave).size() == 1, "the explicit wave key survives");
        if (const core::TimeKey* loadedKey = loaded.map(core::TimeKeyType_Wave).value(4)) {
            check(loadedKey->type() == core::TimeKeyType_Wave, "the key is a wave key");
            const auto& read = static_cast<const core::WaveKey&>(*loadedKey).data();
            check(nearly(read.strength(), data.strength()), "strength survives");
            check(nearly(read.wavelength(), data.wavelength()), "wavelength survives");
            check(nearly(read.angleDeg(), data.angleDeg()), "direction survives");
            check(nearly(read.phase(), data.phase()), "phase survives");
            check(nearly(read.mix(), data.mix()), "mix survives");
            check(nearly(read.speed(), data.speed()), "speed survives");
            check(nearly(read.crest(), data.crest()), "crest survives");
            check(nearly(read.dualAngleDeg(), data.dualAngleDeg()), "the dual angle survives");
            check(nearly(read.dualScale(), data.dualScale()), "the dual scale survives");
            check(read.deform() == data.deform(), "the deform mode survives");
            check(
                read.easing(4).type == util::Easing::Type_Custom
                    && read.easing(4).cubicBezier == eased.cubicBezier,
                "the fifth component's easing survives (five params are stored, not three)"
            );
            const core::TimeKey::Proportional* readProportional = loadedKey->proportionalState();
            check(readProportional != nullptr, "the proportional state survives");
            if (readProportional) {
                check(nearly(readProportional->timeWeight, 0.375f), "the time weight survives");
                check(readProportional->valueValid[4], "the fifth component's weight flag survives");
                check(nearly(readProportional->valueWeight[4], 0.5f), "the fifth component's weight survives");
            }
        } else {
            check(false, "the wave key is present after the round trip");
        }

        if (failures == before)
            std::printf("[4] the 0.18 wave record round-trips\n");
    }

    // ------------------------------------------------------------- [5] mapping
    {
        const int before = failures;
        core::CameraInfo camera;
        // An 800x600 view of an 800x600 canvas at 1:1, so the canvas corners are
        // the viewport corners and every expected pixel is exact.
        camera.reset(QSize(800, 600), 1.0, QSize(800, 600), QPoint(0, 0));

        // A layer at the canvas centre with an 800x600 image: its ImageKey offset
        // is -size/2 and its world matrix is the Move position.
        QMatrix4x4 world;
        world.translate(400.0f, 300.0f);
        const QVector2D imageOffset(-400.0f, -300.0f);
        const QSizeF composite(800, 600);

        const QMatrix4x4 toScreen = core::waveContentToScreen(camera, composite, world, imageOffset);
        const QVector2D origin = mapPoint(toScreen, QVector2D(0, 0));
        const QVector2D farCorner = mapPoint(toScreen, QVector2D(800, 600));
        // The image's top-left is the viewport's top-left, and the framebuffer's
        // row 0 is its BOTTOM (vDestCoord = (ndc + 1) * size / 2).
        check(nearly(origin.x(), 0.0f) && nearly(origin.y(), 600.0f), "content (0,0) maps to the composite's bottom-left");
        check(
            nearly(farCorner.x(), 800.0f) && nearly(farCorner.y(), 0.0f),
            "content (w,h) maps to the composite's top-right"
        );

        // The inverse must undo it: a 1px displacement in content space is a 1px
        // displacement in the composite, sign-flipped on y.
        bool invertible = false;
        const QMatrix4x4 toContent = toScreen.inverted(&invertible);
        check(invertible, "the mapping is invertible");
        const QVector2D back = mapPoint(toContent, QVector2D(120.0f, 480.0f));
        check(nearly(back.x(), 120.0f) && nearly(back.y(), 120.0f), "the inverse maps the sample back to content pixels");

        // A rotation is part of the mapping, not of the field: a layer turned 90
        // degrees carries its content coordinates with it.
        QMatrix4x4 rotated;
        rotated.translate(400.0f, 300.0f);
        rotated.rotate(90.0f, 0.0f, 0.0f, 1.0f);
        const QMatrix4x4 rotatedScreen = core::waveContentToScreen(camera, composite, rotated, imageOffset);
        // content (500,300) is 100px right of the image centre; after the turn it
        // sits 100px BELOW the layer's centre, i.e. canvas (400,400).
        const QVector2D turned = mapPoint(rotatedScreen, QVector2D(500.0f, 300.0f));
        check(nearly(turned.x(), 400.0f) && nearly(turned.y(), 200.0f), "a turned layer's content coordinates turn with it");

        // A node whose transform collapses a dimension has no inverse: the pass
        // must not be built for it.
        QMatrix4x4 collapsed;
        collapsed.scale(0.0f, 1.0f, 1.0f);
        core::TimeInfo time;
        time.fps = 60;
        time.frame = core::Frame(30);
        core::WaveKey::Data active = core::WaveKey::Data::presetData();
        const core::WavePassParams pass =
            core::wavePassFor(camera, time, composite, active, collapsed, imageOffset);
        check(pass.strength == 0.0f, "a collapsed transform disables the pass instead of inverting it");

        // The pass carries the document time as SECONDS (frame / fps), which is
        // what makes an export agree with the timeline.
        const core::WavePassParams timed = core::wavePassFor(camera, time, composite, active, world, imageOffset);
        check(nearly(timed.seconds, 0.5f), "the phase time is frame / fps");
        check(nearly(timed.strength, active.strength()), "the pass carries the blended strength");

        // The clamp rectangle is what keeps a displaced sample inside the image
        // (clamp-to-edge, the way an image effect's texture behaves).
        const core::WavePassParams clamped = core::wavePassFor(
            camera, time, composite, active, world, imageOffset, QVector2D(0, 0), QVector2D(800, 600)
        );
        check(clamped.hasClamp(), "an image layer publishes its content rectangle");
        check(!timed.hasClamp(), "a folder publishes none (its content is bounded by the composite)");

        if (failures == before)
            std::printf("[5] the pass mapping is the layer's own draw path\n");
    }

    // ------------------------------------------------------ [6] the mask record
    {
        const int before = failures;
        core::TimeLine line;
        QVector<core::FilterEntry> entries;
        entries.append(core::FilterEntry(core::TimeKeyType_Wave, true));
        line.setFilterEntries(entries);

        const QRectF content(0.0, 0.0, 200.0, 100.0);
        core::FilterMask& amount =
            line.ensureFilterMask(core::TimeKeyType_Wave, core::FilterMaskChannel_Amount, content);
        check(!amount.isEmpty(), "the amount mask allocates at the content rectangle");
        check(amount.image().size() == QSize(200, 100), "an under-cap content rect maps 1 texel per pixel");
        check(
            amount.image().constScanLine(0)[0] == 255,
            "the AMOUNT mask starts neutral WHITE, so a fresh mask renders like no mask at all"
        );

        core::FilterMask& time =
            line.ensureFilterMask(core::TimeKeyType_Wave, core::FilterMaskChannel_TimeOffset, content);
        check(
            time.image().constScanLine(0)[0] == 0,
            "the TIME OFFSET mask starts neutral BLACK (no delay), which is also a no-op"
        );

        // paint a stripe into each field
        for (int y = 0; y < amount.image().height(); ++y) {
            uchar* line8 = amount.image().scanLine(y);
            for (int x = 0; x < 10; ++x)
                line8[x] = 0;
        }
        amount.markDirty(amount.image().rect());
        for (int y = 0; y < time.image().height(); ++y) {
            uchar* line8 = time.image().scanLine(y);
            for (int x = 0; x < 10; ++x)
                line8[x] = 255;
        }
        time.markDirty(time.image().rect());
        const quint64 amountRevision = amount.revision();
        check(amountRevision > 0, "painting bumps the revision the texture follows");

        std::ostringstream bytes;
        util::StreamWriter writer(bytes);
        core::Serializer out(writer);
        check(line.serialize(out), "a timeline with two masks serializes");
        const std::string stored = bytes.str();

        std::istringstream input(stored);
        util::LEStreamReader reader(input);
        util::IDSolver<void*> solver;
        NullReporter reporter;
        gl::DeviceInfo device;
        core::Deserializer in(
            reader, solver, stored.size() + 1, QVersionNumber(0, AE_PROJECT_FORMAT_MINOR_VERSION), device,
            reporter, 0
        );
        core::TimeLine loaded;
        check(loaded.deserialize(in), "the masks deserialize at the current format");

        const core::FilterMask* readAmount =
            loaded.filterMask(core::TimeKeyType_Wave, core::FilterMaskChannel_Amount);
        const core::FilterMask* readTime =
            loaded.filterMask(core::TimeKeyType_Wave, core::FilterMaskChannel_TimeOffset);
        check(readAmount != nullptr && readTime != nullptr, "both masks come back, on their own channels");
        if (readAmount && readTime) {
            check(readAmount->contentRect() == content, "the number field keeps its content rectangle");
            check(readAmount->image().size() == amount.image().size(), "the number field keeps its texel grid");
            check(readAmount->image().constScanLine(0)[0] == 0, "the painted stripe survives (the amount mask)");
            check(readAmount->image().constScanLine(20)[20] == 255, "the unpainted area survives");
            check(readTime->image().constScanLine(0)[0] == 255, "the painted stripe survives (the time mask)");
            check(readTime->image().constScanLine(20)[20] == 0, "the time mask keeps its neutral elsewhere");
        }

        if (failures == before)
            std::printf("[6] the 0.19 painted-mask record round-trips\n");
    }

    // ------------------------------ [7] a filter's DEFAULT key makes it live
    //
    // The reported defect this gates: a Blur/HSV added from the stack and edited
    // BEFORE any keyframe is written to the lane's DEFAULT key (prop_KeyAccessor's
    // editFrame picks TimeLine::kDefaultKeyIndex while the map is empty), and the
    // renderer used to require "an explicit key at or before this frame" - so the
    // value the panel showed and playback blended was never rendered, while Wave
    // (which already read the blended value) applied. The gate is now the
    // predicate pair the nodes use: the stack entry is enabled AND the BLENDED
    // value is above the type's epsilon.
    {
        const int before = failures;
        core::TimeInfo info;
        info.fps = 60;
        info.frameRange = util::Range(0, 10);
        info.loop = false;
        info.frame = core::Frame(0);

        // The renderer's read order: the accessor blends the frame (it re-blends
        // only when the master cache is missing, so it is cleared first - the same
        // invalidation an edit notification causes) and the predicate then reads
        // that blended value.
        const auto readFrame = [](core::TestNode& aNode, const core::TimeInfo& aTime) -> const core::TimeKeyExpans& {
            aNode.timeLine()->current().clearMasterCache();
            core::TimeCacheLock lock;
            core::TimeCacheAccessor accessor(aNode, lock, aTime, false);
            return aNode.timeLine()->current();
        };
        const auto live = [](const core::TimeLine& aLine, const core::TimeKeyExpans& aExpans, core::TimeKeyType aType) {
            return aLine.filterEnabled(aType) && core::filterValueIsActive(aType, aExpans);
        };

        // An edit with no explicit key, for each type: the value is on the default
        // key, the map is empty, and the filter must still be live.
        {
            core::TestNode node;
            core::TimeLine& line = *node.timeLine();
            QVector<core::FilterEntry> entries;
            entries.append(core::FilterEntry(core::TimeKeyType_Blur, true));
            entries.append(core::FilterEntry(core::TimeKeyType_HSV, true));
            line.setFilterEntries(entries);

            auto* blur = new core::BlurKey();
            blur->setAmount(8.0f); // the edit the panel writes
            line.grabDefaultKey(core::TimeKeyType_Blur, blur);
            auto* hsv = new core::HSVKey();
            hsv->setHue(40); // additive hue 40: not the identity
            line.grabDefaultKey(core::TimeKeyType_HSV, hsv);

            const core::TimeKeyExpans& expans = readFrame(node, info);
            std::printf(
                "[7] default-key edits at frame 0: blur keys=%d radius=%.3f, hsv keys=%d hue=%d\n",
                int(line.map(core::TimeKeyType_Blur).size()), expans.maxBlurRadius(),
                int(line.map(core::TimeKeyType_HSV).size()), expans.hsv().hsv().at(0)
            );
            check(line.map(core::TimeKeyType_Blur).isEmpty(), "an edit before any key adds no explicit blur key");
            check(line.map(core::TimeKeyType_HSV).isEmpty(), "an edit before any key adds no explicit HSV key");
            check(
                core::filterValueIsActive(core::TimeKeyType_Blur, expans),
                "a static blur (default key only) is live"
            );
            check(core::filterValueIsActive(core::TimeKeyType_HSV, expans), "a static HSV (default key only) is live");
            check(live(line, expans, core::TimeKeyType_Blur), "the enabled static blur reads live");
            check(live(line, expans, core::TimeKeyType_HSV), "the enabled static HSV reads live");

            // The same slots, edited back to the type's identity.
            static_cast<core::BlurKey*>(line.defaultKey(core::TimeKeyType_Blur))->setAmount(0.0f);
            static_cast<core::HSVKey*>(line.defaultKey(core::TimeKeyType_HSV))->setHSV({0, 100, 100, 0});
            const core::TimeKeyExpans& identity = readFrame(node, info);
            std::printf(
                "[7] edited back to the identity: radius=%.3f hue=%d sat=%d val=%d\n",
                identity.maxBlurRadius(), identity.hsv().hsv().at(0), identity.hsv().hsv().at(1),
                identity.hsv().hsv().at(2)
            );
            check(
                !core::filterValueIsActive(core::TimeKeyType_Blur, identity),
                "a blur edited back to 0 is not live"
            );
            check(
                !core::filterValueIsActive(core::TimeKeyType_HSV, identity),
                "an HSV back at 0/100/100/0 is not live"
            );
        }

        // A key at a LATER frame: playback holds the first key before the first
        // key, so the filter is live at frame 0 as well - the old "frame >= first
        // key" clause hid it, and no setting expresses "start at the first key".
        {
            core::TestNode node;
            core::TimeLine& line = *node.timeLine();
            QVector<core::FilterEntry> entries;
            entries.append(core::FilterEntry(core::TimeKeyType_Blur, true));
            entries.append(core::FilterEntry(core::TimeKeyType_HSV, true));
            line.setFilterEntries(entries);

            cmnd::Stack commands;
            auto* blur = new core::BlurKey();
            blur->setAmount(5.0f);
            commands.push(line.createPusher(core::TimeKeyType_Blur, 10, blur));
            auto* hsv = new core::HSVKey();
            hsv->setHue(90);
            commands.push(line.createPusher(core::TimeKeyType_HSV, 10, hsv));

            const core::TimeKeyExpans& expans = readFrame(node, info); // frame 0, keys at 10
            std::printf(
                "[7] keyed at frame 10, read at frame 0: radius=%.3f hue=%d\n",
                expans.maxBlurRadius(), expans.hsv().hsv().at(0)
            );
            check(line.map(core::TimeKeyType_Blur).size() == 1, "the blur key sits at a later frame");
            check(line.map(core::TimeKeyType_HSV).size() == 1, "the HSV key sits at a later frame");
            check(
                core::filterValueIsActive(core::TimeKeyType_Blur, expans),
                "a blur keyed at frame 10 is already live at frame 0 (the first-key clamp is gone)"
            );
            check(
                core::filterValueIsActive(core::TimeKeyType_HSV, expans),
                "an HSV keyed at frame 10 is already live at frame 0 (the first-key clamp is gone)"
            );
        }

        // The eye toggle: the value is still there, the filter is not live.
        {
            core::TestNode node;
            core::TimeLine& line = *node.timeLine();
            QVector<core::FilterEntry> entries;
            entries.append(core::FilterEntry(core::TimeKeyType_Blur, false));
            entries.append(core::FilterEntry(core::TimeKeyType_HSV, false));
            line.setFilterEntries(entries);

            auto* blur = new core::BlurKey();
            blur->setAmount(8.0f);
            line.grabDefaultKey(core::TimeKeyType_Blur, blur);
            auto* hsv = new core::HSVKey();
            hsv->setHue(40);
            line.grabDefaultKey(core::TimeKeyType_HSV, hsv);

            const core::TimeKeyExpans& expans = readFrame(node, info);
            std::printf(
                "[7] disabled entries: blur value=%d stack=%d, hsv value=%d stack=%d\n",
                int(core::filterValueIsActive(core::TimeKeyType_Blur, expans)),
                int(live(line, expans, core::TimeKeyType_Blur)),
                int(core::filterValueIsActive(core::TimeKeyType_HSV, expans)),
                int(live(line, expans, core::TimeKeyType_HSV))
            );
            check(
                core::filterValueIsActive(core::TimeKeyType_Blur, expans),
                "a disabled blur still carries its value"
            );
            check(
                core::filterValueIsActive(core::TimeKeyType_HSV, expans),
                "a disabled HSV still carries its value"
            );
            check(!live(line, expans, core::TimeKeyType_Blur), "a disabled blur stack entry is not live");
            check(!live(line, expans, core::TimeKeyType_HSV), "a disabled HSV stack entry is not live");
        }

        if (failures == before)
            std::printf("[7] the blended value (not the key map) decides which filters are live\n");
    }

    if (failures == 0)
        std::printf("wave_deform: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
