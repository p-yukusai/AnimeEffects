#include "util/MathUtil.h"
#include "cmnd/ScopedMacro.h"
#include "core/Constant.h"
#include "core/TimeKeyBlender.h"
#include "ctrl/TimeLineUtil.h"
#include "ctrl/CmndName.h"
#include "ctrl/srt/srt_MoveMode.h"

using namespace core;

namespace ctrl::srt {

    MoveMode::MoveMode(Project& aProject, ObjectNode& aTarget, KeyOwner& aKeyOwner):
        mProject(aProject),
        mTarget(aTarget),
        mKeyOwner(aKeyOwner),
        mSRTParam(),
        mSymbol(),
        mFocus(FocusType_TERM, QVector2D()),
        mAssignMoveRef(),
        mAssignRotateRef(),
        mAssignScaleRef(),
        mTrackRotateRef(),
        mTrackRotateBaseRef(),
        mTrackScaleRef(),
        mTrackMoveRef(),
        mSuspend(),
        mBaseVec(),
        mBaseValue() {
        XC_PTR_ASSERT(mTarget.timeLine());
    }

    void MoveMode::updateParam(const SRTParam& aParam) { mSRTParam = aParam; }

    bool MoveMode::updateCursor(const CameraInfo& aCamera, const AbstractCursor& aCursor) {
        auto keyLocalMtx = mKeyOwner.getLocalSRTMatrixFromKeys();
        auto& keyParentMtx = mKeyOwner.parentMtx;

        bool mod = false;

        // update symbol
        mSymbol.build(keyLocalMtx, keyParentMtx, aCamera);

        if (aCursor.emitsLeftPressedEvent()) {
            mFocus = mSymbol.findFocus(aCursor.screenPos());
            auto focusVector = aCamera.toWorldVector(mFocus.second);
            if (mFocus.first != FocusType_TERM) {
                mSuspend.construct(mProject);
                mAssignMoveRef = nullptr;
                mAssignRotateRef = nullptr;
                mAssignScaleRef = nullptr;

                if (mFocus.first == FocusType_Scale || mFocus.first == FocusType_ScaleX ||
                    mFocus.first == FocusType_ScaleY) {
                    auto keyWorldPos = keyParentMtx.map(QVector3D(mKeyOwner.moveKey->pos()));
                    mBaseVec = mKeyOwner.scaleKey->scale();

                    const QVector3D pos = QVector3D(aCursor.worldPos());
                    const QVector2D vec = (pos - keyWorldPos).toVector2D();
                    const float length = QVector2D::dotProduct(vec, focusVector);
                    mBaseValue = std::max(Constant::normalizable(), length);
                } else if (mFocus.first == srt::FocusType_Rotate) {
                    mBaseVec = (mKeyOwner.invParentSRMtx.map(QVector3D(focusVector))).toVector2D();
                }
                mod = true;
            }
        } else if (aCursor.emitsLeftDraggedEvent()) {
            auto focusVector = aCamera.toWorldVector(mFocus.second);

            auto keyPos = mKeyOwner.moveKey->pos();
            auto keyWorldPos = keyParentMtx.map(QVector3D(keyPos));
            auto cursorWorldPos = QVector3D(aCursor.worldPos());
            auto cursorWorldVel = QVector3D(aCursor.worldVel());

            if (mFocus.first == srt::FocusType_Trans) {
                auto moveData = mKeyOwner.moveKey->data();
                moveData.addPos((mKeyOwner.invParentSRMtx.map(cursorWorldVel)).toVector2D());
                assignMoveKey(moveData);
            } else if (mFocus.first == srt::FocusType_Rotate) {
                auto rotData = mKeyOwner.rotateKey->data();
                auto vec = (mKeyOwner.invParentMtx.map(cursorWorldPos)).toVector2D() - keyPos;
                const float length = vec.length();
                if (length > 1.0f) {
                    auto rotate = util::MathUtil::getAngleDifferenceRad(mBaseVec, vec);
                    rotData.addRotate(rotate);
                    mBaseVec = vec.normalized();
                }
                assignRotateKey(rotData);
            } else if (mFocus.first == srt::FocusType_Scale) {
                auto scaleData = mKeyOwner.scaleKey->data();
                const QVector2D vec = (cursorWorldPos - keyWorldPos).toVector2D();
                const float length = QVector2D::dotProduct(vec, focusVector);
                scaleData.setScaleX(mBaseVec.x() * (length / mBaseValue));
                scaleData.setScaleY(mBaseVec.y() * (length / mBaseValue));
                assignScaleKey(scaleData);
            } else if (mFocus.first == srt::FocusType_ScaleX) {
                auto scaleData = mKeyOwner.scaleKey->data();
                const QVector2D vec = (cursorWorldPos - keyWorldPos).toVector2D();
                const float length = QVector2D::dotProduct(vec, focusVector);
                scaleData.setScaleX(mBaseVec.x() * (length / mBaseValue));
                assignScaleKey(scaleData);
            } else if (mFocus.first == srt::FocusType_ScaleY) {
                auto scaleData = mKeyOwner.scaleKey->data();
                const QVector2D vec = (cursorWorldPos - keyWorldPos).toVector2D();
                const float length = QVector2D::dotProduct(vec, focusVector);
                scaleData.setScaleY(mBaseVec.y() * (length / mBaseValue));
                assignScaleKey(scaleData);
            }

            if (mFocus.first != srt::FocusType_TERM) {
                mod = true;
            }
        } else if (aCursor.emitsLeftReleasedEvent()) {
            clearState();
            mod = true;
        } else {
            srt::FocusType prev = mFocus.first;
            mFocus = mSymbol.findFocus(aCursor.screenPos());
            mod = mFocus.first != prev;
        }

        return mod;
    }

