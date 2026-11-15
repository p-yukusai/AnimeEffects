// COMPILED ONLY WHERE THE MODULE EXISTS (see the header's guard): the default tree is a Qt
// without Canvas Painter.
#if defined(AE_CANVAS_PAINTER) && AE_CANVAS_PAINTER

#include "gui/InkCanvas.h"

#include "gui/GraphPanelFrame.h"

#include <QCanvasPainter>
#include <QFontMetricsF>

#include <cmath>

namespace gui {
namespace ink {

namespace {

// A QRectF's far edge is x + w (see gui/Ink.h's rect convention): the pixel box a fill must
// cover is that span, so the width W is the rect's own width, not one more.
QRectF boxOf(const QRectF& aRect) { return aRect; }

constexpr double kHalf = 0.5;

} // namespace

InkCanvas::InkCanvas(QCanvasPainter& aPainter, const QFont& aFont) : mPainter(aPainter), mFont(aFont) {
    aPainter.setFont(aFont);
    aPainter.setLineCap(QCanvasPainter::LineCap::Round);
    aPainter.setLineJoin(QCanvasPainter::LineJoin::Round);
    aPainter.setAntialias(1.0f);
}

void InkCanvas::setLineAntialias(bool aEnabled) {
    mAntialias = aEnabled;
    // Canvas2D's antialias is a scalar; 0 turns the stroke fringe off, which is what the
    // panel's integer chrome asks for.
    mPainter.setAntialias(aEnabled ? 1.0f : 0.0f);
}

void InkCanvas::pushClip(const QRect& aRect) {
    mClips.append(aRect);
    mPainter.setClipRect(QRectF(aRect));
}

void InkCanvas::popClip() {
    if (mClips.isEmpty())
        return;
    mClips.removeLast();
    if (mClips.isEmpty())
        mPainter.setClipRect(QRectF(0, 0, 1e6, 1e6)); // Canvas2D has no "no clip": the device box
    else
        mPainter.setClipRect(QRectF(mClips.last()));
}

void InkCanvas::fillRect(const QRectF& aRect, const QColor& aColor, double aRadius) {
    mPainter.setFillStyle(aColor);
    if (aRadius > 0.0) {
        mPainter.beginPath();
        mPainter.roundRect(boxOf(aRect), aRadius);
        mPainter.fill();
    } else {
        mPainter.fillRect(boxOf(aRect));
    }
}

void InkCanvas::strokeRect(const QRectF& aRect, const QColor& aColor, double aRadius, double aWidth) {
    mPainter.setStrokeStyle(aColor);
    mPainter.setLineWidth(aWidth);
    mPainter.beginPath();
    // Canvas2D centres its stroke on the path, so inset by half the width for the same
    // reason InkQPainter does: the stroke then covers exactly the rows/columns the box
    // names (see gui::ink::paintMarquee).
    const double half = aWidth * 0.5;
    const QRectF box = aRect.adjusted(half, half, -half, -half);
    if (aRadius > 0.0)
        mPainter.roundRect(boxOf(box), aRadius);
    else {
        const QRectF b = boxOf(box);
        mPainter.moveTo(b.topLeft());
        mPainter.lineTo(b.topRight());
        mPainter.lineTo(b.bottomRight());
        mPainter.lineTo(b.bottomLeft());
        mPainter.closePath();
    }
    mPainter.stroke();
}

void InkCanvas::line(const QPointF& aFrom, const QPointF& aTo, const QColor& aColor, double aWidth) {
    const bool axisAligned = qFuzzyCompare(aFrom.y(), aTo.y()) || qFuzzyCompare(aFrom.x(), aTo.x());
    if (!mAntialias && axisAligned) {
        // A 1px ROW OR COLUMN IS A FILL. Measured on the timeline: Canvas2D inks three columns
        // for a span of [x, x+1) and two for [x+0.5, x+1.5), and setAntialias(0) governs the
        // STROKE fringe, not the rect edge - so the only exact spelling is a filled band, which
        // is also the rule the ImGui backend's half-open non-AA path arrives at.
        const double w = std::max(1.0, std::floor(aWidth + 0.5));
        mPainter.setFillStyle(aColor);
        if (qFuzzyCompare(aFrom.y(), aTo.y())) {
            const double x0 = std::min(aFrom.x(), aTo.x());
            mPainter.fillRect(QRectF(x0, std::floor(aFrom.y()), std::abs(aTo.x() - aFrom.x()), w));
        } else {
            const double y0 = std::min(aFrom.y(), aTo.y());
            mPainter.fillRect(QRectF(std::floor(aFrom.x()), y0, w, std::abs(aTo.y() - aFrom.y())));
        }
        return;
    }
    mPainter.setStrokeStyle(aColor);
    mPainter.setLineWidth(aWidth);
    mPainter.beginPath();
    mPainter.moveTo(aFrom);
    mPainter.lineTo(aTo);
    mPainter.stroke();
}

void InkCanvas::stroke(const QVector<QPointF>& aPoints, const QColor& aColor, double aHalfWidth) {
    if (aPoints.size() < 2)
        return;
    mPainter.setStrokeStyle(aColor);
    mPainter.setLineWidth(aHalfWidth * 2.0);
    mPainter.beginPath();
    mPainter.moveTo(aPoints.first());
    for (int i = 1; i < aPoints.size(); ++i)
        mPainter.lineTo(aPoints.at(i));
    // A path is NOT reset by fill()/stroke(): without closePath/beginPath discipline every
    // marker would be appended to the previous one's outline.
    mPainter.stroke();
}

void InkCanvas::circleFill(const QPointF& aCentre, double aRadius, const QColor& aColor) {
    mPainter.setFillStyle(aColor);
    mPainter.beginPath();
    mPainter.circle(aCentre, aRadius);
    mPainter.fill();
}

void InkCanvas::circleStroke(const QPointF& aCentre, double aRadius, const QColor& aColor, double aWidth) {
    mPainter.setStrokeStyle(aColor);
    mPainter.setLineWidth(aWidth);
    mPainter.beginPath();
    // THE BAND IS INSIDE aRadius: Canvas2D centres a stroke on its path, so the path radius is
    // pulled in by half the width to cover [aRadius - aWidth, aRadius].
    mPainter.circle(aCentre, aRadius - aWidth * kHalf);
    mPainter.stroke();
}

void InkCanvas::text(
    const QRect& aBox, const QString& aText, HAlign aAlign, VAlign aVAlign, const QColor& aColor,
    const QColor& aHalo, int aRightGap
) {
    if (aText.isEmpty() || aBox.isEmpty())
        return;
    mPainter.setFont(mFont);
    const QFontMetricsF metrics(mFont);
    const double advance = metrics.horizontalAdvance(aText);

    double x = aBox.left();
    if (aAlign == HAlign::Centre)
        x += (double(aBox.width()) - advance) * 0.5;
    else if (aAlign == HAlign::Right) {
        const double spare = std::max(0.0, double(aBox.width()) - advance);
        x = double(aBox.left() + aBox.width()) - advance - std::min(double(aRightGap), spare);
    }
    const double snappedX = std::floor(x);
    // TOP MEANS THE BOX'S TOP IS THE LINE BOX'S TOP - the rule the TIMELINE's own ruler draws
    // with (gui::frameRuler::paintRulerCanvas: TextBaseline::Top at the box's top row), so the
    // panel's frame numbers and the timeline's land on the same rows instead of one apart.
    // MEASURED 2026-10-23 (probe_graph_parity): the previous spelling placed the run by its INK
    // box (`inkTop + tightBoundingRect().top()`, i.e. box.top() + kRulerNumberInkTop) and read
    // rows 242..250 against the timeline's 243..252 for the same string and the same box top;
    // the engine's own top baseline reads 243..252. The ink-row arithmetic is GONE from this
    // backend (kRulerNumberInkTop stays for gui::InkImGui, whose line box is its own).
    const bool topAligned = aVAlign == VAlign::Top;
    const double anchorY = topAligned ? double(aBox.top())
        : std::floor(double(aBox.top()) + std::max((double(aBox.height()) - metrics.height()) * 0.5, 0.0)
                     + metrics.ascent());
    mPainter.setTextAlign(QCanvasPainter::TextAlign::Left);
    mPainter.setTextBaseline(topAligned ? QCanvasPainter::TextBaseline::Top
                                        : QCanvasPainter::TextBaseline::Alphabetic);
    if (kDrawTextHalo && aHalo.isValid()) {
        static const int kOffsets[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
        QColor haloInk = aHalo;
        haloInk.setAlphaF(qBound(0.0f, haloInk.alphaF() * kHaloAlpha, 1.0f));
        mPainter.setFillStyle(haloInk);
        for (int radius = kHaloRadius; radius >= 1; --radius)
            for (const int* offset : kOffsets)
                mPainter.fillText(aText, QPointF(snappedX + offset[0] * radius, anchorY + offset[1] * radius));
    }
    mPainter.setFillStyle(aColor);
    mPainter.fillText(aText, QPointF(snappedX, anchorY));
}

} // namespace ink
} // namespace gui

#endif // AE_CANVAS_PAINTER
