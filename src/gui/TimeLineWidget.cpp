#include <QScrollBar>
#include <QApplication>
#include <QStyle>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOptionSlider>
#include <QSettings>
#include <QDateTime>
#include <cmath>
#include "gui/TimeLineWidget.h"

#include "ctrl/AutoScroll.h"
#include "gui/theme/Colors.h"
#include "ctrl/graph/graph_ScrollBarGeometry.h"

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
        // The pill is the application's scroll bar's, from the one definition both
        // gui::AppStyle's CC_ScrollBar and the graph panel's lanes paint: 4px centred
        // in this bar's lane (the widget's own rect), radius-2, across the model's
        // handle. Nothing about it is restated here.
        const ctrl::graph::scrollBar::Pill pill =
            ctrl::graph::scrollBar::pill(rect(), handle, Qt::Horizontal);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(hot ? theme::Colors::current().hover : theme::Colors::current().outline);
        painter.drawRoundedRect(pill.rect, pill.radius, pill.radius);
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
    mViaPoint(aViaPoint),
    mProject(),
    mAnimator(aAnimator),
    mInner(),
    mHorizontalScrollBar(),
    mCameraInfo(),
    mAbstractCursor(),
    mVerticalScrollValue(0),
    mHorizontalOffset(0),
    mPendingScrollPixels(0.0),
    mPendingZoomPixels(0.0),
    mPendingScrollbarAdopt(false),
    mReportedWindowLo(0.0),
    mReportedWindowHi(0.0),
    mScrollbarDragActive(false),
    mScrollbarDragAnchorValue(0),
    mScrollbarDragAnchorX(0),
    mScrollbarDragRatio(0),
    mFitPending(false),
    mIsPanning(false),
    mPanButton(Qt::NoButton),
    mPanByKey(false),
    mPanGrabPos(),
    mPanStartTransform(),
    mTimer(),
    mMarqueeScrollTimer(),
    mMarqueePointer(),
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
    mMarqueeScrollTimer.setInterval(ctrl::autoscroll::kIntervalMs);
    mMarqueeScrollTimer.setTimerType(Qt::PreciseTimer);
    this->connect(&mMarqueeScrollTimer, &QTimer::timeout, this, &TimeLineWidget::autoScrollMarquee);

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

    // The ruler drives the same cursor machinery as the track surface; its
    // frame changes (header seek / playhead drag) must notify the playback
    // chain like the surface's do.
    this->connect(mInner, &TimeLineEditorWidget::rulerFrameChanged, this, [this]() { onFrameUpdated(); });

    // Pan by key: the same configurable MoveCanvas binding the viewport pans
    // with (space by default). Holding it and left-dragging pans the timeline
    // like middle-drag does; the ruler is told too, so a space+left press on
    // the strip pans instead of seeking.
    {
        auto key = mViaPoint.keyCommandMap()->get("MoveCanvas");
        if (key) {
            key->addListener(
                [this]() {
                    mPanByKey = true;
                    mInner->setPanKeyHeld(true);
                },
                [this]() {
                    mPanByKey = false;
                    mInner->setPanKeyHeld(false);
                }
            );
        }
    }

    mGUIResources.onThemeChanged.connect(this, &TimeLineWidget::onThemeUpdated);

    // Audio follows the playhead on the playback clock
    onFrameUpdated.connect(this, &TimeLineWidget::syncAudioTracks);

    updateCamera();
}

