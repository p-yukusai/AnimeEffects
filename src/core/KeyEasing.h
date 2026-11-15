#ifndef CORE_KEYEASING_H
#define CORE_KEYEASING_H

#include "core/TimeKeyType.h"

namespace core {

// The number of independent easing components one key of aType stores, which is
// also the number of animation-graph channels that type publishes.
//
// THIS IS THE SINGLE SOURCE of that number. Every key Data sizes its
// `std::array<util::Easing::Param, kComponents>` from it, and
// ctrl::graph::Channel::componentCount() returns it, so adding a component to a
// key type is one edit here (the value read/write in graph_Channel.cpp and the
// blend in TimeKeyBlender.cpp are the other halves of that component, not a
// second declaration of the count).
//
// The order is the component numbering used everywhere - Channel::value(), its
// value write, playback's blend and the on-disk layout:
//   Move   : 0 pos x, 1 pos y
//   Rotate : 0 angle
//   Scale  : 0 x, 1 y
//   Depth  : 0
//   Opacity    : 0
//   Blur   : 0 blurX, 1 blurY, 2 angleDeg
//   HSV    : 0 hue, 1 saturation, 2 value
//   Wave   : 0 strength, 1 wavelength, 2 angleDeg, 3 phase, 4 mix
//   Image  : 0 offset x, 1 offset y
//
// Bone and Mesh are structural and carry no easing at all. Pose and FFD are
// structural too: each carries ONE easing that shapes the whole key (see
// PoseKey::kComponents / FFDKey::kComponents, both 1), so they publish no graph
// channel and count 0 here.
constexpr int easingComponentCount(TimeKeyType aType) {
    switch (aType) {
    case TimeKeyType_Move: return 2;
    case TimeKeyType_Rotate: return 1;
    case TimeKeyType_Scale: return 2;
    case TimeKeyType_Depth: return 1;
    case TimeKeyType_Opacity: return 1;
    case TimeKeyType_Blur: return 3;
    case TimeKeyType_HSV: return 3;
    case TimeKeyType_Wave: return 5;
    case TimeKeyType_Image: return 2;
    default: return 0;
    }
}

// The number of easing params a key of aType STORES: the graphable count, plus
// one for the structural Pose and FFD keys, whose single param shapes the whole
// key. This is what a key Data arrays and what a whole-key easing edit (the
// timeline header's chip) iterates; the graph uses easingComponentCount, which
// is 0 for those two because they publish no channel.
constexpr int easingStorageCount(TimeKeyType aType) {
    // Structural keys shape a whole key with ONE easing, addressed at index 0:
    // a pose blends bones and a mask blends a FIELD, neither of which has a
    // component axis to ease separately.
    return (aType == TimeKeyType_Pose || aType == TimeKeyType_FFD || aType == TimeKeyType_Mask)
               ? 1
               : easingComponentCount(aType);
}

} // namespace core

#endif // CORE_KEYEASING_H
