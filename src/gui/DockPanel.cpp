#include "gui/DockPanel.h"

#include "gui/DockTabBar.h"
#include "gui/theme/Colors.h"

#include <QContextMenuEvent>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QWindow>
#include <QResizeEvent>

namespace gui {

void showDockPanelMenu(QDockWidget* aDock, const QPoint& aGlobalPos) {
    if (!aDock) return;

    QMenu menu;
    const bool floating = aDock->isFloating();
    // DockPanel::tr, not QObject::tr: the strings belong to this class's catalog
    // context, which is where lupdate files its call sites.
    QAction* floatAction = menu.addAction(floating ? DockPanel::tr("Dock panel")
                                                   : DockPanel::tr("Undock panel"));
    QAction* hideAction = menu.addAction(DockPanel::tr("Hide panel"));

    QAction* chosen = menu.exec(aGlobalPos);
    if (!chosen) return;

    if (chosen == floatAction) {
        dockSpikeLog(aDock, QStringLiteral("menu -> setFloating(%1)").arg(!floating));
        if (auto* panel = qobject_cast<DockPanel*>(aDock))
            panel->setPanelFloating(!floating);
        else
            aDock->setFloating(!floating);
    } else if (chosen == hideAction) {
        // hiding through the dock keeps the Panels menu's check state in step
        dockSpikeLog(aDock, QStringLiteral("hide panel"));
        aDock->hide();
    }
}

class DockPanel::RingBand : public QWidget {
public:
    RingBand(DockPanel* aPanel, Qt::Edges aSpanned):
        QWidget(aPanel),
        mPanel(aPanel),
        mSpanned(aSpanned) {
        setObjectName(QStringLiteral("DockRingBand"));
        setMouseTracking(true);
        // an input surface only: the ring itself is painted by DockPanel
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
    }

protected:
    void mouseMoveEvent(QMouseEvent* aEvent) override {
        mPanel->ensureRing();
        if (!(aEvent->buttons() & Qt::LeftButton))
            setCursor(cursorFor(edgesAt(aEvent->position().toPoint())));
        QWidget::mouseMoveEvent(aEvent);
    }

    void mousePressEvent(QMouseEvent* aEvent) override {
        if (aEvent->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(aEvent);
            return;
        }
        // Only the compositor can resize a frameless Wayland toplevel, and only
        // from an edge it is told about: this is the same call a decorated
        // window's edge gets, which is what keeps the anchor under the cursor.
        mPanel->ensureRing();
        const Qt::Edges edges = edgesAt(aEvent->position().toPoint());
        QWindow* window = mPanel->windowHandle();
        const bool accepted = window && window->startSystemResize(edges);
        dockSpikeLog(mPanel, QStringLiteral("ring band press pos=%1,%2 edges=0x%3 startSystemResize=%4")
                .arg(int(aEvent->position().x())).arg(int(aEvent->position().y()))
                .arg(int(edges), 0, 16).arg(accepted));
        aEvent->accept();
    }

private:
    /// The band spans one edge; a position near either end of it adds the
    /// perpendicular edge, so the corners resize diagonally.
    Qt::Edges edgesAt(const QPoint& aPos) const {
        Qt::Edges edges = mSpanned;
        const QPoint inPanel = mapTo(mPanel, aPos);
        const QRect panelRect = mPanel->rect();
        if (mSpanned & (Qt::TopEdge | Qt::BottomEdge)) {
            if (inPanel.x() < kBand) edges |= Qt::LeftEdge;
            if (inPanel.x() >= panelRect.width() - kBand) edges |= Qt::RightEdge;
        } else {
            if (inPanel.y() < kBand) edges |= Qt::TopEdge;
            if (inPanel.y() >= panelRect.height() - kBand) edges |= Qt::BottomEdge;
        }
        return edges;
    }

