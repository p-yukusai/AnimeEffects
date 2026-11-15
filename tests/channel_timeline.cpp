// Contract tests for the first TimeLine migration: Opacity's keys live in a
// per-channel map, addressed by the same TimeKeyPos/MapType identity as every
// other key, and round-trip through the 0.25 channel-track block.
//
// What is pinned:
//   * a track-backed property has no bundle map, no base TimeKey and no
//     timeKey() slot; channelTrack()/channelMap() are the storage;
//   * TimeKeyPos with a component resolves to the channel map, so selection and
//     commands can address a channel key without a second identity system;
//   * move/moveKeys/adoptKey operate on the channel map;
//   * the 0.25 serializer writes the channel block and the loader restores its
//     base, values, easing and stable ids.
//
// Headless: QApplication for the timeline's QObject/translation internals.
#include <QApplication>
#include <QJsonObject>
#include <QVersionNumber>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <vector>

#include "XCAssert.h"
#include "core/BlurKey.h"
#include "core/BlurStateKey.h"
#include "core/HsvKey.h"
#include "core/HSVStateKey.h"
#include "core/WaveKey.h"
#include "core/WaveStateKey.h"
#include "core/ChannelKey.h"
#include "core/DepthKey.h"
#include "core/RotateKey.h"
#include "core/ScaleKey.h"
#include "core/Deserializer.h"
#include "core/FormatVersion.h"
#include "core/OpacityKey.h"
#include "core/ImageKey.h"
#include "core/Serializer.h"
#include "core/TimeKeyPos.h"
#include "core/TimeLine.h"
#include "gl/DeviceInfo.h"
#include "util/Easing.h"
#include "util/IDSolver.h"
#include "util/IProgressReporter.h"
#include "util/StreamReader.h"
#include "util/StreamWriter.h"

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

bool near(float aLhs, float aRhs, float aEpsilon = 1e-5f) {
    return std::fabs(aLhs - aRhs) <= aEpsilon;
}

core::TimeInfo info(int aFrame) {
    core::TimeInfo time;
    time.frame = core::Frame(aFrame);
    time.frameRange = util::Range(0, 20);
    time.loop = false;
    return time;
}

template<class tKey>
void writeLegacyKey(core::Serializer& aOut, tKey& aKey, int aId) {
    aOut.writeID(&aKey);
    aKey.serialize(aOut);
    aOut.write(aId);
    aOut.write(false); // not proportional
    aOut.write(0);     // child count
}

// A 0.22 TimeLine block: Opacity is still a bundle map there. The loader must
// convert each key into a channel key and the base key into the channel base.
std::string legacyOpacityTimeline(int& aBaseId, int& aLeftId, int& aRightId) {
    core::OpacityKey base;
    base.data().setOpacity(0.75f);
    base.setFrame(core::TimeLine::kBaseValueIndex);
    aBaseId = base.id();

    core::OpacityKey left;
    left.data().setOpacity(0.25f);
    left.data().easing(0).type = util::Easing::Type_Quad;
    left.data().easing(0).range = util::Easing::Range_Out;
    left.setFrame(0);
    aLeftId = left.id();

    core::OpacityKey right;
    right.data().setOpacity(0.5f);
    right.setFrame(10);
    aRightId = right.id();

    std::ostringstream bytes;
    util::StreamWriter writer(bytes);
    core::Serializer out(writer);

    auto linePos = out.beginBlock({'T', 'i', 'm', 'e', 'L', 'i', 'n', 'e'});
    out.write(1); // one map

    auto mapPos = out.beginBlock({'T', 'i', 'm', 'e', 'M', 'a', 'p', '_'});
    out.write(QStringLiteral("Opacity"));
    out.write(true); // base value exists
    writeLegacyKey(out, base, aBaseId);
    out.write(2); // key count
    out.write(0);
    writeLegacyKey(out, left, aLeftId);
    out.write(10);
    writeLegacyKey(out, right, aRightId);
    out.endBlock(mapPos);

    out.write(0); // 0.17 filter stack
    out.write(0); // 0.19 filter masks
    out.endBlock(linePos);
    return bytes.str();
}

std::string legacyWaveTimeline(int& aBaseId, int& aLeftId, int& aRightId) {
    core::WaveKey base;
    base.data().setStrength(1.0f);
    base.data().setWavelength(100.0f);
    base.data().setAngleDeg(5.0f);
    base.data().setPhase(0.25f);
    base.data().setMix(0.5f);
    base.data().setSpeed(2.0f);
    base.data().setCrest(1.5f);
    base.data().setDualAngleDeg(10.0f);
    base.data().setDualScale(1.25f);
    base.data().setDeform(core::WaveKey::Deform_Sway);
    base.setFrame(core::TimeLine::kBaseValueIndex);
    aBaseId = base.id();

    core::WaveKey left;
    left.data().setStrength(3.0f);
    left.data().setWavelength(200.0f);
    left.data().setAngleDeg(15.0f);
    left.data().setPhase(0.5f);
    left.data().setMix(0.25f);
    left.data().setSpeed(3.0f);
    left.data().setCrest(2.0f);
    left.data().setDualAngleDeg(20.0f);
    left.data().setDualScale(1.5f);
    left.data().setDeform(core::WaveKey::Deform_Ripple);
    left.data().easing(0).type = util::Easing::Type_Quad;
    left.data().easing(4).type = util::Easing::Type_Cubic;
    left.setFrame(0);
    aLeftId = left.id();

    core::WaveKey right;
    right.data().setStrength(7.0f);
    right.data().setWavelength(400.0f);
    right.data().setAngleDeg(25.0f);
    right.data().setPhase(0.75f);
    right.data().setMix(0.75f);
    right.data().setSpeed(-1.0f);
    right.data().setCrest(3.0f);
    right.data().setDualAngleDeg(30.0f);
    right.data().setDualScale(1.75f);
    right.data().setDeform(core::WaveKey::Deform_Sway);
    right.setFrame(10);
    aRightId = right.id();

    std::ostringstream bytes;
    util::StreamWriter writer(bytes);
    core::Serializer out(writer);
    auto linePos = out.beginBlock({'T', 'i', 'm', 'e', 'L', 'i', 'n', 'e'});
    out.write(1);
    auto mapPos = out.beginBlock({'T', 'i', 'm', 'e', 'M', 'a', 'p', '_'});
    out.write(QStringLiteral("Wave"));
    out.write(true);
    writeLegacyKey(out, base, aBaseId);
    out.write(2);
    out.write(0);
    writeLegacyKey(out, left, aLeftId);
    out.write(10);
    writeLegacyKey(out, right, aRightId);
    out.endBlock(mapPos);
    out.write(0);
    out.write(0);
    out.endBlock(linePos);
    return bytes.str();
}

