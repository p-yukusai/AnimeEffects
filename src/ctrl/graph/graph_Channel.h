#ifndef CTRL_GRAPH_CHANNEL_H
#define CTRL_GRAPH_CHANNEL_H

#include <QString>
#include <QVector>
#include "core/TimeKey.h"
#include "core/TimeKeyType.h"

namespace ctrl {
namespace graph {

// One graphable channel: a single numeric component of one key type (Move's X,
// Scale's Y, Blur's angle, ...). The graph editor draws one Curve per
// (layer, channel).
//
// Which components exist and how each is read is decided HERE and nowhere else:
// the scaler's fit, the curve sampler, the renderer and the hit-test all ask the
// Channel, so adding a key type to the graph is one edit in one file.
//
// Only types whose easing playback actually applies are graphable. Bone, pose,
// mesh and FFD keys are structural: pose and FFD carry ONE easing that shapes
// the whole key (core::easingStorageCount), not one per component, so drawing
// them as value curves would invent an interpolation the document does not
// have.
class Channel {
public:
    Channel();
    Channel(core::TimeKeyType aType, int aComponent);

    core::TimeKeyType type() const { return mType; }
    int component() const { return mComponent; }

    // Number of graphable components of a key type (0 = not graphable).
    static int componentCount(core::TimeKeyType aType);
    // Every graphable component of aType, in drawing order.
    static QVector<Channel> channelsOf(core::TimeKeyType aType);
    // Every graphable key type, in the timeline's key order.
    static QVector<core::TimeKeyType> graphableTypes();

    // The value this channel reads from one key, using the same accessor the
    // blender uses for that channel. Integral channels (HSV) read as their
    // stored integer, because that is what playback blends into.
    float value(const core::TimeKey& aKey) const;

    // Writes aValue back into the one component this channel names, on the
    // key's OWN Data (so every component the graph does not draw is carried
    // through), through the same setter the timeline's own value edits use - so
    // the key type's own clamp still applies. The exact inverse of value(): a
    // write followed by a read answers aValue for every component except HSV's,
    // which stores an int and answers its truncation. False for a hand-made
    // channel naming no real component, which then writes nothing.
    //
    // This is the graph's one value WRITE, next to its one value READ above:
    // both switches live in this file, so a component can never be read and
    // written through different fields.
    bool writeValue(core::TimeKey& aKey, float aValue) const;

    // True when playback interpolates between this channel's keys. False for a
    // step-held channel (the image offset): the graph draws those as steps,
    // because a curve there would not be what plays.
    bool isInterpolated() const;

    // The component's own label ("X", "Y", "Angle", ...).
    QString componentName() const;
    // The channel's label for a row or caption ("Move X").
    QString name() const;

    bool operator==(const Channel& aRhs) const {
        return mType == aRhs.mType && mComponent == aRhs.mComponent;
    }
    bool operator!=(const Channel& aRhs) const { return !(*this == aRhs); }

private:
    core::TimeKeyType mType;
    int mComponent;
};

} // namespace graph
} // namespace ctrl

#endif // CTRL_GRAPH_CHANNEL_H
