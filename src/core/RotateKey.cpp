#include "core/RotateKey.h"
#include "core/Constant.h"

namespace core {
//-------------------------------------------------------------------------------------------------
RotateKey::Data::Data(): mEasings(), mRotate(0.0f) {}

void RotateKey::Data::clamp() { mRotate = xc_clamp(mRotate, Constant::rotateMin(), Constant::rotateMax()); }

//-------------------------------------------------------------------------------------------------
RotateKey::RotateKey(): mData() {}

TimeKey* RotateKey::createClone() {
    auto newKey = new RotateKey();
    newKey->mData = this->mData;
    return newKey;
}

bool RotateKey::serialize(Serializer& aOut) const {
    aOut.writeEasings(&mData.easing(0), kComponents);
    aOut.write(mData.rotate());
    return aOut.checkStream();
}

bool RotateKey::deserialize(Deserializer& aIn) {
    aIn.pushLogScope("RotateKey");

    // easing
    if (!aIn.readEasings(&mData.easing(0), kComponents)) {
        return aIn.errored("invalid easing param");
    }

    float rotate = 0.0f;
    aIn.read(rotate);
    mData.setRotate(rotate);

    aIn.popLogScope();
    return aIn.checkStream();
}

} // namespace core
