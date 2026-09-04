#include <QScrollBar>
#include <QApplication>
#include <QStyle>
#include <QMouseEvent>
#include <QPainter>
#include "gui/TimeLineWidget.h"
#include "gui/theme/Colors.h"

namespace gui {

//-------------------------------------------------------------------------------------------------
// The horizontal scrollbar paints its handle from the MODEL, not from Qt's
// value math. Qt renders the handle at [f(value), f(value)+len] where the
// position formula reserves len for the handle, so the handle can only reach
// the track's end when value == max. The union track's max is the viewport's
// END while the value is its START — a viewport short — so a value-driven
// handle can never be flush right. The model places the handle at the
// viewport's own pixel span [f(viewportStart), f(viewportEnd)] over the full
// track, which is flush on the side the viewport pokes out — symmetric.
class TimelineScrollBar: public QScrollBar {
public:
    TimelineScrollBar(QWidget* aParent = nullptr): QScrollBar(Qt::Horizontal, aParent) {}

    void setModel(int aTrackMin, int aTrackMax, int aViewportStart, int aViewportEnd) {
        mTrackMin = aTrackMin;
        mTrackMax = aTrackMax;
        mViewportStart = aViewportStart;
        mViewportEnd = aViewportEnd;
        update();
    }

    // The handle's pixel rect per the model: the viewport's span over the
    // full track (1:1 linear map, no style involvement). Used by the drag hit
    // test so it matches what is painted.
    QRect handleRect() const {
        const int span = qMax(1, width());
        const int range = mTrackMax - mTrackMin;
        if (range <= 0) {
            return QRect();
        }
        auto fx = [&](int aValue) {
            const double t = double(aValue - mTrackMin) / range;
            return static_cast<int>(t * span + 0.5);
        };
        const int left = fx(mViewportStart);
        const int right = fx(mViewportEnd);
        return QRect(left, 0, qMax(right - left, 1), height());
    }

