#ifndef GUI_TIMELINEWIDGET_H
#define GUI_TIMELINEWIDGET_H

#include <QScrollArea>
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
#include "ctrl/TimeLineEditor.h"
#include "gui/theme/TimeLine.h"
#include "gui/TimeLineEditorWidget.h"
#include "gui/ViaPoint.h"
#include "gui/GUIResources.h"
#include "gui/AudioPlaybackWidget.h"

namespace gui {

// Vertical scroll step for keyboard panning; matches ObjectTreeWidget's
// scrollbar singleStep so arrows behave identically on both views.
constexpr int kVerticalStep = 24;

class TimeLineWidget: public QScrollArea {
    Q_OBJECT

public:
    // typedef std::function<void(const core::TimeInfo&)> PlayBackFunc;

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
    virtual void scrollContentsBy(int aDx, int aDy);

    int getFps() const;
    double getOneFrameTime() const;
    QPoint viewportTransform() const;
    void setScrollBarValue(const QPoint& aViewportTransform);
    void updateCamera();
    void updateCursor(const core::AbstractCursor& aCursor, Qt::KeyboardModifiers aModifiers);
    void panTo(const QPoint& aTransform);
    void onPlayBackUpdated();
    void syncAudioTracks();
    void onThemeUpdated(theme::Theme&);

    GUIResources& mGUIResources;
    util::LinkPointer<core::Project> mProject;
    core::Animator& mAnimator;
    TimeLineEditorWidget* mInner;
    core::CameraInfo mCameraInfo;
    core::AbstractCursor mAbstractCursor;
    int mVerticalScrollValue;
    int mHorizontalScrollValue;
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
