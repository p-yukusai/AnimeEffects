#include <QDebug>
#include <QSet>
#include "cmnd/BasicCommands.h"
#include "cmnd/ScopedMacro.h"
#include "core/TimeKeyType.h"
#include "core/ImageKeyUpdater.h"
#include "core/BlurKey.h"
#include "core/FFDKeyUpdater.h"
#include "core/ResourceUpdatingWorkspace.h"
#include "ctrl/TimeLineUtil.h"
#include "ctrl/CmndName.h"
#include "ctrl/graph/graph_Channel.h"
#include "ctrl/proportional/prop_Keys.h"

using namespace core;

namespace ctrl {

namespace TimeLineUtil {

    namespace {

        // ONE key's proportional flag and weights, as ONE undo step. Enabling
        // derives the weights from the key's CURRENT position against its CURRENT
        // absolute neighbours (the only time a weight is derived); disabling drops
        // them entirely.
        class SetProportionalKeyCommand: public cmnd::Stable {
        public:
            SetProportionalKeyCommand(core::TimeLine& aLine, core::TimeKey& aKey, bool aEnable):
                mKey(&aKey), mEnable(aEnable), mBaseEnabled(aKey.isProportional()), mBaseState(), mNextState() {
                if (mBaseEnabled) {
                    mBaseState = *aKey.proportionalState();
                }
                if (aEnable) {
                    mNextState = proportional::rebaseState(
                        aKey, proportional::findBounds(aLine.map(aKey.type()), aKey)
                    );
                }
            }

            virtual void undo() {
                if (mBaseEnabled) {
                    mKey->setProportionalState(mBaseState);
                } else {
                    mKey->clearProportionalState();
                }
            }

            virtual void redo() {
                if (mEnable) {
                    mKey->setProportionalState(mNextState);
                } else {
                    mKey->clearProportionalState();
                }
            }

        private:
            core::TimeKey* mKey;
            bool mEnable;
            bool mBaseEnabled;
            core::TimeKey::Proportional mBaseState;
            core::TimeKey::Proportional mNextState;
        };

        // The VALUE half of one key edit, seen as a whole: every proportional key
        // of the edited key's map follows the edit from its stored weights, and
        // undo restores their values AND weights. The capture runs before the
        // edited value is written (construction of this command); apply/restore
        // run after that write in exec and before its undo, which is why the
        // caller pushes this command AFTER the edit command in the same macro.
        class ReflowValuesCommand: public cmnd::Stable {
        public:
            ReflowValuesCommand(core::TimeLine& aLine, core::TimeKeyType aType, core::TimeKey& aEditedKey) {
                mReflow.capture(aLine, aType, QVector<core::TimeKey*> {&aEditedKey});
            }

            // No proportional key in the map: nothing to reflow, so the caller
            // must not push an empty history entry.
            bool hasWork() const { return !mReflow.empty(); }

            virtual void undo() { mReflow.restore(); }
            virtual void redo() { mReflow.apply(); }

        private:
            proportional::ValueReflow mReflow;
        };

    } // namespace

    //---------------------------------------------------------------------------------------
    MoveFrameOfKey::MoveFrameOfKey(const TimeLineEvent& aCommandEvent):
        mSortedTargets(), mContexts(), mCurrent(0), mMove(0) {
        for (const TimeLineEvent::Target& target : aCommandEvent.targets()) {
            // check validity of initial values
            XC_ASSERT(target.pos.index() == target.subIndex);

            mSortedTargets.push_back(target);
        }
        // sort targets for the purpose of the move with no conflict.
        std::sort(mSortedTargets.begin(), mSortedTargets.end(), lessThan);

        // One context per (line, type): the map is what a projection reads, and
        // keys of different maps never share bounds.
        for (int i = 0; i < mSortedTargets.size(); ++i) {
            TimeLineEvent::Target& target = mSortedTargets[i];
            TimeLine* line = target.pos.line();
            if (!line)
                continue;

            Context* context = nullptr;
            for (Context& candidate : mContexts) {
                if (candidate.line == line && candidate.type == target.pos.type()) {
                    context = &candidate;
                    break;
                }
            }
            if (!context) {
                Context fresh;
                fresh.line = line;
                fresh.type = target.pos.type();
                mContexts.append(fresh);
                context = &mContexts.last();
            }
            context->targetIndices.append(i);
        }

        for (Context& context : mContexts) {
            const TimeLine::MapType& map = context.line->map(context.type);

            // Every selected key of this map: a selected target owns its own
            // frame, a proportional one also remembers the weights it had before
            // the move so undo can restore them exactly.
            QSet<TimeKey*> selectedKeys;
            for (int index : context.targetIndices) {
                TimeKey* key = mSortedTargets[index].pos.key();
                if (!key)
                    continue;
                selectedKeys.insert(key);
                if (key->isProportional()) {
                    SelectedProportional selected;
                    selected.key = key;
                    selected.baseState = *key->proportionalState();
                    context.selected.append(selected);
                }
            }

            // Every OTHER proportional key of the map follows the move. Its
            // weights are stored, never re-derived per step: rebaseState is only
            // reached when a different key has become its bound.
            for (TimeKey* key : map) {
                if (!key || selectedKeys.contains(key) || !key->isProportional())
                    continue;

                ProjectedKey projected;
                projected.key = key;
                projected.baseFrame = key->frame();
                projected.currentFrame = projected.baseFrame;
                projected.nextFrame = projected.baseFrame;
                projected.baseState = *key->proportionalState();
                projected.nextState = projected.baseState;
                context.projected.append(projected);
            }
        }
    }

