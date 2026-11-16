#include "ctrl/bone/bone_WeightCommands.h"

#include "XC.h"
#include "core/Bone2.h"
#include "core/BoneKey.h"
#include "core/ObjectNode.h"
#include "core/Project.h"
#include "ctrl/CmndName.h"
#include "ctrl/bone/bone_BakeWeights.h"

namespace ctrl {
namespace bone {

namespace {

bool hasLiveTarget(
    const core::SkinWeights& aWeights, const core::SkinWeights::NodeWeights& aField, int aBoneId
) {
    const core::SkinWeights::NodeWeights* existing = aWeights.find(*aField.node);
    return !existing || !existing->isBaked(aBoneId);
}

} // namespace

//-------------------------------------------------------------------------------------------------
AssignSkinWeightsCommand::AssignSkinWeightsCommand(
    core::Project& aProject, core::ObjectNode& aOwner,
    const QVector<core::SkinWeights::NodeWeights>& aBefore,
    const QVector<core::SkinWeights::NodeWeights>& aAfter, const QString& aName
):
    mProject(aProject.pointee()),
    mOwner(aOwner.pointee()),
    mBefore(aBefore),
    mAfter(aAfter),
    mName(aName) {
    XC_ASSERT(mBefore.size() == mAfter.size());
}

bool AssignSkinWeightsCommand::isUseless() const {
    if (!mOwner || mBefore.size() != mAfter.size()) {
        return true;
    }
    for (int i = 0; i < mBefore.size(); ++i) {
        const bool beforePresent = mBefore[i].node;
        const bool afterPresent = mAfter[i].node;
        if (beforePresent != afterPresent) {
            return false;
        }
        if (beforePresent && !mBefore[i].sameWeightsAs(mAfter[i])) {
            return false;
        }
    }
    return true;
}

void AssignSkinWeightsCommand::exec() { redo(); }

void AssignSkinWeightsCommand::redo() {
    if (!mOwner) {
        return;
    }
    writeAfter();
    if (mProject) {
        mProject->onSkinWeightsModified(*mOwner, false);
    }
}

void AssignSkinWeightsCommand::undo() {
    if (!mOwner) {
        return;
    }
    writeBefore();
    if (mProject) {
        mProject->onSkinWeightsModified(*mOwner, true);
    }
}

void AssignSkinWeightsCommand::writeAfter() {
    for (int i = 0; i < mAfter.size() && i < mBefore.size(); ++i) {
        if (mAfter[i].node) {
            mOwner->skinWeights().setNodeWeights(mAfter[i]);
        } else if (mBefore[i].node) {
            // The record itself was the edit (Revert removed the bone and left
            // no record behind).
            mOwner->skinWeights().removeNodeWeights(*mBefore[i].node);
        }
    }
}

void AssignSkinWeightsCommand::writeBefore() {
    for (int i = 0; i < mBefore.size() && i < mAfter.size(); ++i) {
        if (mBefore[i].node) {
            mOwner->skinWeights().setNodeWeights(mBefore[i]);
        } else if (mAfter[i].node) {
            mOwner->skinWeights().removeNodeWeights(*mAfter[i].node);
        }
    }
}

void AssignSkinWeightsCommand::setAfter(const core::SkinWeights::NodeWeights& aRecord) {
    if (!aRecord.node) {
        return;
    }
    for (int i = 0; i < mBefore.size(); ++i) {
        if (mBefore[i].node.get() == aRecord.node.get()
            || (!mBefore[i].node && mAfter[i].node.get() == aRecord.node.get())) {
            mAfter[i] = aRecord;
            return;
        }
    }
}

//-------------------------------------------------------------------------------------------------
bool bakeBoneWeights(
    core::Project& aProject, core::ObjectNode& aOwner, core::BoneKey& aKey, const core::Bone2& aBone,
    bool aReplace, QString* aError
) {
    QVector<core::SkinWeights::NodeWeights> fields;
    if (!bakeBoneFields(aProject, aOwner, aKey, aBone, fields, aError)) {
        return false;
    }

    const int boneId = aBone.boneId();
    QVector<core::SkinWeights::NodeWeights> before;
    QVector<core::SkinWeights::NodeWeights> after;
    before.reserve(fields.size());
    after.reserve(fields.size());

    for (const core::SkinWeights::NodeWeights& field : fields) {
        if (!field.node) {
            continue;
        }
        // Bake is additive to the STATE, not to the field: an already-baked node
        // is left alone unless the user asked for a Rebake. Writing the live
        // envelope over a painted field by accident is the one thing this
        // command must never do.
        if (!aReplace && !hasLiveTarget(aOwner.skinWeights(), field, boneId)) {
            continue;
        }

        const core::SkinWeights::NodeWeights* existing = aOwner.skinWeights().find(*field.node);
        const core::SkinWeights::BakedBone* baked = field.bakedFor(boneId);
        XC_ASSERT(baked);

        core::SkinWeights::NodeWeights merged = existing ? *existing : field;
        if (aReplace) {
            // The mesh may have changed since the last bake; a rebake is a new
            // bake, so its record describes the mesh it was evaluated on.
            merged.vertexCount = field.vertexCount;
            merged.meshSignature = field.meshSignature;
        }
        merged.setBakedBone(*baked);

        before.push_back(existing ? *existing : core::SkinWeights::NodeWeights());
        after.push_back(merged);
    }

    if (after.isEmpty()) {
        if (aError) {
            *aError = QStringLiteral("Every influenced layer is already baked; use Rebake to replace it.");
        }
        return false;
    }

    auto* command = new AssignSkinWeightsCommand(
        aProject, aOwner, before, after,
        aReplace ? CmndName::tr("Rebake bone weights") : CmndName::tr("Bake bone weights")
    );
    if (command->isUseless()) {
        delete command;
        if (aError) {
            *aError = QStringLiteral("The bake would not change any weight.");
        }
        return false;
    }
    aProject.commandStack().push(command);
    return true;
}

bool revertBoneWeights(
    core::Project& aProject, core::ObjectNode& aOwner, const core::Bone2& aBone, QString* aError
) {
    const int boneId = aBone.boneId();
    QVector<core::SkinWeights::NodeWeights> records = aOwner.skinWeights().nodes();
    QVector<core::SkinWeights::NodeWeights> before;
    QVector<core::SkinWeights::NodeWeights> after;

    for (const core::SkinWeights::NodeWeights& record : records) {
        if (!record.node || !record.isBaked(boneId)) {
            continue;
        }
        core::SkinWeights::NodeWeights next = record;
        next.removeBakedBone(boneId);
        before.push_back(record);
        // No other bone and no field left: the record itself is the state to
        // remove, so the next save does not carry an empty shell.
        after.push_back(next.bakedBones.isEmpty() ? core::SkinWeights::NodeWeights() : next);
    }

    if (after.isEmpty()) {
        if (aError) {
            *aError = QStringLiteral("This bone is not baked.");
        }
        return false;
    }

    aProject.commandStack().push(new AssignSkinWeightsCommand(
        aProject, aOwner, before, after, CmndName::tr("Revert bone weights")
    ));
    return true;
}

bool clearBoneWeights(
    core::Project& aProject, core::ObjectNode& aOwner, const core::Bone2& aBone, QString* aError
) {
    const int boneId = aBone.boneId();
    const QVector<core::SkinWeights::NodeWeights> records = aOwner.skinWeights().nodes();
    QVector<core::SkinWeights::NodeWeights> before;
    QVector<core::SkinWeights::NodeWeights> after;

    for (const core::SkinWeights::NodeWeights& record : records) {
        if (!record.node) {
            continue;
        }
        const core::SkinWeights::BakedBone* baked = record.bakedFor(boneId);
        // The record's presence IS the state: clearing is only meaningful while
        // the bone stays Baked, so an absent record is skipped rather than
        // turned into an empty one (that would be a bake, not a clear).
        if (!baked) {
            continue;
        }
        before.push_back(record);
        core::SkinWeights::NodeWeights next = record;
        next.setBakedBone(boneId, QVector<core::SkinWeights::Entry>());
        after.push_back(next);
    }

    if (after.isEmpty()) {
        if (aError) {
            *aError = QStringLiteral("This bone has no baked weights to clear.");
        }
        return false;
    }

    auto* command = new AssignSkinWeightsCommand(
        aProject, aOwner, before, after, CmndName::tr("Clear bone weights")
    );
    if (command->isUseless()) {
        delete command;
        if (aError) {
            *aError = QStringLiteral("The baked field is already empty.");
        }
        return false;
    }
    aProject.commandStack().push(command);
    return true;
}

} // namespace bone
} // namespace ctrl