    void MoveMode::renderQt(const core::RenderInfo& aInfo, QPainter& aPainter) {
        mSymbol.build(mKeyOwner.getLocalSRTMatrixFromKeys(), mKeyOwner.parentMtx, aInfo.camera);
        bool grayOut = false;
        {
            const auto& aLine = mTarget.timeLine();
            if ( aLine->current().bone().isUnderOfBinding() || aLine->working().bone().isUnderOfBinding()) {
                grayOut = true;
            }
        }
        mSymbol.draw(aInfo, aPainter, mFocus.first, grayOut);
    }

    void MoveMode::clearState() {
        mAssignMoveRef = nullptr;
        mAssignRotateRef = nullptr;
        mAssignScaleRef = nullptr;
        mTrackRotateRef = nullptr;
        mTrackRotateBaseRef = nullptr;
        mTrackScaleRef = nullptr;
        mTrackMoveRef = nullptr;
        mFocus.first = FocusType_TERM;
        mSuspend.destruct();
    }

    void MoveMode::setAssignNotifier(
        cmnd::ScopedMacro& aMacro, TimeKeyType aKeyType, bool aPushKey, bool aBaseValue
    ) {
        auto notifyType = aPushKey ? TimeLineEvent::Type_PushKey : TimeLineEvent::Type_ChangeKeyValue;
        const int frame = mProject.animator().currentFrame().get();

        auto notifier = new TimeLineUtil::Notifier(mProject);
        notifier->event().setType(notifyType);
        if (aBaseValue) {
            notifier->event().pushBaseValueTarget(mTarget, aKeyType);
        } else if (TimeLine::usesChannelTracks(aKeyType)) {
            for (int component = 0; component < core::propertyChannelCount(aKeyType); ++component) {
                notifier->event().pushTarget(
                    mTarget, TimeKeyPos(*mTarget.timeLine(), aKeyType, frame, component)
                );
            }
        } else {
            notifier->event().pushTarget(mTarget, aKeyType, frame);
        }
        aMacro.grabListener(notifier);
    }

    void MoveMode::notifyAssignModification(TimeKeyType aKeyType) {
        const int frame = mProject.animator().currentFrame().get();

        // singleshot notify
        TimeLineEvent event;
        event.setType(TimeLineEvent::Type_ChangeKeyValue);
        if (TimeLine::usesChannelTracks(aKeyType)) {
            for (int component = 0; component < core::propertyChannelCount(aKeyType); ++component) {
                event.pushTarget(mTarget, TimeKeyPos(*mTarget.timeLine(), aKeyType, frame, component));
            }
        } else {
            event.pushTarget(mTarget, aKeyType, frame);
        }
        mProject.onTimeLineModified(event, false);
    }

