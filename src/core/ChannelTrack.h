#ifndef CORE_CHANNELTRACK_H
#define CORE_CHANNELTRACK_H

#include <QMap>

#include "core/ChannelKey.h"
#include "core/ChannelSpec.h"
#include "core/TimeInfo.h"
#include "util/Easing.h"
#include "util/Range.h"

namespace core {

// One scalar channel's frame-ordered keys. Sampling mirrors the rules the old
// TimeKeyGatherer + TimeKeyBlender pair used for a single component:
//
//   * no key: the base value;
//   * an integral frame holding a key: that key's value;
//   * before the first / after the last key: hold, unless looping, in which
//     case the wrapped segment from the last key back to the first is evaluated;
//   * a step-held channel (the Image offset): the last key at or before the
//     frame, else the base - it never interpolates and never loops.
//
// The map is TimeLine::MapType (QMap<int, TimeKey*>), so a channel key is
// selected, focused and moved through the same key identity every other key
// uses. The track owns the ChannelKey objects and deletes them.
class ChannelTrack {
public:
    using MapType = QMap<int, TimeKey*>;

    ChannelTrack();
    ChannelTrack(TimeKeyType aType, int aComponent);
    ~ChannelTrack();

    ChannelTrack(const ChannelTrack&) = delete;
    ChannelTrack& operator=(const ChannelTrack&) = delete;
    ChannelTrack(ChannelTrack&& aOther) noexcept;
    ChannelTrack& operator=(ChannelTrack&& aOther) noexcept;

    TimeKeyType type() const { return mType; }
    int component() const { return mComponent; }
    const ChannelSpec& spec() const { return mSpec; }

    bool isEmpty() const { return mKeys.isEmpty(); }
    int keyCount() const { return mKeys.size(); }
    const MapType& keys() const { return mKeys; }
    MapType& keysRef() { return mKeys; }

    int firstFrame() const { return mKeys.isEmpty() ? 0 : mKeys.firstKey(); }
    int lastFrame() const { return mKeys.isEmpty() ? 0 : mKeys.lastKey(); }

    const ChannelKey* keyAt(int aFrame) const;
    ChannelKey* keyAt(int aFrame);
    bool hasKey(int aFrame) const;
    const ChannelKey* keyBeforeOrAt(int aFrame) const;
    const ChannelKey* keyAfter(int aFrame) const;

    float baseValue() const { return mBaseValue; }
    float& baseValueRef() { return mBaseValue; }
    void setBaseValue(float aValue);

    float clamp(float aValue) const;
    float quantize(float aValue) const;

    // Creates a key and inserts/replaces it at aFrame. The returned reference is
    // owned by the track and stays valid until that frame is removed/moved.
    ChannelKey& insertKey(int aFrame, float aValue, const util::Easing::Param& aEasing);
    ChannelKey& insertKey(int aFrame, float aValue);

    // Takes ownership of aKey on success. Refuses a wrong type/component or an
    // occupied frame; on refusal the caller keeps ownership.
    bool adoptKey(ChannelKey* aKey);
    // Takes ownership on success; deletes the key that was at that frame.
    bool replaceKey(ChannelKey* aKey);

    bool removeKey(int aFrame);
    bool moveKey(int aFrom, int aTo);
    void clearKeys();

    bool setKeyValue(int aFrame, float aValue);
    bool setKeyEasing(int aFrame, const util::Easing::Param& aEasing);

    // The new per-track block: base value, then each key's frame, value/easing
    // (ChannelKey::serialize), stable id and proportional state. The property
    // type/component are written by TimeLine, which owns the channel's identity.
    bool serialize(Serializer& aOut) const;
    bool deserialize(Deserializer& aIn);

    // The played value at aTime. The time's frame range and loop flag are the
    // project's, exactly as the pre-track gatherer read them.
    float sample(const TimeInfo& aTime) const;
    float sample(int aFrame, const util::Range& aFrameRange, bool aLoop) const;

    // The segment the frame lies in: the two bounding keys, the neighbour keys a
    // derived handle reads, whether the frame sits exactly on a key, and the
    // eased fraction from `from` to `to`. Move's spatial spline asks this once
    // per axis.
    struct SegmentSample {
        const ChannelKey* from = nullptr;
        const ChannelKey* to = nullptr;
        const ChannelKey* prev = nullptr;
        const ChannelKey* next = nullptr;
        int fromFrame = 0;
        int toFrame = 0;
        int prevFrame = 0;
        int nextFrame = 0;
        bool exact = false;
        float rate = 0.0f;

        bool hasSegment() const { return from && to; }
    };
    SegmentSample sampleSegment(int aFrame, const util::Range& aFrameRange, bool aLoop) const;
    SegmentSample sampleSegment(const TimeInfo& aTime) const;

private:
    struct Point {
        const ChannelKey* key = nullptr;
        int frame = 0;
        float relativeFrame = 0.0f;
    };
    struct Gather {
        Point p0;
        Point p1;
        Point prev;
        Point next;
        bool exact = false;
    };

    Gather gather(int aFrame, float aFrameDecimal, const util::Range& aRange, bool aLoop) const;
    SegmentSample sampleSegment(
        int aFrame, float aFrameDecimal, const util::Range& aRange, bool aLoop
    ) const;
    float sampleFrame(int aFrame, float aFrameDecimal, const util::Range& aRange, bool aLoop) const;
    float easingRate(
        const Point& aFrom, const Point& aTo, const Point& aPrev, const Point& aNext, float aD
    ) const;

    TimeKeyType mType;
    int mComponent;
    ChannelSpec mSpec;
    float mBaseValue;
    MapType mKeys;
};

} // namespace core

#endif // CORE_CHANNELTRACK_H
