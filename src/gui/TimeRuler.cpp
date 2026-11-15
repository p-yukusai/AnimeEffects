#include "gui/TimeRuler.h"

#include <QPainter>
#include <QMouseEvent>
#include <QCanvasPainter>
#include <QFontMetrics>
#include "core/TimeFormat.h"
#include "ctrl/TimeLineEditor.h"
#include "gui/TimeLineEditorWidget.h"
#include "gui/FrameRuler.h"
#include "gui/FrameRulerCanvas.h"
#include "gui/theme/Colors.h"

namespace gui {

namespace {

} // namespace

//-------------------------------------------------------------------------------------------------
TimeRuler::TimeRuler(TimeLineEditorWidget& aSurface, QWidget* aParent):
    QCanvasPainterWidget(aParent),
    mSurface(aSurface),
    mEditor(aSurface.editor()),
    mCamera(),
    mAbstractCursor(),
    mTimelineTheme(),
    mGesturing(false),
    mPanKeyHeld(false) {
    this->setMouseTracking(true);
    // The ruler owns its strip's paint and left-button gestures, but the
    // context menus live on the surface (the playhead overlays this widget,
    // so right-clicks over the pill would otherwise split the menu paths).
    this->setContextMenuPolicy(Qt::NoContextMenu);
    this->setAutoFillBackground(false);
    // The Canvas host clears its texture every frame; the strip's floor is the ruler band's
    // own colour, and updateTheme() re-reads it when the palette changes.
    setFillColor(mTimelineTheme.headerBackgroundColor());
}

void TimeRuler::setCamera(const core::CameraInfo* aCamera) {
    mCamera = aCamera;
    update();
}

void TimeRuler::updateTheme() {
    mTimelineTheme.reset();
    setFillColor(mTimelineTheme.headerBackgroundColor());
    update();
}

//-------------------------------------------------------------------------------------------------
// THE STRIP'S CONTENT, BUILT ONCE. Both rasterisers below call this, so the ruler's
// decisions (which frame is ticked, which is numbered, where the playhead's pill sits)
// are made in one place and only the ink is the host's.
frameRuler::Model TimeRuler::buildModel(const QRect& aCameraRect, const util::Range& aVisibleRange, int aFps) const {
    const int aHeight = ctrl::TimeLineEditor::kHeaderHeight;
    const ctrl::time::Scaler& scale = mEditor->timeScale();
    const int margin = ctrl::TimeLineEditor::kTimeLineMargin;

    const QRect band(aCameraRect.left(), aCameraRect.top(), aCameraRect.width(), aHeight);
    const QFontMetrics metrics(this->font());
    const core::TimeFormat format(mEditor->frameRange(), aFps);

    frameRuler::Inputs in;
    in.band = band;
    in.visible = band;
    in.labelTop = band.top() + frameRuler::kNumberTop;
    in.frameRange = mEditor->frameRange();
    in.hasFrameRange = true;
    in.activeRange = mEditor->playbackRange();
    in.visibleFrames = aVisibleRange;
    in.fps = aFps;
    in.formatType = mEditor->timeFormatType();

    // The playhead's own column: its frame-number pill carries the only number in that
    // column, so the model suppresses any number whose centre falls inside the pill's
    // half-width. The span is the PILL's own geometry, measured in the font both are
    // painted in.
    const int playFrame = mEditor->currentFrame().get();
    in.hasPlayhead = true;
    in.playheadFrame = playFrame;
    in.playheadText = format.frameToString(playFrame, in.formatType);
    in.playheadTextWidth = metrics.horizontalAdvance(in.playheadText);

    const ctrl::TimeLineEditor::PlayWindow& window = mEditor->playWindow();
    if (window.active()) {
        in.windowStart = window.start;
        in.windowEnd = window.end;
        in.windowStartText = format.frameToString(window.start, in.formatType);
        in.windowEndText = format.frameToString(window.end, in.formatType);
        in.windowStartTextWidth = metrics.horizontalAdvance(in.windowStartText);
        in.windowEndTextWidth = metrics.horizontalAdvance(in.windowEndText);
        in.windowMergedTextWidth = metrics.horizontalAdvance(
            in.windowStartText + QString::fromUtf8("\u2013") + in.windowEndText
        );
    }

    // The timeline's own axis: the ladder's integer positions, offset by the margin that
    // the keys, the playhead and the range markers share.
    frameRuler::Axis axis;
    axis.pixelOfFrame = [&scale](int aFrame) { return double(margin + scale.pixelWidth(aFrame)); };
    axis.frameOfPixel = [&scale](int aPixel) { return scale.frame(aPixel - margin); };
    axis.attributeAt = [&scale](int aFrame, double) { return scale.attribute(aFrame); };
    axis.pixelsPerFrame = scale.pixelsPerFrame();
    axis.tickStride = scale.tickStride(scale.pixelsPerFrame());

    return frameRuler::build(in, axis);
}

frameRuler::PaintInk TimeRuler::paintInk() const {
    const theme::Colors& colors = theme::Colors::current();
    frameRuler::PaintInk ink;
    ink.content = mTimelineTheme.headerContentColor();
    ink.windowBand = colors.windowBand;
    ink.windowEdge = colors.windowEdge;
    ink.windowText = colors.windowText;
    ink.playheadFill = colors.accentBright;
    ink.playheadText = colors.accentBrightText;
    return ink;
}

// CANVAS PAINTER: the same model, the same geometry, the GPU's triangles instead of Qt's
// raster engine. The strip is viewport-fixed, so the paint translates by the view origin
// exactly as the raster host does - the numbers scroll with the content, the strip does not.
void TimeRuler::paint(QCanvasPainter* aPainter) {
    if (!mEditor)
        return;
    const QPoint origin = mCamera ? mCamera->leftTopPos().toPoint() : QPoint();
    const QRect cameraRect(-origin, this->size());
    const ctrl::time::Scaler& scale = mEditor->timeScale();
    const int margin = ctrl::TimeLineEditor::kTimeLineMargin;
    const int bgn = scale.frame(cameraRect.left() - margin - 5);
    const int end = scale.frame(cameraRect.right() - margin + 5);

    const frameRuler::Model model = buildModel(cameraRect, util::Range(bgn, end), mEditor->fps());

    aPainter->save();
    aPainter->translate(float(origin.x()), float(origin.y()));
    // THE HOST'S FONT. The raster ruler draws with its own widget font, so the Canvas one
    // must too - the numbers' size is a widget property, and Qt's default QFont() is not
    // necessarily what a themed widget was given.
    aPainter->setFont(this->font());
    // The band's own floor. The strip covers its rows completely, so this is opaque; the
    // host's clear colour carries the same value for the frame's first row.
    aPainter->setFillStyle(mTimelineTheme.headerBackgroundColor());
    aPainter->fillRect(QRectF(model.band));
    frameRuler::paintRulerCanvas(*aPainter, model, paintInk());
    aPainter->restore();
}

//-------------------------------------------------------------------------------------------------
// Input: the strip's gestures (header seek, shift-drag window) run through
// the editor's cursor machinery, driven by this widget's own AbstractCursor
// — the same updateCursor call the track surface uses, so the controller
// remains the single gesture state machine. The mouse grab keeps the drag
// alive while the cursor leaves the 22px strip. Everything else (MMB pan,
// wheel zoom, right-click menus) propagates to the surface / timeline.
void TimeRuler::mousePressEvent(QMouseEvent* aEvent) {
    if (aEvent->button() != Qt::LeftButton || !mCamera) {
        aEvent->ignore();
        return;
    }
    // With the MoveCanvas key held, left-drag pans the timeline (like the
    // viewport); the strip yields the press so the pan starts from anywhere.
    if (mPanKeyHeld) {
        aEvent->ignore();
        return;
    }
    bool frameMoved = false;
    if (mAbstractCursor.setMousePress(aEvent, *mCamera)) {
        frameMoved = mSurface.updateCursor(mAbstractCursor, aEvent->modifiers());
    }
    aEvent->accept();
    mGesturing = true;
    grabMouse();
    if (frameMoved)
        emit frameChanged();
}

void TimeRuler::mouseMoveEvent(QMouseEvent* aEvent) {
    if (!mGesturing || !mCamera) {
        aEvent->ignore();
        return;
    }
    bool frameMoved = false;
    if (mAbstractCursor.setMouseMove(aEvent, *mCamera)) {
        frameMoved = mSurface.updateCursor(mAbstractCursor, aEvent->modifiers());
    }
    aEvent->accept();
    if (frameMoved)
        emit frameChanged();
}

void TimeRuler::mouseReleaseEvent(QMouseEvent* aEvent) {
    if (!mGesturing || !mCamera || aEvent->button() != Qt::LeftButton) {
        aEvent->ignore();
        return;
    }
    bool frameMoved = false;
    if (mAbstractCursor.setMouseRelease(aEvent, *mCamera)) {
        frameMoved = mSurface.updateCursor(mAbstractCursor, aEvent->modifiers());
    }
    mGesturing = false;
    releaseMouse();
    aEvent->accept();
    if (frameMoved)
        emit frameChanged();
}

} // namespace gui
