#ifndef CTRL_TIMELINEEDITOR_H
#define CTRL_TIMELINEEDITOR_H

#include <QRect>
#include <QVector>
#include <QScopedPointer>
#include "gui/obj/obj_Item.h"
#include "util/Range.h"
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

namespace ctrl {

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
    void pushRow(core::ObjectNode* aNode, util::Range aWorldTB, bool aClosedFolder);
    void updateRowSelection(const core::ObjectNode* aRepresent);
    void render(QPainter& aPainter, const core::CameraInfo& aCamera, theme::TimeLine& aTheme, const QRect& aCullRect) const;

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
    int pixelsPerFrame() const;
    // New-scale position of the content point under the mouse, computed
    // pixel-exact from the old scale (see implementation for why).
    int anchorPixelAfterScale(int aContentPixel, int aOldSpacing) const;
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
    static QString pasteCbKeys(gui::obj::Item* objItem, util::LifeLink::Pointee<core::Project> project, bool isFolder);
    static QList<core::TimeKey*> getTypesFromCb(util::LifeLink::Pointee<core::Project> project, core::ObjectNode *node);
    bool pasteCopiedKeys(core::TimeLineEvent& aEvent, const QPoint& aWorldPos);
    void deleteCheckedKeys(core::TimeLineEvent& aEvent);
    // The "creation selects its output" rule. Call this after new keys are
    // created (a push/copy notification): the current selection is cleared and
    // replaced with exactly the created keys carried by aEvent. Gesture and
    // temporary-selection state are dropped so a stale press/marquee/temporary
    // loan cannot anchor future drags on the fresh selection. No-op when aEvent
    // carries no live explicit-key targets (e.g. a default-key payload or a
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
