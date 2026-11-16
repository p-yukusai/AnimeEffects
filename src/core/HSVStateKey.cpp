#include "core/HSVStateKey.h"

namespace core {

HSVStateKey::HSVStateKey(): mAbsolute(false) {}

StateKey* HSVStateKey::cloneState() const {
    auto* clone = new HSVStateKey();
    clone->mAbsolute = mAbsolute;
    copyBaseTo(*clone);
    return clone;
}

bool HSVStateKey::serialize(Serializer& aOut) const {
    aOut.write(mAbsolute);
    return aOut.checkStream();
}

bool HSVStateKey::deserialize(Deserializer& aIn) {
    aIn.pushLogScope("HSVStateKey");
    aIn.read(mAbsolute);
    aIn.popLogScope();
    return aIn.checkStream();
}

} // namespace core
