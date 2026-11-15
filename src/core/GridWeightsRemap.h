#ifndef CORE_GRIDWEIGHTSREMAP_H
#define CORE_GRIDWEIGHTSREMAP_H

#include <QPoint>
#include <QVector>
#include "core/SkinWeights.h"
#include "util/LinkPointer.h"

namespace core {
class GridMesh;
class ImageStateKey;
using ImageKey = ImageStateKey;
class ObjectNode;

// The authored-weight records that a change of an ImageKey's grid affects,
// before and after the change.
//
// The field is stored per vertex index, so regenerating the grid
// (ImageKey::resetGridMesh, reached by a cell-size edit, an image replacement
// and an external reload) would silently point every value at another vertex.
// The mapping from the old vertices to the new ones is the mesh's own
// geometric one: every new vertex that lies inside an old triangle gets that
// triangle's barycentric interpolation, and vertices outside the old coverage
// are filled from their already-mapped neighbours in the new mesh. This is the
// same old-triangle-to-new-vertex mapping the FFD resource updater has always
// used (GridMesh::TransitionCreater); a weight is an attribute of a vertex just
// as an FFD offset is.
struct GridWeightsRemap {
    util::LinkPointer<ObjectNode> owner;
    SkinWeights::NodeWeights before;
    SkinWeights::NodeWeights after;
};

// Collect the records for aNode that this ImageKey supplied at a BoneKey
// frame, resampled from aOldMesh to aNewMesh. The two origins are the
// ImageKey's resource positions before and after (GridMesh positions are
// content-local, and TransitionCreater expresses one in the other's space).
//
// A record is touched only when its stored signature is the OLD mesh's
// signature and this image key owns the area mesh at one of the owner's bone
// frames. A mesh edit that bypassed the remap, or a MeshKey that supplies the
// area mesh, is therefore left alone and keeps its mesh-signature warning.
QVector<GridWeightsRemap> remapGridWeights(
    ObjectNode& aNode,
    ImageKey& aKey,
    const GridMesh& aOldMesh,
    const QPoint& aOldOrigin,
    const GridMesh& aNewMesh,
    const QPoint& aNewOrigin
);

} // namespace core

#endif // CORE_GRIDWEIGHTSREMAP_H
