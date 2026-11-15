#include "gui/AppStyle.h"
#include "gui/PopupWindow.h"
#include "gui/theme/Colors.h"
#include "gui/theme/Icons.h"

// The scroll bar's lane and pill - the SAME definition the graph panel paints
// from (gui/GraphPanelFrame.cpp's bar pass), so the app's bar and the panel's
// lanes cannot disagree about the thickness, the centring or the caps.
#include "ctrl/graph/graph_ScrollBarGeometry.h"

#include <algorithm>
#include <QAbstractItemView>
#include <QBitmap>
#include <QEvent>
#include <QIcon>
#include <QLinearGradient>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>
#include <QStyleOptionSlider>

namespace gui {

namespace {
// Easing blend slider (a QSlider with no QSS so it routes through CC_Slider):
// a 4px gradient track and a round accent-bright knob, matching the mock.
constexpr int kSliderGrooveThickness = 4;
constexpr int kSliderKnobRadius = 7;     // 14px thumb (border-box, border inside)
constexpr qreal kSliderFillRadius = 5.0; // white fill inside the 2px border
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

AppStyle::AppStyle(QStyle* aBaseStyle): QProxyStyle(aBaseStyle) {}

bool AppStyle::eventFilter(QObject* aWatched, QEvent* aEvent) {
    if (aWatched && aWatched->isWidgetType()) {
        auto* w = static_cast<QWidget*>(aWatched);
        // Mask path only: this filter is installed from polish() where the
        // platform does not round popups itself (see gui/PopupWindow.h), which
        // on Windows means below 11 22H2.
        // Only QMenu gets the rounded mask. The combo drop-down
        // (QComboBoxPrivateContainer) must NOT: Qt's
        // QComboBoxPrivateContainer::paintEvent draws the popup panel
        // (PE_PanelMenu → the rounded floaterBody) only when mask().isEmpty()
        // is true. Stamp a mask on it and QMenu-like panel paint is skipped,
        // leaving the drop-down transparent. Its own rounded corners come from
        // the translucent window + the rounded PE_PanelMenu path — the same
        // mechanism QMenu uses — so the combo needs no mask.
        if ((aEvent->type() == QEvent::Resize || aEvent->type() == QEvent::Show)
            && qobject_cast<QMenu*>(w)) {
            // Rounded mask fallback for compositors that don't composite true
            // translucency on Qt::Popup (notably Windows). Cheap QBitmap mask;
            // also serves as offscreen/test fallback. Radius matches
            // PE_PanelMenu (8px).
            constexpr qreal kRadius = 8.0;
            QBitmap mask(w->size());
            mask.fill(Qt::color0);
            QPainter p(&mask);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setBrush(Qt::color1);
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(QRect(QPoint(0, 0), w->size()), kRadius, kRadius);
            w->setMask(mask);
        }
    }
    return QProxyStyle::eventFilter(aWatched, aEvent);
}

void AppStyle::polish(QWidget* aWidget) {
    // Menus are opaque OS popup windows by default (their backing is the
    // palette Window role). For real rounded corners the popup must be an
    // alpha window, so mark them translucent at polish time — before the
    // platform window is created. The combo popup (QComboBoxPrivateContainer)
    // draws the same PE_PanelMenu and needs the same treatment.
    const bool isFloater = qobject_cast<QMenu*>(aWidget) || aWidget->inherits("QComboBoxPrivateContainer");
    if (isFloater) {
        // Keep Qt::Popup behavior (auto-close on outside click, grab) —
        // FramelessWindowHint alone would make it a normal top-level. OR
        // with existing flags rather than replacing, so Qt::Popup is kept.
        // It is also what makes the translucency real on Windows: Qt only
        // layers a translucent window that has no native frame
        // (QWindowsWindow::setWindowLayered: hasAlpha && hasNoNativeFrame ->
        // UpdateLayeredWindow), and an unlayered window drops the alpha
        // channel, so the corner cut would come out black.
        aWidget->setWindowFlags(aWidget->windowFlags() | Qt::FramelessWindowHint);
        aWidget->setAttribute(Qt::WA_TranslucentBackground, true);
        aWidget->setAttribute(Qt::WA_NoSystemBackground, true);
        // Shape the popup itself. On Windows 11 (22H2+) DWM rounds the window,
        // which is also what makes the system-drawn shadow follow the 8px
        // panel; elsewhere a region mask does the shaping. The two are mutually
        // exclusive — a window region is exactly what makes a window
        // un-roundable for DWM — see gui/PopupWindow.h.
        if (usesSystemPopupRounding()) {
            roundPopupWindow(aWidget);
        } else {
            // Mask must track resizes/shows — install filter on the popup
            // itself.
            aWidget->removeEventFilter(this);
            aWidget->installEventFilter(this);
        }
    }
    // The combo popup paints its items as CE_MenuItem through Qt's
    // QComboMenuDelegate, which builds the style option from the VIEW's
    // palette; the QSS selection-background-color never reaches that paint
    // path. The popup is translucent (above) and carries the QMenu-style
    // grey on the view's palette Highlight without touching the other roles
    // (the view's Base stays transparent so the recessed panel shows).
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
        return ctrl::graph::scrollBar::kLaneThickness;
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
        const qreal kRadius = 8;
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
        const bool hasCaret = aOption->state & QStyle::State_Children;

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
        if (aOption->state & QStyle::State_Sibling) {
            if (hasCaret) {
                aPainter->drawLine(midX, r.top(), midX, box.top() - 1);
                aPainter->drawLine(midX, box.bottom() + 1, midX, r.bottom());
            } else {
                aPainter->drawLine(midX, r.top(), midX, r.bottom());
            }
        } else if (aOption->state & QStyle::State_Item) {
            aPainter->drawLine(midX, r.top(), midX, hasCaret ? box.top() - 1 : midY);
        }

        // The elbow: a 1px horizontal stub at midY from the guide (or the
        // caret box) to the row's right edge, so rows read as connected to
        // the guide lines. QCommonStyle draws the same connector and the
        // QSS images this replaces (branch_more/branch_end) included it;
        // without it the rows would hang off the trunks. LTR layout (the
        // app has no RTL locales; Qt would mirror this for RTL).
        if (aOption->state & QStyle::State_Item) {
            const int fromX = hasCaret ? box.right() + 1 : midX;
            aPainter->drawLine(fromX, midY, r.right(), midY);
        }

        if (hasCaret) {
            // No hover feedback (Figma-style): the click's state change is
            // the feedback, and hover would be inconsistent with the eye.
            const bool open = aOption->state & QStyle::State_Open;
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
        // QSS: flat rounded accent for the selected row and a centered 1px
        // rule for separators. The rule's ink is floaterEdge to stay
        // identical to QMenu::separator in standard.ssa: a rule ON the
        // popup surface must take that surface's own edge token, because
        // hairline shares the floater body's palette cell in dark (both
        // neutral column 8, #262626, so the line would be invisible) while
        // floaterEdge is one step above the body in both themes.
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
            aPainter->setBrush(theme::Colors::current().floaterEdge);
            aPainter->drawRect(QRect(r.left() + inset, r.center().y(), r.width() - inset * 2, 1));
            aPainter->restore();
            return;
        }
        if (opt->state & QStyle::State_Selected) {
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

        // THE pill, from the one definition the graph panel paints from too
        // (ctrl/graph/graph_ScrollBarGeometry.h): 4px centred in the 12px lane,
        // spanning the slider's run, rounded by the shared radius. Nothing about
        // the geometry is computed here.
        const ctrl::graph::scrollBar::Pill pill =
            ctrl::graph::scrollBar::pill(opt->rect, slider, opt->orientation);

        const bool hot =
            (opt->activeSubControls & SC_ScrollBarSlider) || (opt->state & QStyle::State_Sunken);
        aPainter->save();
        aPainter->setRenderHint(QPainter::Antialiasing);
        aPainter->setPen(Qt::NoPen);
        aPainter->setBrush(hot ? theme::Colors::current().hover : theme::Colors::current().outline);
        aPainter->drawRoundedRect(pill.rect, pill.radius, pill.radius);
        aPainter->restore();
        return;
    }
    if (aControl == CC_Slider) {
        // The Easing blend slider (and any non-QSS QSlider): a 4px gradient
        // track (recessed -> accent-bright) and a round accent-bright knob.
        const auto* opt = qstyleoption_cast<const QStyleOptionSlider*>(aOption);
        if (!opt)
            return;
        const theme::Colors& c = theme::Colors::current();
        const QRect groove =
            QProxyStyle::subControlRect(CC_Slider, opt, SC_SliderGroove, aWidget);

        // Position the knob from the track and the thumb size, NOT the base
        // style's SC_SliderHandle rect — that handle's length comes from the
        // style's PM_SliderHandleLength metric, which differs from our 14px
        // thumb, so it left the knob offset from the track ends (not flush at
        // 100%). Using the thumb's own radius (kSliderKnobRadius = 7px) makes
        // it automatic: 0% -> flush left, 100% -> flush right.
        const int span = opt->maximum - opt->minimum;
        const double frac = span != 0
            ? std::clamp(double(opt->sliderPosition - opt->minimum) / double(span), 0.0, 1.0)
            : 0.0;
        const qreal kr = kSliderKnobRadius;

        QRect track;
        QPointF knob;
        if (opt->orientation == Qt::Horizontal) {
            const int y = groove.center().y() - kSliderGrooveThickness / 2;
            track = QRect(groove.left(), y, groove.width(), kSliderGrooveThickness);
            knob = QPointF(track.left() + kr + frac * (track.width() - 2.0 * kr),
                           groove.center().y());
        } else {
            const int x = groove.center().x() - kSliderGrooveThickness / 2;
            track = QRect(x, groove.top(), kSliderGrooveThickness, groove.height());
            knob = QPointF(groove.center().x(),
                           track.top() + kr + frac * (track.height() - 2.0 * kr));
        }

        aPainter->save();
        aPainter->setRenderHint(QPainter::Antialiasing);
        // 4px recessed -> accent-bright gradient track (the mock's range
        // input track is a linear-gradient(90deg, recessed, accent-bright)),
        // not a flat fill.
        QLinearGradient grad(track.topLeft(), track.topRight());
        grad.setColorAt(0.0, c.recessed);
        grad.setColorAt(1.0, c.accentBright);
        aPainter->setPen(Qt::NoPen);
        aPainter->setBrush(grad);
        aPainter->drawRoundedRect(track, kSliderGrooveThickness / 2, kSliderGrooveThickness / 2);

        // Mock thumb is 14px with a 2px accent-bright border INSIDE (border-box)
        // and a white fill. Draw the fill (r=5, 10px) then the border centered
        // on r=6 (spans 5..7 = 2px) so the knob is exactly 14px — the previous
        // pen-on-r7 made a 16px knob (larger than the mock's).
        aPainter->setPen(Qt::NoPen);
        aPainter->setBrush(c.text);
        aPainter->drawEllipse(knob, kSliderFillRadius, kSliderFillRadius);
        QPen pen(c.accentBright);
        pen.setWidthF(2.0);
        aPainter->setPen(pen);
        aPainter->setBrush(Qt::NoBrush);
        aPainter->drawEllipse(knob, kSliderKnobRadius - 1.0, kSliderKnobRadius - 1.0);
        aPainter->restore();
        return;
    }

    QProxyStyle::drawComplexControl(aControl, aOption, aPainter, aWidget);
}

} // namespace gui
