#include "gui/TimeRuler.h"

#include <QPainter>
#include <QMouseEvent>
#include "core/TimeFormat.h"
#include "ctrl/TimeLineEditor.h"
#include "gui/TimeLineEditorWidget.h"
#include "gui/theme/Colors.h"

namespace gui {

namespace {

// Tick appearance, keyed by the Scaler's classification. The Scaler decides
// *which* frames get a tick and *how prominent* it is (TickLevel); this file
// owns how a tier is drawn — its length and its alpha. Keeping both in one
// table means a tier's length and alpha change here, and the painter never
// has to recover a tier from a pixel length.
using TickLevel = ctrl::time::Scaler::TickLevel;
constexpr int kTickLength = 5; // all tiers share one tick length

qreal tickAlpha(TickLevel aLevel) {
    switch (aLevel) {
        case TickLevel::Major:        return 0.85;
        case TickLevel::Intermediate: return 0.60;
        case TickLevel::Minor:        return 0.20;
        case TickLevel::None:         return 0.0;
    }
    return 0.0;
}

} // namespace

//-------------------------------------------------------------------------------------------------
TimeRuler::TimeRuler(TimeLineEditorWidget& aSurface, QWidget* aParent):
    QWidget(aParent),
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
}

void TimeRuler::setCamera(const core::CameraInfo* aCamera) {
    mCamera = aCamera;
    update();
}

void TimeRuler::updateTheme() {
    mTimelineTheme.reset();
    update();
}

