#ifndef CTRL_PROPORTIONAL_PROP_KEYS_H
#define CTRL_PROPORTIONAL_PROP_KEYS_H

#include <QVector>
#include "core/TimeKey.h"
#include "core/TimeKeyType.h"
#include "core/TimeLine.h"

namespace ctrl {
namespace proportional {

// Proportional keys, the AUTHORING half of the timeline.
//
// A core::TimeKey may be marked proportional (core::TimeKey::Proportional):
// instead of owning its frame and component values outright, it keeps a stored
// fractional POSITION between its two nearest ABSOLUTE (non-proportional) keys
// of the same type, one weight per coordinate:
//
//   time:   wT    = (P.frame - L.frame) / (R.frame - L.frame)
//   value:  wV[c] = (P.value[c] - L.value[c]) / (R.value[c] - L.value[c])
//
// Playback never looks at any of this: the key's stored frame and values ARE
// the resolved positions, so the blender keeps reading the document it knows.
// This module is how those stored positions FOLLOW an edit of the bounds: the
// weights are re-applied, never re-derived, because re-deriving them from the
// rounded result of the previous projection accumulates quantization drift
// (project onto a span, round to a frame, read the weight back off that frame,
// project onto another span: the key walks away a little every time).
//
// The weights are therefore STORED (core::TimeKey::Proportional) and rebased
// only when the nearest absolute bounds CHANGE - measured by the keys' stable
// ids, not by their frames or values. Moving a bound is the ordinary case and
// keeps the weights; a different key becoming the bound must re-derive them
// once from the key's current position.
//
// Everything here is pure ctrl logic over a real core::TimeLine: no UI, no GL,
// no widget (see tests/proportional_keys.cpp). Component values are read and
// written exclusively through ctrl::graph::Channel, so a coordinate is never
// read through one field and written through another.

struct Bounds {
    core::TimeKey* left = nullptr;
    core::TimeKey* right = nullptr;
};

// A span (frame or value) at or below this magnitude is treated as zero: the
// coordinate freezes instead of collecting an enormous weight from division by
// nearly nothing. Not a UI tolerance - a guard against float blow-up.
constexpr float kSpanEpsilon = 1e-6f;

// Nearest ABSOLUTE keys strictly before/after aKey.frame() in aMap.
// Proportional keys are skipped, so a run of proportional keys all resolve to
// the same absolute pair. aKey need not be present in aMap (a freshly made key
// resolves its neighbours by frame alone) and is never returned as its own
// bound. aMap must be the map of aKey's own type; negative frames are ordinary
// frames, and the map's frame order is what decides left from right.
Bounds findBounds(const core::TimeLine::MapType& aMap, const core::TimeKey& aKey);

// The weight state recomputed from aKey's CURRENT frame and component values
// against aBounds. This is the ONE place weights are derived; callers normally
// keep the result (setProportionalState) rather than calling it per edit.
// leftId/rightId are the bounds' ids (0 for a missing side); timeValid needs
// both bounds and a non-zero frame span; valueValid[c] additionally needs c to
// be a graphable component of the key's type. A non-zero value span gives the
// key its own value fraction; a FLAT span gives it the TIME fraction when the
// key sits on that flat value (the ordinary "marked while nothing moved" case),
// and otherwise leaves the coordinate frozen (a deliberately offset value has no
// fraction to preserve).
core::TimeKey::Proportional rebaseState(const core::TimeKey& aKey, const Bounds& aBounds);
// rebaseState() assigned onto the key (enabling it if necessary).
void rebase(core::TimeKey& aKey, const Bounds& aBounds);

// True when the key's stored bound ids differ from aBounds' ids - i.e. the
// key's weights were derived against another pair and must be rebased before
// they can follow an edit. Also true for a key with no state at all.
bool boundsChanged(const core::TimeKey& aKey, const Bounds& aBounds);

// The stored-weight projection of aKey's frame. False (and nothing written to
// aFrame) when the key has no state, its time weight is frozen, or a bound is
// missing. The key's own stored frame is NOT consulted: the weight is, so the
// answer is stable across repeated calls and across bound-frame changes.
bool projectedFrame(const core::TimeKey& aKey, const Bounds& aBounds, int* aFrame);

// The stored-weight projection of one component. False (and nothing written)
// when that component's weight is not valid: no bounds, non-graphable, or a flat
// pre-edit span whose key held a DIFFERENT value (a frozen coordinate).
bool projectedValue(const core::TimeKey& aKey, const Bounds& aBounds, int aComponent, float* aValue);

// Writes every valid component projection through graph::Channel::writeValue,
// so the key type's own clamp/int storage applies. Coordinates without a valid
// weight are left exactly as they are (the freeze).
void projectValues(core::TimeKey& aKey, const Bounds& aBounds);

// One value edit (or one live gesture) seen as a whole, so the proportional
// keys between the edited absolute keys follow it without drift and undo.
//
// Capture happens BEFORE the edit. Every proportional key of the group is
// remembered with its base component values and its base state; its next state
// is its stored state, or - when its bounds are stale - a rebase against the
// PRE-EDIT map, computed once and then held fixed. apply() therefore never
// re-derives a weight from an already projected value: it installs the fixed
// next state and reads only the bounds' current values.
//
// The caller edits the keys it names (aEditedKeys): those are never projected
// onto (their own weights are rebased from their new values each step, which is
// exact) and the rest of the proportional keys follow them. A key whose nearest
// bounds were REPLACED during the gesture (a new key became a bound) is rebased
// once from its current position, and that state is kept for the rest of the
// gesture - the one-time exception, not a per-apply re-derivation.
//
// One instance may hold several groups (one per (TimeLine, type)) because one
// graph gesture can edit several types at once. restore() puts every captured
// key's exact pre-edit values and state back; the caller's own edits are the
// caller's to undo.
class ValueReflow {
public:
    // Captures every proportional key of (aLine, aType), excluding the default
    // key. Call BEFORE the edit. Rebases stale bounds against the pre-edit map
    // and stores the fixed next weights. A second capture for the same
    // (line,type) only adds keys it has not captured yet.
    void capture(core::TimeLine& aLine, core::TimeKeyType aType, const QVector<core::TimeKey*>& aEditedKeys);

    // True when no key was captured (nothing for apply()/restore() to do).
    bool empty() const;

    // Projects the non-edited captured keys from their fixed weights and the
    // bounds' current values, rebasing (once) any key whose bounds were
    // replaced. Edited captured keys are rebased from their current values and
    // otherwise left alone. Idempotent: calling it repeatedly with the same
    // document state answers the same values.
    void apply();
    void modify() { apply(); }

    // Restores the captured keys' base component values and base proportional
    // state. Keys that have left the map are skipped.
    void restore();

private:
    struct Entry {
        core::TimeKey* key = nullptr;
        bool edited = false;
        core::TimeKey::Proportional baseState;
        core::TimeKey::Proportional nextState;
        float baseValues[core::TimeKey::kMaxProportionalComponents] = {};
    };
    struct Group {
        core::TimeLine* line = nullptr;
        core::TimeKeyType type = core::TimeKeyType_Move;
        QVector<Entry> entries;
    };

    QVector<Group> mGroups;
};

} // namespace proportional
} // namespace ctrl

#endif // CTRL_PROPORTIONAL_PROP_KEYS_H
