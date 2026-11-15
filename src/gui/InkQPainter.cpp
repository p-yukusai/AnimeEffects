#include "gui/InkQPainter.h"

#include "gui/GraphPanelFrame.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace gui {
namespace ink {

namespace {

// A QRectF's far edge IS x + w (unlike a QRect's x + w - 1), and gui/Ink.h's contract says a
// QRectF(x,y,w,h) names the same pixels as QRect(x,y,w,h) - so the far edge here is the rect's
// own right()/bottom(), never one more. MEASURED on the ImGui backend: adding 1 widened every
// converted rect by a column.
QRectF boxOf(const QRectF& aRect) { return aRect; }

// PENS AND BRUSHES carry their colour's alpha unchanged: the panel's strengths are expressed as
// colours with alpha, and this rasteriser honours that by construction.
QPen penOf(const QColor& aColor, double aWidth) {
    QPen pen(aColor);
    pen.setWidthF(aWidth);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    return pen;
}

} // namespace

InkQPainter::InkQPainter(QPainter& aPainter, const QFont& aFont) : mPainter(aPainter), mFont(aFont) {
    aPainter.setRenderHint(QPainter::Antialiasing, true);
    aPainter.setRenderHint(QPainter::TextAntialiasing, true);
    aPainter.setFont(aFont);
}

void InkQPainter::setLineAntialias(bool aEnabled) {
    mPainter.setRenderHint(QPainter::Antialiasing, aEnabled);
}

void InkQPainter::pushClip(const QRect& aRect) { mPainter.setClipRect(aRect, Qt::IntersectClip); }

void InkQPainter::popClip() { mPainter.setClipping(false); }

void InkQPainter::fillRect(const QRectF& aRect, const QColor& aColor, double aRadius) {
    mPainter.save();
    mPainter.setPen(Qt::NoPen);
    mPainter.setBrush(aColor);
    if (aRadius > 0.0) {
        // ImGui's rounded fill covers the rect's own pixels with the corners cut; Qt's
        // drawRoundedRect insets by the pen only when a pen is set, so with NoPen the shape is
        // the rect's own box.
        mPainter.drawRoundedRect(boxOf(aRect), aRadius, aRadius);
    } else {
        mPainter.fillRect(boxOf(aRect), aColor);
    }
    mPainter.restore();
}

void InkQPainter::strokeRect(const QRectF& aRect, const QColor& aColor, double aRadius, double aWidth) {
    mPainter.save();
    mPainter.setPen(penOf(aColor, aWidth));
    mPainter.setBrush(Qt::NoBrush);
    // THE STROKE IS CENTRED ON THE PATH, so a 1px stroke on an integer edge would split
    // itself over two device rows. Inset by half the width: the stroke then covers exactly
    // the rows the given box names, which is what ImGui's antialiased 1px line already
    // does. The caller hands over the geometry it means (gui::ink::paintMarquee's raw
    // rect) and this conversion stays where the rasteriser convention lives.
    const double half = aWidth * 0.5;
    const QRectF box = aRect.adjusted(half, half, -half, -half);
    if (aRadius > 0.0)
        mPainter.drawRoundedRect(boxOf(box), aRadius, aRadius);
    else
        mPainter.drawRect(boxOf(box));
    mPainter.restore();
}

void InkQPainter::line(const QPointF& aFrom, const QPointF& aTo, const QColor& aColor, double aWidth) {
    mPainter.save();
    const bool axisAligned = qFuzzyCompare(aFrom.y(), aTo.y()) || qFuzzyCompare(aFrom.x(), aTo.x());
    if (!mPainter.renderHints().testFlag(QPainter::Antialiasing) && axisAligned) {
        // THE PANEL'S CHROME CONVENTION, stated as a FILL. A 1px non-antialiased line at an
        // integer coordinate inks ONE whole row or column (ImGui's non-AA path offsets by half a
        // pixel to achieve exactly that). Qt's own non-AA line decides the row by rounding the
        // endpoints, which lands on the neighbouring row for a half-pixel phase the caller never
        // meant to state. Filling the band makes the rule explicit and independent of rounding.
        const double w = std::max(1.0, std::floor(aWidth + 0.5));
        if (qFuzzyCompare(aFrom.y(), aTo.y())) {
            const double x0 = std::min(aFrom.x(), aTo.x());
            mPainter.setPen(Qt::NoPen);
            mPainter.setBrush(aColor);
            mPainter.drawRect(QRectF(x0, std::floor(aFrom.y()), std::abs(aTo.x() - aFrom.x()), w));
        } else {
            const double y0 = std::min(aFrom.y(), aTo.y());
            mPainter.setPen(Qt::NoPen);
            mPainter.setBrush(aColor);
            mPainter.drawRect(QRectF(std::floor(aFrom.x()), y0, w, std::abs(aTo.y() - aFrom.y())));
        }
        mPainter.restore();
        return;
    }
    mPainter.setPen(penOf(aColor, aWidth));
    mPainter.setBrush(Qt::NoBrush);
    mPainter.drawLine(aFrom, aTo);
    mPainter.restore();
}

void InkQPainter::stroke(const QVector<QPointF>& aPoints, const QColor& aColor, double aHalfWidth) {
    if (aPoints.size() < 2)
        return;
    mPainter.save();
    mPainter.setRenderHint(QPainter::Antialiasing, true);
    mPainter.setPen(penOf(aColor, aHalfWidth * 2.0));
    mPainter.setBrush(Qt::NoBrush);
    mPainter.drawPolyline(aPoints.constData(), int(aPoints.size()));
    mPainter.restore();
}

void InkQPainter::circleFill(const QPointF& aCentre, double aRadius, const QColor& aColor) {
    mPainter.save();
    mPainter.setRenderHint(QPainter::Antialiasing, true);
    mPainter.setPen(Qt::NoPen);
    mPainter.setBrush(aColor);
    mPainter.drawEllipse(aCentre, aRadius, aRadius);
    mPainter.restore();
}

void InkQPainter::circleStroke(const QPointF& aCentre, double aRadius, const QColor& aColor, double aWidth) {
    mPainter.save();
    mPainter.setRenderHint(QPainter::Antialiasing, true);
    mPainter.setPen(penOf(aColor, aWidth));
    mPainter.setBrush(Qt::NoBrush);
    // THE BAND IS INSIDE aRadius, which is the convention the panel's geometry was measured
    // against (ImGui's AddCircle strokes inward from the radius it is given). A centred pen at
    // aRadius - aWidth/2 covers exactly [aRadius - aWidth, aRadius].
    mPainter.drawEllipse(aCentre, aRadius - aWidth * 0.5, aRadius - aWidth * 0.5);
    mPainter.restore();
}

void InkQPainter::text(
    const QRect& aBox, const QString& aText, HAlign aAlign, VAlign aVAlign, const QColor& aColor,
    const QColor& aHalo, int aRightGap
) {
    if (aText.isEmpty() || aBox.isEmpty())
        return;
    mPainter.save();
    mPainter.setFont(mFont);
    const QFontMetricsF metrics(mFont);
    const double advance = metrics.horizontalAdvance(aText);

    double x = aBox.left();
    if (aAlign == HAlign::Centre) {
        x += (double(aBox.width()) - advance) * 0.5;
    } else if (aAlign == HAlign::Right) {
        const double spare = std::max(0.0, double(aBox.width()) - advance);
        x = double(aBox.left() + aBox.width()) - advance - std::min(double(aRightGap), spare);
    }
    // THE ORIGIN IS A WHOLE PIXEL, the same rule the ImGui backend states: alignment is float
    // arithmetic on a measured advance, and a fractional origin is what makes a glyph's cell and
    // its ink disagree by a column.
    const double snappedX = std::floor(x);
    // THE BASELINE, from the vertical alignment: VAlign::Centre centres the line box; VAlign::Top
    // puts the LINE BOX'S TOP on the box's top row, which is Qt's own `AlignTop` and the rule the
    // timeline's raster ruler uses (gui::frameRuler::paintRuler: drawText(rect, label.text) at a
    // rect whose top is the number row). Qt draws from the baseline, so both are `y + ascent`.
    // The ink-row arithmetic (`kRulerNumberInkTop` + `tightBoundingRect`) is NOT used here any
    // more: it placed the run one row above the timeline's (measured 2026-10-23 through
    // probe_graph_parity, which read the panel 242..250 against the timeline's 243..252). The
    // constant survives for gui::InkImGui only, whose line box really is its own.
    const double lineHeight = metrics.height();
    const double baseline = aVAlign == VAlign::Top
        ? std::floor(double(aBox.top()) + metrics.ascent())
        : std::floor(double(aBox.top()) + std::max((double(aBox.height()) - lineHeight) * 0.5, 0.0)
                     + metrics.ascent());

    // THE HALO, when the caller asks for one (gui::kDrawTextHalo is off in the shipped panel,
    // where the contrast-outline pass does the ringing): the same 8 offsets, two rings, drawn
    // under the run.
    if (kDrawTextHalo && aHalo.isValid()) {
        static const int kOffsets[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
        QColor haloInk = aHalo;
        haloInk.setAlphaF(qBound(0.0f, haloInk.alphaF() * kHaloAlpha, 1.0f));
        mPainter.setPen(haloInk);
        for (int radius = kHaloRadius; radius >= 1; --radius)
            for (const int* offset : kOffsets)
                mPainter.drawText(QPointF(snappedX + offset[0] * radius, baseline + offset[1] * radius), aText);
    }
    mPainter.setPen(aColor);
    mPainter.drawText(QPointF(snappedX, baseline), aText);
    mPainter.restore();
}

} // namespace ink
} // namespace gui
