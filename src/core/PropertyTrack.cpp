#include "core/PropertyTrack.h"

#include "core/ChannelSpec.h"

namespace core {

PropertyTrack::PropertyTrack():
    mType(TimeKeyType_TERM), mChannelCount(0), mChannels(), mState(TimeKeyType_TERM) {}

PropertyTrack::PropertyTrack(TimeKeyType aType):
    mType(aType), mChannelCount(propertyChannelCount(aType)), mChannels(), mState(aType) {
    for (int i = 0; i < mChannelCount; ++i) {
        mChannels[i] = ChannelTrack(aType, i);
    }
}

ChannelTrack* PropertyTrack::channel(int aComponent) {
    if (aComponent < 0 || mChannelCount <= aComponent) {
        return nullptr;
    }
    return &mChannels[aComponent];
}

const ChannelTrack* PropertyTrack::channel(int aComponent) const {
    if (aComponent < 0 || mChannelCount <= aComponent) {
        return nullptr;
    }
    return &mChannels[aComponent];
}

bool PropertyTrack::isEmpty() const {
    if (!mState.isEmpty()) {
        return false;
    }
    for (int i = 0; i < mChannelCount; ++i) {
        if (!mChannels[i].isEmpty()) {
            return false;
        }
    }
    return true;
}

float PropertyTrack::sampleChannel(int aComponent, const TimeInfo& aTime) const {
    const ChannelTrack* track = channel(aComponent);
    return track ? track->sample(aTime) : 0.0f;
}

std::unique_ptr<PropertyTrack> PropertyTrack::clone() const {
    std::unique_ptr<PropertyTrack> copy(new PropertyTrack(mType));

    // A clone is a new authored object: its keys get fresh ids and no
    // proportional weights (the destination's bounds are resolved lazily), the
    // same rule TimeKey::createClone follows. Values and easing travel.
    for (int i = 0; i < mChannelCount; ++i) {
        const ChannelTrack& source = mChannels[i];
        for (auto itr = source.keys().constBegin(); itr != source.keys().constEnd(); ++itr) {
            auto* sourceKey = static_cast<ChannelKey*>(itr.value());
            auto* clone = static_cast<ChannelKey*>(sourceKey->createClone());
            clone->setFrame(itr.key());
            copy->mChannels[i].adoptKey(clone);
        }
    }

    std::unique_ptr<StateTrack> state = mState.clone();
    copy->mState = std::move(*state);
    return copy;
}


void PropertyTrack::copyKeysTo(PropertyTrack& aDest) const {
    if (aDest.mType != mType) {
        return;
    }
    for (int c = 0; c < mChannelCount; ++c) {
        for (auto itr = mChannels[c].keys().constBegin(); itr != mChannels[c].keys().constEnd(); ++itr) {
            auto* source = static_cast<ChannelKey*>(itr.value());
            auto* clone = static_cast<ChannelKey*>(source->createClone());
            clone->setFrame(itr.key());
            if (!aDest.mChannels[c].adoptKey(clone)) {
                delete clone;
            }
        }
    }
    std::unique_ptr<StateTrack> state = mState.clone();
    aDest.mState = std::move(*state);
}
} // namespace core
