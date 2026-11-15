#ifndef CORE_PROPERTYTRACK_H
#define CORE_PROPERTYTRACK_H

#include <array>
#include <memory>

#include "core/ChannelTrack.h"
#include "core/KeyProportional.h"
#include "core/StateTrack.h"

namespace core {

// One property's authored animation: its scalar channel tracks and its discrete
// state track. A property is a CONTAINER, not a key. Property-level operations
// (the shared dot, "Separate channels", a transform drag) are groups of channel
// and state edits, not a stored property-key object.
class PropertyTrack {
public:
    static constexpr int kMaxChannels = KeyProportional::kMaxComponents;

    PropertyTrack();
    explicit PropertyTrack(TimeKeyType aType);

    PropertyTrack(const PropertyTrack&) = delete;
    PropertyTrack& operator=(const PropertyTrack&) = delete;
    PropertyTrack(PropertyTrack&&) = default;
    PropertyTrack& operator=(PropertyTrack&&) = default;

    TimeKeyType type() const { return mType; }
    int channelCount() const { return mChannelCount; }

    ChannelTrack* channel(int aComponent);
    const ChannelTrack* channel(int aComponent) const;
    const std::array<ChannelTrack, kMaxChannels>& channels() const { return mChannels; }

    StateTrack& state() { return mState; }
    const StateTrack& state() const { return mState; }

    bool isEmpty() const;
    float sampleChannel(int aComponent, const TimeInfo& aTime) const;

    // Deep copy of every channel key and state. The copy gets fresh channel/state
    // ids; the caller sets the destination frames (a paste/duplicate).
    std::unique_ptr<PropertyTrack> clone() const;
    // Clone every key and the state into an existing track of the same type
    // (a node duplicate: fresh key ids, no proportional weights).
    void copyKeysTo(PropertyTrack& aDest) const;

private:
    TimeKeyType mType;
    int mChannelCount;
    std::array<ChannelTrack, kMaxChannels> mChannels;
    StateTrack mState;
};

} // namespace core

#endif // CORE_PROPERTYTRACK_H
