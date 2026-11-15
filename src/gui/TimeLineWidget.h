#ifndef GUI_TIMELINEWIDGET_H
#define GUI_TIMELINEWIDGET_H

#include <QWidget>
#include <QTimer>
#include <QTime>
#include <QScopedPointer>
#include "util/Signaler.h"
#include "core/Project.h"
#include "core/CameraInfo.h"
#include "core/AbstractCursor.h"
#include "core/Animator.h"
#include "core/TimeLineEvent.h"
#include "ctrl/AutoScroll.h"
#include "gui/TimeLineEditorWidget.h"
#include "gui/AnimationRow.h"
#include "gui/ViaPoint.h"
#include "gui/GUIResources.h"
#include "gui/AudioPlaybackWidget.h"

class QScrollBar;

namespace gui {

// Vertical scroll step for keyboard panning; matches ObjectTreeWidget's
// scrollbar singleStep so arrows behave identically on both views.
constexpr int kVerticalStep = 24;

// The timeline. A plain, viewport-fixed surface: the content widget never
// moves — its paint translates by the view origin (see TimeLineEditorWidget's
// paintEvent) — so the ruler, the lanes and the box-select marquee always
// render at their viewport positions, unclipped by the frame range. The
// horizontal scrollbar is a one-way projection of the free horizontal offset;
// the vertical position is bound to the object tree.
class TimeLineWidget: public QWidget {
    Q_OBJECT

public:
    TimeLineWidget(GUIResources& aResources, ViaPoint& aViaPoint, core::Animator& aAnimator, QWidget* aParent);

    void setProject(core::Project* aProject);
    // The dock's row list (gui::ObjectTreeModel). The lanes ARE these rows: the model owns
    // the height rule (ctrl::TimeLineRow::displayHeight) and the content-space offsets, so
    // the tree and the lanes cannot disagree about where a row starts.
    void setRows(const gui::AnimationRows& aRows);
    void setPlayBackActivity(bool aIsActive, std::vector<audioConfig>* pConf, mediaState* mediaPlayer);
    void setPlayBackLoop(bool aDoesLoop);
    void setFrame(core::Frame aFrame);
    core::Frame currentFrame() const;
    // The effective playback range (project frame range, or the temporary
    // playback window while it overrides): transport shortcuts land on its
    // bounds, not the project range's, so they stay consistent with the
    // window even when it extends past the project range.
    util::Range playbackRange() const { return mInner->playbackRange(); }

    util::Signaler<void()> onCursorUpdated;
    util::Signaler<void()> onFrameUpdated;
    util::Signaler<void()> onTimeFormatChanged;
    util::Signaler<void(bool)> onPlayBackStateChanged;
    // Vertical scroll request (MMB pan or keyboard); the object tree applies
    // it, clamps to its own range, and syncs the actual value back
    util::Signaler<void(int)> onVerticalScrollRequested;
    // The VISIBLE FRAME WINDOW changed: a scroll, a zoom, a resize, a new frame
    // range, or another project. Emitted only when visibleFrameRange() really
    // answers something new, so a subscriber that pushes the window somewhere (the
    // animation graph panel, see MainDisplayWidget::setTimeLineWidget) can treat
    // it as "the frames on screen are now these" rather than as a tick.
    util::Signaler<void()> onViewRangeChanged;

public:
    void onTreeViewUpdated();
    void onScrollUpdated(int aValue);
    void onSelectionChanged(core::ObjectNode* aRepresent);
    void onProjectAttributeUpdated();
    void triggerOnTimeFormatChanged();