    bool MoveFrameOfKey::lessThan(const TimeLineEvent::Target& aLhs, const TimeLineEvent::Target& aRhs) {
        return aLhs.pos.index() < aRhs.pos.index();
    }
    bool MoveFrameOfKey::greaterThan(const TimeLineEvent::Target& aLhs, const TimeLineEvent::Target& aRhs) {
        return aLhs.pos.index() > aRhs.pos.index();
    }

    int MoveFrameOfKey::finalFrameOf(
        const Context& aContext, const TimeKey& aKey, const QVector<int>& aFinalFrames
    ) const {
        for (int index : aContext.targetIndices) {
            if (index < 0 || aFinalFrames.size() <= index)
                continue;
            const TimeLineEvent::Target& target = mSortedTargets[index];
            if (target.pos.type() != aContext.type)
                continue;

            // The key this target owns is at its ORIGINAL frame before the batch
            // runs and at the recorded destination afterwards. The key pointer is
            // the same object either way; resolving it through the map (instead of
            // pos.key()) is what stays correct once that frame has moved.
            const int current = (mCurrent == 0) ? target.pos.index() : target.subIndex;
            if (target.pos.map().value(current) == &aKey)
                return aFinalFrames[index];
        }
        return aKey.frame();
    }

    bool MoveFrameOfKey::modifyMove(TimeLineEvent& aModEvent, int aAdd, const util::Range& aFrame, int* aClampedAdd) {
        aModEvent.setType(TimeLineEvent::Type_MoveKey);

        if (aClampedAdd)
            *aClampedAdd = aAdd;
        if (aAdd == 0)
            return true; // empty

        // clamp aAdd value by the frame range
        for (const TimeLineEvent::Target& target : mSortedTargets) {
            XC_ASSERT(!target.pos.isNull());
            const int next = target.subIndex + aAdd;
            aAdd += xc_clamp(next, aFrame.min(), aFrame.max()) - next;
        }
        if (aClampedAdd)
            *aClampedAdd = aAdd;
        if (aAdd == 0)
            return true;

        // Candidate frames of the explicitly moved keys. The same vector answers a
        // bound's FINAL frame while the projected keys are computed below.
        Pending pending;
        pending.selectedFrames.reserve(mSortedTargets.size());
        for (const TimeLineEvent::Target& target : mSortedTargets) {
            XC_ASSERT(!target.pos.isNull());

            const int next = target.subIndex + aAdd;

            // check out of range (fail-safe code)
            if (next < aFrame.min() || aFrame.max() < next)
                return false;

            // key isn't exist
            const int current = (mCurrent == 0) ? target.pos.index() : target.subIndex;
            if (!target.pos.map().contains(current))
                return false;

            pending.selectedFrames.append(next);
        }

        // Project every proportional key of each touched map from its stored
        // weights and the bounds' FINAL frames. Nothing is written yet: a refused
        // batch must leave the document exactly as it was.
        pending.projectedFrames.resize(mContexts.size());
        pending.projectedStates.resize(mContexts.size());
        for (int ci = 0; ci < mContexts.size(); ++ci) {
            Context& context = mContexts[ci];
            const TimeLine::MapType& map = context.line->map(context.type);
            pending.projectedFrames[ci].reserve(context.projected.size());
            pending.projectedStates[ci].reserve(context.projected.size());

            for (ProjectedKey& projected : context.projected) {
                const proportional::Bounds bounds = proportional::findBounds(map, *projected.key);

                // A different key becoming a bound is the ONE case that re-derives
                // a weight; from the key's current position, once, and held.
                const bool stale = proportional::boundsChanged(*projected.key, bounds);
                const TimeKey::Proportional state =
                    stale ? proportional::rebaseState(*projected.key, bounds) : *projected.key->proportionalState();

                int frame = projected.currentFrame;
                if (state.timeValid && bounds.left && bounds.right) {
                    const double left = finalFrameOf(context, *bounds.left, pending.selectedFrames);
                    const double right = finalFrameOf(context, *bounds.right, pending.selectedFrames);
                    frame = qRound(left + static_cast<double>(state.timeWeight) * (right - left));
                }

                pending.projectedFrames[ci].append(frame);
                pending.projectedStates[ci].append(state);
            }
        }

        // Validate the WHOLE batch of every context before any of it runs: two
        // keys may not claim one frame, and a frame already taken may only be
        // taken by a key that this batch vacates.
        for (int ci = 0; ci < mContexts.size(); ++ci) {
            const Context& context = mContexts[ci];
            const TimeLine::MapType& map = context.line->map(context.type);

            QSet<int> sources;
            QSet<int> destinations;
            QSet<int> vacated;

            auto collect = [&](int aFrom, int aTo) -> bool {
                if (!map.contains(aFrom))
                    return false;
                if (sources.contains(aFrom))
                    return false;
                sources.insert(aFrom);

                if (aFrom == aTo)
                    return true; // frozen key: it stays where it is
                if (destinations.contains(aTo))
                    return false;
                destinations.insert(aTo);
                vacated.insert(aFrom);
                return true;
            };

            for (int index : context.targetIndices) {
                const int from = (mCurrent == 0) ? mSortedTargets[index].pos.index()
                                                 : mSortedTargets[index].subIndex;
                if (!collect(from, pending.selectedFrames[index]))
                    return false;
            }
            for (int j = 0; j < context.projected.size(); ++j) {
                if (!collect(context.projected[j].currentFrame, pending.projectedFrames[ci][j]))
                    return false;
            }
            for (int destination : destinations) {
                if (map.contains(destination) && !vacated.contains(destination))
                    return false;
            }
        }

        // update move
        mMove += aAdd;

        if (mCurrent == 0) // this command is not executed
        {
            // Only record where the keys would go. The frame map itself stays as
            // it is until the command is pushed (exec -> redo).
            for (int ci = 0; ci < mContexts.size(); ++ci) {
                Context& context = mContexts[ci];
                for (int j = 0; j < context.projected.size(); ++j) {
                    ProjectedKey& projected = context.projected[j];
                    projected.nextFrame = pending.projectedFrames[ci][j];
                    projected.nextState = pending.projectedStates[ci][j];
                    // The state is installed by exec/redo with the frame move; a
                    // not-yet-pushed command must not touch the document.
                }
            }
            for (TimeLineEvent::Target& target : mSortedTargets) {
                target.subIndex += aAdd;
            }
        } else // this command was already executed
        {
            // setup mod event
            for (int i = 0; i < mSortedTargets.size(); ++i) {
                TimeLineEvent::Target& target = mSortedTargets[i];
                aModEvent.pushTarget(
                    *target.node, target.pos.type(), target.subIndex, pending.selectedFrames[i]
                );
            }

            applyForward(pending);
        }
        return true;
    }

