#ifndef CTRL_SRT_MOVEMODE_H
#define CTRL_SRT_MOVEMODE_H

#include "util/PlacePointer.h"
#include "cmnd/BasicCommands.h"
#include "core/Project.h"
#include "core/ObjectNode.h"
#include "ctrl/ScopedModifier.h"
#include "ctrl/srt/srt_AssignKeyCommand.h"
#include "ctrl/srt/srt_IMode.h"
#include "ctrl/srt/srt_KeyOwner.h"
#include "ctrl/srt/srt_Symbol.h"

namespace ctrl {
namespace srt {

    //-------------------------------------------------------------------------------------------------
    class MoveMode: public IMode {
    public:
        MoveMode(core::Project& aProject, core::ObjectNode& aTarget, KeyOwner& aKey);
        virtual void updateParam(const SRTParam&);
        virtual bool updateCursor(const core::CameraInfo&, const core::AbstractCursor&);
        virtual void renderQt(const core::RenderInfo& aInfo, QPainter& aPainter);

    private:
        // The three live value commands of a pose drag. They are the same one
        // command template (srt_AssignKeyCommand.h): the key's whole Data, plus
        // the proportional companions of its channel, in ONE stack entry that
        // every later step rewrites.
        typedef AssignKeyCommand<core::MoveKey, core::TimeKeyType_Move> AssignMoveCommand;
        typedef AssignKeyCommand<core::RotateKey, core::TimeKeyType_Rotate> AssignRotateCommand;
        typedef AssignKeyCommand<core::ScaleKey, core::TimeKeyType_Scale> AssignScaleCommand;

        void clearState();
        void assignMoveKey(core::MoveKey::Data& aNewData);
        void assignRotateKey(core::RotateKey::Data& aNewData);
        void assignScaleKey(core::ScaleKey::Data& aNewData);
        void assignTrackRotateKey(core::RotateKey::Data& aNewData);
        void assignTrackScaleKey(core::ScaleKey::Data& aNewData);
        void assignTrackMoveKey(core::MoveKey::Data& aNewData);

        void setAssignNotifier(cmnd::ScopedMacro& aMacro, core::TimeKeyType aKeyType, bool aPushKey,
                               bool aBaseValue);
        void notifyAssignModification(core::TimeKeyType aKeyType);

        core::Project& mProject;
        core::ObjectNode& mTarget;
        KeyOwner& mKeyOwner;
        SRTParam mSRTParam;

        Symbol mSymbol;
        Symbol::FocusData mFocus;
        AssignMoveCommand* mAssignMoveRef;
        AssignRotateCommand* mAssignRotateRef;
        AssignScaleCommand* mAssignScaleRef;
        AssignChannelValueCommand* mTrackRotateRef;
        AssignChannelBaseCommand* mTrackRotateBaseRef;
        AssignScaleTrackCommand* mTrackScaleRef;
        AssignMoveTrackCommand* mTrackMoveRef;
        util::PlacePointer<ScopedModifier> mSuspend;
        QVector2D mBaseVec;
        float mBaseValue;
    };

} // namespace srt
} // namespace ctrl

#endif // CTRL_SRT_MOVEMODE_H
