#ifndef GUI_INKCANVAS_H
#define GUI_INKCANVAS_H

#include "gui/Ink.h"

#include <QFont>
#include <QVector>

class QCanvasPainter;

namespace gui {
namespace ink {

// THE GPU BACKEND: the same vocabulary over Qt Canvas Painter (6.12+), which is where the panel
// goes once the module is available - one vocabulary, two rasterisers, no third copy of a single
// shape decision.
//
// The pixel conventions this backend has to state explicitly are the ones the timeline's Canvas
// port already paid for (see AGENTS.md):
//   * a 1px ROW or COLUMN cannot be stroked: a span of [x, x+1) inks three columns and
//     [x+0.5, x+1.5) two, so every axis-aligned chrome line is drawn as a FILLED rect;
//   * a stroke's band sits INSIDE the radius: the painting path is pulled in by half the
//     width, and a caller that wants the band centred on a reference radius adds that
//     half pixel itself (the marker and ring passes do);
//   * text is anchored explicitly (Left/Centre/Right, and a BASELINE computed from the font's
//     own ascent), because a Canvas text origin defaults to a top-left box that is not the box
//     the panel's model measured.
class InkCanvas : public Ink {
public:
    InkCanvas(QCanvasPainter& aPainter, const QFont& aFont);

    void setLineAntialias(bool aEnabled) override;
    void pushClip(const QRect& aRect) override;
    void popClip() override;
    void fillRect(const QRectF& aRect, const QColor& aColor, double aRadius = 0.0) override;
    void strokeRect(const QRectF& aRect, const QColor& aColor, double aRadius, double aWidth) override;
    void line(const QPointF& aFrom, const QPointF& aTo, const QColor& aColor, double aWidth) override;
    void stroke(const QVector<QPointF>& aPoints, const QColor& aColor, double aHalfWidth) override;
    void circleFill(const QPointF& aCentre, double aRadius, const QColor& aColor) override;
    void circleStroke(const QPointF& aCentre, double aRadius, const QColor& aColor, double aWidth) override;
    void text(
        const QRect& aBox, const QString& aText, HAlign aAlign, VAlign aVAlign, const QColor& aColor,
        const QColor& aHalo = QColor(), int aRightGap = 0
    ) override;

private:
    QCanvasPainter& mPainter;
    QFont mFont;
    // Canvas2D's clip is a property, not a stack, so the stack the panel uses is kept here.
    QVector<QRect> mClips;
    bool mAntialias = true;
};

} // namespace ink
} // namespace gui

#endif // GUI_INKCANVAS_H

