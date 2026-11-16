#include "gui/DockSash.h"
#include "gui/DockMetrics.h"
#include "gui/DockSlots.h"
#include "gui/DockTabBar.h"
#include "gui/theme/Colors.h"

#include <QDockWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <dwmapi.h>
#endif


namespace gui {

namespace {
constexpr int kHitBoxSize = 12; // invisible grab area (VS Code style)
/// Two panes count as adjacent when their edges are within this many pixels.
/// Qt leaves no gap between them, but the central widget and an area do not
/// always meet exactly (the corner rules move the seam by a pixel).
constexpr int kAdjacentSlack = 2;

/// One resizable seam: the line, what it resizes, and which way.
struct Boundary {
    /// The slot's panels: a collapse hides them all - a slot collapses, not the
    /// tab that happened to be active in it.
    QVector<QDockWidget*> subjects;
    /// The panel the resize acts through, which has to be one that is *on screen*.
    /// A group keeps its inactive tabs parked off-screen, still reporting a size,
    /// and resizeDocks on one of those does nothing at all (measured: with the
    /// animation panel tabbed onto the properties one, the seam led with the
    /// parked properties dock and a 120px drag left it at 340 - the size range
    /// applied, the resize itself silently dropped).
    QDockWidget* lead = nullptr;
    QDockWidget* partner = nullptr;  ///< what absorbs it, null for the central widget
    Qt::Orientation axis = Qt::Horizontal; ///< Horizontal: the drag changes widths
    int sign = 1;                    ///< +1 if the subject grows with the coordinate
    int line = 0;                    ///< the seam, in window coordinates
    int spanStart = 0;               ///< the seam's extent along the other axis
    int spanEnd = 0;
};
} // namespace

// --------------------------------------------------------------------------
// One seam's grab area: an invisible box over the boundary, drawing only the
// 1px hairline, driving the resize through QMainWindow::resizeDocks.
//
// Two shapes, for two kinds of seam. Against the central widget the box lies
// entirely on the central widget's side: straddling it would cover part of the
// panel's own edge, and for a bottom-docked panel that edge is its header - so
// the drag started from a sash and the right-click escaped past it to the
// window's default popup (both were real bugs). Between two panels there is no
// such side to prefer, so the box is centred on the seam.
class DockSash::HitBox: public QWidget {
public:
    HitBox(DockSash* aSash, QMainWindow* aWindow):
        QWidget(nullptr),
        mSash(aSash),
        mWindow(aWindow) {
        setParent(aWindow);
        // paint only the hairline: with a universal `QWidget { background }`
        // rule in the QSS, the style sheet engine fills even a paintEvent-less
        // widget unless it is marked translucent
        setMouseTracking(true);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
        setWindowFlags(Qt::FramelessWindowHint);
        setObjectName("DockSash");
        hide();
    }

    void setBoundary(const Boundary& aBoundary, bool aCentered) {
        mBoundary = aBoundary;
        mCentered = aCentered;
        mCollapsed = false;

        setCursor(aBoundary.axis == Qt::Horizontal ? Qt::SplitHCursor : Qt::SplitVCursor);

        const int span = aBoundary.spanEnd - aBoundary.spanStart + 1;
        // One-sided boxes lie on the canvas's side of the seam, which is the
        // direction the subject grows in; centred boxes straddle it.
        const int boxStart = aCentered ? aBoundary.line - kHitBoxSize / 2
                                       : (aBoundary.sign > 0 ? aBoundary.line
                                                             : aBoundary.line - kHitBoxSize + 1);
        if (aBoundary.axis == Qt::Horizontal) {
            setGeometry(boxStart, aBoundary.spanStart, kHitBoxSize, span);
        } else {
            setGeometry(aBoundary.spanStart, boxStart, span, kHitBoxSize);
        }

#ifdef Q_OS_WIN
        // Fuck this, fuck the windows DWM, fuck the windows API, fuck everything.
        SetWindowRgn(reinterpret_cast<HWND>(this->winId()), QRegion(lineRect()).toHRGN(), TRUE);
#endif
        raise();
        show();
    }