    void MoveFrameOfKey::applyForward(const Pending& aPending) {
        for (int ci = 0; ci < mContexts.size(); ++ci) {
            Context& context = mContexts[ci];

            // Selected and projected keys move in ONE batch, so a chained move
            // (a key into a frame this same step vacates) never depends on order.
            QVector<QPair<int, int>> moves;
            moves.reserve(context.targetIndices.size() + context.projected.size());
            for (int index : context.targetIndices) {
                const int from = mSortedTargets[index].subIndex;
                const int to = aPending.selectedFrames[index];
                if (from != to)
                    moves.append(qMakePair(from, to));
            }
            for (int j = 0; j < context.projected.size(); ++j) {
                const int from = context.projected[j].currentFrame;
                const int to = aPending.projectedFrames[ci][j];
                if (from != to)
                    moves.append(qMakePair(from, to));
            }

            if (!moves.isEmpty()) {
                const bool success = context.line->moveKeys(context.type, moves);
                XC_ASSERT(success);
                (void)success;
            }

            for (int index : context.targetIndices) {
                mSortedTargets[index].subIndex = aPending.selectedFrames[index];
            }
            for (int j = 0; j < context.projected.size(); ++j) {
                ProjectedKey& projected = context.projected[j];
                projected.currentFrame = aPending.projectedFrames[ci][j];
                projected.nextFrame = projected.currentFrame;
                projected.nextState = aPending.projectedStates[ci][j];
                projected.key->setProportionalState(projected.nextState);
            }

            // A selected proportional key is re-based from the frame it now owns:
            // the exact derivation, held for the next step.
            const TimeLine::MapType& map = context.line->map(context.type);
            for (SelectedProportional& selected : context.selected) {
                proportional::rebase(*selected.key, proportional::findBounds(map, *selected.key));
                selected.nextState = *selected.key->proportionalState();
                selected.nextStateValid = true;
            }
        }
    }

