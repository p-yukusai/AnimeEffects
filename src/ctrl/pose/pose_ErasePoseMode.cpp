#include "util/CollDetect.h"
#include "util/MathUtil.h"
#include "cmnd/ScopedMacro.h"
#include "core/Constant.h"
#include "ctrl/TimeLineUtil.h"
#include "ctrl/CmndName.h"
#include "ctrl/pose/pose_ErasePoseMode.h"
#include "ctrl/bone/bone_Renderer.h"

using namespace core;

namespace ctrl {
namespace pose {

    ErasePoseMode::ErasePoseMode(Project& aProject, const Target& aTarget, KeyOwner& aKey):
        mProject(aProject),
        mTarget(*aTarget.node),
        mTargetMtx(aTarget.mtx),
        mTargetInvMtx(aTarget.invMtx),
        mKeyOwner(aKey),
        mGesture(mProject.commandStack()),
        mBrush(),
        mParam(),
        mStroke(),
        mCursorPos(),
        mIsBrushDrawing() {
        XC_PTR_ASSERT(mKeyOwner.key);
    }

    void ErasePoseMode::updateParam(const PoseParam& aParam) {
        // the shared influence brush (ctrl/Brush.h): a pose erase is a stamp like
        // any other, with the pen's pressure and the brush hardness
        mParam = aParam;
        mParam.brush.erase = true;
        mBrush.setRadius(aParam.brush.radius);
    }

    bool ErasePoseMode::updateCursor(const CameraInfo&, const AbstractCursor& aCursor) {
        mCursorPos = aCursor.worldPos();
        mBrush.setCenter(mCursorPos);
        mBrush.setRadius(mParam.brush.radius);

        if (aCursor.emitsLeftPressedEvent()) {
            mIsBrushDrawing = true;
            mGesture.end();
            mStroke.begin(mParam.brush, mCursorPos, aCursor.pressure(), [this](const BrushStamp& aStamp) {
                updatePaint(aStamp);
            });
        } else if (aCursor.emitsLeftDraggedEvent()) {
            mStroke.move(mCursorPos, aCursor.pressure());
        } else if (aCursor.emitsLeftReleasedEvent()) {
            mStroke.end();
            mIsBrushDrawing = false;
            mGesture.end();
        }

        return true;
    }

    void ErasePoseMode::updatePaint(const BrushStamp& aStamp) {
        cmnd::Stack& stack = mProject.commandStack();
        TimeLine& timeLine = *mTarget.timeLine();
        const int frame = mProject.animator().currentFrame().get();
        auto& topBones = mKeyOwner.key->data().topBones();

        QVector<float> nextRots;
        {
            for (auto topBone : topBones) {
                Bone2::ConstIterator itr(topBone);
                while (itr.hasNext()) {
                    auto ptr = itr.next();
                    auto worldPos = mTargetMtx.map(QVector3D(ptr->worldPos()));
                    // the shared falloff at this stamp: the core erases hardest and
                    // the band fades out, instead of a hard disc with one rate
                    const float distance = (worldPos.toVector2D() - aStamp.center).length();
                    const float weight =
                        aStamp.amount * brushFalloff(distance, aStamp.radius, aStamp.hardness);
                    nextRots.push_back(ptr->rotate() * (1.0f - weight));
                }
            }
        }

        // modify
        if (mGesture.continues()) {
            mGesture.command()->modifyValue(nextRots);

            // notify
            TimeLineEvent event;
            event.setType(TimeLineEvent::Type_ChangeKeyValue);
            event.pushTarget(mTarget, TimeKeyType_Pose, frame);
            mProject.onTimeLineModified(event, false);
        } else {
            mGesture.begin(CmndName::tr("erase a pose"), [&](cmnd::ScopedMacro& macro) {
                // set notifier
                {
                    auto notifier = new TimeLineUtil::Notifier(mProject);
                    notifier->event().setType(
                        mKeyOwner.owns() ? TimeLineEvent::Type_PushKey : TimeLineEvent::Type_ChangeKeyValue
                    );
                    notifier->event().pushTarget(mTarget, TimeKeyType_Pose, frame);
                    macro.grabListener(notifier);
                }
                // push key command
                if (mKeyOwner.owns()) {
                    mKeyOwner.pushOwnsKey(stack, timeLine, frame);
                }

                // push command
                return new RotateAllBones(topBones, nextRots);
            });
        }
    }

    void ErasePoseMode::renderQt(const RenderInfo& aInfo, QPainter& aPainter) {
        bone::Renderer renderer(aPainter, aInfo);
        renderer.setAntialiasing(true);
        // renderer.setFocusConnector(true);
        renderer.setTargetMatrix(mTargetMtx);

        for (auto bone : mKeyOwner.key->data().topBones()) {
            renderer.renderBones(bone);
        }

        renderer.renderBrush(mBrush, mIsBrushDrawing, mParam.brush.hardness);
    }

} // namespace pose
} // namespace ctrl
