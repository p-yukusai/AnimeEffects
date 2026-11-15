#include "HsvKey.h"

namespace core {

HSVKey::Data::Data(): mEasings(), mHue(0), mSaturation(100), mValue(100), mAbsolute(0), mHSV{0, 100, 100, 0} {}

HSVKey::HSVKey(): mData() {}

TimeKey* HSVKey::createClone() {
    auto newKey = new HSVKey();
    newKey->mData = this->mData;
    return newKey;
}

bool HSVKey::serialize(Serializer& aOut) const {
    aOut.writeEasings(&mData.easing(0), kComponents);
    aOut.write(mData.hsv());
    return aOut.checkStream();
}

bool HSVKey::deserialize(Deserializer& aIn) {
    aIn.pushLogScope("HSVKey");
    bool errorCorrection = false;
    if(aIn.version().minorVersion() <= 6){
        errorCorrection = true;
    }

    if (!aIn.readEasings(&mData.easing(0), kComponents, errorCorrection)) {
        return aIn.errored("invalid easing param");
    }

    QList<int> hsv = data().hsv();
    aIn.read(hsv, errorCorrection);
    mData.setHSV(hsv);
    aIn.popLogScope();
    return aIn.checkStream();
}

} // namespace core