    void MoveMode::assignMoveKey(MoveKey::Data& aNewData) {
        if (mKeyOwner.moveIsTrack) {
            assignTrackMoveKey(aNewData);
            return;
        }

        const int frame = mProject.animator().currentFrame().get();
        TimeLine& timeLine = *mTarget.timeLine();
        cmnd::Stack& stack = mProject.commandStack();

        const bool appendKeys = (mSRTParam.necessarilyRotate && mKeyOwner.ownsRotateKey) ||
            (mSRTParam.necessarilyScale && mKeyOwner.ownsScaleKey);

        if (!appendKeys && mAssignMoveRef && mProject.commandStack().isModifiable(mAssignMoveRef)) {
            // modify command
            XC_ASSERT(!mKeyOwner.ownsMoveKey);
            mAssignMoveRef->modifyValue(aNewData);
            notifyAssignModification(TimeKeyType_Move);
        } else {
            cmnd::ScopedMacro macro(stack, CmndName::tr("update a moving key"));
            setAssignNotifier(
                macro, TimeKeyType_Move, mKeyOwner.ownsMoveKey || appendKeys, mKeyOwner.moveIsBaseValue
            );
            // push commands
            mKeyOwner.pushOwningMoveKey(stack, timeLine, frame);

            if (mSRTParam.necessarilyRotate) {
                mKeyOwner.pushOwningRotateKey(stack, timeLine, frame);
            }
            if (mSRTParam.necessarilyScale) {
                mKeyOwner.pushOwningScaleKey(stack, timeLine, frame);
            }

            // The command owns the KEY and its LINE, not the Data field: a
            // proportional companion's reflow needs the map the key lives in.
            // pushOwningMoveKey() guarantees the key here; a null is never a
            // dereference, whatever happens.
            mAssignMoveRef = nullptr;
            if (mKeyOwner.moveKey) {
                mAssignMoveRef = new AssignMoveCommand(*mKeyOwner.moveKey, timeLine, aNewData);
                stack.push(mAssignMoveRef);
            }
        }
    }

    void MoveMode::assignRotateKey(RotateKey::Data& aNewData) {
        if (mKeyOwner.rotateIsTrack) {
            assignTrackRotateKey(aNewData);
            return;
        }

        const int frame = mProject.animator().currentFrame().get();
        TimeLine& timeLine = *mTarget.timeLine();
        cmnd::Stack& stack = mProject.commandStack();

        const bool appendKeys = (mSRTParam.necessarilyMove && mKeyOwner.ownsMoveKey) ||
            (mSRTParam.necessarilyScale && mKeyOwner.ownsScaleKey);

        if (!appendKeys && mAssignRotateRef && mProject.commandStack().isModifiable(mAssignRotateRef)) {
            // modify command
            XC_ASSERT(!mKeyOwner.ownsRotateKey);
            mAssignRotateRef->modifyValue(aNewData);
            notifyAssignModification(TimeKeyType_Rotate);
        } else {
            cmnd::ScopedMacro macro(stack, CmndName::tr("update a rotation key"));
            setAssignNotifier(
                macro, TimeKeyType_Rotate, mKeyOwner.ownsRotateKey || appendKeys, mKeyOwner.rotateIsBaseValue
            );
            // push commands
            mKeyOwner.pushOwningRotateKey(stack, timeLine, frame);

            if (mSRTParam.necessarilyMove) {
                mKeyOwner.pushOwningMoveKey(stack, timeLine, frame);
            }
            if (mSRTParam.necessarilyScale) {
                mKeyOwner.pushOwningScaleKey(stack, timeLine, frame);
            }

            mAssignRotateRef = nullptr;
            if (mKeyOwner.rotateKey) {
                mAssignRotateRef = new AssignRotateCommand(*mKeyOwner.rotateKey, timeLine, aNewData);
                stack.push(mAssignRotateRef);
            }
        }
    }

