#ifndef CORE_WAVESTATEKEY_H
#define CORE_WAVESTATEKEY_H

#include "core/StateKey.h"
#include "core/WaveKey.h"

namespace core {

// The step-held shape of a Wave property: speed, crest, the second wave's
// relative angle/scale, the time-offset mask strength and the deform mode.
// The five graph channels are separate value tracks.
class WaveStateKey: public StateKey {
public:
    WaveStateKey();

    TimeKeyType type() const override { return TimeKeyType_Wave; }

    float speed() const { return mSpeed; }
    void setSpeed(float aValue) { mSpeed = aValue; }
    float crest() const { return mCrest; }
    void setCrest(float aValue) { mCrest = aValue; }
    float dualAngleDeg() const { return mDualAngleDeg; }
    void setDualAngleDeg(float aValue) { mDualAngleDeg = aValue; }
    float dualScale() const { return mDualScale; }
    void setDualScale(float aValue) { mDualScale = aValue; }
    float timeOffset() const { return mTimeOffset; }
    void setTimeOffset(float aValue) { mTimeOffset = aValue; }
    WaveKey::Deform deform() const { return mDeform; }
    void setDeform(WaveKey::Deform aValue) { mDeform = aValue; }

    StateKey* cloneState() const override;
    bool serialize(Serializer& aOut) const override;
    bool deserialize(Deserializer& aIn) override;

private:
    float mSpeed = 0.0f;
    float mCrest = 1.0f;
    float mDualAngleDeg = 0.0f;
    float mDualScale = 1.0f;
    float mTimeOffset = 0.0f;
    WaveKey::Deform mDeform = WaveKey::Deform_Ripple;
};

} // namespace core

#endif // CORE_WAVESTATEKEY_H
