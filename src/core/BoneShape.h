#ifndef CORE_BONESHAPE_H
#define CORE_BONESHAPE_H

#include <array>
#include <QPolygonF>
#include "util/Segment2D.h"
#include "core/Serializer.h"
#include "core/Deserializer.h"
#include "core/BonePolygonIndex.h"

namespace core {

class BoneShape {
public:
    class BendRange {
    public:
        BendRange();
        float getWeight(float aBendAngle) const;
        bool isValid() const;
        float angle[2]; // plus range and minus range
    };

    BoneShape();
    void setSegment(const util::Segment2D& aSegment);
    void setRadius(const QVector2D& aRoot, const QVector2D& aTail);
    void setPolygon(const QPolygonF& aPolygon);
    void setBendRange(const BendRange& aRoot, const BendRange& aTail);
    void setRootBendFromDirections(const QVector2D& aMyDir, const QVector2D& aParentDir);
    void adjustTailBendFromDirections(const QVector2D& aMyDir, const QVector2D& aChildDir);

    float influence(const QVector2D& aPos) const;

    // Exact authored-geometry comparison, ignoring derived values (validity,
    // unit vector, length, bounding box and the polygon index). Two shapes that
    // compare equal produce the same influence for every point.
    bool hasSameAuthoredGeometry(const BoneShape& aRhs) const;

    // serialize
    bool serialize(Serializer& aOut) const;
    QJsonObject serializeToJson() const;
    void deserializeFromJson(QJsonObject json);
    bool deserialize(Deserializer& aIn);

private:
    void updateValidity();
    float getBoneWeight(const QVector2D& aPoint) const;
    float getBoneEllipseWeight(
        const QVector2D& aCenter, const QVector2D& aVUnit, const QVector2D& aRadius, const QVector2D& aPoint
    ) const;
    float getWeakness(float aRate) const;

    bool mIsValid;
    util::Segment2D mSegment;
    QVector2D mVUnit;
    float mVDirAngle;
    float mLength;
    std::array<QVector2D, 2> mRadius;
    QRectF mBounding;
    QPolygonF mPolygon;
    // Derived from mPolygon; never serialized, rebuilt by the setters and the
    // two deserialisers.
    BonePolygonIndex mPolygonIndex;
    BendRange mRootBendRange;
    BendRange mTailBendRange;
};

} // namespace core

#endif // CORE_BONESHAPE_H
