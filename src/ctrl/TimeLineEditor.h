#ifndef CTRL_TIMELINEEDITOR_H
#define CTRL_TIMELINEEDITOR_H

#include <QRect>
#include <QVector>
#include <QScopedPointer>
#include <QJsonObject>
#include <cmath>
#include <functional>
#include "util/Range.h"
#include "util/Easing.h"
#include "util/LinkPointer.h"
#include "util/PlacePointer.h"
#include "core/Project.h"
#include "core/AbstractCursor.h"
#include "core/ObjectNode.h"
#include "core/CameraInfo.h"
#include "core/TimeFormat.h"
#include "core/TimeKeyPos.h"
#include "ctrl/TimeLineRow.h"
#include "ctrl/TimeLineUtil.h"
#include "ctrl/time/time_Current.h"
#include "ctrl/time/time_Scaler.h"
#include "ctrl/time/time_Marquee.h"
#include "ctrl/time/time_Selection.h"
#include "gui/theme/TimeLine.h"

class QPainter;

namespace ctrl {
namespace time {
    struct BodyScene;
}


//-------------------------------------------------------------------------------------------------
class TimeLineEditor {
public:
    enum UpdateFlag { UpdateFlag_ModFrame = 1, UpdateFlag_ModView = 2, UpdateFlag_TERM };
    typedef unsigned int UpdateFlags;

    TimeLineEditor();

    // Ruler constants shared with the header drawing (time_Renderer) and the
    // playhead's frame-number badge (gui::TimeCursor).
    static constexpr int kHeaderHeight = 22;
    // Content-axis gutter before frame 0's tick; the scrollbar track also
    // reserves one margin past the last frame (see TimeLineWidget). A single
    // 24px token keeps both sides symmetric.
    static constexpr int kTimeLineMargin = 24;
    // Ruler number text geometry, relative to the header top. The number text
    // top edge; ALL consumers (the ruler numbers, the playhead badge text and
    // the playback-window pill text) read this so the three number rows stay
    // aligned. It sits 1px below the header top, with the 14px text height
    // settling in the middle of the 22px header.
    static constexpr int kNumberTop = 1;
    static constexpr int kNumberHeight = 14;
    // Playback-window label pills (bracket arms and the playhead badge):
    // horizontal padding and corner radius, shared by time_Renderer and
    // gui::TimeCursor so every pill looks identical.
    static constexpr int kPillPadding = 5;
    static constexpr int kPillRadius = 3;

    // The temporary playback window: an ephemeral ruler gesture (shift-drag)
    // that overrides the project frame range for the playback clock until
    // cleared (shift-click, or the context menu). It is clamped into the
    // project frame range by the gesture; navigation is free and never
    // clamps to it. Inactive when end < start; set() normalizes, so a live
    // drag may pass through any press/release order. `end` is the window's
    // inclusive last frame (the range is [start, end]).
    struct PlayWindow {
        int start = 0;
        int end = -1; // end < start -> inactive
        bool active() const { return start <= end; }
        void set(int aStart, int aEnd) {
            start = qMin(aStart, aEnd);
            end = qMax(aStart, aEnd);
        }
        void clear() {
            start = 0;
            end = -1;
        }
    };

    // THE WINDOW GESTURE, as the three steps an input handler delivers: a
    // press anchors it (nothing is visible yet), the first drag materializes
    // it, and the release applies the gesture's ending rule (a press with no
    // drag, or a drag that collapsed to one frame, dismisses it). Both rulers
    // drive these exact steps - the timeline's own Shift-press/drag/release in
    // updateCursor and the animation graph's band through
    // gui::TimeLineWidget::updatePlayWindow - so there is ONE gesture, not a
    // copy with its own edge cases.
    enum class PlayWindowStep { Begin, Drag, End };
    // Applies one step of the gesture at aFrame (clamped into the project
    // frame range, as the window always is). Returns true when the window's
    // value changed; Begin never does by itself.
    bool updatePlayWindow(PlayWindowStep aStep, core::Frame aFrame);

