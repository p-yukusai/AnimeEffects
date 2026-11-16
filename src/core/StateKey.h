#ifndef CORE_STATEKEY_H
#define CORE_STATEKEY_H

#include <memory>

#include "core/Deserializer.h"
#include "core/Serializer.h"
#include "core/TimeKey.h"
#include "core/TimeKeyType.h"

namespace core {

// A discrete property state at a frame: an image resource, a blur's directional
// mode, a wave's shape settings, a move's spline mode. It is step-held, not
// interpolated - if a value interpolates it is a ChannelKey instead.
//
// Since 0.26 it is also a TimeKey: the timeline selects, focuses, moves and
// copies state keys through the same identity machinery as every other key.
// It still has no easing or proportional payload (those are value concepts),
// and its children are unused until the Image state payload needs them.
class StateKey: public TimeKey {
public:
    StateKey();
    ~StateKey() override = default;

    virtual TimeKeyType type() const override = 0;
    TimeKey* createClone() override { return cloneState(); }
    virtual StateKey* cloneState() const = 0;
    virtual bool serialize(Serializer& aOut) const override = 0;
    virtual bool deserialize(Deserializer& aIn) override = 0;
    virtual void sleep() override {}
    virtual void awake() override {}

    static void reserveId(int aValue);

protected:
    // Clone helpers: a clone gets the data and the frame, never the id.
    void copyBaseTo(StateKey& aTo) const { aTo.setFrame(frame()); }
};

// Builds the state key a property type stores. Null for a type whose state is
// not implemented yet, and for types with no state track.
std::unique_ptr<StateKey> createStateKey(TimeKeyType aType);

} // namespace core

#endif // CORE_STATEKEY_H
