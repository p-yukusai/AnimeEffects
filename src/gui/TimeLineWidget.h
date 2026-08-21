#ifndef GUI_TIMELINEWIDGET_H
#define GUI_TIMELINEWIDGET_H

#include <QWidget>
#include <QTreeWidgetItem>
#include <QTimer>
#include <QTime>
#include <QScopedPointer>
#include "util/Signaler.h"
#include "core/Project.h"
#include "core/CameraInfo.h"
#include "core/AbstractCursor.h"
#include "core/Animator.h"
#include "core/TimeLineEvent.h"
#include "gui/TimeLineEditorWidget.h"
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
    void updateLines(QTreeWidgetItem* aTopNode);
    void setPlayBackActivity(bool aIsActive, std::vector<audioConfig>* pConf, mediaState* mediaPlayer);
    void setPlayBackLoop(bool aDoesLoop);
    void setFrame(core::Frame aFrame);
    core::Frame currentFrame() const;

    util::Signaler<void()> onCursorUpdated;
    util::Signaler<void()> onFrameUpdated;
    util::Signaler<void()> onTimeFormatChanged;
    util::Signaler<void(bool)> onPlayBackStateChanged;
    // Vertical scroll request (MMB pan or keyboard); the object tree applies
    // it, clamps to its own range, and syncs the actual value back
    util::Signaler<void(int)> onVerticalScrollRequested;

public:
    void onTreeViewUpdated(QTreeWidgetItem* aTopNode);
    void onScrollUpdated(int aValue);
    void onSelectionChanged(core::ObjectNode* aRepresent);
    void onProjectAttributeUpdated();
    void triggerOnTimeFormatChanged();

private:
    virtual void mouseMoveEvent(QMouseEvent* aEvent);
    virtual void mousePressEvent(QMouseEvent* aEvent);
    virtual void mouseReleaseEvent(QMouseEvent* aEvent);
    virtual void mouseDoubleClickEvent(QMouseEvent* aEvent);
    virtual void wheelEvent(QWheelEvent* aEvent);
    virtual void keyPressEvent(QKeyEvent* aEvent);
    virtual void resizeEvent(QResizeEvent* aEvent);
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
    void syncAudioTracks();
    void onThemeUpdated(theme::Theme&);

    GUIResources& mGUIResources;
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
    bool mPendingScrollbarAdopt;
    // Custom scrollbar thumb drag: Qt remaps the mouse against the live
    // range, which compounds (exponential gain) while the track grows; the
    // drag is anchored instead — the value<->pixel ratio is frozen at press
    // and the view is moved by our own math, so the drag stays constant-speed
    // while the track still updates live.
    bool mScrollbarDragActive;
    double mScrollbarDragAnchorValue;
    int mScrollbarDragAnchorX;
    double mScrollbarDragRatio;
    bool mIsPanning;
    QPoint mPanGrabPos;
    QPoint mPanStartTransform;

    // for animation
    QTimer mTimer;
    QElapsedTimer mElapsed;
    core::Frame mBeginFrame;
    core::Frame mLastFrame;
    bool mDoesLoop;
};

} // namespace gui

#endif // GUI_TIMELINEWIDGET_H