    void setProject(core::Project* aProject);
    void setFrame(core::Frame aFrame);
    void setFrameRange(const util::Range& aRange);
    // True when a widget-space Y lies on the ruler strip: the top
    // kHeaderHeight pixels. The ruler is pinned at the widget top by the
    // viewport-fixed paint, and input positions (mouse events forwarded from
    // the widget, context-menu requests) share that space, so this single
    // predicate backs the window gesture, the header seek and the ruler's
    // context menu — no call site re-derives the geometry.
    bool isOnRuler(int aWidgetY) const { return aWidgetY < kHeaderHeight; }

    core::TimeFormatType timeFormatType() const { return timelineFormat; }
    // Applied live when the timeline-format setting changes (the ruler, the
    // playhead pill and the playback-window pills all re-read it next paint),
    // instead of requiring an app restart.
    void setTimeFormatType(core::TimeFormatType aType) { timelineFormat = aType; }
    int fps() const { return mFps; }
    // The frame->pixel scale; the ruler (TimeRuler) and the renderer share
    // it for ticks, numbers and the playback-window band.
    const time::Scaler& timeScale() const { return mTimeScale; }

    UpdateFlags updateCursor(const core::AbstractCursor& aCursor, Qt::KeyboardModifiers aModifiers);
    bool updateWheel(int aDelta, bool aInvertScaling);
    void updateKey();
    void updateProjectAttribute();

    void clearRows();
    // Empty the row set but KEEP the selection (its keys are still in the document).
    // The caller rebuilds the rows and then calls retainSelectionInRows(), which
    // drops only entries whose layer the rebuilt rows no longer show. This is the
    // expand/collapse path: collapsing a folder is a view change, not a document
    // change, so it must not clear unrelated keys.
    void resetRows();
    void pushRow(core::ObjectNode* aNode, util::Range aWorldTB, bool aClosedFolder);
    // Drop selection entries whose node no longer has a row (or, for a closed
    // folder, no summary representation). Returns whether the set changed.
    bool retainSelectionInRows();
    void updateRowSelection(const core::ObjectNode* aRepresent);
    // True while a marquee drag is in flight: the host's edge autoscroll timer uses
    // this to start and stop without asking the editor's private gesture state.
    bool isMarqueeActive() const { return mState == State_EncloseKeys && mMarquee.isActive(); }
    // Extend the live marquee to a WORLD point (the host recomputes it after it has
    // scrolled the view). False when no marquee is active, so a timer that races a
    // release can do nothing instead of starting an edit. Commits the new box through
    // the same applyMarquee() the drag path uses.
    bool extendMarquee(const QPoint& aWorldPoint);
    void render(QPainter& aPainter, const core::CameraInfo& aCamera, theme::TimeLine& aTheme, const QRect& aCullRect) const;

    // THE BODY AS A VALUE: every pass the surface draws, in paint order, with no QPainter and no
    // document access. The QPainter surface paints it (paintBodyScene); the Canvas Painter host
    // paints the same scene, which is what makes the host swap a host swap.
    ctrl::time::BodyScene bodyScene(
        const core::CameraInfo& aCamera, theme::TimeLine& aTheme, const QRect& aCullRect
    ) const;

    core::Frame currentFrame() const;
    // The temporary playback window (inactive when empty). The renderer draws
    // its band and brackets; playbackRange() folds it into the effective
    // range. The ruler gesture (updateCursor) and clearPlayWindow() mutate it.
    const PlayWindow& playWindow() const { return mPlayWindow; }
    // Dismisses the temporary playback window (the context menu's clear
    // action, mirroring the shift-click gesture).
    void clearPlayWindow() { mPlayWindow.clear(); }
    // The project's frame range: the timeline's playable extent. The
    // temporary playback window overrides it while active (playbackRange()).
    util::Range frameRange() const { return mFrameRange; }
    // The effective playback range: the temporary window overrides the
    // project frame range while active. It bounds the playback clock (the
    // tick wraps and stops against it) and the ruler's fade basis; manual
    // navigation (scrub, transport, keyboard) is free of it — the playhead
    // never clamps. Both the window and the project range are inclusive of
    // their last frame.
    util::Range playbackRange() const {
        return mPlayWindow.active() ? util::Range(mPlayWindow.start, mPlayWindow.end) : mFrameRange;
    }
    QSize modelSpaceSize() const;
    QPoint currentTimeCursorPos() const;

