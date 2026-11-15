#include <QDebug>
#include <QSet>
#include "cmnd/BasicCommands.h"
#include "util/Easing.h"
#include "cmnd/ScopedMacro.h"
#include "core/TimeKeyType.h"
#include "core/ImageKeyUpdater.h"
#include "core/BlurKey.h"
#include "core/TimeKeyExpans.h"
#include "core/FFDKeyUpdater.h"
#include "core/ResourceUpdatingWorkspace.h"
#include "ctrl/TimeLineUtil.h"
#include "ctrl/AssignPixels.h"
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
        for (const TimeLineEvent::Target& source : aCommandEvent.targets()) {
            // check validity of initial values
            XC_ASSERT(source.pos.index() == source.subIndex);

            TimeLineEvent::Target target = source;
            // A whole-key selection of a track-backed property names component
            // -1; the move addresses the channel map, so a single-channel track
            // resolves to component 0. (Multi-channel whole-key moves will fan
            // out per channel when those types are migrated.)
            if (TimeLine::usesChannelTracks(target.pos.type()) && target.pos.component() < 0) {
                target.pos.setComponent(0);
            }
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
            const int component = target.pos.component() == TimeKeyPos::kStateComponent
                ? TimeKeyPos::kStateComponent
                : target.pos.type() == TimeKeyType_Image && target.pos.component() >= 0
                    ? target.pos.component() : -1;
            for (Context& candidate : mContexts) {
                if (candidate.line == line && candidate.type == target.pos.type()
                    && candidate.component == component) {
                    context = &candidate;
                    break;
                }
            }
            if (!context) {
                Context fresh;
                fresh.line = line;
                fresh.type = target.pos.type();
                fresh.component = component;
                if (component == TimeKeyPos::kStateComponent) {
                    fresh.map = &line->stateMap(fresh.type);
                } else if (component >= 0) {
                    fresh.map = &line->channelMap(fresh.type, component);
                } else if (TimeLine::usesChannelTracks(fresh.type)) {
                    fresh.map = &line->channelMap(fresh.type, 0);
                } else {
                    fresh.map = &line->map(fresh.type);
                }
                mContexts.append(fresh);
                context = &mContexts.last();
            }
            context->targetIndices.append(i);
        }

        for (Context& context : mContexts) {
            const TimeLine::MapType& map = *context.map;

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
            const TimeLine::MapType& map = *context.map;
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
            const TimeLine::MapType& map = *context.map;

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
                const bool success = context.component == TimeKeyPos::kStateComponent
                    ? context.line->moveStateKeys(context.type, moves)
                    : context.component >= 0
                        ? context.line->moveChannelKeys(context.type, context.component, moves)
                        : context.line->moveKeys(context.type, moves);
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
            const TimeLine::MapType& map = *context.map;
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
                const bool success = context.component == TimeKeyPos::kStateComponent
                    ? context.line->moveStateKeys(context.type, moves)
                    : context.component >= 0
                        ? context.line->moveChannelKeys(context.type, context.component, moves)
                        : context.line->moveKeys(context.type, moves);
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

            const TimeLine::MapType& map = *context.map;
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
    void setFrameLinked(
        Project& aProject, ObjectNode& aTarget, TimeKeyType aType, int aFrame, bool aLinked
    ) {
        TimeLine* line = aTarget.timeLine();
        if (!line || !TimeLine::usesChannelTracks(aType)) {
            return;
        }
        const PropertyTrack* prop = line->property(aType);
        if (!prop || (aLinked && !line->canLinkFrame(aType, aFrame))) {
            return;
        }

        struct Prev {
            int component;
            bool linked;
        };
        QVector<Prev> previous;
        for (int c = 0; c < prop->channelCount(); ++c) {
            const ChannelTrack* track = prop->channel(c);
            const ChannelKey* key = track ? track->keyAt(aFrame) : nullptr;
            if (key) {
                previous.push_back({c, key->linked()});
            }
        }
        if (previous.isEmpty()) {
            return;
        }

        cmnd::ScopedMacro macro(
            aProject.commandStack(),
            aLinked ? CmndName::tr("Link Channels") : CmndName::tr("Separate Channels")
        );
        auto* notifier = new Notifier(aProject);
        notifier->event().setType(TimeLineEvent::Type_ChangeKeyValue);
        for (const Prev& prev : previous) {
            notifier->event().pushTarget(
                aTarget, TimeKeyPos(*line, aType, aFrame, prev.component)
            );
        }
        macro.grabListener(notifier);

        const auto restore = [line, aType, aFrame, previous]() {
            for (const Prev& prev : previous) {
                if (ChannelKey* key = line->channelKey(aType, prev.component, aFrame)) {
                    key->setLinked(prev.linked);
                }
            }
        };
        const auto apply = [line, aType, aFrame, aLinked, previous]() {
            for (const Prev& prev : previous) {
                if (ChannelKey* key = line->channelKey(aType, prev.component, aFrame)) {
                    key->setLinked(aLinked);
                }
            }
        };
        aProject.commandStack().push(new cmnd::Delegatable(apply, restore));
    }

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
            if (!key || key->isBaseValue() || seen.contains(key))
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
        tKey* key = (tKey*)(aFrame == TimeLine::kBaseValueIndex ? aTarget.timeLine()->baseValue(tType)
                                                                 : aTarget.timeLine()->timeKey(tType, aFrame));
        XC_PTR_ASSERT(key);

        // Capture the proportional followers BEFORE the value write. The command
        // itself is pushed AFTER the edit, so exec applies them after the edited
        // component and undo restores them before it.
        ReflowValuesCommand* reflow = nullptr;
        if (aFrame != TimeLine::kBaseValueIndex) {
            reflow = new ReflowValuesCommand(*aTarget.timeLine(), tType, *key);
        }

        {
            cmnd::ScopedMacro macro(aProject.commandStack(), aText);

            auto notifier = new Notifier(aProject);
            notifier->event().setType(TimeLineEvent::Type_ChangeKeyValue);
            if (aFrame == TimeLine::kBaseValueIndex) {
                notifier->event().pushBaseValueTarget(aTarget, tType);
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
        tKey* key = (tKey*)(aFrame == TimeLine::kBaseValueIndex ? aTarget.timeLine()->baseValue(tType)
                                                                 : aTarget.timeLine()->timeKey(tType, aFrame));
        XC_PTR_ASSERT(key);

        {
            cmnd::ScopedMacro macro(aProject.commandStack(), aText);

            auto notifier = new Notifier(aProject);
            notifier->event().setType(TimeLineEvent::Type_ChangeKeyValue);
            if (aFrame == TimeLine::kBaseValueIndex) {
                notifier->event().pushBaseValueTarget(aTarget, tType);
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
        tKey* key = (tKey*)(aFrame == TimeLine::kBaseValueIndex ? aTarget.timeLine()->baseValue(tType)
                                                                 : aTarget.timeLine()->timeKey(tType, aFrame));
        XC_PTR_ASSERT(key);

        // Same capture-before / push-after rule as assignKeyData: aFunc writes the
        // edited key, then the followers reflow inside the same macro.
        ReflowValuesCommand* reflow = nullptr;
        if (aFrame != TimeLine::kBaseValueIndex) {
            reflow = new ReflowValuesCommand(*aTarget.timeLine(), tType, *key);
        }

        {
            cmnd::ScopedMacro macro(aProject.commandStack(), aText);

            auto notifier = new Notifier(aProject);
            notifier->event().setType(TimeLineEvent::Type_ChangeKeyValue);
            if (aFrame == TimeLine::kBaseValueIndex) {
                notifier->event().pushBaseValueTarget(aTarget, tType);
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
            aFrame == TimeLine::kBaseValueIndex ? CmndName::tr("Update position")
                                                 : CmndName::tr("Update position key")
        );
    }

    void assignRotateKeyData(Project& aProject, ObjectNode& aTarget, int aFrame, const RotateKey::Data& aNewData) {
        assignKeyData<RotateKey, TimeKeyType_Rotate>(
            aProject,
            aTarget,
            aFrame,
            aNewData,
            aFrame == TimeLine::kBaseValueIndex ? CmndName::tr("Update rotation")
                                                 : CmndName::tr("Update rotation key")
        );
    }

    void assignScaleKeyData(Project& aProject, ObjectNode& aTarget, int aFrame, const ScaleKey::Data& aNewData) {
        assignKeyData<ScaleKey, TimeKeyType_Scale>(
            aProject,
            aTarget,
            aFrame,
            aNewData,
            aFrame == TimeLine::kBaseValueIndex ? CmndName::tr("Update scale")
                                                 : CmndName::tr("Update scale key")
        );
    }

    void assignDepthKeyData(Project& aProject, ObjectNode& aTarget, int aFrame, const DepthKey::Data& aNewData) {
        assignKeyData<DepthKey, TimeKeyType_Depth>(
            aProject,
            aTarget,
            aFrame,
            aNewData,
            aFrame == TimeLine::kBaseValueIndex ? CmndName::tr("Update depth")
                                                 : CmndName::tr("Update depth key")
        );
    }

    void assignOpacityKeyData(Project& aProject, ObjectNode& aTarget, int aFrame, const OpacityKey::Data& aNewData) {
        assignKeyData<OpacityKey, TimeKeyType_Opacity>(
            aProject,
            aTarget,
            aFrame,
            aNewData,
            aFrame == TimeLine::kBaseValueIndex ? CmndName::tr("Update opacity")
                                                 : CmndName::tr("Update opacity key")
        );
    }

    void assignHSVKeyData(Project& aProject, ObjectNode& aTarget, int aFrame, const HSVKey::Data& aNewData) {
        assignKeyData<HSVKey, TimeKeyType_HSV>(
            aProject,
            aTarget,
            aFrame,
            aNewData,
            aFrame == TimeLine::kBaseValueIndex ? CmndName::tr("Update HSV")
                                                 : CmndName::tr("Update HSV key")
        );
    }

    void assignBlurKeyData(Project& aProject, ObjectNode& aTarget, int aFrame, const BlurKey::Data& aNewData) {
        assignKeyData<BlurKey, TimeKeyType_Blur>(
            aProject,
            aTarget,
            aFrame,
            aNewData,
            aFrame == TimeLine::kBaseValueIndex ? CmndName::tr("Update blur")
                                                 : CmndName::tr("Update blur key")
        );
    }

    void assignWaveKeyData(Project& aProject, ObjectNode& aTarget, int aFrame, const WaveKey::Data& aNewData) {
        assignKeyData<WaveKey, TimeKeyType_Wave>(
            aProject,
            aTarget,
            aFrame,
            aNewData,
            aFrame == TimeLine::kBaseValueIndex ? CmndName::tr("Update wave")
                                                 : CmndName::tr("Update wave key")
        );
    }

    void assignPoseKeyEasing(Project& aProject, ObjectNode& aTarget, int aFrame, const util::Easing::Param& aNewData) {
        assignKeyEasing<PoseKey, TimeKeyType_Pose>(
            aProject,
            aTarget,
            aFrame,
            aNewData,
            aFrame == TimeLine::kBaseValueIndex ? CmndName::tr("Update pose")
                                                 : CmndName::tr("Update pose key")
        );
    }

    void assignFFDKeyEasing(Project& aProject, ObjectNode& aTarget, int aFrame, const util::Easing::Param& aNewData) {
        assignKeyEasing<FFDKey, TimeKeyType_FFD>(
            aProject,
            aTarget,
            aFrame,
            aNewData,
            aFrame == TimeLine::kBaseValueIndex ? CmndName::tr("Update FFD")
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
            aFrame == TimeLine::kBaseValueIndex ? CmndName::tr("Update image")
                                                 : CmndName::tr("Update image key"),
            [&](ImageKey* aKey) {
                // image key
                aProject.commandStack().push(ImageKeyUpdater::createResourceUpdater(
                    *aKey, aTarget, aProject, aNewData, workspace, createTransitions
                ));

                // ffd key should be called finally
                if (createTransitions) {
                    aProject.commandStack().push(FFDKeyUpdater::createResourceUpdater(aTarget, workspace));
                }
            }
        );
    }

    void assignImageKeyOffset(
        Project& aProject, ObjectNode& aTarget, int aFrame, const QVector2D& aNewData, int aComponent
    ) {
        TimeLine& line = *aTarget.timeLine();
        PropertyTrack* prop = line.property(TimeKeyType_Image);
        if (!prop || !prop->channel(0) || !prop->channel(1)) {
            return;
        }

        const auto components = aComponent < 0
            ? QVector<int>{ 0, 1 }
            : QVector<int>{ aComponent == 0 ? 0 : 1 };

        if (aFrame == TimeLine::kBaseValueIndex) {
            const float previous[2] = {
                prop->channel(0)->baseValue(), prop->channel(1)->baseValue()
            };
            auto* baseKey = static_cast<ImageKey*>(line.baseValue(TimeKeyType_Image));
            const QVector2D previousImage = baseKey ? baseKey->data().imageOffset() : QVector2D();
            cmnd::ScopedMacro macro(aProject.commandStack(), CmndName::tr("Update image offset"));
            auto* notifier = new Notifier(aProject);
            notifier->event().setType(TimeLineEvent::Type_ChangeKeyValue);
            notifier->event().pushBaseValueTarget(aTarget, TimeKeyType_Image);
            macro.grabListener(notifier);
            core::TimeLine* live = &line;
            ImageKey* liveImage = baseKey;
            aProject.commandStack().push(new cmnd::Delegatable(
                [live, components, aNewData]() {
                    for (int c : components) {
                        live->setChannelBaseValue(TimeKeyType_Image, c, c == 0 ? aNewData.x() : aNewData.y());
                    }
                },
                [live, components, previous]() {
                    for (int c : components) {
                        live->setChannelBaseValue(TimeKeyType_Image, c, previous[c]);
                    }
                }
            ));
            if (liveImage) {
                aProject.commandStack().push(new cmnd::Delegatable(
                    [liveImage, aNewData]() { liveImage->data().setImageOffset(aNewData); },
                    [liveImage, previousImage]() { liveImage->data().setImageOffset(previousImage); }
                ));
            }
            return;
        }

        const QVector2D current(
            prop->channel(0)->clamp(prop->channel(0)->quantize(aNewData.x())),
            prop->channel(1)->clamp(prop->channel(1)->quantize(aNewData.y()))
        );
        cmnd::ScopedMacro macro(aProject.commandStack(), CmndName::tr("Update image offset key"));
        auto* notifier = new Notifier(aProject);
        notifier->event().setType(TimeLineEvent::Type_ChangeKeyValue);
        for (int c : components) {
            notifier->event().pushTarget(
                aTarget, TimeKeyPos(line, TimeKeyType_Image, aFrame, c)
            );
        }
        macro.grabListener(notifier);

        for (int c : components) {
            ChannelTrack* track = prop->channel(c);
            ChannelKey* key = track->keyAt(aFrame);
            if (!key) {
                key = new ChannelKey(TimeKeyType_Image, c);
                key->setFrame(aFrame);
                key->setValue(c == 0 ? current.x() : current.y());
                key->easing() = util::Easing::defaultParam();
                key->setLinked(true);
                aProject.commandStack().push(new cmnd::GrabNewObject<ChannelKey>(key));
                aProject.commandStack().push(
                    line.createChannelPusher(TimeKeyType_Image, c, aFrame, key)
                );
            }
            aProject.commandStack().push(new cmnd::Assign<float>(
                &key->valueRef(), c == 0 ? current.x() : current.y()
            ));
        }

        // Keep the legacy ImageKey field in step when a structural key exists at
        // this frame; playback reads the channel tracks, but the saved ImageKey
        // payload and older readers still name an offset.
        if (ImageKey* imageKey = static_cast<ImageKey*>(line.timeKey(TimeKeyType_Image, aFrame))) {
            const QVector2D prev = imageKey->data().imageOffset();
            const QVector2D next(
                aComponent < 0 || aComponent == 0 ? current.x() : prev.x(),
                aComponent < 0 || aComponent == 1 ? current.y() : prev.y()
            );
            aProject.commandStack().push(new cmnd::Delegatable(
                [imageKey, next]() { imageKey->data().setImageOffset(next); },
                [imageKey, prev]() { imageKey->data().setImageOffset(prev); }
            ));
        }
    }

    void assignImageKeyCellSize(core::Project& aProject, core::ObjectNode& aTarget, int aFrame, int aNewData) {
        ResourceUpdatingWorkspacePtr workspace = std::make_shared<ResourceUpdatingWorkspace>();
        const bool createTransitions = !aTarget.timeLine()->isEmpty(TimeKeyType_FFD);

        assignKeyBy<ImageKey, TimeKeyType_Image>(
            aProject,
            aTarget,
            aFrame,
            aFrame == TimeLine::kBaseValueIndex ? CmndName::tr("Update mesh spacing")
                                                 : CmndName::tr("Update mesh spacing key"),
            [&](ImageKey* aKey) {
                // image key
                aProject.commandStack().push(ImageKeyUpdater::createGridMeshUpdater(
                    *aKey, aTarget, aProject, aNewData, workspace, createTransitions
                ));

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

            appendNewKey(aProject, aTarget, aFrame, aKey, aParentKey);

            // Image is a structural key plus two offset channels. The property
            // dot keys BOTH in the same undo step: the ImageKey for resource/grid
            // and the offset tracks for the values playback reads.
            if (tType == TimeKeyType_Image) {
                auto* image = dynamic_cast<ImageKey*>(aKey);
                if (!image) {
                    return;
                }
                for (int c = 0; c < 2; ++c) {
                    auto* offset = new ChannelKey(TimeKeyType_Image, c);
                    offset->setFrame(aFrame);
                    offset->setValue(
                        c == 0 ? image->data().imageOffset().x()
                               : image->data().imageOffset().y()
                    );
                    offset->easing() = util::Easing::defaultParam();
                    offset->setLinked(true);
                    aProject.commandStack().push(new cmnd::GrabNewObject<ChannelKey>(offset));
                    aProject.commandStack().push(
                        aTarget.timeLine()->createChannelPusher(
                            TimeKeyType_Image, c, aFrame, offset
                        )
                    );
                    notifier->event().pushTarget(
                        aTarget, core::TimeKeyPos(*aTarget.timeLine(), TimeKeyType_Image, aFrame, c)
                    );
                }
            }
        }
    }

    // The COMMAND half of pushNewKey, with no macro and no notifier: the caller owns
    // both, so it decides how many keys are one undo step and which event names them.
    // "Add keys at playhead" on a layer with six animated channels is one step, not six -
    // which is what the tree's own action had to be, and the per-key macro could not
    // express. The key's own type() is the map it lands in.
    void appendNewKey(
        Project& aProject, ObjectNode& aTarget, int aFrame, TimeKey* aKey, TimeKey* aParentKey
    ) {
        XC_PTR_ASSERT(aKey);
        XC_ASSERT(aTarget.timeLine());
        auto& stack = aProject.commandStack();
        TimeLine& line = *aTarget.timeLine();

        if (auto* channelKey = dynamic_cast<ChannelKey*>(aKey)) {
            stack.push(new cmnd::GrabNewObject<TimeKey>(aKey));
            stack.push(line.createChannelPusher(
                aKey->type(), channelKey->component(), aFrame, channelKey
            ));
            return;
        }

        stack.push(new cmnd::GrabNewObject<TimeKey>(aKey));
        stack.push(line.createPusher(aKey->type(), aFrame, aKey));
        if (aParentKey) {
            stack.push(new cmnd::PushBackTree<TimeKey>(&aParentKey->children(), aKey));
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

    void pushNewOpacityKey(Project& aProject, ObjectNode& aTarget, int aFrame, OpacityKey* aKey) {
        pushNewKey<OpacityKey, TimeKeyType_Opacity>(aProject, aTarget, aFrame, aKey, CmndName::tr("Add new opacity key"));
    }

    void pushNewHSVKey(Project& aProject, ObjectNode& aTarget, int aFrame, HSVKey* aKey) {
        pushNewKey<HSVKey, TimeKeyType_HSV>(aProject, aTarget, aFrame, aKey, CmndName::tr("Add new HSV key"));
    }

    void pushNewBlurKey(Project& aProject, ObjectNode& aTarget, int aFrame, BlurKey* aKey) {
        pushNewKey<BlurKey, TimeKeyType_Blur>(aProject, aTarget, aFrame, aKey, CmndName::tr("Add new blur key"));
    }

    void pushNewWaveKey(Project& aProject, ObjectNode& aTarget, int aFrame, WaveKey* aKey) {
        pushNewKey<WaveKey, TimeKeyType_Wave>(aProject, aTarget, aFrame, aKey, CmndName::tr("Add new wave key"));
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

    //-------------------------------------------------------------------------------------------------
    namespace {
        void pushFilterStackCommand(Project& aProject, TimeLine& aLine, const QVector<FilterEntry>& aNext) {
            aProject.commandStack().push(
                new cmnd::ModifiableAssign<QVector<FilterEntry>>(&aLine.filterEntriesRef(), aNext)
            );
        }

        QVector<FilterEntry> filterStackWithout(const QVector<FilterEntry>& aStack, TimeKeyType aType) {
            QVector<FilterEntry> next;
            for (const FilterEntry& entry : aStack) {
                if (entry.type != aType)
                    next.append(entry);
            }
            return next;
        }


    } // namespace

    //-------------------------------------------------------------------------------------------------
    // THE NODE'S MATTE. The field lives in the KEY (not in a per-lane side table the
    // way a filter's painted mask does), so every edit is an ordinary key command and
    // undo comes from the value-replayable stack - nothing here needs a new command
    // type.

    namespace {

        // The key a paint would edit: the one at this frame, or the BASE VALUE. A
        // mask with keys and no key at this frame has nothing to paint - that is the
        // "read-only between keys" state the card shows, and this rule is behind it.
        MaskKey* maskKeyForEdit(TimeLine& aLine, int aFrame) {
            if (const TimeKey* atFrame = aLine.map(TimeKeyType_Mask).value(aFrame))
                return const_cast<MaskKey*>(static_cast<const MaskKey*>(atFrame));
            return static_cast<MaskKey*>(aLine.baseValue(TimeKeyType_Mask));
        }

        // Where a field lives, in the node's CONTENT space. The same two branches
        // ctrl::MaskEditor uses for the painted filter masks, so the layer tool and
        // the filter tool cannot disagree about what a content pixel is: a layer's
        // mask covers its IMAGE, a folder's covers the CANVAS (a folder has no image
        // of its own).
        QRectF maskContentRect(Project& aProject, ObjectNode& aTarget) {
            if (aTarget.type() == ObjectType_Layer) {
                const QSize contentSize = aTarget.timeLine()->current().areaTextureSize();
                if (!contentSize.isEmpty())
                    return QRectF(QPointF(0.0, 0.0), QSizeF(contentSize));
            }
            const QSize canvas = aProject.attribute().imageSize();
            if (canvas.isEmpty())
                return QRectF();
            return QRectF(QPointF(0.0, 0.0), QSizeF(canvas));
        }

        // A fresh field's grid is the CONTENT BOX ITSELF: one texel per content pixel, so a
        // painted matte has the artwork's own resolution (a quarter of its RGBA image). Computed
        // once per authored key, never per frame.
        QSize maskTexelSize(const QRectF& aRect) {
            if (aRect.isEmpty())
                return QSize(1, 1);
            return QSize(
                std::max(1, int(std::lround(aRect.width()))),
                std::max(1, int(std::lround(aRect.height())))
            );
        }

        // The BASE VALUE is a store of its own (TimeLine::setBaseValue owns the key
        // and deletes the one it replaces), so it is written by VALUE: a state is
        // "present, with this field" or "absent", and both undo and redo replay it.
        // That keeps the mask's add/remove value-replayable like every other command
        // without inventing pointer hand-over in setBaseValue.
        class AssignBaseValueMask: public cmnd::Stable {
        public:
            AssignBaseValueMask(TimeLine& aLine, bool aPresent, const MaskKey::Data& aData)
                : mLine(aLine), mPresent(aPresent), mData(aData) {}

            virtual bool redoable() const { return true; }
            virtual bool undoable() const { return true; }
            virtual void redo() { apply(mPresent, mData); }
            virtual void undo() { apply(!mPresent, MaskKey::Data()); }

        private:
            void apply(bool aPresent, const MaskKey::Data& aData) {
                MaskKey* existing = static_cast<MaskKey*>(mLine.baseValue(TimeKeyType_Mask));
                if (aPresent) {
                    if (existing) {
                        existing->data().assign(aData);
                    } else {
                        auto* key = new MaskKey();
                        key->data().assign(aData);
                        mLine.setBaseValue(TimeKeyType_Mask, key);
                    }
                } else if (existing) {
                    mLine.setBaseValue(TimeKeyType_Mask, nullptr);
                }
            }

            TimeLine& mLine;
            bool mPresent;
            MaskKey::Data mData;
        };

    } // namespace

    // One command that writes a key's whole field. The two states are captured, so
    // undo restores the pixels exactly (a mask is too big to re-derive).
    AssignMaskField::AssignMaskField(MaskKey* aKey, const MaskKey::Data& aNext)
        : mKey(aKey), mPrev(aKey->data()), mNext(aNext) {}

    AssignMaskField::AssignMaskField(MaskKey* aKey, const MaskKey::Data& aPrev, const MaskKey::Data& aNext)
        : mKey(aKey), mPrev(aPrev), mNext(aNext) {}

    bool AssignMaskField::redoable() const { return true; }
    bool AssignMaskField::undoable() const { return true; }
    void AssignMaskField::redo() { mKey->data().assign(mNext); }
    void AssignMaskField::undo() { mKey->data().assign(mPrev); }

    MaskKey* maskKeyForPaint(ObjectNode& aTarget, int aFrame) {
        if (!aTarget.timeLine())
            return nullptr;
        return maskKeyForEdit(*aTarget.timeLine(), aFrame);
    }

    void resetMaskField(Project& aProject, ObjectNode& aTarget, MaskKey* aKey) {
        if (!aKey || !aKey->data().isValid())
            return;
        MaskKey::Data next = aKey->data();
        QImage field = next.field();
        field.fill(MaskKey::kNeutral);
        const QRectF rect = next.contentRect();
        const util::Easing::Param easing = next.easing(0);
        next.write(field, rect);
        next.easing(0) = easing;

        cmnd::ScopedMacro macro(aProject.commandStack(), CmndName::tr("Clear mask"));
        auto* notifier = new Notifier(aProject);
        notifier->event().setType(TimeLineEvent::Type_ChangeKeyValue);
        notifier->event().pushTarget(aTarget, TimeKeyType_Mask, aKey->frame());
        macro.grabListener(notifier);
        aProject.commandStack().push(new AssignMaskField(aKey, next));
    }

    void addMask(Project& aProject, ObjectNode& aTarget) {
        if (!aTarget.timeLine())
            return;
        TimeLine& line = *aTarget.timeLine();
        if (!line.map(TimeKeyType_Mask).isEmpty() || line.baseValue(TimeKeyType_Mask))
            return; // one mask per node

        const QRectF rect = maskContentRect(aProject, aTarget);
        if (!rect.isValid() || rect.isEmpty())
            return; // nothing to mask yet (no image and no canvas)

        cmnd::ScopedMacro macro(aProject.commandStack(), CmndName::tr("Add mask"));
        auto* notifier = new Notifier(aProject);
        notifier->event().setType(TimeLineEvent::Type_PushKey);
        notifier->event().pushTarget(aTarget, TimeKeyType_Mask, TimeKey::kBaseValueIndex);
        macro.grabListener(notifier);

        MaskKey::Data data;
        data.alloc(maskTexelSize(rect), rect, MaskKey::kNeutral);
        aProject.commandStack().push(new AssignBaseValueMask(line, true, data));
    }

    void removeMask(Project& aProject, ObjectNode& aTarget) {
        if (!aTarget.timeLine())
            return;
        TimeLine& line = *aTarget.timeLine();
        const QVector<int> frames = line.map(TimeKeyType_Mask).keys();
        if (frames.isEmpty() && !line.baseValue(TimeKeyType_Mask))
            return;

        cmnd::ScopedMacro macro(aProject.commandStack(), CmndName::tr("Remove mask"));
        auto* notifier = new Notifier(aProject);
        notifier->event().setType(frames.isEmpty() ? TimeLineEvent::Type_RemoveKey
                                                   : TimeLineEvent::Type_RemoveKey);
        for (const int frame : frames)
            notifier->event().pushTarget(aTarget, TimeKeyType_Mask, frame);
        notifier->event().pushTarget(aTarget, TimeKeyType_Mask, TimeKey::kBaseValueIndex);
        macro.grabListener(notifier);

        for (const int frame : frames)
            aProject.commandStack().push(line.createRemover(TimeKeyType_Mask, frame));
        if (const MaskKey* key = static_cast<const MaskKey*>(line.baseValue(TimeKeyType_Mask))) {
            aProject.commandStack().push(new AssignBaseValueMask(line, false, key->data()));
        }
    }

    void invertMask(Project& aProject, ObjectNode& aTarget, int aFrame) {
        if (!aTarget.timeLine())
            return;
        TimeLine& line = *aTarget.timeLine();
        MaskKey* key = maskKeyForEdit(line, aFrame);
        if (!key || !key->data().isValid())
            return;

        MaskKey::Data next = key->data();
        QImage field = next.field();
        for (int y = 0; y < field.height(); ++y) {
            uchar* row = field.scanLine(y);
            for (int x = 0; x < field.width(); ++x)
                row[x] = uchar(255 - row[x]);
        }
        const QRectF rect = next.contentRect();
        const util::Easing::Param easing = next.easing(0);
        next.write(field, rect);
        next.easing(0) = easing;

        cmnd::ScopedMacro macro(aProject.commandStack(), CmndName::tr("Invert mask"));
        auto* notifier = new Notifier(aProject);
        notifier->event().setType(TimeLineEvent::Type_ChangeKeyValue);
        notifier->event().pushTarget(aTarget, TimeKeyType_Mask, key->frame());
        macro.grabListener(notifier);

        aProject.commandStack().push(new AssignMaskField(key, next));
    }

    void toggleMaskKey(Project& aProject, ObjectNode& aTarget, int aFrame) {
        if (!aTarget.timeLine())
            return;
        TimeLine& line = *aTarget.timeLine();
        if (line.map(TimeKeyType_Mask).value(aFrame)) {
            cmnd::ScopedMacro macro(aProject.commandStack(), CmndName::tr("Remove mask key"));
            auto* notifier = new Notifier(aProject);
            notifier->event().setType(TimeLineEvent::Type_RemoveKey);
            notifier->event().pushTarget(aTarget, TimeKeyType_Mask, aFrame);
            macro.grabListener(notifier);
            aProject.commandStack().push(line.createRemover(TimeKeyType_Mask, aFrame));
            return;
        }

        // A key HOLDS what the frame currently shows. Keying a mask between two keys
        // therefore does not change the picture - which is what makes keying a shape a
        // safe gesture rather than a jump.
        MaskKey* source = maskKeyForEdit(line, aFrame);
        if (!source || !source->data().isValid())
            return;

        cmnd::ScopedMacro macro(aProject.commandStack(), CmndName::tr("Add mask key"));
        auto* notifier = new Notifier(aProject);
        notifier->event().setType(TimeLineEvent::Type_PushKey);
        notifier->event().pushTarget(aTarget, TimeKeyType_Mask, aFrame);
        macro.grabListener(notifier);

        auto* key = new MaskKey();
        key->data().assign(source->data());
        aProject.commandStack().push(line.createPusher(TimeKeyType_Mask, aFrame, key));
    }

    void addFilter(Project& aProject, ObjectNode& aTarget, TimeKeyType aType) {
        if (!isFilterType(aType) || !aTarget.timeLine())
            return;
        TimeLine& line = *aTarget.timeLine();
        QVector<FilterEntry> next = line.filters();
        for (const FilterEntry& entry : next) {
            if (entry.type == aType)
                return; // already in the stack
        }
        next.append(FilterEntry(aType, true));

        cmnd::ScopedMacro macro(aProject.commandStack(), CmndName::tr("Add filter"));
        auto* notifier = new Notifier(aProject);
        notifier->event().setType(TimeLineEvent::Type_FilterStack);
        macro.grabListener(notifier);
        pushFilterStackCommand(aProject, line, next);

        // A filter starts STATIC: values live on its base value, the row's key
        // dot is hollow, and the timeline gains no keyframe until the dot is
        // pressed. This is the same model 2 rule the inspector's properties use.
        // A fresh filter starts at a DEFINED default, not at a stale default
        // left by an earlier add/remove cycle: HSV's and Blur's are their
        // identity (an adjustment at 0 / a radius at 0), a wave's is its own
        // visible preset because an inert wave would look broken on add. The
        // base value is structural (not part of the add command's undo); the
        // visible stack row and the explicit keys are what the command owns.
        const util::Easing::Param easing = util::Easing::defaultParam();
        if (aType == TimeKeyType_HSV) {
            auto* key = new HSVKey();
            for (int c = 0; c < HSVKey::kComponents; ++c)
                key->data().easing(c) = easing;
            line.setBaseValue(aType, key);
        } else if (aType == TimeKeyType_Wave) {
            // A wave's starter values are NOT its identity: an inert filter
            // would look broken on add. It starts as a gentle water surface
            // (WaveKey::Preset) and is animated by playback from the start,
            // because its phase advances with the document frame.
            auto* key = new WaveKey();
            key->data() = WaveKey::Data::presetData();
            for (int c = 0; c < WaveKey::kComponents; ++c)
                key->data().easing(c) = easing;
            line.setBaseValue(aType, key);
        } else {
            auto* key = new BlurKey();
            for (int c = 0; c < BlurKey::kComponents; ++c)
                key->data().easing(c) = easing;
            line.setBaseValue(aType, key);
        }
    }

    bool hasFilterMask(const ObjectNode& aTarget, TimeKeyType aType, FilterMaskChannel aChannel) {
        const TimeLine* line = aTarget.timeLine();
        if (!line)
            return false;
        const FilterMask* mask = line->filterMask(aType, aChannel);
        return mask && !mask->isEmpty();
    }

    void clearFilterMask(Project& aProject, ObjectNode& aTarget, TimeKeyType aType, FilterMaskChannel aChannel) {
        TimeLine* line = aTarget.timeLine();
        if (!line)
            return;
        FilterMask* mask = line->filterMask(aType, aChannel);
        if (!mask || mask->isEmpty())
            return; // nothing painted: clearing is not an edit

        cmnd::ScopedMacro macro(aProject.commandStack(), CmndName::tr("Clear filter mask"));
        auto* notifier = new Notifier(aProject);
        notifier->event().setType(TimeLineEvent::Type_FilterStack);
        macro.grabListener(notifier);

        // the command carries the pixels it overwrites, so ONE undo brings the field
        // back exactly (and marks it dirty, so the texture re-uploads)
        auto* command = new AssignPixels(*mask);
        command->captureBefore();
        mask->clear();
        command->addAfter(mask->image().rect());
        // the push fires the notifier (Notifier::onExecuted), which repaints
        aProject.commandStack().push(command);
    }

    void removeFilter(Project& aProject, ObjectNode& aTarget, TimeKeyType aType) {
        if (!isFilterType(aType) || !aTarget.timeLine())
            return;
        TimeLine& line = *aTarget.timeLine();
        const QVector<FilterEntry> next = filterStackWithout(line.filters(), aType);

        // The stack entry goes first, so an undo restores the keys (the removers'
        // undo) and then the entry - either order is value-replayable, but this is
        // the order the UI saw: the row disappears, then the keys are gone.
        cmnd::ScopedMacro macro(aProject.commandStack(), CmndName::tr("Remove filter"));
        auto* notifier = new Notifier(aProject);
        QVector<QPair<int, int>> removals; // (component, frame)
        if (TimeLine::usesChannelTracks(aType)) {
            for (int c = 0; c < propertyChannelCount(aType); ++c) {
                const ChannelTrack* track = line.channelTrack(aType, c);
                if (!track || track->keys().isEmpty()) {
                    continue;
                }
                for (auto itr = track->keys().constBegin(); itr != track->keys().constEnd(); ++itr) {
                    removals.append(qMakePair(c, itr.key()));
                }
            }
        } else {
            for (int frame : line.map(aType).keys()) {
                removals.append(qMakePair(-1, frame));
            }
        }
        if (removals.isEmpty()) {
            notifier->event().setType(TimeLineEvent::Type_FilterStack);
        } else {
            notifier->event().setType(TimeLineEvent::Type_RemoveKey);
            for (const auto& removal : removals) {
                if (removal.first < 0) {
                    notifier->event().pushTarget(aTarget, aType, removal.second);
                } else {
                    notifier->event().pushTarget(
                        aTarget, TimeKeyPos(line, aType, removal.second, removal.first)
                    );
                }
            }
        }
        macro.grabListener(notifier);
        pushFilterStackCommand(aProject, line, next);
        for (const auto& removal : removals) {
            if (removal.first < 0) {
                aProject.commandStack().push(line.createRemover(aType, removal.second, true));
            } else {
                aProject.commandStack().push(
                    line.createChannelRemover(aType, removal.first, removal.second, true)
                );
            }
        }
    }

    void moveFilter(Project& aProject, ObjectNode& aTarget, TimeKeyType aType, int aIndex) {
        if (!isFilterType(aType) || !aTarget.timeLine())
            return;
        TimeLine& line = *aTarget.timeLine();
        QVector<FilterEntry> next = line.filters();
        int from = -1;
        for (int i = 0; i < next.size(); ++i) {
            if (next.at(i).type == aType) {
                from = i;
                break;
            }
        }
        if (from < 0)
            return;
        const int to = qBound(0, aIndex, next.size() - 1);
        if (from == to)
            return;
        const FilterEntry entry = next.takeAt(from);
        next.insert(to, entry);

        cmnd::ScopedMacro macro(aProject.commandStack(), CmndName::tr("Reorder filters"));
        auto* notifier = new Notifier(aProject);
        notifier->event().setType(TimeLineEvent::Type_FilterStack);
        macro.grabListener(notifier);
        pushFilterStackCommand(aProject, line, next);
    }

    void setFilterEnabled(Project& aProject, ObjectNode& aTarget, TimeKeyType aType, bool aEnabled) {
        if (!isFilterType(aType) || !aTarget.timeLine())
            return;
        TimeLine& line = *aTarget.timeLine();
        QVector<FilterEntry> next = line.filters();
        bool found = false;
        for (FilterEntry& entry : next) {
            if (entry.type == aType) {
                if (entry.enabled == aEnabled)
                    return;
                entry.enabled = aEnabled;
                found = true;
                break;
            }
        }
        if (!found)
            return;

        cmnd::ScopedMacro macro(aProject.commandStack(), CmndName::tr("Toggle filter"));
        auto* notifier = new Notifier(aProject);
        notifier->event().setType(TimeLineEvent::Type_FilterStack);
        macro.grabListener(notifier);
        pushFilterStackCommand(aProject, line, next);
    }

} // namespace TimeLineUtil

} // namespace ctrl
