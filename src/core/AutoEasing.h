#ifndef CORE_AUTO_EASING
#define CORE_AUTO_EASING

#include "util/Easing.h"

namespace core {
class TimeKey;
}

namespace core {

// util::Easing's Auto/AutoClamped handle modes derive a segment's bezier control
// points from the keys around it, so they need something the easing itself never
// carries: the VALUE of those neighbouring keys. These two functions are that
// bridge, and they are the only place the "which number does an Auto handle
// read" decision is made.

// The scalar the Auto easing reads from aKey, in the units of the segment
// aFrom -> aTo. aComponent is the graphable component being evaluated: a Move
// segment's x curve is shaped by its x values, its y curve by its y values, so
// per-channel Auto handles each read their own axis. Pose and FFD carry one
// shared easing, so their component 0 is the component that moves MOST across
// the segment (a y-only deformation is not shaped by a still x). False when the
// key type carries no such scalar at all (bone, mesh - and image, whose offset
// is step-held and so has no rate to shape), when aKey is not one of the
// segment's own type, or when aComponent names no component of the type.
bool autoEasingValue(const TimeKey& aKey, const TimeKey& aFrom, const TimeKey& aTo, int aComponent, float& aOut);

// The context the Auto easing of the segment aFrom -> aTo evaluates with, taken
// from the neighbouring keys of the same channel. aComponent selects which
// component's values are normalized (see autoEasingValue). Either neighbouring
// key may be null (the first key of a channel has no previous one, the last no
// next), which is exactly what makes that end of the segment fall back to
// Blender's endpoint rule. Frames are relative to aFrom's frame and values are
// normalized to the segment's own change, both as util::Easing::AutoContext
// documents.
//
// An EMPTY context is returned when the segment has no shape to derive - a
// channel that does not change across it, or a key type with no scalar. The Auto
// easing then evaluates as Linear, which is the right answer there: nothing
// changes, so no curve can be told apart from any other.
util::Easing::AutoContext autoEasingContext(
    const TimeKey& aFrom, const TimeKey& aTo, int aComponent,
    const TimeKey* aPrev, const TimeKey* aNext);

} // namespace core

#endif // CORE_AUTO_EASING
