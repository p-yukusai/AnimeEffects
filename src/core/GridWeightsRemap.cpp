#include "core/GridWeightsRemap.h"

#include <algorithm>
#include <cfloat>

#include "XC.h"
#include "core/GridMesh.h"
#include "core/ImageKey.h"
#include "core/ObjectNode.h"
#include "core/TimeInfo.h"
#include "core/TimeKeyBlender.h"
#include "core/TimeLine.h"

namespace core {

namespace {

// Whether this ImageKey supplies aNode's area mesh at one of aOwner's bone
// frames. A record is keyed to one mesh, so changing an image key that no bone
// frame reads must not rewrite it: another ImageKey (or a newer MeshKey) may
// own that frame's mesh, and the same topological signature is not enough to
// tell them apart.
bool imageFeedsABoneFrame(ObjectNode& aNode, ObjectNode& aOwner, ImageKey& aKey) {
    if (!aOwner.timeLine()) {
        return false;
    }

    // Bone keys are always real frame keys (BoneEditor::initializeKey creates
    // one at the current frame, never a base value); a base value has no
    // frame at which an area mesh could be chosen, so it cannot name one.
    const TimeLine::MapType& bones = aOwner.timeLine()->map(TimeKeyType_Bone);
    for (auto itr = bones.begin(); itr != bones.end(); ++itr) {
        TimeInfo time;
        time.frame.set(itr.key());
        if (TimeKeyBlender::getAreaMeshImpl(aNode, time).first == &aKey) {
            return true;
        }
    }
    return false;
}

// A weight field is a scalar attribute of the old vertices; the new vertex
// gets it from the old triangle the mesh transition put it in, with the same
// barycentric coordinates the transition uses for a position.
QVector<SkinWeights::Entry> resampleBone(
    const SkinWeights::BakedBone& aOld, int aOldVertexCount,
    const QVector<GridMesh::Transition>& aTransitions, int aNewVertexCount,
    const QVector<QVector<quint32>>& aNeighbours
) {
    const int count = aNewVertexCount;
    if (count <= 0) {
        return QVector<SkinWeights::Entry>();
    }

    QVector<float> values(count, 0.0f);
    QVector<uint8> filled(count, 0);
    QVector<quint32> queue;
    queue.reserve(count);

    // Seeded vertices: the new vertex lies inside an old triangle.
    for (int i = 0; i < count && i < aTransitions.size(); ++i) {
        const GridMesh::Transition& transition = aTransitions[i];
        if (!transition.pos.isValid()) {
            continue;
        }
        if (transition.id[0] >= quint32(aOldVertexCount)
            || transition.id[1] >= quint32(aOldVertexCount)
            || transition.id[2] >= quint32(aOldVertexCount)) {
            continue;
        }
        const std::array<float, 3> weights = transition.pos.weights();
        float value = weights[0] * aOld.weightAt(transition.id[0])
            + weights[1] * aOld.weightAt(transition.id[1])
            + weights[2] * aOld.weightAt(transition.id[2]);
        values[i] = std::min(1.0f, std::max(0.0f, value));
        filled[i] = 1;
        queue.push_back(quint32(i));
    }

    // A new vertex outside the old coverage has no transition. Fill it from
    // the new mesh's own neighbours, one wave from the seeded vertices. The
    // average keeps the field bounded and continuous along the new boundary;
    // a connected island with no seed at all stays zero.
    int head = 0;
    while (head < queue.size()) {
        const quint32 vertex = queue[head++];
        for (quint32 neighbour : aNeighbours[int(vertex)]) {
            if (filled[int(neighbour)]) {
                continue;
            }
            float sum = 0.0f;
            int contributors = 0;
            for (quint32 other : aNeighbours[int(neighbour)]) {
                if (filled[int(other)]) {
                    sum += values[int(other)];
                    ++contributors;
                }
            }
            if (contributors > 0) {
                values[int(neighbour)] = sum / float(contributors);
                filled[int(neighbour)] = 1;
                queue.push_back(neighbour);
            }
        }
    }

    QVector<SkinWeights::Entry> entries;
    entries.reserve(count);
    for (int i = 0; i < count; ++i) {
        if (values[i] >= FLT_EPSILON) {
            entries.push_back(SkinWeights::Entry{quint32(i), values[i]});
        }
    }
    return entries;
}

QVector<QVector<quint32>> buildNeighbours(const GridMesh& aMesh) {
    QVector<QVector<quint32>> neighbours(aMesh.vertexCount());
    const GLuint* indices = aMesh.indices();
    const int indexCount = aMesh.indexCount();
    for (int i = 0; i + 2 < indexCount; i += 3) {
        const quint32 tri[3] = {indices[i], indices[i + 1], indices[i + 2]};
        for (int a = 0; a < 3; ++a) {
            for (int b = a + 1; b < 3; ++b) {
                if (tri[a] >= quint32(neighbours.size()) || tri[b] >= quint32(neighbours.size())) {
                    continue;
                }
                neighbours[int(tri[a])].push_back(tri[b]);
                neighbours[int(tri[b])].push_back(tri[a]);
            }
        }
    }
    return neighbours;
}

} // namespace

QVector<GridWeightsRemap> remapGridWeights(
    ObjectNode& aNode, ImageKey& aKey, const GridMesh& aOldMesh, const QPoint& aOldOrigin,
    const GridMesh& aNewMesh, const QPoint& aNewOrigin
) {
    QVector<GridWeightsRemap> result;
    if (aOldMesh.vertexCount() <= 0 || aNewMesh.vertexCount() <= 0) {
        return result;   // an empty new mesh has no mapping to write into
    }
    if (!aOldMesh.indices() || aOldMesh.indexCount() <= 0 || !aNewMesh.indices()
        || aNewMesh.indexCount() <= 0) {
        // A mesh without faces has no transition to interpolate across; leaving
        // the record alone keeps its warning instead of silently zeroing it.
        return result;
    }

    const quint64 oldSignature = SkinWeights::meshSignature(aOldMesh);
    const quint64 newSignature = SkinWeights::meshSignature(aNewMesh);

    // The old mesh's triangles, expressed for the new vertices. Build the
    // transitions only after a record proves the work is needed - the BSP over
    // a dense cutout is not free, and a layer with no authored field must pay
    // nothing for this feature.
    bool needed = false;
    for (ObjectNode* owner = &aNode; owner && !needed; owner = owner->parent()) {
        const SkinWeights::NodeWeights* record = owner->skinWeights().find(aNode);
        needed = record && !record->bakedBones.isEmpty() && record->meshSignature == oldSignature
            && imageFeedsABoneFrame(aNode, *owner, aKey);
    }
    if (!needed) {
        return result;
    }

    const GridMesh::Transitions transitions =
        GridMesh::TransitionCreater(aOldMesh, aOldOrigin)
            .create(aNewMesh.positions(), aNewMesh.vertexCount(), aNewOrigin);

    const QVector<QVector<quint32>> neighbours = buildNeighbours(aNewMesh);

    for (ObjectNode* owner = &aNode; owner; owner = owner->parent()) {
        const SkinWeights::NodeWeights* record = owner->skinWeights().find(aNode);
        if (!record || record->bakedBones.isEmpty() || record->meshSignature != oldSignature) {
            continue;
        }
        if (!imageFeedsABoneFrame(aNode, *owner, aKey)) {
            continue;
        }

        SkinWeights::NodeWeights after = *record;
        after.vertexCount = aNewMesh.vertexCount();
        after.meshSignature = newSignature;
        for (int i = 0; i < after.bakedBones.size(); ++i) {
            if (record->bakedBones[i].entries.isEmpty()) {
                after.bakedBones[i].entries.clear();   // baked-to-zero stays zero
                continue;
            }
            after.bakedBones[i].entries = resampleBone(
                record->bakedBones[i], aOldMesh.vertexCount(), transitions.data,
                aNewMesh.vertexCount(), neighbours
            );
        }

        if (!after.sameWeightsAs(*record)) {
            GridWeightsRemap remap;
            remap.owner = owner->pointee();
            remap.before = *record;
            remap.after = after;
            result.push_back(remap);
        }
    }

    return result;
}

} // namespace core
