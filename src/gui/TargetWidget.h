#ifndef GUI_CONTROLWIDGET_H
#define GUI_CONTROLWIDGET_H

#include <QWidget>
#include "gui/ThinSplitter.h"
#include "gui/ObjectTreeWidget.h"
#include "gui/TimeLineWidget.h"
#include "gui/GUIResources.h"
#include "gui/PlayBackWidget.h"
#include "gui/ViaPoint.h"
#include "core/Project.h"
#include "core/Animator.h"
#include "util/Range.h"

namespace gui {

class AnimationSettingDialog;

class TargetWidget: public QWidget, public core::Animator {
public:
    TargetWidget(ViaPoint& aViaPoint, GUIResources& aResources, QWidget* aParent, const QSize& aSizeHint);
    void setProject(core::Project* aProject);

    ObjectTreeWidget& objectTreeWidget() { return *mObjTree; }
    const ObjectTreeWidget& objectTreeWidget() const { return *mObjTree; }

    TimeLineWidget& timeLineWidget() { return *mTimeLine; }
    const TimeLineWidget& timeLineWidget() const { return *mTimeLine; }

    PlayBackWidget& playBackWidget() { return *mPlayBack; }
    const PlayBackWidget& playBackWidget() const { return *mPlayBack; }

    // from Animator
    virtual core::Frame currentFrame() const;
    virtual void stop();
    virtual void suspend();
    virtual void resume();
    virtual bool isSuspended() const;

private:
    virtual void resizeEvent(QResizeEvent* aEvent);
    virtual QSize sizeHint() const { return mSizeHint; }

    void onPlayBackButtonPushed(PlayBackWidget::PushType aType);
    void openAnimationSettings();
    void applyAnimChange();
    void applyAnimState(int aFps, const util::Range& aRange, bool aLoop,
                        int pFps, const util::Range& pRange, bool pLoop);
    void refreshAnimationSettings();

    core::Project* mProject;
    ViaPoint& mViaPoint;
    GUIResources& mResources;
    const QSize mSizeHint;
    ThinSplitter* mHorizontalSplitter;
    ObjectTreeWidget* mObjTree;
    TimeLineWidget* mTimeLine;
    PlayBackWidget* mPlayBack;
    AnimationSettingDialog* mAnimSettings = nullptr;
    int mLastAnimFps = 0;
    util::Range mLastAnimRange;
    bool mLastAnimLoop = false;
    bool mIsFirstTime;
    int mSuspendCount;
};

} // namespace gui

#endif // GUI_CONTROLWIDGET_H