    void MoveFrameOfKey::undo() {
        for (Context& context : mContexts) {
            QVector<QPair<int, int>> moves;
            moves.reserve(context.targetIndices.size() + context.projected.size());
            for (int index : context.targetIndices) {
                const TimeLineEvent::Target& target = mSortedTargets[index];
                if (target.subIndex != target.pos.index())
                    moves.append(qMakePair(target.subIndex, target.pos.index()));
            }
            for (const ProjectedKey& projected : context.projected) {
                if (projected.currentFrame != projected.baseFrame)
                    moves.append(qMakePair(projected.currentFrame, projected.baseFrame));
            }

            if (!moves.isEmpty()) {
                const bool success = context.line->moveKeys(context.type, moves);
                XC_ASSERT(success);
                (void)success;
            }

            for (ProjectedKey& projected : context.projected) {
                projected.key->setProportionalState(projected.baseState);
                projected.currentFrame = projected.baseFrame;
            }
            for (SelectedProportional& selected : context.selected) {
                selected.key->setProportionalState(selected.baseState);
                selected.nextStateValid = false;
            }
        }

        mCurrent = 0;
    }

    void MoveFrameOfKey::redo() {
        for (Context& context : mContexts) {
            QVector<QPair<int, int>> moves;
            moves.reserve(context.targetIndices.size() + context.projected.size());
            for (int index : context.targetIndices) {
                const TimeLineEvent::Target& target = mSortedTargets[index];
                if (target.pos.index() != target.subIndex)
                    moves.append(qMakePair(target.pos.index(), target.subIndex));
            }
            for (const ProjectedKey& projected : context.projected) {
                if (projected.baseFrame != projected.nextFrame)
                    moves.append(qMakePair(projected.baseFrame, projected.nextFrame));
            }

            if (!moves.isEmpty()) {
                const bool success = context.line->moveKeys(context.type, moves);
                XC_ASSERT(success);
                (void)success;
            }

            for (ProjectedKey& projected : context.projected) {
                projected.key->setProportionalState(projected.nextState);
                projected.currentFrame = projected.nextFrame;
            }

            const TimeLine::MapType& map = context.line->map(context.type);
            for (SelectedProportional& selected : context.selected) {
                if (!selected.nextStateValid) {
                    // A first redo (the one exec makes) has no state yet: derive it
                    // from the frame the batch just gave the key.
                    proportional::rebase(*selected.key, proportional::findBounds(map, *selected.key));
                    selected.nextState = *selected.key->proportionalState();
                    selected.nextStateValid = true;
                } else {
                    selected.key->setProportionalState(selected.nextState);
                }
            }
        }

        mCurrent = 1;
    }

