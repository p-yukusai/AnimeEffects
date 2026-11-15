#include <cmath>
#include "img/MeshContentLevel.h"
#include "img/GridMeshCreator.h"

namespace img {

int MeshContentLevel::fromMean(double aMeanAbove2) {
    if (!(aMeanAbove2 > 0.0)) {
        return GridMeshCreator::kContentAlphaThreshold;
    }
    int t = (int)std::lround(0.05 * aMeanAbove2);
    if (t < 1) {
        t = 1;
    }
    if (t > 10) {
        t = 10;
    }
    return t;
}

int MeshContentLevel::derive(const void* aRgba8, const QSize& aSize) {
    if (!aRgba8 || aSize.isEmpty()) {
        return GridMeshCreator::kContentAlphaThreshold;
    }
    const uint8* data = (const uint8*)aRgba8;
    const long pixels = (long)aSize.width() * aSize.height();

    long long mass = 0;
    long n = 0;
    for (long i = 0; i < pixels; ++i) {
        const int a = data[i * 4 + 3];
        if (a > 2) {
            mass += a;
            ++n;
        }
    }
    if (n == 0) {
        return GridMeshCreator::kContentAlphaThreshold;
    }
    return fromMean((double)mass / n);
}

} // namespace img
