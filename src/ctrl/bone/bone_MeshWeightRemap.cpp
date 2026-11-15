#include "ctrl/bone/bone_MeshWeightRemap.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <limits>

#include "XC.h"
#include "cmnd/Stable.h"
#include "ctrl/CmndName.h"
#include "core/ObjectNode.h"
#include "core/Project.h"

namespace ctrl {
namespace bone {

namespace {

quint64 vertexPairKey(int aLhs, int aRhs) {
    const int lo = std::min(aLhs, aRhs);
    const int hi = std::max(aLhs, aRhs);
    return (quint64(quint32(lo)) << 32) | quint64(quint32(hi));
}

// The vector SkiniWeights::meshSignature hashes after mesh_Notifier rebuilds the
// GL attributes. The command runs BEFORE that rebuild, so the arrays cannot be
// read; the faces' own vertex indices are the same sequence. Keeping this in one
// place is what lets the remap store the exact signature the state compares.
QVector<GLuint> currentIndices(const core::MeshKey::Data& aData) {
    QVector<GLuint> indices;
    indices.reserve(aData.faces().size() * 3);
    for (const core::MeshFace* face : aData.faces()) {
        const auto vertices = face->vertices();
        for (int i = 0; i < 3; ++i) {
            indices.push_back(GLuint(vertices[i]->index()));
        }
    }
    return indices;
}

quint64 currentSignature(const core::MeshKey::Data& aData) {
    const QVector<GLuint> indices = currentIndices(aData);
    return core::SkinWeights::meshSignature(
        aData.vertices().size(), indices.constData(), indices.size()
    );
}

struct VertexBlend {
    int a = -1;
    int b = -1;
    float t = 0.0f;
    bool valid = false;
};

} // namespace

class MeshWeightsRemap: public cmnd::Stable {
public:
    MeshWeightsRemap(
        core::Project& aProject, core::ObjectNode& aNode, core::MeshKey& aKey,
        const QList<core::MeshVtx*>& aOldVertices
    ):
        mProject(aProject.pointee()),
        mTarget(aNode.pointee()),
        mKey(&aKey),
        mOldVertices(aOldVertices),
        mOldEdges(),
        mRecords() {
        // The old edge set tells a split's new vertex which pair of its old
        // neighbours it was created on; the new mesh no longer contains enough
        // information (the edge was replaced by two).
        QHash<const core::MeshVtx*, int> oldIndex;
        for (int i = 0; i < mOldVertices.size(); ++i) {
            oldIndex.insert(mOldVertices[i], i);
        }
        for (const core::MeshEdge* edge : mKey->data().edges()) {
            const auto a = oldIndex.constFind(edge->vtx(0));
            const auto b = oldIndex.constFind(edge->vtx(1));
            if (a != oldIndex.constEnd() && b != oldIndex.constEnd()) {
                mOldEdges.insert(vertexPairKey(a.value(), b.value()));
            }
        }

        for (core::ObjectNode* owner = &aNode; owner; owner = owner->parent()) {
            const core::SkinWeights::NodeWeights* record = owner->skinWeights().find(aNode);
            if (!record || !record->node) {
                continue;
            }
            Record entry;
            entry.owner = owner->pointee();
            entry.before = *record;
            mRecords.push_back(entry);
        }
    }

    QString name() const override { return CmndName::tr("Remap bone weights"); }

    bool isUseless() const override {
        if (mRecords.isEmpty() || !mTarget) {
            return true;
        }
        for (const Record& record : mRecords) {
            // Not computed yet (the stack asks this before exec): the edit has
            // to run, not be skipped as a no-op.
            if (!record.after.node) {
                return false;
            }
            if (!record.before.sameWeightsAs(record.after)) {
                return false;
            }
        }
        return true;
    }

    void exec() override {
        if (!mTarget || !mKey) {
            return;
        }
        computeAfter();
        redo();
    }

    void redo() override {
        for (const Record& record : mRecords) {
            if (record.owner && record.after.node) {
                record.owner->skinWeights().setNodeWeights(record.after);
            }
        }
        notify(false);
    }

    void undo() override {
        for (const Record& record : mRecords) {
            if (record.owner && record.before.node) {
                record.owner->skinWeights().setNodeWeights(record.before);
            }
        }
        notify(true);
    }

private:
    struct Record {
        util::LinkPointer<core::ObjectNode> owner;
        core::SkinWeights::NodeWeights before;
        core::SkinWeights::NodeWeights after;
    };

    void notify(bool aUndo) {
        if (!mProject) {
            return;
        }
        for (const Record& record : mRecords) {
            if (record.owner) {
                mProject->onSkinWeightsModified(*record.owner, aUndo);
            }
        }
    }

