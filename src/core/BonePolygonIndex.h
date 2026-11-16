#ifndef CORE_BONEPOLYGONINDEX_H
#define CORE_BONEPOLYGONINDEX_H

#include <QPointF>
#include <QPolygonF>
#include <QVector>

namespace core {

// Exact acceleration for QPolygonF::containsPoint(aPoint, Qt::OddEvenFill).
//
// Qt's scan walks every edge, adds +-1 for the ones crossing the point's
// scanline, and keeps the parity. This class precomputes that edge list and
// buckets the edges by y, so a query visits only the edges that can cross the
// scanline. The arithmetic and the tests are the same ones Qt applies (including
// qFuzzyCompare for horizontal edges and the implicit close), so the boolean is
// identical; order cannot matter because only the parity of an integer sum is
// used.
class BonePolygonIndex {
public:
    // Rebuild from a polygon. It is safe to query before this is called (an
    // empty index contains nothing), and calling it again replaces the data.
    void build(const QPolygonF& aPolygon);
    bool contains(const QPointF& aPoint) const;

private:
    struct Edge {
        qreal x1;
        qreal y1;
        qreal x2;
        qreal y2;
        int dir;
    };

    int bucketOf(qreal aY) const;

    QVector<Edge> mEdges;
    QVector<int> mBuckets; // mBucketCount + 1
    QVector<int> mBucketEdges;
    qreal mYMin = 0.0;
    qreal mYMax = 0.0;
    int mBucketCount = 0;
};

} // namespace core

#endif // CORE_BONEPOLYGONINDEX_H
