#include "core/TimeKey.h"

#include "core/KeyId.h"

namespace core {

TimeKey::TimeKey():
    TreeNodeBase(this), mFocus(), mSelect(), mFrame(), mId(takeKeyId()), mProportional() {}

void TimeKey::reserveId(int aValue) { reserveKeyId(aValue); }

void TimeKey::setProportional(bool aEnabled) {
    if (!aEnabled) {
        mProportional.reset();
    } else if (!mProportional) {
        mProportional.reset(new Proportional());
    }
}

void TimeKey::setProportionalState(const Proportional& aState) {
    if (mProportional) {
        *mProportional = aState;
    } else {
        mProportional.reset(new Proportional(aState));
    }
}

} // namespace core
