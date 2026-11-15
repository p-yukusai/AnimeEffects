#include "ctrl/graph/graph_Curve.h"

#include <QVector3D>

#include "XC.h"
#include "core/AutoEasing.h"
#include "core/KeyEasingAccess.h"
#include "core/BlurKey.h"
#include "core/DepthKey.h"
#include "core/HsvKey.h"
#include "core/ImageKey.h"
#include "core/MoveKey.h"
#include "core/OpaKey.h"
#include "core/RotateKey.h"
#include "core/ScaleKey.h"
#include "util/FergusonCoonsSpline.h"

namespace ctrl {
namespace graph {

// The range a curve holds before setLoop() is given one: an INVALID range (min > max), the same
// "nothing yet" the timeline uses, so it can never be mistaken for a real frame range and clamp a
// frame to it.
namespace {
const util::Range kNoLoopRange(1, 0);
} // namespace

namespace {

// The value a channel reads from the document when the map holds no key at all.
//
// This mirrors getDefaultKeyData<tKey, tType>() in core/TimeKeyBlender.cpp: the
// timeline's own default key wins, and only when the document has none does the
// concrete Data default stand in. The read itself goes through the Channel, so
// the component mapping lives in exactly one file and the fallback cannot drift
// away from the accessor playback uses - in particular ScaleKey::Data() is
// (1,1), not (0,0), and a naive zero here would draw a curve playback never
// plays.
template<class tKey, core::TimeKeyType tType>
float defaultKeyValue(const core::TimeLine& aTimeLine, const Channel& aChannel) {
    if (const core::TimeKey* key = aTimeLine.defaultKey(tType)) {
        XC_ASSERT(key->type() == tType);
        return aChannel.value(*key);
    }

    // A default-constructed key of this type. Only the types routed here are
    // constructed, and their Data holds nothing but scalars, vectors and a
    // QList, so building one has no side effect; ImageKey is deliberately
    // excluded by the caller, whose Data allocates GL buffers.
    const tKey fallback;
    XC_ASSERT(fallback.type() == tType);
    return aChannel.value(fallback);
}

// dispatch on the channel's own type; the concrete key type is only needed to
// construct the no-default-key fallback.
float defaultValue(const core::TimeLine& aTimeLine, const Channel& aChannel) {
    switch (aChannel.type()) {
    case core::TimeKeyType_Move:
        return defaultKeyValue<core::MoveKey, core::TimeKeyType_Move>(aTimeLine, aChannel);
    case core::TimeKeyType_Rotate:
        return defaultKeyValue<core::RotateKey, core::TimeKeyType_Rotate>(aTimeLine, aChannel);
    case core::TimeKeyType_Scale:
        return defaultKeyValue<core::ScaleKey, core::TimeKeyType_Scale>(aTimeLine, aChannel);
    case core::TimeKeyType_Depth:
        return defaultKeyValue<core::DepthKey, core::TimeKeyType_Depth>(aTimeLine, aChannel);
    case core::TimeKeyType_Opa:
        return defaultKeyValue<core::OpaKey, core::TimeKeyType_Opa>(aTimeLine, aChannel);
    case core::TimeKeyType_Blur:
        return defaultKeyValue<core::BlurKey, core::TimeKeyType_Blur>(aTimeLine, aChannel);
    case core::TimeKeyType_HSV:
        return defaultKeyValue<core::HSVKey, core::TimeKeyType_HSV>(aTimeLine, aChannel);
    case core::TimeKeyType_Image: {
        if (const core::TimeKey* key = aTimeLine.defaultKey(core::TimeKeyType_Image)) {
            XC_ASSERT(key->type() == core::TimeKeyType_Image);
            return aChannel.value(*key);
        }
        // blendImageKey() is the one blender path with no getDefaultKeyData():
        // with neither a key nor a default key playback offsets by zero. Reading
        // the zero directly (instead of constructing an ImageKey as the other
        // types do) keeps sample() callable outside a GL context: ImageKey::Data
        // owns a GridMesh, and GridMesh's constructor allocates GL buffers
        // through gl::Global::functions(). The value is the same zero.
        return 0.0f;
    }
    default:
        // Bone, pose, mesh and FFD carry no components, so no Channel can name
        // them; a hand-made Channel samples zero rather than reading a key whose
        // layout it does not match.
        return 0.0f;
    }
}

} // namespace

//-------------------------------------------------------------------------------------------------
Curve::Curve(): mTimeLine(nullptr), mChannel(), mLoop(false), mLoopRange(kNoLoopRange) {}

Curve::Curve(const core::TimeLine& aTimeLine, const Channel& aChannel):
    mTimeLine(&aTimeLine),
    mChannel(aChannel),
    mLoop(false),
    mLoopRange(kNoLoopRange) {}

void Curve::setLoop(bool aLoop, const util::Range& aRange) {
    mLoop = aLoop;
    mLoopRange = aRange;
}

bool Curve::isLooped() const { return mLoop && 0 <= mLoopRange.diff(); }

//-------------------------------------------------------------------------------------------------
QVector<const core::TimeKey*> Curve::keys() const {
    QVector<const core::TimeKey*> result;
    if (!mTimeLine) {
        return result;
    }

    // The map is frame-indexed, so iteration order IS frame order. Only the
    // top-level keys are read, exactly as TimeKeyGatherer does: a nested key
    // (a mesh key under an image key) is not an interpolated value of this
    // channel and playback never blends through it.
    const core::TimeLine::MapType& map = mTimeLine->map(mChannel.type());
    result.reserve(map.size());
    for (auto itr = map.constBegin(); itr != map.constEnd(); ++itr) {
        result.append(itr.value());
    }
    return result;
}

//-------------------------------------------------------------------------------------------------
const core::TimeKey* Curve::findLower(core::Frame aFrame) const {
    if (!mTimeLine) {
        return nullptr;
    }

    // Same query as TimeKeyGatherer::findLastKey(): the last key at or before
    // the frame. Null means the frame sits before the first key.
    const core::TimeLine::MapType& map = mTimeLine->map(mChannel.type());
    auto itr = map.upperBound(aFrame.get());
    if (itr == map.begin()) {
        return nullptr;
    }
    --itr;
    return itr.value();
}

//-------------------------------------------------------------------------------------------------
float Curve::sample(core::Frame aFrame) const {
    if (!mTimeLine) {
        return 0.0f;
    }

    const core::TimeLine::MapType& map = mTimeLine->map(mChannel.type());

    // No key: the layer plays its default value.
    if (map.isEmpty()) {
        return defaultValue(*mTimeLine, mChannel);
    }

    // OUTSIDE the frame range the frame is CLAMPED into it: the animation is defined over the range,
    // so past either end the curve is FLAT at the range's edge value (the playback range clamps its
    // own frames, so the gatherer is never asked about them - this is the graph's own statement).
    // A curve that was never given a range (see kNoLoopRange) has nothing to clamp to.
    const float raw = aFrame.getDecimal();
    const bool haveRange = 0 <= mLoopRange.diff();
    const float at = haveRange
        ? std::min(static_cast<float>(mLoopRange.max()), std::max(static_cast<float>(mLoopRange.min()), raw))
        : raw;
    const float firstFrame = static_cast<float>(map.first()->frame());
    const float lastFrame = static_cast<float>(map.last()->frame());
    const bool wraps = isLooped() && map.size() > 1;
    // The project's period, and the wrapped pair's effective length: the first key sits one span
    // ahead of the last.
    const float span = static_cast<float>(mLoopRange.diff() + 1);
    const float wrapD = static_cast<float>(map.first()->frame() - map.last()->frame()) + span;

    // OUTSIDE the keys, which is the project's own playback rule (see setLoop). The test is on the
    // DECIMAL frame and not on Frame::get(): that truncates toward zero, so a frame just below the
    // first key reports 0 and would not read as "before the first key" at all.
    if (at < firstFrame) {
        // The last key sits one span ahead of the first, so this is the tail of the eased segment
        // from the last key back to the first.
        if (wraps)
            return sampleOver(*map.last(), *map.first(), at + span, wrapD);
        return mChannel.value(*map.first());
    }
    if (lastFrame < at) {
        // The same wrapped segment, continued toward the range's end (the clamp above keeps a frame
        // past the range at the range's edge, so this only reaches the wrap's own tail).
        if (wraps)
            return sampleOver(*map.last(), *map.first(), at, wrapD);
        return mChannel.value(*map.last());
    }

    const core::TimeKey* from = findLower(aFrame);

    // A key exactly at the frame. The gatherer's hasSameFrame() also requires an
    // integral frame: a fractional frame sitting on a key is a point inside that
    // key's segment and must be interpolated with the next key.
    if (from && !aFrame.hasFraction() && from->frame() == aFrame.get()) {
        return mChannel.value(*from);
    }

    // value(), then dereference: dereferencing the iterator yields the MAPPED
    // POINTER (a TimeKey*), which is not a TimeKey& - *upper alone does not
    // compile against sampleBetween's reference parameters.
    const auto upper = map.upperBound(aFrame.get());
    if (!from || upper == map.end()) {
        // Unreachable while the range tests above hold (a frame inside [first,last] always has both
        // neighbours); a hand-built map that says otherwise holds its lower key.
        return mChannel.value(from ? *from : *map.first());
    }
    return sampleBetween(*from, *upper.value(), aFrame);
}

//-------------------------------------------------------------------------------------------------
float Curve::sampleBetween(const core::TimeKey& aFrom, const core::TimeKey& aTo, core::Frame aFrame) const {
    return sampleOver(
        aFrom, aTo, aFrame.getDecimal(), static_cast<float>(aTo.frame() - aFrom.frame())
    );
}

//-------------------------------------------------------------------------------------------------
void Curve::neighborsOf(const core::TimeKey& aFrom, const core::TimeKey& aTo,
                        const core::TimeKey*& aPrev, const core::TimeKey*& aNext) const {
    aPrev = nullptr;
    aNext = nullptr;

    const core::TimeLine::MapType& map = mTimeLine->map(mChannel.type());
    auto fromItr = map.constFind(aFrom.frame());
    if (fromItr != map.constBegin() && fromItr != map.constEnd()) {
        auto prev = fromItr;
        --prev;
        aPrev = prev.value();
    }
    auto toItr = map.constFind(aTo.frame());
    if (toItr != map.constEnd()) {
        ++toItr;
        if (toItr != map.constEnd()) {
            aNext = toItr.value();
        }
    }
}

//-------------------------------------------------------------------------------------------------
float Curve::sampleOver(const core::TimeKey& aFrom, const core::TimeKey& aTo, float aAt, float aD) const {
    const core::TimeKeyType type = mChannel.type();
    const float v0 = mChannel.value(aFrom);
    const float v1 = mChannel.value(aTo);

    // A step-held channel never blends, and this is tested before anything else
    // because the image key does carry an easing param that playback ignores:
    // sampling it here would draw an interpolation the document does not have.
    if (!mChannel.isInterpolated()) {
        return v0;
    }

    const float d = aD;
    if (d == 0.0f) {
        // TimeLine::createPusher() replaces the key at an occupied frame, so a
        // document edited through the timeline cannot hold two keys on one
        // frame; a hand-built TimeLine could, and Easing::calculate() would
        // divide by it. Hold the `from` value rather than crash or return NaN.
        return v0;
    }

    // The blender evaluates the easing of the `from` key at the point's frame
    // offset inside the segment (t = frame - k0->frame), never at the segment's
    // endpoints and never with the `to` key's param.
    const float t = aAt - static_cast<float>(aFrom.frame());
    const int component = mChannel.component();
    const util::Easing::Param* easing = easingOf(aFrom, type, component);
    const util::Easing::Param* toEasing = easingOf(aTo, type, component);
    float rate = 0.0f;
    if (!easing) {
        // No graphable channel reaches this: types without an easing param have
        // no components and so no Channel. A hand-made Channel gets the neutral
        // (linear) rate instead of an invented curve.
        rate = t / d;
    } else if (easing->type == util::Easing::Type_Custom) {
        // A Custom curve may DERIVE either side from the keys around the
        // segment. This is the same derivation playback runs (core::
        // autoEasingContext + HandleMode), so the drawn curve is the played one.
        // The per-side decision is Curve::derivesHandle's (a Custom FROM param
        // plus a derived mode on that side), and controlPoint applies exactly
        // that predicate to each side - the model's ghost publication asks
        // derivesHandle, so a published kind and the drawn/evaluated pair cannot
        // disagree. A pair with both sides stored skips the neighbour walk
        // entirely: the stored controls are the answer then, and they are the
        // common case.
        const util::Easing::HandleMode inMode =
            toEasing ? toEasing->handleIn : util::Easing::HandleMode_Aligned;
        if (util::Easing::isDerived(easing->handleOut) || util::Easing::isDerived(inMode)) {
            const core::TimeKey* prev = nullptr;
            const core::TimeKey* next = nullptr;
            neighborsOf(aFrom, aTo, prev, next);
            util::Easing::AutoContext ctx = core::autoEasingContext(aFrom, aTo, component, prev, next);
            ctx.inMode = inMode;
            rate = util::Easing::calculate(*easing, ctx, t, 0.0f, 1.0f, d);
        } else {
            rate = util::Easing::calculate(*easing, t, 0.0f, 1.0f, d);
        }
    } else {
        rate = util::Easing::calculate(*easing, t, 0.0f, 1.0f, d);
    }

    if (type == core::TimeKeyType_Move) {
        // Move is the one channel playback does not lerp: getMoveExpans() feeds
        // the two key positions and their Catmull-Rom velocities into a
        // Ferguson-Coons spline and walks it by arc length. The graph has to run
        // the same spline, otherwise a curved move path would be drawn as the
        // straight line it is not.
        const core::MoveKey& k0 = static_cast<const core::MoveKey&>(aFrom);
        const core::MoveKey& k1 = static_cast<const core::MoveKey&>(aTo);

        // The gatherer's point(-1) and point(2): the neighbour keys that shape
        // the velocities. Either is missing at the ends of the key list, and
        // getCatmullRomVels() handles nulls.
        const core::TimeKey* prev = nullptr;
        const core::TimeKey* next = nullptr;
        neighborsOf(aFrom, aTo, prev, next);
        const auto knot = static_cast<const core::MoveKey*>(prev);
        const auto knot2 = static_cast<const core::MoveKey*>(next);

        const auto vels = core::MoveKey::getCatmullRomVels(knot, &k0, &k1, knot2);
        util::FergusonCoonsSpline<QVector3D> spline;
        spline.set(k0.pos().toVector3D(), k1.pos().toVector3D(), vels[0].toVector3D(), vels[1].toVector3D());
        const QVector2D pos = spline.getByLinear(rate).toVector2D();
        return (mChannel.component() == 1) ? pos.y() : pos.x();
    }

    if (type == core::TimeKeyType_HSV) {
        // blendHSVKey() pushes the blended float through setHue()/setSaturation()
        // /setValue(), which take ints, so the played value is the TRUNCATED
        // one. Layer opacity and depth keep their float.
        return static_cast<float>(static_cast<int>(v0 * (1.0f - rate) + v1 * rate));
    }

    return v0 * (1.0f - rate) + v1 * rate;
}

//-------------------------------------------------------------------------------------------------
int Curve::segmentCount() const {
    const int count = keys().size();
    return (count > 0) ? (count - 1) : 0;
}

Curve::Segment Curve::segment(int aIndex) const {
    Segment result = {};

    const QVector<const core::TimeKey*> all = keys();
    if (aIndex < 0 || aIndex + 1 >= all.size()) {
        // Out of range: a null segment (from == nullptr). The renderer and the
        // hit-test both walk 0..segmentCount()-1, so this only shields a caller
        // that guesses an index.
        return result;
    }

    result.from = all[aIndex];
    result.to = all[aIndex + 1];
    result.fromValue = mChannel.value(*result.from);
    result.toValue = mChannel.value(*result.to);

    // A COPY, never a reference: the curve does not own the document, and a
    // segment handed to the renderer may outlive the edit that follows the
    // paint (an undo removing the key would dangle a reference).
    if (const util::Easing::Param* easing = easingOf(*result.from, mChannel.type(), mChannel.component())) {
        result.easing = *easing;
    }
    // The arriving handle belongs to the TO key, so its MODE is read there while
    // its stored value stays in result.easing (the FROM param's x2/y2).
    if (const util::Easing::Param* toEasing = easingOf(*result.to, mChannel.type(), mChannel.component())) {
        result.inMode = toEasing->handleIn;
    }
    return result;
}

//-------------------------------------------------------------------------------------------------
bool Curve::isCustom(const Segment& aSegment) const {
    // "Fully authored": a Custom curve whose BOTH sides are stored (Free or
    // Aligned). A side that derives its control point has no stored position to
    // draw, so the panel's stored-pair fast path must use effectiveHandles()
    // instead. Analytic easings expose no handles at all, exactly as before.
    return aSegment.easing.type == util::Easing::Type_Custom
        && !derivesHandle(aSegment, 0) && !derivesHandle(aSegment, 1);
}

bool Curve::derivesHandle(const Segment& aSegment, const int aHandleIndex) {
    // Handle 0 is the FROM key's OUTGOING side, stored in aSegment.easing; handle
    // 1 is the TO key's INCOMING side, whose mode arrives as aSegment.inMode even
    // though its stored x2/y2 lives in the FROM param.
    if (aSegment.easing.type != util::Easing::Type_Custom)
        return false;
    const util::Easing::HandleMode mode =
        (aHandleIndex == 1) ? aSegment.inMode : aSegment.easing.handleOut;
    return util::Easing::isDerived(mode);
}

bool Curve::effectiveHandles(const Segment& aSegment, QPointF aOut[2]) const {
    if (!mTimeLine || !aSegment.from || !aSegment.to) {
        return false;
    }
    if (aSegment.easing.type != util::Easing::Type_Custom) {
        return false;
    }

    // THE PER-SIDE DECISION COMES FROM derivesHandle, the same query the
    // publication marks a dot with. A stored side is its document value; only a
    // derived side consults controlPoint (which is also what playback's
    // Easing::calculate does for that side), so the pair cannot claim a
    // derivation the curve does not perform.
    const bool outDerived = derivesHandle(aSegment, 0);
    const bool inDerived = derivesHandle(aSegment, 1);

    // Only a derived side needs the neighbours; a stored pair is returned as-is
    // and costs no walk.
    util::Easing::AutoContext ctx;
    if (outDerived || inDerived) {
        const core::TimeKey* prev = nullptr;
        const core::TimeKey* next = nullptr;
        neighborsOf(*aSegment.from, *aSegment.to, prev, next);
        ctx = core::autoEasingContext(
            *aSegment.from, *aSegment.to, mChannel.component(), prev, next);
        ctx.inMode = aSegment.inMode;
    }
    const float span = static_cast<float>(aSegment.to->frame() - aSegment.from->frame());
    aOut[0] = outDerived
        ? util::Easing::controlPoint(aSegment.easing, aSegment.inMode, ctx, span, 0)
        : handle(aSegment, 0);
    aOut[1] = inDerived
        ? util::Easing::controlPoint(aSegment.easing, aSegment.inMode, ctx, span, 1)
        : handle(aSegment, 1);
    return true;
}

QVector<QPointF> Curve::ghostHandles(const core::TimeKey& aFrom) const {
    QVector<QPointF> result;
    if (!mTimeLine) {
        return result;
    }

    // The segment this easing shapes: from aFrom to its successor in the
    // channel's map. The LAST key of a channel has no next key at all - the
    // wrapped segment a looping playback draws is not a segment the panel hands
    // handles to.
    const core::TimeLine::MapType& map = mTimeLine->map(mChannel.type());
    auto toItr = map.constFind(aFrom.frame());
    if (toItr == map.constEnd()) {
        return result;
    }
    ++toItr;
    if (toItr == map.constEnd()) {
        return result;
    }

    // Find the index of the from key in the key list so the segment can be built
    // with the same code every other reader uses (including the TO key's
    // incoming mode).
    const QVector<const core::TimeKey*> all = keys();
    int index = -1;
    for (int i = 0; i + 1 < all.size(); ++i) {
        if (all[i] == &aFrom) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        return result;
    }
    const Segment seg = segment(index);
    // Derived pair only: both sides stored means there is nothing a "ghost"
    // caller needs (the stored pair is Curve::handle()).
    if (seg.easing.type != util::Easing::Type_Custom
        || (!util::Easing::isDerived(seg.easing.handleOut) && !util::Easing::isDerived(seg.inMode))) {
        return result;
    }

    QPointF pair[2];
    if (!effectiveHandles(seg, pair)) {
        return result;
    }
    result.append(pair[0]);
    result.append(pair[1]);
    return result;
}

QPointF Curve::handle(const Segment& aSegment, int aHandleIndex) {
    const util::Easing::CubicBezier& bezier = aSegment.easing.cubicBezier;
    if (aHandleIndex == 1) {
        return QPointF(bezier.x2, bezier.y2);
    }
    return QPointF(bezier.x1, bezier.y1);
}

void Curve::setHandle(util::Easing::Param& aEasing, int aHandleIndex, const QPointF& aNormalized) {
    // x is the handle's position in time inside the segment, and a cubic bezier
    // only has a solvable parameterization while x is in [0,1]; a control point
    // beyond that makes the curve non-monotonic in its own domain, so x is
    // clamped.
    const float x = qBound(0.0f, static_cast<float>(aNormalized.x()), 1.0f);
    // y is the value, and overshoot is the point of Back/Elastic and of a
    // deliberate anticipation. Clamping it would erase the curve the user is
    // shaping, so it stays free.
    const float y = static_cast<float>(aNormalized.y());

    if (aHandleIndex == 1) {
        aEasing.cubicBezier.x2 = x;
        aEasing.cubicBezier.y2 = y;
    } else {
        aEasing.cubicBezier.x1 = x;
        aEasing.cubicBezier.y1 = y;
    }
    // aEasing.type is deliberately untouched: dragging a tangent handle edits the
    // shape of the Custom curve, and only the picker writes the type.
}

//-------------------------------------------------------------------------------------------------
const util::Easing::Param* Curve::easingOf(const core::TimeKey& aKey, core::TimeKeyType aType, int aComponent) {
    // The application's one key -> param bridge (core/KeyEasingAccess.h). A
    // graphable type answers the component's own param; Pose/FFD their single
    // shared one at index 0 (the chip and the header's quick easings edit them);
    // Image its stored (step-held) pair; Bone/Mesh null.
    return core::keyEasingOf(aKey, aType, aComponent);
}

util::Easing::Param* Curve::easingOf(core::TimeKey& aKey, core::TimeKeyType aType, int aComponent) {
    return core::keyEasingOf(aKey, aType, aComponent);
}

//-------------------------------------------------------------------------------------------------
void Curve::buildPolyline(
    core::Frame aFrom, core::Frame aTo, int aSamplesPerSegment, QVector<QPointF>& aOut
) const {
    aOut.clear();

    const QVector<const core::TimeKey*> all = keys();
    if (all.isEmpty()) {
        // Nothing to draw: the value does not vary over frames.
        return;
    }

    const float fromFrame = aFrom.getDecimal();
    const float toFrame = aTo.getDecimal();
    if (toFrame < fromFrame) {
        return;
    }

    // At least one interior sample or a curve would be drawn as the chord
    // between its keys, which is flatly wrong for the analytic easings.
    const int steps = qMax(1, aSamplesPerSegment);

    // Outside the keys the polyline reaches the window's own EDGES - stopping at the first and last
    // key would leave the rest of the plot empty - and it carries the value playback reports there:
    // the hold (no loop), the wrapped easing inside the frame range (loop on), or the flat edge value
    // past the range. sample() is that rule, so the extension asks it.
    const core::TimeKey* firstKey = all.first();
    const core::TimeKey* lastKey = all.last();
    const bool wraps = isLooped() && all.size() > 1;
    auto appendExtension = [&](float aLo, float aHi) {
        for (int s = 0; s <= steps; ++s) {
            const float frame = aLo + (aHi - aLo) * (static_cast<float>(s) / static_cast<float>(steps));
            aOut.append(QPointF(frame, sample(core::Frame::fromDecimal(frame))));
        }
    };
    if (fromFrame < static_cast<float>(firstKey->frame())) {
        if (wraps) {
            appendExtension(fromFrame, static_cast<float>(firstKey->frame()));
        } else {
            // The hold is a SEGMENT of the plot, not a point: it runs from the window's left edge
            // to the first key - or to the window's right edge when the keys sit past the whole
            // window. Publishing a single point here would make the curve vanish (the panel draws
            // nothing for a polyline shorter than two points) even though playback holds a value
            // across this entire window.
            const float firstFrame = static_cast<float>(firstKey->frame());
            const float holdEnd = std::min(toFrame, firstFrame);
            const float value = mChannel.value(*firstKey);
            aOut.append(QPointF(fromFrame, value));
            if (holdEnd < firstFrame)
                aOut.append(QPointF(holdEnd, value));
            // (When the window reaches the first key, the first touched segment already starts
            // there, so the hold's far end is not appended twice.)
        }
    }

    for (int i = 0; i + 1 < all.size(); ++i) {
        const core::TimeKey* from = all[i];
        const core::TimeKey* to = all[i + 1];
        const float f0 = static_cast<float>(from->frame());
        const float f1 = static_cast<float>(to->frame());

        // Only the segments the window touches.
        if (f1 < fromFrame || toFrame < f0) {
            continue;
        }

        const float v0 = mChannel.value(*from);
        const float v1 = mChannel.value(*to);

        if (!mChannel.isInterpolated()) {
            // The step shape sample() plays: hold the `from` value across the
            // whole span, then jump to the `to` value at the `to` frame (where
            // sample() reports v1, because the key is exactly on that frame).
            aOut.append(QPointF(f0, v0));
            aOut.append(QPointF(f1, v0));
            aOut.append(QPointF(f1, v1));
            continue;
        }

        // The samples go over the part of the segment the WINDOW SHOWS, not over the whole segment:
        // the budget a caller hands in is spent on what is on screen (a segment spanning hundreds of
        // frames would otherwise be drawn as a handful of straight chords in a view of a few frames
        // of it). The segment's own endpoints are emitted as well, so the caller's clip to the plot
        // rect cuts a chord that reaches the edge.
        const float visLo = std::max(f0, fromFrame);
        const float visHi = std::min(f1, toFrame);
        aOut.append(QPointF(f0, v0));
        if (visHi > visLo) {
            for (int s = 0; s <= steps; ++s) {
                const float frame = visLo + (visHi - visLo) * (static_cast<float>(s) / static_cast<float>(steps));
                if (frame <= f0 || f1 <= frame)
                    continue; // the segment's own ends are already emitted
                // Through sample(), so a drawn point is the played value - a
                // fractional frame included, and the HSV truncation with it.
                aOut.append(QPointF(frame, sample(core::Frame::fromDecimal(frame))));
            }
        }
        aOut.append(QPointF(f1, v1));
    }

    // AFTER the last key, appended here so the polyline stays in frame order: the rest of the wrap, or
    // the held last value.
    if (static_cast<float>(lastKey->frame()) < toFrame) {
        if (wraps) {
            appendExtension(static_cast<float>(lastKey->frame()), toFrame);
        } else {
            // The mirror of the hold above: from the last key to the window's right edge - and
            // when the keys are behind the whole window, the hold spans it from the left edge,
            // because then no segment was drawn at all and one point would draw nothing.
            const float lastFrame = static_cast<float>(lastKey->frame());
            const float value = mChannel.value(*lastKey);
            if (lastFrame < fromFrame)
                aOut.append(QPointF(fromFrame, value));
            aOut.append(QPointF(toFrame, value));
        }
    }

    // FLOOR: a polyline shorter than two points draws NOTHING (gui::drawPanel's size() < 2), and the
    // only way to get here with one point is a hold whose window edge lands exactly on the one key
    // that would have supplied the other end (a single-key channel at the plot's edge). The hold is
    // still a real value across the window, so publish it between the window's own edges; the
    // publication stays unclipped because these ARE the window's edges, not a cut of anything.
    if (aOut.size() < 2 && fromFrame < toFrame) {
        const float start = sample(core::Frame::fromDecimal(fromFrame));
        const float end = sample(core::Frame::fromDecimal(toFrame));
        aOut.clear();
        aOut.append(QPointF(fromFrame, start));
        aOut.append(QPointF(toFrame, end));
    }
}

} // namespace graph
} // namespace ctrl
