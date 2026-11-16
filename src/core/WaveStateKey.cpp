#include "core/WaveStateKey.h"

namespace core {

WaveStateKey::WaveStateKey() = default;

StateKey* WaveStateKey::cloneState() const {
    auto* clone = new WaveStateKey();
    clone->mSpeed = mSpeed;
    clone->mCrest = mCrest;
    clone->mDualAngleDeg = mDualAngleDeg;
    clone->mDualScale = mDualScale;
    clone->mTimeOffset = mTimeOffset;
    clone->mDeform = mDeform;
    copyBaseTo(*clone);
    return clone;
}

bool WaveStateKey::serialize(Serializer& aOut) const {
    aOut.write(mSpeed);
    aOut.write(mCrest);
    aOut.write(mDualAngleDeg);
    aOut.write(mDualScale);
    aOut.write(mTimeOffset);
    aOut.write(static_cast<int>(mDeform));
    return aOut.checkStream();
}

bool WaveStateKey::deserialize(Deserializer& aIn) {
    aIn.pushLogScope("WaveStateKey");
    int deform = 0;
    aIn.read(mSpeed);
    aIn.read(mCrest);
    aIn.read(mDualAngleDeg);
    aIn.read(mDualScale);
    aIn.read(mTimeOffset);
    aIn.read(deform);
    mDeform = (deform == WaveKey::Deform_Ripple || deform == WaveKey::Deform_Sway)
        ? static_cast<WaveKey::Deform>(deform) : WaveKey::Deform_Ripple;
    aIn.popLogScope();
    return aIn.checkStream();
}

} // namespace core