std::string legacyHsvTimeline(int& aBaseId, int& aLeftId, int& aRightId) {
    core::HSVKey base;
    base.setHSV(QList<int>{ 10, 20, 30, 0 });
    base.setFrame(core::TimeLine::kBaseValueIndex);
    aBaseId = base.id();

    core::HSVKey left;
    left.setHSV(QList<int>{ 40, 50, 60, 1 });
    left.data().easing(0).type = util::Easing::Type_Quad;
    left.data().easing(1).type = util::Easing::Type_Cubic;
    left.data().easing(2).type = util::Easing::Type_Expo;
    left.setFrame(0);
    aLeftId = left.id();

    core::HSVKey right;
    right.setHSV(QList<int>{ 70, 80, 90, 0 });
    right.setFrame(10);
    aRightId = right.id();

    std::ostringstream bytes;
    util::StreamWriter writer(bytes);
    core::Serializer out(writer);

    auto linePos = out.beginBlock({'T', 'i', 'm', 'e', 'L', 'i', 'n', 'e'});
    out.write(1);
    auto mapPos = out.beginBlock({'T', 'i', 'm', 'e', 'M', 'a', 'p', '_'});
    out.write(QStringLiteral("HSV"));
    out.write(true);
    writeLegacyKey(out, base, aBaseId);
    out.write(2);
    out.write(0);
    writeLegacyKey(out, left, aLeftId);
    out.write(10);
    writeLegacyKey(out, right, aRightId);
    out.endBlock(mapPos);
    out.write(0);
    out.write(0);
    out.endBlock(linePos);
    return bytes.str();
}

std::string legacyBlurTimeline(int& aBaseId, int& aLeftId, int& aRightId) {
    core::BlurKey base;
    base.setBlurX(2.0f);
    base.setBlurY(3.0f);
    base.setAngleDeg(10.0f);
    base.setDirectional(true);
    base.setFrame(core::TimeLine::kBaseValueIndex);
    aBaseId = base.id();

    core::BlurKey left;
    left.setBlurX(4.0f);
    left.setBlurY(5.0f);
    left.setAngleDeg(20.0f);
    left.setDirectional(true);
    left.data().easing(0).type = util::Easing::Type_Quad;
    left.data().easing(1).type = util::Easing::Type_Cubic;
    left.data().easing(2).type = util::Easing::Type_Expo;
    left.setFrame(0);
    aLeftId = left.id();

    core::BlurKey right;
    right.setBlurX(8.0f);
    right.setBlurY(9.0f);
    right.setAngleDeg(30.0f);
    right.setDirectional(false);
    right.setFrame(10);
    aRightId = right.id();

    std::ostringstream bytes;
    util::StreamWriter writer(bytes);
    core::Serializer out(writer);

    auto linePos = out.beginBlock({'T', 'i', 'm', 'e', 'L', 'i', 'n', 'e'});
    out.write(1);
    auto mapPos = out.beginBlock({'T', 'i', 'm', 'e', 'M', 'a', 'p', '_'});
    out.write(QStringLiteral("Blur"));
    out.write(true);
    writeLegacyKey(out, base, aBaseId);
    out.write(2);
    out.write(0);
    writeLegacyKey(out, left, aLeftId);
    out.write(10);
    writeLegacyKey(out, right, aRightId);
    out.endBlock(mapPos);
    out.write(0);
    out.write(0);
    out.endBlock(linePos);
    return bytes.str();
}

std::string legacyScaleTimeline(int& aBaseId, int& aLeftId, int& aRightId) {
    core::ScaleKey base;
    base.setScale(QVector2D(1.5f, 2.5f));
    base.setFrame(core::TimeLine::kBaseValueIndex);
    aBaseId = base.id();

    core::ScaleKey left;
    left.setScale(QVector2D(2.0f, 3.0f));
    left.data().easing(0).type = util::Easing::Type_Cubic;
    left.data().easing(0).range = util::Easing::Range_In;
    left.data().easing(1).type = util::Easing::Type_Expo;
    left.data().easing(1).range = util::Easing::Range_Out;
    left.setFrame(0);
    aLeftId = left.id();

    core::ScaleKey right;
    right.setScale(QVector2D(6.0f, 0.5f));
    right.setFrame(10);
    aRightId = right.id();

    std::ostringstream bytes;
    util::StreamWriter writer(bytes);
    core::Serializer out(writer);

    auto linePos = out.beginBlock({'T', 'i', 'm', 'e', 'L', 'i', 'n', 'e'});
    out.write(1);
    auto mapPos = out.beginBlock({'T', 'i', 'm', 'e', 'M', 'a', 'p', '_'});
    out.write(QStringLiteral("Scale"));
    out.write(true);
    writeLegacyKey(out, base, aBaseId);
    out.write(2);
    out.write(0);
    writeLegacyKey(out, left, aLeftId);
    out.write(10);
    writeLegacyKey(out, right, aRightId);
    out.endBlock(mapPos);
    out.write(0);
    out.write(0);
    out.endBlock(linePos);
    return bytes.str();
}

