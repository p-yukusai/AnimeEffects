#ifndef CTRL_BONE_WEIGHTHEATMAP_H
#define CTRL_BONE_WEIGHTHEATMAP_H

#include <QMatrix4x4>
#include "core/BoneKey.h"
#include "core/RenderInfo.h"

class QPainter;

namespace ctrl {
namespace bone {

    /// The normalised weight of one bone over every skinned mesh of a key, as
    /// a colour-ramped SURFACE wash. Every mesh triangle is DRAWN ON THE GPU with
    /// one weight per vertex: the rasteriser interpolates the weight across the
    /// triangle and the fragment shader evaluates the ramp, so a vertex is a
    /// control value and the field between vertices is the surface itself. There
    /// is no screen-sized image, no CPU rasterisation and no image upload.
    ///
    /// The screen positions come from the influence map's own product: the
    /// cache's inner matrix times the node's origin offset, then the key owner's
    /// target matrix and the camera - so the wash sits exactly on the mesh the
    /// bone editor displays. It is a diagnostic over the artwork, so it is drawn
    /// before the bones and the skeleton stays legible above it.
    void drawWeightHeatmap(
        QPainter& aPainter, const core::RenderInfo& aInfo, const core::BoneKey& aKey,
        const core::Bone2& aBone, const QMatrix4x4& aTargetMtx
    );

} // namespace bone
} // namespace ctrl

#endif // CTRL_BONE_WEIGHTHEATMAP_H