    /// A hitbox stays put (and stays alive) while its panel is collapsed
    /// mid-drag: hiding it would drop the implicit mouse grab this drag depends
    /// on, so the panel could never be dragged back out in the same gesture.
    bool isDragging() const { return mDragging; }

    /// Whether this hitbox already holds the seam that resizes this subject,
    /// against this partner, along this axis. Identity is *what it resizes*, not
    /// where the line currently is: the line moves while the seam is dragged, and
    /// a hitbox that keeps the drag's mouse grab and baseline has to keep its
    /// identity while its geometry follows.
    bool holds(const QVector<QDockWidget*>& aSubjects, QDockWidget* aPartner,
               Qt::Orientation aAxis) const {
        return mBoundary.subjects == aSubjects && mBoundary.partner == aPartner
                && mBoundary.axis == aAxis;
    }
    QDockWidget* lead() const {
        return mBoundary.lead ? mBoundary.lead : mBoundary.subjects.value(0);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        // Nothing at all while the slot is collapsed: the panel it was the edge of
        // is gone, so a line there is a line on nothing - and it is the one the
        // user just dragged away, so it reads as the seam having refused to go. The
        // hitbox itself stays alive and still tracks the pointer, because the same
        // gesture has to be able to drag the slot back out (see isDragging).
        if (mCollapsed) return;

        // draw only the 1px hairline; never call the base paintEvent, so the
        // universal QWidget QSS background cannot fill the hitbox
        const theme::Colors c = theme::Colors::current();
        QPainter p(this);
        p.fillRect(lineRect(), mDragging ? c.hover : (mHovered ? c.hairlineHover : c.hairline));
    }

    // the hover feedback is a repaint; paintEvent itself decides whether there is
    // anything to draw (a collapsed slot draws nothing)
    void enterEvent(QEnterEvent*) override {
        mHovered = true;
        update();
    }

    void leaveEvent(QEvent*) override {
        mHovered = false;
        update();
    }

    void mousePressEvent(QMouseEvent* aEvent) override {
        if (aEvent->button() != Qt::LeftButton || mBoundary.subjects.isEmpty())
            return;
        mDragging = true;
        // the widget keeps an implicit mouse grab until release, so the drag
        // survives the cursor leaving the hitbox
        const QPointF global = aEvent->globalPosition();
        mPressPos = (mBoundary.axis == Qt::Horizontal) ? global.x() : global.y();
        // The baseline is this moment's sizes, and it has to be captured here
        // rather than refreshed from the relayout: every resize triggers a
        // relayout, so a moving baseline made each move re-apply its whole delta
        // (measured: a 76px drag grew the panel by 181).
        mStartSize = (mBoundary.axis == Qt::Horizontal) ? lead()->width() : lead()->height();
        mPartnerStart = (mBoundary.partner && mBoundary.partner->isVisible())
                ? ((mBoundary.axis == Qt::Horizontal) ? mBoundary.partner->width()
                                                      : mBoundary.partner->height())
                : 0;
        // After the baseline is captured: the receiver drops the opening
        // minimums, and that may re-lay-out the dock; the drag must still start
        // from the size the user saw and grabbed.
        if (mSash) emit mSash->resizeStarted();
        dockSpikeLog(lead(),
                QStringLiteral("sash press slot=%1 partner=%2 axis=%3 line=%4 span=%5..%6 "
                               "start=%7 partnerStart=%8 centered=%9")
                .arg(lead()->objectName())
                .arg(mBoundary.partner ? mBoundary.partner->objectName() : QStringLiteral("central"))
                .arg(mBoundary.axis == Qt::Horizontal ? QStringLiteral("widths") : QStringLiteral("heights"))
                .arg(mBoundary.line).arg(mBoundary.spanStart).arg(mBoundary.spanEnd)
                .arg(mStartSize).arg(mPartnerStart).arg(mCentered));
        update();
    }

