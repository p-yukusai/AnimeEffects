#include <QPainter>
#include "core/BoneKey.h"
#include "core/LayerMesh.h"
#include "core/TimeKeyBlender.h"
#include "ctrl/Painter.h"
#include "ctrl/bone/bone_WeightHeatmap.h"

namespace ctrl {
namespace bone {

void drawWeightHeatmap(
    QPainter& aPainter, const core::RenderInfo& aInfo, const core::BoneKey& aKey,
    const core::Bone2& aBone, const QMatrix4x4& aTargetMtx
) {
    // A GL SURFACE, not a QPainter image. The field is one indexed triangle draw per
    // skinned mesh through ctrl::GLCorePaintEngine: the GPU interpolates the selected bone's
    // weight across every triangle and the fragment shader evaluates the ramp. There is no
    // screen-sized intermediate, nothing is rasterised on the CPU, and the field is drawn at
    // the framebuffer's own resolution (including its multisampling).
    //
    // A raster QPainter (a test, a screenshot without the GL host) has no scene surface to
    // wash, so it draws nothing - which is honest: the wash is an overlay of the viewport,
    // and only the viewport's engine can draw it.
    auto* engine = dynamic_cast<GLCorePaintEngine*>(aPainter.paintEngine());
    if (!engine) {
        return;
    }

    const int boneIndex = aKey.data().boneIndex(aBone);
    if (boneIndex < 0) {
        return;
    }

    // Reused across caches: the arrays are uploaded by the engine during the draw, so the
    // vector only has to hold one mesh's weights at a time.
    QVector<float> weights;

    for (core::BoneKey::Cache* cache : aKey.caches()) {
        if (!cache || !cache->node()) {
            continue;
        }
        core::ObjectNode& node = *cache->node();
        core::LayerMesh* mesh = core::TimeKeyBlender::getAreaMesh(node, aInfo.time);        if (!mesh || mesh->primitiveMode() != GL_TRIANGLES) {
            continue;
        }

        const int vertices = mesh->vertexCount();
        const core::BoneInfluenceMap& map = cache->influence();
        // The map is built from this mesh; a count that disagrees means the two are not the
        // same geometry, and an index would read past the weight array.
        if (vertices <= 0 || map.vertexCount() != vertices || boneIndex >= map.boneCount()) {
            continue;
        }

        const core::BoneInfluenceMap::Accessor accessor = map.accessor();
        if (accessor.slotCount() <= 0) {
            continue;
        }

        // One weight per vertex. This is a READ of the map the build already produced, not a
        // second evaluation: Accessor::weight answers for both storage forms (the one-slot
        // implicit 1 included). A cache this bone does not touch is skipped before the draw.
        weights.resize(vertices);
        bool any = false;
        for (int i = 0; i < vertices; ++i) {
            const float weight = accessor.weight(boneIndex, i);
            weights[i] = weight;
            any = any || weight >= GLCorePaintEngine::kWeightFieldMinWeight;
        }
        if (!any) {
            continue;
        }

        // The mesh's own element buffer, the one the layer draw uses. Its count must agree
        // with the mesh's: a stale or never-filled buffer draws nothing rather than garbage.
        const int indexCount = mesh->indexCount();
        gl::BufferObject& indices = mesh->getIndexBuffer();
        if (!mesh->indices() || indexCount < 3 || indices.dataCount() != indexCount) {
            continue;
        }

        // EXACTLY the build's matrix, from the one function that defines it, then the
        // camera: target * inner * (origin offset), the same product the influence map was
        // built with and the same one the old surface rasteriser mapped its vertices with.
        const QMatrix4x4 worldMtx =
            aTargetMtx * core::TimeKeyBlender::getInfluenceMapMatrix(cache->innerMatrix(), node, aInfo.time);

        GLCorePaintEngine::WeightField field;
        field.positions = mesh->positions();
        field.weights = weights.constData();
        field.vertexCount = vertices;
        field.indices = &indices;
        field.indexCount = indexCount;
        field.modelView = aInfo.camera.viewMatrix() * worldMtx;
        engine->drawWeightField(field);
    }
}

} // namespace bone
} // namespace ctrl
