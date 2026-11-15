// The skeleton bone cap: one number shared by the pose palette, the influence
// build and the skinning shader.
//
// `core::PosePalette::kMaxCount` sizes the palette that poses bones and binds
// rigid layers, `BoneKey::Cache` passes the same constant to the influence
// map's bone list, and `MeshTransformerResource` sets the shader's
// `MAX_BONES` variation from it. Before this gate the three limits were 32,
// and a 33-bone skeleton (skel_practice_12.anie in the dataset) silently lost
// its last bone: no influence weight, no posed transform, no rigid binding.
//
// What is pinned here:
//   * a 33-bone chain, the dataset's boundary: the influence build keeps all
//     33 bones and a vertex inside the 33rd bone's envelope stores index 32
//     (the old 32-bone cap stored the default bone 0 weight 1 instead);
//   * the palette builds 33 entries and the 33rd pose transform reaches
//     matrices()[32] and dualQuaternions()[32];
//   * a 65-bone chain keeps all 65 bones (past the old 64 cap, so this is the
//     size the uniform block exists for);
//   * the cap still exists at the uint8 ceiling: a 257-bone chain stops at
//     kMaxCount and a vertex only the 257th bone covers gets the default
//     influence.
//
// The shader half (a bone index of 32 selecting uBoneDualQuat[64..65]) cannot
// be compiled without a GL context; the real project's frames are the GL gate
// (see the influence plan's §7), and MeshTransformerResource ties the uniform
// size to the same constant this file pins.
//
// No project, no GL, no window. `Project.cpp` references the GUI's audio
// sidecar, so the stub below is the same one bone_influence_storage uses.
#include <QApplication>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QPolygonF>
#include <QVector2D>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
#include "XCAssert.h"
#include "core/Bone2.h"
#include "core/BoneKey.h"
#include "core/BoneShape.h"
#include "core/PoseKey.h"
#include "core/PosePalette.h"
#include "gui/AudioPlaybackWidget.h"
#include "util/Segment2D.h"

// Project.cpp references this GUI entry point; the test never opens a .aemus.
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

struct core::BoneInfluenceMapTestProbe {
    /// Sets up the work buffer the way writeAsync does, then runs the real
    /// makeBoneList -> writeWeights -> writeVertexAttribute path. Returns the
    /// number of bone parameters the cap allowed into the map.
    static int buildFromBones(
        core::BoneInfluenceMap& aMap, const QList<core::Bone2*>& aTopBones,
        const std::vector<QVector2D>& aVertices
    ) {
        aMap.mVertexCount = int(aVertices.size());
        aMap.freeStorage();
        aMap.mWorks.reset(new core::BoneInfluenceMap::WorkAttribute[aMap.mVertexCount]);
        for (size_t i = 0; i < aVertices.size(); ++i) {
            aMap.mWorks[i].clear();
            aMap.mWorks[i].vertex = aVertices[i];
        }

        aMap.makeBoneList(aTopBones);
        aMap.writeWeights();
        aMap.writeVertexAttribute();

        const int params = aMap.mBoneList.params.size();
        aMap.mWorks.reset();
        return params;
    }
};

namespace {

class HarnessAssertHandler: public XCAssertHandler {
public:
    void failure() const override { std::abort(); }
};
class HarnessErrorHandler: public XCErrorHandler {
public:
    void critical(const QString&, const QString&, const QString&) const override { std::abort(); }
};
HarnessAssertHandler gAssertHandler;
HarnessErrorHandler gErrorHandler;
int failures = 0;

void check(bool aCondition, const char* aWhat) {
    if (aCondition) {
        std::printf("PASS  %s\n", aWhat);
    } else {
        std::printf("FAIL  %s\n", aWhat);
        ++failures;
    }
}

core::Bone2* boneAtIndex(const QList<core::Bone2*>& aTopBones, int aIndex) {
    int index = 0;
    for (core::Bone2* top : aTopBones) {
        for (core::Bone2::Iterator itr(top); itr.hasNext();) {
            core::Bone2* bone = itr.next();
            if (index++ == aIndex)
                return bone;
        }
    }
    return nullptr;
}

/// A chain of `aCount` bones, one unit apart along x, owned by a real BoneKey so
/// the palette sees a real BoneKey::Data. Bone i owns a unit envelope at its
/// root, so a vertex at (i, 0) is influenced by bone i only; Bone2's destructor
/// deletes the children through the root.
struct Chain {
    core::BoneKey key;