    void MoveMode::assignTrackScaleKey(ScaleKey::Data& aNewData) {
        const int frame = mProject.animator().currentFrame().get();
        TimeLine& timeLine = *mTarget.timeLine();
        cmnd::Stack& stack = mProject.commandStack();
        ChannelTrack* xTrack = timeLine.channelTrack(TimeKeyType_Scale, 0);
        ChannelTrack* yTrack = timeLine.channelTrack(TimeKeyType_Scale, 1);
        if (!xTrack || !yTrack) {
            return;
        }
        const QVector2D value = aNewData.scale();

        const bool appendKeys = (mSRTParam.necessarilyMove && mKeyOwner.ownsMoveKey) ||
            (mSRTParam.necessarilyRotate && mKeyOwner.ownsRotateKey);

        if (mKeyOwner.scaleIsBaseValue) {
            if (mTrackScaleRef && stack.isModifiable(mTrackScaleRef)) {
                mTrackScaleRef->modifyValue(value);
                notifyAssignModification(TimeKeyType_Scale);
            } else {
                cmnd::ScopedMacro macro(stack, CmndName::tr("update a scale key"));
                setAssignNotifier(macro, TimeKeyType_Scale, false, true);
                mTrackScaleRef = new AssignScaleTrackCommand(*xTrack, nullptr, *yTrack, nullptr, value);
                stack.push(mTrackScaleRef);
            }
            return;
        }

        ChannelKey* xKey = timeLine.channelKey(TimeKeyType_Scale, 0, frame);
        ChannelKey* yKey = timeLine.channelKey(TimeKeyType_Scale, 1, frame);
        if (!appendKeys && mTrackScaleRef && stack.isModifiable(mTrackScaleRef) && xKey && yKey) {
            mTrackScaleRef->modifyValue(value);
            notifyAssignModification(TimeKeyType_Scale);
            return;
        }

        cmnd::ScopedMacro macro(stack, CmndName::tr("update a scale key"));
        setAssignNotifier(
            macro, TimeKeyType_Scale, mKeyOwner.ownsScaleKey || appendKeys, false
        );
        mKeyOwner.pushOwningScaleKey(stack, timeLine, frame);
        if (mSRTParam.necessarilyMove) {
            mKeyOwner.pushOwningMoveKey(stack, timeLine, frame);
        }
        if (mSRTParam.necessarilyRotate) {
            mKeyOwner.pushOwningRotateKey(stack, timeLine, frame);
        }

        xKey = timeLine.channelKey(TimeKeyType_Scale, 0, frame);
        yKey = timeLine.channelKey(TimeKeyType_Scale, 1, frame);
        mTrackScaleRef = nullptr;
        if (xKey && yKey) {
            mTrackScaleRef = new AssignScaleTrackCommand(*xTrack, xKey, *yTrack, yKey, value);
            stack.push(mTrackScaleRef);
        }
    }

    void MoveMode::assignTrackMoveKey(MoveKey::Data& aNewData) {
        const int frame = mProject.animator().currentFrame().get();
        TimeLine& timeLine = *mTarget.timeLine();
        cmnd::Stack& stack = mProject.commandStack();
        AssignMoveTrackCommand::Channels channels;
        for (int c = 0; c < 4; ++c) {
            channels.track[c] = timeLine.channelTrack(TimeKeyType_Move, c);
            if (!channels.track[c]) {
                return;
            }
        }
        const QVector2D pos = aNewData.pos();
        const QVector2D centroid = aNewData.centroid();

        const bool appendKeys = (mSRTParam.necessarilyRotate && mKeyOwner.ownsRotateKey) ||
            (mSRTParam.necessarilyScale && mKeyOwner.ownsScaleKey);

        if (mKeyOwner.moveIsBaseValue) {
            if (mTrackMoveRef && stack.isModifiable(mTrackMoveRef)) {
                mTrackMoveRef->modifyValue(pos, centroid);
                notifyAssignModification(TimeKeyType_Move);
            } else {
                cmnd::ScopedMacro macro(stack, CmndName::tr("update a move key"));
                setAssignNotifier(macro, TimeKeyType_Move, false, true);
                // A checked Rotate/Scale box still creates that key on the same
                // gesture, exactly as the legacy move write did.
                if (mSRTParam.necessarilyRotate) {
                    mKeyOwner.pushOwningRotateKey(stack, timeLine, frame);
                }
                if (mSRTParam.necessarilyScale) {
                    mKeyOwner.pushOwningScaleKey(stack, timeLine, frame);
                }
                mTrackMoveRef = new AssignMoveTrackCommand(channels, pos, centroid);
                stack.push(mTrackMoveRef);
            }
            return;
        }

        for (int c = 0; c < 4; ++c) {
            channels.key[c] = timeLine.channelKey(TimeKeyType_Move, c, frame);
        }
        const bool allKeys = channels.key[0] && channels.key[1] && channels.key[2] && channels.key[3];
        if (!appendKeys && mTrackMoveRef && stack.isModifiable(mTrackMoveRef) && allKeys) {
            mTrackMoveRef->modifyValue(pos, centroid);
            notifyAssignModification(TimeKeyType_Move);
            return;
        }

        cmnd::ScopedMacro macro(stack, CmndName::tr("update a move key"));
        setAssignNotifier(
            macro, TimeKeyType_Move, mKeyOwner.ownsMoveKey || appendKeys, false
        );
        mKeyOwner.pushOwningMoveKey(stack, timeLine, frame);
        if (mSRTParam.necessarilyRotate) {
            mKeyOwner.pushOwningRotateKey(stack, timeLine, frame);
        }
        if (mSRTParam.necessarilyScale) {
            mKeyOwner.pushOwningScaleKey(stack, timeLine, frame);
        }

        for (int c = 0; c < 4; ++c) {
            channels.key[c] = timeLine.channelKey(TimeKeyType_Move, c, frame);
        }
        mTrackMoveRef = nullptr;
        if (channels.key[0] && channels.key[1] && channels.key[2] && channels.key[3]) {
            mTrackMoveRef = new AssignMoveTrackCommand(channels, pos, centroid);
            stack.push(mTrackMoveRef);
        }
    }

