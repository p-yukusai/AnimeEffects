#include "gui/TargetWidget.h"
#include "gui/AnimationSettingDialog.h"
#include "gui/MainMenuBar.h"
#include "cmnd/BasicCommands.h"
#include "cmnd/ScopedMacro.h"
#include "ctrl/CmndName.h"
#include "core/ProjectEvent.h"
#include <QDialog>
#include <QScopedPointer>


namespace gui {

TargetWidget::TargetWidget(ViaPoint& aViaPoint, GUIResources& aResources, QWidget* aParent, const QSize& aSizeHint):
    QSplitter(Qt::Vertical, aParent),
    mProject(),
    mViaPoint(aViaPoint),
    mResources(aResources),
    mSizeHint(aSizeHint),
    mIsFirstTime(true),
    mSuspendCount(0) {
    mHorizontalSplitter = new QSplitter(this);
    mObjTree = new ObjectTreeWidget(aViaPoint, aResources, this);
    mTimeLine = new TimeLineWidget(aResources, aViaPoint, *this, this);
    mPlayBack = new PlayBackWidget(aResources, this, *mProject);

    mInfoLabel = new TimeLineInfoWidget(aResources, this);

    mTimeLine->onFrameUpdated.connect(mInfoLabel, &TimeLineInfoWidget::onUpdate);
    mTimeLine->onTimeFormatChanged.connect(mInfoLabel, &TimeLineInfoWidget::onUpdate);

    mHorizontalSplitter->addWidget(mObjTree);
    mHorizontalSplitter->addWidget(mTimeLine);
    mHorizontalSplitter->addWidget(mPlayBack);
    mHorizontalSplitter->setCollapsible(0, false);
    mHorizontalSplitter->setCollapsible(1, false);
    mHorizontalSplitter->setCollapsible(2, false);

    this->addWidget(mHorizontalSplitter);
    this->addWidget(mInfoLabel);

    mPlayBack->setPushDelegate([=](PlayBackWidget::PushType aType) { this->onPlayBackButtonPushed(aType); });
}

void TargetWidget::resizeEvent(QResizeEvent* aEvent) {
    QSplitter::resizeEvent(aEvent);
    if (mIsFirstTime) {
        mIsFirstTime = false;
        // mHorizontalSplitter->moveSplitter(this->geometry().width() / 4, 1);
    }
    // mHorizontalSplitter->moveSplitter(this->geometry().width() - mPlayBack->constantWidth(), 2);
}

void TargetWidget::setProject(core::Project* aProject) {
    mPlayBack->pushPauseButton();
    mProject = aProject;
    mObjTree->setProject(aProject);
    mTimeLine->setProject(aProject);
    mInfoLabel->setProject(aProject);
    mInfoLabel->setPlayback(mPlayBack);
    mPlayBack->aProject = aProject;
}

core::Frame TargetWidget::currentFrame() const { return mTimeLine->currentFrame(); }

void refreshMedia(core::Project* mProject, PlayBackWidget* mPlayBack){
    if(mProject->mediaRefresh){
        mPlayBack->aConf = mProject->pConf;
        mPlayBack->mediaPlayer = *mProject->mediaPlayer;
        mProject->mediaRefresh = false;
    }
}

void TargetWidget::stop() { mPlayBack->pushPauseButton(); }

void TargetWidget::suspend() { ++mSuspendCount; }

void TargetWidget::resume() {
    XC_ASSERT(mSuspendCount > 0);
    --mSuspendCount;
}

bool TargetWidget::isSuspended() const { return mSuspendCount > 0; }

void TargetWidget::onPlayBackButtonPushed(PlayBackWidget::PushType aType) {
    if (!mProject)
        return;
    if (aType == PlayBackWidget::PushType_Play) {
        refreshMedia(mProject, mPlayBack);
        mTimeLine->setPlayBackActivity(true, mPlayBack->aConf, &mPlayBack->mediaPlayer);
    } else if (aType == PlayBackWidget::PushType_Pause) {
        refreshMedia(mProject, mPlayBack);
        mTimeLine->setPlayBackActivity(false, mPlayBack->aConf, &mPlayBack->mediaPlayer);
    } else if (aType == PlayBackWidget::PushType_Step) {
        mTimeLine->setFrame(currentFrame().added(1));
    } else if (aType == PlayBackWidget::PushType_StepBack) {
        mTimeLine->setFrame(currentFrame().added(-1));
    } else if (aType == PlayBackWidget::PushType_Rewind) {
        mTimeLine->setFrame(core::Frame(0));
    } else if (aType == PlayBackWidget::PushType_Fast) {
        mTimeLine->setFrame(core::Frame(mProject->attribute().maxFrame()));
    } else if (aType == PlayBackWidget::PushType_Options) {
        openAnimationSettings();
    } else if (aType == PlayBackWidget::PushType_Loop) {
        mTimeLine->setPlayBackLoop(mPlayBack->isLoopChecked());
    }
}

void TargetWidget::openAnimationSettings() {
    if (!mProject)
        return;

    const int curFPS = mProject->attribute().fps();
    const int curMaxFrame = mProject->attribute().maxFrame();
    const bool curLoop = mPlayBack->isLoopChecked();

    QScopedPointer<AnimationSettingDialog> dialog(new AnimationSettingDialog(*mProject, curLoop, this));
    dialog->exec();
    if (dialog->result() != QDialog::Accepted)
        return;

    const int newFPS = dialog->fps();
    const int newMaxFrame = dialog->maxFrame();
    const bool newLoop = dialog->isLoopChecked();
    if (curFPS == newFPS && curMaxFrame == newMaxFrame && curLoop == newLoop)
        return;

    // One undo step covers every change made in the dialog, matching the
    // project attribute dialogs. Loop is the live playback flag, not a project
    // attribute, so it rides along in the same command.
    {
        cmnd::ScopedMacro macro(mProject->commandStack(), CmndName::tr("Change animation settings"));

        core::Project* projectPtr = mProject;
        auto command = new cmnd::Delegatable(
            [=]() {
                projectPtr->attribute().setFps(newFPS);
                projectPtr->attribute().setMaxFrame(newMaxFrame);
                mPlayBack->checkLoop(newLoop);
                mTimeLine->setPlayBackLoop(newLoop);
                auto event = core::ProjectEvent::maxFrameChangeEvent(*projectPtr);
                projectPtr->onProjectAttributeModified(event, false);
                this->refreshAnimationSettings();
            },
            [=]() {
                projectPtr->attribute().setFps(curFPS);
                projectPtr->attribute().setMaxFrame(curMaxFrame);
                mPlayBack->checkLoop(curLoop);
                mTimeLine->setPlayBackLoop(curLoop);
                auto event = core::ProjectEvent::maxFrameChangeEvent(*projectPtr);
                projectPtr->onProjectAttributeModified(event, true);
                this->refreshAnimationSettings();
            }
        );
        mProject->commandStack().push(command);
    }
}

void TargetWidget::refreshAnimationSettings() {
    if (auto* menu = mViaPoint.mainMenuBar()) {
        // Same fan-out as the project attribute dialogs: display, timeline
        // editor and playback driver all re-read the attributes.
        menu->onProjectAttributeUpdated();
        menu->onVisualUpdated();
    }
    // FPS changes the ruler strings (relative-to-FPS / timecode formats).
    mTimeLine->triggerOnTimeFormatChanged();
}


} // namespace gui
