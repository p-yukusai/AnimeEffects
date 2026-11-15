#ifndef CORE_TIMEKEY_H
#define CORE_TIMEKEY_H

#include <limits>
#include <memory>
#include "util/LifeLink.h"
#include "util/TreeNodeBase.h"
#include "util/TreeIterator.h"
#include "util/NonCopyable.h"
#include "cmnd/SleepableObject.h"
#include "core/Serializer.h"
#include "core/Deserializer.h"
#include "core/TimeKeyType.h"

namespace core {

class TimeKey: public util::TreeNodeBase<TimeKey>, public cmnd::SleepableObject, private util::NonCopyable {
public:
    // The default key's frame marker. Historically -1, but negative project
    // ranges make every int a legitimate key/playhead frame, so the marker
    // moved to INT_MIN — a frame no real project can reach. It is never
    // serialized (map keys write their index; the default key's frame is
    // set on load), so files are byte-compatible.
    static constexpr int kDefaultKeyIndex = std::numeric_limits<int>::min();

    // Blur and HSV carry three graphable components, Wave five; no key type has
    // more. This is the size of the serialized per-component weight arrays, so
    // growing it is a FORMAT change: TimeLine writes kMaxProportionalComponents
    // entries and reads the count the file's version implies (0.16-0.17 stored
    // three, 0.18 stores five).
    static constexpr int kMaxProportionalComponents = 5;

    // A proportional key keeps stored fractional weights between its nearest
    // ABSOLUTE neighbours of the same type, so the resolved frame and values
    // follow those neighbours when they move. The weights are stored rather
    // than re-derived from the rounded positions: re-deriving would accumulate
    // quantization drift over repeated edits.
    struct Proportional {
        int leftId = 0;   // 0 = no absolute bound on that side
        int rightId = 0;
        bool timeValid = false;
        float timeWeight = 0.0f;
        bool valueValid[kMaxProportionalComponents] = {};
        float valueWeight[kMaxProportionalComponents] = {};
    };

    typedef util::TreeNodeBase<TimeKey>::Children ChildrenType;
    typedef util::TreeIterator<TimeKey, ChildrenType::Iterator> Iterator;
    typedef util::TreeIterator<const TimeKey, ChildrenType::ConstIterator> ConstIterator;

    TimeKey();

    virtual ~TimeKey() {}

    virtual TimeKeyType type() const = 0;
    virtual bool canHoldChild() const { return false; }

    virtual void sleep() {}
    virtual void awake() {}

    int frame() const { return mFrame; }
    void setFrame(int aFrame) { mFrame = aFrame; }

    bool isDefaultKey() const { return mFrame == kDefaultKeyIndex; }

    // Stable identity within a loaded project. Assigned from a file-local
    // counter on construction and preserved by map moves and undo (they move
    // the same objects); deserialization restores the stored id. Clones get a
    // fresh id.
    int id() const { return mId; }
    // Raise the id counter so stored ids stay unique. Ids <= 0 are not stored
    // ids (older files carry none), so they never move the counter.
    static void reserveId(int aValue);

    // Pointer presence is the enable flag: an absolute key pays one pointer.
    bool isProportional() const { return static_cast<bool>(mProportional); }
    void setProportional(bool aEnabled);
    const Proportional* proportionalState() const { return mProportional.get(); }
    Proportional* proportionalState() { return mProportional.get(); }
    void setProportionalState(const Proportional& aState);
    void clearProportionalState() { mProportional.reset(); }

    bool isFocused() const { return mFocus.isLinking(); }
    void setFocus(util::LifeLink& aLink) { mFocus = aLink; }

    bool isSelected() const { return mSelect.isLinking(); }
    void setSelect(util::LifeLink& aLink) { mSelect = aLink; }

    virtual TimeKey* createClone() = 0;

    virtual bool serialize(Serializer& aOut) const = 0;
    virtual bool deserialize(Deserializer& aIn) = 0;

private:
    // Restoring a serialized id is the loader's job, not an ordinary edit:
    // TimeLine::deserializeTimeKey assigns the stored id after reserving it.
    friend class TimeLine;
    void setId(int aId) { mId = aId; }

    util::LifeLink::Node mFocus;
    util::LifeLink::Node mSelect;
    int mFrame;
    int mId;
    std::unique_ptr<Proportional> mProportional;
};

} // namespace core


#define TIMEKEY_TYPE_ASSERT(aKey, aType) \
    XC_MSG_ASSERT((aKey).type() == core::TimeKeyType_##aType, "timekey type error: %d", (aKey).type())

#define TIMEKEY_PTR_TYPE_ASSERT(aKeyPtr, aType) \
    do { \
        XC_PTR_ASSERT(aKeyPtr); \
        TIMEKEY_TYPE_ASSERT(*(aKeyPtr), aType); \
    } while (0)


#endif // CORE_TIMEKEY_H
