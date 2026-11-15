// The influence build's polygon containment must be bit-identical to Qt's.
//
// BoneShape precomputes the edges of its influence polygon and buckets them by
// y (core::BonePolygonIndex) instead of calling QPolygonF::containsPoint for
// every vertex. That is only allowed because the result is the same boolean:
// Qt's odd-even scan adds +-1 per crossing edge and keeps the parity, and the
// index evaluates the same crossings with the same arithmetic. This gate
// compares the two on grids, vertices, edge midpoints and random points so a
// change to the bucket layout cannot silently move a weight.
//
// Headless: QPolygonF only, no GL and no project.
#include <QPointF>
#include <QPolygonF>
#include <cmath>
#include <cstdio>
#include <limits>

#include "core/BonePolygonIndex.h"

namespace {

int failures = 0;
int comparisons = 0;

// Deterministic LCG: the same polygons and points on every run.
quint32 gState = 0x12345678u;

double nextRandom() {
    gState = gState * 1664525u + 1013904223u;
    return (gState >> 8) / double(1u << 24);
}

double nextCoordinate(double aExtent) { return (nextRandom() * 2.0 - 1.0) * aExtent; }

void compare(const QPolygonF& aPolygon, const QPointF& aPoint, const char* aCase) {
    const bool expected = aPolygon.containsPoint(aPoint, Qt::OddEvenFill);
    core::BonePolygonIndex index;
    // build every comparison: a stale index must not be able to pass
    index.build(aPolygon);
    const bool actual = index.contains(aPoint);
    ++comparisons;
    if (expected != actual) {
        if (failures < 10) {
            std::printf(
                "FAIL  %s point=(%.17g, %.17g) qt=%d index=%d\n",
                aCase,
                aPoint.x(),
                aPoint.y(),
                int(expected),
                int(actual)
            );
        }
        ++failures;
    }
}

void comparePolygon(const QPolygonF& aPolygon, const char* aCase, int aRandomPoints) {
    // relevance: build once for the grid/random sweep (compare() rebuilds for
    // the edge cases below, which is slow but a stronger test)
    core::BonePolygonIndex index;
    index.build(aPolygon);

    QRectF bounds = aPolygon.boundingRect();
    const double step = qMax(bounds.width(), bounds.height()) / 8.0;
    if (!(step > 0.0))
        return;

    // A grid that includes the edges and vertices exactly (the scan's
    // boundary rule is the part that is easy to get wrong).
    for (double x = bounds.left() - step; x <= bounds.right() + step; x += step) {
        for (double y = bounds.top() - step; y <= bounds.bottom() + step; y += step) {
            const QPointF point(x, y);
            const bool expected = aPolygon.containsPoint(point, Qt::OddEvenFill);
            const bool actual = index.contains(point);
            ++comparisons;
            if (expected != actual) {
                if (failures < 10) {
                    std::printf(
                        "FAIL  %s grid point=(%.17g, %.17g) qt=%d index=%d\n", aCase, x, y, int(expected), int(actual)
                    );
                }
                ++failures;
            }
        }
    }

    // Vertices and edge midpoints: exact boundary cases.
    for (int i = 0; i < aPolygon.size(); ++i) {
        compare(aPolygon, aPolygon.at(i), aCase);
        const QPointF mid = (aPolygon.at(i) + aPolygon.at((i + 1) % aPolygon.size())) / 2.0;
        compare(aPolygon, mid, aCase);
    }

    for (int i = 0; i < aRandomPoints; ++i) {
        const QPointF point(
            bounds.left() - step + nextRandom() * (bounds.width() + 2.0 * step),
            bounds.top() - step + nextRandom() * (bounds.height() + 2.0 * step)
        );
        compare(aPolygon, point, aCase);
    }
}

} // namespace

int main() {
    {
        QPolygonF empty;
        core::BonePolygonIndex index;
        index.build(empty);
        if (index.contains(QPointF(0.0, 0.0)))
            ++failures;
        ++comparisons;

        QPolygonF line;
        line << QPointF(0.0, 0.0) << QPointF(1.0, 1.0);
        index.build(line);
        if (index.contains(QPointF(0.5, 0.5)))
            ++failures;
        ++comparisons;
    }

    // A square with horizontal edges and an implicit close.
    {
        QPolygonF square;
        square << QPointF(0.0, 0.0) << QPointF(10.0, 0.0) << QPointF(10.0, 10.0) << QPointF(0.0, 10.0);
        comparePolygon(square, "square", 300);

        // A non-finite coordinate must not become an out-of-range float-to-int
        // conversion in the bucket lookup; Qt answers false for both.
        const double nan = std::numeric_limits<double>::quiet_NaN();
        compare(square, QPointF(nan, 5.0), "nan x");
        compare(square, QPointF(5.0, nan), "nan y");
        compare(square, QPointF(nan, nan), "nan xy");

        // The same square explicitly closed: the final duplicate must not add a
        // second closing edge.
        QPolygonF closed = square;
        closed << QPointF(0.0, 0.0);
        comparePolygon(closed, "closed square", 300);
    }

    // A triangle and a polygon with a repeated consecutive point.
    {
        QPolygonF triangle;
        triangle << QPointF(-3.0, -2.0) << QPointF(7.0, 1.0) << QPointF(2.0, 9.0);
        comparePolygon(triangle, "triangle", 300);

        QPolygonF repeated;
        repeated << QPointF(0.0, 0.0) << QPointF(4.0, 0.0) << QPointF(4.0, 0.0) << QPointF(4.0, 4.0)
                 << QPointF(0.0, 4.0);
        comparePolygon(repeated, "repeated point", 300);
    }

    // A pathological polygon (475 points): the bucket table must still find
    // every crossing edge.
    {
        QPolygonF circle;
        const int count = 475;
        for (int i = 0; i < count; ++i) {
            const double angle = 2.0 * M_PI * double(i) / double(count);
            circle << QPointF(40.0 * std::cos(angle), 25.0 * std::sin(angle));
        }
        comparePolygon(circle, "475-point circle", 1000);
    }

    // Random polygons, including collinear and horizontal runs produced by
    // rounding some coordinates to integers.
    for (int p = 0; p < 300; ++p) {
        const int count = 3 + int(nextRandom() * 38.0);
        QPolygonF polygon;
        for (int i = 0; i < count; ++i) {
            double x = nextCoordinate(20.0);
            double y = nextCoordinate(20.0);
            if (nextRandom() < 0.25)
                x = std::floor(x);
            if (nextRandom() < 0.25)
                y = std::floor(y);
            polygon << QPointF(x, y);
        }
        comparePolygon(polygon, "random polygon", 100);
    }

    if (failures == 0) {
        std::printf("PASS: polygon index matches QPolygonF odd-even (%d comparisons)\n", comparisons);
        return 0;
    }
    std::printf("FAILED %d of %d comparisons\n", failures, comparisons);
    return 1;
}