    // The controller the inner surface renders, and the handle the animation graph
    // panel reaches the timeline's own state through: its live selection (editor()
    // ->selection()) is the set the panel shares, and its scale is what
    // visibleFrameRange() below is derived from.
    ctrl::TimeLineEditor* editor() const;
    // Opens the timeline's own KEY MENU at aGlobalPos for the key aPressedKey names -
    // the animation graph panel's right-click route to the menu a right-click in the
    // timeline opens (see TimeLineEditorWidget::openKeyMenu, which owns the menu, the
    // selection rule and the temporary loan). aGlobalPos is in GLOBAL (screen)
    // coordinates. False when there is no menu to open: no editor, or aPressedKey is
    // not a key the document holds.
    bool openKeyMenu(const core::TimeLineEvent::Target& aPressedKey, const QPoint& aGlobalPos);
    // The frames this widget is showing right now: the visible window on the
    // content axis, in frames, derived through the editor's own scale (the same
    // mapping every input path here converts a pixel with). This is what the
    // animation graph panel's horizontal window IS while the two are attached
    // (see MainDisplayWidget::setTimeLineWidget).
    util::Range visibleFrameRange() const;
    // The same edges EXACTLY (fractional frames): the shared frame window the panel
    // maps, see GraphEditor::setFrameWindow. Both are 0.0 with no surface to read them
    // from, which the caller treats as "no window".
    void visibleFrameWindow(double& aLo, double& aHi) const;
    // The two horizontal gestures the panel delegates here, in FRAMES - the one
    // unit both views can agree on: a pan (positive reveals LATER frames) and a
    // wheel notch about aAnchorFrame, fractional, so the frame under a cursor the
    // panel owns pixel-exactly can be anchored pixel-exactly here too. A held zoom
    // carries its sub-pixel translation remainder between notches, so the frame
    // under the pointer stays there over the WHOLE gesture, not only for one notch.
    // Both drive this widget's own view transform and zoom ladder, which is what
    // keeps a gesture made over the panel and one made over the timeline on the same
    // frames.
    void scrollFramesBy(double aFrameDelta);
    void zoomFramesAbout(int aWheelDelta, double aAnchorFrame);

    // THE FIT: scale and translate this widget's frame axis so the project's frame
    // range is shown whole with the SAME gutter at each end (see
    // ctrl::TimeLineEditor::fitFrameRange, which computes the two numbers). This is
    // the header's fit button and the new-project fit (setProject), in one place.
    // A widget with no laid-out width remembers the request and applies it on the
    // first camera pass that has one.
    void fitFrameRange();

    // THE TEMPORARY PLAYBACK WINDOW'S GESTURE, for the animation graph's ruler band
    // (ctrl::GraphEditor::setPlayWindowDelegate): one Begin/Drag/End step on the
    // timeline's own ctrl::TimeLineEditor::PlayWindow. The window is the TIMELINE's
    // state - the panel only overlays it - so the graph's Shift-drag moves exactly
    // the window the timeline's own Shift-drag does, and this repaints both rulers.
    // Returns true when the window's value changed.
    bool updatePlayWindow(ctrl::TimeLineEditor::PlayWindowStep aStep, core::Frame aFrame);

private:
    virtual void mouseMoveEvent(QMouseEvent* aEvent);
    virtual void mousePressEvent(QMouseEvent* aEvent);
    virtual void mouseReleaseEvent(QMouseEvent* aEvent);
    virtual void mouseDoubleClickEvent(QMouseEvent* aEvent);
    virtual void wheelEvent(QWheelEvent* aEvent);
    virtual void keyPressEvent(QKeyEvent* aEvent);
    virtual void resizeEvent(QResizeEvent* aEvent);
    virtual void hideEvent(QHideEvent* aEvent);
    virtual QSize minimumSizeHint() const;
    virtual QSize sizeHint() const;
    virtual bool eventFilter(QObject* aObject, QEvent* aEvent);

    int getFps() const;
    double getOneFrameTime() const;
    QPoint viewportTransform() const;
    void setViewTransform(const QPoint& aViewportTransform);
    void projectHorizontalScrollBar();
    void updateCamera();
    void updateCursor(const core::AbstractCursor& aCursor, Qt::KeyboardModifiers aModifiers);
    void panTo(const QPoint& aTransform);
    void beginScrollbarDrag(const QPoint& aPos);
    void moveScrollbarDrag(const QPoint& aPos);
    void endScrollbarDrag();
    void scrollViewBy(int aValueDelta);
    void onPlayBackUpdated();
    // Edge autoscroll for a marquee drag: the shared ctrl::autoscroll step is
    // applied to the horizontal view and the object tree's vertical scroll, then
    // the box is extended to the pointer's new world position (the pointer stays
    // still while the content moves under it).
    void updateMarqueeAutoScroll();
    void autoScrollMarquee();
    // The lanes' viewport, not the whole surface: the ruler/header above them is
    // not a scroll edge, so a box dragged along the top row is near the LANE edge.
    QRect marqueeScrollViewport() const;
    void syncAudioTracks();
    void onThemeUpdated(theme::Theme&);
    // Emits onViewRangeChanged when the visible frame window moved since the last
    // call. Called from the one place the view's projection is recomputed
    // (projectHorizontalScrollBar), so every path that can change the window -
    // scroll, zoom, resize, frame range, project - reports exactly once, and a
    // path that changed nothing reports nothing.
    void notifyViewRange();

