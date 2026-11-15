// The edge-autoscroll rule both views share (ctrl/AutoScroll.h).
//
// The timeline and the graph panel must scroll at the same distance, the same
// speed and in the same direction while a marquee is dragged, so the rule is one
// pure function. This gate pins its sign convention and its acceleration; the
// two hosts only apply the returned delta.
#include <QPoint>
#include <QRect>
#include <cstdio>
#include "ctrl/AutoScroll.h"

#define CHECK(cond)                                                                                    \
    do {                                                                                               \
        if (!(cond)) {                                                                                 \
            std::printf("FAIL %d: %s\n", __LINE__, #cond);                                             \
            return 1;                                                                                  \
        }                                                                                              \
    } while (false)

int main() {
    using namespace ctrl::autoscroll;
    const QRect viewport(100, 50, 800, 600);

    // The middle of the view is not near an edge.
    CHECK(edgeStep(QPoint(500, 350), viewport).isNull());

    // The sign convention: positive reveals RIGHT and BELOW, negative LEFT and ABOVE.
    const Step right = edgeStep(QPoint(viewport.right() - 1, 350), viewport);
    CHECK(right.x > 0);
    CHECK(right.y == 0);
    const Step left = edgeStep(QPoint(viewport.left() + 1, 350), viewport);
    CHECK(left.x < 0);
    const Step bottom = edgeStep(QPoint(500, viewport.bottom() - 1), viewport);
    CHECK(bottom.y > 0);
    const Step top = edgeStep(QPoint(500, viewport.top() + 1), viewport);
    CHECK(top.y < 0);

    // A corner scrolls both axes, and the edge itself is the fastest step.
    const Step corner = edgeStep(QPoint(viewport.right(), viewport.bottom()), viewport);
    CHECK(corner.x == kMaxStep);
    CHECK(corner.y == kMaxStep);

    // Acceleration is monotone: moving deeper into the margin speeds up, and the
    // margin's inner boundary is the slowest step, not zero (zero would make the
    // first pixel of the band dead).
    int previous = 0;
    for (int distance = kEdgeMargin; distance >= 0; --distance) {
        const int speed = speedForDistance(distance);
        CHECK(speed >= kMinStep);
        CHECK(speed <= kMaxStep);
        if (distance < kEdgeMargin)
            CHECK(speed >= previous);
        previous = speed;
    }
    CHECK(speedForDistance(kEdgeMargin) == kMinStep);
    CHECK(speedForDistance(0) == kMaxStep);

    // Outside the viewport keeps the maximum step, so dragging past the edge does
    // not stick (a marquee can leave the widget).
    const Step outside = edgeStep(QPoint(viewport.right() + 100, viewport.bottom() + 100), viewport);
    CHECK(outside.x == kMaxStep);
    CHECK(outside.y == kMaxStep);

    // A degenerate viewport has no band to be near.
    CHECK(edgeStep(QPoint(0, 0), QRect()).isNull());

    // THE DWELL IS DIRECTION-AWARE. A drag along the top edge must not scroll up;
    // a drag moving toward an edge arms it and waits kDwellMs; moving back inward
    // disarms it; leaving and re-entering waits again.
    EdgeDwell horizontal;
    const QPoint topEdge(viewport.center().x(), viewport.top() + 2);
    horizontal.observe(topEdge, viewport, 1000); // press baseline
    horizontal.observe(QPoint(topEdge.x() + 40, topEdge.y()), viewport, 1016);
    CHECK(!horizontal.inEdge());
    CHECK(horizontal.update(QPoint(topEdge.x() + 40, topEdge.y()), viewport, 2000).isNull());

    EdgeDwell approach;
    const QPoint rightNear(viewport.right() - 6, viewport.center().y());
    const QPoint rightEdge(viewport.right() - 1, viewport.center().y());
    approach.observe(rightNear, viewport, 1000); // baseline, in the band but not armed
    CHECK(!approach.inEdge());
    approach.observe(rightEdge, viewport, 1016); // moved toward the right edge
    CHECK(approach.inEdge());
    CHECK(approach.update(rightEdge, viewport, 1016 + kDwellMs - 1).isNull());
    CHECK(!approach.update(rightEdge, viewport, 1016 + kDwellMs).isNull());

    // Moving back inward stops the axis even while still inside the band.
    approach.observe(QPoint(rightEdge.x() - 4, rightEdge.y()), viewport, 1200);
    CHECK(!approach.inEdge());
    CHECK(approach.update(QPoint(rightEdge.x() - 4, rightEdge.y()), viewport, 2000).isNull());

    // Re-entering waits the dwell again; outside the view counts as pushing past it.
    approach.observe(rightEdge, viewport, 2200);
    CHECK(approach.inEdge());
    CHECK(approach.update(rightEdge, viewport, 2200 + kDwellMs - 1).isNull());
    CHECK(!approach.update(rightEdge, viewport, 2200 + kDwellMs).isNull());

    EdgeDwell outsideGate;
    const QPoint pastRight(viewport.right() + 8, viewport.center().y());
    outsideGate.observe(pastRight, viewport, 3000);
    CHECK(outsideGate.inEdge());
    CHECK(outsideGate.update(pastRight, viewport, 3000 + kDwellMs - 1).isNull());
    CHECK(outsideGate.update(pastRight, viewport, 3000 + kDwellMs).x == kMaxStep);


    std::printf(
        "autoscroll: shared edge rule: margin=%d max=%d interval=%dms dwell=%dms, signs "
        "(+right/+down), monotone acceleration, direction-aware dwell/re-entry\n",
        kEdgeMargin, kMaxStep, kIntervalMs, kDwellMs
    );
    return 0;
}