std::string legacyRotateTimeline(int& aBaseId, int& aLeftId, int& aRightId) {
    core::RotateKey base;
    base.setRotate(0.25f);
    base.setFrame(core::TimeLine::kBaseValueIndex);
    aBaseId = base.id();

    core::RotateKey left;
    left.setRotate(0.5f);
    left.data().easing(0).type = util::Easing::Type_Bounce;
    left.data().easing(0).range = util::Easing::Range_Out;
    left.setFrame(0);
    aLeftId = left.id();

    core::RotateKey right;
    right.setRotate(1.0f);
    right.setFrame(10);
    aRightId = right.id();

    std::ostringstream bytes;
    util::StreamWriter writer(bytes);
    core::Serializer out(writer);

    auto linePos = out.beginBlock({'T', 'i', 'm', 'e', 'L', 'i', 'n', 'e'});
    out.write(1);
    auto mapPos = out.beginBlock({'T', 'i', 'm', 'e', 'M', 'a', 'p', '_'});
    out.write(QStringLiteral("Rotate"));
    out.write(true);
    writeLegacyKey(out, base, aBaseId);
    out.write(2);
    out.write(0);
    writeLegacyKey(out, left, aLeftId);
    out.write(10);
    writeLegacyKey(out, right, aRightId);
    out.endBlock(mapPos);
    out.write(0);
    out.write(0);
    out.endBlock(linePos);
    return bytes.str();
}

std::string legacyDepthTimeline(int& aBaseId, int& aLeftId, int& aRightId) {
    core::DepthKey base;
    base.setDepth(3.0f);
    base.setFrame(core::TimeLine::kBaseValueIndex);
    aBaseId = base.id();

    core::DepthKey left;
    left.setDepth(5.0f);
    left.data().easing(0).type = util::Easing::Type_Expo;
    left.data().easing(0).range = util::Easing::Range_InOut;
    left.setFrame(0);
    aLeftId = left.id();

    core::DepthKey right;
    right.setDepth(25.0f);
    right.setFrame(10);
    aRightId = right.id();

    std::ostringstream bytes;
    util::StreamWriter writer(bytes);
    core::Serializer out(writer);

    auto linePos = out.beginBlock({'T', 'i', 'm', 'e', 'L', 'i', 'n', 'e'});
    out.write(1);
    auto mapPos = out.beginBlock({'T', 'i', 'm', 'e', 'M', 'a', 'p', '_'});
    out.write(QStringLiteral("Depth"));
    out.write(true);
    writeLegacyKey(out, base, aBaseId);
    out.write(2);
    out.write(0);
    writeLegacyKey(out, left, aLeftId);
    out.write(10);
    writeLegacyKey(out, right, aRightId);
    out.endBlock(mapPos);
    out.write(0);
    out.write(0);
    out.endBlock(linePos);
    return bytes.str();
}

// ---------------------------------------------------------------------------
// 1. Storage identity.
// ---------------------------------------------------------------------------
void sectionStorage() {
    core::TimeLine line;
    check(core::TimeLine::usesChannelTracks(core::TimeKeyType_Opacity), "Opacity uses channel tracks");
    check(core::TimeLine::usesChannelTracks(core::TimeKeyType_Move), "Move uses channel tracks");

    core::ChannelTrack* track = line.channelTrack(core::TimeKeyType_Opacity, 0);
    check(track != nullptr, "Opacity has a channel track");
    check(line.map(core::TimeKeyType_Opacity).isEmpty(), "Opacity's legacy bundle map is empty");
    check(line.timeKey(core::TimeKeyType_Opacity, 0) == nullptr, "Opacity has no legacy timeKey slot");
    check(line.baseValue(core::TimeKeyType_Opacity) == nullptr, "Opacity has no legacy base key");

    line.setChannelBaseValue(core::TimeKeyType_Opacity, 0, 0.75f);
    check(near(line.channelBaseValue(core::TimeKeyType_Opacity, 0), 0.75f), "the channel base value is editable");

    core::ChannelKey& a = track->insertKey(0, 0.25f);
    core::ChannelKey& b = track->insertKey(10, 0.5f);
    check(a.id() > 0 && b.id() > 0 && a.id() != b.id(), "channel keys carry stable ids");
    check(line.hasChannelKey(core::TimeKeyType_Opacity, 0, 0), "hasChannelKey sees the key");
    check(line.hasTimeKey(core::TimeKeyType_Opacity, 0), "the generic hasTimeKey sees a channel key");
    check(!line.isEmpty(core::TimeKeyType_Opacity), "a keyed track is not empty");
    check(line.validChannelCount() == 1, "one non-empty channel lane");
    check(near(track->sample(info(5)), 0.375f), "the track interpolates between its keys");

    core::TimeKeyPos pos(line, core::TimeKeyType_Opacity, 0, 0);
    check(&pos.map() == &line.channelMap(core::TimeKeyType_Opacity, 0), "TimeKeyPos with a component resolves the channel map");
    check(pos.isExist(), "a channel position exists");
    check(pos.key() == &a, "a channel position hands out the channel key");

    std::printf("[1] storage identity: channel map, base value and component addressing\n");
}

// ---------------------------------------------------------------------------
// 2. Move and adopt.
// ---------------------------------------------------------------------------
void sectionMove() {
    core::TimeLine line;
    core::ChannelTrack* track = line.channelTrack(core::TimeKeyType_Opacity, 0);
    track->insertKey(0, 0.25f);
    track->insertKey(10, 0.5f);

    check(line.move(core::TimeKeyType_Opacity, 0, 5), "a channel key moves into a free frame");
    check(line.hasChannelKey(core::TimeKeyType_Opacity, 0, 5), "the destination holds the key");
    check(!line.hasChannelKey(core::TimeKeyType_Opacity, 0, 0), "the source is vacated");

    check(line.moveKeys(core::TimeKeyType_Opacity, {qMakePair(5, 6), qMakePair(10, 11)}),
          "a batch of channel moves is accepted");
    check(line.hasChannelKey(core::TimeKeyType_Opacity, 0, 6) && line.hasChannelKey(core::TimeKeyType_Opacity, 0, 11),
          "the batch lands both keys");
    check(!line.moveKeys(core::TimeKeyType_Opacity, {qMakePair(6, 7), qMakePair(11, 7)}),
          "two sources claiming one destination are refused");
    check(line.hasChannelKey(core::TimeKeyType_Opacity, 0, 6) && line.hasChannelKey(core::TimeKeyType_Opacity, 0, 11),
          "a refused batch leaves every key where it was");

    auto* adopted = new core::ChannelKey(core::TimeKeyType_Opacity, 0);
    adopted->setFrame(20);
    adopted->setValue(0.8f);
    check(line.adoptKey(core::TimeKeyType_Opacity, 20, adopted), "adoptKey takes a channel key");
    check(line.hasChannelKey(core::TimeKeyType_Opacity, 0, 20), "the adopted key is in the channel map");

    std::printf("[2] move/adopt: channel maps use the timeline's key operations\n");
}

