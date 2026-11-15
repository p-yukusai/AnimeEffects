#ifndef CORE_TIMEKEYPOS
#define CORE_TIMEKEYPOS

#include "core/TimeLine.h"

namespace core {

class TimeKeyPos {
public:
    // A state key has no channel map; this sentinel addresses its property's
    // state track through the same TimeKeyPos currency as a channel key.
    static constexpr int kStateComponent = -2;

    TimeKeyPos(): mLine(), mType(), mIndex(), mComponent(-1) {}

    TimeKeyPos(TimeLine& aLine, TimeKeyType aType, int aIndex, int aComponent = -1):
        mLine(&aLine), mType(aType), mIndex(aIndex), mComponent(aComponent) {}

    void setLine(TimeLine* aLine) { mLine = aLine; }
    void setType(TimeKeyType aType) { mType = aType; }
    void setIndex(int aIndex) { mIndex = aIndex; }
    void setComponent(int aComponent) { mComponent = aComponent; }

    bool isNull() const { return mLine == NULL; }

    TimeLine* line() { return mLine; }
    const TimeLine* line() const { return mLine; }

    TimeKeyType type() const { return mType; }
    int index() const { return mIndex; }
    // -1 is the legacy bundle key, >= 0 names a channel map, and
    // kStateComponent names the property's state track.
    int component() const { return mComponent; }
    bool isState() const { return mComponent == kStateComponent; }

    bool isExist() const {
        if (mLine)
            return map().contains(mIndex);
        return false;
    }

    TimeKey* key() {
        if (mLine)
            return map().value(mIndex);
        return nullptr;
    }

    const TimeLine::MapType& map() const {
        XC_PTR_ASSERT(mLine);
        if (mComponent == kStateComponent) {
            return mLine->stateMap(mType);
        }
        if (mComponent >= 0) {
            return mLine->channelMap(mType, mComponent);
        }
        // A single-channel track-backed property IS its only channel: a
        // whole-property position and that channel are one thing, so the legacy
        // component -1 spelling resolves there too. Multi-channel properties keep
        // -1 as the whole-property spelling until their commands fan it out.
        if (mLine->usesChannelTracks(mType)) {
            const PropertyTrack* prop = mLine->property(mType);
            if (prop && prop->channelCount() == 1) {
                return mLine->channelMap(mType, 0);
            }
        }
        return mLine->map(mType);
    }

private:
    TimeLine* mLine;
    TimeKeyType mType;
    int mIndex;
    int mComponent;
};

} // namespace core

#endif // CORE_TIMEKEYPOS
