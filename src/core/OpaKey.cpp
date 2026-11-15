#include "core/OpaKey.h"

namespace core {

OpaKey::Data::Data(): mEasings(), mOpacity(1.0f) {}

bool OpaKey::Data::isZero() const { return mOpacity == 0.0f; }

void OpaKey::Data::clamp() { mOpacity = xc_clamp(mOpacity, 0.0f, 1.0f); }

OpaKey::OpaKey(): mData() {}

TimeKey* OpaKey::createClone() {
    auto newKey = new OpaKey();
    newKey->mData = this->mData;
    return newKey;
}

bool OpaKey::serialize(Serializer& aOut) const {
    aOut.writeEasings(&mData.easing(0), kComponents);
    aOut.write(mData.opacity());
    return aOut.checkStream();
}

bool OpaKey::deserialize(Deserializer& aIn) {
    aIn.pushLogScope("OpaKey");

    if (!aIn.readEasings(&mData.easing(0), kComponents)) {
        return aIn.errored("invalid easing param");
    }

    float opa = 1.0f;
    aIn.read(opa);
    mData.setOpacity(opa);

    aIn.popLogScope();
    return aIn.checkStream();
}

} // namespace core