    void computeAfter() {
        const core::MeshKey::Data& data = mKey->data();
        const QList<core::MeshVtx*>& now = data.vertices();

        QHash<const core::MeshVtx*, int> oldIndex;
        for (int i = 0; i < mOldVertices.size(); ++i) {
            oldIndex.insert(mOldVertices[i], i);
        }
        QHash<const core::MeshVtx*, int> newIndex;
        for (int i = 0; i < now.size(); ++i) {
            newIndex.insert(now[i], i);
        }

        QVector<int> oldToNew(mOldVertices.size(), -1);
        for (int i = 0; i < mOldVertices.size(); ++i) {
            const auto itr = newIndex.constFind(mOldVertices[i]);
            if (itr != newIndex.constEnd()) {
                oldToNew[i] = itr.value();
            }
        }

        // A new vertex's old neighbours, read from the NEW edge list: a split
        // connects its centre to the two vertices of the edge it was created on
        // (plus, for a single-edge split, the face's opposite vertices - the old
        // edge set below rejects those pairs because they were not the split
        // edge, and the point-on-segment test picks the right one anyway).
        QHash<int, QVector<int>> oldNeighbours;
        for (const core::MeshEdge* edge : data.edges()) {
            const auto a = oldIndex.constFind(edge->vtx(0));
            const auto b = oldIndex.constFind(edge->vtx(1));
            const auto na = newIndex.constFind(edge->vtx(0));
            const auto nb = newIndex.constFind(edge->vtx(1));
            if (na == newIndex.constEnd() || nb == newIndex.constEnd()) {
                continue;
            }
            const bool aOld = a != oldIndex.constEnd();
            const bool bOld = b != oldIndex.constEnd();
            if (!aOld && bOld) {
                oldNeighbours[na.value()].push_back(b.value());
            } else if (aOld && !bOld) {
                oldNeighbours[nb.value()].push_back(a.value());
            }
        }

        QHash<int, VertexBlend> blends;
        for (int i = 0; i < now.size(); ++i) {
            if (oldIndex.contains(now[i])) {
                continue;
            }
            const QVector<int>& neighbours = oldNeighbours.value(i);
            if (neighbours.isEmpty()) {
                continue;
            }
            if (neighbours.size() == 1) {
                VertexBlend blend;
                blend.a = neighbours[0];
                blend.t = 0.0f;
                blend.valid = true;
                blends.insert(i, blend);
                continue;
            }

            const QVector2D pos = now[i]->vec();
            float bestDistance = std::numeric_limits<float>::max();
            VertexBlend best;
            for (int a = 0; a < neighbours.size(); ++a) {
                for (int b = a + 1; b < neighbours.size(); ++b) {
                    const int ia = neighbours[a];
                    const int ib = neighbours[b];
                    // Only a pair that was an EDGE before the edit can be the
                    // edge this vertex split; two old vertices connected only
                    // through the new one have no weights to interpolate.
                    if (!mOldEdges.contains(vertexPairKey(ia, ib))) {
                        continue;
                    }
                    if (ia < 0 || ia >= mOldVertices.size() || ib < 0
                        || ib >= mOldVertices.size()) {
                        continue;
                    }
                    const QVector2D p0 = mOldVertices[ia]->vec();
                    const QVector2D p1 = mOldVertices[ib]->vec();
                    const QVector2D edge = p1 - p0;
                    const float lengthSq = edge.lengthSquared();
                    if (lengthSq <= 1.0e-12f) {
                        continue;
                    }
                    float t = QVector2D::dotProduct(pos - p0, edge) / lengthSq;
                    t = std::min(std::max(t, 0.0f), 1.0f);
                    const QVector2D closest = p0 + edge * t;
                    const float distance = (pos - closest).length();
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        best.a = ia;
                        best.b = ib;
                        best.t = t;
                        best.valid = true;
                    }
                }
            }
            // A split's centre is ON the old edge (CollDetect's point is
            // computed on the segment); an arbitrary new vertex is not, and
            // starts at zero rather than inheriting the nearest pair's field.
            if (best.valid && bestDistance <= 0.05f) {
                blends.insert(i, best);
            }
        }

        const quint64 signature = currentSignature(data);
        const int vertexCount = now.size();

        for (Record& record : mRecords) {
            core::SkinWeights::NodeWeights after = record.before;
            after.vertexCount = vertexCount;
            after.meshSignature = signature;

            for (core::SkinWeights::BakedBone& bone : after.bakedBones) {
                QVector<core::SkinWeights::Entry> entries;
                entries.reserve(bone.entries.size());
                for (const core::SkinWeights::Entry& entry : bone.entries) {
                    if (entry.vertex >= quint32(mOldVertices.size())) {
                        continue;   // already dormant beyond the old mesh
                    }
                    const int mapped = oldToNew[int(entry.vertex)];
                    if (mapped >= 0) {
                        entries.push_back(core::SkinWeights::Entry{quint32(mapped), entry.weight});
                    }
                }
                for (auto itr = blends.constBegin(); itr != blends.constEnd(); ++itr) {
                    const VertexBlend& blend = itr.value();
                    const float wa = bone.weightAt(quint32(blend.a));
                    float weight = wa;
                    if (blend.b >= 0) {
                        const float wb = bone.weightAt(quint32(blend.b));
                        weight = wa * (1.0f - blend.t) + wb * blend.t;
                    }
                    if (weight >= FLT_EPSILON) {
                        entries.push_back(
                            core::SkinWeights::Entry{quint32(itr.key()), weight}
                        );
                    }
                }
                bone.entries = entries;
                std::stable_sort(
                    bone.entries.begin(), bone.entries.end(),
                    [](const core::SkinWeights::Entry& aLhs, const core::SkinWeights::Entry& aRhs) {
                        return aLhs.vertex < aRhs.vertex;
                    }
                );
            }
            record.after = after;
        }
    }

    util::LinkPointer<core::Project> mProject;
    util::LinkPointer<core::ObjectNode> mTarget;
    // The mesh commands in the tree hold the key by reference for the same
    // reason: the edit's lifetime is the key's.
    core::MeshKey* mKey;
    QList<core::MeshVtx*> mOldVertices;
    QSet<quint64> mOldEdges;
    QVector<Record> mRecords;
};

cmnd::Base* createMeshWeightsRemap(
    core::Project& aProject, core::ObjectNode& aNode, core::MeshKey& aMeshKey,
    const QList<core::MeshVtx*>& aOldVertices
) {
    auto* command = new MeshWeightsRemap(aProject, aNode, aMeshKey, aOldVertices);
    if (command->isUseless()) {
        delete command;
        return nullptr;
    }
    return command;
}

} // namespace bone
} // namespace ctrl