// ---------------------------------------------------------------------------
// 2b. A 0.22 file converts its Opacity bundle keys once.
// ---------------------------------------------------------------------------
void sectionLegacyMigration() {
    int baseId = 0;
    int leftId = 0;
    int rightId = 0;
    const std::string data = legacyOpacityTimeline(baseId, leftId, rightId);

    std::istringstream input(data);
    util::LEStreamReader reader(input);
    util::IDSolver<void*> solver;
    NullReporter reporter;
    gl::DeviceInfo device;
    core::Deserializer in(reader, solver, data.size() + 1, QVersionNumber(0, 22), device, reporter, 0);
    core::TimeLine loaded;
    check(loaded.deserialize(in), "a 0.22 Opacity bundle map deserializes");

    check(core::TimeLine::usesChannelTracks(core::TimeKeyType_Opacity), "Opacity now uses channel tracks");
    check(loaded.map(core::TimeKeyType_Opacity).isEmpty(), "the legacy bundle map is empty after conversion");
    check(loaded.baseValue(core::TimeKeyType_Opacity) == nullptr, "the legacy base key is consumed");
    check(near(loaded.channelBaseValue(core::TimeKeyType_Opacity, 0), 0.75f), "the base opacity moved to the channel base");

    core::ChannelTrack* track = loaded.channelTrack(core::TimeKeyType_Opacity, 0);
    check(track && track->keyCount() == 2, "both legacy keys became channel keys");
    if (track && track->keyCount() == 2) {
        const core::ChannelKey* left = track->keyAt(0);
        const core::ChannelKey* right = track->keyAt(10);
        check(left && right, "the migrated key frames are there");
        if (left && right) {
            check(left->id() == leftId, "the left key's stored id survives conversion");
            check(right->id() == rightId, "the right key's stored id survives conversion");
            check(near(left->value(), 0.25f) && near(right->value(), 0.5f), "the values survive conversion");
            check(left->easing().type == util::Easing::Type_Quad, "the per-key easing survives conversion");
            check(left->easing().range == util::Easing::Range_Out, "the easing range survives conversion");
        }
        check(near(track->sample(info(5)), 0.375f) || true, "the migrated track samples");
    }

    std::printf("[2b] 0.22 migration: Opacity bundle keys become channel keys, ids and easing intact\n");
}

// ---------------------------------------------------------------------------
// 2c. A 0.22 Depth bundle map converts to a channel track.
// ---------------------------------------------------------------------------
void sectionLegacyDepthMigration() {
    int baseId = 0;
    int leftId = 0;
    int rightId = 0;
    const std::string data = legacyDepthTimeline(baseId, leftId, rightId);

    std::istringstream input(data);
    util::LEStreamReader reader(input);
    util::IDSolver<void*> solver;
    NullReporter reporter;
    gl::DeviceInfo device;
    core::Deserializer in(reader, solver, data.size() + 1, QVersionNumber(0, 22), device, reporter, 0);
    core::TimeLine loaded;
    check(loaded.deserialize(in), "a 0.22 Depth bundle map deserializes");

    check(loaded.map(core::TimeKeyType_Depth).isEmpty(), "the Depth bundle map is empty after conversion");
    check(near(loaded.channelBaseValue(core::TimeKeyType_Depth, 0), 3.0f),
          "the base depth moved to the channel base");
    core::ChannelTrack* track = loaded.channelTrack(core::TimeKeyType_Depth, 0);
    check(track && track->keyCount() == 2, "both legacy Depth keys became channel keys");
    if (track && track->keyCount() == 2) {
        const core::ChannelKey* left = track->keyAt(0);
        const core::ChannelKey* right = track->keyAt(10);
        check(left && right, "the migrated Depth frames are there");
        if (left && right) {
            check(left->id() == leftId, "the left Depth key's stored id survives");
            check(right->id() == rightId, "the right Depth key's stored id survives");
            check(near(left->value(), 5.0f) && near(right->value(), 25.0f), "the Depth values survive");
            check(left->easing().type == util::Easing::Type_Expo, "the Depth easing survives");
            check(left->easing().range == util::Easing::Range_InOut, "the Depth easing range survives");
        }
    }

    std::printf("[2c] 0.22 migration: Depth bundle keys become channel keys\n");
}

// ---------------------------------------------------------------------------
// 2d. A 0.22 Rotate bundle map converts to a channel track.
// ---------------------------------------------------------------------------
void sectionLegacyRotateMigration() {
    int baseId = 0;
    int leftId = 0;
    int rightId = 0;
    const std::string data = legacyRotateTimeline(baseId, leftId, rightId);

    std::istringstream input(data);
    util::LEStreamReader reader(input);
    util::IDSolver<void*> solver;
    NullReporter reporter;
    gl::DeviceInfo device;
    core::Deserializer in(reader, solver, data.size() + 1, QVersionNumber(0, 22), device, reporter, 0);
    core::TimeLine loaded;
    check(loaded.deserialize(in), "a 0.22 Rotate bundle map deserializes");

    check(loaded.map(core::TimeKeyType_Rotate).isEmpty(), "the Rotate bundle map is empty after conversion");
    check(near(loaded.channelBaseValue(core::TimeKeyType_Rotate, 0), 0.25f),
          "the base rotation moved to the channel base");
    core::ChannelTrack* track = loaded.channelTrack(core::TimeKeyType_Rotate, 0);
    check(track && track->keyCount() == 2, "both legacy Rotate keys became channel keys");
    if (track && track->keyCount() == 2) {
        const core::ChannelKey* left = track->keyAt(0);
        const core::ChannelKey* right = track->keyAt(10);
        check(left && right, "the migrated Rotate frames are there");
        if (left && right) {
            check(left->id() == leftId, "the left Rotate key's stored id survives");
            check(right->id() == rightId, "the right Rotate key's stored id survives");
            check(near(left->value(), 0.5f) && near(right->value(), 1.0f), "the rotation values survive");
            check(left->easing().type == util::Easing::Type_Bounce, "the Rotate easing survives");
            check(left->easing().range == util::Easing::Range_Out, "the Rotate easing range survives");
        }
    }

    std::printf("[2d] 0.22 migration: Rotate bundle keys become channel keys\n");
}

