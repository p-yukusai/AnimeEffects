#include "ctrl/bone/bone_WeightState.h"

#include "core/Bone2.h"
#include "core/BoneKey.h"
#include "core/LayerMesh.h"
#include "core/ObjectNode.h"
#include "core/Project.h"
#include "core/SkinWeights.h"
#include "core/TimeKeyBlender.h"

namespace ctrl {
namespace bone {

WeightState computeWeightState(
    core::Project& aProject, core::ObjectNode& aOwner, core::BoneKey& aKey, const core::Bone2* aBone
) {
    WeightState state;
    if (!aBone) {
        state.kind = aOwner.timeLine() ? WeightState::Kind_SelectBone : WeightState::Kind_None;
        return state;
    }
    if (!aOwner.timeLine()) {
        return state;
    }

    state.boneIndex = aKey.data().boneIndex(*aBone);
    const int boneId = aBone->boneId();

    auto time = aProject.currentTimeInfo();
    time.frame.set(aKey.frame());

    QVector<core::ObjectNode*> targets;
    aKey.collectInfluenceTargets(time, aOwner, targets);

    for (core::ObjectNode* node : targets) {
        if (!node) {
            continue;
        }
        const core::LayerMesh* mesh = core::TimeKeyBlender::getAreaMesh(*node, time);
        if (!mesh || mesh->vertexCount() <= 0) {
            // No mesh at this frame: the bake cannot write this node, and the
            // build has no map for it either. It is neither live nor baked.
            continue;
        }

        const core::SkinWeights::NodeWeights* record = aOwner.skinWeights().find(*node);
        const core::SkinWeights::BakedBone* baked = record ? record->bakedFor(boneId) : nullptr;
        if (baked) {
            ++state.bakedNodes;
            state.bakedVertices += int(baked->entries.size());
            if (record->meshSignature != core::SkinWeights::meshSignature(*mesh)) {
                state.meshMismatch = true;
            }
        } else {
            ++state.liveNodes;
        }
    }

    if (state.bakedNodes + state.liveNodes == 0) {
        state.kind = WeightState::Kind_NoTargets;
    } else if (state.bakedNodes == 0) {
        state.kind = WeightState::Kind_Envelope;
    } else if (state.liveNodes == 0) {
        state.kind = WeightState::Kind_Baked;
    } else {
        state.kind = WeightState::Kind_Mixed;
    }
    return state;
}

} // namespace bone
} // namespace ctrl
