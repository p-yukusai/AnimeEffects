#ifndef GUI_APPSTYLE_H
#define GUI_APPSTYLE_H

#include <QProxyStyle>

namespace gui {

// The app-wide QProxyStyle (installed by GUIResources::applyAppearance, slotted
// under the stylesheet wrapper). Custom-paints the primitives QSS can't do
// deterministically:
//  - CC_ScrollBar: a 4px pill centered in a 12px invisible track (the QSS
//    border-image path distorts the pill's caps; colors from theme tokens).
//  - PE_PanelMenu / CE_MenuItem: rounded popup panels and combo rows.
//  - PE_IndicatorBranch: object-tree guide lines + the caret, drawn as a real
//    QIcon at a fixed pixel size (QSS ::branch has no sizing knob).
class AppStyle: public QProxyStyle {
    Q_OBJECT
public:
    explicit AppStyle(QStyle* aBaseStyle);

    int pixelMetric(PixelMetric aMetric, const QStyleOption* aOption = nullptr,
                    const QWidget* aWidget = nullptr) const override;
    QSize sizeFromContents(ContentsType aType, const QStyleOption* aOption,
                           const QSize& aContentsSize, const QWidget* aWidget = nullptr) const override;
    void drawComplexControl(ComplexControl aControl, const QStyleOptionComplex* aOption,
                            QPainter* aPainter, const QWidget* aWidget = nullptr) const override;
    void drawControl(ControlElement aElement, const QStyleOption* aOption, QPainter* aPainter,
                     const QWidget* aWidget = nullptr) const override;
    void drawPrimitive(PrimitiveElement aElement, const QStyleOption* aOption, QPainter* aPainter,
                       const QWidget* aWidget = nullptr) const override;
    void polish(QWidget* aWidget) override;
};

} // namespace gui

#endif // GUI_APPSTYLE_H
