#ifndef CORE_KEYEASINGACCESS_H
#define CORE_KEYEASINGACCESS_H

#include "core/BlurKey.h"
#include "core/ChannelKey.h"
#include "core/DepthKey.h"
#include "core/FFDKey.h"
#include "core/HsvKey.h"
#include "core/ImageKey.h"
#include "core/MoveKey.h"
#include "core/OpacityKey.h"
#include "core/PoseKey.h"
#include "core/RotateKey.h"
#include "core/ScaleKey.h"
#include "core/WaveKey.h"
#include "util/Easing.h"

namespace core {

// THE one key -> easing param bridge. Every reader and writer of a key's easing
// goes through it (the graph's Curve, the timeline's picker/chip, the selection
// easing action), so a key type that carries an easing cannot be writable in one
// place and unreadable in another - which is what happened when the graph's own
// table answered null for Pose/FFD while the timeline's local table did not.
//
// aComponent is the component index of the TYPE (core/KeyEasing.h's order):
//   * a graphable type returns that component's own param;
//   * Pose and FFD store ONE shared param at index 0 (they publish no graph
//     channel, but their easing shapes the interval playback plays);
//   * an Image key returns its stored (step-held) pair, which playback does not
//     consult but the chip/picker may read and write;
//   * a type that carries no easing (Bone, Mesh) answers null, as does any
//     out-of-range component.
inline util::Easing::Param* keyEasingOf(TimeKey& aKey, const TimeKeyType aType, const int aComponent) {
    if (auto* channelKey = dynamic_cast<ChannelKey*>(&aKey)) {
        return channelKey->component() == aComponent ? &channelKey->easing() : nullptr;
    }
    switch (aType) {
    case TimeKeyType_Move:
        return &static_cast<MoveKey&>(aKey).data().easing(aComponent);
    case TimeKeyType_Rotate:
        return &static_cast<RotateKey&>(aKey).data().easing(aComponent);
    case TimeKeyType_Scale:
        return &static_cast<ScaleKey&>(aKey).data().easing(aComponent);
    case TimeKeyType_Depth:
        return &static_cast<DepthKey&>(aKey).data().easing(aComponent);
    case TimeKeyType_Opacity:
        return &static_cast<OpacityKey&>(aKey).data().easing(aComponent);
    case TimeKeyType_Blur:
        return &static_cast<BlurKey&>(aKey).data().easing(aComponent);
    case TimeKeyType_HSV:
        return &static_cast<HSVKey&>(aKey).data().easing(aComponent);
    case TimeKeyType_Wave:
        return &static_cast<WaveKey&>(aKey).data().easing(aComponent);
    case TimeKeyType_Image:
        return &static_cast<ImageKey&>(aKey).data().easing(aComponent);
    case TimeKeyType_Pose:
        return &static_cast<PoseKey&>(aKey).data().easing(0);
    case TimeKeyType_FFD:
        return &static_cast<FFDKey&>(aKey).data().easing(0);
    default:
        return nullptr;
    }
}

inline const util::Easing::Param* keyEasingOf(
    const TimeKey& aKey, const TimeKeyType aType, const int aComponent) {
    if (const auto* channelKey = dynamic_cast<const ChannelKey*>(&aKey)) {
        return channelKey->component() == aComponent ? &channelKey->easing() : nullptr;
    }
    switch (aType) {
    case TimeKeyType_Move:
        return &static_cast<const MoveKey&>(aKey).data().easing(aComponent);
    case TimeKeyType_Rotate:
        return &static_cast<const RotateKey&>(aKey).data().easing(aComponent);
    case TimeKeyType_Scale:
        return &static_cast<const ScaleKey&>(aKey).data().easing(aComponent);
    case TimeKeyType_Depth:
        return &static_cast<const DepthKey&>(aKey).data().easing(aComponent);
    case TimeKeyType_Opacity:
        return &static_cast<const OpacityKey&>(aKey).data().easing(aComponent);
    case TimeKeyType_Blur:
        return &static_cast<const BlurKey&>(aKey).data().easing(aComponent);
    case TimeKeyType_HSV:
        return &static_cast<const HSVKey&>(aKey).data().easing(aComponent);
    case TimeKeyType_Wave:
        return &static_cast<const WaveKey&>(aKey).data().easing(aComponent);
    case TimeKeyType_Image:
        return &static_cast<const ImageKey&>(aKey).data().easing(aComponent);
    case TimeKeyType_Pose:
        return &static_cast<const PoseKey&>(aKey).data().easing(0);
    case TimeKeyType_FFD:
        return &static_cast<const FFDKey&>(aKey).data().easing(0);
    default:
        return nullptr;
    }
}

} // namespace core

#endif // CORE_KEYEASINGACCESS_H
