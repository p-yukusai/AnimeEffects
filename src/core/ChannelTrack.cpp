#include "core/ChannelTrack.h"

#include <algorithm>

namespace core {

namespace {

ChannelKey* asChannel(TimeKey* aKey) { return static_cast<ChannelKey*>(aKey); }

} // namespace

ChannelTrack::ChannelTrack():
    mType(TimeKeyType_TERM), mComponent(-1), mSpec(), mBaseValue(0.0f), mKeys() {}

ChannelTrack::ChannelTrack(TimeKeyType aType, int aComponent):
    mType(aType),
    mComponent(aComponent),
    mSpec(channelSpec(aType, aComponent)),
    mBaseValue(mSpec.valid ? clamp(mSpec.defaultValue) : 0.0f),
    mKeys() {}

ChannelTrack::~ChannelTrack() { clearKeys(); }

ChannelTrack::ChannelTrack(ChannelTrack&& aOther) noexcept:
    mType(aOther.mType),
    mComponent(aOther.mComponent),
    mSpec(aOther.mSpec),
    mBaseValue(aOther.mBaseValue),
    mKeys(std::move(aOther.mKeys)) {
    aOther.mKeys.clear();
}

ChannelTrack& ChannelTrack::operator=(ChannelTrack&& aOther) noexcept {
    if (this == &aOther) {
        return *this;
    }
    clearKeys();
    mType = aOther.mType;
    mComponent = aOther.mComponent;
    mSpec = aOther.mSpec;
    mBaseValue = aOther.mBaseValue;
    mKeys = std::move(aOther.mKeys);
    aOther.mKeys.clear();
    return *this;
}

const ChannelKey* ChannelTrack::keyAt(int aFrame) const {
    auto itr = mKeys.constFind(aFrame);
    return itr == mKeys.constEnd() ? nullptr : asChannel(itr.value());
}

ChannelKey* ChannelTrack::keyAt(int aFrame) {
    auto itr = mKeys.find(aFrame);
    return itr == mKeys.end() ? nullptr : asChannel(itr.value());
}

bool ChannelTrack::hasKey(int aFrame) const { return mKeys.contains(aFrame); }

const ChannelKey* ChannelTrack::keyBeforeOrAt(int aFrame) const {
    auto itr = mKeys.upperBound(aFrame);
    if (itr == mKeys.constBegin()) {
        return nullptr;
    }
    --itr;
    return asChannel(itr.value());
}

const ChannelKey* ChannelTrack::keyAfter(int aFrame) const {
    auto itr = mKeys.upperBound(aFrame);
    return itr == mKeys.constEnd() ? nullptr : asChannel(itr.value());
}

void ChannelTrack::setBaseValue(float aValue) { mBaseValue = clamp(quantize(aValue)); }

float ChannelTrack::clamp(float aValue) const {
    if (!mSpec.valid) {
        return aValue;
    }
    return std::max(mSpec.minValue, std::min(mSpec.maxValue, aValue));
}

float ChannelTrack::quantize(float aValue) const {
    // Playback's HSV setters take ints and truncate the blended float. The track
    // stores the same number the old key stored; a consumer that interpolates
    // still blends the floats and truncates at its own setter boundary.
    return mSpec.integral ? static_cast<float>(static_cast<int>(aValue)) : aValue;
}

ChannelKey& ChannelTrack::insertKey(int aFrame, float aValue, const util::Easing::Param& aEasing) {
    // An edit at an occupied frame updates the existing key object, preserving
    // its stable id and selection focus. replaceKey() is the ownership-taking
    // path for a paste/load, which deliberately creates a new authored key.
    if (ChannelKey* existing = keyAt(aFrame)) {
        existing->setValue(clamp(quantize(aValue)));
        existing->easing() = aEasing;
        return *existing;
    }

    auto* key = new ChannelKey(mType, mComponent);
    key->setFrame(aFrame);
    key->setValue(clamp(quantize(aValue)));
    key->easing() = aEasing;
    replaceKey(key);
    return *key;
}

ChannelKey& ChannelTrack::insertKey(int aFrame, float aValue) {
    return insertKey(aFrame, aValue, util::Easing::Param());
}

bool ChannelTrack::adoptKey(ChannelKey* aKey) {
    if (!aKey || aKey->type() != mType || aKey->component() != mComponent
        || mKeys.contains(aKey->frame())) {
        return false;
    }
    aKey->setValue(clamp(quantize(aKey->value())));
    mKeys.insert(aKey->frame(), aKey);
    return true;
}

bool ChannelTrack::replaceKey(ChannelKey* aKey) {
    if (!aKey || aKey->type() != mType || aKey->component() != mComponent) {
        return false;
    }
    const int frame = aKey->frame();
    auto existing = mKeys.find(frame);
    if (existing != mKeys.end()) {
        delete existing.value();
    }
    aKey->setValue(clamp(quantize(aKey->value())));
    mKeys.insert(frame, aKey);
    return true;
}

bool ChannelTrack::removeKey(int aFrame) {
    auto itr = mKeys.find(aFrame);
    if (itr == mKeys.end()) {
        return false;
    }
    delete itr.value();
    mKeys.erase(itr);
    return true;
}

bool ChannelTrack::moveKey(int aFrom, int aTo) {
    if (aFrom == aTo) {
        return true;
    }
    auto from = mKeys.find(aFrom);
    if (from == mKeys.end() || mKeys.contains(aTo)) {
        return false;
    }
    ChannelKey* key = asChannel(from.value());
    mKeys.erase(from);
    key->setFrame(aTo);
    mKeys.insert(aTo, key);
    return true;
}

void ChannelTrack::clearKeys() {
    for (TimeKey* key : mKeys) {
        delete key;
    }
    mKeys.clear();
}

bool ChannelTrack::setKeyValue(int aFrame, float aValue) {
    ChannelKey* key = keyAt(aFrame);
    if (!key) {
        return false;
    }
    key->setValue(clamp(quantize(aValue)));
    return true;
}

bool ChannelTrack::setKeyEasing(int aFrame, const util::Easing::Param& aEasing) {
    ChannelKey* key = keyAt(aFrame);
    if (!key) {
        return false;
    }
    key->easing() = aEasing;
    return true;
}

float ChannelTrack::sample(const TimeInfo& aTime) const {
    return sampleFrame(aTime.frame.get(), aTime.frame.getDecimal(), aTime.frameRange, aTime.loop);
}

float ChannelTrack::sample(int aFrame, const util::Range& aFrameRange, bool aLoop) const {
    return sampleFrame(aFrame, static_cast<float>(aFrame), aFrameRange, aLoop);
}

float ChannelTrack::sampleFrame(
    int aFrame, float aFrameDecimal, const util::Range& aFrameRange, bool aLoop
) const {
    if (mKeys.isEmpty()) {
        return mBaseValue;
    }

    const SegmentSample segment = sampleSegment(aFrame, aFrameDecimal, aFrameRange, aLoop);
    if (segment.exact) {
        return segment.from ? segment.from->value() : mBaseValue;
    }
    if (segment.from && segment.to) {
        return segment.from->value() * (1.0f - segment.rate) + segment.to->value() * segment.rate;
    }
    if (segment.from) {
        return segment.from->value();
    }
    if (segment.to) {
        return segment.to->value();
    }
    return mBaseValue;
}

ChannelTrack::SegmentSample ChannelTrack::sampleSegment(const TimeInfo& aTime) const {
    return sampleSegment(aTime.frame.get(), aTime.frameRange, aTime.loop);
}

ChannelTrack::SegmentSample ChannelTrack::sampleSegment(
    int aFrame, const util::Range& aFrameRange, bool aLoop
) const {
    return sampleSegment(aFrame, static_cast<float>(aFrame), aFrameRange, aLoop);
}

ChannelTrack::SegmentSample ChannelTrack::sampleSegment(
    int aFrame, float aFrameDecimal, const util::Range& aFrameRange, bool aLoop
) const {
    SegmentSample out;
    if (mKeys.isEmpty()) {
        return out;
    }

    if (mSpec.interpolation == ChannelInterpolation::Step) {
        // The Image offset is step-held. blendImageKey's historical rule is
        // findLastKey(map) - the last key at or before the frame, else the base;
        // it does not wrap before the first key.
        out.from = keyBeforeOrAt(aFrame);
        out.exact = out.from && out.from == keyAt(aFrame);
        return out;
    }

    const Gather g = gather(aFrame, aFrameDecimal, aFrameRange, aLoop);
    out.from = g.p0.key;
    out.to = g.p1.key;
    out.prev = g.prev.key;
    out.next = g.next.key;
    out.fromFrame = g.p0.frame;
    out.toFrame = g.p1.frame;
    out.prevFrame = g.prev.frame;
    out.nextFrame = g.next.frame;
    out.exact = g.exact;

    if (!out.exact && out.from && out.to) {
        const float d = g.p1.relativeFrame - g.p0.relativeFrame;
        if (d > 0.0f) {
            out.rate = easingRate(g.p0, g.p1, g.prev, g.next, d);
        }
    }
    return out;
}

ChannelTrack::Gather ChannelTrack::gather(
    int aFrame, float aFrameDecimal, const util::Range& aRange, bool aLoop
) const {
    Gather g;
    if (mKeys.isEmpty()) {
        return g;
    }

    // The exact-key fast path is the gatherer's rule: an integral frame that
    // the map holds is the key itself, not a point inside a segment.
    if (aFrameDecimal == static_cast<float>(aFrame)) {
        auto exact = mKeys.constFind(aFrame);
        if (exact != mKeys.constEnd()) {
            g.exact = true;
            g.p0.key = asChannel(exact.value());
            g.p0.frame = aFrame;
            g.p0.relativeFrame = 0.0f;
            return g;
        }
    }

    const int count = mKeys.size();
    const int loopSpan = aRange.diff() + 1;

    // Backward: the key at or before the frame, plus one more for a derived
    // handle. A loop wrap moves the reference base one span forward, exactly as
    // TimeKeyGatherer does.
    auto backward = mKeys.upperBound(aFrame);
    float backwardBase = aFrameDecimal;
    if (backward == mKeys.constBegin()) {
        if (aLoop && count > 1) {
            backward = mKeys.constEnd();
            backwardBase += static_cast<float>(loopSpan);
        }
    }
    if (backward != mKeys.constBegin()) {
        --backward;
        g.p0.key = asChannel(backward.value());
        g.p0.frame = backward.key();
        g.p0.relativeFrame = static_cast<float>(backward.key()) - backwardBase;

        if (backward != mKeys.constBegin()) {
            --backward;
            g.prev.key = asChannel(backward.value());
            g.prev.frame = backward.key();
        }
    }

    // Forward: the first key after the frame, plus one more.
    auto forward = mKeys.upperBound(aFrame);
    float forwardBase = aFrameDecimal;
    if (forward == mKeys.constEnd()) {
        if (aLoop && count > 1) {
            forward = mKeys.constBegin();
            forwardBase -= static_cast<float>(loopSpan);
        }
    }
    if (forward != mKeys.constEnd()) {
        g.p1.key = asChannel(forward.value());
        g.p1.frame = forward.key();
        g.p1.relativeFrame = static_cast<float>(forward.key()) - forwardBase;

        ++forward;
        if (forward != mKeys.constEnd()) {
            g.next.key = asChannel(forward.value());
            g.next.frame = forward.key();
        }
    }

    return g;
}

float ChannelTrack::easingRate(
    const Point& aFrom, const Point& aTo, const Point& aPrev, const Point& aNext, float aD
) const {
    if (!aFrom.key || !aTo.key) {
        return 0.0f;
    }

    const util::Easing::Param& param = aFrom.key->easing();
    const float t = -aFrom.relativeFrame;

    if (param.type == util::Easing::Type_Custom) {
        const util::Easing::HandleMode inMode = aTo.key->easing().handleIn;
        if (util::Easing::isDerived(param.handleOut) || util::Easing::isDerived(inMode)) {
            util::Easing::AutoContext ctx;
            ctx.inMode = inMode;

            // A derived handle reads the neighbours' values in the segment's own
            // change. The frames are relative to the FROM key, which is what
            // core::autoEasingContext filled for the pre-track blender.
            const float change = aTo.key->value() - aFrom.key->value();
            if (change != 0.0f) {
                if (aPrev.key) {
                    ctx.prev.valid = true;
                    ctx.prev.frame = static_cast<float>(aPrev.frame - aFrom.frame);
                    ctx.prev.value = (aPrev.key->value() - aFrom.key->value()) / change;
                }
                if (aNext.key) {
                    ctx.next.valid = true;
                    ctx.next.frame = static_cast<float>(aNext.frame - aFrom.frame);
                    ctx.next.value = (aNext.key->value() - aFrom.key->value()) / change;
                }
            }
            return util::Easing::calculate(param, ctx, t, 0.0f, 1.0f, aD);
        }
    }

    return util::Easing::calculate(param, t, 0.0f, 1.0f, aD);
}


bool ChannelTrack::serialize(Serializer& aOut) const {
    aOut.write(mBaseValue);
    aOut.write(static_cast<int>(mKeys.size()));
    for (auto itr = mKeys.constBegin(); itr != mKeys.constEnd(); ++itr) {
        const auto* key = static_cast<const ChannelKey*>(itr.value());
        aOut.write(itr.key());
        if (!key->serialize(aOut)) {
            return false;
        }
        aOut.write(key->id());

        const KeyProportional* proportional = key->proportionalState();
        aOut.write(static_cast<bool>(proportional));
        if (proportional) {
            aOut.write(proportional->leftId);
            aOut.write(proportional->rightId);
            aOut.write(proportional->timeValid);
            aOut.write(proportional->timeWeight);
            for (int c = 0; c < TimeKey::kMaxProportionalComponents; ++c) {
                aOut.write(proportional->valueValid[c]);
                aOut.write(proportional->valueWeight[c]);
            }
        }
    }
    return aOut.checkStream();
}

bool ChannelTrack::deserialize(Deserializer& aIn) {
    aIn.pushLogScope("ChannelTrack");

    float base = 0.0f;
    aIn.read(base);
    setBaseValue(base);

    int count = 0;
    aIn.read(count);
    if (count < 0 || 10000000 < count) {
        return aIn.errored("invalid channel key count");
    }

    clearKeys();
    for (int i = 0; i < count; ++i) {
        int frame = 0;
        aIn.read(frame);

        auto* key = new ChannelKey(mType, mComponent);
        key->setFrame(frame);
        if (!key->deserialize(aIn)) {
            delete key;
            return aIn.errored("invalid channel key value");
        }

        int id = 0;
        bool proportional = false;
        aIn.read(id);
        aIn.read(proportional);
        KeyProportional state;
        if (proportional) {
            aIn.read(state.leftId);
            aIn.read(state.rightId);
            aIn.read(state.timeValid);
            aIn.read(state.timeWeight);
            for (int c = 0; c < TimeKey::kMaxProportionalComponents; ++c) {
                aIn.read(state.valueValid[c]);
                aIn.read(state.valueWeight[c]);
            }
        }

        if (id > 0) {
            TimeKey::reserveId(id);
            key->setId(id);
        }
        if (proportional) {
            key->setProportionalState(state);
        }
        if (!adoptKey(key)) {
            delete key;
            return aIn.errored("duplicate channel key frame");
        }
    }

    aIn.popLogScope();
    return aIn.checkStream();
}

} // namespace core
