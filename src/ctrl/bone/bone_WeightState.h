#ifndef CTRL_BONE_WEIGHTSTATE_H
#define CTRL_BONE_WEIGHTSTATE_H

#include <QString>

namespace core {
class Bone2;
class BoneKey;
class ObjectNode;
class Project;
} // namespace core

namespace ctrl {
namespace bone {

// The Envelope/Baked state of ONE selected bone, as the PaintWeights panel
// needs it. The state is per bone and aggregates over the nodes the key skins:
//
//   Envelope - no influenced node has a baked record for the bone (the live
//              BoneShape still defines it);
//   Baked    - every influenced node with a mesh at the key's frame has one;
//   Mixed    - some do, some do not (a layer added after the bake, a node whose
//              mesh is not at this frame, a dormant record). The brush paints
//              the baked nodes and the panel says how many are still live.
//
// The numbers are target counts and vertex counts, not guesses: they come from
// the same collectInfluenceTargets set the bake and the build use.
struct WeightState {
    enum Kind {
        // No document BoneKey at this frame (the editor only owns a transient
        // key), or the mode is not active. The panel says why.
        Kind_None,
        // The rig exists, no bone is targeted yet.
        Kind_SelectBone,
        // A bone is targeted but nothing it skins has a mesh at this frame.
        Kind_NoTargets,
        Kind_Envelope,
        Kind_Baked,
        Kind_Mixed,
    };

    Kind kind = Kind_None;
    // The selected bone's pre-order index in the key (the only identity a bone
    // has: the model has no names). -1 when nothing is selected.
    int boneIndex = -1;
    int bakedNodes = 0;
    int liveNodes = 0;
    // The selected bone's authored entry count over the baked nodes.
    int bakedVertices = 0;
    // A baked record whose stored mesh signature differs from the mesh at this
    // frame. The field is kept (never shifted silently); the panel warns.
    bool meshMismatch = false;
    // The last action's refusal, shown under the state until the selection or a
    // later action changes it. Empty when the last action succeeded (or none ran).
    QString error;

    bool canPaint() const { return kind == Kind_Baked || kind == Kind_Mixed; }
    // Bake writes the LIVE nodes only: an already-baked node keeps its manual
    // paint unless the user explicitly Rebakes. That is what makes a layered rig
    // workable - baking a newly added layer must not drop the painted rest.
    bool canBake() const { return (kind == Kind_Envelope || kind == Kind_Mixed) && liveNodes > 0; }
    bool canRebake() const { return kind == Kind_Baked || kind == Kind_Mixed; }
    bool canRevert() const { return kind == Kind_Baked || kind == Kind_Mixed; }
    bool canClear() const { return bakedVertices > 0; }
};

// Evaluate the state of aBone (may be null) for aKey at its own frame. The
// caller must have a document key; an editor-owned transient key has no
// influenced cache set and answers Kind_None.
WeightState computeWeightState(
    core::Project& aProject, core::ObjectNode& aOwner, core::BoneKey& aKey, const core::Bone2* aBone
);

} // namespace bone
} // namespace ctrl

#endif // CTRL_BONE_WEIGHTSTATE_H
