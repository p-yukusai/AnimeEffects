#ifndef CTRL_GRAPH_CURVE_H
#define CTRL_GRAPH_CURVE_H

#include <QPointF>
#include <QVector>
#include "core/TimeKey.h"
#include "core/TimeLine.h"
#include "util/Easing.h"
#include "ctrl/graph/graph_Channel.h"

namespace ctrl {
namespace graph {

// One channel's curve over one layer: the keys, the segments between them, and
// the bezier tangent handles a Custom easing exposes.
//
// Sampling mirrors core::TimeKeyBlender EXACTLY, and that is the whole point of
// this class: the graph has to draw what playback plays. The rate therefore
// always comes from util::Easing::calculate with the arguments the blender
// passes (t = frame - p0.frame, b = 0, c = 1, d = p1.frame - p0.frame taken from
// the `from` key's easing), and the Move channel goes through the same
// util::FergusonCoonsSpline with the same Catmull-Rom velocities, so a spatial
// move path keeps its curvature in the graph.
//
// The curve does not own the timeline: it reads the key map on demand, so it
// stays correct while the document is edited underneath it.
class Curve {
public:
    Curve();
    Curve(const core::TimeLine& aTimeLine, const Channel& aChannel);

    const Channel& channel() const { return mChannel; }
    // The layer this curve belongs to.
    const core::TimeLine& timeLine() const { return *mTimeLine; }
    bool isEmpty() const { return keys().isEmpty(); }

    // The channel's keys in frame order (empty when the map holds none).
    QVector<const core::TimeKey*> keys() const;
    // The value stored on aKey for this channel.
    float valueOf(const core::TimeKey& aKey) const { return mChannel.value(aKey); }

    // The value the document holds at aFrame - the same number playback
    // evaluates, including the no-key, single-key and step-held cases.
    float sample(core::Frame aFrame) const;

    // One drawable segment: the two bounding keys and the easing that shapes the
    // curve between them (always the `from` key's easing, as in playback).
    struct Segment {
        const core::TimeKey* from;
        const core::TimeKey* to;
        float fromValue;
        float toValue;
        util::Easing::Param easing;
    };
    int segmentCount() const;
    // The drawable segment at aIndex (0 <= aIndex < segmentCount()).
    Segment segment(int aIndex) const;

    // A Custom easing's two tangent handles, in NORMALIZED easing space: x in
    // [0,1] along the segment (its unit of time) and y free to overshoot the
    // segment's value span. That normalization is what makes a handle mean the
    // same thing at any zoom, and it is the space core::TimeKeyBlender consumes.
    // aHandleIndex 0 is the handle leaving `from`, 1 the handle arriving at `to`.
    bool isCustom(const Segment& aSegment) const;
    static QPointF handle(const Segment& aSegment, int aHandleIndex);
    // Writes a handle back into an easing param: x is clamped into [0,1] (a
    // cubic bezier requires it) while y stays free, so overshoot remains
    // expressible.
    static void setHandle(util::Easing::Param& aEasing, int aHandleIndex, const QPointF& aNormalized);

    // Polyline of the channel across [aFrom,aTo] in (frame, value) space; the
    // caller maps it through the Scaler. aSamplesPerSegment bounds the error of
    // the analytic curves (one sample per frame is NOT enough: Expo and Elastic
    // are steep inside a single frame at high zoom).
    void buildPolyline(core::Frame aFrom, core::Frame aTo, int aSamplesPerSegment, QVector<QPointF>& aOut) const;

    // The easing param that shapes the segment starting at one key, for a
    // graphable type. Null for a type that carries no easing.
    static const util::Easing::Param* easingOf(const core::TimeKey& aKey, core::TimeKeyType aType);
    static util::Easing::Param* easingOf(core::TimeKey& aKey, core::TimeKeyType aType);

private:
    // The key whose segment starts at or before aFrame, and its successor.
    // Mirrors the gatherer's no-key / single / sandwiched cases.
    const core::TimeKey* findLower(core::Frame aFrame) const;
    float sampleBetween(const core::TimeKey& aFrom, const core::TimeKey& aTo, core::Frame aFrame) const;

    const core::TimeLine* mTimeLine;
    Channel mChannel;
};

} // namespace graph
} // namespace ctrl

#endif // CTRL_GRAPH_CURVE_H