    //---------------------------------------------------------------------------------------
    void setProportionalKeys(Project& aProject, const TimeLineEvent& aTargets, bool aEnabled) {
        QVector<QPair<TimeLine*, TimeKey*>> keys;
        QVector<TimeLineEvent::Target> targets;
        QSet<TimeKey*> seen;

        for (const TimeLineEvent::Target& source : aTargets.targets()) {
            // A non-const copy: pos.line()/pos.key() have no const overload.
            TimeLineEvent::Target target = source;
            TimeLine* line = target.pos.line();
            if (!line)
                continue;

            TimeKey* key = target.pos.key();
            if (!key || key->isDefaultKey() || seen.contains(key))
                continue;
            seen.insert(key);

            keys.append(qMakePair(line, key));
            targets.append(target);
        }

        // Nothing valid targeted: pushing a macro would still announce a commit.
        if (keys.isEmpty())
            return;

        cmnd::ScopedMacro macro(
            aProject.commandStack(),
            aEnabled ? CmndName::tr("Make keys proportional") : CmndName::tr("Make keys absolute")
        );

        auto notifier = new Notifier(aProject);
        notifier->event().setType(TimeLineEvent::Type_ChangeKeyProportional);
        for (const TimeLineEvent::Target& target : targets) {
            notifier->event().pushTarget(*target.node, target.pos.type(), target.pos.index());
        }
        macro.grabListener(notifier);

        for (const QPair<TimeLine*, TimeKey*>& pair : keys) {
            aProject.commandStack().push(new SetProportionalKeyCommand(*pair.first, *pair.second, aEnabled));
        }
    }

    //---------------------------------------------------------------------------------------
    AssignKeyValue::AssignKeyValue(TimeKey& aKey, TimeKeyType aType, int aComponent, float aValue):
        mKey(&aKey), mType(aType), mComponent(aComponent), mPrev(0.0f), mNext(aValue), mDone(false) {}

    void AssignKeyValue::modifyValue(float aValue) {
        mNext = aValue;
        if (mDone) {
            redo();
        }
    }

    void AssignKeyValue::exec() {
        // The value the key holds NOW is the one undo restores - the same
        // capture cmnd::ModifiableAssign::exec does, done through the channel
        // because the component (not the whole Data) is what this command owns.
        mPrev = graph::Channel(mType, mComponent).value(*mKey);
        redo();
    }

    void AssignKeyValue::undo() {
        if (mDone) {
            graph::Channel(mType, mComponent).writeValue(*mKey, mPrev);
            mDone = false;
        }
    }

    void AssignKeyValue::redo() {
        graph::Channel(mType, mComponent).writeValue(*mKey, mNext);
        mDone = true;
    }

    //---------------------------------------------------------------------------------------
    template<class tKey, TimeKeyType tType>
    void assignKeyData(
        Project& aProject, ObjectNode& aTarget, int aFrame, const typename tKey::Data& aNewData, const QString& aText
    ) {
        XC_ASSERT(aTarget.timeLine());
        tKey* key = (tKey*)(aFrame == TimeLine::kDefaultKeyIndex ? aTarget.timeLine()->defaultKey(tType)
                                                                 : aTarget.timeLine()->timeKey(tType, aFrame));
        XC_PTR_ASSERT(key);

        // Capture the proportional followers BEFORE the value write. The command
        // itself is pushed AFTER the edit, so exec applies them after the edited
        // component and undo restores them before it.
        ReflowValuesCommand* reflow = nullptr;
        if (aFrame != TimeLine::kDefaultKeyIndex) {
            reflow = new ReflowValuesCommand(*aTarget.timeLine(), tType, *key);
        }

        {
            cmnd::ScopedMacro macro(aProject.commandStack(), aText);

            auto notifier = new Notifier(aProject);
            notifier->event().setType(TimeLineEvent::Type_ChangeKeyValue);
            if (aFrame == TimeLine::kDefaultKeyIndex) {
                notifier->event().pushDefaultTarget(aTarget, tType);
            } else {
                notifier->event().pushTarget(aTarget, tType, aFrame);
            }
            macro.grabListener(notifier);

            auto command = new cmnd::Assign<typename tKey::Data>(&(key->data()), aNewData);
            aProject.commandStack().push(command);

            if (reflow) {
                if (reflow->hasWork()) {
                    aProject.commandStack().push(reflow);
                } else {
                    delete reflow; // no proportional key to follow the edit
                }
            }
        }
    }

    //---------------------------------------------------------------------------------------
    template<class tKey, TimeKeyType tType>
    void assignKeyEasing(
        Project& aProject, ObjectNode& aTarget, int aFrame, const util::Easing::Param& aNewData, const QString& aText
    ) {
        XC_ASSERT(aTarget.timeLine());
        tKey* key = (tKey*)(aFrame == TimeLine::kDefaultKeyIndex ? aTarget.timeLine()->defaultKey(tType)
                                                                 : aTarget.timeLine()->timeKey(tType, aFrame));
        XC_PTR_ASSERT(key);

        {
            cmnd::ScopedMacro macro(aProject.commandStack(), aText);

            auto notifier = new Notifier(aProject);
            notifier->event().setType(TimeLineEvent::Type_ChangeKeyValue);
            if (aFrame == TimeLine::kDefaultKeyIndex) {
                notifier->event().pushDefaultTarget(aTarget, tType);
            } else {
                notifier->event().pushTarget(aTarget, tType, aFrame);
            }
            macro.grabListener(notifier);

            auto command = new cmnd::Assign<util::Easing::Param>(&(key->data().easing(0)), aNewData);
            aProject.commandStack().push(command);
        }
    }

