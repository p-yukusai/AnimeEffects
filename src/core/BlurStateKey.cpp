#include "core/BlurStateKey.h"

namespace core {

BlurStateKey::BlurStateKey(): mDirectional(false) {}

StateKey* BlurStateKey::cloneState() const {
    auto* clone = new BlurStateKey();
    clone->mDirectional = mDirectional;
    copyBaseTo(*clone);
    return clone;
}

bool BlurStateKey::serialize(Serializer& aOut) const {
    aOut.write(mDirectional);
    return aOut.checkStream();
}

bool BlurStateKey::deserialize(Deserializer& aIn) {
    aIn.pushLogScope("BlurStateKey");
    aIn.read(mDirectional);
    aIn.popLogScope();
    return aIn.checkStream();
}

} // namespace core
