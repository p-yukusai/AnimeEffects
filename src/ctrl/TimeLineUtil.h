#ifndef CTRL_TIMELINEUTIL_H
#define CTRL_TIMELINEUTIL_H

#include <QVector>
#include "util/Range.h"
#include "cmnd/Stable.h"
#include "cmnd/Listener.h"
#include "cmnd/ScopedMacro.h"
#include "core/Project.h"
#include "core/TimeLine.h"
#include "core/TimeLineEvent.h"
#include "core/MoveKey.h"
#include "core/RotateKey.h"
#include "core/ScaleKey.h"
#include "core/BlurKey.h"
#include "core/WaveKey.h"
#include "core/DepthKey.h"
#include "core/OpaKey.h"
#include "core/HsvKey.h"
#include "core/BoneKey.h"
#include "core/PoseKey.h"
#include "core/FFDKey.h"
#include "core/MeshKey.h"
#include "core/ImageKey.h"
#include "core/MaskKey.h"

namespace ctrl {

namespace TimeLineUtil {

    //-------------------------------------------------------------------------------------------------
    class MoveFrameOfKey: public cmnd::Stable {
    public:
        MoveFrameOfKey(const core::TimeLineEvent& aCommandEvent);
        bool modifyMove(core::TimeLineEvent& aModEvent, int aAdd, const util::Range& aFrame, int* aClampedAdd);
        static bool lessThan(const core::TimeLineEvent::Target& aLhs, const core::TimeLineEvent::Target& aRhs);
        static bool greaterThan(const core::TimeLineEvent::Target& aLhs, const core::TimeLineEvent::Target& aRhs);
        virtual void undo();
        virtual void redo();

    private:
        // One key type of one timeline this move touches. A selected key is
        // moved explicitly; every other PROPORTIONAL key of the same map is
        // projected from its stored weight.
        struct SelectedProportional {
            core::TimeKey* key = nullptr;
            core::TimeKey::Proportional baseState;
            core::TimeKey::Proportional nextState;
            bool nextStateValid = false;
        };
        struct ProjectedKey {
            core::TimeKey* key = nullptr;
            int baseFrame = 0;    // frame at construction, restored by undo
            int currentFrame = 0; // where the map has it now
            int nextFrame = 0;    // last projected destination, replayed by redo
            core::TimeKey::Proportional baseState;
            core::TimeKey::Proportional nextState;
        };
        struct Context {
            core::TimeLine* line = nullptr;
            core::TimeKeyType type = core::TimeKeyType_Move;
            QVector<int> targetIndices; // into mSortedTargets
            QVector<SelectedProportional> selected;
            QVector<ProjectedKey> projected;
        };
        struct Pending {
            QVector<int> selectedFrames;
            QVector<QVector<int>> projectedFrames;
            QVector<QVector<core::TimeKey::Proportional>> projectedStates;
        };

        void applyForward(const Pending& aPending);
        int finalFrameOf(const Context& aContext, const core::TimeKey& aKey, const QVector<int>& aFinalFrames) const;

        QVector<core::TimeLineEvent::Target> mSortedTargets;
        QVector<Context> mContexts;
        int mCurrent;
        int mMove;
    };

    //-------------------------------------------------------------------------------------------------
    // Turns each targeted key into a proportional key (aEnabled) or back into an
    // absolute one, as ONE undo step announcing
    // core::TimeLineEvent::Type_ChangeKeyProportional for the targeted keys.
    // Enabling derives each key's weights from its CURRENT position against its
    // CURRENT absolute neighbours; disabling drops them entirely. Duplicate
    // targets collapse, and default/missing keys are ignored - nothing valid
    // targeted means no command.
    void setProportionalKeys(core::Project& aProject, const core::TimeLineEvent& aTargets, bool aEnabled);