    // The frame range's extent on the content axis (px): the left margin
    // gutter plus the scale position of the range's start/end frames. The
    // range spans [frameStartPixel, frameEndPixel]; the scrollbar track adds
    // one margin past each end.
    int frameStartPixel() const;
    int frameEndPixel() const;
    // The frame axis' rate, FRACTIONAL below 1 px/frame (see time::Scaler's ladder): the
    // content axis is still integer pixels, but a frame no longer needs a whole one.
    double pixelsPerFrame() const;
    // New-scale position of the content point under the mouse, computed
    // pixel-exact from the old scale (see implementation for why).
    int anchorPixelAfterScale(int aContentPixel, double aOldSpacing) const;

    // THE TIMELINE'S FIT, computed here because both numbers are properties of
    // this axis (the ladder and the margin token): the rung to show the frame
    // range at, and the content pixel the viewport's left edge must sit on.
    //
    // The range is CENTRED, not pushed against an edge: the content axis keeps
    // its own kTimeLineMargin gutter before frame min, and the result leaves the
    // SAME gutter after frame max - so a fit reads as symmetric padding instead
    // of "flush left, whatever is left over on the right". The rung is the
    // sparsest one whose rate still fits the span plus a gutter at each end, so
    // the padding is never the arbitrary remainder of the next rung up, and a
    // negative-centred range is additionally capped so the centred view stays
    // inside gui::TimeLineWidget::updateCamera's overlap bound.
    //
    // viewLeftPixel is on the content pixel grid (whole pixels, like the view
    // transform), which keeps the two paddings within one pixel of each other.
    // aViewportWidth <= 0 answers the CURRENT rung and no view.
    struct FrameFit {
        int scaleIndex = 0;
        int viewLeftPixel = 0;
        bool valid = false;
    };
    FrameFit fitFrameRange(double aViewportWidth) const;

