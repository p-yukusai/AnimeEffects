#include "gui/AppStyle.h"
#include "gui/theme/Colors.h"
#include "gui/theme/Icons.h"

#include <QIcon>
#include <QMenu>
#include <QAbstractItemView>

#include "ctrl/System.h"
#ifdef Q_OS_WIN
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
    #include <dwmapi.h>
#endif

namespace gui {

namespace {
constexpr int kTrackThickness = 12; // hit area / extent (invisible)
constexpr int kPillThickness = 4;
constexpr int kPillRadius = 2;
// Object-tree branch caret: the QIcon box the caret is rendered into
// (glyph inside is ~0.73 of the box at phosphor's 256 canvas).
constexpr int kBranchCaretSize = 14;
// Caret alpha: the caret is the dimmest element of the row (below the
// eye's baked 0.45 and the folder/file's antialiased ~0.45). No hover
// state, consistent with the eye.
constexpr qreal kBranchCaretOpacity = 0.4;
// Guide-line alpha: the tree's indentation lines are dimmer still than the
// caret, matching the row's secondary-chrome hierarchy.
constexpr qreal kBranchLineOpacity = 0.4;
// The pill colors come from the theme tokens (outline at rest, hover when
// hovered/dragged); see theme/Colors.h.
} // namespace

#ifdef Q_OS_WIN
namespace {
    class WinResizeFilter : public QObject
    {
    public:
        explicit WinResizeFilter(QObject *parent = nullptr) : QObject(parent) {}
        bool eventFilter(QObject *watched, QEvent *event) override
        {
            if (event->type() == QEvent::Resize) {
                // auto *resizeEvent = dynamic_cast<QResizeEvent*>(event);
                const QWidget* aWidget = qobject_cast<QWidget*>(watched);
                const auto hwnd = reinterpret_cast<HWND>(aWidget->winId());
                const QRect rect = aWidget->rect();
                /*const int width = rect.width();
                const int height = rect.height();*/
                constexpr int highFactor = (kPillRadius + kPillThickness) * 2;
                // constexpr int lowFactor = highFactor / 2;
                int pillRadiusW = kPillRadius; int pillRadiusH = kPillRadius;
                pillRadiusW += highFactor; pillRadiusH += highFactor;
                const auto hrgn = CreateRoundRectRgn(
                    rect.topLeft().x(), rect.topLeft().y(),
                    rect.bottomRight().x() , rect.bottomRight().y(),
                    pillRadiusW, pillRadiusH);
                SetWindowRgn(hwnd, hrgn, TRUE);
            }
            return QObject::eventFilter(watched, event);
        }
    };
}
#endif

AppStyle::AppStyle(QStyle* aBaseStyle): QProxyStyle(aBaseStyle) {}

void AppStyle::polish(QWidget* aWidget) {
    // Mark as translucent so the window manager doesn't draw behind them
    if (qobject_cast<QMenu*>(aWidget) || aWidget->inherits("QComboBoxPrivateContainer")) {
        aWidget->setAttribute(Qt::WA_TranslucentBackground);
        aWidget->setAttribute(Qt::WA_NoSystemBackground);
#ifdef Q_OS_WIN
        // We setup this because Windows is dumb and stupid and can't composite stuff without hand-holding
        if (QOperatingSystemVersion::current().microVersion() > 22000) {
            const auto hwnd = reinterpret_cast<HWND>(aWidget->winId());
            constexpr DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
            DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
        }
        else{ aWidget->installEventFilter(new WinResizeFilter()); }
#endif
    }
    // The style is re-polished on theme changes, so Colors::current() is
    // fresh.
    if (aWidget->inherits("QComboBoxPrivateContainer")) {
        QPalette pal = aWidget->palette();
        pal.setColor(QPalette::Highlight, theme::Colors::current().outline);
        pal.setColor(QPalette::HighlightedText, theme::Colors::current().text);
        aWidget->setPalette(pal);
        if (auto* view = aWidget->findChild<QAbstractItemView*>()) {
            QPalette vpal = view->palette();
            vpal.setColor(QPalette::Highlight, theme::Colors::current().outline);
            vpal.setColor(QPalette::HighlightedText, theme::Colors::current().text);
            view->setPalette(vpal);
        }
    }
    QProxyStyle::polish(aWidget);
}

int AppStyle::pixelMetric(PixelMetric aMetric, const QStyleOption* aOption,
                                const QWidget* aWidget) const {
    if (aMetric == PM_ScrollBarExtent)
        return kTrackThickness;
    return QProxyStyle::pixelMetric(aMetric, aOption, aWidget);
}

QSize AppStyle::sizeFromContents(ContentsType aType, const QStyleOption* aOption,
                                       const QSize& aContentsSize, const QWidget* aWidget) const {
    if (aType == CT_MenuItem && aOption) {
        // The combo popup's QComboMenuDelegate sizes its rows via
        // CT_MenuItem: 24px rows with no inter-row gap, and a 9px separator
        // row (4px breathing room above and below the centered 1px line).
        const auto* opt = qstyleoption_cast<const QStyleOptionMenuItem*>(aOption);
        if (opt) {
            QSize s = QProxyStyle::sizeFromContents(aType, aOption, aContentsSize, aWidget);
            s.setHeight(opt->menuItemType == QStyleOptionMenuItem::Separator ? 9 : 24);
            return s;
        }
    }
    return QProxyStyle::sizeFromContents(aType, aOption, aContentsSize, aWidget);
}

void AppStyle::drawPrimitive(PrimitiveElement aElement, const QStyleOption* aOption,
                                   QPainter* aPainter, const QWidget* aWidget) const {
    if (aElement == PE_PanelMenu && aOption) {
        // Deterministic menu/combo-popup panel: a sunken surface (recessed)
        // edged with the base token, drawn as ONE rounded path so body and
        // border share the same curve. QSS can't do this: the background fill
        // follows the border's centerline while the widget's square backing
        // bleeds into the corner cut, leaving a square body under a rounded
        // border. The QMenu rule must not declare a box
        // (background/border/radius), or QStyleSheetStyle draws the panel
        // itself.
        const theme::Colors c = theme::Colors::current();
        constexpr qreal kRadius = 8;
        // the floater tokens: sunken body on dark, white body on light
        const QColor kBody = c.floaterBody;
        const QColor kEdge = c.floaterEdge;
        aPainter->save();
        aPainter->setRenderHint(QPainter::Antialiasing);
        // The popup window is translucent, so the corner cut shows the
        // backdrop through real alpha instead of any backing fill.
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
    if (aElement == PE_IndicatorBranch && aOption) {
        // Object-tree branches: QSS has no sizing knob for ::branch (the
        // image is scaled to the style-computed cell, and width/height are
        // ignored), so the caret is drawn here as a real QIcon at a fixed
        // pixel size. The guide lines are painted with the hairline token
        // (same as the tinted icons they replace) and routed around the
        // caret box so they never show through the translucent glyph.
        // Fusion contributes no branch drawing of its own (it only paints
        // the arrow primitive), so nothing is lost by skipping the base call.
        const QRect r = aOption->rect;
        const int midX = r.left() + r.width() / 2;
        const int midY = r.top() + r.height() / 2;
        const bool hasCaret = aOption->state & State_Children;

        QRect box;
        if (hasCaret) {
            box = QRect(r.left() + (r.width() - kBranchCaretSize) / 2,
                        r.top() + (r.height() - kBranchCaretSize) / 2,
                        kBranchCaretSize, kBranchCaretSize);
        }

        aPainter->save();
        aPainter->setPen(theme::Colors::current().hairline);
        aPainter->setRenderHint(QPainter::Antialiasing, false);
        aPainter->setOpacity(kBranchLineOpacity);
        // Vertical connector: full height when a sibling row follows, else
        // only down to the corner (last child). Split around the caret box
        // so the guide line never crosses the glyph.
        if (aOption->state & State_Sibling) {
            if (hasCaret) {
                aPainter->drawLine(midX, r.top(), midX, box.top() - 1);
                aPainter->drawLine(midX, box.bottom() + 1, midX, r.bottom());
            } else {
                aPainter->drawLine(midX, r.top(), midX, r.bottom());
            }
        } else if (aOption->state & State_Item) {
            aPainter->drawLine(midX, r.top(), midX, hasCaret ? box.top() - 1 : midY);
        }

        // The elbow: a 1px horizontal stub at midY from the guide (or the
        // caret box) to the row's right edge, so rows read as connected to
        // the guide lines. QCommonStyle draws the same connector and the
        // QSS images this replaces (branch_more/branch_end) included it;
        // without it the rows would hang off the trunks. LTR layout (the
        // app has no RTL locales; Qt would mirror this for RTL).
        if (aOption->state & State_Item) {
            const int fromX = hasCaret ? box.right() + 1 : midX;
            aPainter->drawLine(fromX, midY, r.right(), midY);
        }

        if (hasCaret) {
            // No hover feedback (Figma-style): the click's state change is
            // the feedback, and hover would be inconsistent with the eye.
            const bool open = aOption->state & State_Open;
            const QString stem = QStringLiteral("caret-%1-bold").arg(open ? QStringLiteral("down") : QStringLiteral("right"));
            QIcon icon(theme::iconDir() + QLatin1Char('/') + stem + QStringLiteral(".svg"));
            aPainter->setRenderHint(QPainter::Antialiasing, true);
            aPainter->setOpacity(kBranchCaretOpacity);
            icon.paint(aPainter, box, Qt::AlignCenter);
            aPainter->setOpacity(1.0);
        }
        aPainter->restore();
        return;
    }
    QProxyStyle::drawPrimitive(aElement, aOption, aPainter, aWidget);
}

void AppStyle::drawControl(ControlElement aElement, const QStyleOption* aOption,
                                 QPainter* aPainter, const QWidget* aWidget) const {
    if (aElement == CE_MenuItem && aOption) {
        // Combo popups draw their items as CE_MenuItem rows; the QSS
        // QMenu::item rules do not reach them (the popup container is not a
        // QMenu), so without this the selected row shows Fusion's stock
        // highlight-with-border. Draw the same treatment QMenu gets from
        // QSS: flat rounded accent for the selected row, a centered 1px
        // hairline for separators.
        const auto* opt = qstyleoption_cast<const QStyleOptionMenuItem*>(aOption);
        if (!opt)
            return QProxyStyle::drawControl(aElement, aOption, aPainter, aWidget);
        if (opt->menuItemType == QStyleOptionMenuItem::Separator) {
            const QRect r = opt->rect;
            // Inset to the item content extent so the line's ends align
            // with the item text (8px each side, like the text offset).
            const int inset = 8;
            aPainter->save();
            aPainter->setPen(Qt::NoPen);
            aPainter->setBrush(theme::Colors::current().hairline);
            aPainter->drawRect(QRect(r.left() + inset, r.center().y(), r.width() - inset * 2, 1));
            aPainter->restore();
            return;
        }
        if (opt->state & State_Selected) {
            const theme::Colors c = theme::Colors::current();
            // 4px off the popup edge, matching the QMenu item inset (the
            // row rect itself sits ~3px in, so the pill inset is 1px)
            QRect pill = opt->rect.adjusted(1, 2, -1, -2);
            aPainter->save();
            aPainter->setRenderHint(QPainter::Antialiasing);
            aPainter->setPen(Qt::NoPen);
            aPainter->setBrush(c.accentHover);
            aPainter->drawRoundedRect(pill, 4, 4);
            // Text at the same x the base style uses for the unselected
            // rows (rect + 8px) so nothing jumps when the row is hovered.
            // accentText (not `text`): the row is on the accent fill, and
            // the 0.93/1.0 content tokens exist for exactly this contrast.
            aPainter->setPen(c.accentText);
            aPainter->setFont(opt->font);
            aPainter->drawText(QRect(opt->rect.left() + 7, pill.top(), opt->rect.width() - 14, pill.height()),
                               Qt::AlignVCenter | Qt::AlignLeft, opt->text);
            aPainter->restore();
            return;
        }
    }
    QProxyStyle::drawControl(aElement, aOption, aPainter, aWidget);
}

void AppStyle::drawComplexControl(ComplexControl aControl, const QStyleOptionComplex* aOption,
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
            (opt->activeSubControls & SC_ScrollBarSlider) || (opt->state & State_Sunken);
        aPainter->save();
        aPainter->setRenderHint(QPainter::Antialiasing);
        aPainter->setPen(Qt::NoPen);
        aPainter->setBrush(hot ? theme::Colors::current().hover : theme::Colors::current().outline);
        aPainter->drawRoundedRect(pill, kPillRadius, kPillRadius);
        aPainter->restore();
        return;
    }
    QProxyStyle::drawComplexControl(aControl, aOption, aPainter, aWidget);
}

} // namespace gui