    void mouseMoveEvent(QMouseEvent* aEvent) override {
        if (!mDragging || mBoundary.subjects.isEmpty() || !lead())
            return;
        const QPointF global = aEvent->globalPosition();
        const int pos = (mBoundary.axis == Qt::Horizontal) ? global.x() : global.y();
        const int newSize = mStartSize + mBoundary.sign * (pos - mPressPos);

        // The collapse point is half the minimum, not the minimum: stopping at
        // the minimum would make the first few pixels of "too small" hide the
        // panel, so a panel could only ever be dragged to exactly its minimum
        // and never a hair less. Dragging back out asks just as unambiguously
        // for it back, so the gesture stays reversible while the button is down.
        const int collapseSize = (mBoundary.axis == Qt::Horizontal) ? dockMetrics::kCollapseWidth
                                                                    : dockMetrics::kCollapseHeight;
        if (newSize < collapseSize) {
            if (!mCollapsed) {
                dockSpikeLog(lead(),
                        QStringLiteral("sash drag requested=%1 < collapse point %2 -> collapsed slot (%3 panels)")
                        .arg(newSize).arg(collapseSize).arg(mBoundary.subjects.size()));
                for (QDockWidget* panel : mBoundary.subjects) panel->hide();
                mCollapsed = true;
            }
            return;
        }
        if (mCollapsed) {
            dockSpikeLog(lead(),
                    QStringLiteral("sash drag requested=%1 >= collapse point %2 -> restored slot (%3 panels)")
                    .arg(newSize).arg(collapseSize).arg(mBoundary.subjects.size()));
            for (QDockWidget* panel : mBoundary.subjects) panel->show();
            mCollapsed = false;
        }

        // A seam between two panels keeps their total: the partner takes what
        // the subject gives up, which is what makes the seam follow the cursor
        // instead of only one side of it moving.
        if (mBoundary.partner && mBoundary.partner->isVisible() && mPartnerStart > 0) {
            const int total = mStartSize + mPartnerStart;
            mWindow->resizeDocks({lead(), mBoundary.partner},
                                 {newSize, qMax(1, total - newSize)}, mBoundary.axis);
        } else {
            mWindow->resizeDocks({lead()}, {newSize}, mBoundary.axis);
        }
    }

    void mouseReleaseEvent(QMouseEvent* aEvent) override {
        if (aEvent->button() == Qt::LeftButton) {
            mDragging = false;
            mCollapsed = false;
            dockSpikeLog(lead(),
                    QStringLiteral("sash release slot=%1 now=%2 partner=%3 now=%4")
                    .arg(lead() ? lead()->objectName() : QStringLiteral("-"))
                    .arg(lead() ? (mBoundary.axis == Qt::Horizontal ? lead()->width()
                                                                    : lead()->height()) : -1)
                    .arg(mBoundary.partner ? mBoundary.partner->objectName() : QStringLiteral("central"))
                    .arg(mBoundary.partner
                            ? (mBoundary.axis == Qt::Horizontal ? mBoundary.partner->width()
                                                                : mBoundary.partner->height())
                            : -1));
            // A collapsed dock is no longer a target, so its sash has to go -
            // and it cannot go during the drag (see isDragging), which is why
            // this is the moment, not the collapse itself. Without it the last
            // hitbox stayed drawn at the boundary of a panel that is not there.
            if (mSash) mSash->scheduleRelayout();
            update();
        }
    }

private:
    /// The 1px line the box paints, always on the seam itself: a one-sided box
    /// puts it on the edge facing the panel, a centred one in the middle.
    QRect lineRect() const {
        int lineSize = 1;
#ifdef Q_OS_WIN
        lineSize = 2;
#endif
        // A one-sided box lies on the canvas's side of the seam, so the line is
        // at the edge that touches the panel: the box's start when the subject
        // grows towards positive coordinates, its end when it grows the other
        // way (the bottom area's seam, for instance, has the canvas above it).
        if (mBoundary.axis == Qt::Horizontal) {
            const int x = mCentered ? width() / 2
                                    : (mBoundary.sign > 0 ? 0 : width() - lineSize);
            return QRect(x, 0, lineSize, height());
        }
        const int y = mCentered ? height() / 2 : (mBoundary.sign > 0 ? 0 : height() - lineSize);
        return QRect(0, y, width(), lineSize);
    }

