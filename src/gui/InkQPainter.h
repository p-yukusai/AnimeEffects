#ifndef GUI_INKQPAINTER_H
#define GUI_INKQPAINTER_H

#include "gui/Ink.h"

#include <QFont>

class QPainter;

namespace gui {
namespace ink {

// THE PORTABLE BACKEND: the same vocabulary over Qt's own rasteriser, which is what the panel
// uses on a Qt without the Canvas Painter module (6.11 and older) - and the reference the
// Canvas backend is measured against, because Qt is where every text and pixel convention the
// panel inherited was measured in the first place.
//
// It needs a QFont because Qt, unlike ImGui's atlas, asks for one at draw time: the HOST's font
// is the panel's measuring font (the model is built with the same one), so the numbers a run
// occupies and the box the model reserved for it agree.
class InkQPainter : public Ink {
public:
    // The painter must already be active on its device; this class does not begin()/end() it,
    // because the host owns the device (a widget, an image, a print).
    InkQPainter(QPainter& aPainter, const QFont& aFont);

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
    QPainter& mPainter;
    QFont mFont;
};

} // namespace ink
} // namespace gui

#endif // GUI_INKQPAINTER_H
