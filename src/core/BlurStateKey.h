#ifndef CORE_BLURSTATEKEY_H
#define CORE_BLURSTATEKEY_H

#include "core/StateKey.h"

namespace core {

// The discrete mode of a Blur property. The radii and angle are channel tracks;
// the directional toggle is a mode and is step-held.
class BlurStateKey: public StateKey {
public:
    BlurStateKey();

    TimeKeyType type() const override { return TimeKeyType_Blur; }

    bool directional() const { return mDirectional; }
    void setDirectional(bool aValue) { mDirectional = aValue; }

    StateKey* cloneState() const override;
    bool serialize(Serializer& aOut) const override;
    bool deserialize(Deserializer& aIn) override;

private:
    bool mDirectional;
};

} // namespace core

#endif // CORE_BLURSTATEKEY_H