    //-------------------------------------------------------------------------------------------------
    // The VALUE half of the graph's 2-axis key move: rewrites ONE numeric
    // component of ONE key, in place, and puts it back on undo.
    //
    // It is a cmnd::Stable carrying both values, so undo and redo rebuild the
    // document from the command alone (the requirement cmnd/Stable.h states) and
    // a whole drag stays ONE history entry when this command is pushed inside the
    // same cmnd::ScopedMacro as the frame half (MoveFrameOfKey above) - which is
    // the only reason the graph splits one key move into two commands rather than
    // one: the frame is a map operation and the value is a field write, and each
    // already has a command that knows how to replay it.
    //
    // The component is named by (type, component) - the graph's own Channel
    // vocabulary - and the write goes through graph::Channel::writeValue, the one
    // place that knows which field of which key type a component is. The key is
    // held by POINTER, like the easing gesture's cmnd::ModifiableAssign, which is
    // safe here because TimeLine::move re-keys the same key OBJECT: the frame half
    // of the gesture moves the map entry, never the key this command writes.
    class AssignKeyValue: public cmnd::Stable {
    public:
        AssignKeyValue(core::TimeKey& aKey, core::TimeKeyType aType, int aComponent, float aValue);

        // A later step of the same gesture rewrites this command, which is what
        // makes a whole drag one undo step. Mirrors cmnd::ModifiableAssign's own
        // modifyValue, including applying the new value at once while the command
        // is the stack's executed one.
        void modifyValue(float aValue);

        // Captures the value the key holds NOW as the one undo restores, then
        // applies the new value - cmnd::ModifiableAssign's own exec.
        virtual void exec();
        virtual void undo();
        virtual void redo();

    private:
        core::TimeKey* mKey;
        core::TimeKeyType mType;
        int mComponent;
        float mPrev;
        float mNext;
        bool mDone;
    };

    //-------------------------------------------------------------------------------------------------
    class Notifier: public cmnd::Listener {
    public:
        Notifier(core::Project& aProject): mProject(aProject), mEvent() {}

        core::TimeLineEvent& event() { return mEvent; }
        const core::TimeLineEvent& event() const { return mEvent; }

        virtual void onExecuted() { mProject.onTimeLineModified(mEvent, false); }

        virtual void onUndone() { mProject.onTimeLineModified(mEvent, true); }

        virtual void onRedone() { mProject.onTimeLineModified(mEvent, false); }

    private:
        core::Project& mProject;
        core::TimeLineEvent mEvent;
    };

    //-------------------------------------------------------------------------------------------------
    // ONE LIVE EDIT'S LATER STEPS: the ONE path that rewrites the commands a
    // gesture already pushed AND tells the timeline about the new values.
    //
    // A gesture that keeps ONE command and rewrites it per step (the graph's
    // handle drag, the picker's live easing session, and any later gesture of the
    // same shape) has two halves that must never separate:
    //
    //   * the REWRITE goes straight through the stack (Gesture::command() or
    //     ModifiableAssign::modifyValue) and therefore fires no listener - the
    //     first commit's Notifier already ran and will not run again;
    //   * the ANNOUNCE is a core::TimeLineEvent naming every key the step changed,
    //     sent with core::Project::onTimeLineModified.
    //
    // Writing the second by hand after the first is the duplication this class
    // exists to delete. A bare modifyValue() followed by onTimeLineModified() is
    // the old pattern: a step that rewrites without announcing leaves the
    // timeline, the header chip and the graph stale until an unrelated event, so
    // route every later step through RewriteAnnounce instead.
    //
    // Contract:
    //   * begin() is the FIRST step. It opens ONE cmnd::ScopedMacro (one undo
    //     entry), grabs ONE Notifier carrying aTargets' type and targets, and runs
    //     the builder inside that macro. The builder pushes every command the
    //     gesture will rewrite and returns the command the stack must keep
    //     modifiable (the last one pushed). The notifier announces this step when
    //     the macro closes; begin() never announces a second time.
    //   * The TARGETS are established here, once: step() cannot add or drop one,
    //     so every step of one gesture announces the same keys. Duplicates
    //     collapse to one Target per (node, type, frame) - one notification per
    //     key, however many components of it the gesture writes.
    //   * step() runs the rewrite, then announces EXACTLY ONCE - and only when
    //     the rewrite reports that it changed something (aRewrite returns bool).
    //     It refuses (returns false, announces nothing) once the stack no longer
    //     tops the command - an undo/redo or another edit - so the caller opens a
    //     fresh edit with begin() instead of corrupting the history.
    //   * end() closes the session. The commands stay on the stack holding their
    //     last value, so the gesture remains ONE undo entry.
    //
    // Call begin() only for a step that will push something: a macro holding a
    // notifier announces its commit even when the builder pushed no command.
    class RewriteAnnounce {
    public:
        RewriteAnnounce(): mProject(nullptr), mStack(nullptr), mCommand(nullptr), mEvent() {}