    // WHICH PROJECT CHANGES ARE FITTED (see gui::TimeLineWidget::setProject):
    // a project that arrives from NO project is newly opened or newly created, so
    // the timeline has no view of it and starts fitted; a project that replaces
    // another one is a tab switch back to a project this widget is already
    // showing, and its live zoom/pan is the user's saved view - kept.
    static bool fitsOnProjectAttach(bool aHadProject, bool aHasProject) {
        return !aHadProject && aHasProject;
    }
    // The VIEW translation (content px == screen px) of ONE anchored zoom notch,
    // and the sub-pixel part of it that the whole-pixel view transform cannot
    // apply yet. aAnchorFrame is the frame under the caller's cursor, FRACTIONAL
    // (the panel derives it with frameAtPixel, never frameAt); aOldSpacing and
    // aNewSpacing are the scale's rate either side of the notch.
    //
    // The anchored point's content x is kTimeLineMargin + spacing*frame, so the
    // view moves by frame*(oldSpacing - newSpacing): the margin cancels and
    // nothing rounds except the final integer view write. That is the whole
    // point. The pair this replaces - round the anchor's content pixel, then
    // round the scaled one - loses up to ~1 px per notch AND the zoom ladder
    // makes the loss different every notch, so it does not cancel: measured
    // 0.86 of a frame over 20 notches at 7 px/frame on the docked panel.
    //
    // aRemainder must be owned by the caller across notches (the same discipline
    // as TimeLineWidget::scrollFramesBy's pan remainder) and start at 0.0. It
    // keeps the applied total within HALF A PIXEL of the exact total instead of
    // dropping a fraction per notch; drag/no-zoom calls that translate nothing
    // leave it untouched.
    static int zoomAnchorDelta(
        double aAnchorFrame, double aOldSpacing, double aNewSpacing, double& aRemainder
    ) {
        aRemainder += aAnchorFrame * (aOldSpacing - aNewSpacing);
        const int delta = static_cast<int>(std::lround(aRemainder));
        aRemainder -= static_cast<double>(delta);
        return delta;
    }
    // Context-menu key targeting. selectKeysAt() resolves the key under
    // aPos into aEvent. A click on an already-selected key operates on the
    // whole selection; right-clicking an UNSELECTED key clears the current
    // selection and lends the clicked key a temporary selection for the
    // lifetime of the right-click menu (the highlight included, every menu
    // action consumes it) — the previous selection is NOT restored.
    // endTemporarySelection() drops the temporary selection once the menu
    // closes, leaving nothing selected.
    bool selectKeysAt(core::TimeLineEvent& aEvent, const QPoint& aPos);
    void endTemporarySelection();
    bool retrieveSelectionTargets(core::TimeLineEvent& aEvent) const;
    // THE BULK SELECTION (Ctrl+A on the timeline surface): replaces the shared
    // selection with EVERY KEY OF EVERY ROW THIS TIMELINE SHOWS. The enumeration is
    // the marquee's own walk over the rows, without the box and without a frame
    // window:
    //   * every ctrl::TimeLineRow in mRows - every expanded object-tree row, on or
    //     off screen (the row list is the tree, not the viewport), in tree order;
    //   * for each row, the row's own node and, when the row is a CLOSED FOLDER,
    //     every descendant - those keys are drawn on the folder's row and have no
    //     rows of their own (an OPEN folder's children are rows and are covered by
    //     their own entry);
    //   * every key type in TimeLine::getTimeKeyTypeInOrderOfOperations order, and
    //     every frame its map holds - not only the frames inside the visible
    //     window or the project's frame range.
    // Each key is one KEY-LEVEL entry (time::Selection::kKeyLevel), which is the
    // timeline's currency and covers every component the graph panel draws. With
    // no rows there is nothing to select, so the set is emptied - "select all"
    // over an empty enumeration means empty, the same rule the graph's
    // selectAllMarkers follows. Gesture state and a right-click temporary loan are
    // dropped. True when the set CHANGED (so a caller only repaints then), the
    // same answer every time::Selection mutator gives.
    bool selectAllKeys();
    // The LIVE selection, by reference: the one instance this editor writes and
    // the timeline's renderer highlights. It is handed out rather than copied so
    // another view of the same keys (the animation graph panel) can SHARE it
    // instead of keeping a second selection that would have to agree - two
    // selections that must agree are two selections that eventually do not.
    // Every mutator here rebuilds the keys' focus (time::Selection::
    // rebuildFocus), so a write through this reference is highlighted by the
    // timeline's next paint exactly like one made from the timeline's own
    // gestures: there is one set and one highlight state.
    //
    // The caller must not outlive this editor; an owner that may outlive it
    // (the panel, which is destroyed with its project while this widget is not)
    // is expected to drop the reference first.
    time::Selection& selection() { return mSelection; }
    const time::Selection& selection() const { return mSelection; }
    // Paste the clipboard's keys onto ONE node. It used to take the tree's VIEW item
    // (gui::obj::Item), which made this editing class depend on the tree column's widget
    // class for two facts it can be told: which node, and whether it is a folder.
    static QString pasteCbKeys(
        core::ObjectNode& aNode, util::LifeLink::Pointee<core::Project> aProject, bool aIsFolder
    );
    static QList<core::TimeKey*> getTypesFromCb(util::LifeLink::Pointee<core::Project> project, core::ObjectNode *node);

