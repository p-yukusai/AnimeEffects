#ifndef UTIL_TRIANGLE2DPOS_H
#define UTIL_TRIANGLE2DPOS_H

#include <array>
#include "util/Triangle2D.h"

namespace util {

class Triangle2DPos {
public:
    static Triangle2DPos make(const Triangle2D& aTriangle, const QVector2D& aPoint);

    Triangle2DPos();
    Triangle2DPos(int aIndex, float aFirst, float aSecond);

    bool isValid() const { return mIndex >= 0; }
    QVector2D get(const Triangle2D& aTriangle) const;
    QVector2D get(const std::array<QVector2D, 3>& aTriangle) const;

    /// The three vertex weights this position encodes: result[i] is the weight
    /// of the triangle's vertex i (the corner scheme is mIndex,
    /// (mIndex + 1) % 3, (mIndex + 2) % 3). They sum to one, which is what lets
    /// an attribute be interpolated by the same barycentric coordinates as the
    /// position.
    std::array<float, 3> weights() const;

private:
    int mIndex;
    float mFirst;
    float mSecond;
};

} // namespace util

#endif // UTIL_TRIANGLE2DPOS_H