//-------------------------------------------------------------------------------------------------
void TimeRuler::paintEvent(QPaintEvent* aEvent) {
    QPainter painter;
    painter.begin(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    // The strip is viewport-fixed: its paint translates by the view origin,
    // exactly like the track surface, so the numbers scroll with the content
    // while the strip itself never moves. camRect mirrors the renderer's
    // camera rect in this widget's painter space.
    const QPoint origin = mCamera ? mCamera->leftTopPos().toPoint() : QPoint();
    painter.translate(origin);
    const QRect cameraRect(-origin, this->size());

    const QRect cullRect(aEvent->rect().translated(-origin).marginsAdded(QMargins(2, 2, 2, 2)));
    const int margin = ctrl::TimeLineEditor::kTimeLineMargin;
    const ctrl::time::Scaler& scale = mEditor->timeScale();
    const int bgn = scale.frame(cullRect.left() - margin - 5);
    const int end = scale.frame(cullRect.right() - margin + 5);

    renderHeader(painter, cameraRect, util::Range(bgn, end), mEditor->fps());
    painter.end();
}

void TimeRuler::renderHeader(QPainter& aPainter, const QRect& aCameraRect, const util::Range& aVisibleRange, int aFps) {
    const int aHeight = ctrl::TimeLineEditor::kHeaderHeight;
    const ctrl::time::Scaler& scale = mEditor->timeScale();
    const int margin = ctrl::TimeLineEditor::kTimeLineMargin;

    aPainter.setRenderHint(QPainter::Antialiasing, false);

    // draw header background (the hairline under it is drawn by the surface:
    // it sits on the first track row, below this widget)
    {
        const QBrush kBrush(mTimelineTheme.headerBackgroundColor());
        QRect rect = aCameraRect;
        rect.setHeight(aHeight);
        aPainter.setPen(QPen(kBrush, 1));
        aPainter.setBrush(kBrush);
        aPainter.drawRect(rect);
    }

    // draw header info
    {
        const QBrush kBrush(mTimelineTheme.headerContentColor());
        const int numberWidth = 6;
        const QPoint lt(margin, aCameraRect.top());
        // the ticks end one row above the ruler hairline (drawn at
        // top + height) so they never draw over it
        const QPoint rb = lt + QPoint(scale.maxPixelWidth(), aHeight - 1);
        const core::TimeFormat timeFormat(aVisibleRange, aFps);

        // The project's last frame owns the end of the ruler: when it is
        // in view but not on a scale multiple, it reads as a major tick
        // with its number, and any numbered neighbor whose label would
        // overlap it yields instead. The label occupies its slot in view
        // regardless of grid alignment — the loop draws it when the frame
        // falls on a scale multiple, the end block below otherwise — so
        // the yield rule must not depend on which path drew it (alignment
        // changes with zoom; gating on it made neighbor labels pop in and
        // out with the view).
        const int lastFrame = scale.frameEnd();
        const auto lastAttr = scale.attribute(lastFrame);
        const bool endLabelInView = lastFrame >= aVisibleRange.min() && lastFrame <= aVisibleRange.max();
        // The boundary label owns the range edge whenever the frame isn't
        // already drawn as a full numbered major tick by the loop.
        const bool lastLabelActive = endLabelInView && lastAttr.level != TickLevel::Major;
        const int lastLabelCenter = lt.x() + lastAttr.pos;
        const QString lastNumber = timeFormat.frameToString(lastFrame, mEditor->timeFormatType());
        const int lastNumberWidth = numberWidth * lastNumber.size();

        // The project's first frame mirrors the end label: off the scale
        // grid it reads as a major tick with its number, and numbered
        // neighbors yield to it the same way.
        const int firstFrame = mEditor->frameRange().min();
        const auto firstAttr = scale.attribute(firstFrame);
        const bool firstLabelInView = firstFrame >= aVisibleRange.min() && firstFrame <= aVisibleRange.max();
        const bool firstLabelActive = firstLabelInView && firstAttr.level != TickLevel::Major;
        const int firstLabelCenter = lt.x() + firstAttr.pos;
        const QString firstNumber = timeFormat.frameToString(firstFrame, mEditor->timeFormatType());
        const int firstNumberWidth = numberWidth * firstNumber.size();

        // Number labels never come closer than this gap: without a
        // margin, adjacent numbers (a boundary label and a nearby major)
        // read as one.
        constexpr int kNumberMargin = 8;

        // A narrow range's two boundary labels can come within the margin
        // (or overlap); collapse them into one "first–last" label
        // centered between the ticks, mirroring how the playback
        // window's pills collapse. The collision test ignores grid
        // alignment: an on-major boundary is drawn by the tick loop
        // itself, but it still occupies the same slot and must collapse
        // the same way — a range [1, 5] reads "1–5", not a lone "5"
        // eating the start. The ticks still mark both edges.
        const bool boundaryLabelsCollide = firstLabelInView && endLabelInView &&
            (firstLabelCenter + (firstNumberWidth >> 1) + kNumberMargin >=
             lastLabelCenter - (lastNumberWidth >> 1));
        const QString mergedNumber = boundaryLabelsCollide
            ? firstNumber + QString::fromUtf8("\u2013") + lastNumber
            : QString();
        const int mergedNumberWidth = numberWidth * mergedNumber.size();
        const int mergedLabelCenter = (firstLabelCenter + lastLabelCenter) / 2;

        const util::Range& activeRange = mEditor->playbackRange();

        for (int i = aVisibleRange.min(); i <= aVisibleRange.max(); ++i) {
            // The boundary labels own their frames: when the range's
            // first/last frame label is active (the frame is off-major)
            // or the labels are merged, the loop's tier tick would stack
            // under the boundary's major tick — skip the frame entirely,
            // the boundary block below draws both the tick and the
            // number. On-major boundaries keep their loop tick only when
            // they are not merged.
            if ((i == firstFrame && (boundaryLabelsCollide || firstLabelActive)) ||
                (i == lastFrame && (boundaryLabelsCollide || lastLabelActive)))
                continue;

            auto attr = scale.attribute(i);
            if (attr.level == TickLevel::None) continue;

            QPoint pos(lt.x() + attr.pos, rb.y());

            // Fade the tick tiers so minor blips recede and even the
            // numbered major ticks stay a step below full brightness.
            qreal alpha = tickAlpha(attr.level);
            // Ticks outside the active playback range recede further:
            // the tier structure stays, but a fixed extra dim makes the
            // range edge read at a glance. The active range is the
            // project frame range — or the temporary playback window
            // while it overrides — and the boundary frames themselves
            // keep full tier brightness.
            const bool inRange = activeRange.contains(i);
            if (!inRange)
                alpha *= 0.4;
            QColor tickColor = kBrush.color();
            tickColor.setAlphaF(alpha);
            aPainter.setPen(QPen(tickColor, 1));
            aPainter.drawLine(pos, pos + QPoint(0, -kTickLength));

            if (attr.showNumber) {
                QString number = timeFormat.frameToString(i, mEditor->timeFormatType());
                const int width = static_cast<int>(numberWidth * number.size());
                // A neighbor label never comes within kNumberMargin of
                // the range's boundary labels: without a gap, adjacent
                // numbers read as one. The boundary label wins the space
                // it occupies — its own span when the labels are apart,
                // the collapsed "first–last" span when the range is too
                // narrow for both. The test is symmetric — a label yields
                // only when it actually enters the reserved span, on
                // either side (a one-sided test would swallow every label
                // past the end). Each boundary reserves its slot whenever
                // it is in view: the first label's test must not ride on
                // the end label's visibility (the end can be scrolled out
                // while the start is still on screen).
                const bool yieldToLast = endLabelInView && i != lastFrame;
                const bool yieldToFirst = firstLabelInView && i != firstFrame;
                if (yieldToLast || yieldToFirst) {
                    const int left = pos.x() - (width >> 1);
                    const int right = pos.x() + (width >> 1);
                    const int m = kNumberMargin;
                    if (boundaryLabelsCollide) {
                        const int center = mergedLabelCenter;
                        const int half = (mergedNumberWidth >> 1) + m;
                        if (left < center + half && right > center - half)
                            continue;
                    } else {
                        if (yieldToLast &&
                            left < lastLabelCenter + (lastNumberWidth >> 1) + m &&
                            right > lastLabelCenter - (lastNumberWidth >> 1) - m)
                            continue;
                        if (yieldToFirst &&
                            left < firstLabelCenter + (firstNumberWidth >> 1) + m &&
                            right > firstLabelCenter - (firstNumberWidth >> 1) - m)
                            continue;
                    }
                }
                QColor numberColor = kBrush.color();
                if (!inRange)
                    numberColor.setAlphaF(0.4);
                aPainter.setPen(QPen(numberColor, 1));
                const int left = pos.x() - (width >> 1);
                const int top = lt.y() + ctrl::TimeLineEditor::kNumberTop;
                const QRect rect(
                    QPoint(left, top), QPoint(left + width + 1, top + ctrl::TimeLineEditor::kNumberHeight)
                );
                aPainter.drawText(rect, number);
            }
        }

        // The boundary ticks mark both edges at major prominence; each
        // recedes with the out-of-range content while a playback window
        // overrides the range. A merged label owns both edges, so both
        // ticks draw even when one boundary frame is on-major (its loop
        // tick was skipped).
        const bool drawFirstTick = boundaryLabelsCollide || firstLabelActive;
        const bool drawLastTick = boundaryLabelsCollide || lastLabelActive;
        if (drawFirstTick) {
            const qreal firstAlpha = activeRange.contains(firstFrame) ? 1.0 : 0.4;
            const QPoint pos(lt.x() + firstAttr.pos, rb.y());

            QColor tickColor = kBrush.color();
            tickColor.setAlphaF(tickAlpha(TickLevel::Major) * firstAlpha);
            aPainter.setPen(QPen(tickColor, 1));
            aPainter.drawLine(pos, pos + QPoint(0, -kTickLength));
        }
        if (drawLastTick) {
            const qreal lastAlpha = activeRange.contains(lastFrame) ? 1.0 : 0.4;
            const QPoint pos(lt.x() + lastAttr.pos, rb.y());

            QColor tickColor = kBrush.color();
            tickColor.setAlphaF(tickAlpha(TickLevel::Major) * lastAlpha);
            aPainter.setPen(QPen(tickColor, 1));
            aPainter.drawLine(pos, pos + QPoint(0, -kTickLength));
        }

        // The loop skips the boundary frames while their labels are
        // active, so the numbers are always ours here (the yield rule
        // keeps numbered neighbors away from the reserved slots). A
        // range too narrow for both labels collapses into one
        // "first–last" label, like the playback window's pills.
        if (boundaryLabelsCollide) {
            const bool inRange = activeRange.contains(firstFrame) && activeRange.contains(lastFrame);
            const qreal mergedAlpha = inRange ? 1.0 : 0.4;

            QColor numberColor = kBrush.color();
            numberColor.setAlphaF(mergedAlpha);
            aPainter.setPen(QPen(numberColor, 1));
            const int left = mergedLabelCenter - (mergedNumberWidth >> 1);
            const int top = lt.y() + ctrl::TimeLineEditor::kNumberTop;
            const QRect rect(
                QPoint(left, top), QPoint(left + mergedNumberWidth + 1, top + ctrl::TimeLineEditor::kNumberHeight)
            );
            aPainter.drawText(rect, mergedNumber);
        } else {
            if (firstLabelActive) {
                const qreal firstAlpha = activeRange.contains(firstFrame) ? 1.0 : 0.4;

                QColor numberColor = kBrush.color();
                numberColor.setAlphaF(firstAlpha);
                aPainter.setPen(QPen(numberColor, 1));
                const int left = firstLabelCenter - (firstNumberWidth >> 1);
                const int top = lt.y() + ctrl::TimeLineEditor::kNumberTop;
                const QRect rect(
                    QPoint(left, top), QPoint(left + firstNumberWidth + 1, top + ctrl::TimeLineEditor::kNumberHeight)
                );
                aPainter.drawText(rect, firstNumber);
            }
            if (lastLabelActive) {
                const qreal lastAlpha = activeRange.contains(lastFrame) ? 1.0 : 0.4;

                QColor numberColor = kBrush.color();
                numberColor.setAlphaF(lastAlpha);
                aPainter.setPen(QPen(numberColor, 1));
                const int left = lastLabelCenter - (lastNumberWidth >> 1);
                const int top = lt.y() + ctrl::TimeLineEditor::kNumberTop;
                const QRect rect(
                    QPoint(left, top), QPoint(left + lastNumberWidth + 1, top + ctrl::TimeLineEditor::kNumberHeight)
                );
                aPainter.drawText(rect, lastNumber);
            }
        }

        // The playback window overlays the ruler content: the translucent
        // band washes ticks and numbers, the bracket stems and label
        // pills sit on top (the playhead widget paints above all of it).
        renderPlayWindow(aPainter, aCameraRect, aVisibleRange, aFps);
    }
}

void TimeRuler::renderPlayWindow(QPainter& aPainter, const QRect& aCameraRect, const util::Range& aVisibleRange, int aFps) {
    const ctrl::TimeLineEditor::PlayWindow& window = mEditor->playWindow();
    if (!window.active())
        return;

    // Window edges on the content axis: the start frame's tick and the end
    // frame's tick. A frame is a point at its tick (its key and number sit
    // at `margin + spacing*frame`), so the inclusive window [start, end]
    // spans the tick-points start..end: the band is [tick(start), tick(end)]
    // and the end bracket lands on frame `end`'s tick — the same convention
    // as the range marker at the last frame's tick (pixelWidth(max)).
    const ctrl::time::Scaler& scale = mEditor->timeScale();
    const int margin = ctrl::TimeLineEditor::kTimeLineMargin;
    const int x0 = margin + scale.attribute(window.start).pos;
    const int x1 = margin + scale.attribute(window.end).pos;
    if (x1 <= x0)
        return;

    const int top = aCameraRect.top();
    const theme::Colors& c = theme::Colors::current();
    // The bracket stems and label pills wrap the number-text row, so their top
    // edge rides one pixel below the number text's top (kNumberTop + 1) and
    // moves with it. The translucent band stays at the header top.
    const int pillTop = top + ctrl::TimeLineEditor::kNumberTop + 1;

    // The translucent band washes the ruler content inside the window,
    // clipped to the visible ruler (off-view pills are clipped by the
    // widget itself).
    const int c0 = qMax(x0, aCameraRect.left());
    const int c1 = qMin(x1 + 1, aCameraRect.right() + 1);
    if (c1 > c0) {
        aPainter.setPen(Qt::NoPen);
        aPainter.setBrush(c.windowBand);
        aPainter.drawRect(QRect(c0, top, c1 - c0, ctrl::TimeLineEditor::kHeaderHeight));
    }

    // Bracket stems: 2px, running from the label pill's top to the ruler
    // bottom, drawn UNDER the pills: a merged pill wider than the window
    // would otherwise cover the stems' upper portion — the stems remain
    // visible below it, and the label glyphs are never cut.
    aPainter.setRenderHint(QPainter::Antialiasing, false);
    aPainter.setPen(Qt::NoPen);
    aPainter.setBrush(c.windowEdge);
    const int stemBottom = top + ctrl::TimeLineEditor::kHeaderHeight;
    aPainter.drawRect(QRect(x0, pillTop, 2, stemBottom - pillTop));
    aPainter.drawRect(QRect(x1 - 1, pillTop, 2, stemBottom - pillTop));

    // Label pills: the frame number is the bracket's horizontal arm,
    // attached to the stem's inner face. Rounded on the outer corners,
    // square on the face against the stem, so the joint stays
    // continuous. Two pills that would collide collapse into one
    // centered "a–b" label.
    const core::TimeFormat timeFormat(aVisibleRange, aFps);
    const QString sL = timeFormat.frameToString(window.start, mEditor->timeFormatType());
    const QString sR = timeFormat.frameToString(window.end, mEditor->timeFormatType());
    const QFontMetrics fm(aPainter.font());
    const int wL = fm.horizontalAdvance(sL) + ctrl::TimeLineEditor::kPillPadding * 2;
    const int wR = fm.horizontalAdvance(sR) + ctrl::TimeLineEditor::kPillPadding * 2;
    const QRect pillL(x0 + 2, pillTop, wL, ctrl::TimeLineEditor::kNumberHeight);
    const QRect pillR(x1 - 1 - wR, pillTop, wR, ctrl::TimeLineEditor::kNumberHeight);

    aPainter.setRenderHint(QPainter::Antialiasing, true);
    aPainter.setPen(Qt::NoPen);
    aPainter.setBrush(c.windowEdge);
    if (pillL.right() >= pillR.left()) {
        const QString merged = sL + QString::fromUtf8("\u2013") + sR;
        const int w = fm.horizontalAdvance(merged) + ctrl::TimeLineEditor::kPillPadding * 2;
        const QRect pill((x0 + x1) / 2 - w / 2, pillTop, w, ctrl::TimeLineEditor::kNumberHeight);
        aPainter.drawRoundedRect(
            QRectF(pill), ctrl::TimeLineEditor::kPillRadius, ctrl::TimeLineEditor::kPillRadius, Qt::AbsoluteSize
        );
        drawPillText(aPainter, pill, merged, c.windowText, top);
    } else {
        aPainter.drawRoundedRect(
            QRectF(pillL), ctrl::TimeLineEditor::kPillRadius, ctrl::TimeLineEditor::kPillRadius, Qt::AbsoluteSize
        );
        aPainter.drawRoundedRect(
            QRectF(pillR), ctrl::TimeLineEditor::kPillRadius, ctrl::TimeLineEditor::kPillRadius, Qt::AbsoluteSize
        );
        // Square the inner corners (the faces against the stems) by
        // overdrawing the rounded slivers with the pill color, so the
        // bracket joint reads continuous. The cut pixels run to
        // right()/bottom() inclusive, so the right/bottom overdraws
        // start at -r+1 (right()-r / bottom()-r would miss the last
        // pixel row/column and leave the corner visibly rounded).
        const int r = ctrl::TimeLineEditor::kPillRadius;
        aPainter.fillRect(QRect(pillL.x(), pillL.y(), r, r), c.windowEdge);
        aPainter.fillRect(QRect(pillL.x(), pillL.bottom() - r + 1, r, r), c.windowEdge);
        aPainter.fillRect(QRect(pillR.right() - r + 1, pillR.y(), r, r), c.windowEdge);
        aPainter.fillRect(QRect(pillR.right() - r + 1, pillR.bottom() - r + 1, r, r), c.windowEdge);
        drawPillText(aPainter, pillL, sL, c.windowText, top);
        drawPillText(aPainter, pillR, sR, c.windowText, top);
    }

    aPainter.setRenderHint(QPainter::Antialiasing, false);
}

void TimeRuler::drawPillText(QPainter& aPainter, const QRect& aPill, const QString& aText, const QColor& aColor, int aTop) {
    const QFontMetrics fm(aPainter.font());
    const int textWidth = fm.horizontalAdvance(aText);
    // The exact same rect convention as the ruler numbers (top-aligned
    // at kNumberTop), so the pill text lines up with them precisely.
    const QRect numberRect(
        aPill.center().x() - textWidth / 2, aTop + ctrl::TimeLineEditor::kNumberTop, textWidth,
        ctrl::TimeLineEditor::kNumberHeight
    );
    aPainter.setPen(QPen(aColor, 1));
    aPainter.drawText(numberRect, aText);
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
