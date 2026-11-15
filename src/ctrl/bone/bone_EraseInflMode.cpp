#include "util/CollDetect.h"
#include "cmnd/ScopedMacro.h"
#include "ctrl/CmndName.h"
#include "ctrl/bone/bone_EraseInflMode.h"
#include "ctrl/bone/bone_Renderer.h"
#include "ctrl/bone/bone_Notifier.h"

using namespace core;

namespace ctrl {
namespace bone {

    EraseInflMode::EraseInflMode(Project& aProject, const Target& aTarget, KeyOwner& aKey):
        mProject(aProject),
        mTarget(*aTarget.node),
        mKeyOwner(aKey),
        mTargetMtx(aTarget.mtx),
        mTargetInvMtx(aTarget.invMtx),
        mCommandRef(),
        mBrush(),
        mParam(),
        mStroke(),
        mCursorPos(),
        mIsBrushDrawing() {
        XC_PTR_ASSERT(mKeyOwner.key);
    }

    void EraseInflMode::updateParam(const BoneParam& aParam) {
        // one brush for both influence modes: erase is the same stamp deposited
        // the other way, so the size and hardness follow the tool mode switch
        mParam = aParam;
        mParam.brush.erase = true;
        mBrush.setRadius(aParam.brush.radius);
    }

    bool EraseInflMode::updateCursor(const CameraInfo&, const AbstractCursor& aCursor) {
        mCursorPos = aCursor.worldPos();
        mBrush.setCenter(mCursorPos);
        mBrush.setRadius(mParam.brush.radius);

        if (aCursor.emitsLeftPressedEvent()) {
            mIsBrushDrawing = true;
            mCommandRef = nullptr;
            mStroke.begin(mParam.brush, mCursorPos, aCursor.pressure(), [this](const BrushStamp& aStamp) {
                updatePaint(aStamp);
            });
        } else if (aCursor.emitsLeftDraggedEvent()) {
            mStroke.move(mCursorPos, aCursor.pressure());
        } else if (aCursor.emitsLeftReleasedEvent()) {
            mStroke.end();
            mIsBrushDrawing = false;
            mCommandRef = nullptr;
        }

        return true;
    }

    bool EraseInflMode::updatePaint(const BrushStamp& aStamp) {
        bool modified = false;

        // getSink() reads the circle: keep it at the stamp so the geometry and the
        // falloff describe the same brush
        mBrush.setCenter(aStamp.center);
        mBrush.setRadius(aStamp.radius);

        auto& topBones = mKeyOwner.key->data().topBones();
        for (auto topBone : topBones) {
            Bone2::Iterator itr(topBone);
            while (itr.hasNext()) {
                auto bone = itr.next();

                auto sinkRes = getSink(*bone, mBrush);
                auto sink = sinkRes.first;
                auto ratio = sinkRes.second;

                if (sink <= 0.0f)
                    continue;

                std::array<QVector2D, 2> prevRange = {bone->range(0), bone->range(1)};
                std::array<QVector2D, 2> nextRange = prevRange;
                for (int i = 0; i < 2; ++i) {
                    // the shared falloff: sink is how far the brush reaches past the
                    // bone's own range, the falloff says how much of that it deposits
                    const float weight = aStamp.amount * brushFalloff(sink, aStamp.radius, aStamp.hardness);
                    auto sub = weight * sink * (i == 0 ? (1.0f - ratio) : ratio);
                    nextRange[i].setX(std::max(nextRange[i].x() - sub, 0.0f));
                    if (nextRange[i].y() > nextRange[i].x()) {
                        nextRange[i].setY(nextRange[i].x());
                    }
                }
                assignInfluence(*bone, prevRange, nextRange);
                modified = true;
            }
        }

        if (modified) {
            notifyAssign();
        }

        return modified;
    }

    void EraseInflMode::renderQt(const RenderInfo& aInfo, QPainter& aPainter) {
        bone::Renderer renderer(aPainter, aInfo);
        renderer.setAntialiasing(true);
        renderer.setFocusConnector(true);
        renderer.setTargetMatrix(mTargetMtx);

        for (auto bone : mKeyOwner.key->data().topBones()) {
            renderer.renderInfluence(bone);
        }

        for (auto bone : mKeyOwner.key->data().topBones()) {
            renderer.renderBones(bone);
        }

        renderer.renderBrush(mBrush, mIsBrushDrawing, mParam.brush.hardness);
    }

    std::pair<float, float> EraseInflMode::getSink(const core::Bone2& aBone, const util::Circle& aBrush) const {
        std::pair<float, float> result(0.0f, 0.0f);

        auto parent = aBone.parent();
        if (!parent)
            return result;

        const QVector2D brushPos = (mTargetInvMtx.map(QVector3D(aBrush.center()))).toVector2D();
        const float brushRange = (mTargetInvMtx.map(QVector3D(mBrush.radius(), 0.0f, 0.0f)) - mTargetInvMtx.map(QVector3D()))
                                     .toVector2D()
                                     .length();

        const QVector2D ppos = parent->worldPos();
        const QVector2D cpos = aBone.worldPos();
        const util::Segment2D boneSeg(ppos, cpos - ppos);

        const float ratio = util::CollDetect::getRawSegmentRate(boneSeg, brushPos);
        auto linearRange = aBone.blendedRange(ratio);


        if (ratio < 0.0f) {
            auto len = (brushPos - ppos).length() - brushRange;
            if (len < linearRange.x()) {
                result.first = linearRange.x() - len;
                result.second = 0.0f;
            }
        } else if (ratio < 1.0f) {
            auto v = brushPos - (boneSeg.start + boneSeg.dir * ratio);
            auto len = v.length() - brushRange;

            if (len < linearRange.x()) {
                result.first = linearRange.x() - len;
                result.second = ratio;
            }
        } else {
            auto len = (brushPos - cpos).length() - brushRange;
            if (len < linearRange.x()) {
                result.first = linearRange.x() - len;
                result.second = 1.0f;
            }
        }

        return result;
    }

    void EraseInflMode::assignInfluence(
        core::Bone2& aTarget, const std::array<QVector2D, 2>& aPrev, const std::array<QVector2D, 2>& aNext
    ) {
        XC_ASSERT(!mKeyOwner.owns());
        auto eventType = TimeLineEvent::Type_ChangeKeyValue;
        cmnd::Stack& stack = mProject.commandStack();

        // modify
        if (mCommandRef && stack.isModifiable(mCommandRef)) {
            mCommandRef->push(aTarget, aPrev, aNext);
        } else {
            cmnd::ScopedMacro macro(stack, CmndName::tr("Update bone influence"));
            macro.grabListener(new Notifier(mProject, mTarget, *mKeyOwner.key, eventType));

            // push command
            mCommandRef = new FuzzyAssignInfluence();
            mCommandRef->push(aTarget, aPrev, aNext);
            stack.push(mCommandRef);
        }
    }

    void EraseInflMode::notifyAssign() {
        // singleshot notify
        auto eventType = TimeLineEvent::Type_ChangeKeyValue;
        Notifier notifier(mProject, mTarget, *mKeyOwner.key, eventType);
        notifier.notify();
    }

} // namespace bone
} // namespace ctrl
