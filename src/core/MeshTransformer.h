#ifndef CORE_MESHTRANSFORMER_H
#define CORE_MESHTRANSFORMER_H

#include <vector>
#include <QScopedPointer>
#include "util/NonCopyable.h"
#include "util/ArrayBlock.h"
#include "gl/Vector3.h"
#include "gl/BufferObject.h"
#include "core/TimeKeyExpans.h"
#include "core/LayerMesh.h"
namespace core {
class MeshTransformerResource;
}

namespace core {

class MeshTransformer: private util::NonCopyable {
public:
    MeshTransformer(const QString& aShaderPath);
    MeshTransformer(MeshTransformerResource& aResource);
    ~MeshTransformer();

    void callGL(
        const TimeKeyExpans& aExpans,
        LayerMesh::MeshBuffer& aMeshBuffer,
        const QVector2D& aOriginOffset,
        util::ArrayBlock<const gl::Vector3> aPositions,
        bool aNonPosed = false,
        bool aUseInfluence = true
    );

    gl::BufferObject& positions() { return *mOutPositions; }
    const gl::BufferObject& positions() const { return *mOutPositions; }

    gl::BufferObject& xArrows() { return *mOutXArrows; }
    const gl::BufferObject& xArrows() const { return *mOutXArrows; }

    gl::BufferObject& yArrows() { return *mOutYArrows; }
    const gl::BufferObject& yArrows() const { return *mOutYArrows; }

private:
    MeshTransformerResource& mResource;
    bool mResourceOwns;
    gl::BufferObject* mOutPositions;
    gl::BufferObject* mOutXArrows;
    gl::BufferObject* mOutYArrows;
    // The uniform-block palette. Allocated to the full declared block once (so
    // the block is always backed), then only the required prefix is uploaded.
    // Null until the first skinned draw.
    QScopedPointer<gl::BufferObject> mPaletteBuffer;
    // Reused padding buffer for a palette shorter than the map's bone count (a
    // cleared palette / a pose key older than the skeleton). Empty in the common
    // case, where the palette's own storage is uploaded.
    std::vector<PosePalette::DualQuaternion> mPaletteUpload;
};

} // namespace core

#endif // CORE_MESHTRANSFORMER_H
