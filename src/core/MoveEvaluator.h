#ifndef CORE_MOVEEVALUATOR_H
#define CORE_MOVEEVALUATOR_H

#include <QVector2D>

#include "core/PropertyTrack.h"
#include "core/TimeInfo.h"

namespace core {

// Move is the one property whose channels can deliberately couple: its spatial
// Catmull-Rom path needs both axes to have the same knots. The end-state rule is
// explicit, not hidden in storage:
//
//   * if the X and Y channel tracks have the same key-frame set, the evaluator
//     builds the spatial spline and returns its x at X's eased rate and its y at
//     Y's;
//   * if the key frames differ, the channels are sampled independently as scalar
//     eased curves.
//
// The graph panel and the blender both call this evaluator, so the drawn curve
// and the played curve cannot disagree. Centroid is a pair of hidden channels
// (components 2 and 3) and samples like any other channel.
class MoveEvaluator {
public:
    // True when X and Y have the same key frames: the spatial path is applicable.
    static bool hasPairedChannels(const PropertyTrack& aProperty);

    static QVector2D samplePosition(const PropertyTrack& aProperty, const TimeInfo& aTime);
    static QVector2D sampleCentroid(const PropertyTrack& aProperty, const TimeInfo& aTime);

    // The value a graph curve for `aComponent` draws at aTime. For position X/Y
    // this includes the spatial-spline coupling when it applies; centroid and a
    // non-paired property use the channel's own scalar track.
    static float sampleChannel(
        const PropertyTrack& aProperty, int aComponent, const TimeInfo& aTime
    );

private:
    static bool splineIsLinearAt(const PropertyTrack& aProperty, int aFrame);
};

} // namespace core

#endif // CORE_MOVEEVALUATOR_H