    static Qt::CursorShape cursorFor(Qt::Edges aEdges) {
        const bool top = aEdges & Qt::TopEdge;
        const bool bottom = aEdges & Qt::BottomEdge;
        const bool left = aEdges & Qt::LeftEdge;
        if ((top && left) || (bottom && (aEdges & Qt::RightEdge))) return Qt::SizeFDiagCursor;
        if ((top && (aEdges & Qt::RightEdge)) || (bottom && left)) return Qt::SizeBDiagCursor;
        if (top || bottom) return Qt::SizeVerCursor;
        return Qt::SizeHorCursor;
    }

    DockPanel* mPanel;
    Qt::Edges mSpanned;
};

DockPanel::DockPanel(QWidget* aParent):
    QDockWidget(aParent) {
    // The title row is a tab bar - the same widget Qt builds for a group - so a
    // panel alone in its area and a panel sharing one are the same thing: one
    // tab or several, one widget class, one QSS rule (see DockTabBar).
    setTitleBarWidget(new DockTabBar(this));
    // top, bottom, then the vertical bands: created last, they take the corners
    mRing = {new RingBand(this, Qt::TopEdge), new RingBand(this, Qt::BottomEdge),
             new RingBand(this, Qt::LeftEdge), new RingBand(this, Qt::RightEdge)};
    installEventFilter(this);
    connect(this, &QDockWidget::topLevelChanged, this, &DockPanel::onTopLevelChanged);
}

void DockPanel::ensureRing() {
    if (isFloating() && mRingSize != size())
        layoutRing();
}

void DockPanel::layoutRing() {
    if (mRing.size() != 4) return;

    mRingSize = size();

    const int w = width();
    const int h = height();
    mRing[0]->setGeometry(0, 0, w, kBand);
    mRing[1]->setGeometry(0, h - kBand, w, kBand);
    mRing[2]->setGeometry(0, 0, kBand, h);
    mRing[3]->setGeometry(w - kBand, 0, kBand, h);

    // the ring is the floating window's edge, so it only exists while floating;
    // a docked panel's edges belong to the main window's sashes
    const bool visible = isFloating();
    for (RingBand* band : mRing) {
        if (visible) band->raise();
        band->setVisible(visible);
    }

    if (visible) logRingProbe();
}

/// Diagnostic, with the bands placed: what the window's own edge actually
/// hit-tests as, inside and outside the frame line. childAt() uses the same
/// hit-test input dispatch does, so this answers "is the edge grabbable" from
/// the app rather than from a guess.
void DockPanel::logRingProbe() {
    if (!dockSpikeLogEnabled()) return;
    const int h = height();
    const int w = width();
    const auto nameAt = [this](int aX, int aY) {
        QWidget* hit = childAt(aX, aY);
        return hit ? hit->objectName() : QStringLiteral("dock");
    };
    dockSpikeLog(this, QStringLiteral("ring probe %1x%2 radius=%3 halo=%4 band=%5 | left edge=0:%6 "
                                      "frame=%7:%8 inside=%9:%10 | top 0:%11 frame=%12 inside=%13 | "
                                      "header=%14,%15 %16x%17")
            .arg(w).arg(h).arg(kRadius).arg(kGrabMargin).arg(kBand)
            .arg(nameAt(0, h / 2)).arg(kGrabMargin).arg(nameAt(kGrabMargin, h / 2))
            .arg(kBand - 1).arg(nameAt(kBand - 1, h / 2))
            .arg(nameAt(w / 2, 0)).arg(nameAt(w / 2, kGrabMargin)).arg(nameAt(w / 2, kBand - 1))
            .arg(titleBarWidget() ? titleBarWidget()->x() : -1)
            .arg(titleBarWidget() ? titleBarWidget()->y() : -1)
            .arg(titleBarWidget() ? titleBarWidget()->width() : -1)
            .arg(titleBarWidget() ? titleBarWidget()->height() : -1));
}

void DockPanel::resizeEvent(QResizeEvent* aEvent) {
    QDockWidget::resizeEvent(aEvent);
    layoutRing();
}

void DockPanel::setPanelFloating(bool aFloating) {
    // Set before setFloating(): the attribute is read when the floating window
    // is created, and cleared again on the way back so a docked panel is an
    // ordinary opaque child (a translucent child that paints nothing keeps
    // stale pixels, which looked like panels overlapping after a layout change).
    setAttribute(Qt::WA_TranslucentBackground, aFloating);
    setAttribute(Qt::WA_NoSystemBackground, aFloating);
    setFloating(aFloating);
    if (!aFloating) {
        setAttribute(Qt::WA_TranslucentBackground, false);
        setAttribute(Qt::WA_NoSystemBackground, false);
    }
}

bool DockPanel::isTabGrouped() {
    // Asked of the main window rather than inferred from the widget tree: a
    // tabified dock keeps living as a direct child of the QMainWindow (measured
    // - Qt 6.11 does not move it into a QDockWidgetGroupWindow), so the only
    // public answer to "am I sharing an area" is QMainWindow::tabifiedDockWidgets.
    // The remembered window is the reliable handle, because a floating panel is
    // its own toplevel and its parent is not the window any more; fall back to
    // the parent for a panel built outside MainWindow (the tests do that).
    auto* window = mMainWindow ? mMainWindow.data() : qobject_cast<QMainWindow*>(parentWidget());
    return window && !window->tabifiedDockWidgets(this).isEmpty();
}

void DockPanel::updateTitleChrome() {
    QWidget* header = titleBarWidget();
    if (!header) return;
    const bool showHeader = isFloating() || !isTabGrouped();
    if (header->isVisible() != showHeader) {
        header->setVisible(showHeader);
        dockSpikeLog(this, QStringLiteral("header visible=%1 (tab grouped=%2 parent=%3)")
                .arg(showHeader).arg(isTabGrouped())
                .arg(parentWidget() ? QString::fromLatin1(parentWidget()->metaObject()->className())
                                    : QStringLiteral("-")));
    }
}

bool DockPanel::event(QEvent* aEvent) {
    // Membership of a tab group is only observable through the parent, and it
    // changes when docks are tabified or split apart again.
    switch (aEvent->type()) {
    case QEvent::ParentChange:
    case QEvent::Show:
    case QEvent::LayoutRequest:
        updateTitleChrome();
        break;
    default:
        break;
    }
    return QDockWidget::event(aEvent);
}

void DockPanel::paintEvent(QPaintEvent* aEvent) {
    QDockWidget::paintEvent(aEvent);

    const theme::Colors c = theme::Colors::current();

    // The dock paints its own floor in both states. Floating it needs one (the
    // window would otherwise be bare), and docked it is the safe default: with
    // WA_NoSystemBackground set for the float, Qt does not erase this widget,
    // so anything left unpainted would retain whatever was on screen before.
    if (!isFloating()) {
        QPainter p(this);
        p.fillRect(rect(), c.base);
        return;
    }

    // The floating window is translucent so its corners can actually be round:
    // everything outside the rounded body stays unpainted and the backdrop
    // shows through, the same trick AppStyle uses for menus. The body sits
    // kGrabMargin inside the window - that margin is the outer half of the
    // resize band and is never painted, so the frame line the user sees is not
    // the window edge and can be grabbed from either side. The content's own
    // square corners sit kRing inside the body, which is exactly the radius.
    QPainterPath body;
    const qreal inset = kGrabMargin + 0.5;
    body.addRoundedRect(QRectF(rect()).adjusted(inset, inset, -inset, -inset), kRadius, kRadius);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillPath(body, c.base);
    p.setPen(QPen(c.outline, 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(body);
}

void DockPanel::contextMenuEvent(QContextMenuEvent* aEvent) {
    // the floating header has its own menu; this one covers the frame and the
    // body, and is what a floating panel's right-click reaches outside the
    // header's inset
    showDockPanelMenu(this, aEvent->globalPos());
    aEvent->accept();
}

bool DockPanel::eventFilter(QObject* aObject, QEvent* aEvent) {
    // Diagnostic: what the dock itself receives. Qt's dock drag needs the press
    // to arrive here and then the moves, so this separates "the press never
    // arrived" from "it arrived and Qt's drag still did nothing".
    if (aObject == this) {
        switch (aEvent->type()) {
        case QEvent::MouseButtonPress: {
            auto* m = static_cast<QMouseEvent*>(aEvent);
            dockSpikeLog(this, QStringLiteral("dock press pos=%1,%2 button=0x%3 floating=%4")
                    .arg(int(m->position().x())).arg(int(m->position().y()))
                    .arg(int(m->button()), 0, 16).arg(isFloating()));
            // A grouped panel draws no header - its title is the group's tab -
            // which leaves the strip where the header was as bare dock surface.
            // A press there reaches the QDockWidget, and that is what starts
            // Qt's own dock drag, which floats the panel: measured, pressing
            // there undocked a tabbed panel mid-click. The strip belongs to the
            // tab now, so it swallows the press instead.
            QWidget* header = titleBarWidget();
            if (m->button() == Qt::LeftButton && header && !header->isVisible()) {
                dockSpikeLog(this, QStringLiteral("grouped header strip press -> swallowed "
                                                  "(the tab is the handle)"));
                return true;
            }
            break;
        }
        case QEvent::MouseMove: {
            auto* m = static_cast<QMouseEvent*>(aEvent);
            if ((m->buttons() & Qt::LeftButton) && dockSpikeLogEnabled())
                dockSpikeLog(this, QStringLiteral("dock drag-move pos=%1,%2")
                        .arg(int(m->position().x())).arg(int(m->position().y())));
            break;
        }
        case QEvent::MouseButtonRelease:
            dockSpikeLog(this, QStringLiteral("dock release"));
            break;
        default:
            break;
        }
    }
    // no gesture is claimed here: the ring bands own the edges, and a header
    // press belongs to Qt's dock drag
    return false;
}

void DockPanel::onTopLevelChanged(bool aFloating) {
    if (!aFloating) {
        // Qt can dock a floating panel on its own (a drop, or its own
        // double-click), so the opaque state is restored here too
        setAttribute(Qt::WA_TranslucentBackground, false);
        setAttribute(Qt::WA_NoSystemBackground, false);
    }

    // The floating look is the theme's business, so the ring is a QSS rule
    // selected by this property (see standard.ssa); QSS property selectors are
    // only re-evaluated on a repolish.
    setProperty("aeFloating", aFloating);
    style()->unpolish(this);
    style()->polish(this);

    // Qt installs its resize handler when the dock floats - after this object's
    // constructor - and event filters run in reverse order of installation.
    // Re-installing puts this filter back in front, so a ring press is decided
    // by edgesAt() instead of by Qt.
    installEventFilter(this);

    layoutRing();
    update();
    if (auto* header = titleBarWidget()) header->update();

    if (dockSpikeLogEnabled()) {
        const QWindow* window = windowHandle();
        const QMargins margins = window ? window->frameMargins() : QMargins(-1, -1, -1, -1);
        const theme::Colors c = theme::Colors::current();
        dockSpikeLog(this, QStringLiteral("topLevelChanged floating=%1 alphaBuffer=%2 frameWidth=%3 flags=0x%4 "
                                          "frameMargins=%5,%6,%7,%8 geometry=%9,%10 %11x%12 "
                                          "tokens base=%13 outline=%14 hairline=%15 floaterEdge=%16")
                .arg(aFloating)
                .arg(window ? window->format().alphaBufferSize() : -1)
                .arg(style()->pixelMetric(QStyle::PM_DockWidgetFrameWidth, nullptr, this))
                .arg(window ? int(window->flags()) : 0, 0, 16)
                .arg(margins.left()).arg(margins.top()).arg(margins.right()).arg(margins.bottom())
                .arg(geometry().x()).arg(geometry().y()).arg(width()).arg(height())
                .arg(c.base.name(), c.outline.name(), c.hairline.name(), c.floaterEdge.name()));
    }
}

} // namespace gui
