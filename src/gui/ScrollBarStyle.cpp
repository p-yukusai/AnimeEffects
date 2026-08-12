#include "gui/ScrollBarStyle.h"

#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>
#include <QStyleOptionSlider>

namespace gui {

namespace {
constexpr int kTrackThickness = 12; // hit area / extent (invisible)
constexpr int kPillThickness = 4;
constexpr int kPillRadius = 2;
// design tokens: rest = outline #565656 (present but not loud), hover/drag
// brightens to #7e7e7e. The old #7e7e7e-at-rest thumb grabbed too much
// attention in panels like the property dock.
const QColor kPillColor(0x56, 0x56, 0x56);
const QColor kPillColorActive(0x7e, 0x7e, 0x7e);
} // namespace

ScrollBarStyle::ScrollBarStyle(QStyle* aBaseStyle): QProxyStyle(aBaseStyle) {}

void ScrollBarStyle::polish(QWidget* aWidget) {
    // Menus are opaque OS popup windows by default (their backing is the
    // palette Window role). For real rounded corners the popup must be an
    // alpha window, so mark them translucent at polish time — before the
    // platform window is created.
    if (qobject_cast<QMenu*>(aWidget)) {
        aWidget->setAttribute(Qt::WA_TranslucentBackground);
        aWidget->setAttribute(Qt::WA_NoSystemBackground);
    }
    QProxyStyle::polish(aWidget);
}

int ScrollBarStyle::pixelMetric(PixelMetric aMetric, const QStyleOption* aOption,
                                const QWidget* aWidget) const {
    if (aMetric == PM_ScrollBarExtent)
        return kTrackThickness;
    return QProxyStyle::pixelMetric(aMetric, aOption, aWidget);
}

void ScrollBarStyle::drawPrimitive(PrimitiveElement aElement, const QStyleOption* aOption,
                                   QPainter* aPainter, const QWidget* aWidget) const {
    if (aElement == PE_PanelMenu && aOption) {
        // Deterministic menu panel: a base surface (#262626) edged with the
        // hairline token, drawn as ONE rounded path so body and border share
        // the same curve. QSS can't do this: the background fill follows the
        // border's centerline while the widget's square backing bleeds into
        // the corner cut, leaving a square body under a rounded border.
        // The QMenu rule must not declare a box (background/border/radius),
        // or QStyleSheetStyle draws the panel itself.
        const qreal kRadius = 8;
        const QColor kBody(0x26, 0x26, 0x26);
        const QColor kEdge(0x34, 0x34, 0x34);
        aPainter->save();
        aPainter->setRenderHint(QPainter::Antialiasing);
        // The popup window is translucent (see eventFilter), so the corner
        // cut shows the backdrop through real alpha instead of any backing
        // fill.
        const QRectF rect = QRectF(aOption->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath path;
        path.addRoundedRect(rect, kRadius, kRadius);
        aPainter->fillPath(path, kBody);
        aPainter->setPen(QPen(kEdge, 1));
        aPainter->setBrush(Qt::NoBrush);
        aPainter->drawPath(path);
        aPainter->restore();
        return;
    }
    QProxyStyle::drawPrimitive(aElement, aOption, aPainter, aWidget);
}

void ScrollBarStyle::drawComplexControl(ComplexControl aControl, const QStyleOptionComplex* aOption,
                                        QPainter* aPainter, const QWidget* aWidget) const {
    if (aControl == CC_ScrollBar) {
        // Invisible track (the dock surface shows through); only the pill.
        const auto* opt = qstyleoption_cast<const QStyleOptionSlider*>(aOption);
        if (!opt)
            return;
        const QRect slider = subControlRect(CC_ScrollBar, opt, SC_ScrollBarSlider, aWidget);
        if (slider.isEmpty())
            return;

        // A 4px pill centered in the 12px track, spanning the slider's run.
        QRect pill;
        if (opt->orientation == Qt::Vertical) {
            const int x = (opt->rect.width() - kPillThickness) / 2;
            pill = QRect(x, slider.y(), kPillThickness, slider.height());
        } else {
            const int y = (opt->rect.height() - kPillThickness) / 2;
            pill = QRect(slider.x(), y, slider.width(), kPillThickness);
        }

        const bool hot =
            (opt->activeSubControls & SC_ScrollBarSlider) || (opt->state & QStyle::State_Sunken);
        aPainter->save();
        aPainter->setRenderHint(QPainter::Antialiasing);
        aPainter->setPen(Qt::NoPen);
        aPainter->setBrush(hot ? kPillColorActive : kPillColor);
        aPainter->drawRoundedRect(pill, kPillRadius, kPillRadius);
        aPainter->restore();
        return;
    }
    QProxyStyle::drawComplexControl(aControl, aOption, aPainter, aWidget);
}

} // namespace gui
