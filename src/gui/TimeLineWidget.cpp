#include <QScrollBar>
#include "gui/TimeLineWidget.h"
#include "gui/PlayBackWidget.h"

namespace gui {

//-------------------------------------------------------------------------------------------------
TimeLineWidget::TimeLineWidget(
    GUIResources& aResources, ViaPoint& aViaPoint, core::Animator& aAnimator, QWidget* aParent
):
    QScrollArea(aParent),
    mGUIResources(aResources),
    mProject(),
    mAnimator(aAnimator),
    mInner(),
    mCameraInfo(),
    mAbstractCursor(),
    mVerticalScrollValue(0),
    mHorizontalScrollValue(0),
    mTimer(),
    mElapsed(),
    mBeginFrame(),
    mLastFrame(),
    mDoesLoop(false) {
    mInner = new TimeLineEditorWidget(aViaPoint, this);

    this->setWidget(mInner);
    this->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->setMouseTracking(true);
    this->connect(&mTimer, &QTimer::timeout, this, &TimeLineWidget::onPlayBackUpdated);

    mGUIResources.onThemeChanged.connect(this, &TimeLineWidget::onThemeUpdated);

    updateCamera();
}

void TimeLineWidget::setProject(core::Project* aProject) {
    mProject.reset();
    if (aProject) {
        mProject = aProject->pointee();
    }
    mInner->setProject(aProject);
}

void TimeLineWidget::setPlayBackActivity(bool aIsActive, std::vector<audioConfig>* pConf, mediaState* mediaPlayer) {
    mProject->pConf = pConf;
    mProject->mediaPlayer = mediaPlayer;
    if (aIsActive) {
        mTimer.setInterval(static_cast<int>(getOneFrameTime()));
        mTimer.start();
        mElapsed.start();
        mBeginFrame = currentFrame();
        mLastFrame = mBeginFrame;
        // Play audio
        AudioPlaybackWidget::aPlayer(pConf, true, mediaPlayer, getFps(), currentFrame().get(),
                                     mProject->attribute().maxFrame());
        mediaPlayer->playing = true;
    }
    else {
        mTimer.stop();
        mBeginFrame.set(0);
        mLastFrame.set(0);
        // Stop audio
        if(mediaPlayer->playing) {
            AudioPlaybackWidget::aPlayer(pConf, false, mediaPlayer, getFps(), currentFrame().get(),
                                         mProject->attribute().maxFrame()
            );
            mediaPlayer->playing = false;
        }
    }
    onPlayBackStateChanged(aIsActive);
}

void TimeLineWidget::setPlayBackLoop(bool aDoesLoop) { mDoesLoop = aDoesLoop; }

void TimeLineWidget::setFrame(core::Frame aFrame) {
    mInner->setFrame(aFrame);
    onFrameUpdated();
}

core::Frame TimeLineWidget::currentFrame() const { return mInner->currentFrame(); }

int TimeLineWidget::getFps() const { return mProject ? mProject->attribute().fps() : 60; }

double TimeLineWidget::getOneFrameTime() const { return 1000.0 / getFps(); }

QPoint TimeLineWidget::viewportTransform() const {
    // Do *not* use the value from the scrollbar itself, it does not work as intended.
    QPoint point = {-this->horizontalScrollBar()->value(), -mVerticalScrollValue};
    return point;
}

void TimeLineWidget::setScrollBarValue(const QPoint& aViewportTransform) {
    this->horizontalScrollBar()->setValue(-aViewportTransform.x());
    this->verticalScrollBar()->setValue(-aViewportTransform.y());
    mVerticalScrollValue = -aViewportTransform.y();
    mHorizontalScrollValue = -aViewportTransform.x();
}

void TimeLineWidget::updateCamera() {
    mCameraInfo.setScreenWidth(this->rect().width());
    mCameraInfo.setScreenHeight(this->rect().height());
    mCameraInfo.setLeftTopPos(QVector2D(viewportTransform()));
    mCameraInfo.setScale(1.0f);
    mInner->updateCamera(mCameraInfo);
}

void TimeLineWidget::updateCursor(const core::AbstractCursor& aCursor) {
    onCursorUpdated();
    if (mInner->updateCursor(aCursor)) {
        onFrameUpdated();
    }
}

//-------------------------------------------------------------------------------------------------
void TimeLineWidget::onTreeViewUpdated(QTreeWidgetItem* aTopNode) { mInner->updateLines(aTopNode); }

void TimeLineWidget::onScrollUpdated(int aValue) {
    this->verticalScrollBar()->setValue(aValue);
    mVerticalScrollValue = aValue;
    updateCamera();
}

void TimeLineWidget::onSelectionChanged(core::ObjectNode* aRepresent) { mInner->updateLineSelection(aRepresent); }

void TimeLineWidget::onPlayBackUpdated() {
    if (!mAnimator.isSuspended()) {
        const double oneFrameTime = getOneFrameTime();
        const core::Frame curFrame = currentFrame();
        double nextFrame = static_cast<double>(curFrame.getDecimal()) + 1.0;

        if (mDoesLoop && nextFrame > mInner->maxFrame()) {
            nextFrame = 0.0;
            mBeginFrame.set(0);
            mElapsed.restart();
            mTimer.setInterval(static_cast<int>(oneFrameTime));
        } else {
            if (curFrame.get() == mInner->maxFrame()) {
                mProject->animator().stop();
            }
            if (mLastFrame == curFrame) {
                const int elapsedTime = mElapsed.elapsed();
                const double elapsedFrame = elapsedTime / oneFrameTime;
                nextFrame = static_cast<double>(mBeginFrame.getDecimal()) + elapsedFrame;

                const double nextUpdateTime = oneFrameTime * (static_cast<int>(elapsedFrame) + 1);
                const double intervalTime = nextUpdateTime - elapsedTime;
                mTimer.setInterval(std::max(static_cast<int>(intervalTime), 1));
            } else {
                mBeginFrame = curFrame;
                mElapsed.restart();
                mTimer.setInterval(static_cast<int>(oneFrameTime));
            }
        }

#if 0
        setFrame(core::Frame::fromDecimal(nextFrame));
        mLastFrame = core::Frame::fromDecimal(nextFrame);
#else
        setFrame(core::Frame(static_cast<int>(nextFrame)));
        mLastFrame = core::Frame(static_cast<int>(nextFrame));
#endif
    }
}

void TimeLineWidget::onThemeUpdated(theme::Theme& aTheme) {
    QFile stylesheet(aTheme.path() + "/stylesheet/timelinewidget.ssa");
    if (stylesheet.open(QIODevice::ReadOnly | QIODevice::Text)) {
        this->setStyleSheet(QTextStream(&stylesheet).readAll());
        mInner->updateTheme(aTheme);
    }
}

void TimeLineWidget::onProjectAttributeUpdated() { mInner->updateProjectAttribute(); }

void TimeLineWidget::triggerOnTimeFormatChanged() {
    onTimeFormatChanged();
    updateCamera();
}

//-------------------------------------------------------------------------------------------------
void TimeLineWidget::mouseMoveEvent(QMouseEvent* aEvent) {
    QScrollArea::mouseMoveEvent(aEvent);
    if (mAbstractCursor.setMouseMove(aEvent, mCameraInfo)) {
        updateCursor(mAbstractCursor);
    }
}

void TimeLineWidget::mousePressEvent(QMouseEvent* aEvent) {
    QScrollArea::mousePressEvent(aEvent);
    if (mAbstractCursor.setMousePress(aEvent, mCameraInfo)) {
        updateCursor(mAbstractCursor);
    }
}

void TimeLineWidget::mouseReleaseEvent(QMouseEvent* aEvent) {
    QScrollArea::mouseReleaseEvent(aEvent);
    if (mAbstractCursor.setMouseRelease(aEvent, mCameraInfo)) {
        updateCursor(mAbstractCursor);
    }
}

void TimeLineWidget::mouseDoubleClickEvent(QMouseEvent* aEvent) {
    QScrollArea::mouseDoubleClickEvent(aEvent);
    if (mAbstractCursor.setMouseDoubleClick(aEvent, mCameraInfo)) {
        updateCursor(mAbstractCursor);
    }
}

void TimeLineWidget::wheelEvent(QWheelEvent* aEvent) {
    aEvent->ignore();
    QPoint viewTrans = viewportTransform();
    const QPoint cursor = aEvent->position().toPoint();
    mInner->updateWheel(aEvent);
    const QRect rectNext = mInner->rect();
    viewTrans.setX(cursor.x() * rectNext.width() / mInner->parentWidget()->size().width() * -1);
    setScrollBarValue(viewTrans);
    updateCamera();
}

void TimeLineWidget::resizeEvent(QResizeEvent* aEvent) {
    QScrollArea::resizeEvent(aEvent);
    updateCamera();
}

void TimeLineWidget::scrollContentsBy(int aDx, int aDy) {
    QScrollArea::scrollContentsBy(aDx, aDy);
    updateCamera();
}

} // namespace gui
