#ifndef CORE_BONEINFLUENCEMAP_H
#define CORE_BONEINFLUENCEMAP_H

#include <QtOpenGL/QtOpenGL>
#include <QVector>
#include <QVector2D>
#include <QRectF>
#include <QMatrix4x4>
#include "util/NonCopyable.h"
#include "thr/Task.h"
#include "gl/Vector4.h"
#include "gl/Vector4I.h"
#include "core/Bone2.h"
#include "core/SkinWeights.h"
namespace core {
class Project;
}
namespace core {
class LayerMesh;
}

namespace core {

class BoneInfluenceMap: private util::NonCopyable {
public:
    // Attribute storage: a map stores only as many influence slots as its
    // busiest vertex uses. One slot is a uint8 bone index per vertex and NO
    // weight (every weight is exactly 1); two to seven slots add one float32
    // weight per slot, slot-major, bound as scalar attributes. The eight-slot
    // cap keeps the historical dense form (4 x uint8 + 4 x float32 per set,
    // glVertexAttribIPointer + ivec4/vec4). Weights stay float32: the uint16
    // experiment is recorded in the plan (it exceeded the 1-LSB gate on 2.anie).
    // Nothing here is persisted (format 0.21).
    typedef GLubyte IndicesType;
    typedef GLfloat WeightsType;

    class Accessor {
    public:
        Accessor();
        Accessor(const BoneInfluenceMap& aOwner);
        /// 1..7: sparse slot-major arrays. 8: the dense two-set layout.
        int slotCount() const;
        bool dense() const;

        /// Sparse form (slotCount() < 8): one array per slot, vertexCount long.
        const IndicesType* indices(int aSlot) const;
        /// Null when slotCount() == 1: every vertex weight is exactly 1.
        const WeightsType* weights(int aSlot) const;

        /// Dense form (slotCount() == 8): the historical two sets of four.
        const IndicesType* denseIndices(int aGroup) const;
        const WeightsType* denseWeights(int aGroup) const;

        /// The normalised weight of one bone at one vertex, 0 when the bone is
        /// not among that vertex's slots. A one-slot map's weight is implicit
        /// (every stored weight is exactly 1), so this is the only reader that
        /// can answer for every layout without the caller knowing the slot
        /// count - the influence heatmap and the storage gate both use it.
        float weight(int aBoneIndex, int aVertex) const;

        /// The bytes the array storage occupies (not the work buffer).
        qint64 storedBytes() const;

    private:
        const BoneInfluenceMap* mOwner;
    };

    enum { kBonePerVtxMaxEach = 4, kBonePerVtxMaxAll = 8 };
    static_assert(kBonePerVtxMaxEach == 4, "the dense shader reads one ivec4/vec4 per set");
    static_assert(sizeof(IndicesType) == 1, "bone indices are packed uint8");
    static_assert(sizeof(WeightsType) == 4, "bone weights stay float32");

    BoneInfluenceMap();
    ~BoneInfluenceMap();

    void setMaxBoneCount(int aBoneCount);
    void allocate(int aVertexCount, bool aInitialize = true);
    int vertexCount() const { return mVertexCount; }

    void
    writeAsync(
        Project& aProject, const QList<Bone2*>& aTopBones, const QMatrix4x4& aGroupMtx,
        const LayerMesh& aMesh, const SkinWeights::NodeWeights* aBakedWeights = nullptr
    );

    Accessor accessor() const;

    /// The number of bone parameters this map was built with (the index ceiling
    /// a vertex's bone indices address). Call after accessor() joins the build.
    int boneCount() const { return int(mBoneList.params.size()); }

    /// Resident bytes of a map of aVertexCount vertices stored in aSlotCount
    /// slots: 1 B/vertex for one influence (its weight is implicit), otherwise
    /// 5 B per slot. The dense eight-slot form is the same 40 B/vertex.
    static constexpr qint64 storedBytes(int aVertexCount, int aSlotCount) {
        if (aVertexCount <= 0 || aSlotCount <= 0)
            return 0;
        qint64 bytes = qint64(aVertexCount) * aSlotCount * qint64(sizeof(IndicesType));
        // A one-slot map has no weight array (all weights are exactly 1).
        if (aSlotCount >= 2) {
            bytes += qint64(aVertexCount) * aSlotCount * qint64(sizeof(WeightsType));
        }
        return bytes;
    }
    qint64 storedBytes() const { return storedBytes(mVertexCount, mSlotCount); }

private:
    class BoneParam {
    public:
        BoneParam();
        // The authored identity of the bone this index stands for. The GPU
        // index above is pre-order and can change; the baked field is keyed by
        // this id.
        int boneId;
        // Bone2::hasInfluenceEnvelope(): the envelope path skips a bone without
        // it; the shared predicate keeps the bake from re-deriving the rule.
        bool hasEnvelope;
        BoneShape shape;
    };

    class BoneList {
    public:
        BoneList() {}
        QVector<BoneParam> params;
    };

    struct WorkAttribute {
        int id[kBonePerVtxMaxAll];
        float weight[kBonePerVtxMaxAll];
        short count;
        QVector2D vertex;
        void clear();
        void tryPushBoneWeight(int aId, float aWeight);
    };

    class BuildTask: public thr::Task {
    public:
        BuildTask(Project& aProject, BoneInfluenceMap& aOwner);
        void run() override;
        void cancel();

    private:
        Project& mProject;
        BoneInfluenceMap& mOwner;
    };

    void makeBoneList(const QList<Bone2*>& aTopBones);
    void build();
    void transformVertices();
    void writeWeights();
    void writeVertexAttribute();
    void writeSparseAttributes(int aSlotCount);
    void writeDenseAttributes();
    void ensureSparseStorage(int aSlotCount);
    void ensureDenseStorage();
    void freeStorage();
    bool isBuildCanceled() const;
    void waitBuilding() const;

    // tests/bone_influence_storage.cpp drives the writer with synthetic work
    // attributes, so every slot count is gated without a project fixture.
    friend struct BoneInfluenceMapTestProbe;

    int mVertexCount;
    int mSlotCount;
    int mMaxBoneCount;
    QMatrix4x4 mGroupMtx;
    BoneList mBoneList;
    // A snapshot of the target node's baked bones, taken before the task is
    // queued: the document may change (an undo, another stroke) while the build
    // runs, and the build must see the field it was started with. The copy is
    // ONLY the bones - not NodeWeights, whose life-linked node would mutate a
    // LifeLink from the worker thread.
    QVector<SkinWeights::BakedBone> mBakedBones;
    // Sparse form (slotCount 1..7): one contiguous array per slot.
    QScopedArrayPointer<IndicesType> mSparseIndices;
    QScopedArrayPointer<WeightsType> mSparseWeights;
    // Dense form (slotCount 8): the historical two sets of four.
    QScopedArrayPointer<IndicesType> mIndices[2];
    QScopedArrayPointer<WeightsType> mWeights[2];
    QScopedArrayPointer<WorkAttribute> mWorks;
    QScopedPointer<BuildTask> mBuildTask;
};

} // namespace core

#endif // CORE_BONEINFLUENCEMAP_H