        template<typename tBuild>
        bool begin(
            core::Project& aProject, cmnd::Stack& aStack, const QString& aName,
            const core::TimeLineEvent& aTargets, tBuild&& aBuild
        ) {
            end();
            mProject = &aProject;
            mStack = &aStack;
            mEvent.setType(aTargets.type());
            for (const core::TimeLineEvent::Target& target : aTargets.targets()) {
                appendUniqueTarget(target);
            }

            cmnd::ScopedMacro macro(aStack, aName);
            auto* notifier = new Notifier(aProject);
            notifier->event().setType(mEvent.type());
            notifier->event().targets() = mEvent.targets();
            macro.grabListener(notifier);

            mCommand = aBuild();
            return mCommand != nullptr;
        }

        template<typename tRewrite>
        bool step(tRewrite&& aRewrite) {
            if (!continues()) {
                return false; // an undo/redo/another edit: the caller must begin()
            }
            if (!aRewrite()) {
                return false; // nothing rewritten, so nothing to announce
            }
            mProject->onTimeLineModified(mEvent, false);
            return true;
        }

        // The stack still tops the command begin() pushed, so a step may rewrite it.
        bool continues() const { return mCommand && mStack && mStack->isModifiable(mCommand); }

        // The command begin() returned, in the gesture's own type.
        cmnd::Base* command() const { return mCommand; }
        template<typename tCommand>
        tCommand* commandAs() const {
            return static_cast<tCommand*>(mCommand);
        }

        void end() {
            mProject = nullptr;
            mStack = nullptr;
            mCommand = nullptr;
            mEvent = core::TimeLineEvent();
        }

    private:
        void appendUniqueTarget(const core::TimeLineEvent::Target& aTarget) {
            for (const core::TimeLineEvent::Target& pushed : mEvent.targets()) {
                if (pushed.node == aTarget.node && pushed.pos.type() == aTarget.pos.type()
                    && pushed.pos.index() == aTarget.pos.index()) {
                    return;
                }
            }
            mEvent.targets().push_back(aTarget);
        }

        core::Project* mProject;
        cmnd::Stack* mStack;
        cmnd::Base* mCommand;
        core::TimeLineEvent mEvent;
    };

    class ResourceModificationNotifier: public cmnd::Listener {
    public:
        ResourceModificationNotifier(core::Project& aProject): mProject(aProject), mEvent(aProject) {}

        core::ResourceEvent& event() { return mEvent; }
        const core::ResourceEvent& event() const { return mEvent; }

        virtual void onExecuted() { mProject.onResourceModified(mEvent, false); }

        virtual void onUndone() { mProject.onResourceModified(mEvent, true); }

        virtual void onRedone() { mProject.onResourceModified(mEvent, false); }

    private:
        core::Project& mProject;
        core::ResourceEvent mEvent;
    };

    //-------------------------------------------------------------------------------------------------
    /// @note you can also set a data to default keys in following assignment functions.
    ///@{
    void assignMoveKeyData(
        core::Project& aProject, core::ObjectNode& aTarget, int aFrame, const core::MoveKey::Data& aNewData
    );