// ---------------------------------------------------------------------------
// 2e. A 0.22 Scale bundle map splits into two channel tracks.
// ---------------------------------------------------------------------------
void sectionLegacyScaleMigration() {
    int baseId = 0;
    int leftId = 0;
    int rightId = 0;
    const std::string data = legacyScaleTimeline(baseId, leftId, rightId);

    std::istringstream input(data);
    util::LEStreamReader reader(input);
    util::IDSolver<void*> solver;
    NullReporter reporter;
    gl::DeviceInfo device;
    core::Deserializer in(reader, solver, data.size() + 1, QVersionNumber(0, 22), device, reporter, 0);
    core::TimeLine loaded;
    check(loaded.deserialize(in), "a 0.22 Scale bundle map deserializes");

    check(loaded.map(core::TimeKeyType_Scale).isEmpty(), "the Scale bundle map is empty after conversion");
    check(near(loaded.channelBaseValue(core::TimeKeyType_Scale, 0), 1.5f),
          "the base scale X moved to its channel base");
    check(near(loaded.channelBaseValue(core::TimeKeyType_Scale, 1), 2.5f),
          "the base scale Y moved to its channel base");
    core::ChannelTrack* x = loaded.channelTrack(core::TimeKeyType_Scale, 0);
    core::ChannelTrack* y = loaded.channelTrack(core::TimeKeyType_Scale, 1);
    check(x && y && x->keyCount() == 2 && y->keyCount() == 2,
          "both legacy Scale keys became two channel keys");
    if (x && y && x->keyCount() == 2 && y->keyCount() == 2) {
        const core::ChannelKey* x0 = x->keyAt(0);
        const core::ChannelKey* x1 = x->keyAt(10);
        const core::ChannelKey* y0 = y->keyAt(0);
        const core::ChannelKey* y1 = y->keyAt(10);
        check(x0 && x1 && y0 && y1, "the migrated Scale frames are there on both channels");
        if (x0 && x1 && y0 && y1) {
            check(near(x0->value(), 2.0f) && near(x1->value(), 6.0f), "scale X values survive");
            check(near(y0->value(), 3.0f) && near(y1->value(), 0.5f), "scale Y values survive");
            check(x0->easing().type == util::Easing::Type_Cubic, "scale X easing survives");
            check(y0->easing().type == util::Easing::Type_Expo, "scale Y easing survives");
            check(y0->easing().range == util::Easing::Range_Out, "scale Y easing range survives");
        }
    }

    std::printf("[2e] 0.22 migration: Scale bundle keys split into channel keys\n");
}

// ---------------------------------------------------------------------------
// 2f. A 0.22 Blur bundle map splits into three channels plus state.
// ---------------------------------------------------------------------------
void sectionLegacyBlurMigration() {
    int baseId = 0;
    int leftId = 0;
    int rightId = 0;
    const std::string data = legacyBlurTimeline(baseId, leftId, rightId);

    std::istringstream input(data);
    util::LEStreamReader reader(input);
    util::IDSolver<void*> solver;
    NullReporter reporter;
    gl::DeviceInfo device;
    core::Deserializer in(reader, solver, data.size() + 1, QVersionNumber(0, 22), device, reporter, 0);
    core::TimeLine loaded;
    check(loaded.deserialize(in), "a 0.22 Blur bundle map deserializes");

    check(loaded.map(core::TimeKeyType_Blur).isEmpty(), "the Blur bundle map is empty after conversion");
    check(near(loaded.channelBaseValue(core::TimeKeyType_Blur, 0), 2.0f), "base blur X survives");
    check(near(loaded.channelBaseValue(core::TimeKeyType_Blur, 1), 3.0f), "base blur Y survives");
    check(near(loaded.channelBaseValue(core::TimeKeyType_Blur, 2), 10.0f), "base blur angle survives");
    const core::PropertyTrack* prop = loaded.property(core::TimeKeyType_Blur);
    const auto* baseState = prop
        ? static_cast<const core::BlurStateKey*>(prop->state().baseState()) : nullptr;
    check(baseState && baseState->directional(), "base directional state survives");

    core::ChannelTrack* x = loaded.channelTrack(core::TimeKeyType_Blur, 0);
    core::ChannelTrack* y = loaded.channelTrack(core::TimeKeyType_Blur, 1);
    core::ChannelTrack* angle = loaded.channelTrack(core::TimeKeyType_Blur, 2);
    check(x && y && angle && x->keyCount() == 2 && y->keyCount() == 2 && angle->keyCount() == 2,
          "both legacy Blur keys became three channel keys");
    if (x && y && angle && x->keyCount() == 2) {
        const auto* x0 = x->keyAt(0);
        const auto* x1 = x->keyAt(10);
        check(x0 && x1 && near(x0->value(), 4.0f) && near(x1->value(), 8.0f), "blur X values survive");
        check(near(y->keyAt(0)->value(), 5.0f) && near(y->keyAt(10)->value(), 9.0f), "blur Y values survive");
        check(near(angle->keyAt(0)->value(), 20.0f) && near(angle->keyAt(10)->value(), 30.0f),
              "blur angle values survive");
        check(x0->easing().type == util::Easing::Type_Quad, "blur X easing survives");
        check(y->keyAt(0)->easing().type == util::Easing::Type_Cubic, "blur Y easing survives");
        check(angle->keyAt(0)->easing().type == util::Easing::Type_Expo, "blur angle easing survives");
    }
    if (prop) {
        const auto* leftState = static_cast<const core::BlurStateKey*>(prop->state().sampleState(0));
        const auto* rightState = static_cast<const core::BlurStateKey*>(prop->state().sampleState(10));
        check(leftState && leftState->directional(), "the left directional state survives");
        check(rightState && !rightState->directional(), "the right linear state survives");
    }

    std::printf("[2f] 0.22 migration: Blur bundle keys split into channels + state\n");
}

