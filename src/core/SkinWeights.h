#ifndef CORE_SKINWEIGHTS_H
#define CORE_SKINWEIGHTS_H

#include <QMatrix4x4>
#include <QVector>
#include <QtGlobal>
#include <QtOpenGL/QtOpenGL>
#include "util/LinkPointer.h"

namespace gl {
struct Vector2;
struct Vector3;
}

namespace core {
class BoneShape;
class LayerMesh;
class ObjectNode;

// AUTHORED SKIN WEIGHTS (format 0.22): a per-(bone key owner, influenced node)
// record of the weight field a baked bone contributes.
//
// The state is per bone, not per vertex: a bone is Envelope (no BakedBone
// record; the live BoneShape evaluates every rebuild) or Baked (a record is
// present; its entries are the WHOLE field and a missing vertex is exactly 0).
// An empty entry list is therefore a valid Baked state that contributes
// nothing - do not infer the state from the entry count. See
// docs/design/skeletal/influence-painting-plan.md §2-§4.
//
// One rig can own several BoneKeys (one per keyed frame). The weights live on
// the owner node, not on a key, so every key of that owner shares one baked
// field; baking refuses when a key's same-id bone has different authored
// geometry (Bone2::hasSameAuthoredGeometry).
class SkinWeights {
public:
    struct Entry {
        quint32 vertex = 0;
        float weight = 0.0f;

        bool operator==(const Entry& aRhs) const {
            return vertex == aRhs.vertex && weight == aRhs.weight;
        }
    };

    // One baked bone for one node. `entries` is sorted by vertex and stores
    // only nonzero weights; setWeight/removeWeight keep that invariant, so the
    // influence build walks the list as a cursor beside its vertex loop.
    struct BakedBone {
        int boneId = 0;
        QVector<Entry> entries;

        // Sorted-by-id lookup, shared by NodeWeights and the influence build's
        // thread-safe snapshot of the bones.
        static const BakedBone* findById(const QVector<BakedBone>& aBones, int aBoneId);

        const Entry* find(quint32 aVertex) const;
        float weightAt(quint32 aVertex) const;
        // Inserts, updates or removes (weight <= FLT_EPSILON) one vertex.
        // Returns whether the field changed; the entries stay sorted.
        bool setWeight(quint32 aVertex, float aWeight);
        bool removeWeight(quint32 aVertex);

        bool operator==(const BakedBone& aRhs) const {
            return boneId == aRhs.boneId && entries == aRhs.entries;
        }
        bool operator!=(const BakedBone& aRhs) const { return !(*this == aRhs); }
    };

    // The records for one influenced node of the owner's subtree. The node is
    // life-linked: a deleted node turns the pointer null instead of dangling,
    // and the record is then dormant (ignored by the build, skipped on save).
    struct NodeWeights {
        util::LinkPointer<ObjectNode> node;
        int vertexCount = 0;
        // Hash of the mesh at bake time; a mismatch warns but never fails a
        // load and never shifts an entry to another vertex (§3.4).
        quint64 meshSignature = 0;
        // Sorted by bone id; presence is the Baked state.
        QVector<BakedBone> bakedBones;

        NodeWeights();
        explicit NodeWeights(ObjectNode& aNode, int aVertexCount = 0, quint64 aMeshSignature = 0);

        bool isBaked(int aBoneId) const;
        const BakedBone* bakedFor(int aBoneId) const;
        BakedBone* bakedFor(int aBoneId);
        // Replaces (or inserts) the record for a bone id. The entries are
        // normalised (sorted, zero weights dropped); presence is preserved even
        // when the normalised field is empty.
        void setBakedBone(int aBoneId, const QVector<Entry>& aEntries);
        void setBakedBone(const BakedBone& aBone);
        // Returns whether a record was present.
        bool removeBakedBone(int aBoneId);

        // Value comparison of the authored field, ignoring the life-linked node
        // pointer: two records for the same target are equal when they carry the
        // same count, signature and bones. Used by the commands to tell a real
        // edit from a no-op without comparing pointers.
        bool sameWeightsAs(const NodeWeights& aRhs) const {
            return vertexCount == aRhs.vertexCount && meshSignature == aRhs.meshSignature
                && bakedBones == aRhs.bakedBones;
        }
    };

    SkinWeights();

    int count() const { return mNodes.size(); }
    bool isEmpty() const { return mNodes.isEmpty(); }
    const QVector<NodeWeights>& nodes() const { return mNodes; }
    QVector<NodeWeights>& nodes() { return mNodes; }

    const NodeWeights* find(const ObjectNode& aNode) const;
    NodeWeights* find(const ObjectNode& aNode);
    // Replaces the record for its node (or appends one). A record whose node is
    // null is ignored.
    void setNodeWeights(const NodeWeights& aNode);
    bool removeNodeWeights(const ObjectNode& aNode);

    // One baked bone evaluated from an envelope over a node's mesh. The matrix
    // is the build's whole map matrix (the cache's inner matrix translated by
    // the origin offset), and the positions are the mesh's own; the result is
    // the exact entry list the envelope path pushes, which is what makes the
    // bake frame bit-identical to the live field.
    static BakedBone bakeBone(
        const BoneShape& aShape, const QMatrix4x4& aMapMatrix, const gl::Vector3* aPositions,
        int aVertexCount, int aBoneId
    );

    // Hash(vertex count, index buffer) used for the mesh-change warning.
    //
    // The signature names the MAPPING the entries are stored in, not the mesh's
    // shape: an entry is addressed by its position in the vertex array, so it is
    // wrong only when the array is reordered, inserted into or removed from.
    // Moving a vertex or editing a uv keeps the index mapping and must NOT warn
    // (the plan's §7: "position edits keep indices: the weights follow"). The
    // index buffer is what changes on a structural edit; a count change covers a
    // vertex that no face uses.
    static quint64 meshSignature(const LayerMesh& aMesh);
    static quint64 meshSignature(int aVertexCount, const GLuint* aIndices, int aIndexCount);

private:
    QVector<NodeWeights> mNodes;
};

} // namespace core

#endif // CORE_SKINWEIGHTS_H
