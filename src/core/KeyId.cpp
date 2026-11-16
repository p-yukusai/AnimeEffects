#include "core/KeyId.h"

#include <limits>

namespace {

int sNextId = 1;

} // namespace

namespace core {

int takeKeyId() {
    const int id = sNextId;
    if (sNextId < std::numeric_limits<int>::max()) {
        ++sNextId;
    }
    return id;
}

void reserveKeyId(int aValue) {
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

} // namespace core