    GUIResources& mGUIResources;
    ViaPoint& mViaPoint;
    util::LinkPointer<core::Project> mProject;
    core::Animator& mAnimator;
    TimeLineEditorWidget* mInner;
    QScrollBar* mHorizontalScrollBar;
    core::CameraInfo mCameraInfo;
    core::AbstractCursor mAbstractCursor;
    int mVerticalScrollValue;
    // The horizontal view offset: content origin x in surface coordinates.
    // Free — zoom anchoring and pan can place it anywhere, including outside
    // the frame range. The horizontal scrollbar is a one-way projection of it
    // and never feeds back, except through explicit user slider actions.
    int mHorizontalOffset;
    // A delegated pan's frame remainder, carried between scrollFramesBy calls so whole-content-pixel
    // rounding cannot make the view drift behind the pointer over a drag.
    double mPendingScrollPixels;
    // A delegated zoom's sub-pixel remainder, carried between zoomFramesAbout calls for the same
    // reason: the view transform is whole content pixels and the ladder makes each notch's
    // fraction different, so discarding it per notch walks the anchored frame away over a held
    // zoom (see ctrl::TimeLineEditor::zoomAnchorDelta).
    double mPendingZoomPixels;
    bool mPendingScrollbarAdopt;
    // The window the last onViewRangeChanged emission reported, in FRACTIONAL frames (the same
    // values the signal's consumers get), so the signal fires on every change of it.
    double mReportedWindowLo;
    double mReportedWindowHi;
    // Custom scrollbar thumb drag: Qt remaps the mouse against the live
    // range, which compounds (exponential gain) while the track grows; the
    // drag is anchored instead — the value<->pixel ratio is frozen at press
    // and the view is moved by our own math, so the drag stays constant-speed
    // while the track still updates live.
    bool mScrollbarDragActive;
    double mScrollbarDragAnchorValue;
    int mScrollbarDragAnchorX;
    double mScrollbarDragRatio;
    // A fit requested before this widget had a laid-out width (a project opened on
    // a hidden timeline): updateCamera applies it on the first pass that has one.
    bool mFitPending;
    bool mIsPanning;
    // WHICH button started the pan. The pan is a gesture of one button, and
    // only that button's release may end it: while a middle-drag pan is in
    // flight the surface still forwards a left press to the editor's cursor
    // (a marquee), so a release test of "panning && (Middle || Left)" swallows
    // the LEFT release and leaves the editor stuck in State_EncloseKeys — the
    // selection box then stays painted. Measured 2026-10-11.
    Qt::MouseButton mPanButton;
    // The MoveCanvas key (space by default, configurable like the viewport's
    // pan) is held: left-drag pans instead of selecting/scrubbing. Mirrors
    // MainDisplayWidget's mMovingCanvasByKey.
    bool mPanByKey;
    QPoint mPanGrabPos;
    QPoint mPanStartTransform;

    // for animation
    QTimer mTimer;
    // The marquee's edge autoscroll (see updateMarqueeAutoScroll): one timer while a
    // box drag is in flight, and the pointer's last SCREEN position, which stays put
    // while the view scrolls under it.
    QTimer mMarqueeScrollTimer;
    QPoint mMarqueePointer;
    ctrl::autoscroll::EdgeDwell mMarqueeDwell;
    QElapsedTimer mElapsed;
    core::Frame mBeginFrame;
    core::Frame mLastFrame;
    bool mDoesLoop;
};

} // namespace gui

#endif // GUI_TIMELINEWIDGET_H