    //---------------------------------------------------------------------------------------
    template<class tKey, TimeKeyType tType>
    void assignKeyBy(
        Project& aProject,
        ObjectNode& aTarget,
        int aFrame,
        const QString& aText,
        const std::function<void(tKey*)>& aFunc
    ) {
        XC_ASSERT(aTarget.timeLine());
        tKey* key = (tKey*)(aFrame == TimeLine::kDefaultKeyIndex ? aTarget.timeLine()->defaultKey(tType)
                                                                 : aTarget.timeLine()->timeKey(tType, aFrame));
        XC_PTR_ASSERT(key);

        // Same capture-before / push-after rule as assignKeyData: aFunc writes the
        // edited key, then the followers reflow inside the same macro.
        ReflowValuesCommand* reflow = nullptr;
        if (aFrame != TimeLine::kDefaultKeyIndex) {
            reflow = new ReflowValuesCommand(*aTarget.timeLine(), tType, *key);
        }

        {
            cmnd::ScopedMacro macro(aProject.commandStack(), aText);

            auto notifier = new Notifier(aProject);
            notifier->event().setType(TimeLineEvent::Type_ChangeKeyValue);
            if (aFrame == TimeLine::kDefaultKeyIndex) {
                notifier->event().pushDefaultTarget(aTarget, tType);
            } else {
                notifier->event().pushTarget(aTarget, tType, aFrame);
            }
            macro.grabListener(notifier);

            aFunc(key);

            if (reflow) {
                if (reflow->hasWork()) {
                    aProject.commandStack().push(reflow);
                } else {
                    delete reflow; // no proportional key to follow the edit
                }
            }
        }
    }

    //---------------------------------------------------------------------------------------
    void assignMoveKeyData(Project& aProject, ObjectNode& aTarget, int aFrame, const MoveKey::Data& aNewData) {
        assignKeyData<MoveKey, TimeKeyType_Move>(
            aProject,
            aTarget,
            aFrame,
            aNewData,
            aFrame == TimeLine::kDefaultKeyIndex ? CmndName::tr("Update default movement key")
                                                 : CmndName::tr("Update movement key")
        );
    }

    void assignRotateKeyData(Project& aProject, ObjectNode& aTarget, int aFrame, const RotateKey::Data& aNewData) {
        assignKeyData<RotateKey, TimeKeyType_Rotate>(
            aProject,
            aTarget,
            aFrame,
            aNewData,
            aFrame == TimeLine::kDefaultKeyIndex ? CmndName::tr("Update default rotation key")
                                                 : CmndName::tr("Update rotation key")
        );
    }

    void assignScaleKeyData(Project& aProject, ObjectNode& aTarget, int aFrame, const ScaleKey::Data& aNewData) {
        assignKeyData<ScaleKey, TimeKeyType_Scale>(
            aProject,
            aTarget,
            aFrame,
            aNewData,
            aFrame == TimeLine::kDefaultKeyIndex ? CmndName::tr("Update default scaling key")
                                                 : CmndName::tr("Update scaling key")
        );
    }

    void assignDepthKeyData(Project& aProject, ObjectNode& aTarget, int aFrame, const DepthKey::Data& aNewData) {
        assignKeyData<DepthKey, TimeKeyType_Depth>(
            aProject,
            aTarget,
            aFrame,
            aNewData,
            aFrame == TimeLine::kDefaultKeyIndex ? CmndName::tr("Update default depth key")
                                                 : CmndName::tr("Update depth key")
        );
    }

    void assignOpaKeyData(Project& aProject, ObjectNode& aTarget, int aFrame, const OpaKey::Data& aNewData) {
        assignKeyData<OpaKey, TimeKeyType_Opa>(
            aProject,
            aTarget,
            aFrame,
            aNewData,
            aFrame == TimeLine::kDefaultKeyIndex ? CmndName::tr("Update default opacity key")
                                                 : CmndName::tr("Update opacity key")
        );
    }

