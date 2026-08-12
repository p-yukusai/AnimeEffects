#ifndef GUI_SCROLLBARSTYLE_H
#define GUI_SCROLLBARSTYLE_H

#include <QProxyStyle>

namespace gui {

// Renders scrollbar handles deterministically: a 4px pill drawn with
// QPainter::drawRoundedRect, centered in a 12px invisible track. The QSS
// border-image path for scrollbar subcontrols distorts a 4px pill's caps
// (protruding / inverted ends), so scrollbars get their own proxy style.
// Muted at rest (#565656), bright on hover/drag (#7e7e7e).
class ScrollBarStyle: public QProxyStyle {
    Q_OBJECT
public:
    explicit ScrollBarStyle(QStyle* aBaseStyle);

    int pixelMetric(PixelMetric aMetric, const QStyleOption* aOption = nullptr,
                    const QWidget* aWidget = nullptr) const override;
    void drawComplexControl(ComplexControl aControl, const QStyleOptionComplex* aOption,
                            QPainter* aPainter, const QWidget* aWidget = nullptr) const override;
    void drawPrimitive(PrimitiveElement aElement, const QStyleOption* aOption, QPainter* aPainter,
                       const QWidget* aWidget = nullptr) const override;
    bool eventFilter(QObject* aObject, QEvent* aEvent) override;
};

} // namespace gui

#endif // GUI_SCROLLBARSTYLE_H