    // ---- THE CLIPBOARD'S EASING PAYLOAD ----
    //
    // The lane JSON carries one easing per COMPONENT now (a Move key has X and Y,
    // a Blur key three). The writer (gui::TimeLineEditorWidget's copy) and the
    // reader (getTypesFromCb, below) use these two functions, so the field names
    // and the fallbacks cannot drift:
    //   eType   the name (util::Easing::getTypeName; "Auto" for the legacy Auto)
    //   eRange  the range name;  eWeight the blend
    //   eIn/eOut the two HANDLE MODES as their enum ordinals (0.15+)
    //   x1,y1,x2,y2 the stored cubic, so a Custom curve's shape survives a copy
    // A payload without eIn/eOut predates the per-side model. Its "Auto" name
    // meant the retired Auto TYPE - both sides CLAMPED
    // (kDefaultAutomaticHandleMode). The current writer names the clamped preset
    // pair "Auto" too, so an older reader sees the same behaviour. The per-component "eEasings"
    // array is therefore what a reader trusts; the top-level fields are the
    // component-0 fallback for payloads that carry no array (and for old builds
    // reading a new clipboard).
    static QJsonObject easingToJson(const util::Easing::Param& aParam);
    static util::Easing::Param easingFromJson(const QJsonObject& aObject);
    // Every component's param for one key: the eEasings array when present, else
    // the top-level fields broadcast to aStoredCount components (which is
    // lossless for pre-per-component payloads).
    static QVector<util::Easing::Param> easingsFromJson(
        const QJsonObject& aObject, int aStoredCount);
    bool pasteCopiedKeys(core::TimeLineEvent& aEvent, const QPoint& aWorldPos);
    void deleteCheckedKeys(core::TimeLineEvent& aEvent);
    // THE SELECTION'S EASING EDIT: apply aNext to every COMPONENT the live
    // selection names, as ONE undo step (one macro, one notification naming every
    // key written). This lives here because the selection and the document do; the
    // timeline widget's picker and quick easings are its callers, and the animation
    // graph's own action is the same rule over its channels.
    //
    // A marker entry (graph-panel press) edits ONLY its component; a whole-key entry
    // (every timeline gesture) edits every component the type stores. Pose/FFD
    // answer their one shared param at component 0; an Image key's stored pair is
    // editable too. A key whose type carries no easing is skipped, and so is a
    // component aNext would not change, so an edit that changes nothing leaves no
    // history entry behind. TRUE when something was written.
    bool assignSelectionEasing(
        const std::function<util::Easing::Param(const util::Easing::Param&)>& aNext);

    // THE AUTO PRESET APPLIES TO THE SEGMENT AFTER THE SELECTED KEY.
    //
    // A tangent PAIR belongs to a segment, and a segment's curve is the FROM
    // key's param: the FROM key's OUTGOING side is the start tangent and the TO
    // key's INCOMING side the end tangent. So applying Auto to key K makes
    // exactly the segment K -> next automatic, and writes:
    //   * K's own param: its TYPE (Custom, because the OUTGOING mode is only
    //     consulted on a Custom curve - without it the mode is inert) and its
    //     OUTGOING side;
    //   * the NEXT key's INCOMING side. That key's type is left alone: it shapes
    //     the segment AFTER next, which this action does not own.
    // The segment ARRIVING at K (prev -> K) is NOT touched: its curve is the
    // previous key's param, so neither that param nor K's INCOMING side - the
    // end tangent of that segment - is written. (The ONE exception is a key
    // with no next key, below: its own pair is the only thing the action can
    // mark, and its incoming side then belongs to that pair.)
    // A LAST key (or the only key) has no segment leaving it YET, so the preset
    // writes the key's OWN pair instead: its type becomes Custom and BOTH its
    // OUTGOING and INCOMING sides become automatic, so the segments that appear
    // when a second key is added derive on both sides. No other param is
    // written: there is no next key's incoming side to name. So a single-key
    // Auto writes ONE param, in ONE macro and therefore ONE undo step, while
    // the middle-key case still writes two. appendAutoPresetWrites merges a
    // param named twice (two selected neighbours), so the multi-key case writes
    // every param once.
    bool assignSelectionAutoHandles();

