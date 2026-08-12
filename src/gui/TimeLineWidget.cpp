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
    mIsPanning(false),
    mPanGrabPos(),
    mPanStartTransform(),
    mTimer(),
    mElapsed(),
    mBeginFrame(),
    mLastFrame(),
    mDoesLoop(true) {
    mInner = new TimeLineEditorWidget(aViaPoint, this);

    this->setWidget(mInner);
    this->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->setMouseTracking(true);
    this->connect(&mTimer, &QTimer::timeout, this, &TimeLineWidget::onPlayBackUpdated);

    mGUIResources.onThemeChanged.connect(this, &TimeLineWidget::onThemeUpdated);

    // Audio follows the playhead on the playback clock
    onFrameUpdated.connect(this, &TimeLineWidget::syncAudioTracks);

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
        AudioPlaybackWidget::aPlayer(pConf, true, mediaPlayer, getFps(), currentFrame().get());
        mediaPlayer->playing = true;
    }
    else {
        mTimer.stop();
        mBeginFrame.set(0);
        mLastFrame.set(0);
        // Stop audio
        if(mediaPlayer->playing) {
            AudioPlaybackWidget::aPlayer(pConf, false, mediaPlayer, getFps(), currentFrame().get());
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

void TimeLineWidget::updateCursor(const core::AbstractCursor& aCursor, Qt::KeyboardModifiers aModifiers) {
    onCursorUpdated();
    if (mInner->updateCursor(aCursor, aModifiers)) {
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

void TimeLineWidget::syncAudioTracks() {
    if (!mProject)
        return;
    AudioPlaybackWidget::syncTracks(mProject.get(), currentFrame().get(), mLastFrame.get(), getFps());
}

//-------------------------------------------------------------------------------------------------
void TimeLineWidget::mouseMoveEvent(QMouseEvent* aEvent) {
    if (mIsPanning) {
        panTo(mPanStartTransform + (aEvent->position().toPoint() - mPanGrabPos));
        aEvent->accept();
        return;
    }
    QScrollArea::mouseMoveEvent(aEvent);
    if (mAbstractCursor.setMouseMove(aEvent, mCameraInfo)) {
        updateCursor(mAbstractCursor, aEvent->modifiers());
    }
}

void TimeLineWidget::mousePressEvent(QMouseEvent* aEvent) {
    if (aEvent->button() == Qt::MiddleButton) {
        mPanGrabPos = aEvent->position().toPoint();
        mPanStartTransform = viewportTransform();
        mIsPanning = true;
        setCursor(Qt::ClosedHandCursor);
        aEvent->accept();
        return;
    }
    QScrollArea::mousePressEvent(aEvent);
    if (mAbstractCursor.setMousePress(aEvent, mCameraInfo)) {
        updateCursor(mAbstractCursor, aEvent->modifiers());
    }
}

void TimeLineWidget::mouseReleaseEvent(QMouseEvent* aEvent) {
    if (mIsPanning && aEvent->button() == Qt::MiddleButton) {
        mIsPanning = false;
        unsetCursor();
        aEvent->accept();
        return;
    }
    QScrollArea::mouseReleaseEvent(aEvent);
    if (mAbstractCursor.setMouseRelease(aEvent, mCameraInfo)) {
        updateCursor(mAbstractCursor, aEvent->modifiers());
    }
}

void TimeLineWidget::mouseDoubleClickEvent(QMouseEvent* aEvent) {
    QScrollArea::mouseDoubleClickEvent(aEvent);
    if (mAbstractCursor.setMouseDoubleClick(aEvent, mCameraInfo)) {
        updateCursor(mAbstractCursor, aEvent->modifiers());
    }
}

void TimeLineWidget::wheelEvent(QWheelEvent* aEvent) {
    aEvent->ignore();
    QPoint viewTrans = viewportTransform();

    // Check if Shift is pressed for horizontal scrolling
    if (aEvent->modifiers() & Qt::ShiftModifier) {
        // Horizontal scroll: adjust the X position based on wheel delta
        const int delta = aEvent->angleDelta().y();
        const int scrollStep = 50; // Pixels to scroll per wheel step
        viewTrans.setX(viewTrans.x() + (delta > 0 ? scrollStep : -scrollStep));

        // Clamp to valid range
        const int maxScroll = mInner->width() - viewport()->width();
        viewTrans.setX(qBound(-maxScroll, viewTrans.x(), 0));

        setScrollBarValue(viewTrans);
        updateCamera();
        return;
    }

    // Zoom to mouse: keep the frame under the cursor at the same screen position after zoom
    const int mouseViewportX = aEvent->position().toPoint().x();
    const int mouseContentX = mouseViewportX - viewTrans.x();
    
    int frameBefore = 0;
    int pixelAfter = 0;
    mInner->updateWheel(aEvent, mouseContentX, frameBefore, pixelAfter);
    
    // Adjust scroll so the frame stays under the mouse
    // viewportX = contentX + scrollOffset, so: newScrollOffset = mouseViewportX - pixelAfter
    const int newScrollOffset = mouseViewportX - pixelAfter;
    const int maxScroll = mInner->width() - viewport()->width();
    viewTrans.setX(qBound(-maxScroll, newScrollOffset, 0));
    
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

// MMB pan: translate the viewport in both axes. Horizontal is the timeline's
// own scrollbar; vertical goes through the object tree so the two views stay
// aligned. The tree is the binding constraint: it clamps to its own range and
// syncs the actual value back, so the timeline can never scroll further than
// the tree allows (its own content can be taller than the tree's range).
void TimeLineWidget::panTo(const QPoint& aTransform) {
    const int maxX = qMax(0, mInner->width() - viewport()->width());
    const int newX = qBound(-maxX, aTransform.x(), 0);
    if (newX != viewportTransform().x()) {
        this->horizontalScrollBar()->setValue(-newX);
        mHorizontalScrollValue = -newX;
        updateCamera();
    }
    // Vertical pan never writes the timeline's own state: the request goes to
    // the tree, which clamps and syncs the actual value back via onScrollUpdated.
    const int maxY = qMax(0, mInner->height() - viewport()->height());
    const int newY = qBound(-maxY, aTransform.y(), 0);
    if (newY != -mVerticalScrollValue) {
        onVerticalScrollRequested(-newY);
    }
}

// Vertical keyboard scrolling must stay in lock-step with the object tree:
// the timeline's own (hidden) scrollbar spans the full content height, which
// can exceed the tree's scroll range. Route vertical keys through the tree so
// both views clamp to the same bounds; horizontal keys keep the default
// QScrollArea behavior.
void TimeLineWidget::keyPressEvent(QKeyEvent* aEvent) {
    const int current = mVerticalScrollValue;
    int target = current;
    bool isVertical = false;
    switch (aEvent->key()) {
    case Qt::Key_Up:       target = current - kVerticalStep; isVertical = true; break;
    case Qt::Key_Down:     target = current + kVerticalStep; isVertical = true; break;
    case Qt::Key_PageUp:   target = current - verticalScrollBar()->pageStep(); isVertical = true; break;
    case Qt::Key_PageDown: target = current + verticalScrollBar()->pageStep(); isVertical = true; break;
    default: break;
    }
    if (isVertical) {
        onVerticalScrollRequested(target);
        aEvent->accept();
        return;
    }
    QScrollArea::keyPressEvent(aEvent);
}

} // namespace gui
