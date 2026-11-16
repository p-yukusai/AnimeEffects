#include "cmnd/BasicCommands.h"
#include "core/TimeKeyExpans.h"
#include "core/ObjectNodeUtil.h"
#include "ctrl/srt/srt_KeyOwner.h"

using namespace core;

namespace ctrl {
namespace srt {

    KeyOwner::KeyOwner():
        moveKey(),
        rotateKey(),
        scaleKey(),
        ownsMoveKey(),
        ownsRotateKey(),
        ownsScaleKey(),
        rotateIsTrack(),
        rotateTrackProxy(),
        scaleIsTrack(),
        scaleTrackProxy(),
        moveIsTrack(),
        moveTrackProxy(),
        moveIsBaseValue(),
        rotateIsBaseValue(),
        scaleIsBaseValue(),
        parentMtx(),
        invParentMtx(),
        invParentSRMtx(),
        locSRMtx(),
        locSRTMtx(),
        locCSRTMtx(),
        hasInv() {}

    KeyOwner::~KeyOwner() { deleteOwningKeys(); }

    void KeyOwner::pushOwningMoveKey(cmnd::Stack& aStack, core::TimeLine& aLine, int aFrame) {
        if (!ownsMoveKey) {
            return;
        }
        if (moveIsTrack) {
            const QVector2D pos = moveKey->pos();
            const QVector2D centroid = moveKey->centroid();
            const float values[4] = { pos.x(), pos.y(), centroid.x(), centroid.y() };
            for (int component = 0; component < 4; ++component) {
                core::ChannelTrack* track = aLine.channelTrack(TimeKeyType_Move, component);
                if (!track) {
                    continue;
                }
                auto* key = new core::ChannelKey(TimeKeyType_Move, component);
                key->setFrame(aFrame);
                key->setValue(track->clamp(track->quantize(values[component])));
                const core::ChannelKey* before = track->keyBeforeOrAt(aFrame - 1);
                key->easing() = before ? before->easing() : util::Easing::defaultParam();
                aStack.push(new cmnd::GrabNewObject<core::ChannelKey>(key));
                aStack.push(aLine.createChannelPusher(TimeKeyType_Move, component, aFrame, key));
            }
        } else {
            aStack.push(new cmnd::GrabNewObject<MoveKey>(moveKey));
            aStack.push(aLine.createPusher(TimeKeyType_Move, aFrame, moveKey));
        }
        ownsMoveKey = false;
    }

    void KeyOwner::pushOwningRotateKey(cmnd::Stack& aStack, core::TimeLine& aLine, int aFrame) {
        if (!ownsRotateKey) {
            return;
        }
        if (rotateIsTrack) {
            core::ChannelTrack* track = aLine.channelTrack(TimeKeyType_Rotate, 0);
            if (!track) {
                ownsRotateKey = false;
                return;
            }
            auto* key = new core::ChannelKey(TimeKeyType_Rotate, 0);
            key->setFrame(aFrame);
            key->setValue(track->clamp(track->quantize(rotateKey->rotate())));
            const core::ChannelKey* before = track->keyBeforeOrAt(aFrame - 1);
            key->easing() = before ? before->easing() : util::Easing::defaultParam();
            aStack.push(new cmnd::GrabNewObject<core::ChannelKey>(key));
            aStack.push(aLine.createChannelPusher(TimeKeyType_Rotate, 0, aFrame, key));
        } else {
            aStack.push(new cmnd::GrabNewObject<RotateKey>(rotateKey));
            aStack.push(aLine.createPusher(TimeKeyType_Rotate, aFrame, rotateKey));
        }
        ownsRotateKey = false;
    }

    void KeyOwner::pushOwningScaleKey(cmnd::Stack& aStack, core::TimeLine& aLine, int aFrame) {
        if (!ownsScaleKey) {
            return;
        }
        if (scaleIsTrack) {
            for (int component = 0; component < 2; ++component) {
                core::ChannelTrack* track = aLine.channelTrack(TimeKeyType_Scale, component);
                if (!track) {
                    continue;
                }
                auto* key = new core::ChannelKey(TimeKeyType_Scale, component);
                key->setFrame(aFrame);
                key->setValue(track->clamp(track->quantize(
                    component == 0 ? scaleKey->scale().x() : scaleKey->scale().y()
                )));
                const core::ChannelKey* before = track->keyBeforeOrAt(aFrame - 1);
                key->easing() = before ? before->easing() : util::Easing::defaultParam();
                aStack.push(new cmnd::GrabNewObject<core::ChannelKey>(key));
                aStack.push(aLine.createChannelPusher(TimeKeyType_Scale, component, aFrame, key));
            }
        } else {
            aStack.push(new cmnd::GrabNewObject<ScaleKey>(scaleKey));
            aStack.push(aLine.createPusher(TimeKeyType_Scale, aFrame, scaleKey));
        }
        ownsScaleKey = false;
    }

    void KeyOwner::deleteOwningKeys() {
        if (moveKey && (ownsMoveKey || moveTrackProxy))
            delete moveKey;
        ownsMoveKey = false;
        moveIsTrack = false;
        moveTrackProxy = false;
        moveIsBaseValue = false;
        moveKey = nullptr;

        if (rotateKey && (ownsRotateKey || rotateTrackProxy))
            delete rotateKey;
        ownsRotateKey = false;
        rotateIsTrack = false;
        rotateTrackProxy = false;
        rotateIsBaseValue = false;
        rotateKey = nullptr;

        if (scaleKey && (ownsScaleKey || scaleTrackProxy))
            delete scaleKey;
        ownsScaleKey = false;
        scaleIsTrack = false;
        scaleTrackProxy = false;
        scaleIsBaseValue = false;
        scaleKey = nullptr;
    }

    bool KeyOwner::updatePosture(const TimeKeyExpans& aExpans) {
        XC_PTR_ASSERT(moveKey);
        XC_PTR_ASSERT(rotateKey);
        XC_PTR_ASSERT(scaleKey);

        if (ownsMoveKey && !moveIsTrack) {
            moveKey->setPos(aExpans.srt().pos());
            moveKey->setCentroid(aExpans.srt().centroid());
        }
        if (ownsRotateKey && !rotateIsTrack) {
            rotateKey->setRotate(aExpans.srt().rotate());
        }
        if (ownsScaleKey && !scaleIsTrack) {
            scaleKey->setScale(aExpans.srt().scale());
        }

        parentMtx = aExpans.srt().parentMatrix();
        invParentMtx = parentMtx.inverted(&hasInv);

        if (!hasInv) {
            return false;
        }

        invParentSRMtx = invParentMtx;
        invParentSRMtx.setColumn(3, QVector4D(0.0f, 0.0f, 0.0f, 1.0f));

        locSRMtx = aExpans.srt().localSRMatrix();
        locSRTMtx = aExpans.srt().localSRTMatrix();
        locCSRTMtx = aExpans.srt().localCSRTMatrix();

        return true;
    }

    QMatrix4x4 KeyOwner::getLocalSRTMatrixFromKeys() const {
        if (!static_cast<bool>(*this))
            return {};

        SRTExpans expans;
        expans.setPos(moveKey->pos());
        expans.setRotate(rotateKey->rotate());
        expans.setScale(scaleKey->scale());
        return expans.localSRTMatrix();
    }

    QMatrix4x4 KeyOwner::getLocalSRMatrixFromKeys() const {
        if (!static_cast<bool>(*this))
            return {};

        SRTExpans expans;
        expans.setRotate(rotateKey->rotate());
        expans.setScale(scaleKey->scale());
        return expans.localSRMatrix();
    }

} // namespace srt
} // namespace ctrl
