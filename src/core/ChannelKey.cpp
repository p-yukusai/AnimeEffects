#include "core/ChannelKey.h"

#include <QVersionNumber>

namespace core {

ChannelKey::ChannelKey(TimeKeyType aType, int aComponent):
    TimeKey(), mType(aType), mComponent(aComponent), mValue(0.0f), mEasing() {}

TimeKey* ChannelKey::createClone() {
    auto* clone = new ChannelKey(mType, mComponent);
    clone->mValue = mValue;
    clone->mEasing = mEasing;
    clone->mLinked = mLinked;
    clone->setFrame(frame());
    return clone;
}

bool ChannelKey::serialize(Serializer& aOut) const {
    aOut.write(mValue);
    aOut.write(mEasing);
    // 0.24: whether this key belongs to its frame's linked property key.
    aOut.write(mLinked);
    return aOut.checkStream();
}

bool ChannelKey::deserialize(Deserializer& aIn) {
    aIn.pushLogScope("ChannelKey");
    aIn.read(mValue);
    if (!aIn.read(mEasing)) {
        return aIn.errored("invalid channel easing");
    }
    // 0.23 and older wrote every channel key as a linked property key.
    mLinked = aIn.version() >= QVersionNumber(0, 24) ? aIn.getRead<bool>() : true;
    aIn.popLogScope();
    return aIn.checkStream();
}

} // namespace core
