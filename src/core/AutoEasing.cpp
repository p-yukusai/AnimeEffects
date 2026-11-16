#include "core/AutoEasing.h"

#include <algorithm>
#include <cmath>

#include "core/ChannelKey.h"
#include "core/TimeKey.h"
#include "core/MoveKey.h"
#include "core/RotateKey.h"
#include "core/ScaleKey.h"
#include "core/DepthKey.h"
#include "core/OpacityKey.h"
#include "core/HsvKey.h"
#include "core/BlurKey.h"
#include "core/WaveKey.h"
#include "core/PoseKey.h"
#include "core/FFDKey.h"
#include "core/Bone2.h"

namespace {

// The component an Auto handle reads: the one that moves most between the
// segment's two bounding keys. Ties - and a segment that does not move at all -
// stay on the first component considered.
struct Dominant {
    int index = 0;
    float change = 0.0f;

    void consider(int aIndex, float aFrom, float aTo) {
        const float d = std::fabs(aTo - aFrom);
        if (d > change) {
            change = d;
            index = aIndex;
        }
    }
};

// A pose key's bone rotations in the order core::TimeKeyBlender blends them
// (each top bone's descendants, depth first, the top bone itself excluded), so
// the same index names the same bone on every key of the channel.
QVector<float> flattenPose(const core::PoseKey& aKey) {
    QVector<float> out;
    for (const core::Bone2* top : aKey.data().topBones()) {
        for (core::Bone2::ConstIterator itr(top); itr.hasNext();) {
            out.append(itr.next()->rotate());
        }
    }
    return out;
}

// One key's easing param for a graphable component, or null for a key type that
// carries none. Used to read the TO key's incoming handle mode for the segment's
// arriving side; the dispatch is the same one valueOf() makes.
const util::Easing::Param* paramOf(const core::TimeKey& aKey, int aComponent) {
    if (const auto* channelKey = dynamic_cast<const core::ChannelKey*>(&aKey)) {
        return channelKey->component() == aComponent ? &channelKey->easing() : nullptr;
    }
    switch (aKey.type()) {
    case core::TimeKeyType_Move:
        return &static_cast<const core::MoveKey&>(aKey).data().easing(aComponent);
    case core::TimeKeyType_Scale:
        return &static_cast<const core::ScaleKey&>(aKey).data().easing(aComponent);
    case core::TimeKeyType_Blur:
        return &static_cast<const core::BlurKey&>(aKey).data().easing(aComponent);
    case core::TimeKeyType_Wave:
        return &static_cast<const core::WaveKey&>(aKey).data().easing(aComponent);
    case core::TimeKeyType_HSV:
        return &static_cast<const core::HSVKey&>(aKey).data().easing(aComponent);
    case core::TimeKeyType_Rotate:
        return &static_cast<const core::RotateKey&>(aKey).data().easing(aComponent);
    case core::TimeKeyType_Depth:
        return &static_cast<const core::DepthKey&>(aKey).data().easing(aComponent);
    case core::TimeKeyType_Opacity:
        return &static_cast<const core::OpacityKey&>(aKey).data().easing(aComponent);
    case core::TimeKeyType_Pose:
        return &static_cast<const core::PoseKey&>(aKey).data().easing(aComponent);
    case core::TimeKeyType_FFD:
        return &static_cast<const core::FFDKey&>(aKey).data().easing(aComponent);
    default:
        return nullptr;
    }
}

bool valueOf(
    const core::TimeKey& aKey, const core::TimeKey& aFrom, const core::TimeKey& aTo, int aComponent, float& aOut) {
    // The caller pairs keys out of one channel's map, but a hand-built call must
    // not read a Move key through a Scale cast.
    if (aKey.type() != aFrom.type() || aKey.type() != aTo.type())
        return false;

    if (const auto* channelKey = dynamic_cast<const core::ChannelKey*>(&aKey)) {
        if (channelKey->component() != aComponent) {
            return false;
        }
        aOut = channelKey->value();
        return true;
    }

    switch (aKey.type()) {
    case core::TimeKeyType_Move: {
        if (aComponent != 0 && aComponent != 1)
            return false;
        const QVector2D val = static_cast<const core::MoveKey&>(aKey).data().pos();
        aOut = (aComponent == 0) ? val.x() : val.y();
        return true;
    }
    case core::TimeKeyType_Scale: {
        if (aComponent != 0 && aComponent != 1)
            return false;
        const QVector2D val = static_cast<const core::ScaleKey&>(aKey).data().scale();
        aOut = (aComponent == 0) ? val.x() : val.y();
        return true;
    }
    case core::TimeKeyType_Blur: {
        const core::BlurKey::Data& val = static_cast<const core::BlurKey&>(aKey).data();
        switch (aComponent) {
        case 0: aOut = val.blurX(); return true;
        case 1: aOut = val.blurY(); return true;
        case 2: aOut = val.angleDeg(); return true;
        default: return false;
        }
    }
    case core::TimeKeyType_Wave: {
        const core::WaveKey::Data& val = static_cast<const core::WaveKey&>(aKey).data();
        switch (aComponent) {
        case 0: aOut = val.strength(); return true;
        case 1: aOut = val.wavelength(); return true;
        case 2: aOut = val.angleDeg(); return true;
        case 3: aOut = val.phase(); return true;
        case 4: aOut = val.mix(); return true;
        default: return false;
        }
    }
    case core::TimeKeyType_HSV: {
        const QList<int>& val = static_cast<const core::HSVKey&>(aKey).data().hsv();
        // Playback blends the first three entries; the fourth is a flag.
        if (val.size() < 3 || aComponent < 0 || 2 < aComponent)
            return false;
        aOut = static_cast<float>(val.at(aComponent));
        return true;
    }
    case core::TimeKeyType_Rotate:
        if (aComponent != 0)
            return false;
        aOut = static_cast<const core::RotateKey&>(aKey).data().rotate();
        return true;
    case core::TimeKeyType_Depth:
        if (aComponent != 0)
            return false;
        aOut = static_cast<const core::DepthKey&>(aKey).data().depth();
        return true;
    case core::TimeKeyType_Opacity:
        if (aComponent != 0)
            return false;
        aOut = static_cast<const core::OpacityKey&>(aKey).data().opacity();
        return true;
    case core::TimeKeyType_Pose: {
        // Pose carries ONE easing for the whole key. Its derived shape reads the
        // bone that moves most, so a single rotating bone is not shaped by a
        // still one.
        if (aComponent != 0)
            return false;
        const QVector<float> from = flattenPose(static_cast<const core::PoseKey&>(aFrom));
        const QVector<float> to = flattenPose(static_cast<const core::PoseKey&>(aTo));
        const QVector<float> val = flattenPose(static_cast<const core::PoseKey&>(aKey));
        if (from.isEmpty() || from.size() != to.size() || from.size() != val.size())
            return false;
        Dominant d;
        for (int i = 0; i < from.size(); ++i) {
            d.consider(i, from.at(i), to.at(i));
        }
        aOut = val.at(d.index);
        return true;
    }
    case core::TimeKeyType_FFD: {
        // FFD carries ONE easing for the whole mesh: the vertex AND axis that
        // moves most across the segment decides.
        if (aComponent != 0)
            return false;
        const core::FFDKey::Data& from = static_cast<const core::FFDKey&>(aFrom).data();
        const core::FFDKey::Data& to = static_cast<const core::FFDKey&>(aTo).data();
        const core::FFDKey::Data& val = static_cast<const core::FFDKey&>(aKey).data();
        const int count = std::min(from.count(), std::min(to.count(), val.count()));
        if (count <= 0)
            return false;
        // The vertex AND axis that moves most across the segment decides, so a
        // deformation that travels in y is not shaped by a still x.
        Dominant d;
        for (int i = 0; i < count; ++i) {
            const gl::Vector3& f = from.positions()[i];
            const gl::Vector3& t = to.positions()[i];
            d.consider(i * 3 + 0, f.x, t.x);
            d.consider(i * 3 + 1, f.y, t.y);
            d.consider(i * 3 + 2, f.z, t.z);
        }
        const gl::Vector3& v = val.positions()[d.index / 3];
        switch (d.index % 3) {
        case 0: aOut = v.x; break;
        case 1: aOut = v.y; break;
        default: aOut = v.z; break;
        }
        return true;
    }
    default:
        // Bone and mesh carry no easing at all; image's offset is step-held, so
        // it has no rate for an Auto handle to shape.
        return false;
    }
}

} // namespace

