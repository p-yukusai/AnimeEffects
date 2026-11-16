#include "core/DepthKey.h"
#include "core/Constant.h"

namespace core {
//-------------------------------------------------------------------------------------------------
DepthKey::Data::Data(): mEasings(), mDepth(0.0f) {}

void DepthKey::Data::clamp() { mDepth = xc_clamp(mDepth, Constant::transMin(), Constant::transMax()); }

//-------------------------------------------------------------------------------------------------
DepthKey::DepthKey(): mData() {}

TimeKey* DepthKey::createClone() {
    auto newKey = new DepthKey();
    newKey->mData = this->mData;
    return newKey;
}

bool DepthKey::serialize(Serializer& aOut) const {
    aOut.writeEasings(&mData.easing(0), kComponents);
    aOut.write(mData.depth());
    return aOut.checkStream();
}

bool DepthKey::deserialize(Deserializer& aIn) {
    aIn.pushLogScope("DepthKey");

    // easing
    if (!aIn.readEasings(&mData.easing(0), kComponents)) {
        return aIn.errored("invalid easing param");
    }

    float depth = 0.0f;
    aIn.read(depth);
    mData.setDepth(depth);

    aIn.popLogScope();
    return aIn.checkStream();
}

} // namespace core