    explicit Chain(int aCount) {
        XC_ASSERT(aCount >= 1);
        QList<core::Bone2*>& tops = key.data().topBones();

        auto* root = new core::Bone2();
        tops.push_back(root);
        root->updateWorldTransform();

        core::Bone2* parent = root;
        for (int i = 1; i < aCount; ++i) {
            auto* bone = new core::Bone2();
            parent->children().pushBack(bone);
            bone->setWorldPos(QVector2D(float(i), 0.0f), parent);
            bone->updateWorldTransform();
            parent = bone;

            // Geometry is in world space; the influence build only reads shapes.
            core::BoneShape shape;
            shape.setSegment(util::Segment2D(QVector2D(float(i), 0.0f), QVector2D(0.0f, 1.0f)));
            shape.setRadius(QVector2D(0.25f, 0.25f), QVector2D(0.25f, 0.25f));
            QPolygonF polygon;
            polygon << QPointF(i - 0.5, -0.5) << QPointF(i + 0.5, -0.5) << QPointF(i + 0.5, 0.5)
                    << QPointF(i - 0.5, 0.5);
            shape.setPolygon(polygon);
            bone->setShape(shape);
            bone->setRange(0, QVector2D(0.25f, 0.25f));
            bone->setRange(1, QVector2D(0.25f, 0.25f));
        }
    }
};

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    using core::BoneInfluenceMap;
    using Probe = core::BoneInfluenceMapTestProbe;

    // The skeleton cap is the uint8 index ceiling (indices 0..255 = 256 bones).
    // The palette travels in a uniform block: its guaranteed 16384 bytes hold all
    // 256 dual quaternions (8192 bytes), while a default-block uniform array
    // would spend the whole 1024-component budget at 128 bones. The shader's
    // MAX_BONES is set from this constant by MeshTransformerResource; a change
    // here must also update that arithmetic.
    static_assert(core::PosePalette::kMaxCount == 256, "the bone index is uint8: the cap is 256 bones");

    // THE DATASET'S BOUNDARY: 33 bones. skel_practice_12.anie's "wings 1" key
    // holds 33; with the old cap its last bone was dropped from the influence
    // build and from the palette, silently.
    {
        Chain chain(33);
        const QList<core::Bone2*>& tops = chain.key.data().topBones();

        // Influence side, through the production holder: BoneKey::Cache sets
        // the map's cap.
        core::BoneKey::Cache cache;
        const std::vector<QVector2D> vertices = {QVector2D(32.0f, 0.0f)};
        const int params = Probe::buildFromBones(cache.influence(), tops, vertices);
        check(params == 33, "the influence build keeps all 33 bones");
        const BoneInfluenceMap::Accessor accessor = cache.influence().accessor();
        check(accessor.slotCount() == 1, "the 33rd-bone vertex has one influence");
        check(
            accessor.indices(0) && accessor.indices(0)[0] == 32,
            "a vertex inside the 33rd bone stores bone index 32"
        );

        // CONTROL: the same chain under the old 32-bone limit loses that bone.
        // This is what the gate would read if the cap regressed, and it is the
        // exact defect the dataset project showed (index 0, weight 1 instead).
        {
            BoneInfluenceMap oldCap;
            oldCap.setMaxBoneCount(32);
            const int oldParams = Probe::buildFromBones(oldCap, tops, vertices);
            const BoneInfluenceMap::Accessor oldAccessor = oldCap.accessor();
            check(oldParams == 32, "the 32-bone control stops at 32 parameters");
            check(
                oldAccessor.indices(0) && oldAccessor.indices(0)[0] == 0,
                "the 32-bone control drops the 33rd bone to the default influence"
            );
        }

        // Palette side: the 33rd pose bone's transform is built and uploaded.
        core::PoseKey::Data pose;
        pose.createBonesBy(chain.key);
        core::Bone2* posed = boneAtIndex(pose.topBones(), 32);
        check(posed != nullptr, "the pose has a 33rd bone");
        if (posed) {
            posed->setRotate(0.5f);
            for (core::Bone2* top : pose.topBones())
                top->updateWorldTransform();
        }

        core::Bone2* keyBone32 = boneAtIndex(tops, 32);
        check(keyBone32 != nullptr, "the key has a 33rd bone");
        if (keyBone32) {
            check(
                core::PosePalette::getBoneIndex(chain.key.data(), *keyBone32) == 32,
                "the 33rd bone's palette index is 32"
            );
            check(
                chain.key.data().boneIndex(*keyBone32) == 32,
                "the 33rd bone's influence-map index is 32"
            );
        }
        check(
            chain.key.data().boneIndex(*boneAtIndex(tops, 0)) == 0,
            "the root bone's influence-map index is 0"
        );
        core::Bone2 outsider;
        check(
            chain.key.data().boneIndex(outsider) == -1,
            "a bone outside the skeleton has no influence-map index"
        );

        // The bone a stroke starts on: the nearest envelope in target space.
        // Bone i owns the segment (i-1,0)->(i,0) in this chain.
        check(
            chain.key.data().nearestBone(QVector2D(3.5f, 0.2f)) == boneAtIndex(tops, 4),
            "the nearest bone is the one whose segment is closest"
        );
        check(
            chain.key.data().nearestBone(QVector2D(-1.0f, 0.1f)) == boneAtIndex(tops, 1),
            "a point before the root clamps to the first child"
        );
        {
            core::BoneKey rootOnly;
            rootOnly.data().topBones().push_back(new core::Bone2());
            check(
                rootOnly.data().nearestBone(QVector2D(0.0f, 0.0f)) == nullptr,
                "a root-only skeleton has no influence bone to select"
            );
        }

        core::PosePalette palette;
        core::PosePalette::KeyPairs pairs;
        pairs.push_back(core::PosePalette::KeyPair{&chain.key.data(), &pose});
        palette.build(pairs);

        check(palette.usedCount() == 33, "the palette's used count is the skeleton's 33 bones");
        check(
            palette.matrices().count() == 33,
            "the palette is sized to the skeleton, not to kMaxCount (matrices)"
        );
        check(
            palette.dualQuaternions().count() == 33,
            "the palette is sized to the skeleton, not to kMaxCount (dual quaternions)"
        );
        if (posed && keyBone32) {
            const QMatrix4x4 mtx = palette.matrices()[32];
            check(
                std::abs(mtx(0, 0) - std::cos(0.5f)) < 1e-4f && std::abs(mtx(1, 0) - std::sin(0.5f)) < 1e-4f,
                "the 33rd pose rotation reaches matrices()[32]"
            );
            const core::PosePalette::DualQuaternion dq = palette.dualQuaternions()[32];
            check(
                std::abs(dq.real.x - std::cos(0.25f)) < 1e-4f,
                "the 33rd pose rotation reaches dualQuaternions()[32]"
            );
        } else {
            check(false, "the 33rd pose bone exists (palette check skipped)");
        }
    }

