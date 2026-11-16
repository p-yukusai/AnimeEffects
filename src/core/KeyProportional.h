#ifndef CORE_KEYPROPORTIONAL_H
#define CORE_KEYPROPORTIONAL_H

namespace core {

// A proportional key keeps stored fractional weights between its nearest
// ABSOLUTE neighbours of the same channel, so the resolved frame and values
// follow those neighbours when they move. The weights are stored rather than
// re-derived from the rounded positions: re-deriving would accumulate
// quantization drift over repeated edits.
//
// This used to be nested in TimeKey. It is a value record shared by every kind
// of authored key, so it has its own header: ChannelKey (the per-channel track
// model) must be able to hold one without dragging in the tree/selection
// machinery TimeKey owns.
struct KeyProportional {
    // Blur and HSV carry three graphable components, Wave five; no graphable
    // type has more. This is the size of the serialized per-component weight
    // arrays, so growing it is a FORMAT change.
    static constexpr int kMaxComponents = 5;

    int leftId = 0;   // 0 = no absolute bound on that side
    int rightId = 0;
    bool timeValid = false;
    float timeWeight = 0.0f;
    bool valueValid[kMaxComponents] = {};
    float valueWeight[kMaxComponents] = {};
};

} // namespace core

#endif // CORE_KEYPROPORTIONAL_H