    void assignRotateKeyData(
        core::Project& aProject, core::ObjectNode& aTarget, int aFrame, const core::RotateKey::Data& aNewData
    );

    void assignScaleKeyData(
        core::Project& aProject, core::ObjectNode& aTarget, int aFrame, const core::ScaleKey::Data& aNewData
    );

    void assignDepthKeyData(
        core::Project& aProject, core::ObjectNode& aTarget, int aFrame, const core::DepthKey::Data& aNewData
    );

    void assignOpaKeyData(
        core::Project& aProject, core::ObjectNode& aTarget, int aFrame, const core::OpaKey::Data& aNewData
    );

    void assignHSVKeyData(
        core::Project& aProject, core::ObjectNode& aTarget, int aFrame, const core::HSVKey::Data& aNewData
    );

    void assignBlurKeyData(
        core::Project& aProject, core::ObjectNode& aTarget, int aFrame, const core::BlurKey::Data& aNewData
    );

    void assignWaveKeyData(
        core::Project& aProject, core::ObjectNode& aTarget, int aFrame, const core::WaveKey::Data& aNewData
    );

    void assignPoseKeyEasing(
        core::Project& aProject, core::ObjectNode& aTarget, int aFrame, const util::Easing::Param& aNewData
    );

    void assignFFDKeyEasing(
        core::Project& aProject, core::ObjectNode& aTarget, int aFrame, const util::Easing::Param& aNewData
    );

    void
    assignImageKeyResource(core::Project& aProject, core::ObjectNode& aTarget, int aFrame, img::ResourceNode& aNewData);

    void
    assignImageKeyOffset(core::Project& aProject, core::ObjectNode& aTarget, int aFrame, const QVector2D& aNewData);

    void assignImageKeyCellSize(core::Project& aProject, core::ObjectNode& aTarget, int aFrame, int aNewData);
    ///@}

    //-------------------------------------------------------------------------------------------------
    void pushNewMoveKey(core::Project& aProject, core::ObjectNode& aTarget, int aFrame, core::MoveKey* aKey);

    void pushNewRotateKey(core::Project& aProject, core::ObjectNode& aTarget, int aFrame, core::RotateKey* aKey);

    void pushNewScaleKey(core::Project& aProject, core::ObjectNode& aTarget, int aFrame, core::ScaleKey* aKey);

    void pushNewDepthKey(core::Project& aProject, core::ObjectNode& aTarget, int aFrame, core::DepthKey* aKey);

    void pushNewOpaKey(core::Project& aProject, core::ObjectNode& aTarget, int aFrame, core::OpaKey* aKey);

    void pushNewHSVKey(core::Project& aProject, core::ObjectNode& aTarget, int aFrame, core::HSVKey* aKey);
    void pushNewBlurKey(core::Project& aProject, core::ObjectNode& aTarget, int aFrame, core::BlurKey* aKey);

    void pushNewWaveKey(core::Project& aProject, core::ObjectNode& aTarget, int aFrame, core::WaveKey* aKey);

    void pushNewPoseKey(
        core::Project& aProject, core::ObjectNode& aTarget, int aFrame, core::PoseKey* aKey, core::BoneKey* aParentKey
    );

    void pushNewFFDKey(
        core::Project& aProject, core::ObjectNode& aTarget, int aFrame, core::FFDKey* aKey, core::TimeKey* aParentKey
    );

    void pushNewImageKey(core::Project& aProject, core::ObjectNode& aTarget, int aFrame, core::ImageKey* aKey);

    // A filter lane's PAINTED MASK (core/FilterMask.h), cleared back to its neutral
    // value as ONE undo step. The field stays allocated (a later paint reuses it);
    // undoing restores the pixels the command carries.
    void clearFilterMask(
        core::Project& aProject, core::ObjectNode& aTarget, core::TimeKeyType aType,
        core::FilterMaskChannel aChannel
    );
    bool hasFilterMask(const core::ObjectNode& aTarget, core::TimeKeyType aType, core::FilterMaskChannel aChannel);

