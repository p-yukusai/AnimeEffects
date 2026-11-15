#include "core/ResourceUpdatingWorkspace.h"
#include "ctrl/time/time_BodyModel.h"
#include <algorithm>
#include <climits>
#include <QSet>
#include "core/BlurKey.h"
#include "core/WaveKey.h"
#include "core/KeyEasing.h"
#include "core/KeyEasingAccess.h"
#include "core/TimeKeyExpans.h"
#include "core/TimeKeyBlender.h"
#include "ffd/ffd_Target.h"
#include "qapplication.h"
#include "qclipboard.h"
#include "qjsonarray.h"
#include "qjsondocument.h"
#include "qjsonobject.h"
#include "cmnd/ScopedMacro.h"
#include "cmnd/BasicCommands.h"
#include "ctrl/TimeLineEditor.h"
#include "ctrl/CmndName.h"
#include "ctrl/time/time_Renderer.h"
#include "core/FFDKeyUpdater.h"

using namespace core;

namespace {
    constexpr int kDefaultFrameCount = 600;
    const util::Range kDefaultFrameRange(1, kDefaultFrameCount);

} // namespace

namespace ctrl {

//-------------------------------------------------------------------------------------------------
TimeLineEditor::TimeLineEditor():
    // Members are initialised in DECLARATION order, not in the order written
    // here — so this list is written in declaration order. A list that disagrees
    // is a lie waiting to matter, and here it does matter: mMarquee takes mRows
    // and mTimeScale, both declared before it, so the references it is given are
    // only guaranteed live because declaration order happens to be right.
    mProject(),
    mRows(),
    mSelectingRow(),
    mFrameRange(),
    mFps(60),
    mState(State_Standby),
    mTimeCurrent(kTimeLineMargin),
    mTimeScale(),
    mMarquee(mRows, mTimeScale, kTimeLineMargin),
    mSelection(),
    mMoveRef(),
    mSelectionTemporary(false),
    mMoveFrame(),
    mPressFrame(),
    mOnUpdatingKey(false),
    mMarqueeMode(Marquee_Replace),
    mPreGestureSelection(),
    mPressedTarget(),
    mPressedTargetValid(false),
    mPlayWindow(),
    mWindowAnchorFrame(0),
    mWindowDragged(false) {
    mRows.reserve(64);

    mTimeScale.setFps(mFps);

    // reset frame range
    setFrameRange(kDefaultFrameRange);
}

void TimeLineEditor::setFrameRange(const util::Range& aRange) {
    mFrameRange = aRange;
    mTimeScale.setFrameEnd(mFrameRange.max());
    // The playhead starts at the range's first frame (1 for the default
    // [1, frameEnd] range — frames are user-facing and 1-indexed).
    mTimeCurrent.setFrame(mTimeScale, Frame(mFrameRange.min()));
}

void TimeLineEditor::setProject(Project* aProject) {
    clearRows();
    mProject.reset();
    // The window is a viewport gesture over the current project's frame
    // range; a different project invalidates it.
    mPlayWindow.clear();

    if (aProject) {
        mProject = aProject->pointee();
        setFrameRange(mProject->attribute().frameRange());
        mFps = mProject->attribute().fps();
        mTimeScale.setFps(mFps);
    } else {
        setFrameRange(kDefaultFrameRange);
    }
}

void TimeLineEditor::clearRows() {
    mRows.clear();
    clearSelection();
}

void TimeLineEditor::resetRows() {
    // A row rebuild is a VIEW change (an expand/collapse, a scroll sync, a tree
    // reshuffle). The selection belongs to the document, so it survives; the caller
    // prunes only what the new rows do not show (retainSelectionInRows).
    mRows.clear();
    clearState();
}

bool TimeLineEditor::retainSelectionInRows() {
    // A closed folder's descendants are drawn in its summary lane, so they are still
    // visible: the predicate inserts every node the rebuilt rows REPRESENT, not only
    // the rows' own nodes. That is what makes "collapse the folder that contains the
    // selected layer" keep the selection while its summary shows the keys, and makes
    // "collapse the root" (no rows at all) clear it.
    QSet<core::ObjectNode*> visible;
    for (const TimeLineRow& row : mRows) {
        if (!row.node)
            continue;
        visible.insert(row.node);
        if (!row.closedFolder)
            continue;
        ObjectNode::Iterator itr(row.node);
        while (itr.hasNext())
            visible.insert(itr.next());
    }
    return mSelection.retain([&visible](const time::Selection::Item& aItem) {
        return aItem.target.node && visible.contains(aItem.target.node);
    });
}

// Gesture state only — the selection survives (it is the visual anchor of
// what is selected; the marquee box is transient).
bool TimeLineEditor::extendMarquee(const QPoint& aWorldPoint) {
    if (mState != State_EncloseKeys || !mMarquee.isActive())
        return false;
    mMarquee.update(aWorldPoint);
    applyMarquee();
    return true;
}

void TimeLineEditor::clearState() {
    mState = State_Standby;
    mMoveRef = nullptr;
    mMoveFrame = 0;
    mPressFrame = 0;
    mMarquee.clear();
    mMarqueeMode = Marquee_Replace;
    mPreGestureSelection = core::TimeLineEvent();
    mPressedTarget = core::TimeLineEvent::Target();
    mPressedTargetValid = false;
    mWindowDragged = false;
}

void TimeLineEditor::clearSelection() {
    mSelection.clear();
    clearState();
}

void TimeLineEditor::pushRow(ObjectNode* aNode, util::Range aWorldTB, bool aClosedFolder) {
    // The lane's vertical band on the content axis. Only top/height are read
    // (vertical-band culling and keyHeight); the rect's x/width are unused —
    // the lane fill, seams and labels key off the camera rect, keys render at
    // margin + spacing*frame, and hit-testing uses frame ranges. Keep a fixed
    // gutter x so the rect does not read as encoding a horizontal span.
    const QRect rect(kTimeLineMargin, aWorldTB.min(), 1, aWorldTB.diff() + 1);
    mRows.push_back(TimeLineRow(aNode, rect, aClosedFolder, aNode == mSelectingRow));
}

void TimeLineEditor::updateRowSelection(const ObjectNode* aRepresent) {
    mSelectingRow = aRepresent;
    for (auto& row : mRows) {
        row.selecting = (row.node && row.node == aRepresent);
    }
}

void TimeLineEditor::updateKey() {
    // a timeline modification can leave stored targets stale (keys moved,
    // removed or recreated): follow the moves, drop the dead entries, and
    // keep the rest. Data-only edits like easing changes leave every entry
    // valid, so they keep the selection.
    mSelection.refresh();
}

void TimeLineEditor::selectCreatedKeys(const core::TimeLineEvent& aEvent) {
    // Only live explicit-key targets map to real timeline keys. Rebuild the
    // event through pos.isExist() so a stale index (a notification that named a
    // frame the key no longer occupies) can never inject a null-key entry into
    // the selection — it is simply skipped. Default-key payloads
    // (kDefaultKeyIndex) carry nothing selectable in the lane maps, so they
    // leave the current selection untouched rather than clearing it.
    core::TimeLineEvent live;
    for (const core::TimeLineEvent::Target& target : aEvent.targets()) {
        if (target.pos.isExist()) {
            live.pushTarget(*target.node, target.pos);
        }
    }
    if (live.targets().isEmpty())
        return;

    // Replace the whole selection with the created keys, then drop gesture and
    // temporary-selection state so a stale press/marquee target or a right-click
    // temporary loan cannot anchor future drags on the fresh selection.
    mSelection.set(live);
    mSelectionTemporary = false;
    clearState();
}

void TimeLineEditor::updateProjectAttribute() {
    clearState(); // fps/range changes keep the targets valid
    if (mProject) {
        const util::Range newRange = mProject->attribute().frameRange();
        if (mFrameRange != newRange) {
            // The window is a viewport gesture over the current project's
            // frame range; a range change invalidates it (like a project
            // switch), so a stale window cannot override playback outside
            // the newly configured range.
            mPlayWindow.clear();
            setFrameRange(newRange);
        }

        const int newFps = mProject->attribute().fps();
        if (mFps != newFps) {
            mFps = newFps;
            mTimeScale.setFps(mFps);
        }
    }
}

bool TimeLineEditor::updatePlayWindow(PlayWindowStep aStep, core::Frame aFrame) {
    // The window is clamped into the project frame range: playback previews
    // frames the project owns, never content beyond its extent (which stays
    // reachable through free navigation). The clamp is part of the gesture, so
    // every caller gets it - the timeline's ruler and the graph's band alike.
    const int frame = qBound(mFrameRange.min(), aFrame.get(), mFrameRange.max());
    const PlayWindow before = mPlayWindow;

    switch (aStep) {
    case PlayWindowStep::Begin:
        // A press only ANCHORS: nothing is drawn until the first drag, so a
        // Shift-click can dismiss an existing window without flashing it.
        mWindowAnchorFrame = frame;
        mWindowDragged = false;
        break;
    case PlayWindowStep::Drag:
        mPlayWindow.set(mWindowAnchorFrame, frame);
        mWindowDragged = true;
        break;
    case PlayWindowStep::End:
        // A click in disguise dismisses the window: a press and release with no
        // drag, or a drag that collapsed to a single frame (easy at max zoom -
        // and a 1-frame active window would gate playback to that one frame
        // while rendering nothing). A drag keeps what it set.
        if (!mWindowDragged || mPlayWindow.start == mPlayWindow.end)
            mPlayWindow.clear();
        mWindowDragged = false;
        break;
    }

    return before.start != mPlayWindow.start || before.end != mPlayWindow.end;
}

TimeLineEditor::UpdateFlags TimeLineEditor::updateCursor(const AbstractCursor& aCursor, Qt::KeyboardModifiers aModifiers) {
    UpdateFlags flags = 0;

    if (!mProject) {
        return flags;
    }

    const QPoint worldPoint = aCursor.worldPoint();
    const bool ctrl = aModifiers.testFlag(Qt::ControlModifier);
    const bool shift = aModifiers.testFlag(Qt::ShiftModifier);

    if (aCursor.emitsLeftPressedEvent()) {
        const QVector2D handlePos(mTimeCurrent.handlePos());

        // Shift on the ruler is the temporary playback window gesture: press
        // anchors it (nothing shows yet), the first drag materializes it, a
        // click dismisses it. It takes precedence over the playhead handle
        // and the header seek.
        if (shift && isOnRuler(aCursor.screenPos().y())) {
            // The same three steps the graph panel's band drives (see
            // updatePlayWindow): the clamp into the project frame range and the
            // click/drag ending rule live there, once.
            updatePlayWindow(PlayWindowStep::Begin, Frame(mTimeScale.frame(worldPoint.x() - kTimeLineMargin)));
            mState = State_SetWindow;
        } else if ((aCursor.screenPos() - handlePos).length() < mTimeCurrent.handleRange()) {
            // playhead: handle or header click moves the current frame
            mState = State_MoveCurrent;
            flags |= UpdateFlag_ModView;
        } else if (isOnRuler(aCursor.screenPos().y())) {
            // Navigation is free: a header click seeks to the clicked frame
            // even when the temporary playback window is active (the window
            // only constrains the playback clock, not the playhead).
            mTimeCurrent.setHandlePos(mTimeScale, aCursor.worldPos().toPoint());
            mState = State_MoveCurrent;
            flags |= UpdateFlag_ModView;
            flags |= UpdateFlag_ModFrame;
        } else {
            const time::Hit hit = mMarquee.hitTest(worldPoint, radiiFactor);

            if (hit.isValid()) {
                // clicked a key: standard creative-tool selection conventions.
                // plain click selects only this key (collapse); shift-click
                // and ctrl-click both toggle membership
                // a fresh press never inherits a previous gesture's pending
                // collapse target (an aborted move can leave one behind)
                mPressedTargetValid = false;
                const TimeLineEvent::Target target(*hit.node, hit.pos, hit.pos.index());
                if (shift || ctrl) {
                    mSelection.toggle(target);
                    flags |= UpdateFlag_ModView;
                } else {
                    // plain click: select this key. If it is already in the
                    // selection, keep the group for a pending drag-move and
                    // collapse to just this key on a click (no drag).
                    mPressedTarget = target;
                    mPressedTargetValid = true;
                    if (!mSelection.contains(target)) {
                        TimeLineEvent single;
                        single.pushTarget(*hit.node, hit.pos);
                        mSelection.set(single);
                        flags |= UpdateFlag_ModView;
                    }
                }
                mPressFrame = mTimeScale.frame(worldPoint.x() - kTimeLineMargin);
                mState = State_MoveKeys; // move is armed lazily on first drag
            } else {
                // empty space: marquee. plain replaces the selection, shift
                // adds to it, ctrl subtracts from it
                mMarquee.begin(worldPoint);
                mMarqueeMode = ctrl ? Marquee_Subtract : (shift ? Marquee_Add : Marquee_Replace);
                if (mMarqueeMode != Marquee_Replace) {
                    mPreGestureSelection = core::TimeLineEvent();
                    mSelection.assign(mPreGestureSelection);
                } else {
                    mSelection.clear();
                    flags |= UpdateFlag_ModView;
                }
                mState = State_EncloseKeys;
            }
        }
    } else if (aCursor.emitsLeftDraggedEvent()) {
        if (mState == State_SetWindow) {
            updatePlayWindow(PlayWindowStep::Drag, Frame(mTimeScale.frame(worldPoint.x() - kTimeLineMargin)));
            flags |= UpdateFlag_ModView;
        } else if (mState == State_MoveCurrent) {
            mTimeCurrent.setHandlePos(mTimeScale, aCursor.worldPos().toPoint());
            flags |= UpdateFlag_ModView;
            flags |= UpdateFlag_ModFrame;
        } else if (mState == State_MoveKeys) {
            if (!mMoveRef) {
                if (!beginMoveKeys()) {
                    mState = State_Standby;
                }
            }
            if (mState == State_MoveKeys && !modifyMoveKeys(aCursor.worldPoint())) {
                mState = State_Standby;
                mMoveRef = nullptr;
                flags |= UpdateFlag_ModView;
            }
        } else if (mState == State_EncloseKeys) {
            mMarquee.update(aCursor.worldPoint());
            applyMarquee();
            flags |= UpdateFlag_ModView;
        }
    } else if (aCursor.emitsLeftReleasedEvent()) {
        if (mState == State_SetWindow) {
            // The ending rule (a click, or a drag collapsed to one frame,
            // dismisses; a real drag keeps what it set) lives with the gesture
            // in updatePlayWindow, because the graph's band ends it too.
            updatePlayWindow(PlayWindowStep::End, Frame(mTimeScale.frame(worldPoint.x() - kTimeLineMargin)));
            mState = State_Standby;
            flags |= UpdateFlag_ModView;
        } else if (mState == State_MoveKeys) {
            // keys moved: re-resolve the selection to their new frames so
            // later copy/delete/move target the right keys
            if (mMoveRef) {
                mSelection.refresh();
                mMoveRef = nullptr;
            } else if (mPressedTargetValid) {
                // a plain click (no drag) always leaves exactly the clicked
                // key selected — idempotent when it was already alone
                TimeLineEvent single;
                single.pushTarget(*mPressedTarget.node, mPressedTarget.pos);
                mSelection.set(single);
                flags |= UpdateFlag_ModView;
            }
            clearState();
        } else if (mState == State_EncloseKeys) {
            const QRect box = mMarquee.rect();
            const bool degenerate = box.width() < 2 && box.height() < 2;
            // a bare modifier click on empty space: ctrl deselects, shift
            // keeps the selection; plain already cleared at press
            if (degenerate && mMarqueeMode == Marquee_Subtract) {
                mSelection.clear();
            }
            clearState(); // the box is transient; the selection highlights remain
            // ...and a repaint is what takes it down: the drag repainted the box
            // on every move, so a release that clears the state without asking
            // for a view update leaves the dashed box on the surface until
            // something else happens to repaint it (measured 2026-10-11: a plain
            // release left the box drawn until the next scroll - the reported
            // "stays drawn after the button is released").
            flags |= UpdateFlag_ModView;
        }
    } else {
        // hover: nothing to do — selection lives in the Selection object
    }

    return flags;
}

// Lazy move arming: called on the first drag of a State_MoveKeys gesture. The
// command is only pushed once the user actually drags, so a bare click leaves
// no phantom "Move keys" history entry. The delta is measured from the press
// frame, which was recorded when the gesture began.
bool TimeLineEditor::beginMoveKeys() {
    bool success = false;
    mOnUpdatingKey = true;
    {
        auto notifier = new TimeLineUtil::Notifier(*mProject);
        notifier->event().setType(TimeLineEvent::Type_MoveKey);
        mSelection.assign(notifier->event());

        if (!notifier->event().targets().isEmpty()) {
            cmnd::ScopedMacro macro(mProject->commandStack(), CmndName::tr("Move keys"));

            macro.grabListener(notifier);
            mMoveRef = new TimeLineUtil::MoveFrameOfKey(notifier->event());
            mProject->commandStack().push(mMoveRef);
            mMoveFrame = mPressFrame;
            success = true;
        } else {
            delete notifier;
            mMoveRef = nullptr;
        }
    }
    mOnUpdatingKey = false;
    return success;
}

bool TimeLineEditor::modifyMoveKeys(const QPoint& aWorldPos) {
    if (mProject->commandStack().isModifiable(mMoveRef)) {
        const int newFrame = mTimeScale.frame(aWorldPos.x() - kTimeLineMargin);
        const int addFrame = newFrame - mMoveFrame;
        TimeLineEvent modEvent;

        mOnUpdatingKey = true;
        int clampedAdd = addFrame;
        // Keys may move to any frame; the frame range is playback/export
        // territory and does not constrain the keying system. Conflicts are
        // still rejected by modifyMove (occupying a frame with an existing
        // key fails).
        if (mMoveRef->modifyMove(modEvent, addFrame,
            util::Range(INT_MIN, INT_MAX), &clampedAdd)) {
            mMoveFrame = newFrame;
            mProject->onTimeLineModified(modEvent, false);
        }
        mOnUpdatingKey = false;
        return true;
    }
    return false;
}

// The marquee box is committed into the selection on every drag update, so
// the highlights stay live while dragging and the box itself is transient.
void TimeLineEditor::applyMarquee() {
    if (mState != State_EncloseKeys)
        return;

    TimeLineEvent box;
    mMarquee.gather(box);

    if (mMarqueeMode == Marquee_Replace) {
        mSelection.set(box);
    } else if (mMarqueeMode == Marquee_Add) {
        TimeLineEvent merged = mPreGestureSelection;
        for (const TimeLineEvent::Target& t : box.targets()) {
            merged.pushTarget(*t.node, t.pos);
        }
        mSelection.set(merged);
    } else {
        mSelection.subtract(box);
    }
}

bool TimeLineEditor::selectKeysAt(TimeLineEvent& aEvent, const QPoint& aPos) {
    const time::Hit hit = mMarquee.hitTest(aPos, radiiFactor);
    if (!hit.isValid())
        return false;

    const TimeLineEvent::Target target(*hit.node, hit.pos, hit.pos.index());
    if (mSelection.contains(target)) {
        // right-click on a selected key: operate on the whole selection
        mSelection.assign(aEvent);
        return true;
    }

    // right-click on an unselected key: clear the current selection and lend
    // the clicked key a temporary selection for the lifetime of the context
    // menu — it shows the highlight and every menu action consumes it. The
    // previous selection is NOT restored; endTemporarySelection() drops the
    // temporary selection once the menu closes.
    TimeLineEvent single;
    single.pushTarget(*hit.node, hit.pos);
    mSelection.set(single);
    mSelectionTemporary = true;
    mSelection.assign(aEvent);
    return true;
}

void TimeLineEditor::endTemporarySelection() {
    if (!mSelectionTemporary)
        return;
    mSelection.clear();
    mSelectionTemporary = false;
}

bool TimeLineEditor::selectAllKeys() {
    // The marquee's row walk (see time_Marquee.cpp) without its box: every row,
    // every live key of its node's timeline, and - for a closed folder - its
    // descendants, whose keys are drawn on that row. The frame window is NOT
    // applied: "all" is every key the row holds, not the slice on screen.
    core::TimeLineEvent all;
    for (const TimeLineRow& row : mRows) {
        if (!row.node)
            continue;
        core::ObjectNode::Iterator nodeItr(row.node);
        while (nodeItr.hasNext()) {
            core::ObjectNode* node = nodeItr.next();
            if (!node || !node->timeLine())
                continue;
            const core::TimeLine& timeLine = *(node->timeLine());
            for (int i = 0; i < core::TimeKeyType_TERM; ++i) {
                const core::TimeKeyType type = core::TimeLine::getTimeKeyTypeInOrderOfOperations(i);
                const core::TimeLine::MapType& map = timeLine.map(type);
                for (auto itr = map.begin(); itr != map.end(); ++itr) {
                    if (itr.value())
                        all.pushTarget(*node, type, itr.key());
                }
            }
            if (!row.closedFolder)
                break; // an open folder's children are rows of their own
        }
    }
    // An explicit bulk selection replaces whatever was there, and a temporary
    // right-click loan has no menu to outlive: drop both the gesture state and the
    // loan flag so a stale press cannot anchor on the fresh selection.
    mSelectionTemporary = false;
    clearState();
    return mSelection.set(all);
}

bool TimeLineEditor::retrieveSelectionTargets(TimeLineEvent& aEvent) const {
    if (mSelection.empty())
        return false;
    mSelection.assign(aEvent);
    return true;
}

static bool isKeyJsonValid(QJsonObject json) {
    if (json.contains("TargetsSize") && json["TargetsSize"] != 0 && json.contains("Keys") &&
        json.value("Keys").toArray().size() != 0) {
        return true;
    }
    return false;
}

static QVector2D objToVec(QJsonObject obj, const QString& varName) {
    return {static_cast<float>(obj[varName + "X"].toDouble()), static_cast<float>(obj[varName + "Y"].toDouble())};
}

// The clipboard's easing codec (see the header): the ONE description of these
// fields. aRange/eType name the value the same way the timeline's menus and the
// .anie files do; the two mode ordinals and the four cubic floats are what a
// per-side Custom curve actually consists of.
QJsonObject ctrl::TimeLineEditor::easingToJson(const util::Easing::Param& aParam) {
    QJsonObject easing;
    // The automatic pair, whatever its clamp: the preset writes AutoClamped
    // (Easing::kAutoPresetHandleMode) and a deliberate per-side pick can write
    // unclamped Auto. A build without the per-side model has only the retired
    // Auto TYPE, whose shape was the clamped one, so both write "Auto" - the
    // closest thing that reader has. The modes below are what THIS build reads
    // back (and what a retired-Auto legacy payload, which has no modes, is
    // disambiguated from).
    const bool autoPreset = aParam.type == util::Easing::Type_Custom
        && (aParam.handleIn == util::Easing::HandleMode_Auto
            || aParam.handleIn == util::Easing::kAutoPresetHandleMode)
        && aParam.handleIn == aParam.handleOut;
    easing["eType"] = autoPreset ? QStringLiteral("Auto") : util::Easing::getTypeName(aParam.type);
    easing["eRange"] = util::Easing::getRangeName(aParam.range);
    easing["eWeight"] = aParam.weight;
    easing["eIn"] = int(aParam.handleIn);
    easing["eOut"] = int(aParam.handleOut);
    easing["x1"] = aParam.cubicBezier.x1;
    easing["y1"] = aParam.cubicBezier.y1;
    easing["x2"] = aParam.cubicBezier.x2;
    easing["y2"] = aParam.cubicBezier.y2;
    return easing;
}

util::Easing::Param ctrl::TimeLineEditor::easingFromJson(const QJsonObject& aObject) {
    util::Easing::Param easing;
    const QString name = aObject["eType"].toString();
    easing.type = util::Easing::easingToEnum(name);
    easing.range = util::Easing::rangeToEnum(aObject["eRange"].toString());
    easing.weight = static_cast<float>(aObject["eWeight"].toDouble(1.0));

    // A payload with modes describes them exactly (0.15+). A payload without
    // them predates the per-side model: "Auto" there was the retired Auto TYPE,
    // which always CLAMPED - the same behaviour the preset writes now, so the
    // name round-trips losslessly. Every other legacy name kept its stored modes'
    // default (Aligned), which is what that build wrote.
    const bool hasModes = aObject.contains("eIn") && aObject.contains("eOut");
    if (hasModes) {
        const int in = aObject["eIn"].toInt();
        const int out = aObject["eOut"].toInt();
        if (0 <= in && in < util::Easing::HandleMode_TERM)
            easing.handleIn = static_cast<util::Easing::HandleMode>(in);
        if (0 <= out && out < util::Easing::HandleMode_TERM)
            easing.handleOut = static_cast<util::Easing::HandleMode>(out);
    } else if (name == QStringLiteral("Auto")) {
        easing.type = util::Easing::Type_Custom;
        easing.handleIn = util::Easing::kDefaultAutomaticHandleMode;
        easing.handleOut = util::Easing::kDefaultAutomaticHandleMode;
    }
    if (aObject.contains("x1") && aObject.contains("y1")
        && aObject.contains("x2") && aObject.contains("y2")) {
        easing.cubicBezier.x1 = static_cast<float>(aObject["x1"].toDouble());
        easing.cubicBezier.y1 = static_cast<float>(aObject["y1"].toDouble());
        easing.cubicBezier.x2 = static_cast<float>(aObject["x2"].toDouble());
        easing.cubicBezier.y2 = static_cast<float>(aObject["y2"].toDouble());
    }

    // An invalid mode/type/range would be a document-corrupting paste; the model's
    // own validity rule is the guard, and a bad field falls back to the default's.
    if (!easing.isValidParam())
        return util::Easing::Param();
    return easing;
}

QVector<util::Easing::Param> ctrl::TimeLineEditor::easingsFromJson(
    const QJsonObject& aObject, const int aStoredCount) {
    const int count = qMax(0, aStoredCount);
    QVector<util::Easing::Param> result;
    result.reserve(count);
    const QJsonArray array = aObject["eEasings"].toArray();
    for (int component = 0; component < count; ++component) {
        if (component < array.size() && array.at(component).isObject()) {
            result.push_back(easingFromJson(array.at(component).toObject()));
            continue;
        }
        if (component == 0)
            result.push_back(easingFromJson(aObject));
        else
            result.push_back(array.isEmpty() ? result.front() : util::Easing::Param());
    }
    return result;
}

// One param per component: a key's components own their curves, so a paste must
// write each one it was given and never broadcast component 0 over its siblings.
template<typename TKey>
static void assignClipboardEasing(TKey* aKey, const QVector<util::Easing::Param>& aEasings) {
    const int count = qMin(aEasings.size(), int(TKey::kComponents));
    for (int component = 0; component < count; ++component) {
        aKey->data().easing(component) = aEasings.at(component);
    }
}

static TimeKey* getKeyFromObj(QJsonObject obj, util::LifeLink::Pointee<Project> project, bool isFolder, ObjectNode* aOwner) {
    TimeKeyType type = TimeLine::getTimeKeyType(obj["Type"].toString());
    // We're losing precision for float casts from json strings because
    // the cast rounds at the third decimal for some godforsaken reason.
    switch (type) {
        case TimeKeyType_Move: {
            auto* moveKey = new MoveKey;
            QVector2D pos = objToVec(obj, "Pos");
            QVector2D centre = objToVec(obj, "Centre");
            MoveKey::SplineType spline =
                obj["Spline"].toString() == "Catmull" ? MoveKey::SplineType_CatmullRom : MoveKey::SplineType_Linear;
            moveKey->data().setPos(pos);
            moveKey->data().setCentroid(centre);
            moveKey->data().setSpline(spline);
            assignClipboardEasing(moveKey, TimeLineEditor::easingsFromJson(obj, core::MoveKey::kComponents));
            moveKey->setFrame(obj["Frame"].toInt());
            return moveKey;
        }
        case TimeKeyType_Rotate: {
            auto* rotateKey = new RotateKey;
            rotateKey->setRotate(obj["Rotate"].toDouble());
            assignClipboardEasing(rotateKey, TimeLineEditor::easingsFromJson(obj, core::RotateKey::kComponents));
            rotateKey->setFrame(obj["Frame"].toInt());
            return rotateKey;
        }
        case TimeKeyType_Scale: {
            auto* scaleKey = new ScaleKey;
            scaleKey->setScale(objToVec(obj, "Scale"));
            assignClipboardEasing(scaleKey, TimeLineEditor::easingsFromJson(obj, core::ScaleKey::kComponents));
            scaleKey->setFrame(obj["Frame"].toInt());
            return scaleKey;
        }
        case TimeKeyType_Depth: {
            auto* depthKey = new DepthKey;
            depthKey->setDepth(static_cast<float>(obj["Depth"].toDouble()));
            assignClipboardEasing(depthKey, TimeLineEditor::easingsFromJson(obj, core::DepthKey::kComponents));
            depthKey->setFrame(obj["Frame"].toInt());
            return depthKey;
        }
        case TimeKeyType_Opa: {
            auto* opaKey = new OpaKey;
            opaKey->setOpacity(static_cast<float>(obj["Opacity"].toDouble()));
            assignClipboardEasing(opaKey, TimeLineEditor::easingsFromJson(obj, core::OpaKey::kComponents));
            opaKey->setFrame(obj["Frame"].toInt());
            return opaKey;
        }
        case TimeKeyType_Bone: {
            auto* boneKey = new BoneKey;
            QJsonArray boneArray = obj["Bones"].toArray();
            // top bone count
            if (static_cast<int>(boneArray.size()) < 0) { return nullptr; }
            // deserialize all bones
            for (auto bone : boneArray) {
                auto* topBone = new Bone2();
                boneKey->data().topBones().push_back(topBone);
                topBone->deserializeFromJson(bone.toObject(), project.address, nullptr);
                topBone->updateWorldTransform();
            }
            boneKey->resetCaches(*project.address, *aOwner);
            boneKey->setFrame(obj["Frame"].toInt());

            return boneKey;
        }
        case TimeKeyType_Pose: {
            auto* poseKey = new PoseKey;
            QJsonArray boneArray = obj["Bone"].toArray();
            QList<Bone2*> bones;
            for (QJsonValue bone : boneArray) {
                QJsonObject boneObj = bone.toObject();
                auto* newBone = new Bone2;
                newBone->deserializeFromJson(boneObj, project.address, nullptr);
                bones.append(newBone);
            }
            poseKey->data().topBones() = bones;
            // Pose carries ONE shared easing (kComponents == 1); it travels like
            // every other key's, so a copy no longer drops it.
            assignClipboardEasing(
                poseKey, TimeLineEditor::easingsFromJson(obj, core::PoseKey::kComponents));
            poseKey->setFrame(obj["Frame"].toInt());
            return poseKey;
        }
        case TimeKeyType_Mesh: {
            // Key type not acknowledged by folders
            if (isFolder) {
                return nullptr;
            }
            auto* meshKey = new MeshKey;
            QJsonObject mesh = obj["Mesh"].toObject();
            meshKey->data().deserializeFromJson(mesh);
            meshKey->setFrame(obj["Frame"].toInt());
            return meshKey;
        }
        case TimeKeyType_FFD: {
            FFDKey* ffdKey = new FFDKey;
            assignClipboardEasing(ffdKey, TimeLineEditor::easingsFromJson(obj, core::FFDKey::kComponents));
            ffdKey->deserializeFromJson(obj);
            ffdKey->setFrame(obj["Frame"].toInt());
            return ffdKey;
        }
        case TimeKeyType_Image: {
            // Key type not acknowledged by folders
            if (isFolder) {
                return nullptr;
            }
            auto* imageKey = new ImageKey;
            assignClipboardEasing(imageKey, TimeLineEditor::easingsFromJson(obj, core::ImageKey::kComponents));
            if (imageKey->deserializeFromJson(obj, project)) {
                return imageKey;
            }
            return nullptr;
        }
        case TimeKeyType_HSV: {
            auto* hsvKey = new HSVKey;
            assignClipboardEasing(hsvKey, TimeLineEditor::easingsFromJson(obj, core::HSVKey::kComponents));
            QList hsv{obj["Hue"].toInt(), obj["Saturation"].toInt(), obj["Value"].toInt(), obj["Absolute"].toInt()};
            hsvKey->setHSV(hsv);
            hsvKey->setFrame(obj["Frame"].toInt());
            return hsvKey;
        }
        case TimeKeyType_Blur: {
            auto* blurKey = new BlurKey;
            assignClipboardEasing(blurKey, TimeLineEditor::easingsFromJson(obj, core::BlurKey::kComponents));
            blurKey->setAmount(obj["Amount"].toDouble());
            // directional data only exists in newer clipboard payloads; fall back to the
            // isotropic amount otherwise
            blurKey->setDirectional(obj["Directional"].toBool());
            if (blurKey->isDirectional()){
                blurKey->setBlurX(obj["BlurX"].toDouble());
                blurKey->setBlurY(obj["BlurY"].toDouble());
                blurKey->setAngleDeg(obj["Angle"].toDouble());
            }
            blurKey->setFrame(obj["Frame"].toInt());
            return blurKey;
        }
        case TimeKeyType_Wave: {
            auto* waveKey = new WaveKey;
            assignClipboardEasing(waveKey, TimeLineEditor::easingsFromJson(obj, core::WaveKey::kComponents));
            waveKey->setStrength(float(obj["Strength"].toDouble()));
            waveKey->setWavelength(float(obj["Wavelength"].toDouble(WaveKey::Preset::wavelength)));
            waveKey->setAngleDeg(float(obj["Direction"].toDouble()));
            waveKey->setPhase(float(obj["Phase"].toDouble()));
            waveKey->setMix(float(obj["Mix"].toDouble(WaveKey::Preset::mix)));
            waveKey->setSpeed(float(obj["Speed"].toDouble(WaveKey::Preset::speed)));
            waveKey->setCrest(float(obj["Crest"].toDouble(WaveKey::Preset::crest)));
            waveKey->setDualAngleDeg(float(obj["DualAngle"].toDouble(WaveKey::Preset::dualAngleDeg)));
            waveKey->setDualScale(float(obj["DualScale"].toDouble(WaveKey::Preset::dualScale)));
            waveKey->setDeform(
                obj["Deform"].toString() == QLatin1String("Sway") ? WaveKey::Deform_Sway : WaveKey::Deform_Ripple
            );
            waveKey->setFrame(obj["Frame"].toInt());
            return waveKey;
        }
        // If you end up here you've done goofed.
        case TimeKeyType_TERM: {
            return nullptr;
        }
    }
    return nullptr;
}

QList<TimeKey*> TimeLineEditor::getTypesFromCb(util::LifeLink::Pointee<Project> project, ObjectNode* node) {
    QClipboard* qcb = QGuiApplication::clipboard(); // qDebug() << qcb->text();
    QJsonObject keyJson = QJsonDocument::fromJson(QByteArray::fromStdString(qcb->text().toStdString())).object();
    if (!isKeyJsonValid(keyJson)) {
        return {};
    }
    QJsonArray keys = keyJson["Keys"].toArray(); // qDebug() << keys;
    QList<TimeKey*> keyList;
    for (QJsonValue key : keys) {
        auto keyObj = key.toObject();
        // To get all keys isFolder is set to false.
        TimeKey* pastedKey = getKeyFromObj(keyObj, project, false, node);
        if (pastedKey != nullptr) {
            // The clipboard carries the flag only: the weights are empty until the
            // key's next projection resolves its bounds here.
            pastedKey->setProportional(keyObj["Proportional"].toBool());
            keyList.append(pastedKey);
        }
    }
    return keyList;
}

QString TimeLineEditor::pasteCbKeys(ObjectNode& aNode, util::LifeLink::Pointee<Project> project, bool isFolder) {
    QClipboard* qcb = QGuiApplication::clipboard(); // qDebug() << qcb->text();
    QJsonObject keyJson = QJsonDocument::fromJson(QByteArray::fromStdString(qcb->text().toStdString())).object();
    if (!isKeyJsonValid(keyJson))
        return "Invalid Json";
    QJsonArray tlKeys = keyJson["Keys"].toArray(); // qDebug() << keys;
    QList<TimeKey*> keyList;
    int nullLog = 0;
    QStringList keyErrored;
    for (QJsonValue key : tlKeys) {
        auto keyObj = key.toObject();
        TimeKey* pastedKey = getKeyFromObj(keyObj, project, isFolder, &aNode);
        if (pastedKey != nullptr) {
            pastedKey->setProportional(keyObj["Proportional"].toBool());
            keyList.append(pastedKey);
        }
        else {
            auto keyType = TimeLine::getTimeKeyType(keyObj["Type"].toString());
            keyErrored.append(TimeLine::getTimeKeyName(keyType));
            nullLog++;
        }
    }
    QString returnString;
    if (keyList.empty()) {
        returnString = "No keys to copy.";
    }
    // qDebug() << project.address->fileName();
    int frameLessThanZero = 0;
    int timelineHasKey = 0;

    if (!keyList.empty()) {
        int pastedKeys = 0;
        // One undo step + one notification for the whole paste. A per-key macro
        // fragments the undo history, and with a single-aggregate "creation
        // selects its output" rule it would only ever select the last pasted
        // key. The notifier names each created key at its real frame.
        cmnd::Stack& stack = project.address->commandStack();
        auto notifier = new TimeLineUtil::Notifier(*project.address);
        notifier->event().setType(TimeLineEvent::Type_PushKey);
        {
            cmnd::ScopedMacro macro(stack, CmndName::tr("Paste clipboard keys"));
            macro.grabListener(notifier);

            for (int x = 0; x < keyList.size(); x++) {
                TimeKey* keyframe = keyList[x];
                int newFrame = keyframe->frame();
                TimeKeyType type = keyframe->type();
                TimeLine* timeLine = aNode.timeLine();
                // invalid frame
                if (newFrame < 0) { frameLessThanZero++; }
                else {
                    if (timeLine->hasTimeKey(type, newFrame)) { timelineHasKey++; }
                    else {
                        if (type == TimeKeyType_FFD) {
                            // find area key and mesh
                            auto [aKey, aMesh] = core::TimeKeyBlender::getAreaMeshImpl(
                                aNode, project.address->currentTimeInfo()
                            );
                            TimeKey* areaKey = aKey;
                            LayerMesh* areaMesh = aMesh;
                            timeLine->current().setFFDMesh(areaMesh);
                            timeLine->current().setFFDMeshParent(areaKey);
                            // connect to parent mesh
                            aKey->children().pushBack(keyframe);
                        }
                        // Announce the created key at its actual frame. The old
                        // code named the loop index x, which never matched a key
                        // in the lane map and left follow-up consumers (and the
                        // created-key selection) pointing at nothing.
                        notifier->event().pushTarget(aNode, TimeKeyPos(*timeLine, type, newFrame));

                        QHash<const TimeKey*, TimeKey*> parentHash;
                        struct ChildInfo {
                            TimeKey* key;
                            TimeKey* parent;
                        };
                        QList<ChildInfo> childList;
                        auto line = timeLine;
                        XC_PTR_ASSERT(line);

                        auto copiedKey = keyframe;
                        XC_PTR_ASSERT(copiedKey);
                        auto parentKey = copiedKey->parent();

                        TimeKey* newKey = copiedKey->createClone();
                        // createClone copies the key's data only, so the flag the
                        // clipboard reader armed on the source key is re-armed here
                        // (fresh id, empty weights; the bounds resolve lazily).
                        if (copiedKey->isProportional())
                            newKey->setProportional(true);

                        newKey->setFrame(newFrame);

                        stack.push(new cmnd::GrabNewObject<TimeKey>(newKey));
                        stack.push(line->createPusher(type, newFrame, newKey));

                        if (newKey->canHoldChild()) {
                            parentHash[copiedKey] = newKey;
                        }
                        if (parentKey) {
                            ChildInfo info = {newKey, parentKey};
                            childList.push_back(info);
                        }

                        // connect to parents
                        for (auto child : childList) {
                            auto parent = child.parent;
                            // if the parent was also copied, connect to a new parent key.
                            auto it = parentHash.find(parent);
                            if (it != parentHash.end())
                                parent = it.value();
                            stack.push(new cmnd::PushBackTree<TimeKey>(&parent->children(), child.key));
                        }
                        pastedKeys++;
                    }
                }
            }
        }
        returnString = "Successfully pasted [" + QString::number(pastedKeys) + "] key(s).";
    }
    if (frameLessThanZero != 0 || timelineHasKey != 0 || nullLog != 0) {
        returnString.append(
            "\nNumber of errors is [" + QString::number(frameLessThanZero + timelineHasKey + nullLog) + "]"
        );
        // Error logging
        if (frameLessThanZero != 0) {
            returnString.append("\nError: Frame is less than zero [" + QString::number(frameLessThanZero) + "]");
        }
        if (timelineHasKey != 0) {
            returnString.append("\nError: Timeline already has a key [" + QString::number(timelineHasKey) + "]");
        }

        if (nullLog != 0) {
            returnString.append("\nError: Null key(s) detected [" + QString::number(nullLog) + "]");
            returnString.append("\nNull Log:");
            bool folderError = false;
            int folderErrorCount = 0;
            if (keyErrored.contains("Image") && !isFolder) {
                returnString.append("\nImage identifier could not be found in the resource holder.");
            } else if (keyErrored.contains("Image")) {
                folderError = true;
                folderErrorCount++;
            }
            if (keyErrored.contains("FFD") && !isFolder) {
                returnString.append("\nFFD key pasting is unsupported");
            } else if (keyErrored.contains("FFD")) {
                folderError = true;
                folderErrorCount++;
            }
            if (keyErrored.contains("Mesh") && isFolder) {
                folderError = true;
                folderErrorCount++;
            }
            QStringList keys{"Move", "Rotate", "Scale", "Depth", "Opa", "Bone", "Pose", "HSV", "Blur"};
            bool containsOtherKeys = false;
            int containsCount = 0;
            for (const QString& key : keys) {
                if (keyErrored.contains(key)) {
                    containsOtherKeys = true;
                    containsCount++;
                }
            }
            if (folderError) {
                returnString.append("\nKey cannot be used on a folder");
                returnString.append("\nNumber of types with a folder error: " + QString::number(folderErrorCount));
            }
            if (containsOtherKeys) {
                returnString.append("\nKey parameters are incorrect.");
                returnString.append("\nNumber of types with a param error: " + QString::number(containsCount));
            }
            QString keysErrored = "\nKey types errored: ";
            for (const QString& key : keyErrored) {
                keysErrored.append(key + ";");
            }
            returnString.append(keysErrored);
        }
    }
    return returnString;
}

bool TimeLineEditor::pasteCopiedKeys(TimeLineEvent& aEvent, const QPoint& aWorldPos) {
    XC_ASSERT(!aEvent.targets().isEmpty());

    // a minimum frame for key pasting
    auto pasteFrame = mTimeScale.frame(aWorldPos.x() - kTimeLineMargin);

    // a minimum frame in copied keys (the paste offset is measured from the
    // earliest copied key; copying out-of-range keys must not cap it at the
    // frame range's end)
    int copiedFrame = INT_MAX;
    for (auto target : aEvent.targets()) {
        copiedFrame = std::min(copiedFrame, target.pos.index());
    }

    const int frameOffset = pasteFrame - copiedFrame;

    // check validity: the destination frame is unconstrained (the frame range
    // does not gate the keying system); only an existing key conflicts.
    for (auto target : aEvent.targets()) {
        auto newFrame = target.pos.index() + frameOffset;

        // a key already exists.
        auto type = target.pos.type();
        if (target.pos.line()->hasTimeKey(type, newFrame)) {
            return false;
        }
    }

    mOnUpdatingKey = true;
    {
        cmnd::Stack& stack = mProject->commandStack();

        // create notifier. The notification must name the *created* keys (at
        // their new frames), not the copied-from targets, so consumers — the
        // "creation selects its output" rule and any modifier that follows the
        // paste — resolve to the keys that were actually added.
        auto notifier = new TimeLineUtil::Notifier(*mProject);
        notifier->event().setType(TimeLineEvent::Type_CopyKey);

        // push paste keys command
        cmnd::ScopedMacro macro(stack, CmndName::tr("Paste keys"));
        macro.grabListener(notifier);

        QHash<const TimeKey*, TimeKey*> parentMap;
        struct ChildInfo {
            TimeKey* key;
            TimeKey* parent;
        };
        QList<ChildInfo> childList;

        for (auto target : aEvent.targets()) {
            auto type = target.pos.type();
            auto line = target.pos.line();
            XC_PTR_ASSERT(line);

            auto copiedKey = target.pos.key();
            XC_PTR_ASSERT(copiedKey);
            auto parentKey = copiedKey->parent();

            TimeKey* newKey = copiedKey->createClone();
            // createClone copies the key's data only (ids and the proportional
            // state are deliberately not cloned), so the flag is re-armed here:
            // a fresh id and empty weights are what a paste wants, and the
            // destination's bounds are resolved lazily on the next projection.
            if (copiedKey->isProportional())
                newKey->setProportional(true);

            auto newFrame = copiedKey->frame() + frameOffset;
            newKey->setFrame(newFrame);

            stack.push(new cmnd::GrabNewObject<TimeKey>(newKey));
            stack.push(line->createPusher(type, newFrame, newKey));

            notifier->event().pushTarget(*target.node, type, newFrame);

            if (newKey->canHoldChild()) {
                parentMap[copiedKey] = newKey;
            }
            if (parentKey) {
                ChildInfo info = {newKey, parentKey};
                childList.push_back(info);
            }
        }
        // connect to parents
        for (auto child : childList) {
            auto parent = child.parent;
            // if the parent was also copied, connect to a new parent key.
            auto it = parentMap.find(parent);
            if (it != parentMap.end())
                parent = it.value();
            stack.push(new cmnd::PushBackTree<TimeKey>(&parent->children(), child.key));
        }
    }
    mOnUpdatingKey = false;

    return true;
}

void TimeLineEditor::deleteCheckedKeys(TimeLineEvent& aEvent) {
    XC_ASSERT(!aEvent.targets().isEmpty());

    mOnUpdatingKey = true;
    {
        cmnd::Stack& stack = mProject->commandStack();

        // create notifier
        auto notifier = new TimeLineUtil::Notifier(*mProject);
        notifier->event() = aEvent;
        notifier->event().setType(TimeLineEvent::Type_RemoveKey);

        // push delete keys command
        cmnd::ScopedMacro macro(stack, CmndName::tr("Delete keys"));
        macro.grabListener(notifier);

        for (auto target : aEvent.targets()) {
            TimeLine* line = target.pos.line();
            XC_PTR_ASSERT(line);
            stack.push(line->createRemover(target.pos.type(), target.pos.index(), true));
        }
    }
    mOnUpdatingKey = false;

    clearSelection();
}

bool TimeLineEditor::assignSelectionEasing(
    const std::function<util::Easing::Param(const util::Easing::Param&)>& aNext) {
    if (!mProject)
        return false;

    // WHAT CAN BE WRITTEN, collected before the batch is pushed: pushEasingWrites
    // drops a param that would not change, and pushes NOTHING when nothing is left
    // (cmnd::Stack appends a macro even when it holds no command and counts it as
    // an edit - the same guard the graph panel's own action carries).
    QVector<EasingWrite> writes;
    // The live selection, the only set that names a COMPONENT: a timeline gesture
    // selects whole keys (kKeyLevel), a graph-panel press selects one marker. A
    // whole-key entry edits every component the type stores; a marker edits ONLY
    // its own - the rule that keeps one channel's edit from overwriting its
    // siblings'.
    for (const time::Selection::Item& item : mSelection.items()) {
        // The selection is iterated by CONST reference, but TimeKeyPos::key() is a
        // non-const member (it hands out the mutable key), so the position is taken
        // by value and asked there: three words of copy, no cast, and the key it
        // names is the document's own.
        core::TimeLineEvent::Target target = item.target;
        core::TimeKeyPos pos = target.pos;
        core::TimeKey* key = pos.key();
        if (!key || !target.node)
            continue; // a stale selection names a key that is not in the document

        const int stored = core::easingStorageCount(pos.type());
        const bool wholeKey = (item.component == time::Selection::kKeyLevel);
        if (!wholeKey && (item.component < 0 || stored <= item.component))
            continue;

        for (int component = 0; component < stored; ++component) {
            if (!wholeKey && component != item.component)
                continue;

            // The one key -> param bridge: graphable components, Pose/FFD's shared
            // param and an Image key's stored pair; Bone/Mesh answer null.
            util::Easing::Param* param = core::keyEasingOf(*key, pos.type(), component);
            if (!param)
                continue;
            EasingWrite write;
            write.param = param;
            write.next = aNext(*param);
            write.node = target.node;
            write.type = pos.type();
            write.frame = core::Frame(key->frame());
            writes.append(write);
        }
    }
    return pushEasingWrites(*mProject, writes, CmndName::tr("Change key(s) easing"));
}

bool TimeLineEditor::assignSelectionAutoHandles() {
    if (!mProject)
        return false;

    // The Auto preset writes the SEGMENT LEAVING each selected key (see the
    // header): that segment's curve is the key's own param and its two tangents
    // are the key's OUTGOING side and the NEXT key's INCOMING side. Nothing
    // before the key is written. A key with NO next key has no leaving segment
    // yet, so the preset writes that key's OWN pair instead - both sides plus
    // the type - which is what makes it automatic as soon as a neighbour is
    // added. appendAutoPresetWrites merges a param named twice, so a multi-key
    // selection writes each param once.
    QVector<EasingWrite> writes;
    for (const time::Selection::Item& item : mSelection.items()) {
        core::TimeLineEvent::Target target = item.target;
        core::TimeKeyPos pos = target.pos;
        core::TimeKey* key = pos.key();
        if (!key || !target.node)
            continue; // a stale selection names a key that is not in the document

        const int stored = core::easingStorageCount(pos.type());
        const bool wholeKey = (item.component == time::Selection::kKeyLevel);
        if (!wholeKey && (item.component < 0 || stored <= item.component))
            continue;

        for (int component = 0; component < stored; ++component) {
            if (!wholeKey && component != item.component)
                continue;
            if (!core::keyEasingOf(*key, pos.type(), component))
                continue; // Bone/Mesh: no easing at all
            appendAutoPresetWrites(*pos.line(), *target.node, pos.type(), component,
                                   core::Frame(key->frame()), writes);
        }
    }
    // The action's name is the SEMANTIC, not the type: it makes the segment
    // LEAVING each selected key automatic (the history entry says so too).
    return pushEasingWrites(
        *mProject, writes, CmndName::tr("Make the segment after the key automatic"));
}

void TimeLineEditor::appendAutoPresetWrites(core::TimeLine& aLine, core::ObjectNode& aNode,
                                            const core::TimeKeyType aType, const int aComponent,
                                            const core::Frame aFrame,
                                            QVector<EasingWrite>& aOut) {
    // The only neighbour that matters is the NEXT key in ITS OWN map: this key's
    // segment LEAVING it ends there. The PREVIOUS key is never read - the
    // segment arriving at this key is not the one this action owns. Same type,
    // same component - one curve per component, so the neighbour's mode for the
    // SAME component is the one that shapes the segment.
    const core::TimeLine::MapType& map = aLine.map(aType);
    auto self = map.constFind(aFrame.get());
    if (self == map.constEnd() || !self.value())
        return;

    core::TimeKey* next = nullptr;
    auto after = self;
    ++after;
    if (after != map.constEnd())
        next = after.value();

    // The forced fields: aType makes the segment's curve a Custom one (without it
    // this key's OUTGOING mode would be inert), aIn/aOut are the two automatic
    // sides. A param already in aOut is MERGED, so an entry is never written
    // twice and two selected neighbours agree rather than append twice.
    const auto write = [&aOut, &aNode, &aType, aComponent](
                           core::TimeKey* aKey, const bool aForceType, const bool aForceIn,
                           const bool aForceOut) {
        if (!aKey)
            return;
        util::Easing::Param* param = core::keyEasingOf(*aKey, aType, aComponent);
        if (!param)
            return;

        for (EasingWrite& existing : aOut) {
            if (existing.param != param)
                continue;
            if (aForceType)
                existing.next.type = util::Easing::Type_Custom;
            if (aForceIn)
                existing.next.handleIn = util::Easing::kAutoPresetHandleMode;
            if (aForceOut)
                existing.next.handleOut = util::Easing::kAutoPresetHandleMode;
            return;
        }

        EasingWrite entry;
        entry.param = param;
        entry.next = *param;
        entry.node = &aNode;
        entry.type = aType;
        entry.frame = core::Frame(aKey->frame());
        if (aForceType)
            entry.next.type = util::Easing::Type_Custom;
        if (aForceIn)
            entry.next.handleIn = util::Easing::kAutoPresetHandleMode;
        if (aForceOut)
            entry.next.handleOut = util::Easing::kAutoPresetHandleMode;
        aOut.append(entry);
    };

    // A LAST key (including the ONLY key) has no segment leaving it YET, so the
    // preset shapes the key's OWN pair instead: BOTH of its sides become
    // automatic, and its type becomes Custom so either side is consulted when a
    // segment appears beside it. The pair is then symmetric by construction -
    // one slope for both sides - which is what the user asked Auto for even
    // before they add the neighbour that will use it. No other param is named:
    // there is no next incoming side to write.
    if (!next) {
        write(self.value(), true, true, true);
        return;
    }

    // this -> next: the segment's curve IS this key's param, so its TYPE is
    // forced Custom and its OUTGOING side becomes the automatic start tangent.
    // This key's INCOMING side is left alone: it is the end tangent of the
    // segment ARRIVING here, which belongs to the previous key's segment.
    write(self.value(), true, false, true);
    // ...and the next key's INCOMING side is the segment's automatic end
    // tangent. Its type is not touched - that param shapes the segment AFTER it.
    write(next, false, true, false);
}

bool TimeLineEditor::pushEasingWrites(core::Project& aProject,
                                      const QVector<EasingWrite>& aWrites,
                                      const QString& aName) {
    // Only a param that actually CHANGES is written: pushing a command whose exec
    // and undo are the same value is a history entry for a change that did not
    // happen. A param named twice (a stale/duplicated selection entry) is written
    // once; the merged Auto batch is already unique by construction.
    QVector<EasingWrite> changed;
    changed.reserve(aWrites.size());
    for (const EasingWrite& edit : aWrites) {
        if (!edit.param || !edit.node)
            continue;
        if (edit.next == *edit.param)
            continue;
        bool duplicate = false;
        for (const EasingWrite& kept : changed) {
            if (kept.param == edit.param) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            changed.append(edit);
    }
    if (changed.isEmpty())
        return false;

    // ONE macro and ONE notifier for the whole batch, so a multi-key (and, for the
    // Auto preset, multi-param) edit is ONE entry in the history. The command per
    // param is the one the timeline's per-key route pushes
    // (TimeLineUtil::assignKeyEasing: a cmnd::Assign of the key's
    // util::Easing::Param plus a Type_ChangeKeyValue notifier). That per-key helper
    // CANNOT be called in this loop: it opens a macro of its own, and
    // cmnd::Stack::beginMacro asserts on a nested one.
    cmnd::ScopedMacro macro(aProject.commandStack(), aName);
    auto* notifier = new TimeLineUtil::Notifier(aProject);
    notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
    macro.grabListener(notifier);
    for (const EasingWrite& edit : changed) {
        // One notification per KEY: the components of one key are one change to
        // its timeline.
        bool named = false;
        for (const TimeLineEvent::Target& pushed : notifier->event().targets()) {
            if (pushed.node == edit.node && pushed.pos.type() == edit.type
                && pushed.pos.index() == edit.frame.get()) {
                named = true;
                break;
            }
        }
        if (!named) {
            notifier->event().pushTarget(*edit.node, edit.type, edit.frame.get());
        }
        aProject.commandStack().push(
            new cmnd::Assign<util::Easing::Param>(edit.param, edit.next));
    }
    return true;
}

bool TimeLineEditor::updateWheel(int aDelta, bool aInvertScaling) {
    const bool changed = mTimeScale.update(aInvertScaling ? -aDelta : aDelta);
    if (!changed)
        return false;

    mTimeCurrent.update(mTimeScale);
    return true;
}

void TimeLineEditor::setFrame(Frame aFrame) { mTimeCurrent.setFrame(mTimeScale, aFrame); }

Frame TimeLineEditor::currentFrame() const { return mTimeCurrent.frame(); }

QSize TimeLineEditor::modelSpaceSize() const {
    int height = kHeaderHeight;

    if (!mRows.empty()) {
        height += mRows.back().rect.bottom() - mRows.front().rect.top();
    }

    const int width = mTimeScale.maxPixelWidth() + 2 * kTimeLineMargin;

    return QSize(width, height);
}

QPoint TimeLineEditor::currentTimeCursorPos() const { return mTimeCurrent.handlePos(); }

int TimeLineEditor::frameStartPixel() const {
    // The range's first frame on the content axis: the left margin gutter
    // plus its scale position. The scrollbar track adds one more margin past
    // this, so flush-left leaves the tick and its number breathing room.
    return kTimeLineMargin + mTimeScale.pixelWidth(mFrameRange.min());
}

int TimeLineEditor::frameEndPixel() const {
    // The range's last frame on the content axis: the left margin gutter
    // plus its scale position (the renderer's range-marker x1 uses the same
    // convention). The scrollbar track adds one more margin past this, so
    // flush-right leaves the tick and its number breathing room.
    return kTimeLineMargin + mTimeScale.maxPixelWidth();
}

double TimeLineEditor::pixelsPerFrame() const {
    return mTimeScale.pixelsPerFrame();
}

TimeLineEditor::FrameFit TimeLineEditor::fitFrameRange(double aViewportWidth) const {
    FrameFit fit;
    fit.scaleIndex = mTimeScale.index();
    if (!(aViewportWidth > 0.0))
        return fit;

    // The range's span on the content axis is ppf * frames, whatever the zoom;
    // the frame range's inclusive ends are what the fit must show.
    const double span = std::max(1.0, static_cast<double>(mFrameRange.diff()));

    // THE RUNG: the sparsest one that fits the span PLUS a kTimeLineMargin
    // gutter at each end. Asking for the span alone picks a rung whose margin is
    // whatever the ladder leaves over - often none at all on one side - which is
    // the asymmetry a fit must not have.
    const auto ppfAt = [this](int aIndex) {
        // time::Scaler's rate is a function of the index and its notch constant
        // is private to the scaler, so the rate is read from a copy rather than
        // re-derived here.
        time::Scaler probe = mTimeScale;
        probe.setIndex(aIndex);
        return probe.pixelsPerFrame();
    };
    const double gutters = 2.0 * kTimeLineMargin;
    double wantedRate = (aViewportWidth - gutters) / span;

    // ...and the camera's OVERLAP BOUND (gui::TimeLineWidget::updateCamera) caps
    // the rung when the range's CENTRE is left of the content origin: the centred
    // view must not sit more than one viewport before that origin (v >= -viewport),
    // or the camera shifts it right and the symmetry we just computed is lost. A
    // range entirely left of frame 0 (negative ranges are valid) is exactly this
    // case.
    //   v = margin + ppf*min - (W - ppf*span)/2 >= -W
    //   <=> ppf*(min + max)/2 >= -(W/2 + margin)
    const double centre = 0.5 * (static_cast<double>(mFrameRange.min()) + static_cast<double>(mFrameRange.max()));
    if (centre < 0.0)
        wantedRate = std::min(wantedRate, (aViewportWidth / 2.0 + kTimeLineMargin) / -centre);

    fit.scaleIndex = mTimeScale.indexForRate(wantedRate);

    // CENTRED: the leftover width is split evenly, so the tick of frame min and
    // the tick of frame max each keep the same distance from their end of the
    // viewport. The view transform is whole content pixels, so the two paddings
    // can differ by at most that one pixel (see the header).
    const double ppf = ppfAt(fit.scaleIndex);
    const double margin = (aViewportWidth - ppf * span) / 2.0;
    fit.viewLeftPixel = qRound(kTimeLineMargin + ppf * static_cast<double>(mFrameRange.min()) - margin);
    // Half-pixel rounding at the bound must not push the request past it: a view
    // the camera will move is not a fit.
    fit.viewLeftPixel = std::max(fit.viewLeftPixel, -qRound(aViewportWidth));
    fit.valid = true;
    return fit;
}

int TimeLineEditor::anchorPixelAfterScale(int aContentPixel, double aOldSpacing) const {
    // The zoom anchor is the content pixel under the mouse, scaled exactly
    // (no frame rounding, no range clamp). The half-spacing bias rounds
    // half-up for positive content and away from zero for negative. Both
    // spacings are rates, so they are fractional below 1 px/frame - the
    // ratio is what matters and it is exact.
    const double newSpacing = mTimeScale.pixelsPerFrame();
    const double scaled = (static_cast<double>(aContentPixel) - kTimeLineMargin) * newSpacing / aOldSpacing;
    return kTimeLineMargin + static_cast<int>(scaled >= 0.0 ? scaled + 0.5 : scaled - 0.5);
}

void TimeLineEditor::render(
    QPainter& aPainter, const core::CameraInfo& aCamera, theme::TimeLine& aTheme, const QRect& aCullRect
) const {
    paintBodyScene(aPainter, bodyScene(aCamera, aTheme, aCullRect));
}

ctrl::time::BodyScene TimeLineEditor::bodyScene(
    const core::CameraInfo& aCamera, theme::TimeLine& aTheme, const QRect& aCullRect
) const {
    if (aCamera.screenWidth() < 2 * kTimeLineMargin)
        return ctrl::time::BodyScene();

    const QRect camRect(-aCamera.leftTopPos().toPoint(), aCamera.screenSize());
    const QRect cullRect(aCullRect.marginsAdded(QMargins(2, 2, 2, 2))); // use culling

    constexpr int margin = kTimeLineMargin;
    const int bgn = mTimeScale.frame(cullRect.left() - margin - 5);
    const int end = mTimeScale.frame(cullRect.right() - margin + 5);

    // THE CALLER'S PALETTE, not a default one: the widget's theme carries the runtime colours
    // (and the tests set them), so a default-constructed theme here would repaint every lane in
    // the wrong ink.
    ctrl::time::Inks inks;
    inks.body = aTheme.trackColor();
    inks.bodySelected = aTheme.trackSelectColor();
    inks.edge = aTheme.trackEdgeColor();
    inks.separator = aTheme.trackSeperatorColor();

    const QRect marquee = (mState == State_EncloseKeys && mMarquee.isActive()) ? mMarquee.rect() : QRect();
    return buildBodyScene(
        mRows, camRect, cullRect, mTimeScale, util::Range(bgn, end), mFrameRange, mPlayWindow.active(), marquee,
        margin, inks
    );
}

} // namespace ctrl
