#ifndef CTRL_BONE_INFLUENCEMODE_H
#define CTRL_BONE_INFLUENCEMODE_H

#include "core/Project.h"
#include "core/ObjectNode.h"
#include "core/BoneKey.h"
#include "ctrl/bone/bone_IMode.h"
#include "ctrl/bone/bone_KeyOwner.h"
#include "ctrl/bone/bone_Target.h"
#include "ctrl/bone/bone_Focuser.h"
#include "ctrl/bone/bone_AssignInfluence.h"

namespace ctrl {
namespace bone {

    class InfluenceMode: public IMode {
    public:
        InfluenceMode(core::Project& aProject, const Target& aTarget, KeyOwner& aKey);

        virtual void updateParam(const BoneParam&);
        virtual bool updateCursor(const core::CameraInfo&, const core::AbstractCursor&);
        virtual void renderQt(const core::RenderInfo& aInfo, QPainter& aPainter);

    private:
        void assignInfluence(core::Bone2& aTarget, int aIndex, const QVector2D& aRange);

        core::Project& mProject;
        core::ObjectNode& mTarget;
        KeyOwner& mKeyOwner;
        QMatrix4x4 mTargetMtx;
        QMatrix4x4 mTargetInvMtx;
        Focuser mFocuser;
        core::Bone2* mHoverBone;
        BoneParam mParam;

        QVector2D mFocusHAxis;
        QVector2D mFocusVAxis;
        bool mFocusChild;
        AssignInfluence* mCommandRef;
    };

} // namespace bone
} // namespace ctrl

#endif // CTRL_BONE_INFLUENCEMODE_H
