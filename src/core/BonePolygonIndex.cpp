#include <QtGlobal>
#include <utility>
#include "core/BonePolygonIndex.h"

namespace core {

int BonePolygonIndex::bucketOf(qreal aY) const {
    if (mBucketCount <= 1)
        return 0;
    const qreal span = mYMax - mYMin;
    if (!(span > 0.0))
        return 0;
    return qBound(0, int((aY - mYMin) / span * mBucketCount), mBucketCount - 1);
}

bool BonePolygonIndex::contains(const QPointF& aPoint) const {
    if (mBucketCount <= 0)
        return false;

    const qreal y = aPoint.y();
    // Outside every edge's y interval Qt's scan adds nothing. The negated
    // form also rejects a non-finite y, which Qt's comparisons would.
    if (!(y >= mYMin && y < mYMax))
        return false;

    const int bucket = bucketOf(y);
    const qreal x = aPoint.x();
    int winding = 0;
    for (int k = mBuckets[bucket]; k < mBuckets[bucket + 1]; ++k) {
        const Edge& edge = mEdges[mBucketEdges[k]];
        // The same test and arithmetic, in the same order, as
        // qt_polygon_isect_line() in Qt's qpolygon.cpp.
        if (y >= edge.y1 && y < edge.y2) {
            const qreal crossX = edge.x1 + ((edge.x2 - edge.x1) / (edge.y2 - edge.y1)) * (y - edge.y1);
            if (crossX <= x)
                winding += edge.dir;
        }
    }
    return (winding % 2) != 0;
}

void BonePolygonIndex::build(const QPolygonF& aPolygon) {
    mEdges.clear();
    mBuckets.clear();
    mBucketEdges.clear();
    mBucketCount = 0;
    mYMin = 0.0;
    mYMax = 0.0;

    const int count = aPolygon.size();
    if (count < 3)
        return;

    const auto addEdge = [this](const QPointF& aP1, const QPointF& aP2) {
        qreal x1 = aP1.x();
        qreal y1 = aP1.y();
        qreal x2 = aP2.x();
        qreal y2 = aP2.y();
        int dir = 1;
        // ignore horizontal lines, as Qt's scan conversion does
        if (qFuzzyCompare(y1, y2))
            return;
        if (y2 < y1) {
            std::swap(x1, x2);
            std::swap(y1, y2);
            dir = -1;
        }
        mEdges.push_back(Edge{x1, y1, x2, y2, dir});
    };

    // The same edge walk (including the implicit close) as QPolygonF.
    const QPointF first = aPolygon.at(0);
    QPointF last = first;
    for (int i = 1; i < count; ++i) {
        const QPointF& point = aPolygon.at(i);
        addEdge(last, point);
        last = point;
    }
    if (last != first)
        addEdge(last, first);

    if (mEdges.isEmpty())
        return;

    qreal yMin = mEdges[0].y1;
    qreal yMax = mEdges[0].y2;
    for (const Edge& edge : mEdges) {
        yMin = qMin(yMin, edge.y1);
        yMax = qMax(yMax, edge.y2);
    }
    mYMin = yMin;
    mYMax = yMax;

    // One bucket per edge (capped): a scanline normally crosses only a few
    // edges, so this is the useful range for both small and pathological
    // polygons.
    mBucketCount = qBound(1, mEdges.size(), 64);
    mBuckets.fill(0, mBucketCount + 1);
    for (const Edge& edge : mEdges) {
        const int begin = bucketOf(edge.y1);
        const int end = bucketOf(edge.y2);
        for (int b = begin; b <= end; ++b) {
            ++mBuckets[b + 1];
        }
    }
    for (int b = 0; b < mBucketCount; ++b) {
        mBuckets[b + 1] += mBuckets[b];
    }

    mBucketEdges.resize(mBuckets[mBucketCount]);
    QVector<int> cursor = mBuckets;
    for (int i = 0; i < mEdges.size(); ++i) {
        const Edge& edge = mEdges[i];
        const int begin = bucketOf(edge.y1);
        const int end = bucketOf(edge.y2);
        for (int b = begin; b <= end; ++b) {
            mBucketEdges[cursor[b]++] = i;
        }
    }
}

} // namespace core
