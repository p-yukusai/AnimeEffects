#ifndef CTRL_BONE_PAINTINFLMODE_H
#define CTRL_BONE_PAINTINFLMODE_H

#include "util/Circle.h"
#include "ctrl/Brush.h"
#include "core/Project.h"
#include "core/ObjectNode.h"
#include "core/BoneKey.h"
#include "ctrl/bone/bone_IMode.h"
#include "ctrl/bone/bone_KeyOwner.h"
#include "ctrl/bone/bone_Target.h"
#include "ctrl/bone/bone_AssignInfluence.h"

namespace ctrl {
namespace bone {

    class PaintInflMode: public IMode {
    public:
        PaintInflMode(core::Project& aProject, const Target& aTarget, KeyOwner& aKey);

        virtual void updateParam(const BoneParam&);
        virtual bool updateCursor(const core::CameraInfo&, const core::AbstractCursor&);
        virtual void renderQt(const core::RenderInfo& aInfo, QPainter& aPainter);

    private:
        // one shared-brush stamp: the falloff, hardness and amount come from
        // ctrl/Brush.h like every other painting tool
        bool updatePaint(const BrushStamp& aStamp);
        void assignInfluence(
            core::Bone2& aTarget, const std::array<QVector2D, 2>& aPrev, const std::array<QVector2D, 2>& aNext
        );
        void notifyAssign();

        core::Project& mProject;
        core::ObjectNode& mTarget;
        KeyOwner& mKeyOwner;
        QMatrix4x4 mTargetMtx;
        QMatrix4x4 mTargetInvMtx;
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