    void MoveMode::assignTrackRotateKey(RotateKey::Data& aNewData) {
        const int frame = mProject.animator().currentFrame().get();
        TimeLine& timeLine = *mTarget.timeLine();
        cmnd::Stack& stack = mProject.commandStack();
        ChannelTrack* track = timeLine.channelTrack(TimeKeyType_Rotate, 0);
        if (!track) {
            return;
        }

        const bool appendKeys = (mSRTParam.necessarilyMove && mKeyOwner.ownsMoveKey) ||
            (mSRTParam.necessarilyScale && mKeyOwner.ownsScaleKey);

        if (mKeyOwner.rotateIsBaseValue) {
            if (mTrackRotateBaseRef && stack.isModifiable(mTrackRotateBaseRef)) {
                mTrackRotateBaseRef->modifyValue(aNewData.rotate());
                notifyAssignModification(TimeKeyType_Rotate);
            } else {
                cmnd::ScopedMacro macro(stack, CmndName::tr("update a rotation key"));
                setAssignNotifier(macro, TimeKeyType_Rotate, false, true);
                mTrackRotateBaseRef = new AssignChannelBaseCommand(*track, aNewData.rotate());
                stack.push(mTrackRotateBaseRef);
            }
            return;
        }

        if (!appendKeys && mTrackRotateRef && stack.isModifiable(mTrackRotateRef)
            && timeLine.hasChannelKey(TimeKeyType_Rotate, 0, frame)) {
            mTrackRotateRef->modifyValue(aNewData.rotate());
            notifyAssignModification(TimeKeyType_Rotate);
            return;
        }

        cmnd::ScopedMacro macro(stack, CmndName::tr("update a rotation key"));
        setAssignNotifier(
            macro, TimeKeyType_Rotate, mKeyOwner.ownsRotateKey || appendKeys, false
        );
        mKeyOwner.pushOwningRotateKey(stack, timeLine, frame);
        if (mSRTParam.necessarilyMove) {
            mKeyOwner.pushOwningMoveKey(stack, timeLine, frame);
        }
        if (mSRTParam.necessarilyScale) {
            mKeyOwner.pushOwningScaleKey(stack, timeLine, frame);
        }

        mTrackRotateRef = nullptr;
        if (ChannelKey* key = timeLine.channelKey(TimeKeyType_Rotate, 0, frame)) {
            mTrackRotateRef = new AssignChannelValueCommand(*track, *key, aNewData.rotate());
            stack.push(mTrackRotateRef);
        }
    }

    void MoveMode::assignScaleKey(ScaleKey::Data& aNewData) {
        if (mKeyOwner.scaleIsTrack) {
            assignTrackScaleKey(aNewData);
            return;
        }

        const int frame = mProject.animator().currentFrame().get();
        TimeLine& timeLine = *mTarget.timeLine();
        cmnd::Stack& stack = mProject.commandStack();

        const bool appendKeys = (mSRTParam.necessarilyMove && mKeyOwner.ownsMoveKey) ||
            (mSRTParam.necessarilyRotate && mKeyOwner.ownsRotateKey);

        if (!appendKeys && mAssignScaleRef && mProject.commandStack().isModifiable(mAssignScaleRef)) {
            // modify command
            XC_ASSERT(!mKeyOwner.ownsScaleKey);
            mAssignScaleRef->modifyValue(aNewData);
            notifyAssignModification(TimeKeyType_Scale);
        } else {
            cmnd::ScopedMacro macro(stack, CmndName::tr("update a scaling key"));
            setAssignNotifier(
                macro, TimeKeyType_Scale, mKeyOwner.ownsScaleKey || appendKeys, mKeyOwner.scaleIsBaseValue
            );
            // push commands
            mKeyOwner.pushOwningScaleKey(stack, timeLine, frame);

            if (mSRTParam.necessarilyMove) {
                mKeyOwner.pushOwningMoveKey(stack, timeLine, frame);
            }
            if (mSRTParam.necessarilyRotate) {
                mKeyOwner.pushOwningRotateKey(stack, timeLine, frame);
            }

            mAssignScaleRef = nullptr;
            if (mKeyOwner.scaleKey) {
                mAssignScaleRef = new AssignScaleCommand(*mKeyOwner.scaleKey, timeLine, aNewData);
                stack.push(mAssignScaleRef);
            }
        }
    }

} // namespace ctrl::srt