    // 65 BONES: past the old cap. The influence build must keep all 65, and the
    // palette must size itself to 65, not to a path threshold.
    {
        Chain chain(65);
        const QList<core::Bone2*>& tops = chain.key.data().topBones();
        core::BoneKey::Cache cache;
        const std::vector<QVector2D> vertices = {QVector2D(64.0f, 0.0f)};
        const int params = Probe::buildFromBones(cache.influence(), tops, vertices);
        check(params == 65, "a 65-bone skeleton keeps all 65 influence parameters");
        const BoneInfluenceMap::Accessor accessor = cache.influence().accessor();
        check(
            accessor.indices(0) && accessor.indices(0)[0] == 64,
            "a vertex inside the 65th bone stores index 64"
        );

        core::PoseKey::Data pose;
        pose.createBonesBy(chain.key);
        for (core::Bone2* top : chain.key.data().topBones())
            top->updateWorldTransform();
        for (core::Bone2* top : pose.topBones())
            top->updateWorldTransform();
        core::PosePalette palette;
        core::PosePalette::KeyPairs pairs;
        pairs.push_back(core::PosePalette::KeyPair{&chain.key.data(), &pose});
        palette.build(pairs);
        check(palette.usedCount() == 65, "the 65-bone palette keeps all 65 bones");
        check(
            palette.dualQuaternions().count() == 65 && palette.matrices().count() == 65,
            "the 65-bone palette is sized to 65 on both arrays"
        );
    }

    // THE HARD CAP STILL EXISTS: 257 bones stop at the uint8 ceiling (256).
    {
        Chain chain(core::PosePalette::kMaxCount + 1);
        core::BoneKey::Cache cache;
        const std::vector<QVector2D> vertices = {QVector2D(float(core::PosePalette::kMaxCount), 0.0f)};
        const int params = Probe::buildFromBones(cache.influence(), chain.key.data().topBones(), vertices);
        check(
            params == core::PosePalette::kMaxCount,
            "a skeleton beyond the uint8 ceiling stops at kMaxCount bone parameters"
        );
        const BoneInfluenceMap::Accessor accessor = cache.influence().accessor();
        check(
            accessor.indices(0) && accessor.indices(0)[0] == 0,
            "a vertex only the dropped bone covers gets the default influence"
        );
    }

    if (failures == 0) {
        std::printf("PASS: bone cap\n");
    } else {
        std::printf("FAILED %d check(s)\n", failures);
    }
    return failures == 0 ? 0 : 1;
}
