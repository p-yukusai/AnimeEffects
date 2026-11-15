#include "core/MoveStateKey.h"

namespace core {

MoveStateKey::MoveStateKey(): mSpline(MoveKey::kDefaultSplineType) {}

StateKey* MoveStateKey::cloneState() const {
    auto* clone = new MoveStateKey();
    clone->mSpline = mSpline;
    copyBaseTo(*clone);
    return clone;
}

bool MoveStateKey::serialize(Serializer& aOut) const {
    aOut.write(static_cast<int>(mSpline));
    return aOut.checkStream();
}

bool MoveStateKey::deserialize(Deserializer& aIn) {
    aIn.pushLogScope("MoveStateKey");
    int spline = 0;
    aIn.read(spline);
    if (spline < 0 || MoveKey::SplineType_TERM <= spline) {
        return aIn.errored("invalid move spline type");
    }
    mSpline = static_cast<MoveKey::SplineType>(spline);
    aIn.popLogScope();
    return aIn.checkStream();
}

} // namespace core