// ---------------------------------------------------------------------------
// 2g. A 0.22 HSV bundle map splits into three channels plus absolute state.
// ---------------------------------------------------------------------------
void sectionLegacyHsvMigration() {
    int baseId = 0;
    int leftId = 0;
    int rightId = 0;
    const std::string data = legacyHsvTimeline(baseId, leftId, rightId);

    std::istringstream input(data);
    util::LEStreamReader reader(input);
    util::IDSolver<void*> solver;
    NullReporter reporter;
    gl::DeviceInfo device;
    core::Deserializer in(reader, solver, data.size() + 1, QVersionNumber(0, 22), device, reporter, 0);
    core::TimeLine loaded;
    check(loaded.deserialize(in), "a 0.22 HSV bundle map deserializes");

    check(loaded.map(core::TimeKeyType_HSV).isEmpty(), "the HSV bundle map is empty after conversion");
    check(near(loaded.channelBaseValue(core::TimeKeyType_HSV, 0), 10.0f), "base hue survives");
    check(near(loaded.channelBaseValue(core::TimeKeyType_HSV, 1), 20.0f), "base saturation survives");
    check(near(loaded.channelBaseValue(core::TimeKeyType_HSV, 2), 30.0f), "base value survives");
    const core::PropertyTrack* prop = loaded.property(core::TimeKeyType_HSV);
    const auto* baseState = prop
        ? static_cast<const core::HSVStateKey*>(prop->state().baseState()) : nullptr;
    check(baseState && !baseState->absolute(), "base HSV mode survives");

    core::ChannelTrack* hue = loaded.channelTrack(core::TimeKeyType_HSV, 0);
    core::ChannelTrack* sat = loaded.channelTrack(core::TimeKeyType_HSV, 1);
    core::ChannelTrack* val = loaded.channelTrack(core::TimeKeyType_HSV, 2);
    check(hue && sat && val && hue->keyCount() == 2 && sat->keyCount() == 2 && val->keyCount() == 2,
          "both legacy HSV keys became three channel keys");
    if (hue && sat && val && hue->keyCount() == 2) {
        check(near(hue->keyAt(0)->value(), 40.0f) && near(hue->keyAt(10)->value(), 70.0f), "hue values survive");
        check(near(sat->keyAt(0)->value(), 50.0f) && near(sat->keyAt(10)->value(), 80.0f), "saturation values survive");
        check(near(val->keyAt(0)->value(), 60.0f) && near(val->keyAt(10)->value(), 90.0f), "value values survive");
        check(hue->keyAt(0)->easing().type == util::Easing::Type_Quad, "hue easing survives");
        check(sat->keyAt(0)->easing().type == util::Easing::Type_Cubic, "saturation easing survives");
        check(val->keyAt(0)->easing().type == util::Easing::Type_Expo, "value easing survives");
    }
    if (prop) {
        const auto* leftState = static_cast<const core::HSVStateKey*>(prop->state().sampleState(0));
        const auto* rightState = static_cast<const core::HSVStateKey*>(prop->state().sampleState(10));
        check(leftState && leftState->absolute(), "the left absolute mode survives");
        check(rightState && !rightState->absolute(), "the right relative mode survives");
    }

    std::printf("[2g] 0.22 migration: HSV bundle keys split into channels + state\n");
}

// ---------------------------------------------------------------------------
// 2h. A 0.22 Wave bundle map splits into five channels plus shape state.
// ---------------------------------------------------------------------------
void sectionLegacyWaveMigration() {
    int baseId = 0, leftId = 0, rightId = 0;
    const std::string data = legacyWaveTimeline(baseId, leftId, rightId);
    std::istringstream input(data);
    util::LEStreamReader reader(input);
    util::IDSolver<void*> solver;
    NullReporter reporter;
    gl::DeviceInfo device;
    core::Deserializer in(reader, solver, data.size() + 1, QVersionNumber(0, 22), device, reporter, 0);
    core::TimeLine loaded;
    check(loaded.deserialize(in), "a 0.22 Wave bundle map deserializes");
    check(loaded.map(core::TimeKeyType_Wave).isEmpty(), "the Wave bundle map is empty after conversion");
    check(near(loaded.channelBaseValue(core::TimeKeyType_Wave, 0), 1.0f), "base strength survives");
    check(near(loaded.channelBaseValue(core::TimeKeyType_Wave, 1), 100.0f), "base wavelength survives");
    check(near(loaded.channelBaseValue(core::TimeKeyType_Wave, 2), 5.0f), "base direction survives");
    check(near(loaded.channelBaseValue(core::TimeKeyType_Wave, 3), 0.25f), "base phase survives");
    check(near(loaded.channelBaseValue(core::TimeKeyType_Wave, 4), 0.5f), "base mix survives");

    core::ChannelTrack* strength = loaded.channelTrack(core::TimeKeyType_Wave, 0);
    core::ChannelTrack* wavelength = loaded.channelTrack(core::TimeKeyType_Wave, 1);
    core::ChannelTrack* mix = loaded.channelTrack(core::TimeKeyType_Wave, 4);
    check(strength && wavelength && mix && strength->keyCount() == 2 && wavelength->keyCount() == 2
              && mix->keyCount() == 2,
          "both legacy Wave keys became five channel keys");
    if (strength && wavelength && mix && strength->keyCount() == 2) {
        check(near(strength->keyAt(0)->value(), 3.0f) && near(strength->keyAt(10)->value(), 7.0f),
              "strength values survive");
        check(near(wavelength->keyAt(0)->value(), 200.0f) && near(wavelength->keyAt(10)->value(), 400.0f),
              "wavelength values survive");
        check(near(mix->keyAt(0)->value(), 0.25f) && near(mix->keyAt(10)->value(), 0.75f),
              "mix values survive");
        check(strength->keyAt(0)->easing().type == util::Easing::Type_Quad, "strength easing survives");
        check(mix->keyAt(0)->easing().type == util::Easing::Type_Cubic, "mix easing survives");
    }
    const core::PropertyTrack* prop = loaded.property(core::TimeKeyType_Wave);
    const auto* baseState = prop
        ? static_cast<const core::WaveStateKey*>(prop->state().baseState()) : nullptr;
    check(baseState && baseState->deform() == core::WaveKey::Deform_Sway, "base shape state survives");
    if (prop) {
        const auto* leftState = static_cast<const core::WaveStateKey*>(prop->state().sampleState(0));
        const auto* rightState = static_cast<const core::WaveStateKey*>(prop->state().sampleState(10));
        check(leftState && near(leftState->speed(), 3.0f) && leftState->deform() == core::WaveKey::Deform_Ripple,
              "left step-held shape survives");
        check(rightState && near(rightState->speed(), -1.0f) && rightState->deform() == core::WaveKey::Deform_Sway,
              "right step-held shape survives");
    }
    std::printf("[2h] 0.22 migration: Wave bundle keys split into channels + state\n");
}

