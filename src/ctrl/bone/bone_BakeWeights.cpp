#include "ctrl/bone/bone_BakeWeights.h"
#include "core/Bone2.h"
#include "core/LayerMesh.h"
#include "core/TimeKeyBlender.h"

namespace ctrl {
namespace bone {

namespace {

core::Bone2* findBoneById(core::BoneKey& aKey, int aBoneId) {
    for (core::Bone2* top : aKey.data().topBones()) {
        for (core::Bone2::Iterator itr(top); itr.hasNext();) {
            core::Bone2* bone = itr.next();
            if (bone->boneId() == aBoneId) {
                return bone;
            }
        }
    }
    return nullptr;
}

QString frameError(const QString& aWhat, int aFrame) {
    return QStringLiteral("%1 (frame %2)").arg(aWhat).arg(aFrame);
}

} // namespace

bool bakeBoneFields(
    core::Project& aProject, core::ObjectNode& aOwner, core::BoneKey& aKey, const core::Bone2& aBone,
    QVector<core::SkinWeights::NodeWeights>& aFields, QString* aError
) {
    aFields.clear();

    if (!aOwner.timeLine()) {
        if (aError) {
            *aError = QStringLiteral("The bone owner has no timeline.");
        }
        return false;
    }

    // The shared-rig rule: one authored field is addressed by every BoneKey of
    // the owner. Each key must carry the same-id bone with the same authored
    // envelope, or silently sharing the field would deform another skeleton.
    const core::TimeLine::MapType& keys = aOwner.timeLine()->map(core::TimeKeyType_Bone);
    for (auto itr = keys.begin(); itr != keys.end(); ++itr) {
        auto* key = dynamic_cast<core::BoneKey*>(itr.value());
        if (!key) {
            continue;
        }
        const core::Bone2* same = findBoneById(*key, aBone.boneId());
        if (!same) {
            if (aError) {
                *aError = frameError(
                    QStringLiteral("The baked bone does not exist on another key of this rig."),
                    key->frame()
                );
            }
            return false;
        }
        if (!aBone.hasSameAuthoredGeometry(*same)) {
            if (aError) {
                *aError = frameError(
                    QStringLiteral("The rig differs on another key; make the keys share the same bone geometry."),
                    key->frame()
                );
            }
            return false;
        }
    }

    auto time = aProject.currentTimeInfo();
    time.frame.set(aKey.frame());

    QVector<core::ObjectNode*> targets;
    aKey.collectInfluenceTargets(time, aOwner, targets);

    for (core::ObjectNode* node : targets) {
        if (!node) {
            continue;
        }
        const core::LayerMesh* mesh = core::TimeKeyBlender::getAreaMesh(*node, time);
        // No mesh at this key: the node is not bakeable here. Its state stays
        // Envelope, which makes the bone Mixed for the panel.
        if (!mesh || mesh->vertexCount() <= 0) {
            continue;
        }

        // The exact matrix BoneKey::updateCaches hands the build, from the one
        // function that defines it.
        const QMatrix4x4 mapMtx = core::TimeKeyBlender::getInfluenceMapMatrix(*node, time, &aOwner);

        core::SkinWeights::NodeWeights record(
            *node, mesh->vertexCount(), core::SkinWeights::meshSignature(*mesh)
        );
        // Match the envelope build's own guard: it skips a parentless bone and
        // a bone with no valid range, even if a shape was authored. The baked
        // field for such a bone is empty, not its shape evaluated.
        if (aBone.hasInfluenceEnvelope()) {
            record.setBakedBone(core::SkinWeights::bakeBone(
                aBone.shape(), mapMtx, mesh->positions(), mesh->vertexCount(), aBone.boneId()
            ));
        } else {
            core::SkinWeights::BakedBone empty;
            empty.boneId = aBone.boneId();
            record.setBakedBone(empty);
        }
        aFields.push_back(record);
    }

    if (aFields.isEmpty()) {
        if (aError) {
            *aError = QStringLiteral("The rig has no mesh to bake at this frame.");
        }
        return false;
    }
    return true;
}

BakeWeightsCommand::BakeWeightsCommand(
    core::Project& aProject, core::ObjectNode& aOwner,
    const QVector<core::SkinWeights::NodeWeights>& aFields
):
    mProject(aProject.pointee()), mOwner(aOwner.pointee()), mAfter(aFields), mBefore() {}

void BakeWeightsCommand::exec() {
    if (mCaptured || !mOwner) {
        redo();
        return;
    }

    // The incoming fields carry ONE selected bone. Merge it into whatever the
    // node already has: baking a second bone must not drop the first, and a
    // record that already existed keeps its vertex count and bake-time mesh
    // signature (one signature per node, as the format defines).
    const QVector<core::SkinWeights::NodeWeights> fields = mAfter;
    mBefore.clear();
    mAfter.clear();
    mBefore.reserve(fields.size());
    mAfter.reserve(fields.size());

    for (const core::SkinWeights::NodeWeights& field : fields) {
        if (!field.node) {
            mBefore.push_back(core::SkinWeights::NodeWeights());
            mAfter.push_back(core::SkinWeights::NodeWeights());
            continue;
        }

        const core::SkinWeights::NodeWeights* existing = mOwner->skinWeights().find(*field.node);
        mBefore.push_back(existing ? *existing : core::SkinWeights::NodeWeights());

        core::SkinWeights::NodeWeights merged = existing ? *existing : field;
        for (const core::SkinWeights::BakedBone& bone : field.bakedBones) {
            merged.setBakedBone(bone);
        }
        mAfter.push_back(merged);
    }

    mCaptured = true;
    redo();
}

void BakeWeightsCommand::redo() {
    if (!mOwner) {
        return;
    }

    for (const core::SkinWeights::NodeWeights& record : mAfter) {
        if (record.node) {
            mOwner->skinWeights().setNodeWeights(record);
        }
    }

    if (mProject) {
        mProject->onSkinWeightsModified(*mOwner, false);
    }
}

void BakeWeightsCommand::undo() {
    if (!mOwner) {
        return;
    }

    for (int i = 0; i < mAfter.size() && i < mBefore.size(); ++i) {
        const core::SkinWeights::NodeWeights& before = mBefore[i];
        if (before.node) {
            mOwner->skinWeights().setNodeWeights(before);
        } else if (mAfter[i].node) {
            // The node was Envelope before the bake: the record itself is the
            // state to remove, even when its entry list was empty.
            mOwner->skinWeights().removeNodeWeights(*mAfter[i].node);
        }
    }

    if (mProject) {
        mProject->onSkinWeightsModified(*mOwner, true);
    }
}

} // namespace bone
} // namespace ctrl