    // The drag state (set by TimeLineWidget) so the pill lights up while the
    // thumb is being dragged, matching the style's hot state.
    void setDragging(bool aDragging) {
        if (mDragging != aDragging) {
            mDragging = aDragging;
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent*) override {
        // Painted entirely from the model: no track (like the app's other
        // scrollbars, the surface shows through), just the viewport's pill at
        // [f(viewportStart), f(viewportEnd)] over the full width. Matches
        // AppStyle's scrollbar rendering (4px pill, no border, theme colors);
        // the style's own value-driven handle cannot represent the model.
        QPainter painter(this);
        const QRect handle = handleRect();
        if (handle.isEmpty()) {
            return;
        }
        QStyleOptionSlider opt;
        initStyleOption(&opt);
        // Hot when dragged or hovered (initStyleOption never reports a
        // sub-control here; the style's own hover tracking is not active
        // for a fully custom-painted bar).
        const bool hot = mDragging || underMouse();
        constexpr int kPillThickness = 4;
        constexpr int kPillRadius = 2;
        const int y = (height() - kPillThickness) / 2;
        const QRect pill(handle.x(), y, handle.width(), kPillThickness);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(hot ? theme::Colors::current().hover : theme::Colors::current().outline);
        painter.drawRoundedRect(pill, kPillRadius, kPillRadius);
    }

private:
    int mTrackMin = 0;
    int mTrackMax = 0;
    int mViewportStart = 0;
    int mViewportEnd = 0;
    bool mDragging = false;
};

//-------------------------------------------------------------------------------------------------
TimeLineWidget::TimeLineWidget(
    GUIResources& aResources, ViaPoint& aViaPoint, core::Animator& aAnimator, QWidget* aParent
):
    QWidget(aParent),
    mGUIResources(aResources),
    mProject(),
    mAnimator(aAnimator),
    mInner(),
    mHorizontalScrollBar(),
    mCameraInfo(),
    mAbstractCursor(),
    mVerticalScrollValue(0),
    mHorizontalOffset(0),
    mPendingScrollbarAdopt(false),
    mScrollbarDragActive(false),
    mScrollbarDragAnchorValue(0),
    mScrollbarDragAnchorX(0),
    mScrollbarDragRatio(0),
    mIsPanning(false),
    mPanGrabPos(),
    mPanStartTransform(),
    mTimer(),
    mElapsed(),
    mBeginFrame(),
    mLastFrame(),
    mDoesLoop(true) {
    // The content surface covers the widget; its paint translates by the view
    // origin, so it is viewport-fixed (the ruler, lanes and selection box are
    // never clipped by the frame range).
    mInner = new TimeLineEditorWidget(aViaPoint, this);
    mHorizontalScrollBar = new TimelineScrollBar(this);

    this->setMouseTracking(true);
    this->setFocusPolicy(Qt::StrongFocus);
    this->connect(&mTimer, &QTimer::timeout, this, &TimeLineWidget::onPlayBackUpdated);

    // The horizontal scrollbar is a one-way projection of the free offset:
    // user interactions (track click, wheel, keyboard) emit a slider action,
    // so the resulting value is adopted into the offset; programmatic writes
    // never emit actions and must never touch it. The value is the viewport's
    // start (see projectHorizontalScrollBar); the thumb drag is handled
    // entirely by us (see eventFilter) so the mapping stays anchored and the
    // track stays live.
    this->connect(mHorizontalScrollBar, &QScrollBar::actionTriggered, this, [this](int) {
        mPendingScrollbarAdopt = true;
    });
    this->connect(mHorizontalScrollBar, &QScrollBar::valueChanged, this, [this](int aValue) {
        if (!mPendingScrollbarAdopt)
            return;
        mPendingScrollbarAdopt = false;
        mHorizontalOffset = -aValue; // value = viewportStart = v = -offset
        // The track is the union of the frame range and the current viewport:
        // it follows the view on every offset change, live (no freeze).
        projectHorizontalScrollBar();
        updateCamera();
    });
    mHorizontalScrollBar->installEventFilter(this);

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
    projectHorizontalScrollBar();
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
    // The content origin in surface coordinates. The horizontal component is
    // free: zoom anchoring and pan can place it anywhere, including outside
    // the frame range (content floating with empty space beside it). Read the
    // offset member, never the scrollbar value — the scrollbar is a projection
    // of it, not the other way around.
    QPoint point = {mHorizontalOffset, -mVerticalScrollValue};
    return point;
}

void TimeLineWidget::setViewTransform(const QPoint& aViewportTransform) {
    mHorizontalOffset = aViewportTransform.x();
    mVerticalScrollValue = -aViewportTransform.y();
    projectHorizontalScrollBar();
    updateCamera();
}

void TimeLineWidget::projectHorizontalScrollBar() {
    // One axis everywhere: content pixels, 1 unit = 1 px. Frame 0's tick sits
    // at the left margin gutter (kTimeLineMargin); the last frame's tick and
    // range marker sit at margin + its scale position (frameEndPixel, the
    // renderer's range-marker x1 convention). The view's scroll position
    // v = -offset is the content position at the view's left edge.
    //
    //   frameRange = [frameStart, frameEnd]     = [0, frameEndPixel]
    //   viewport   = [viewportStart, viewportEnd] = [v, v + W]
    //   track = [min(frameStart, viewportStart), max(frameEnd, viewportEnd)]
    //   value = viewportStart;  pageStep = viewportEnd - viewportStart
    //
    // Both entities are start/end pairs on the same axis — no widths, no
    // view-position units. The track is the union and never clamps the view;
    // the handle is painted at the viewport's own pixel span (see
    // TimelineScrollBar), so it is flush left when viewportStart <=
    // frameStart and flush right when viewportEnd >= frameEnd — symmetric.
    // frameEnd is the last frame's position PLUS the trailing margin: a
    // fixed 24px, the same token as the left gutter before frame 0's tick,
    // so flush-right leaves a full margin of empty track after the last
    // frame (the centered number overhangs it slightly; that is accepted).
    const int frameStart = 0;
    const int frameEnd = mInner->frameEndPixel() + ctrl::TimeLineEditor::kTimeLineMargin;
    const int windowWidth = mInner->width();

    const int viewportStart = -mHorizontalOffset;
    const int viewportEnd = viewportStart + windowWidth;

    const int trackStart = qMin(frameStart, viewportStart);
    const int trackEnd = qMax(frameEnd, viewportEnd);

    auto* bar = static_cast<TimelineScrollBar*>(mHorizontalScrollBar);
    bar->setModel(trackStart, trackEnd, viewportStart, viewportEnd);
    mHorizontalScrollBar->setRange(trackStart, trackEnd);
    mHorizontalScrollBar->setPageStep(qMax(1, windowWidth));
    mHorizontalScrollBar->setSingleStep(kVerticalStep);
    mHorizontalScrollBar->setVisible(true);
    // The viewport is always within the union, so the value never clamps.
    mHorizontalScrollBar->setValue(viewportStart);
}

QSize TimeLineWidget::minimumSizeHint() const {
    // The plain-widget default hint is invalid, and QSplitter treats an
    // invalid hint as (1,1) — the object tree would swallow the split at
    // startup. Claim a usable default so both panes start reasonable.
    const int hbarHeight = mHorizontalScrollBar ? mHorizontalScrollBar->sizeHint().height() : 0;
    return QSize(280, ctrl::TimeLineEditor::kHeaderHeight + 2 * ctrl::TimeLineRow::kHeight + hbarHeight);
}

QSize TimeLineWidget::sizeHint() const {
    const int hbarHeight = mHorizontalScrollBar ? mHorizontalScrollBar->sizeHint().height() : 0;
    return QSize(640, ctrl::TimeLineEditor::kHeaderHeight + 4 * ctrl::TimeLineRow::kHeight + hbarHeight);
}

//-------------------------------------------------------------------------------------------------
// Custom scrollbar thumb drag. Qt's own drag recomputes the value from the
// mouse against the live range, which compounds while the union track grows
// (the same mouse delta covers more value). Ours anchors the value<->pixel
// ratio at press and moves the view by our own math: constant drag speed,
// and the track keeps updating live with the view.

void TimeLineWidget::beginScrollbarDrag(const QPoint& aPos) {
    // Anchor on the viewport's true start (v = -offset) so the drag moves the
    // view from where it actually is; the ratio is frozen at press.
    mScrollbarDragAnchorValue = -mHorizontalOffset;
    mScrollbarDragAnchorX = aPos.x();
    // The painted handle moves W/range * span px per value unit, so a 1:1
    // thumb follow needs value-per-pixel = range/span (Qt's +pageStep ratio
    // belongs to its own handle geometry, not the model's).
    const double range = mHorizontalScrollBar->maximum() - mHorizontalScrollBar->minimum();
    const double span = qMax(1, mHorizontalScrollBar->width());
    mScrollbarDragRatio = range / span;
    mScrollbarDragActive = true;
    static_cast<TimelineScrollBar*>(mHorizontalScrollBar)->setDragging(true);
    // All mouse events now come to the scrollbar even when the cursor leaves
    // the handle: the grab keeps the drag alive while the thumb rides the
    // track's edge (flush left/right of the union) as the view keeps
    // floating beyond the frame range.
    mHorizontalScrollBar->grabMouse();
}

void TimeLineWidget::moveScrollbarDrag(const QPoint& aPos) {
    const double newValue =
        mScrollbarDragAnchorValue + (aPos.x() - mScrollbarDragAnchorX) * mScrollbarDragRatio;
    const int value = qRound(newValue);
    mHorizontalOffset = -value; // viewportStart = v = -offset
    updateCamera();
    // The track follows the view live; setValue here is guarded (no slider
    // action precedes it) and only repositions the thumb.
    projectHorizontalScrollBar();
}

void TimeLineWidget::endScrollbarDrag() {
    mScrollbarDragActive = false;
    static_cast<TimelineScrollBar*>(mHorizontalScrollBar)->setDragging(false);
    mHorizontalScrollBar->releaseMouse();
}

bool TimeLineWidget::eventFilter(QObject* aObject, QEvent* aEvent) {
    if (aObject == mHorizontalScrollBar) {
        switch (aEvent->type()) {
        case QEvent::MouseButtonPress: {
            auto* me = static_cast<QMouseEvent*>(aEvent);
            if (me->button() == Qt::LeftButton) {
                const QPoint pos = me->position().toPoint();
                const auto* bar = static_cast<TimelineScrollBar*>(mHorizontalScrollBar);
                const QRect handle = bar->handleRect();
                if (handle.contains(pos)) {
                    beginScrollbarDrag(pos);
                } else if (!handle.isEmpty()) {
                    // Track click pages toward the click, like a stock
                    // scrollbar's page buttons, routed through our own math.
                    // Every press is swallowed: Qt's native thumb drag maps
                    // the mouse against its value-driven handle geometry
                    // (handle-length reserved), which differs from the model
                    // pill and would detach the thumb from the cursor with
                    // live-remapped compounding gain (see the model
                    // scrollbar comment above).
                    const int dir = pos.x() < handle.center().x() ? -1 : 1;
                    scrollViewBy(dir * mHorizontalScrollBar->pageStep());
                }
                return true; // swallow: Qt must not start its own drag
            }
            break;
        }
        case QEvent::Wheel: {
            auto* we = static_cast<QWheelEvent*>(aEvent);
            if (mScrollbarDragActive) {
                return true; // ignore wheels mid-drag; the drag owns the view
            }
            // Qt's own wheel handler clamps at the range ends, which dead-zones
            // at the track boundary (the union should keep extending); move the
            // view ourselves with the same convention Qt uses (horizontal bars
            // invert: wheel up scrolls left) and magnitude (wheelScrollLines
            // steps, or one page with Shift/Ctrl).
            const int delta =
                we->angleDelta().y() != 0 ? we->angleDelta().y() : we->angleDelta().x();
            if (delta == 0)
                return false; // pixel-delta-only: let Qt handle it
            const int dir = delta > 0 ? -1 : 1;
            const int stepSize =
                (we->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier))
                ? mHorizontalScrollBar->pageStep()
                : qMax(1, QApplication::wheelScrollLines()) * mHorizontalScrollBar->singleStep();
            scrollViewBy(dir * stepSize);
            return true;
        }
        case QEvent::KeyPress:
            // Deliberately absent: QScrollBar is Qt::NoFocus, so keys never
            // reach the bar; the timeline's own keyPressEvent handles
            // Left/Right/PageUp/PageDown on the focused surface.
            break;
        case QEvent::MouseMove:
            if (mScrollbarDragActive) {
                moveScrollbarDrag(static_cast<QMouseEvent*>(aEvent)->position().toPoint());
                return true;
            }
            break;
        case QEvent::MouseButtonRelease:
            if (mScrollbarDragActive) {
                endScrollbarDrag();
                return true;
            }
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(aObject, aEvent);
}

// Moves the view by aValueDelta in value units (value = the viewport's
// start). Anchored on the view's true position, so scrolling from a float
// moves the view from where it actually is; the track follows live.
void TimeLineWidget::scrollViewBy(int aValueDelta) {
    const int newValue = -mHorizontalOffset + aValueDelta;
    mHorizontalOffset = -newValue;
    updateCamera();
    projectHorizontalScrollBar();
}

void TimeLineWidget::updateCamera() {
    mCameraInfo.setScreenWidth(mInner->width());
    mCameraInfo.setScreenHeight(mInner->height());
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
            mLastFrame = core::Frame(INT32_MAX);
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

        setFrame(core::Frame(static_cast<int>(nextFrame)));
        mLastFrame = core::Frame(static_cast<int>(nextFrame));
    }
}

void TimeLineWidget::onThemeUpdated(theme::Theme& aTheme) {
    this->setStyleSheet(aTheme.loadStylesheet("timelinewidget.ssa"));
    mInner->updateTheme(aTheme);
}

void TimeLineWidget::onProjectAttributeUpdated() {
    mInner->updateProjectAttribute();
    // maxFrame may have changed: the union track and page step follow the
    // frame range's extent, so re-project (updateSize is repaint-only now).
    projectHorizontalScrollBar();
}

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
    if (mAbstractCursor.setMouseRelease(aEvent, mCameraInfo)) {
        updateCursor(mAbstractCursor, aEvent->modifiers());
    }
}

void TimeLineWidget::mouseDoubleClickEvent(QMouseEvent* aEvent) {
    if (mAbstractCursor.setMouseDoubleClick(aEvent, mCameraInfo)) {
        updateCursor(mAbstractCursor, aEvent->modifiers());
    }
}

void TimeLineWidget::wheelEvent(QWheelEvent* aEvent) {
    // A slider action may have fired without a value change (e.g. dragging at
    // the range end); it must not leak into a later zoom's scrollbar
    // adjustments.
    mPendingScrollbarAdopt = false;
    QPoint viewTrans = viewportTransform();

    // Check if Shift is pressed for horizontal scrolling
    if (aEvent->modifiers() & Qt::ShiftModifier) {
        // Horizontal scroll, free like pan: the view can float anywhere, so
        // there is no clamp to the frame range.
        const int delta = aEvent->angleDelta().y();
        const int scrollStep = 50; // Pixels to scroll per wheel step
        viewTrans.setX(viewTrans.x() + (delta > 0 ? scrollStep : -scrollStep));

        setViewTransform(viewTrans);
        aEvent->accept();
        return;
    }

    // Zoom to mouse: keep the content point under the cursor at the same
    // screen position after zoom
    const int mouseViewportX = aEvent->position().toPoint().x();
    const int mouseContentX = mouseViewportX - viewTrans.x();

    int pixelAfter = 0;
    if (!mInner->updateWheel(aEvent, mouseContentX, pixelAfter)) {
        aEvent->accept();
        return; // zoom clamped at the floor/ceiling: the wheel did nothing, so scroll stays put
    }

    // The anchor is exact and pixel-based: the content point under the mouse
    // stays under it, whether or not it lies inside the frame range. No
    // frame-range clamp either — the geometry bounds the offset to where the
    // content intersects the viewport, so the timeline can never be lost by
    // zooming.
    const int newScrollOffset = mouseViewportX - pixelAfter;
    viewTrans.setX(newScrollOffset);

    setViewTransform(viewTrans);
    aEvent->accept();
}

void TimeLineWidget::resizeEvent(QResizeEvent* aEvent) {
    QWidget::resizeEvent(aEvent);
    // The surface fills the area above the scrollbar; the scrollbar stays at
    // the bottom. The camera follows the surface size.
    const int hbarHeight = mHorizontalScrollBar->sizeHint().height();
    mHorizontalScrollBar->setGeometry(0, this->height() - hbarHeight, this->width(), hbarHeight);
    mInner->setGeometry(0, 0, this->width(), this->height() - hbarHeight);
    updateCamera();
    // The viewport width changed, so the union track (frame range + viewport)
    // and the page step must be recomputed.
    projectHorizontalScrollBar();
}

// MMB pan: translate the viewport in both axes. Horizontal is free, consistent
// with the canvas viewport pan — the content can be dragged anywhere, and the
// scrollbar pins at the ends while it floats. Vertical goes through the object
// tree so the two views stay aligned; the tree is the binding constraint, it
// clamps to its own range and syncs the actual value back via onScrollUpdated.
void TimeLineWidget::panTo(const QPoint& aTransform) {
    if (aTransform.x() != viewportTransform().x()) {
        setViewTransform(QPoint(aTransform.x(), viewportTransform().y()));
    }
    // Vertical pan never writes the timeline's own state: the request goes to
    // the tree, which clamps and syncs the actual value back via onScrollUpdated.
    const int targetY = -aTransform.y();
    if (targetY != mVerticalScrollValue) {
        onVerticalScrollRequested(targetY);
    }
}

// Vertical keyboard scrolling must stay in lock-step with the object tree:
// route vertical keys through the tree so both views clamp to the same bounds;
// horizontal keys move the free offset directly.
void TimeLineWidget::keyPressEvent(QKeyEvent* aEvent) {
    const int current = mVerticalScrollValue;
    int target = current;
    bool isVertical = false;
    switch (aEvent->key()) {
    case Qt::Key_Up:       target = current - kVerticalStep; isVertical = true; break;
    case Qt::Key_Down:     target = current + kVerticalStep; isVertical = true; break;
    case Qt::Key_PageUp:   target = current - this->height(); isVertical = true; break;
    case Qt::Key_PageDown: target = current + this->height(); isVertical = true; break;
    case Qt::Key_Left: {
        QPoint viewTrans = viewportTransform();
        viewTrans.setX(viewTrans.x() - kVerticalStep);
        setViewTransform(viewTrans);
        aEvent->accept();
        return;
    }
    case Qt::Key_Right: {
        QPoint viewTrans = viewportTransform();
        viewTrans.setX(viewTrans.x() + kVerticalStep);
        setViewTransform(viewTrans);
        aEvent->accept();
        return;
    }
    default: break;
    }
    if (isVertical) {
        onVerticalScrollRequested(target);
        aEvent->accept();
        return;
    }
    QWidget::keyPressEvent(aEvent);
}

} // namespace gui