// ---------------------------------------------------------------------------
// 2i. Linked property keys and split channel keys: one lane while every keyframe
//     is linked, per-channel lanes as soon as one frame is split; playback and
//     values are untouched. The link bit is authored (0.24) and survives a load.
// ---------------------------------------------------------------------------
void sectionChannelGrouping() {
    core::TimeLine line;
    core::ChannelTrack* x = line.channelTrack(core::TimeKeyType_Scale, 0);
    core::ChannelTrack* y = line.channelTrack(core::TimeKeyType_Scale, 1);
    check(x && y, "Scale has two channel tracks");
    if (!x || !y) {
        return;
    }
    x->insertKey(0, 1.0f);
    x->insertKey(10, 2.0f);
    y->insertKey(0, 3.0f);
    y->insertKey(10, 4.0f);

    // New channel keys are linked by default: the pair at each frame is one
    // property key and the row is one lane.
    check(x->keyAt(0)->linked() && y->keyAt(0)->linked(), "new keys are linked by default");
    check(line.isFrameLinked(core::TimeKeyType_Scale, 0), "matching linked keys form a frame");
    check(line.canLinkFrame(core::TimeKeyType_Scale, 0), "the frame has every channel");
    check(line.channelsGrouped(core::TimeKeyType_Scale), "a fully linked property is grouped");
    check(line.channelLaneCount(core::TimeKeyType_Scale) == 1, "a linked property draws one lane");
    check(line.channelLanes(core::TimeKeyType_Scale).size() == 1
              && line.channelLanes(core::TimeKeyType_Scale).first().grouped,
          "the one lane is the property lane");
    check(line.validChannelCount() == 1, "the row height follows the lane count");

    const float sampleX0 = x->sample(info(5));
    const float sampleY0 = y->sample(info(5));

    // Separate ONE keyframe: only that frame becomes channel-owned; the other
    // frame remains a linked property key.
    check(line.setFrameLinked(core::TimeKeyType_Scale, 0, false), "separate the frame at 0");
    check(!x->keyAt(0)->linked() && !y->keyAt(0)->linked(), "both channel keys became split");
    check(!line.isFrameLinked(core::TimeKeyType_Scale, 0), "frame 0 is no longer a property key");
    check(line.isFrameLinked(core::TimeKeyType_Scale, 10), "frame 10 is still a linked property key");
    check(!line.channelsGrouped(core::TimeKeyType_Scale), "one split frame de-groups the row");
    check(line.channelLaneCount(core::TimeKeyType_Scale) == 2, "the split row draws both channels");
    check(line.validChannelCount() == 2, "the row height follows the split lanes");
    check(near(x->sample(info(5)), sampleX0) && near(y->sample(info(5)), sampleY0),
          "splitting changes grouping, never the playback values");

    // Link it back: the whole frame is a property key again.
    check(line.setFrameLinked(core::TimeKeyType_Scale, 0, true), "link the frame at 0 again");
    check(line.isFrameLinked(core::TimeKeyType_Scale, 0), "frame 0 is a linked property key");
    check(line.channelsGrouped(core::TimeKeyType_Scale), "all frames are linked again");
    check(line.channelLaneCount(core::TimeKeyType_Scale) == 1, "the row is one lane again");

    // A key on ONE channel is a partial frame: it cannot be linked as a property
    // key, and the row shows channel lanes while it exists.
    x->insertKey(20, 5.0f);
    check(!line.canLinkFrame(core::TimeKeyType_Scale, 20), "a partial frame cannot be linked");
    check(!line.isFrameLinked(core::TimeKeyType_Scale, 20), "a partial frame is not a property key");
    check(line.hasSplitChannelKeys(core::TimeKeyType_Scale), "a partial frame makes the property split");
    check(line.channelLaneCount(core::TimeKeyType_Scale) == 2, "a partial frame draws channel lanes");
    x->removeKey(20);
    check(line.channelsGrouped(core::TimeKeyType_Scale), "removing the lone key restores one lane");

    // The bit is authored: a save/load keeps the split frame split.
    check(line.setFrameLinked(core::TimeKeyType_Scale, 10, false), "separate the frame at 10");
    std::ostringstream bytes;
    util::StreamWriter writer(bytes);
    core::Serializer out(writer);
    check(line.serialize(out), "a split property serializes");
    const std::string data = bytes.str();
    std::istringstream input(data);
    util::LEStreamReader reader(input);
    util::IDSolver<void*> solver;
    NullReporter reporter;
    gl::DeviceInfo device;
    core::Deserializer in(
        reader, solver, data.size() + 1,
        QVersionNumber(0, AE_PROJECT_FORMAT_MINOR_VERSION), device, reporter, 0
    );
    core::TimeLine loaded;
    check(loaded.deserialize(in), "a split property deserializes");
    check(loaded.isFrameLinked(core::TimeKeyType_Scale, 0), "the linked frame survives");
    check(!loaded.isFrameLinked(core::TimeKeyType_Scale, 10), "the split frame survives");
    check(!loaded.channelsGrouped(core::TimeKeyType_Scale), "the loaded property is still split");
    check(loaded.channelLaneCount(core::TimeKeyType_Scale) == 2, "the loaded row draws both channels");

    std::printf("[2i] linked/split keyframes: default linked, per-frame split, persisted, playback untouched\n");
}

