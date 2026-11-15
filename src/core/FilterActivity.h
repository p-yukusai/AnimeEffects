#ifndef CORE_FILTERACTIVITY_H
#define CORE_FILTERACTIVITY_H

#include <QList>
#include "core/FilterFrame.h"
#include "core/TimeKeyExpans.h"

namespace core {

// An HSV value is a no-op when it does not change the pixel color: additive hue 0,
// sat/val at 100, and not absolute. Such a value must not isolate the node for a
// filter pass. (This lives here, not in one node's anonymous namespace, because the
// layer and the folder must answer the identity question the same way.)
inline bool hsvDataIsIdentity(const QList<int>& aHSV) {
    return aHSV.size() < 4 || (aHSV[0] == 0 && aHSV[1] == 100 && aHSV[2] == 100 && aHSV[3] == 0);
}

// IS THIS FILTER DOING ANYTHING ON THIS FRAME?
//
// The question is asked of the BLENDED expansions, never of the key map. A value
// edited while a lane has no explicit key is written to that lane's BASE VALUE
// (see gui/prop/prop_KeyAccessor.cpp's editFrame, "Model 2"), and TimeKeyBlender
// reads the base value between keys - falling back to the type's identity Data
// only when there is none. So the blended value IS the picture, and "the filter is
// enabled AND that value is above the type's identity epsilon" is the only honest
// test of live.
//
// The renderer used to ask a different question - "the lane has explicit keys and
// this frame is at or after the first key" - as a proxy for activity. That threw
// away exactly the static value the panel showed (and Wave already applied), and it
// pinned every filter to its first key. The second part was wrong on the app's own
// terms: playback holds a property's first key BEFORE the first key
// (src/ctrl/GraphEditor.cpp:2367-2368 - "playback holds the first key before the
// first key"), and no setting expresses "start at the first key". The consequence
// is deliberate: a keyed blur/HSV now also applies before its first key, exactly
// as it plays.
//
// The thresholds are the ones each type already documents: HSV's inert Data
// above, FilterFrame::kMinActiveBlurRadius (a visible-pixel epsilon, not a step, so
// an interpolated amount ramps in continuously) and WaveKey::Data::isActive
// (kMinActiveStrength). Any other type has no blended filter value here, so it
// answers false.
inline bool filterValueIsActive(TimeKeyType aType, const TimeKeyExpans& aExpans) {
    switch (aType) {
    case TimeKeyType_HSV:
        return !hsvDataIsIdentity(aExpans.hsv().hsv());
    case TimeKeyType_Blur:
        return aExpans.maxBlurRadius() > FilterFrame::kMinActiveBlurRadius;
    case TimeKeyType_Wave:
        return aExpans.wave().isActive();
    default:
        return false;
    }
}

} // namespace core

#endif // CORE_FILTERACTIVITY_H
