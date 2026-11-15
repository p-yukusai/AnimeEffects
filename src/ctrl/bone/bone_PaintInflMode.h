#ifndef CTRL_BONE_PAINTINFLMODE_H
#define CTRL_BONE_PAINTINFLMODE_H

#include "util/Circle.h"
#include "ctrl/Brush.h"
#include "core/Project.h"
#include "core/ObjectNode.h"
#include "core/BoneKey.h"
#include "ctrl/bone/bone_Focuser.h"
#include "ctrl/bone/bone_IMode.h"
#include "ctrl/bone/bone_KeyOwner.h"
#include "ctrl/bone/bone_Target.h"
#include "ctrl/bone/bone_AssignInfluence.h"

namespace ctrl {
namespace bone {

    // Paints ONE bone's envelope. The earlier version widened every bone whose
    // envelope passed under the brush, so a stroke near an overlapping joint
    // silently changed its neighbours; the standard weight-painting workflow
    // edits the bone you chose and nothing else. The target is chosen by
    // hovering a joint and clicking it with the RIGHT button (left is the
    // stroke), or - when the first stroke starts on the mesh instead - by the
    // nearest bone under the brush. The selected bone is drawn selected and its
    // normalised weight field is the heatmap.
    class PaintInflMode: public IMode {
    public:
        PaintInflMode(core::Project& aProject, const Target& aTarget, KeyOwner& aKey);

        virtual void updateParam(const BoneParam&);
        virtual bool updateCursor(const core::CameraInfo&, const core::AbstractCursor&);
        virtual void renderQt(const core::RenderInfo& aInfo, QPainter& aPainter);

    private:
        // One shared-brush stamp: SIZE/HARDNESS are only the footprint, FLOW is
        // the per-stamp rate, and the envelope range is moved toward the explicit
        // BoneParam::envelopeValue (below it shrinks, above it grows).
        bool updatePaint(const BrushStamp& aStamp);
        void assignInfluence(
            core::Bone2& aTarget, const std::array<QVector2D, 2>& aPrev, const std::array<QVector2D, 2>& aNext
        );
        void notifyAssign();
        void selectBone(core::Bone2* aBone);
        QVector2D targetPos(const core::AbstractCursor& aCursor) const;

        core::Project& mProject;
        core::ObjectNode& mTarget;
        KeyOwner& mKeyOwner;
        QMatrix4x4 mTargetMtx;
        QMatrix4x4 mTargetInvMtx;
        Focuser mFocuser;
        core::Bone2* mActiveBone;
        core::Bone2* mHoverBone;
        FuzzyAssignInfluence* mCommandRef;

        util::Circle mBrush;      // the cursor circle (size) the renderer draws
        BoneParam mParam;
        BrushStroke mStroke;
        QVector2D mCursorPos;
        bool mIsBrushDrawing;
    };

} // namespace bone
} // namespace ctrl

#endif // CTRL_BONE_PAINTINFLMODE_H
