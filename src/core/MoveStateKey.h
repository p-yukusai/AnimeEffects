#ifndef CORE_MOVESTATEKEY_H
#define CORE_MOVESTATEKEY_H

#include "core/MoveKey.h"
#include "core/StateKey.h"

namespace core {

// The non-channel state of a Move property: the spatial spline mode. Position
// and centroid are channel tracks; the spline is a discrete property setting and
// is step-held from the state key at or before the frame.
class MoveStateKey: public StateKey {
public:
    MoveStateKey();

    TimeKeyType type() const override { return TimeKeyType_Move; }

    MoveKey::SplineType spline() const { return mSpline; }
    void setSpline(MoveKey::SplineType aSpline) { mSpline = aSpline; }

    StateKey* cloneState() const override;
    bool serialize(Serializer& aOut) const override;
    bool deserialize(Deserializer& aIn) override;

private:
    MoveKey::SplineType mSpline;
};

} // namespace core

#endif // CORE_MOVESTATEKEY_H