void TimeLineWidget::setProject(core::Project* aProject) {
    // BEFORE the reset: whether this widget was showing a project a moment ago.
    const bool hadProject = static_cast<bool>(mProject);

    mProject.reset();
    if (aProject) {
        mProject = aProject->pointee();
    }
    mInner->setProject(aProject);
    projectHorizontalScrollBar();

    // THE NEW-PROJECT FIT. A project that arrives from NO project is newly
    // opened or newly created (MainWindow's open/new paths both clear the
    // timeline first), so this widget has no view of it and it opens FITTED to
    // its frame range instead of at whatever zoom the previous document left.
    // A tab switch hands over a project this widget is already showing - that
    // live zoom/pan IS the saved view, so it is kept (the rule is
    // ctrl::TimeLineEditor::fitsOnProjectAttach). When the surface has no
    // width yet, fitFrameRange() remembers the request for updateCamera.
    if (ctrl::TimeLineEditor::fitsOnProjectAttach(hadProject, aProject != nullptr))
        this->fitFrameRange();
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
    // A frame that did not move changes nothing: the playhead, the ruler's pill and
    // the document's blended state are all already at it. Returning here is what keeps
    // a playback tick that lands inside the CURRENT frame from pushing a whole-tree
    // re-blend and a full viewport repaint (measured: 78 ticks/s ran for a 60 fps
    // project, i.e. ~20% of the frames painted were the same frame twice).
    if (mInner->currentFrame() == aFrame) {
        return;
    }
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
    // One axis everywhere: content pixels, 1 unit = 1 px. The range's first
    // frame's tick sits at the left margin gutter (kTimeLineMargin); the last
    // frame's tick and range marker sit at margin + its scale position
    // (frameEndPixel, the renderer's range-marker x1 convention). The view's
    // scroll position v = -offset is the content position at the view's left
    // edge.
    //
    //   frameRange = [frameStartPixel, frameEndPixel]
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
    // fixed 24px, the same token as the leading gutter before the first
    // frame's tick, so flush-right leaves a full margin of empty track after
    // the last frame (the centered number overhangs it slightly; that is
    // accepted).
    const int frameStart = mInner->frameStartPixel();
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

    // Every path that can move the visible frame window lands here (a scroll, a
    // zoom, a slider adoption, a resize, a new frame range, a project switch), so
    // this is the one place the change is reported from - and only when it really
    // changed.
    notifyViewRange();
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
    // A fit requested while the surface had no width (a project opened on a hidden
    // timeline) is applied on the first pass that knows one. fitFrameRange clears the
    // flag before it re-enters here through setViewTransform, so this cannot loop.
    if (mFitPending && mInner->width() > 0 && editor()) {
        mFitPending = false;
        fitFrameRange();
    }

    // KEEP THE CONTENT IN REACH. The track never clamps the view (see the scrollbar note), so a pan
    // may float past the content - but a view that has left the content entirely is a view of
    // nothing. The bound is an OVERLAP, not a centring: the view is free to move and only stops at
    // the ends, so panning works at every zoom.
    if (const ctrl::TimeLineEditor* controller = editor()) {
        const int contentSpan = controller->frameEndPixel() + ctrl::TimeLineEditor::kTimeLineMargin;
        const int viewport = mInner->width();
        // The view's start v = -offset and its span is [v, v + viewport]; overlap with the
        // content [0, contentSpan] means v <= contentSpan and v + viewport >= 0.
        if (viewport > 0)
            mHorizontalOffset = std::max(-contentSpan, std::min(viewport, mHorizontalOffset));
    }

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
void TimeLineWidget::setRows(const gui::AnimationRows& aRows) { mInner->setRows(aRows); }

void TimeLineWidget::onTreeViewUpdated() {}

void TimeLineWidget::onScrollUpdated(int aValue) {
    mVerticalScrollValue = aValue;
    updateCamera();
}

void TimeLineWidget::onSelectionChanged(core::ObjectNode* aRepresent) { mInner->updateLineSelection(aRepresent); }

void TimeLineWidget::onPlayBackUpdated() {
    if (!mAnimator.isSuspended()) {
        const double oneFrameTime = getOneFrameTime();
        const core::Frame curFrame = currentFrame();

        // The effective playback range (the temporary window overrides the
        // project frame range while active): the playback clock stays inside
        // [playStart, playEnd]. Navigation is free (the playhead never
        // clamps), so the tick clamps its own result.
        const util::Range range = mInner->playbackRange();
        const int playStart = range.min();
        const int playEnd = range.max();

        double nextFrame = static_cast<double>(curFrame.getDecimal()) + 1.0;

        if (nextFrame < playStart) {
            // Playback may start outside the window (or with the window set
            // mid-flight): jump to the window start.
            nextFrame = playStart;
            mBeginFrame.set(playStart);
            mElapsed.restart();
            mTimer.setInterval(static_cast<int>(oneFrameTime));
        } else if (mDoesLoop && nextFrame > playEnd) {
            nextFrame = playStart;
            mBeginFrame.set(playStart);
            mElapsed.restart();
            mTimer.setInterval(static_cast<int>(oneFrameTime));
        } else {
            // >= (not ==) so a playhead scrubbed past the end still stops.
            if (curFrame.get() >= playEnd) {
                mProject->animator().stop();
            }
            if (mLastFrame == curFrame) {
                const int elapsedTime = mElapsed.elapsed();
                const double elapsedFrame = elapsedTime / oneFrameTime;
                nextFrame = static_cast<double>(mBeginFrame.getDecimal()) + elapsedFrame;

                // ARM FOR THE NEXT FRAME BOUNDARY, ROUNDED UP. Truncating the interval
                // woke the timer up to a millisecond BEFORE the boundary, which could not
                // advance the frame: that tick re-armed for the remaining fraction (1 ms)
                // and fired again at the boundary, so a 60 fps project got ~78 ticks/s
                // - 30 % of them painting a frame that had not changed. Rounding up means
                // one tick per frame, and `nextFrame` above still catches up from the
                // wall clock if a tick is late.
                const double nextUpdateTime = oneFrameTime * (static_cast<int>(elapsedFrame) + 1);
                const double intervalTime = nextUpdateTime - elapsedTime;
                mTimer.setInterval(std::max(static_cast<int>(std::ceil(intervalTime)), 1));
            } else {
                mBeginFrame = curFrame;
                mElapsed.restart();
                mTimer.setInterval(static_cast<int>(oneFrameTime));
            }
        }

        // The clock is the one path bound by the effective range: clamp the
        // result (a timer that ran late may compute past playEnd) so the
        // playhead never visibly overshoots the window.
        const int clamped = qBound(playStart, static_cast<int>(nextFrame), playEnd);
        setFrame(core::Frame(clamped));
        mLastFrame = core::Frame(clamped);
    }
}

void TimeLineWidget::onThemeUpdated(theme::Theme& aTheme) {
    this->setStyleSheet(aTheme.loadStylesheet("timelinewidget.ssa"));
    mInner->updateTheme(aTheme);
}

void TimeLineWidget::onProjectAttributeUpdated() {
    mInner->updateProjectAttribute();
    // The frame range may have changed: the union track and page step follow
    // its extent, so re-project (updateSize is repaint-only now).
    projectHorizontalScrollBar();
}

void TimeLineWidget::triggerOnTimeFormatChanged() {
    // The setting is applied live rather than on the next launch: re-read it
    // into the editor so the ruler, playhead pill and window pills all
    // re-render with the new representation.
    if (mInner) {
        if (auto* editor = mInner->editor()) {
            QSettings settings;
            const auto format = static_cast<core::TimeFormatType>(
                settings.value("generalsettings/ui/timeformat", int(core::TimeFormatType_Frames_From0)).toInt()
            );
            editor->setTimeFormatType(format);
        }
    }
    onTimeFormatChanged();
    updateCamera();
}

void TimeLineWidget::syncAudioTracks() {
    if (!mProject)
        return;
    AudioPlaybackWidget::syncTracks(mProject.get(), currentFrame().get(), mLastFrame.get(), getFps());
}

QRect TimeLineWidget::marqueeScrollViewport() const {
    if (!mInner)
        return QRect();
    const int top = ctrl::TimeLineEditor::kHeaderHeight;
    return QRect(0, top, mInner->width(), std::max(0, mInner->height() - top));
}

//-------------------------------------------------------------------------------------------------
void TimeLineWidget::updateMarqueeAutoScroll() {
    ctrl::TimeLineEditor* ed = editor();
    if (!ed || !ed->isMarqueeActive() || !mAbstractCursor.isPressedLeft() || !mInner) {
        mMarqueeScrollTimer.stop();
        mMarqueeDwell.reset();
        return;
    }
    // Direction is part of the gate: a drag along the top edge must not scroll up.
    // `observe` arms only an axis the pointer is moving TOWARD (or is already outside
    // the viewport on), so no timer is needed for a horizontal pass at the top.
    mMarqueeDwell.observe(
        mMarqueePointer, marqueeScrollViewport(), QDateTime::currentMSecsSinceEpoch()
    );
    if (!mMarqueeDwell.inEdge()) {
        mMarqueeScrollTimer.stop();
    } else if (!mMarqueeScrollTimer.isActive()) {
        mMarqueeScrollTimer.start();
    }
}

void TimeLineWidget::autoScrollMarquee() {
    ctrl::TimeLineEditor* ed = editor();
    if (!ed || !ed->isMarqueeActive() || !mAbstractCursor.isPressedLeft() || !mInner) {
        mMarqueeScrollTimer.stop();
        mMarqueeDwell.reset();
        return;
    }

    const ctrl::autoscroll::Step step = mMarqueeDwell.update(
        mMarqueePointer, marqueeScrollViewport(), QDateTime::currentMSecsSinceEpoch()
    );
    if (step.isNull()) {
        // A dwell tick is not a reason to stop; a pointer that left the band resets
        // the dwell in update() and updateMarqueeAutoScroll stops the timer.
        if (!mMarqueeDwell.inEdge())
            mMarqueeScrollTimer.stop();
        return;
    }

    const int oldX = mHorizontalOffset;
    const int oldY = mVerticalScrollValue;
    if (step.x != 0)
        scrollViewBy(step.x); // positive reveals later frames
    if (step.y != 0)
        onVerticalScrollRequested(mVerticalScrollValue + step.y); // positive scrolls down

    // At a content bound the request changes nothing; stop until the pointer moves
    // so a held edge does not keep a 60 Hz timer alive for a no-op.
    if (oldX == mHorizontalOffset && oldY == mVerticalScrollValue) {
        mMarqueeScrollTimer.stop();
        mMarqueeDwell.reset();
        return;
    }

    // The pointer did not move; the WORLD point under it did. Extend the box to the
    // new world position through the editor's own gesture (it applies the new box).
    const QPoint world = mCameraInfo.toWorldPos(QPointF(mMarqueePointer)).toPoint();
    if (ed->extendMarquee(world))
        mInner->update();
}

//-------------------------------------------------------------------------------------------------
void TimeLineWidget::mouseMoveEvent(QMouseEvent* aEvent) {
    if (mIsPanning) {
        panTo(mPanStartTransform + (aEvent->position().toPoint() - mPanGrabPos));
        aEvent->accept();
        return;
    }
    mMarqueePointer = aEvent->position().toPoint();
    if (mAbstractCursor.setMouseMove(aEvent, mCameraInfo)) {
        updateCursor(mAbstractCursor, aEvent->modifiers());
    }
    updateMarqueeAutoScroll();
}

void TimeLineWidget::mousePressEvent(QMouseEvent* aEvent) {
    // Middle-drag always pans; holding the MoveCanvas key (space by default,
    // configurable) makes left-drag pan too — the same keybind the viewport
    // uses, so the timeline follows the user's pan preference.
    const bool panPress =
        aEvent->button() == Qt::MiddleButton || (mPanByKey && aEvent->button() == Qt::LeftButton);
    if (panPress) {
        mMarqueeScrollTimer.stop();
        mMarqueeDwell.reset();
        mPanGrabPos = aEvent->position().toPoint();
        mPanStartTransform = viewportTransform();
        mIsPanning = true;
        mPanButton = aEvent->button();
        setCursor(Qt::ClosedHandCursor);
        aEvent->accept();
        return;
    }
    mMarqueePointer = aEvent->position().toPoint();
    mMarqueeDwell.reset();
    if (mInner) {
        // Seed the direction baseline. A press on an edge is NOT a move toward it.
        mMarqueeDwell.observe(
            mMarqueePointer, marqueeScrollViewport(), QDateTime::currentMSecsSinceEpoch()
        );
    }
    if (mAbstractCursor.setMousePress(aEvent, mCameraInfo)) {
        updateCursor(mAbstractCursor, aEvent->modifiers());
    }
    // A press only arms the marquee. Autoscroll starts on the first move: a
    // press-and-hold on an edge must not scroll before a box exists.
}

void TimeLineWidget::mouseReleaseEvent(QMouseEvent* aEvent) {
    mMarqueeScrollTimer.stop();
    mMarqueeDwell.reset();
    // Only the button that STARTED the pan ends it. The old test accepted
    // either button, so a middle-drag pan in flight ate the release of a left
    // button the editor had already turned into a marquee (see mPanButton).
    if (mIsPanning && aEvent->button() == mPanButton) {
        mIsPanning = false;
        mPanButton = Qt::NoButton;
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

void TimeLineWidget::hideEvent(QHideEvent* aEvent) {
    // Switching the dock to the Graph page hides this surface mid-gesture: the
    // release may never reach it, so the marquee's autoscroll must not keep ticking.
    mMarqueeScrollTimer.stop();
    mMarqueeDwell.reset();
    QWidget::hideEvent(aEvent);
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

//-------------------------------------------------------------------------------------------------
// THE ANIMATION GRAPH PANEL'S SIDE OF THIS WIDGET.
//
// The panel is a second view of the SAME keys on the SAME frames, so the two
// views must not each keep their own idea of which frames are on screen or which
// keys are selected. The selection is shared outright - the panel is pointed at
// ctrl::TimeLineEditor::selection(), the live set this widget's gestures write
// (see MainDisplayWidget::setTimeLineWidget) - and the frame window is shared by
// VALUE: the panel shows visibleFrameRange() below, and the horizontal gestures
// made over the panel come back here as frames (a pan) or as a wheel notch about
// a frame (a zoom), so the panel scrolls and zooms THIS widget rather than
// drifting beside it.

ctrl::TimeLineEditor* TimeLineWidget::editor() const { return mInner ? mInner->editor() : nullptr; }

bool TimeLineWidget::openKeyMenu(const core::TimeLineEvent::Target& aPressedKey, const QPoint& aGlobalPos) {
    // The key menu's route for a caller that is not the timeline's own context-menu
    // handler (see TimeLineEditorWidget::openKeyMenu, which owns the menu, the
    // selection rule and the loan). Forwarded whole: the caller passes a key, not a
    // selection, so nothing is filtered out on the way.
    return mInner->openKeyMenu(aPressedKey, aGlobalPos);
}

util::Range TimeLineWidget::visibleFrameRange() const {
    const ctrl::TimeLineEditor* controller = editor();
    if (!controller || !mInner || mInner->width() <= 0)
        return util::Range();

    const ctrl::time::Scaler& scale = controller->timeScale();
    const int left = -mHorizontalOffset;
    const int right = left + mInner->width();
    const int first = scale.frame(left - ctrl::TimeLineEditor::kTimeLineMargin);
    const int last = scale.frame(right - ctrl::TimeLineEditor::kTimeLineMargin);
    return util::Range(qMin(first, last), qMax(first, last));
}

void TimeLineWidget::visibleFrameWindow(double& aLo, double& aHi) const {
    aLo = aHi = 0.0;

    const ctrl::TimeLineEditor* controller = editor();
    if (!controller || !mInner || mInner->width() <= 0)
        return; // no surface (or no project) yet: no window

    // The same edges as visibleFrameRange, in content pixels and therefore in
    // fractions of a frame: the viewport's left edge is -mHorizontalOffset, a content
    // pixel x is frame (x - kTimeLineMargin) / pixelsPerFrame. Dividing last keeps the
    // edges exact; the rounded form rounds them into whole frames.
    const double ppf = controller->pixelsPerFrame();
    const int left = -mHorizontalOffset;
    const int right = left + mInner->width();
    const double first = (left - ctrl::TimeLineEditor::kTimeLineMargin) / ppf;
    const double last = (right - ctrl::TimeLineEditor::kTimeLineMargin) / ppf;
    aLo = std::min(first, last);
    aHi = std::max(first, last);
}

void TimeLineWidget::notifyViewRange() {
    // The window is reported in FRACTIONAL frames, and only a change of it is a change: rounding to
    // whole frames here would emit only when a frame boundary crosses the viewport, which at a high
    // zoom is many pixels and quantizes every consumer of the signal - the graph panel's whole X.
    double lo = 0.0;
    double hi = 0.0;
    visibleFrameWindow(lo, hi);
    if (lo == mReportedWindowLo && hi == mReportedWindowHi)
        return;

    mReportedWindowLo = lo;
    mReportedWindowHi = hi;
    onViewRangeChanged();
}

void TimeLineWidget::scrollFramesBy(double aFrameDelta) {
    ctrl::TimeLineEditor* controller = editor();
    if (!controller)
        return;

    // Frames to the content pixels this widget's view transform is expressed in:
    // the editor's own pixels-per-frame, so a pan of N frames moves the view
    // exactly N frames whatever the zoom. scrollViewBy takes the delta of the
    // viewport's START (v = -mHorizontalOffset) and updates the camera and the
    // scrollbar, which is where the new window is reported from (notifyViewRange).
    //
    // The content axis is whole pixels, so this conversion rounds - and a drag arrives as MANY small
    // deltas, so rounding each one away would lose a fraction per event and the view would fall
    // behind the pointer. The remainder is carried between calls instead, so the total is exact.
    //
    // THE CARRY IS IN CONTENT PIXELS, NOT IN FRAMES, and that is load-bearing: a remainder measured in
    // frames is re-priced by any scale change, so a pan, a zoom (one notch is x1.1, twenty is x7.4)
    // and a pan again made the second pan absorb the first's residue - measured 2026-10-19 through
    // probe_graph_anchor.sh as 37.78px of a 40px drag at 7px/frame (a 0.42px residue became ~3.1px).
    // Pixels are the unit the view transform rounds to, so they survive a zoom unchanged.
    const double rate = controller->pixelsPerFrame();
    mPendingScrollPixels += aFrameDelta * rate;
    const int delta = static_cast<int>(std::lround(mPendingScrollPixels));
    if (delta == 0)
        return; // sub-pixel: a frame delta too small to move this view is no move
    mPendingScrollPixels -= static_cast<double>(delta);
    scrollViewBy(delta);
}

void TimeLineWidget::zoomFramesAbout(int aWheelDelta, double aAnchorFrame) {
    ctrl::TimeLineEditor* controller = editor();
    if (!controller)
        return;

    // The editor's own zoom. The invert flag is false because aWheelDelta arrives in
    // the CALLER's sense (the panel applied its own user setting to the notch, and to
    // its value half beside it) - this widget's setting belongs to its own wheel, and
    // applying it here would invert the caller's notch twice.
    const double oldSpacing = controller->pixelsPerFrame();
    if (!controller->updateWheel(aWheelDelta, false))
        return; // clamped: the scale did not move, so the view has nothing to re-anchor

    // aAnchorFrame is the frame under the caller's cursor, FRACTIONAL (the panel
    // derives it with frameAtPixel, never frameAt), so the anchored point is known
    // exactly. Go straight from it to the view translation: the content position is
    // margin + rate*frame, and the margin cancels. Do NOT re-derive the anchor through
    // frameStartPixel()/the integer content axis and round it - that (plus rounding the
    // scaled pixel) is the pair that lost a fraction per notch and walked the anchored
    // frame 0.86 of a frame over 20 notches at 7 px/frame.
    const int delta = ctrl::TimeLineEditor::zoomAnchorDelta(
        aAnchorFrame, oldSpacing, controller->pixelsPerFrame(), mPendingZoomPixels
    );

    // Applied even when delta is 0: the SCALE changed, so the visible frame window did
    // too, and setViewTransform is what reports it (projectHorizontalScrollBar ->
    // notifyViewRange). Returning early would leave the panel on the old window.
    QPoint viewTrans = viewportTransform();
    viewTrans.setX(viewTrans.x() + delta);
    setViewTransform(viewTrans);
}

void TimeLineWidget::fitFrameRange() {
    ctrl::TimeLineEditor* controller = editor();
    // A surface with no width cannot fit anything: remember the request and let
    // updateCamera apply it once the widget is laid out (a project opened while
    // the timeline is hidden). With no controller there is no axis to fit at all.
    if (!controller || mInner->width() <= 0) {
        mFitPending = (controller != nullptr);
        return;
    }
    mFitPending = false;

    // The fit's two numbers are the controller's (the ladder and the margin token
    // are its own), and it returns a whole-pixel view position so the two gutters
    // can differ by at most that pixel.
    const ctrl::TimeLineEditor::FrameFit fit = controller->fitFrameRange(mInner->width());

    // MOVE THE LADDER TO THE FIT'S RUNG. One notch is 120 raw delta (the scaler's
    // kWheelValue) and updateWheel SUBTRACTS it, so the sign is current - target.
    // The fit computed its view pixel at the target rung, so the scale is set
    // BEFORE the translation is applied.
    const int delta = controller->timeScale().index() - fit.scaleIndex;
    if (delta != 0)
        controller->updateWheel(delta * 120, false);

    // CENTRE THE RANGE. The old composition zoomed for the span ALONE and then
    // scrolled until the range's first frame sat on the viewport's left edge: that
    // scroll is the step that threw the leading margin away (the content axis' own
    // kTimeLineMargin gutter), and the span-only rung left only the ladder's
    // remainder at the other end. The fit now centres instead: the view transform's
    // horizontal component is the content origin in surface coordinates, so putting
    // the fit's left content pixel under the viewport's left edge is
    // -viewLeftPixel. setViewTransform re-projects the scrollbar and reports the new
    // window (notifyViewRange), which is what the docked panel follows; updateCamera
    // clamps the offset it just allowed.
    QPoint viewTrans = viewportTransform();
    viewTrans.setX(-fit.viewLeftPixel);
    setViewTransform(viewTrans);
}

bool TimeLineWidget::updatePlayWindow(ctrl::TimeLineEditor::PlayWindowStep aStep, core::Frame aFrame) {
    // The surface is the timeline's own home of this gesture (it emits
    // playWindowChanged and repaints both rulers, exactly as the ruler path does),
    // so the graph's band lands on the SAME notification the timeline's Shift-drag
    // produces - which is what pushes the new window back into the panel's editor.
    return mInner ? mInner->updatePlayWindow(aStep, aFrame) : false;
}

} // namespace gui
