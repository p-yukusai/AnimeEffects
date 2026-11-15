#ifndef CTRL_AUTOSCROLL_H
#define CTRL_AUTOSCROLL_H

#include <QPoint>
#include <QRect>
#include <QtGlobal>
#include <algorithm>

namespace ctrl {
namespace autoscroll {

// THE ONE EDGE-AUTOSCROLL RULE, SHARED BY THE TIMELINE AND THE GRAPH PANEL.
//
// Both surfaces drag a marquee, and both must scroll when the pointer is held near
// an edge. The geometry (how close counts as "near", how fast it moves, how often)
// is not a property of either view, so it lives here once: a view supplies its own
// pointer and viewport and applies the returned delta in its own units.
//
// SIGN CONVENTION, which is what keeps the two callers honest:
//   * x > 0 reveals content to the RIGHT (later frames),
//   * y > 0 reveals content BELOW (later rows, or lower graph values).
// The timeline maps x to its free horizontal view and y to the object tree's
// vertical scrollbar; the graph maps x to its frame window and y to its value
// window. Neither view re-derives a margin or a speed.
constexpr int kEdgeMargin = 24; // px from an edge where scrolling begins
constexpr int kMaxStep = 16;    // px per tick at the edge itself
constexpr int kMinStep = 1;     // px per tick at the inner edge of the margin
constexpr int kIntervalMs = 16; // ~60 Hz, the same cadence for both surfaces
// How long the pointer must HOLD in an edge band before it starts scrolling. A
// marquee can begin on the edge (a press near the frame range's end is ordinary),
// and scrolling the moment it moves there made every such drag jump the view. The
// delay is also what keeps a drag that merely crosses the band on its way somewhere
// else from scrolling at all.
constexpr int kDwellMs = 150;

struct Step {
    int x = 0;
    int y = 0;

    bool isNull() const { return x == 0 && y == 0; }
};

// Speed for a distance inside the margin: 0 at the edge, kMinStep at the margin's
// inner boundary. A pointer outside the viewport has a negative distance and gets
// the maximum, so dragging past the edge keeps scrolling instead of sticking.
inline int speedForDistance(int aDistance) {
    const int distance = std::clamp(aDistance, 0, kEdgeMargin);
    return kMaxStep - (kMaxStep - kMinStep) * distance / kEdgeMargin;
}

// The scroll step for aPointer inside aViewport. The two axes are independent, so
// a corner produces a diagonal step and an edge in the middle of one axis produces
// only the other. A degenerate viewport has no edges to be near and returns zero.
inline Step edgeStep(const QPoint& aPointer, const QRect& aViewport) {
    Step step;
    if (aViewport.width() <= 0 || aViewport.height() <= 0)
        return step;

    const int left = aViewport.left();
    const int right = aViewport.right();
    const int top = aViewport.top();
    const int bottom = aViewport.bottom();

    if (aPointer.x() < left + kEdgeMargin)
        step.x = -speedForDistance(aPointer.x() - left);
    else if (aPointer.x() > right - kEdgeMargin)
        step.x = speedForDistance(right - aPointer.x());

    if (aPointer.y() < top + kEdgeMargin)
        step.y = -speedForDistance(aPointer.y() - top);
    else if (aPointer.y() > bottom - kEdgeMargin)
        step.y = speedForDistance(bottom - aPointer.y());

    return step;
}

// The dwell latch around edgeStep, with DIRECTION. A view calls observe() on every
// pointer MOVE (to record which way the pointer went) and update() once per timer
// tick. An axis arms only when the pointer moves TOWARD that edge (or is already
// outside it), so a drag that starts on the top edge and runs HORIZONTALLY along a
// row does not scroll the view vertically: there was no intent to go up. Once an
// axis is armed, holding still keeps scrolling; moving back inward or leaving the
// band disarms it. The first tick in a band only starts the kDwellMs clock.
//
// The previous pointer is kept HERE rather than in each host, so the timeline and
// the graph cannot answer "did the pointer move toward the edge?" differently.
struct EdgeDwell {
    bool insideX = false;
    bool insideY = false;
    qint64 sinceX = 0;
    qint64 sinceY = 0;
    QPoint previous;
    bool hasPrevious = false;

    void reset() {
        insideX = insideY = false;
        sinceX = sinceY = 0;
        previous = QPoint();
        hasPrevious = false;
    }

    bool inEdge() const { return insideX || insideY; }

    // The pointer moved. `aNowMs` starts the dwell for an axis that arms here.
    void observe(const QPoint& aPointer, const QRect& aViewport, qint64 aNowMs) {
        const Step raw = edgeStep(aPointer, aViewport);
        const bool outsideX = aPointer.x() < aViewport.left() || aPointer.x() > aViewport.right();
        const bool outsideY = aPointer.y() < aViewport.top() || aPointer.y() > aViewport.bottom();

        const auto arm = [this, aNowMs](
                             int aStep, bool aOutside, int aNow, int aBefore, bool& aInside, qint64& aSince
                         ) {
            if (aStep == 0) {
                aInside = false;
                return;
            }
            // Outside the view means the pointer is already pushing past this edge.
            if (aOutside) {
                if (!aInside) {
                    aInside = true;
                    aSince = aNowMs;
                }
                return;
            }
            if (!hasPrevious)
                return;
            const bool toward = (aStep > 0) ? (aNow > aBefore) : (aNow < aBefore);
            const bool away = (aStep > 0) ? (aNow < aBefore) : (aNow > aBefore);
            if (away) {
                aInside = false; // moving back inward stops this axis
            } else if (toward && !aInside) {
                aInside = true;
                aSince = aNowMs;
            }
        };

        arm(raw.x, outsideX, aPointer.x(), previous.x(), insideX, sinceX);
        arm(raw.y, outsideY, aPointer.y(), previous.y(), insideY, sinceY);
        previous = aPointer;
        hasPrevious = true;
    }

    // A timer tick. Returns only the axes that are armed AND have held for the dwell.
    Step update(const QPoint& aPointer, const QRect& aViewport, qint64 aNowMs) {
        const Step raw = edgeStep(aPointer, aViewport);
        if (raw.x == 0)
            insideX = false;
        if (raw.y == 0)
            insideY = false;

        Step step;
        if (insideX && aNowMs - sinceX >= kDwellMs)
            step.x = raw.x;
        if (insideY && aNowMs - sinceY >= kDwellMs)
            step.y = raw.y;
        return step;
    }
};
} // namespace autoscroll
} // namespace ctrl

#endif // CTRL_AUTOSCROLL_H