    void assignHSVKeyData(Project& aProject, ObjectNode& aTarget, int aFrame, const HSVKey::Data& aNewData) {
        assignKeyData<HSVKey, TimeKeyType_HSV>(
            aProject,
            aTarget,
            aFrame,
            aNewData,
            aFrame == TimeLine::kDefaultKeyIndex ? CmndName::tr("Update default HSV key")
                                                 : CmndName::tr("Update HSV key")
        );
    }

    void assignBlurKeyData(Project& aProject, ObjectNode& aTarget, int aFrame, const BlurKey::Data& aNewData) {
        assignKeyData<BlurKey, TimeKeyType_Blur>(
            aProject,
            aTarget,
            aFrame,
            aNewData,
            aFrame == TimeLine::kDefaultKeyIndex ? CmndName::tr("Update default blur key")
                                                 : CmndName::tr("Update blur key")
        );
    }

    void assignPoseKeyEasing(Project& aProject, ObjectNode& aTarget, int aFrame, const util::Easing::Param& aNewData) {
        assignKeyEasing<PoseKey, TimeKeyType_Pose>(
            aProject,
            aTarget,
            aFrame,
            aNewData,
            aFrame == TimeLine::kDefaultKeyIndex ? CmndName::tr("Update default pose key")
                                                 : CmndName::tr("Update pose key")
        );
    }

    void assignFFDKeyEasing(Project& aProject, ObjectNode& aTarget, int aFrame, const util::Easing::Param& aNewData) {
        assignKeyEasing<FFDKey, TimeKeyType_FFD>(
            aProject,
            aTarget,
            aFrame,
            aNewData,
            aFrame == TimeLine::kDefaultKeyIndex ? CmndName::tr("Update default FFD key")
                                                 : CmndName::tr("Update FFD key")
        );
    }

    void assignImageKeyResource(Project& aProject, ObjectNode& aTarget, int aFrame, img::ResourceNode& aNewData) {
        ResourceUpdatingWorkspacePtr workspace = std::make_shared<ResourceUpdatingWorkspace>();
        const bool createTransitions = !aTarget.timeLine()->isEmpty(TimeKeyType_FFD);

        assignKeyBy<ImageKey, TimeKeyType_Image>(
            aProject,
            aTarget,
            aFrame,
            aFrame == TimeLine::kDefaultKeyIndex ? CmndName::tr("Update default image resource key")
                                                 : CmndName::tr("update image resource key"),
            [&](ImageKey* aKey) {
                // image key
                aProject.commandStack().push(
                    ImageKeyUpdater::createResourceUpdater(*aKey, aNewData, workspace, createTransitions)
                );

                // ffd key should be called finally
                if (createTransitions) {
                    aProject.commandStack().push(FFDKeyUpdater::createResourceUpdater(aTarget, workspace));
                }
            }
        );
    }

    void assignImageKeyOffset(Project& aProject, ObjectNode& aTarget, int aFrame, const QVector2D& aNewData) {
        assignKeyBy<ImageKey, TimeKeyType_Image>(
            aProject,
            aTarget,
            aFrame,
            aFrame == TimeLine::kDefaultKeyIndex ? CmndName::tr("Update the image's default offset key")
                                                 : CmndName::tr("Update the image's offset key"),
            [&](ImageKey* aKey) {
                auto prevOffset = aKey->data().imageOffset();

                aProject.commandStack().push(new cmnd::Delegatable(
                    [=]() { aKey->data().setImageOffset(aNewData); }, [=]() { aKey->data().setImageOffset(prevOffset); }
                ));
            }
        );
    }

    void assignImageKeyCellSize(core::Project& aProject, core::ObjectNode& aTarget, int aFrame, int aNewData) {
        ResourceUpdatingWorkspacePtr workspace = std::make_shared<ResourceUpdatingWorkspace>();
        const bool createTransitions = !aTarget.timeLine()->isEmpty(TimeKeyType_FFD);

        assignKeyBy<ImageKey, TimeKeyType_Image>(
            aProject,
            aTarget,
            aFrame,
            aFrame == TimeLine::kDefaultKeyIndex ? CmndName::tr("Update the default mesh size key")
                                                 : CmndName::tr("Update mesh size key"),
            [&](ImageKey* aKey) {
                // image key
                aProject.commandStack().push(
                    ImageKeyUpdater::createGridMeshUpdater(*aKey, aNewData, workspace, createTransitions)
                );

                // ffd key should be called finally
                if (createTransitions) {
                    aProject.commandStack().push(FFDKeyUpdater::createResourceUpdater(aTarget, workspace));
                }
            }
        );
    }

