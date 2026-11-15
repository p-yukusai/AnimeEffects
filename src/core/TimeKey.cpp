#include "core/TimeKey.h"

#include <limits>

namespace {

// Ids start at 1 so 0 can keep meaning "no bound" in a proportional state.
int sNextId = 1;

// Saturates at INT_MAX instead of overflowing into negative ids.
int takeNextId() {
    const int id = sNextId;
    if (sNextId < std::numeric_limits<int>::max()) {
        ++sNextId;
    }
    return id;
}

} // namespace

namespace core {

TimeKey::TimeKey():
    TreeNodeBase(this), mFocus(), mSelect(), mFrame(), mId(takeNextId()), mProportional() {}

void TimeKey::reserveId(int aValue) {
    // A non-positive value is not a stored id (files before 0.16 carry none),
    // so it must not move the counter. Saturate at INT_MAX rather than
    // overflowing; no real project approaches it.
    if (aValue <= 0 || aValue < sNextId) {
        return;
    }
    if (aValue == std::numeric_limits<int>::max()) {
        sNextId = aValue;
    } else {
        sNextId = aValue + 1;
    }
}

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