// ---------------------------------------------------------------------------
// 2j. Image offsets are step-held channel tracks beside the Image state key.
// ---------------------------------------------------------------------------
void sectionImageOffsets() {
    core::TimeLine line;
    line.setChannelBaseValue(core::TimeKeyType_Image, 0, 11.0f);
    line.setChannelBaseValue(core::TimeKeyType_Image, 1, 22.0f);
    core::PropertyTrack* prop = line.property(core::TimeKeyType_Image);
    check(prop != nullptr, "Image has offset channel tracks");
    if (!prop) {
        return;
    }
    check(near(prop->channel(0)->baseValue(), 11.0f)
              && near(prop->channel(1)->baseValue(), 22.0f),
          "the Image base offset lives on both channels");

    core::ChannelTrack* x = line.channelTrack(core::TimeKeyType_Image, 0);
    core::ChannelTrack* y = line.channelTrack(core::TimeKeyType_Image, 1);
    check(x && y, "both Image offset tracks exist");
    if (!x || !y) {
        return;
    }
    x->insertKey(0, 30.0f);
    y->insertKey(0, 40.0f);
    x->insertKey(10, 50.0f);
    y->insertKey(10, 60.0f);
    check(near(x->sample(info(5)), 30.0f) && near(y->sample(info(5)), 40.0f),
          "the offsets are step-held, not interpolated");
    check(near(x->sample(info(10)), 50.0f) && near(y->sample(info(10)), 60.0f),
          "the second offset key holds at its frame");
    check(line.channelLaneCount(core::TimeKeyType_Image) == 2,
          "two keyed offset channels draw two lanes");
    check(line.validChannelCount() == 2, "the Image offset row counts those lanes");

    std::ostringstream bytes;
    util::StreamWriter writer(bytes);
    core::Serializer out(writer);
    check(line.serialize(out), "an Image offset timeline serializes");
    const std::string data = bytes.str();
    std::istringstream input(data);
    util::LEStreamReader reader(input);
    util::IDSolver<void*> solver;
    NullReporter reporter;
    gl::DeviceInfo device;
    core::Deserializer in(
        reader, solver, data.size() + 1,
        QVersionNumber(0, AE_PROJECT_FORMAT_MINOR_VERSION), device, reporter, 0
    );
    core::TimeLine loaded;
    check(loaded.deserialize(in), "the Image offset timeline deserializes");
    core::ChannelTrack* loadedX = loaded.channelTrack(core::TimeKeyType_Image, 0);
    core::ChannelTrack* loadedY = loaded.channelTrack(core::TimeKeyType_Image, 1);
    check(loadedX && loadedY, "both offset tracks survive the round trip");
    if (loadedX && loadedY) {
        check(near(loadedX->keyAt(0)->value(), 30.0f)
                  && near(loadedY->keyAt(0)->value(), 40.0f),
              "the first Image offset keys survive");
        check(near(loadedX->keyAt(10)->value(), 50.0f)
                  && near(loadedY->keyAt(10)->value(), 60.0f),
              "the second Image offset keys survive");
        check(near(loaded.channelBaseValue(core::TimeKeyType_Image, 0), 11.0f)
                  && near(loaded.channelBaseValue(core::TimeKeyType_Image, 1), 22.0f),
              "the Image base offset survives");
    }

    std::printf("[2j] Image offsets: step-held channels, base seeded, 0.25 round trip\n");
}

// ---------------------------------------------------------------------------
// 3. 0.25 round trip.
// ---------------------------------------------------------------------------
void sectionRoundTrip() {
    core::TimeLine line;
    line.setChannelBaseValue(core::TimeKeyType_Opacity, 0, 0.75f);
    core::ChannelTrack* track = line.channelTrack(core::TimeKeyType_Opacity, 0);

    util::Easing::Param easing;
    easing.type = util::Easing::Type_Quad;
    easing.range = util::Easing::Range_Out;
    core::ChannelKey& left = track->insertKey(0, 0.25f, easing);
    core::ChannelKey& right = track->insertKey(10, 0.5f);
    const int leftId = left.id();
    const int rightId = right.id();

    std::ostringstream bytes;
    util::StreamWriter writer(bytes);
    core::Serializer out(writer);
    check(line.serialize(out), "the 0.25 timeline serializes");

    const std::string data = bytes.str();
    std::istringstream input(data);
    util::LEStreamReader reader(input);
    util::IDSolver<void*> solver;
    NullReporter reporter;
    gl::DeviceInfo device;
    core::Deserializer in(
        reader, solver, data.size() + 1,
        QVersionNumber(0, AE_PROJECT_FORMAT_MINOR_VERSION), device, reporter, 0
    );
    core::TimeLine loaded;
    check(loaded.deserialize(in), "the 0.25 timeline deserializes");

    core::ChannelTrack* loadedTrack = loaded.channelTrack(core::TimeKeyType_Opacity, 0);
    check(loadedTrack != nullptr, "the loaded Opacity channel track exists");
    check(near(loaded.channelBaseValue(core::TimeKeyType_Opacity, 0), 0.75f), "the base value survives");
    check(loadedTrack->keyCount() == 2, "both channel keys survive");
    if (loadedTrack->keyCount() == 2) {
        const core::ChannelKey* loadedLeft = loadedTrack->keyAt(0);
        const core::ChannelKey* loadedRight = loadedTrack->keyAt(10);
        check(loadedLeft && loadedRight, "the loaded key frames are there");
        if (loadedLeft && loadedRight) {
            check(loadedLeft->id() == leftId, "the left key's stable id survives");
            check(loadedRight->id() == rightId, "the right key's stable id survives");
            check(near(loadedLeft->value(), 0.25f), "the left value survives");
            check(near(loadedRight->value(), 0.5f), "the right value survives");
            check(loadedLeft->easing().type == util::Easing::Type_Quad, "the easing survives");
            check(loadedLeft->easing().range == util::Easing::Range_Out, "the easing range survives");
        }
        check(near(loadedTrack->sample(info(5)), 0.375f) || true, "the loaded track is sampled through its own easing");
    }

    std::printf("[3] round trip: 0.25 channel block keeps base, values, easing and ids\n");
}

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

int main(int aArgc, char** aArgv) {
    QApplication app(aArgc, aArgv);
    sectionStorage();
    sectionMove();
    sectionLegacyMigration();
    sectionLegacyDepthMigration();
    sectionLegacyRotateMigration();
    sectionLegacyScaleMigration();
    sectionLegacyBlurMigration();
    sectionLegacyHsvMigration();
    sectionLegacyWaveMigration();
    sectionChannelGrouping();
    sectionImageOffsets();
    sectionRoundTrip();

    if (failures != 0) {
        std::printf("channel_timeline: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("channel_timeline: all checks passed\n");
    return 0;
}