namespace core {

bool autoEasingValue(const TimeKey& aKey, const TimeKey& aFrom, const TimeKey& aTo, int aComponent, float& aOut) {
    return valueOf(aKey, aFrom, aTo, aComponent, aOut);
}

util::Easing::AutoContext autoEasingContext(
    const TimeKey& aFrom, const TimeKey& aTo, int aComponent, const TimeKey* aPrev, const TimeKey* aNext) {
    util::Easing::AutoContext ctx;

    // The arriving side of the segment belongs to the TO key, so its mode is
    // read there (its stored value is still the FROM param's x2/y2). Filling it
    // here means every caller - playback, the graph, the picker previews - gets
    // the same pair without a second lookup.
    if (const util::Easing::Param* toEasing = paramOf(aTo, aComponent)) {
        ctx.inMode = toEasing->handleIn;
    }

    float from = 0.0f, to = 0.0f;
    if (!valueOf(aFrom, aFrom, aTo, aComponent, from) || !valueOf(aTo, aFrom, aTo, aComponent, to))
        return ctx;

    // The segment's own change is the axis the neighbours are expressed in. A
    // channel that does not change across the segment has no shape to derive.
    const float change = to - from;
    if (change == 0.0f)
        return ctx;

    const float fromFrame = static_cast<float>(aFrom.frame());
    auto fill = [&](const TimeKey* aKey, util::Easing::AutoKey& aOut) {
        float value = 0.0f;
        if (!aKey || !valueOf(*aKey, aFrom, aTo, aComponent, value))
            return;
        aOut.valid = true;
        aOut.frame = static_cast<float>(aKey->frame()) - fromFrame;
        aOut.value = (value - from) / change;
    };
    fill(aPrev, ctx.prev);
    fill(aNext, ctx.next);

    return ctx;
}

} // namespace core
