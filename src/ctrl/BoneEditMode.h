#ifndef CTRL_BONEEDITMODE
#define CTRL_BONEEDITMODE

namespace ctrl {

// The historical Influence* names below describe the procedural BoneShape; the UI
// calls it the ENVELOPE. The envelope has ONE brush: Size/Hardness are its footprint,
// Flow its rate, and BoneParam::envelopeValue the range it paints toward (below it
// shrinks, above it grows), so EraseInfl is retired from the UI while its enum value is
// kept for the persisted mode index. The baked per-vertex weights are influence too.
// Do not renumber or rename these values.
enum BoneEditMode {
    BoneEditMode_Create,
    BoneEditMode_Delete,
    BoneEditMode_MoveJoint,
    BoneEditMode_BindNodes,
    BoneEditMode_Influence,
    BoneEditMode_PaintInfl,
    BoneEditMode_EraseInfl,
    // Authored per-vertex weights: bake the selected bone's envelope, then paint
    // the baked field. Appended rather than inserted so the existing mode indices
    // (persisted in the dock's QSettings and pinned by tests) do not move.
    BoneEditMode_PaintWeights,
    BoneEditMode_TERM
};

} // namespace ctrl

#endif // CTRL_BONEEDITMODE