    //---------------------------------------------------------------------------------------
    template<class tKey, TimeKeyType tType>
    void pushNewKey(
        Project& aProject,
        ObjectNode& aTarget,
        int aFrame,
        tKey* aKey,
        const QString& aText,
        TimeKey* aParentKey = nullptr
    ) {
        XC_PTR_ASSERT(aKey);
        XC_ASSERT(aTarget.timeLine());

        {
            cmnd::ScopedMacro macro(aProject.commandStack(), aText);

            // set notifier
            auto notifier = new Notifier(aProject);
            notifier->event().setType(TimeLineEvent::Type_PushKey);
            notifier->event().pushTarget(aTarget, tType, aFrame);
            macro.grabListener(notifier);

            // create commands
            auto& stack = aProject.commandStack();
            stack.push(new cmnd::GrabNewObject<tKey>(aKey));
            stack.push(aTarget.timeLine()->createPusher(tType, aFrame, aKey));

            if (aParentKey) {
                stack.push(new cmnd::PushBackTree<TimeKey>(&aParentKey->children(), aKey));
            }
        }
    }

    void pushNewMoveKey(Project& aProject, ObjectNode& aTarget, int aFrame, MoveKey* aKey) {
        pushNewKey<MoveKey, TimeKeyType_Move>(aProject, aTarget, aFrame, aKey, CmndName::tr("Add new movement key"));
    }
    void pushNewRotateKey(Project& aProject, ObjectNode& aTarget, int aFrame, RotateKey* aKey) {
        pushNewKey<RotateKey, TimeKeyType_Rotate>(
            aProject, aTarget, aFrame, aKey, CmndName::tr("Add new rotation key")
        );
    }

    void pushNewScaleKey(Project& aProject, ObjectNode& aTarget, int aFrame, ScaleKey* aKey) {
        pushNewKey<ScaleKey, TimeKeyType_Scale>(aProject, aTarget, aFrame, aKey, CmndName::tr("Add new scaling key"));
    }

    void pushNewDepthKey(Project& aProject, ObjectNode& aTarget, int aFrame, DepthKey* aKey) {
        pushNewKey<DepthKey, TimeKeyType_Depth>(aProject, aTarget, aFrame, aKey, CmndName::tr("Add new depth key"));
    }

    void pushNewOpaKey(Project& aProject, ObjectNode& aTarget, int aFrame, OpaKey* aKey) {
        pushNewKey<OpaKey, TimeKeyType_Opa>(aProject, aTarget, aFrame, aKey, CmndName::tr("Add new opacity key"));
    }

    void pushNewHSVKey(Project& aProject, ObjectNode& aTarget, int aFrame, HSVKey* aKey) {
        pushNewKey<HSVKey, TimeKeyType_HSV>(aProject, aTarget, aFrame, aKey, CmndName::tr("Add new HSV key"));
    }

    void pushNewBlurKey(Project& aProject, ObjectNode& aTarget, int aFrame, BlurKey* aKey) {
        pushNewKey<BlurKey, TimeKeyType_Blur>(aProject, aTarget, aFrame, aKey, CmndName::tr("Add new blur key"));
    }

    void pushNewPoseKey(Project& aProject, ObjectNode& aTarget, int aFrame, PoseKey* aKey, BoneKey* aParentKey) {
        XC_PTR_ASSERT(aParentKey);
        pushNewKey<PoseKey, TimeKeyType_Pose>(
            aProject, aTarget, aFrame, aKey, CmndName::tr("Add new pose key"), aParentKey
        );
    }

    void pushNewFFDKey(Project& aProject, ObjectNode& aTarget, int aFrame, FFDKey* aKey, TimeKey* aParentKey) {
        XC_ASSERT(!aParentKey || aParentKey->type() == TimeKeyType_Mesh || aParentKey->type() == TimeKeyType_Image);
        pushNewKey<FFDKey, TimeKeyType_FFD>(
            aProject, aTarget, aFrame, aKey, CmndName::tr("Add new FFD key"), aParentKey
        );
    }

    void pushNewImageKey(Project& aProject, ObjectNode& aTarget, int aFrame, ImageKey* aKey) {
        pushNewKey<ImageKey, TimeKeyType_Image>(aProject, aTarget, aFrame, aKey, CmndName::tr("Add new image key"));
    }

} // namespace TimeLineUtil

} // namespace ctrl
