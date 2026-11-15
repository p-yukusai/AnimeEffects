#ifndef CORE_TIMEKEYTYPE
#define CORE_TIMEKEYTYPE

namespace core {

enum TimeKeyType {
    TimeKeyType_Move,
    TimeKeyType_Rotate,
    TimeKeyType_Scale,
    TimeKeyType_Depth,
    TimeKeyType_Opa,
    TimeKeyType_Bone,
    TimeKeyType_Pose,
    TimeKeyType_Mesh,
    TimeKeyType_FFD,
    TimeKeyType_Image,
    TimeKeyType_HSV,
    TimeKeyType_Blur,
    TimeKeyType_Wave,
    TimeKeyType_Mask,
    TimeKeyType_TERM
};

// The key types that are FILTERS: a post-process applied to an isolated layer or
// folder composite, rather than a property of the node itself. This is the list
// the filter stack UI offers and the renderer reads; a new filter type is added
// here (and to the catalog in gui/qml/FilterStackModel.cpp) before it can be
// stacked.
inline bool isFilterType(TimeKeyType aType) {
    return aType == TimeKeyType_HSV || aType == TimeKeyType_Blur || aType == TimeKeyType_Wave;
}

// One entry in a node's filter stack. `enabled` is the eye toggle; the type names
// the animation lane that carries its values. The stack stores ORDER and the
// toggle; the keys stay in TimeLine::mMap, so every existing animation path keeps
// working.
struct FilterEntry {
    TimeKeyType type = TimeKeyType_TERM;
    bool enabled = true;

    FilterEntry() = default;
    FilterEntry(TimeKeyType aType, bool aEnabled = true): type(aType), enabled(aEnabled) {}

    bool operator==(const FilterEntry& aOther) const { return type == aOther.type && enabled == aOther.enabled; }
    bool operator!=(const FilterEntry& aOther) const { return !(*this == aOther); }
};

} // namespace core

#endif // CORE_TIMEKEYTYPE
