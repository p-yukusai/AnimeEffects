#ifndef CORE_CHANNELSPEC_H
#define CORE_CHANNELSPEC_H

#include <limits>
#include "core/TimeKeyType.h"

namespace core {

// How one scalar channel of one property behaves. This is the metadata the
// timeline, graph, inspector, blender and serialization all read.
//
// A property may have more channels than it has graphable components: Move's
// centroid X/Y interpolate like any value, but they are not drawn in the graph.
// core::easingComponentCount remains the count of GRAPHABLE components that the
// legacy key Data sizes its easing arrays from; propertyChannelCount() is the
// full count of keyable scalar channels.
enum class ChannelInterpolation {
    Eased, // interpolate between keys; the key's easing param shapes the segment
    Step,  // hold the last key at or before the frame (Image's offset)
};

struct ChannelSpec {
    bool valid = false;
    bool graphable = false;
    bool integral = false;
    ChannelInterpolation interpolation = ChannelInterpolation::Eased;
    float minValue = std::numeric_limits<float>::lowest();
    float maxValue = std::numeric_limits<float>::max();
    float defaultValue = 0.0f;
};

// The number of keyable scalar channels of a property. This can exceed
// easingComponentCount: Move has four (position X/Y and centroid X/Y), of which
// only the first two are graphable. Structural types have 0.
int propertyChannelCount(TimeKeyType aType);

// The spec of one channel. `valid` is false for a structural type or a component
// past propertyChannelCount(). A hidden channel (Move's centroid) is valid but
// has graphable == false.
ChannelSpec channelSpec(TimeKeyType aType, int aComponent);

// True when the type publishes channel tracks at all.
bool hasChannelTracks(TimeKeyType aType);

} // namespace core

#endif // CORE_CHANNELSPEC_H
