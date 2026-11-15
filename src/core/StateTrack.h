#ifndef CORE_STATETRACK_H
#define CORE_STATETRACK_H

#include <memory>
#include <QMap>

#include "core/Deserializer.h"
#include "core/Serializer.h"
#include "core/StateKey.h"
#include "core/TimeKeyType.h"

namespace core {

// Frame-ordered discrete states of one property, plus the base state. Sampling a
// state is step-held: the last state key at or before the frame, else the base.
// There is no interpolation and no loop - a state is a setting, not a value.
//
// The map is QMap<int, TimeKey*> so a state key uses the same TimeKey identity,
// selection, focus, move and copy machinery as every other timeline key. Its
// values are StateKey objects; the casts are checked at insertion.
class StateTrack {
public:
    using MapType = QMap<int, TimeKey*>;

    StateTrack();
    explicit StateTrack(TimeKeyType aType);

    StateTrack(const StateTrack&) = delete;
    StateTrack& operator=(const StateTrack&) = delete;
    StateTrack(StateTrack&&) = default;
    StateTrack& operator=(StateTrack&&) = default;

    ~StateTrack();

    TimeKeyType type() const { return mType; }

    bool isEmpty() const { return mKeys.isEmpty() && !mBase; }
    int keyCount() const { return mKeys.size(); }
    const MapType& keys() const { return mKeys; }
    MapType& keysRef() { return mKeys; }

    StateKey* keyAt(int aFrame);
    const StateKey* keyAt(int aFrame) const;
    const StateKey* keyBeforeOrAt(int aFrame) const;

    // Takes ownership on success. Refuses null, a wrong type, or a frame that
    // already holds a state key.
    StateKey* insertKey(int aFrame, std::unique_ptr<StateKey> aKey);
    bool removeKey(int aFrame);
    bool moveKey(int aFrom, int aTo);
    void clear();

    const StateKey* sampleState(int aFrame) const;
    const StateKey* baseState() const { return mBase.get(); }
    StateKey* baseState() { return mBase.get(); }
    void setBaseState(std::unique_ptr<StateKey> aState);

    std::unique_ptr<StateTrack> clone() const;

    bool serialize(Serializer& aOut) const;
    bool deserialize(Deserializer& aIn);

private:
    TimeKeyType mType;
    MapType mKeys;
    std::unique_ptr<StateKey> mBase;
};

} // namespace core

#endif // CORE_STATETRACK_H
