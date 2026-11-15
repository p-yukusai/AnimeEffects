#ifndef CTRL_GRAPH_CURVE_H
#define CTRL_GRAPH_CURVE_H

#include <QPointF>
#include <QVector>
#include "core/TimeKey.h"
#include "core/TimeLine.h"
#include "util/Range.h"
#include "util/Easing.h"
#include "util/EasingSegment.h"
#include "util/FergusonCoonsSpline.h"
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
        // The TO key's incoming handle mode. The arriving control point belongs
        // to the TO key even though its STORED value is the FROM param's x2/y2,
        // so a segment needs both keys' modes to derive its two sides.
        util::Easing::HandleMode inMode = util::Easing::HandleMode_Aligned;
    };
    int segmentCount() const;
    // The drawable segment at aIndex (0 <= aIndex < segmentCount()).
    Segment segment(int aIndex) const;

    // A Custom easing's two tangent handles, in NORMALIZED easing space: x in
    // [0,1] along the segment (its unit of time) and y free to overshoot the
    // segment's value span. That normalization is what makes a handle mean the
    // same thing at any zoom, and it is the space core::TimeKeyBlender consumes.
    // aHandleIndex 0 is the handle leaving `from`, 1 the handle arriving at `to`.
    // These read the STORED pair (what a gesture writes back); use
    // effectiveHandles() for the pair the curve is actually drawn with.
    //
    // "Fully authored": a Custom curve whose BOTH sides read their stored value.
    // This is the STORED-PAIR question, never a per-side one: a segment with one
    // derived side is not fully authored, but it still has a stored handle on the
    // other side (see derivesHandle). Anything that publishes or draws handles one
    // at a time must ask derivesHandle for that side.
    bool isCustom(const Segment& aSegment) const;
    // Whether ONE side of the segment is derived (Vector/Auto/AutoClamped) rather
    // than stored. aHandleIndex is the handlePoint order: 0 is the FROM key's
    // outgoing side (its handleOut), 1 the TO key's incoming one (inMode). This is
    // the per-side rule the drawing layer and the hit-test share: a derived side
    // is a ghost, a stored side is the document's handle.
    static bool derivesHandle(const Segment& aSegment, int aHandleIndex);
    static QPointF handle(const Segment& aSegment, int aHandleIndex);
    // Writes a handle back into an easing param: x is clamped into [0,1] (a
    // cubic bezier requires it) while y stays free, so overshoot remains
    // expressible.
    static void setHandle(util::Easing::Param& aEasing, int aHandleIndex, const QPointF& aNormalized);

    // THE PAIR THE SEGMENT IS DRAWN AND GRABBED WITH, per side: a Free/Aligned
    // side is its stored control point, a derived side (Vector/Auto/
    // AutoClamped) is the one util::Easing::controlPoint derives from the
    // neighbours. The per-side decision is derivesHandle (publication's query),
    // and controlPoint is what playback's Easing::calculate runs for that side,
    // so the two sides cannot be classified one way and evaluated another. False
    // when the segment exposes no handles at all (an analytic easing, or a null
    // segment). This is the ONE mapping the drawing layer, the editor's hit-test
    // and a drag's starting point all use, so they cannot disagree about where a
    // dot is.
    //
    // FALSE IS ALSO THE ANSWER FOR A DERIVED INCOMING MODE ON AN ANALYTIC SEGMENT,
    // and the panel must publish nothing for it: a segment's curve is its FROM
    // key's param, so an analytic FROM never calls controlPoint and the TO key's
    // handleIn is inert for this segment. (A key still OWNS both tangents - the
    // incoming one becomes live the moment the segment's FROM is Custom, which is
    // what the Auto preset now arranges by also writing the previous key.)
    bool effectiveHandles(const Segment& aSegment, QPointF aOut[2]) const;

    // A segment's DERIVED pair, or empty when neither side derives one (both
    // modes stored, an analytic easing, the last key of a channel, or a type
    // that carries no easing). The pair is the EFFECTIVE one - a stored side is
    // returned as its stored control point - because the caller draws a mixed
    // segment's two dots from one pair. Kept as a QVector (the panel's handle
    // pass wants a pair-or-empty answer) and in the same normalized space
    // handle() reads.
    QVector<QPointF> ghostHandles(const core::TimeKey& aFrom) const;

    // Polyline of the channel across [aFrom,aTo] in (frame, value) space; the caller maps it through
    // the Scaler. aSamplesPerSegment bounds the error of the analytic curves (one sample per frame is
    // NOT enough: Expo and Elastic are steep inside a single frame at high zoom) and is spent on the
    // part of each segment the window SHOWS, plus each touched segment's own endpoints for the caller
    // to clip against. The frames outside the keys follow setLoop below: a hold SPANS the window even
    // when the window misses the keys entirely (a horizontal segment at the nearest key's value),
    // because a polyline of one point is not a drawable curve.
    void buildPolyline(core::Frame aFrom, core::Frame aTo, int aSamplesPerSegment, QVector<QPointF>& aOut) const;

    // How the frames OUTSIDE the keys are evaluated, which is the project's own playback rule
    // (core::TimeKeyGatherer reads core::TimeInfo the same way):
    //   * inside the frame range, loop ON  - the value is the eased segment from the LAST key back
    //     to the FIRST, wrapped by the range's span (the animation repeats over the range, so the
    //     first key sits one span ahead of the last);
    //   * inside it, loop OFF - the value HOLDS: the first key before it, the last key after it;
    //   * OUTSIDE the range the frame is CLAMPED into it, so the curve is flat at the range's edge
    //     value - the animation is defined over the range and a repeated period is not something a
    //     curve should extrapolate to infinity.
    // A single key, or a range with no span, means no wrap.
    void setLoop(bool aLoop, const util::Range& aRange);
    bool isLooped() const;


    // The easing param that shapes one key's segment of a graph channel, for a
    // graphable type and one of its components. Null for a type that carries no
    // easing (bone, mesh) and for a component the type does not have. Pose and
    // FFD store one shared param at index 0 (their channel count is 0, but the
    // timeline chip and the header's quick easings edit them), so they answer it.
    static const util::Easing::Param* easingOf(const core::TimeKey& aKey, core::TimeKeyType aType, int aComponent);
    static util::Easing::Param* easingOf(core::TimeKey& aKey, core::TimeKeyType aType, int aComponent);

