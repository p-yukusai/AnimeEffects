#include "core/StateTrack.h"

#include <algorithm>

namespace core {

StateTrack::StateTrack(): mType(TimeKeyType_TERM), mKeys(), mBase() {}

StateTrack::StateTrack(TimeKeyType aType): mType(aType), mKeys(), mBase() {}

StateTrack::~StateTrack() { clear(); }

StateKey* StateTrack::keyAt(int aFrame) {
    auto itr = mKeys.find(aFrame);
    return itr == mKeys.end() ? nullptr : static_cast<StateKey*>(itr.value());
}

const StateKey* StateTrack::keyAt(int aFrame) const {
    auto itr = mKeys.constFind(aFrame);
    return itr == mKeys.constEnd() ? nullptr : static_cast<const StateKey*>(itr.value());
}

const StateKey* StateTrack::keyBeforeOrAt(int aFrame) const {
    auto itr = mKeys.upperBound(aFrame);
    if (itr == mKeys.constBegin()) {
        return nullptr;
    }
    --itr;
    return static_cast<const StateKey*>(itr.value());
}

StateKey* StateTrack::insertKey(int aFrame, std::unique_ptr<StateKey> aKey) {
    if (!aKey || aKey->type() != mType || mKeys.contains(aFrame)) {
        return nullptr;
    }
    aKey->setFrame(aFrame);
    StateKey* raw = aKey.release();
    mKeys.insert(aFrame, raw);
    return raw;
}

bool StateTrack::removeKey(int aFrame) {
    auto itr = mKeys.find(aFrame);
    if (itr == mKeys.end()) {
        return false;
    }
    delete itr.value();
    mKeys.erase(itr);
    return true;
}

bool StateTrack::moveKey(int aFrom, int aTo) {
    if (aFrom == aTo) {
        return true;
    }
    auto from = mKeys.find(aFrom);
    if (from == mKeys.end() || mKeys.contains(aTo)) {
        return false;
    }
    TimeKey* key = from.value();
    mKeys.erase(from);
    key->setFrame(aTo);
    mKeys.insert(aTo, key);
    return true;
}

void StateTrack::clear() {
    for (TimeKey* key : mKeys) {
        delete key;
    }
    mKeys.clear();
    mBase.reset();
}

const StateKey* StateTrack::sampleState(int aFrame) const {
    const StateKey* key = keyBeforeOrAt(aFrame);
    return key ? key : mBase.get();
}

void StateTrack::setBaseState(std::unique_ptr<StateKey> aState) {
    if (aState && aState->type() == mType) {
        mBase = std::move(aState);
    }
}

std::unique_ptr<StateTrack> StateTrack::clone() const {
    std::unique_ptr<StateTrack> copy(new StateTrack(mType));
    for (auto itr = mKeys.constBegin(); itr != mKeys.constEnd(); ++itr) {
        if (!itr.value()) {
            continue;
        }
        std::unique_ptr<StateKey> key(static_cast<StateKey*>(itr.value()->createClone()));
        if (key) {
            copy->insertKey(itr.key(), std::move(key));
        }
    }
    if (mBase) {
        std::unique_ptr<StateKey> base(static_cast<StateKey*>(mBase->createClone()));
        copy->setBaseState(std::move(base));
    }
    return copy;
}

bool StateTrack::serialize(Serializer& aOut) const {
    aOut.write(static_cast<int>(mKeys.size()));
    for (auto itr = mKeys.constBegin(); itr != mKeys.constEnd(); ++itr) {
        const auto* key = static_cast<const StateKey*>(itr.value());
        aOut.write(itr.key());
        aOut.write(key->id());
        if (!key->serialize(aOut)) {
            return false;
        }
    }
    aOut.write(static_cast<bool>(mBase));
    if (mBase && !mBase->serialize(aOut)) {
        return false;
    }
    return aOut.checkStream();
}

bool StateTrack::deserialize(Deserializer& aIn) {
    aIn.pushLogScope("StateTrack");
    int count = 0;
    aIn.read(count);
    if (count < 0 || 10000000 < count) {
        return aIn.errored("invalid state key count");
    }
    clear();
    for (int i = 0; i < count; ++i) {
        int frame = 0;
        int id = 0;
        aIn.read(frame);
        aIn.read(id);
        std::unique_ptr<StateKey> key = createStateKey(mType);
        if (!key) {
            return aIn.errored("unknown state key type");
        }
        key->setFrame(frame);
        if (!key->deserialize(aIn)) {
            return aIn.errored("invalid state key payload");
        }
        if (id > 0) {
            StateKey::reserveId(id);
            key->setId(id);
        }
        if (!insertKey(frame, std::move(key))) {
            return aIn.errored("duplicate state key frame");
        }
    }
    bool hasBase = false;
    aIn.read(hasBase);
    if (hasBase) {
        std::unique_ptr<StateKey> base = createStateKey(mType);
        if (!base || !base->deserialize(aIn)) {
            return aIn.errored("invalid base state");
        }
        setBaseState(std::move(base));
    }
    aIn.popLogScope();
    return aIn.checkStream();
}

} // namespace core