    // --- the filter stack -------------------------------------------------
    // The stack is ORDER + ENABLED on the timeline; values stay in the HSV/Blur
    // key maps. Every operation is one undo step and announces
    // core::TimeLineEvent::Type_FilterStack, so the property board refreshes even
    // when the operation touches no key.
    //
    // addFilter appends a filter that has no key yet (the row exists, identity
    // values; the first field edit creates the key). removeFilter also removes
    // every explicit key of that filter's lane, because a stack entry is what
    // makes the lane meaningful. moveFilter moves one entry to a new index in the
    // effective stack (0 = top). setFilterEnabled flips one entry's eye.
    // ---- the node's MATTE (core::MaskKey) -------------------------------------
    //
    // One mask per node, and the four edits every surface calls: the inspector's
    // card, the tree's chip and the Mask tool all go through these, so the states
    // they paint and the states they show cannot drift.
    //
    // A mask starts at REVEAL ALL (the field is 255 everywhere), so adding one
    // changes nothing on screen until it is painted - unlike a filter mask, whose
    // neutral is per input.
    // The KEY a stroke may write: the one at aFrame, else the default key, else
    // nullptr (nothing to paint - the card's "Add mask" is the entry, and the tool
    // does not invent a mask behind the user's back).
    core::MaskKey* maskKeyForPaint(core::ObjectNode& aTarget, int aFrame);

    // One undoable write of a mask key's whole field. The brush captures the field
    // before its stroke and calls this at the end, so a stroke is ONE undo step and
    // undo restores the pixels exactly (a matte is too big to re-derive).
    class AssignMaskField: public cmnd::Stable {
    public:
        AssignMaskField(core::MaskKey* aKey, const core::MaskKey::Data& aNext);
        // Both states given: a STROKE writes the key in place and pushes this at the
        // release, so the field it captured before the first stamp is what undo restores.
        AssignMaskField(core::MaskKey* aKey, const core::MaskKey::Data& aPrev,
                        const core::MaskKey::Data& aNext);
        virtual bool redoable() const;
        virtual bool undoable() const;
        virtual void redo();
        virtual void undo();

    private:
        core::MaskKey* mKey;
        core::MaskKey::Data mPrev;
        core::MaskKey::Data mNext;
    };

    // Clears a mask key's field back to REVEAL ALL (the matte's neutral).
    void resetMaskField(core::Project& aProject, core::ObjectNode& aTarget, core::MaskKey* aKey);

    void addMask(core::Project& aProject, core::ObjectNode& aTarget);
    // Remove the mask and all its keys. Undoable, like removing a filter.
    void removeMask(core::Project& aProject, core::ObjectNode& aTarget);
    // Invert the matte at the key it would be painted on (that key's own field).
    void invertMask(core::Project& aProject, core::ObjectNode& aTarget, int aFrame);
    // Hollow dot -> a key at aFrame holding the current blend (a hold); filled dot ->
    // remove that frame's key. The same rule every other channel's dot follows.
    void toggleMaskKey(core::Project& aProject, core::ObjectNode& aTarget, int aFrame);

    void addFilter(core::Project& aProject, core::ObjectNode& aTarget, core::TimeKeyType aType);
    void removeFilter(core::Project& aProject, core::ObjectNode& aTarget, core::TimeKeyType aType);
    void moveFilter(core::Project& aProject, core::ObjectNode& aTarget, core::TimeKeyType aType, int aIndex);
    void setFilterEnabled(core::Project& aProject, core::ObjectNode& aTarget, core::TimeKeyType aType, bool aEnabled);

} // namespace TimeLineUtil

} // namespace ctrl

#endif // CTRL_TIMELINEUTIL_H
