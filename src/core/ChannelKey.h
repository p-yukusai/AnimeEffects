#ifndef CORE_CHANNELKEY_H
#define CORE_CHANNELKEY_H

#include "core/TimeKey.h"
#include "util/Easing.h"

namespace core {

// One key of one scalar channel. It is a TimeKey so it carries the stable id,
// frame and selection focus every authored key already needs; its payload is
// exactly one value and the easing that shapes the segment leaving it. The
// PropertyTrack's ChannelTrack owns a frame-ordered map of these.
//
// This is deliberately a key object rather than a bundle: there is no second
// channel's value in it and no mask to ask about. Move X and Move Y at the same
// frame are two distinct ChannelKey objects in two channel maps.
class ChannelKey: public TimeKey {
public:
    ChannelKey(TimeKeyType aType, int aComponent);

    TimeKeyType type() const override { return mType; }
    int component() const override { return mComponent; }

    float value() const { return mValue; }
    void setValue(float aValue) { mValue = aValue; }
    float& valueRef() { return mValue; }

    util::Easing::Param& easing() { return mEasing; }
    const util::Easing::Param& easing() const { return mEasing; }

    // Whether this channel key is part of its property frame's LINKED group.
    // A linked keyframe is a property key: every graphable channel has a key
    // there, all linked, and edits write all of them. A split key is one
    // channel's owned key; edits touch only it. The flag is authored document
    // data (0.24) because it changes edit behaviour and must survive a reload;
    // it is not a property-key object - the property key is still the set of
    // channel keys at one frame.
    bool linked() const { return mLinked; }
    void setLinked(bool aLinked) { mLinked = aLinked; }

    TimeKey* createClone() override;
    bool serialize(Serializer& aOut) const override;
    bool deserialize(Deserializer& aIn) override;

private:
    TimeKeyType mType;
    int mComponent;
    float mValue;
    util::Easing::Param mEasing;
    bool mLinked = true;
};

} // namespace core

#endif // CORE_CHANNELKEY_H
