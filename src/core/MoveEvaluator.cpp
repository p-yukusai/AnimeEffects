#include "core/MoveEvaluator.h"

#include <QVector3D>

#include "core/ChannelTrack.h"
#include "core/MoveKey.h"
#include "core/MoveStateKey.h"
#include "util/FergusonCoonsSpline.h"

namespace core {

namespace {

bool sameFrames(const ChannelTrack& aLhs, const ChannelTrack& aRhs) {
    if (aLhs.isEmpty() || aRhs.isEmpty() || aLhs.keyCount() != aRhs.keyCount()) {
        return false;
    }

    auto lhs = aLhs.keys().constBegin();
    auto rhs = aRhs.keys().constBegin();
    while (lhs != aLhs.keys().constEnd() && rhs != aRhs.keys().constEnd()) {
        if (lhs.key() != rhs.key()) {
            return false;
        }
        ++lhs;
        ++rhs;
    }
    return lhs == aLhs.keys().constEnd() && rhs == aRhs.keys().constEnd();
}

bool stateIsLinear(const PropertyTrack& aProperty, int aFrame) {
    const auto* state = static_cast<const MoveStateKey*>(aProperty.state().sampleState(aFrame));
    return !state || state->spline() == MoveKey::SplineType_Linear;
}

} // namespace

bool MoveEvaluator::hasPairedChannels(const PropertyTrack& aProperty) {
    if (aProperty.type() != TimeKeyType_Move) {
        return false;
    }
    const ChannelTrack* x = aProperty.channel(0);
    const ChannelTrack* y = aProperty.channel(1);
    return x && y && sameFrames(*x, *y);
}

bool MoveEvaluator::splineIsLinearAt(const PropertyTrack& aProperty, int aFrame) {
    return stateIsLinear(aProperty, aFrame);
}

float MoveEvaluator::sampleChannel(
    const PropertyTrack& aProperty, int aComponent, const TimeInfo& aTime
) {
    const ChannelTrack* track = aProperty.channel(aComponent);
    if (!track) {
        return 0.0f;
    }

    // Only the position pair participates in the spatial path. Centroid and any
    // other channel are ordinary scalar tracks.
    if (aComponent < 0 || 1 < aComponent || !hasPairedChannels(aProperty)) {
        return track->sample(aTime);
    }

    const ChannelTrack* x = aProperty.channel(0);
    const ChannelTrack* y = aProperty.channel(1);
    const ChannelTrack::SegmentSample sx = x->sampleSegment(aTime);
    const ChannelTrack::SegmentSample sy = y->sampleSegment(aTime);

    // Exact keys and the hold cases are the channel's own answer; there is no
    // segment to shape.
    if (!sx.hasSegment() || !sy.hasSegment() || sx.exact || sy.exact) {
        return track->sample(aTime);
    }

    const QVector2D p1(sx.from->value(), sy.from->value());
    const QVector2D p2(sx.to->value(), sy.to->value());

    QVector2D p0;
    QVector2D p3;
    const QVector2D* prev = nullptr;
    const QVector2D* next = nullptr;
    if (sx.prev) {
        const ChannelKey* yPrev = y->keyAt(sx.prevFrame);
        if (yPrev) {
            p0 = QVector2D(sx.prev->value(), yPrev->value());
            prev = &p0;
        }
    }
    if (sx.next) {
        const ChannelKey* yNext = y->keyAt(sx.nextFrame);
        if (yNext) {
            p3 = QVector2D(sx.next->value(), yNext->value());
            next = &p3;
        }
    }

    const std::array<QVector2D, 2> velocities = MoveKey::getCatmullRomVels(
        p1, p2, prev, next, splineIsLinearAt(aProperty, sx.fromFrame),
        splineIsLinearAt(aProperty, sx.toFrame)
    );

    util::FergusonCoonsSpline<QVector3D> spline;
    spline.set(
        p1.toVector3D(), p2.toVector3D(), velocities[0].toVector3D(), velocities[1].toVector3D()
    );

    // The x axis follows X's eased rate and the y axis Y's, exactly as the old
    // getMoveExpans did with its two per-component rates.
    const QVector3D xAt = spline.getByLinear(sx.rate);
    const QVector3D yAt = spline.getByLinear(sy.rate);
    return aComponent == 0 ? xAt.x() : yAt.y();
}

QVector2D MoveEvaluator::samplePosition(const PropertyTrack& aProperty, const TimeInfo& aTime) {
    return QVector2D(
        sampleChannel(aProperty, 0, aTime), sampleChannel(aProperty, 1, aTime)
    );
}

QVector2D MoveEvaluator::sampleCentroid(const PropertyTrack& aProperty, const TimeInfo& aTime) {
    return QVector2D(
        sampleChannel(aProperty, 2, aTime), sampleChannel(aProperty, 3, aTime)
    );
}

} // namespace core