    // ONE param write produced by a selection-level easing action: the key's own
    // param, the value to assign, and the key whose timeline must re-read (the
    // notification is one per KEY, not per component).
    struct EasingWrite {
        util::Easing::Param* param = nullptr;
        util::Easing::Param next;
        core::ObjectNode* node = nullptr;
        core::TimeKeyType type = core::TimeKeyType_TERM;
        core::Frame frame{0};
    };
    // Appends the Auto preset's writes for ONE key and ONE easing component: the
    // params described on assignSelectionAutoHandles (the key's own pair when it
    // has no next key). An entry that is already present is MERGED, never
    // appended twice, so a param two selected keys both name is written once
    // with the union of the forced sides. Only the type and the two handle modes
    // are the preset's; the stored cubic, range and weight ride along untouched.
    static void appendAutoPresetWrites(core::TimeLine& aLine, core::ObjectNode& aNode,
                                       core::TimeKeyType aType, int aComponent,
                                       core::Frame aFrame, QVector<EasingWrite>& aOut);

    // THE ONE PUSH for a selection-level easing batch: filters out every param
    // that would not change (a command whose exec and undo are the same value is
    // a history entry for a change that did not happen), then pushes ONE macro
    // carrying one cmnd::Assign per param and ONE TimeLineUtil::Notifier naming
    // every key written. FALSE - and nothing pushed - when the batch is empty:
    // cmnd::Stack counts an empty macro as an edit. Both this editor and the
    // graph panel's selection action go through it, so their undo/notification
    // shape cannot drift.
    static bool pushEasingWrites(core::Project& aProject, const QVector<EasingWrite>& aWrites,
                                 const QString& aName);

    // The "creation selects its output" rule. Call this after new keys are
    // created (a push/copy notification): the current selection is cleared and
    // replaced with exactly the created keys carried by aEvent. Gesture and
    // temporary-selection state are dropped so a stale press/marquee/temporary
    // loan cannot anchor future drags on the fresh selection. No-op when aEvent
    // carries no live explicit-key targets (e.g. a base-value payload or a
    // stale index), so a malformed event can never inject null keys into the
    // selection.
    void selectCreatedKeys(const core::TimeLineEvent& aEvent);
    util::LinkPointer<core::Project> mProject;

private:
    enum State { State_Standby, State_MoveCurrent, State_MoveKeys, State_EncloseKeys, State_SetWindow, State_TERM };
    enum MarqueeMode { Marquee_Replace, Marquee_Add, Marquee_Subtract };

    void clearState();
    void clearSelection();
    bool beginMoveKeys();
    bool modifyMoveKeys(const QPoint& aWorldPos);
    void applyMarquee(); // commit the active marquee box into the selection

    QVector<TimeLineRow> mRows;
    const core::ObjectNode* mSelectingRow;
    util::Range mFrameRange;
    int mFps;
    State mState;
    time::Current mTimeCurrent;
    time::Scaler mTimeScale;
    time::Marquee mMarquee;
    time::Selection mSelection;
    QSettings mSettings;
    core::TimeFormatType timelineFormat = static_cast<core::TimeFormatType>(mSettings.value("generalsettings/ui/timeformat").toInt());
    TimeLineUtil::MoveFrameOfKey* mMoveRef;
    bool mSelectionTemporary; // right-click lent selection active; dropped on menu close
    int mMoveFrame;
    int mPressFrame;       // frame where the current gesture pressed
    bool mOnUpdatingKey;
    MarqueeMode mMarqueeMode;
    core::TimeLineEvent mPreGestureSelection;
    core::TimeLineEvent::Target mPressedTarget; // plain-press target, for click-collapse
    bool mPressedTargetValid;
    PlayWindow mPlayWindow;   // the temporary playback window (ruler gesture)
    int mWindowAnchorFrame;   // frame where the window gesture pressed
    bool mWindowDragged;      // the gesture moved past a click (drag vs click)
};

} // namespace ctrl

#endif // CTRL_TIMELINEEDITOR_H
