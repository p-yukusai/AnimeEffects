#include "ctrl/proportional/prop_Keys.h"

#include <QtGlobal>
#include "ctrl/graph/graph_Channel.h"
#include "core/ChannelKey.h"
#include "core/ChannelSpec.h"

namespace ctrl {
namespace proportional {

namespace {

// The ids aBounds would store: 0 for a missing side (core::TimeKey::Proportional
// uses 0 as "no bound on that side", and a real key id is always positive).
int boundId(const core::TimeKey* aKey) { return aKey ? aKey->id() : 0; }

// True when aState's stored bounds ARE aBounds' keys. Frame and value changes
// deliberately do not matter: only a different key becoming the bound does.
bool sameBoundIds(const core::TimeKey::Proportional& aState, const Bounds& aBounds) {
    return aState.leftId == boundId(aBounds.left) && aState.rightId == boundId(aBounds.right);
}

// The number of coordinates this module may store weights for: the type's
// graphable components, capped by the fixed state array. Structural types
// (Bone/Pose/Mesh/FFD) count 0, so they read no component at all.
int weightComponentCount(core::TimeKeyType aType) {
    return qMin(ctrl::graph::Channel::componentCount(aType), core::TimeKey::kMaxProportionalComponents);
}

// True when component aComponent's pre-edit value span is flat AND the key sits on
// that flat value, i.e. the key has no value fraction of its own but its TIME
// fraction is a meaningful stand-in (see rebaseState's flat-span rule). Used both
// by rebaseState and by ValueReflow's migration of states written before that rule.
int keyComponent(const core::TimeKey& aKey);

bool flatValueOnKey(const core::TimeKey& aKey, const Bounds& aBounds, int aComponent) {
    if (!aBounds.left || !aBounds.right)
        return false;
    const int own = keyComponent(aKey);
    if (own >= 0 && aComponent != own)
        return false;
    const graph::Channel channel(aKey.type(), aComponent);
    const double leftValue = static_cast<double>(channel.value(*aBounds.left));
    const double rightValue = static_cast<double>(channel.value(*aBounds.right));
    if (qAbs(rightValue - leftValue) > static_cast<double>(kSpanEpsilon))
        return false;
    return qAbs(static_cast<double>(channel.value(aKey)) - leftValue)
        <= static_cast<double>(kSpanEpsilon);
}

double frameOf(const core::TimeKey& aKey) { return static_cast<double>(aKey.frame()); }

// A channel-backed key owns exactly one component; a bundle key owns all of its
// type's graphable components.
int keyComponent(const core::TimeKey& aKey) {
    if (const auto* channel = dynamic_cast<const core::ChannelKey*>(&aKey)) {
        return channel->component();
    }
    return -1;
}

} // namespace

//-------------------------------------------------------------------------------------------------
Bounds findBounds(const core::TimeLine::MapType& aMap, const core::TimeKey& aKey) {
    Bounds bounds;
    const int frame = aKey.frame();

    // The map is frame-ordered, so lowerBound() lands on the first entry at or
    // after the key's frame: everything before it is earlier, everything from
    // it on is later or equal. Equal entries are skipped on both walks, which
    // is what keeps aKey from becoming its own bound when it is in the map.
    auto itr = aMap.lowerBound(frame);

    // left: walk back through the strictly earlier entries.
    auto left = itr;
    while (left != aMap.begin()) {
        --left;
        core::TimeKey* candidate = left.value();
        if (!candidate || candidate == &aKey || candidate->isProportional())
            continue;
        bounds.left = candidate;
        break;
    }

    // right: walk forward, dropping every entry at or before the key's frame
    // (the key itself, and any proportional key sharing its frame).
    auto right = itr;
    while (right != aMap.end()) {
        core::TimeKey* candidate = right.value();
        if (right.key() <= frame || !candidate || candidate == &aKey || candidate->isProportional()) {
            ++right;
            continue;
        }
        bounds.right = candidate;
        break;
    }

    return bounds;
}

//-------------------------------------------------------------------------------------------------
core::TimeKey::Proportional rebaseState(const core::TimeKey& aKey, const Bounds& aBounds) {
    core::TimeKey::Proportional state;

    // The ids are recorded even with one bound missing: they are what says
    // which pair the (frozen) weights belong to, so moving that one bound's
    // frame does not force a re-derivation.
    state.leftId = boundId(aBounds.left);
    state.rightId = boundId(aBounds.right);

    if (!aBounds.left || !aBounds.right) {
        // A key without both bounds is inert, never almost-valid.
        return state;
    }

    const double leftFrame = frameOf(*aBounds.left);
    const double rightFrame = frameOf(*aBounds.right);
    const double frameSpan = rightFrame - leftFrame;
    if (qAbs(frameSpan) > static_cast<double>(kSpanEpsilon)) {
        state.timeValid = true;
        state.timeWeight = static_cast<float>((frameOf(aKey) - leftFrame) / frameSpan);
    }
    // else: the two bounds share a frame, so there is no fraction to store; the
    // frame coordinate freezes at the key's current one.

    const int own = keyComponent(aKey);
    const int count = own >= 0 ? 1 : weightComponentCount(aKey.type());
    for (int index = 0; index < count; ++index) {
        const int component = own >= 0 ? own : index;
        const graph::Channel channel(aKey.type(), component);
        const double leftValue = static_cast<double>(channel.value(*aBounds.left));
        const double rightValue = static_cast<double>(channel.value(*aBounds.right));
        const double keyValue = static_cast<double>(channel.value(aKey));
        const double valueSpan = rightValue - leftValue;
        if (qAbs(valueSpan) <= static_cast<double>(kSpanEpsilon)) {
            // A FLAT pre-edit segment has no value fraction of its own. When the
            // key sits ON that flat value - the ordinary case of marking a run of
            // equal-valued keys proportional, then dragging one bound - the time
            // fraction is the only fraction the key has, so it becomes the value
            // fraction: the key then follows the first bound edit instead of
            // staying inert forever. A key that holds a DIFFERENT value (a
            // deliberately offset key on a flat segment) still freezes, because
            // there is no fraction to preserve and inventing one would move it.
            if (state.timeValid && flatValueOnKey(aKey, aBounds, component)) {
                state.valueValid[component] = true;
                state.valueWeight[component] = state.timeWeight;
            }
            continue;
        }

        state.valueValid[component] = true;
        state.valueWeight[component] = static_cast<float>((keyValue - leftValue) / valueSpan);
    }

    return state;
}

void rebase(core::TimeKey& aKey, const Bounds& aBounds) { aKey.setProportionalState(rebaseState(aKey, aBounds)); }

//-------------------------------------------------------------------------------------------------
bool boundsChanged(const core::TimeKey& aKey, const Bounds& aBounds) {
    const core::TimeKey::Proportional* state = aKey.proportionalState();
    return !state || !sameBoundIds(*state, aBounds);
}

//-------------------------------------------------------------------------------------------------
bool projectedFrame(const core::TimeKey& aKey, const Bounds& aBounds, int* aFrame) {
    const core::TimeKey::Proportional* state = aKey.proportionalState();
    if (!state || !state->timeValid || !aBounds.left || !aBounds.right)
        return false;

    // The projection is the exact solution of the stored fraction: it is never
    // read back off the key's frame, which is what makes it drift-free.
    const double leftFrame = frameOf(*aBounds.left);
    const double rightFrame = frameOf(*aBounds.right);
    const int frame =
        qRound(leftFrame + static_cast<double>(state->timeWeight) * (rightFrame - leftFrame));
    if (aFrame)
        *aFrame = frame;
    return true;
}

bool projectedValue(const core::TimeKey& aKey, const Bounds& aBounds, int aComponent, float* aValue) {
    const core::TimeKey::Proportional* state = aKey.proportionalState();
    if (!state || !aBounds.left || !aBounds.right)
        return false;
    if (aComponent < 0 || core::TimeKey::kMaxProportionalComponents <= aComponent)
        return false;
    const int own = keyComponent(aKey);
    if (own >= 0 && aComponent != own)
        return false;
    if (aComponent >= weightComponentCount(aKey.type()))
        return false;
    if (!state->valueValid[aComponent])
        return false;

    const graph::Channel channel(aKey.type(), aComponent);
    const double leftValue = static_cast<double>(channel.value(*aBounds.left));
    const double rightValue = static_cast<double>(channel.value(*aBounds.right));
    const float value =
        static_cast<float>(leftValue + static_cast<double>(state->valueWeight[aComponent]) * (rightValue - leftValue));
    if (aValue)
        *aValue = value;
    return true;
}

void projectValues(core::TimeKey& aKey, const Bounds& aBounds) {
    const core::TimeKey::Proportional* state = aKey.proportionalState();
    if (!state)
        return;

    const int own = keyComponent(aKey);
    const int count = own >= 0 ? 1 : weightComponentCount(aKey.type());
    for (int index = 0; index < count; ++index) {
        const int component = own >= 0 ? own : index;
        if (!state->valueValid[component])
            continue; // frozen coordinate: leave the stored value untouched

        float value = 0.0f;
        if (!projectedValue(aKey, aBounds, component, &value))
            continue;
        // The channel's own write, so the key type's clamp and int storage are
        // the ones every other value edit goes through.
        graph::Channel(aKey.type(), component).writeValue(aKey, value);
    }
}

//-------------------------------------------------------------------------------------------------
void ValueReflow::capture(core::TimeLine& aLine, core::TimeKeyType aType,
                          const QVector<core::TimeKey*>& aEditedKeys) {
    // One group per (line, type): a second capture in the same gesture extends
    // the group instead of splitting its restore base.
    Group* group = nullptr;
    for (Group& candidate : mGroups) {
        if (candidate.line == &aLine && candidate.type == aType) {
            group = &candidate;
            break;
        }
    }
    if (!group) {
        Group fresh;
        fresh.line = &aLine;
        fresh.type = aType;
        mGroups.append(fresh);
        group = &mGroups.last();
    }

    const auto captureMap = [&](const core::TimeLine::MapType& aMap, int aComponent) {
        const bool channelBacked = aComponent >= 0;
        const int count = channelBacked ? 1 : weightComponentCount(aType);
        for (core::TimeKey* key : aMap) {
            if (!key || key->isBaseValue() || !key->isProportional())
                continue;

            bool alreadyCaptured = false;
            for (const Entry& entry : group->entries) {
                if (entry.key == key) {
                    alreadyCaptured = true;
                    break;
                }
            }
            if (alreadyCaptured)
                continue;

            Entry entry;
            entry.key = key;
            entry.component = aComponent;
            entry.edited = aEditedKeys.contains(key);
            entry.baseState = *key->proportionalState();
            for (int i = 0; i < count; ++i) {
                const int component = channelBacked ? aComponent : i;
                entry.baseValues[channelBacked ? 0 : i] =
                    graph::Channel(aType, component).value(*key);
            }

            const Bounds bounds = findBounds(aMap, *key);
            const bool stale = boundsChanged(*key, bounds);
            entry.nextState = stale ? rebaseState(*key, bounds) : entry.baseState;

            if (!stale) {
                for (int i = 0; i < count; ++i) {
                    const int component = channelBacked ? aComponent : i;
                    if (!entry.nextState.valueValid[component] && entry.nextState.timeValid
                        && flatValueOnKey(*key, bounds, component)) {
                        entry.nextState.valueValid[component] = true;
                        entry.nextState.valueWeight[component] = entry.nextState.timeWeight;
                    }
                }
            }

            group->entries.append(entry);
        }
    };

    if (core::TimeLine::usesChannelTracks(aType)) {
        for (int c = 0; c < core::propertyChannelCount(aType); ++c) {
            captureMap(aLine.channelMap(aType, c), c);
        }
    } else {
        captureMap(aLine.map(aType), -1);
    }
}

bool ValueReflow::empty() const {
    for (const Group& group : mGroups) {
        if (!group.entries.isEmpty())
            return false;
    }
    return true;
}

void ValueReflow::apply() {
    for (Group& group : mGroups) {
        if (!group.line)
            continue;

        for (Entry& entry : group.entries) {
            core::TimeKey* key = entry.key;
            if (!key)
                continue;
            const core::TimeLine::MapType& map = entry.component >= 0
                ? group.line->channelMap(group.type, entry.component)
                : group.line->map(group.type);

            // The key must still be the map's entry at its own frame: a key
            // deleted or replaced during the gesture has left this reflow.
            if (map.value(key->frame()) != key)
                continue;

            const Bounds bounds = findBounds(map, *key);

            if (entry.edited) {
                rebase(*key, bounds);
                continue;
            }

            if (!sameBoundIds(entry.nextState, bounds)) {
                rebase(*key, bounds);
                entry.nextState = *key->proportionalState();
            } else {
                key->setProportionalState(entry.nextState);
            }

            projectValues(*key, bounds);
        }
    }
}

void ValueReflow::restore() {
    for (Group& group : mGroups) {
        if (!group.line)
            continue;

        for (Entry& entry : group.entries) {
            core::TimeKey* key = entry.key;
            if (!key)
                continue;
            const core::TimeLine::MapType& map = entry.component >= 0
                ? group.line->channelMap(group.type, entry.component)
                : group.line->map(group.type);
            if (map.value(key->frame()) != key)
                continue;

            if (entry.component >= 0) {
                graph::Channel(group.type, entry.component).writeValue(*key, entry.baseValues[0]);
            } else {
                const int count = weightComponentCount(group.type);
                for (int component = 0; component < count; ++component) {
                    graph::Channel(group.type, component).writeValue(*key, entry.baseValues[component]);
                }
            }
            key->setProportionalState(entry.baseState);
        }
    }
}

} // namespace proportional
} // namespace ctrl
