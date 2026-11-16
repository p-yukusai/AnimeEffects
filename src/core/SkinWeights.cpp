#include <algorithm>
#include <cfloat>
#include "core/SkinWeights.h"
#include "core/BoneShape.h"
#include "core/LayerMesh.h"
#include "core/ObjectNode.h"
#include "gl/Vector3.h"

namespace core {

namespace {

bool weightIsStored(float aWeight) { return aWeight >= FLT_EPSILON; }

quint64 hashUInt(quint64 aHash, quint32 aValue) {
    aHash ^= aValue;
    aHash *= 1099511628211ULL;
    return aHash;
}

} // namespace

//-------------------------------------------------------------------------------------------------
const SkinWeights::BakedBone* SkinWeights::BakedBone::findById(
    const QVector<BakedBone>& aBones, int aBoneId
) {
    const auto itr = std::lower_bound(
        aBones.begin(), aBones.end(), aBoneId,
        [](const BakedBone& aBone, int aValue) { return aBone.boneId < aValue; }
    );
    if (itr == aBones.end() || itr->boneId != aBoneId) {
        return nullptr;
    }
    return &(*itr);
}

const SkinWeights::Entry* SkinWeights::BakedBone::find(quint32 aVertex) const {
    const auto itr = std::lower_bound(
        entries.begin(), entries.end(), aVertex,
        [](const Entry& aEntry, quint32 aValue) { return aEntry.vertex < aValue; }
    );
    if (itr == entries.end() || itr->vertex != aVertex) {
        return nullptr;
    }
    return &(*itr);
}

float SkinWeights::BakedBone::weightAt(quint32 aVertex) const {
    const Entry* entry = find(aVertex);
    return entry ? entry->weight : 0.0f;
}

bool SkinWeights::BakedBone::setWeight(quint32 aVertex, float aWeight) {
    const auto itr = std::lower_bound(
        entries.begin(), entries.end(), aVertex,
        [](const Entry& aEntry, quint32 aValue) { return aEntry.vertex < aValue; }
    );
    const bool present = itr != entries.end() && itr->vertex == aVertex;

    if (!weightIsStored(aWeight)) {
        if (!present) {
            return false;
        }
        entries.erase(itr);
        return true;
    }

    if (present) {
        if (itr->weight == aWeight) {
            return false;
        }
        itr->weight = aWeight;
        return true;
    }

    entries.insert(itr, Entry{aVertex, aWeight});
    return true;
}

bool SkinWeights::BakedBone::removeWeight(quint32 aVertex) {
    return setWeight(aVertex, 0.0f);
}

//-------------------------------------------------------------------------------------------------
SkinWeights::NodeWeights::NodeWeights(): node(), vertexCount(0), meshSignature(0), bakedBones() {}

SkinWeights::NodeWeights::NodeWeights(ObjectNode& aNode, int aVertexCount, quint64 aMeshSignature):
    node(aNode.pointee()), vertexCount(aVertexCount), meshSignature(aMeshSignature), bakedBones() {}

bool SkinWeights::NodeWeights::isBaked(int aBoneId) const { return bakedFor(aBoneId) != nullptr; }

const SkinWeights::BakedBone* SkinWeights::NodeWeights::bakedFor(int aBoneId) const {
    return BakedBone::findById(bakedBones, aBoneId);
}

SkinWeights::BakedBone* SkinWeights::NodeWeights::bakedFor(int aBoneId) {
    // A non-const lookup must go through a non-const begin(): that is what
    // detaches a copy-on-write vector shared with a command snapshot, before
    // the caller can mutate an entry in place.
    const auto itr = std::lower_bound(
        bakedBones.begin(), bakedBones.end(), aBoneId,
        [](const BakedBone& aBone, int aValue) { return aBone.boneId < aValue; }
    );
    if (itr == bakedBones.end() || itr->boneId != aBoneId) {
        return nullptr;
    }
    return &(*itr);
}

void SkinWeights::NodeWeights::setBakedBone(int aBoneId, const QVector<Entry>& aEntries) {
    BakedBone bone;
    bone.boneId = aBoneId;
    bone.entries = aEntries;
    // Stable sort: for a duplicated vertex the LAST supplied value wins, which
    // is the same result as calling setWeight() for each entry in order. A zero
    // in that last position removes the vertex.
    std::stable_sort(bone.entries.begin(), bone.entries.end(), [](const Entry& aLhs, const Entry& aRhs) {
        return aLhs.vertex < aRhs.vertex;
    });

    QVector<Entry> normalised;
    normalised.reserve(bone.entries.size());
    for (int i = 0; i < bone.entries.size();) {
        const quint32 vertex = bone.entries[i].vertex;
        float weight = bone.entries[i].weight;
        while (++i < bone.entries.size() && bone.entries[i].vertex == vertex) {
            weight = bone.entries[i].weight;
        }
        if (weightIsStored(weight)) {
            normalised.push_back(Entry{vertex, weight});
        }
    }
    bone.entries = normalised;
    setBakedBone(bone);
}

void SkinWeights::NodeWeights::setBakedBone(const BakedBone& aBone) {
    const auto itr = std::lower_bound(
        bakedBones.begin(), bakedBones.end(), aBone.boneId,
        [](const BakedBone& aBoneValue, int aValue) { return aBoneValue.boneId < aValue; }
    );
    if (itr != bakedBones.end() && itr->boneId == aBone.boneId) {
        *itr = aBone;
        return;
    }
    bakedBones.insert(itr, aBone);
}

bool SkinWeights::NodeWeights::removeBakedBone(int aBoneId) {
    const auto itr = std::lower_bound(
        bakedBones.begin(), bakedBones.end(), aBoneId,
        [](const BakedBone& aBone, int aValue) { return aBone.boneId < aValue; }
    );
    if (itr == bakedBones.end() || itr->boneId != aBoneId) {
        return false;
    }
    bakedBones.erase(itr);
    return true;
}

//-------------------------------------------------------------------------------------------------
SkinWeights::SkinWeights(): mNodes() {}

const SkinWeights::NodeWeights* SkinWeights::find(const ObjectNode& aNode) const {
    for (const NodeWeights& record : mNodes) {
        if (record.node.get() == &aNode) {
            return &record;
        }
    }
    return nullptr;
}

SkinWeights::NodeWeights* SkinWeights::find(const ObjectNode& aNode) {
    for (NodeWeights& record : mNodes) {
        if (record.node.get() == &aNode) {
            return &record;
        }
    }
    return nullptr;
}

void SkinWeights::setNodeWeights(const NodeWeights& aNode) {
    if (!aNode.node) {
        return;
    }
    if (NodeWeights* existing = find(*aNode.node)) {
        *existing = aNode;
        return;
    }
    mNodes.push_back(aNode);
}

bool SkinWeights::removeNodeWeights(const ObjectNode& aNode) {
    for (auto itr = mNodes.begin(); itr != mNodes.end(); ++itr) {
        if (itr->node.get() == &aNode) {
            mNodes.erase(itr);
            return true;
        }
    }
    return false;
}

SkinWeights::BakedBone SkinWeights::bakeBone(
    const BoneShape& aShape, const QMatrix4x4& aMapMatrix, const gl::Vector3* aPositions,
    int aVertexCount, int aBoneId
) {
    BakedBone bone;
    bone.boneId = aBoneId;
    if (!aPositions || aVertexCount <= 0) {
        return bone;
    }

    bone.entries.reserve(aVertexCount);
    for (int i = 0; i < aVertexCount; ++i) {
        // The build transforms the 2D position (z = 0), never the mesh's own
        // z; doing anything else would change a float and break the
        // bake-equals-envelope gate.
        const QVector3D pos(aPositions[i].x, aPositions[i].y, 0.0f);
        const QVector2D transformed = aMapMatrix.map(pos).toVector2D();
        const float weight = aShape.influence(transformed);
        if (weight >= FLT_EPSILON) {
            bone.entries.push_back(Entry{quint32(i), weight});
        }
    }
    return bone;
}

quint64 SkinWeights::meshSignature(const LayerMesh& aMesh) {
    return meshSignature(aMesh.vertexCount(), aMesh.indices(), aMesh.indexCount());
}

quint64 SkinWeights::meshSignature(int aVertexCount, const GLuint* aIndices, int aIndexCount) {
    // FNV-1a over the mapping (vertex count and the index buffer), not over the
    // geometry: an entry addresses a vertex ARRAY POSITION, so only a change to
    // that mapping invalidates it. See the declaration for the policy.
    quint64 hash = 1469598103934665603ULL;
    hash = hashUInt(hash, quint32(aVertexCount));
    hash = hashUInt(hash, quint32(aIndexCount));

    if (!aIndices && aIndexCount > 0) {
        hash = hashUInt(hash, 0xffffffffu);
    } else {
        for (int i = 0; i < aIndexCount; ++i) {
            hash = hashUInt(hash, quint32(aIndices[i]));
        }
    }
    return hash;
}

} // namespace core