    DockSash* mSash;
    QMainWindow* mWindow;
    Boundary mBoundary;
    bool mCentered = false;
    bool mHovered = false;
    bool mDragging = false;
    bool mCollapsed = false;
    int mPressPos = 0;
    int mStartSize = 0;
    int mPartnerStart = 0;
};

// --------------------------------------------------------------------------
DockSash::DockSash(QMainWindow* aWindow): QObject(aWindow), mWindow(aWindow) {
    mWindow->installEventFilter(this);
    watchDocks();
    scheduleRelayout();
}

bool DockSash::eventFilter(QObject*, QEvent* aEvent) {
    // never walk the widget tree synchronously here: during window teardown a
    // dying dock emits ChildRemoved mid-destruction, and touching the child
    // list then is use-after-free. Everything goes through the deferred,
    // context-bound relayout.
    switch (aEvent->type()) {
    case QEvent::Resize:
    case QEvent::LayoutRequest:
    case QEvent::Move:
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::ChildAdded:
    case QEvent::ChildRemoved:
        scheduleRelayout();
        break;
    default:
        break;
    }
    return false;
}

void DockSash::watchDocks() {
    // track every dock's geometry/visibility changes through the same filter
    for (auto* dock : mWindow->findChildren<QDockWidget*>())
        dock->installEventFilter(this);
}

void DockSash::scheduleRelayout() {
    // coalesce bursts of layout/move events into one relayout per event loop
    // turn
    QTimer::singleShot(0, this, [this]() { relayout(); });
}

void DockSash::relayout() {
    watchDocks();

    // Seams come from the slot model, which is the one place that knows what an
    // area is. Doing it from docks here meant guessing at three things at once -
    // which docks are on screen (an inactive tab is parked off-screen but still
    // reports a size), how far a slot reaches (its rail belongs to the slot, not
    // to any panel in it), and what a collapse should hide (the slot, not
    // whichever tab was active). Measured consequences of each: an area covering
    // the whole window, a seam that stopped at the tabs, and a collapse that left
    // the slot's other tabs on screen.
    QVector<Boundary> boundaries;
    const QVector<DockSlot> areas = dockSlots(*mWindow);
    for (const DockSlot& slot : areas) {
        if (slot.onScreen.isEmpty()) continue;

        Boundary boundary;
        boundary.subjects = slot.panels;
        boundary.lead = slot.onScreen.first();
        boundary.partner = nullptr;
        switch (slot.area) {
        case Qt::LeftDockWidgetArea:
            boundary.axis = Qt::Horizontal;
            boundary.sign = 1;
            boundary.line = slot.rect.right() + 1;
            boundary.spanStart = slot.rect.top();
            boundary.spanEnd = slot.rect.bottom();
            break;
        case Qt::RightDockWidgetArea:
            boundary.axis = Qt::Horizontal;
            boundary.sign = -1;
            boundary.line = slot.rect.left();
            boundary.spanStart = slot.rect.top();
            boundary.spanEnd = slot.rect.bottom();
            break;
        case Qt::TopDockWidgetArea:
            boundary.axis = Qt::Vertical;
            boundary.sign = 1;
            boundary.line = slot.rect.bottom() + 1;
            boundary.spanStart = slot.rect.left();
            boundary.spanEnd = slot.rect.right();
            break;
        default:
            boundary.axis = Qt::Vertical;
            boundary.sign = -1;
            boundary.line = slot.rect.top();
            boundary.spanStart = slot.rect.left();
            boundary.spanEnd = slot.rect.right();
            break;
        }
        boundaries.push_back(boundary);

        // Panels sharing a slot can also be arranged against each other, which is
        // the one case where two docks move together - and the only case
        // QMainWindow::resizeDocks supports (docks nested on the same level).
        for (int i = 0; i < slot.onScreen.size(); ++i) {
            for (int j = 0; j < slot.onScreen.size(); ++j) {
                if (i == j) continue;
                const QRect a = slot.onScreen[i]->geometry();
                const QRect b = slot.onScreen[j]->geometry();
                const int vTop = qMax(a.top(), b.top());
                const int vBottom = qMin(a.bottom(), b.bottom());
                const int hLeft = qMax(a.left(), b.left());
                const int hRight = qMin(a.right(), b.right());

                Boundary between;
                between.subjects = {slot.onScreen[i]};
                between.lead = slot.onScreen[i];
                between.partner = slot.onScreen[j];
                bool found = false;
                if (vBottom > vTop) {
                    if (qAbs(b.right() + 1 - a.left()) <= kAdjacentSlack) {
                        between.axis = Qt::Horizontal; between.sign = -1;
                        between.line = a.left();
                        between.spanStart = vTop; between.spanEnd = vBottom;
                        found = true;
                    } else if (qAbs(b.left() - 1 - a.right()) <= kAdjacentSlack) {
                        between.axis = Qt::Horizontal; between.sign = 1;
                        between.line = a.right() + 1;
                        between.spanStart = vTop; between.spanEnd = vBottom;
                        found = true;
                    }
                }
                if (!found && hRight > hLeft) {
                    if (qAbs(b.bottom() + 1 - a.top()) <= kAdjacentSlack) {
                        between.axis = Qt::Vertical; between.sign = -1;
                        between.line = a.top();
                        between.spanStart = hLeft; between.spanEnd = hRight;
                        found = true;
                    } else if (qAbs(b.top() - 1 - a.bottom()) <= kAdjacentSlack) {
                        between.axis = Qt::Vertical; between.sign = 1;
                        between.line = a.bottom() + 1;
                        between.spanStart = hLeft; between.spanEnd = hRight;
                        found = true;
                    }
                }
                if (found) boundaries.push_back(between);
            }
        }
    }

    // Each seam goes back to the hitbox that already holds it, matched by what it
    // resizes rather than by its place in a list. That is what keeps a drag
    // coherent - the hitbox keeps the mouse grab and the baseline sizes taken at
    // the press - while letting its geometry follow the edge as the layout moves,
    // which freezing it in place did not: measured, the separator's highlight
    // stayed where it was while the area's edge moved away from it.
    QVector<bool> taken(mHitBoxes.size(), false);
    for (const Boundary& boundary : boundaries) {
        int index = -1;
        for (int i = 0; i < mHitBoxes.size(); ++i) {
            if (!taken[i] && mHitBoxes[i]->holds(boundary.subjects, boundary.partner, boundary.axis)) {
                index = i;
                break;
            }
        }
        if (index < 0) {
            if (mHitBoxes.size() >= kMaxHitBoxes) break;
            mHitBoxes.push_back(new HitBox(this, mWindow));
            taken.push_back(false);
            index = mHitBoxes.size() - 1;
        }
        taken[index] = true;
        mHitBoxes[index]->setBoundary(boundary, boundary.partner != nullptr);
    }
    for (int i = 0; i < mHitBoxes.size(); ++i) {
        if (taken[i]) continue;
        // a hitbox whose slot was just collapsed stays where it is for the rest
        // of the drag, so the slot can be dragged back out (see isDragging)
        if (!mHitBoxes[i]->isDragging()) mHitBoxes[i]->hide();
    }

    // Temporary: what the seam pass actually produced - the geometry a press has
    // to land in, and what each seam resizes. "Handles are missing" is a claim
    // about this list, so the list has to be readable instead of inferred from
    // pressing at guessed coordinates.
    if (qEnvironmentVariableIsSet("AE_DOCK_SASH_PROBE")) {
        dockSpikeLog(nullptr, QStringLiteral("sash: %1 boundaries -> %2 hitboxes")
                .arg(boundaries.size()).arg(mHitBoxes.size()));
        for (int i = 0; i < mHitBoxes.size(); ++i) {
            const QRect g = mHitBoxes[i]->geometry();
            const QDockWidget* lead = mHitBoxes[i]->lead();
            dockSpikeLog(lead, QStringLiteral("sash: #%1 %2 lead=%3 size=%4x%5 box=%6,%7 %8x%9")
                    .arg(i).arg(taken[i] ? QStringLiteral("shown") : QStringLiteral("unused"))
                    .arg(lead ? lead->objectName() : QStringLiteral("-"))
                    .arg(lead ? lead->width() : -1).arg(lead ? lead->height() : -1)
                    .arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height()));
        }
    }
}

} // namespace gui