private:
    // The key whose segment starts at or before aFrame, and its successor.
    // Mirrors the gatherer's no-key / single / sandwiched cases.
    const core::TimeKey* findLower(core::Frame aFrame) const;
    // The keys either side of the segment aFrom -> aTo in this channel's map:
    // the blender gatherer's point(-1) / point(2). Either is null at an end of
    // the key list. They are what a derived (Auto/AutoClamped) handle reads,
    // and what the Move spline takes its Catmull-Rom velocities from.
    void neighborsOf(const core::TimeKey& aFrom, const core::TimeKey& aTo,
                     const core::TimeKey*& aPrev, const core::TimeKey*& aNext) const;
    float sampleBetween(const core::TimeKey& aFrom, const core::TimeKey& aTo, core::Frame aFrame) const;
    // The value aAt (a float frame) inside the pair aFrom/aTo over a length of aD frames: the easing
    // belongs to aFrom, and aD is the pair's effective length - the frames' own difference, or the
    // loop span for a wrapped pair.
    float sampleOver(const core::TimeKey& aFrom, const core::TimeKey& aTo, float aAt, float aD) const;

    // EVERYTHING A SEGMENT'S EVALUATION NEEDS THAT DOES NOT DEPEND ON THE SAMPLE.
    //
    // `sampleOver` used to rebuild all of this per point: the channel values, the easing
    // params, the neighbour walk, the AutoContext and two derived control points. A drawn
    // curve samples one segment `aSamplesPerSegment` times per repaint, so the preparation
    // was paid ~600 times for a segment that is the same every time. Measured (Release):
    // one-shot derived evaluation 78.9 ns, prepared 5.7 ns, and the panel's 8 curves x 600
    // samples 379 us -> 27 us.
    //
    // IT IS NOT CACHED ACROSS CALLS, and that is a correctness decision, not an unfinished
    // optimisation: a memo keyed by the two keys (and their span) cannot see an EDIT that
    // leaves the keys where they are - changing a key's spline type, its handle modes or its
    // easing rewrites the document without moving a pointer, and the memo would serve the
    // curve from before the edit. tests/graph_curve caught exactly that (a Move channel
    // switched to Linear kept drawing its Catmull-Rom bow). So the preparation is a VALUE
    // the caller holds for as long as its document state is the same - `buildPolyline` owns
    // one per segment for the length of its loop, which is where the win lives.
    struct Prepared {
        const core::TimeKey* from = nullptr;
        const core::TimeKey* to = nullptr;
        // The segment's effective length: the frames' own difference, or the loop span for
        // a wrapped pair. Part of the key, because the same two keys can be evaluated over
        // two different lengths.
        float d = 0.0f;
        bool usable = false;      // a Custom easing with an evaluable pair
        bool interpolated = true; // false = the step-held shape
        float v0 = 0.0f;
        float v1 = 0.0f;
        // Move's Catmull-Rom knots and HSV's truncation need the neighbours too, so they
        // are prepared with the rest.
        const core::TimeKey* prev = nullptr;
        const core::TimeKey* next = nullptr;
        // The FROM key's easing param - the one playback evaluates. A non-Custom type is
        // evaluated from this per sample (the closed-form table is a handful of flops), and
        // a Custom one uses the prepared segment below, which is where the cost was.
        const util::Easing::Param* param = nullptr;
        std::optional<util::EasingSegment> easing;
        // Move's arc-length spline, prepared for the same reason: its knots and its
        // 8-segment linearization table depend only on the two keys and their neighbours.
        // Measured (Release): vels+set+getByLinear 226 ns per point, getByLinear alone
        // 25.9 ns - i.e. 200 ns of the 270 ns a Move polyline point used to cost was
        // rebuilding a spline it already had.
        std::optional<util::FergusonCoonsSpline<QVector3D>> spline;
    };
    Prepared prepareOver(const core::TimeKey& aFrom, const core::TimeKey& aTo, float aD) const;
    // The value of a prepared segment at a frame (a float, inside the segment).
    float evaluateOver(const Prepared& aPrep, float aAt) const;

    const core::TimeLine* mTimeLine;
    Channel mChannel;
    bool mLoop;
    util::Range mLoopRange;
};

} // namespace graph
} // namespace ctrl

#endif // CTRL_GRAPH_CURVE_H
