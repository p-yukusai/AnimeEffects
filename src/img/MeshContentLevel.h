#ifndef IMG_MESHCONTENTLEVEL_H
#define IMG_MESHCONTENTLEVEL_H

#include <QSize>

namespace img {

// The alpha level a layer's mesh content test uses: a pixel counts as content when its alpha is
// above this level.
//
// A fixed level is wrong in both directions: too high cuts the skirt of deliberate soft paint, too
// low lets erase residue become content and bend the mesh silhouette. The level is therefore derived
// from the layer's own pixels:
//
//     T = 10 if no pixel is above alpha 2;
//     otherwise T = clamp(round(0.05 * mean(alpha : alpha > 2)), 1, 10)
//
// The rule must stay a pure function of the bitmap: no file, layer name or path may enter it.
class MeshContentLevel {
public:
    // pass as aContentAlphaThreshold to mean "derive it from this image"
    enum { kDerive = -1 };

    // The level for one RGBA8 image, or GridMeshCreator::kContentAlphaThreshold when there is
    // nothing above alpha 2 to measure.
    static int derive(const void* aRgba8, const QSize& aSize);

    // The mapping, exposed for tests. A non-positive mean returns the strict level, not the
    // clamp's lower bound.
    static int fromMean(double aMeanAbove2);
};

} // namespace img

#endif // IMG_MESHCONTENTLEVEL_H
