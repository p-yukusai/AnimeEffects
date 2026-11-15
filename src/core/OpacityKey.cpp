#include "core/OpacityKey.h"

namespace core {

OpacityKey::Data::Data(): mEasings(), mOpacity(1.0f) {}

bool OpacityKey::Data::isZero() const { return mOpacity == 0.0f; }

void OpacityKey::Data::clamp() { mOpacity = xc_clamp(mOpacity, 0.0f, 1.0f); }

OpacityKey::OpacityKey(): mData() {}

TimeKey* OpacityKey::createClone() {
    auto newKey = new OpacityKey();
    newKey->mData = this->mData;
    return newKey;
}

bool OpacityKey::serialize(Serializer& aOut) const {
    aOut.writeEasings(&mData.easing(0), kComponents);
    aOut.write(mData.opacity());
    return aOut.checkStream();
}

bool OpacityKey::deserialize(Deserializer& aIn) {
    aIn.pushLogScope("OpacityKey");

    if (!aIn.readEasings(&mData.easing(0), kComponents)) {
        return aIn.errored("invalid easing param");
    }

    float opacity = 1.0f;
    aIn.read(opacity);
    mData.setOpacity(opacity);

    aIn.popLogScope();
    return aIn.checkStream();
}

} // namespace core
