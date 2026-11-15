#include "ctrl/graph/graph_Curve.h"

#include <QVector3D>

#include "XC.h"
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
Curve::Curve(): mTimeLine(nullptr), mChannel() {}

Curve::Curve(const core::TimeLine& aTimeLine, const Channel& aChannel):
    mTimeLine(&aTimeLine),
    mChannel(aChannel) {}

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

    const core::TimeKey* from = findLower(aFrame);

    // Before the first key: playback holds the first key ("perfect following",
    // the gatherer's isSingle()), not the default value.
    if (!from) {
        return mChannel.value(*map.first());
    }

    // A key exactly at the frame. The gatherer's hasSameFrame() also requires an
    // integral frame: a fractional frame sitting on a key is a point inside that
    // key's segment and must be interpolated with the next key.
    if (!aFrame.hasFraction() && from->frame() == aFrame.get()) {
        return mChannel.value(*from);
    }

    const auto upper = map.upperBound(aFrame.get());
    if (upper == map.end()) {
        // Past the last key: playback holds it to the end of the layer.
        return mChannel.value(*from);
    }
    // value(), then dereference: dereferencing the iterator yields the MAPPED
    // POINTER (a TimeKey*), which is not a TimeKey& - *upper alone does not
    // compile against sampleBetween's reference parameters.
    return sampleBetween(*from, *upper.value(), aFrame);
}

//-------------------------------------------------------------------------------------------------
float Curve::sampleBetween(const core::TimeKey& aFrom, const core::TimeKey& aTo, core::Frame aFrame) const {
    const core::TimeKeyType type = mChannel.type();
    const float v0 = mChannel.value(aFrom);
    const float v1 = mChannel.value(aTo);

    // A step-held channel never blends, and this is tested before anything else
    // because the image key does carry an easing param that playback ignores:
    // sampling it here would draw an interpolation the document does not have.
    if (!mChannel.isInterpolated()) {
        return v0;
    }

    const float d = static_cast<float>(aTo.frame() - aFrom.frame());
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
    const float t = aFrame.getDecimal() - static_cast<float>(aFrom.frame());
    const util::Easing::Param* easing = easingOf(aFrom, type);
    const float rate = easing ?
        util::Easing::calculate(*easing, t, 0.0f, 1.0f, d) :
        // No graphable channel reaches this: types without an easing param have
        // no components and so no Channel. A hand-made Channel gets the neutral
        // (linear) rate instead of an invented curve.
        (t / d);

    if (type == core::TimeKeyType_Move) {
        // Move is the one channel playback does not lerp: getMoveExpans() feeds
        // the two key positions and their Catmull-Rom velocities into a
        // Ferguson-Coons spline and walks it by arc length. The graph has to run
        // the same spline, otherwise a curved move path would be drawn as the
        // straight line it is not.
        const core::MoveKey& k0 = static_cast<const core::MoveKey&>(aFrom);
        const core::MoveKey& k1 = static_cast<const core::MoveKey&>(aTo);
        const core::TimeLine::MapType& map = mTimeLine->map(type);

        // The gatherer's point(-1) and point(2): the neighbour keys that shape
        // the velocities. Either is missing at the ends of the key list, and
        // getCatmullRomVels() handles nulls.
        const core::MoveKey* knot = nullptr;
        auto fromItr = map.constFind(k0.frame());
        if (fromItr != map.constBegin() && fromItr != map.constEnd()) {
            auto prev = fromItr;
            --prev;
            knot = static_cast<const core::MoveKey*>(prev.value());
            XC_ASSERT(knot->type() == core::TimeKeyType_Move);
        }
        const core::MoveKey* knot2 = nullptr;
        auto toItr = map.constFind(k1.frame());
        if (toItr != map.constEnd()) {
            ++toItr;
            if (toItr != map.constEnd()) {
                knot2 = static_cast<const core::MoveKey*>(toItr.value());
                XC_ASSERT(knot2->type() == core::TimeKeyType_Move);
            }
        }

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
    if (const util::Easing::Param* easing = easingOf(*result.from, mChannel.type())) {
        result.easing = *easing;
    }
    return result;
}

//-------------------------------------------------------------------------------------------------
bool Curve::isCustom(const Segment& aSegment) const {
    return aSegment.easing.type == util::Easing::Type_Custom;
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
const util::Easing::Param* Curve::easingOf(const core::TimeKey& aKey, core::TimeKeyType aType) {
    // Every graphable type carries exactly one param, on its single Data object.
    // Bone, pose, mesh and FFD have no Data easing at all, so the graph must
    // report "no easing" rather than manufacture one - playback applies no
    // easing there. The cast is the caller's pairing of a key with its own type,
    // the same contract Channel::value() carries.
    switch (aType) {
    case core::TimeKeyType_Move:
        return &static_cast<const core::MoveKey&>(aKey).data().easing();
    case core::TimeKeyType_Rotate:
        return &static_cast<const core::RotateKey&>(aKey).data().easing();
    case core::TimeKeyType_Scale:
        return &static_cast<const core::ScaleKey&>(aKey).data().easing();
    case core::TimeKeyType_Depth:
        return &static_cast<const core::DepthKey&>(aKey).data().easing();
    case core::TimeKeyType_Opa:
        return &static_cast<const core::OpaKey&>(aKey).data().easing();
    case core::TimeKeyType_Blur:
        return &static_cast<const core::BlurKey&>(aKey).data().easing();
    case core::TimeKeyType_HSV:
        return &static_cast<const core::HSVKey&>(aKey).data().easing();
    case core::TimeKeyType_Image:
        return &static_cast<const core::ImageKey&>(aKey).data().easing();
    default:
        return nullptr;
    }
}

util::Easing::Param* Curve::easingOf(core::TimeKey& aKey, core::TimeKeyType aType) {
    switch (aType) {
    case core::TimeKeyType_Move:
        return &static_cast<core::MoveKey&>(aKey).data().easing();
    case core::TimeKeyType_Rotate:
        return &static_cast<core::RotateKey&>(aKey).data().easing();
    case core::TimeKeyType_Scale:
        return &static_cast<core::ScaleKey&>(aKey).data().easing();
    case core::TimeKeyType_Depth:
        return &static_cast<core::DepthKey&>(aKey).data().easing();
    case core::TimeKeyType_Opa:
        return &static_cast<core::OpaKey&>(aKey).data().easing();
    case core::TimeKeyType_Blur:
        return &static_cast<core::BlurKey&>(aKey).data().easing();
    case core::TimeKeyType_HSV:
        return &static_cast<core::HSVKey&>(aKey).data().easing();
    case core::TimeKeyType_Image:
        return &static_cast<core::ImageKey&>(aKey).data().easing();
    default:
        return nullptr;
    }
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

        // The segment's own endpoints are always emitted when the window
        // touches the segment: a polyline clipped to the interior samples alone
        // would fall up to one step short of the plot edge. The caller clips to
        // the plot rect anyway.
        aOut.append(QPointF(f0, v0));
        for (int s = 1; s < steps; ++s) {
            const float frame = f0 + (f1 - f0) * (static_cast<float>(s) / static_cast<float>(steps));
            if (frame < fromFrame || toFrame < frame) {
                continue;
            }
            // Through sample(), so a drawn point is the played value - a
            // fractional frame included, and the HSV truncation with it.
            aOut.append(QPointF(frame, sample(core::Frame::fromDecimal(frame))));
        }
        aOut.append(QPointF(f1, v1));
    }
}

} // namespace graph
} // namespace ctrl
