#include "gui/DockTabBar.h"

#include "gui/DockDropOverlay.h"
#include "gui/DockPanel.h"
#include "gui/theme/Colors.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDrag>
#include <QFile>
#include <QGuiApplication>
#include <QMainWindow>
#include <QMimeData>
#include <QMouseEvent>
#include <QTime>
#include <QWindow>
#include <cstdio>

namespace gui {

bool dockSpikeLogEnabled() {
    // One switch for the whole temporary harness. The call sites sit on gestures
    // and on startup, so without this a normal run writes to stderr and appends to
    // the file on every drag the user makes; with it everything is quiet until a
    // probe asks for it, and AE_DOCK_SPIKE_LOG still names the file to read the
    // behaviour back from.
    //
    // Exposed so a hot path (a mouse move) can skip BUILDING its message too: an
    // argument is evaluated before the call, so an early return inside this
    // function alone would still pay for the QString on every event.
    static const bool enabled = qEnvironmentVariableIsSet("AE_DOCK_SPIKE_LOG")
                             || qEnvironmentVariableIsSet("AE_DOCK_LOG");
    return enabled;
}

void dockSpikeLog(const QDockWidget* aDock, const QString& aMessage) {
    if (!dockSpikeLogEnabled()) return;

    static const QString path = qEnvironmentVariable(
            "AE_DOCK_SPIKE_LOG", QStringLiteral("/tmp/ae-dock-spike.log"));
    const QString line = QStringLiteral("%1 [%2] %3")
            .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")),
                 aDock ? aDock->objectName() : QStringLiteral("-"), aMessage);
    std::fprintf(stderr, "dock-spike: %s\n", qPrintable(line));
    std::fflush(stderr);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write(line.toUtf8());
        file.write("\n");
    }
}

DockTabBar::DockTabBar(QDockWidget* aDock):
    QTabBar(aDock) {
    // Named so the theme dresses this row and a group's Qt bar with one rule.
    setObjectName(QStringLiteral("dockTabBar"));
    setDrawBase(false);
    setExpanding(false);
    setMovable(false);
    setFocusPolicy(Qt::NoFocus);
    setUsesScrollButtons(false);
    if (aDock) {
        addTab(aDock->windowTitle());
        connect(aDock, &QDockWidget::windowTitleChanged, this, [this](const QString& aTitle) {
            if (count() > 0) setTabText(0, aTitle);
        });
        dockSpikeLog(aDock, QStringLiteral("title tab bar installed: platform=%1 qt=%2")
                .arg(QGuiApplication::platformName(), QString::fromLatin1(qVersion())));
    }
}

namespace {
/// Whether this row's panel shares its slot, in which case the slot's rail is the
/// title UI and this row has no business taking space.
bool rowIsRedundant(QDockWidget* aDock) {
    if (!aDock || aDock->isFloating()) return false;
    auto* panel = qobject_cast<DockPanel*>(aDock);
    return panel && panel->isTabGrouped();
}
} // namespace

QSize DockTabBar::sizeHint() const {
    if (rowIsRedundant(qobject_cast<QDockWidget*>(parentWidget()))) return QSize(0, 0);
    return QTabBar::sizeHint();
}

QSize DockTabBar::minimumSizeHint() const {
    if (rowIsRedundant(qobject_cast<QDockWidget*>(parentWidget()))) return QSize(0, 0);
    return QTabBar::minimumSizeHint();
}

DockTabBarFilter::DockTabBarFilter(QTabBar* aBar, QObject* aParent):
    QObject(aParent),
    mBar(aBar) {
    if (mBar) mBar->installEventFilter(this);
}

QDockWidget* DockTabBarFilter::dockForTab(int aIndex) const {
    // A panel's own title row lives inside its dock; a group's bar is the main
    // window's, and its tabs are named after the panels they hold.
    if (auto* dock = qobject_cast<QDockWidget*>(mBar->parentWidget())) return dock;
    if (auto* window = qobject_cast<QMainWindow*>(mBar->window())) {
        const QString title = aIndex >= 0 ? mBar->tabText(aIndex) : QString();
        for (QDockWidget* dock : window->findChildren<QDockWidget*>()) {
            if (!dock->property("aeDockPreview").toBool() && dock->windowTitle() == title)
                return dock;
        }
    }
    return nullptr;
}

bool DockTabBarFilter::isTitleRow() const {
    return mBar && qobject_cast<QDockWidget*>(mBar->parentWidget()) != nullptr;
}

QRect DockTabBarFilter::railRect() const {
    // The rail is the bar itself, plus a couple of pixels of slack so that
    // nudging a tab a hair past the row's edge does not start docking.
    return mBar->rect().adjusted(-2, -2, 2, 2);
}

int DockTabBarFilter::pressedTabIndex() const {
    // The pressed tab by name, not by the index it had at press: Qt's movable-tab
    // drag reorders the bar while the pointer is inside the rail, so the recorded
    // index can be another tab's by the time it is used.
    if (!mBar || !mPressed) return mTabIndex;
    for (int i = 0; i < mBar->count(); ++i) {
        if (mBar->tabText(i) == mPressed->windowTitle()) return i;
    }
    return mTabIndex;
}

DockDragImage DockTabBarFilter::dragImageFor(const QPoint& aPos) const {
    DockDragImage image;
    if (!mBar || mBar->count() == 0) return image;

    // The tab under the press; for a row that is one panel's title the row has a
    // single tab, and that is what the user sees themselves grabbing even when
    // they pressed the empty part of the row.
    const int found = pressedTabIndex();
    const int index = (found >= 0 && found < mBar->count()) ? found : 0;
    const QRect tab = mBar->tabRect(index);
    if (tab.isEmpty()) return image;

    image.pixmap = mBar->grab(tab);
    // where inside the tab the pointer is, so the image does not jump under the
    // cursor when it takes over from Qt's copy
    image.hotSpot = aPos - tab.topLeft();
    if (!tab.contains(aPos)) {
        // pressed past the last tab: aim at the tab's middle instead of outside it
        image.hotSpot = QPoint(tab.width() / 2, qMin(aPos.y(), tab.height() - 1));
    }
    return image;
}

void DockTabBarFilter::liftTab(int aIndex) {
    // Only a rail with other tabs to close the gap: a single-tab row *is* that
    // panel's title, and the panel is still there while it is being dragged, so
    // emptying it would leave the panel with no title instead of reading as "this
    // tab has left the strip".
    if (!mBar || mBar->count() < 2 || aIndex < 0 || aIndex >= mBar->count()) {
        log(QStringLiteral("tab not lifted: bar=%1 count=%2 index=%3 pressed=%4")
                .arg(mBar ? QStringLiteral("ok") : QStringLiteral("gone"))
                .arg(mBar ? mBar->count() : -1).arg(aIndex)
                .arg(mPressed ? mPressed->objectName() : QStringLiteral("-")));
        return;
    }
    mLiftedBar = mBar;
    mLiftedTab = mBar->tabText(aIndex);
    mBar->setTabVisible(aIndex, false);
    log(QStringLiteral("tab lifted out of the rail: %1").arg(mLiftedTab));
}

void DockTabBarFilter::restoreTab() {
    if (!mLiftedBar || mLiftedTab.isEmpty()) return;
    for (int i = 0; i < mLiftedBar->count(); ++i) {
        if (mLiftedBar->tabText(i) == mLiftedTab) {
            mLiftedBar->setTabVisible(i, true);
            log(QStringLiteral("tab put back in the rail: %1").arg(mLiftedTab));
            break;
        }
    }
    mLiftedBar = nullptr;
    mLiftedTab.clear();
}

void DockTabBarFilter::endRailDrag() {
    if (!mBar || !mBar->isMovable()) return;

    // Qt's own way of ending a tab move, rather than reaching into its state: it
    // animates the tab home, hides the floating copy it made for the drag and
    // drops dragInProgress/dragStartPosition. Sent at the pressed tab, so the
    // release cannot land on a different tab and activate it.
    const int found = pressedTabIndex();
    const int index = (found >= 0 && found < mBar->count()) ? found : mBar->currentIndex();
    if (index < 0 || index >= mBar->count()) return;
    const QPointF where(mBar->tabRect(index).center());
    QMouseEvent release(QEvent::MouseButtonRelease, where, mBar->mapToGlobal(where.toPoint()),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(mBar, &release);
    log(QStringLiteral("rail drag ended (Qt's tab move released)"));
}

bool DockTabBarFilter::eventFilter(QObject* aObject, QEvent* aEvent) {
    if (!mBar || aObject != mBar) return QObject::eventFilter(aObject, aEvent);

    switch (aEvent->type()) {
    case QEvent::MouseButtonPress: {
        auto* e = static_cast<QMouseEvent*>(aEvent);
        if (e->button() != Qt::LeftButton) break;
        mPressPos = e->position().toPoint();
        mMoving = false;
        mRailDrag = false;
        mDockDragArmed = false;
        mPressed = nullptr;

        const int index = mBar->tabAt(mPressPos);
        QDockWidget* dock = dockForTab(index);
        if (index >= 0 && dock) {
            // A press on a tab is a rail drag first: inside the rail it moves the
            // tab, and docking starts only when the pointer leaves the rail (see
            // MouseMove). Starting the dock drag on the first few pixels - which
            // is what this did - left no way to reorder a tab at all.
            mPressed = dock;
            mRailDrag = true;
            mTabIndex = index;
            log(QStringLiteral("tab press pos=%1,%2 tab=%3 (%4) floating=%5 -> rail drag")
                    .arg(mPressPos.x()).arg(mPressPos.y()).arg(index).arg(dock->objectName())
                    .arg(dock->isFloating()));
        } else if (dock && !dock->isFloating() && isTitleRow()) {
            // The rest of a lone panel's title row, which is all of it but the tab
            // text: one tab reads as the title, so the row is the panel's handle
            // and not a tab strip with dead space in it. Measured before this: the
            // only interactive part was tabRect(0), local x 8..78, so 65% of the
            // tools title row and 93% of the animation one did nothing on a drag -
            // which is "dragging the tabs does not initiate docking any more".
            mPressed = dock;
            mDockDragArmed = true;
            log(QStringLiteral("title row press pos=%1,%2 -> dock drag armed")
                    .arg(mPressPos.x()).arg(mPressPos.y()));
        } else if (dock && dock->isFloating()) {
            // The rail's empty space past the last tab is window chrome: it moves
            // the window (see MouseMove) instead of starting a docking drag, which
            // is what makes a floating panel readable as a window.
            log(QStringLiteral("rail press pos=%1,%2 -> window move").arg(mPressPos.x()).arg(mPressPos.y()));
        }
        break;
    }
    case QEvent::MouseMove: {
        auto* e = static_cast<QMouseEvent*>(aEvent);
        if (!(e->buttons() & Qt::LeftButton)) break;
        const QPoint pos = e->position().toPoint();
        if ((pos - mPressPos).manhattanLength() < QApplication::startDragDistance()) break;

        if (mRailDrag && mPressed) {
            if (railRect().contains(pos)) {
                // Still in the rail, so this is a reorder rather than a docking
                // gesture - and it is the bar's own reorder: Qt's dock tab bar is
                // movable, so QTabBar slides the neighbours aside, keeps a floating
                // copy of the grabbed tab under the cursor and emits tabMoved,
                // which QMainWindowLayout turns into the dock order. None of that
                // is reimplemented here, and it must not be: moving tabs here as
                // well was what made the reorder janky. The bar paints the pressed
                // tab displaced by its own dragOffset, so a hand-made moveTab left
                // the tab visibly in the wrong place until the next move recomputed
                // the offset - the teleport that corrected itself a move later.
                // Leaving the event alone is the whole gesture.
                if (qEnvironmentVariableIsSet("AE_DOCK_RAIL_PROBE")) {
                    QStringList rects;
                    for (int i = 0; i < mBar->count(); ++i) {
                        const QRect r = mBar->tabRect(i);
                        rects << QStringLiteral("%1@%2,%3+%4").arg(mBar->tabText(i))
                                     .arg(r.x()).arg(r.y()).arg(r.width());
                    }
                    log(QStringLiteral("rail move pos=%1 tabAt=%2 index=%3 movable=%4 [%5]")
                            .arg(pos.x()).arg(mBar->tabAt(pos)).arg(mTabIndex)
                            .arg(mBar->isMovable())
                            .arg(rects.join(QStringLiteral(", "))));
                }
                return false;
            }
            // Left the rail: docking takes over, and it is the same drag a floating
            // panel's title starts. Two things have to happen in this order - Qt's
            // in-flight tab move is ended the way Qt ends one (a release, which
            // hides its floating copy and drops the drag offsets), and the docking
            // drag is given the grabbed *tab* as its image, so the pointer keeps
            // carrying the tab it picked up instead of the copy disappearing at the
            // rail's edge.
            QDockWidget* dock = mPressed;
            // Read while this gesture's state is still intact: endRailDrag below
            // sends the release that clears it (which is also what ends Qt's move).
            const int tabIndex = pressedTabIndex();
            // the grab point, not where the pointer is now: the image has to stay
            // under the finger, and a hotspot taken from the leaving point makes
            // the ghost trail further off the further the drag goes
            const DockDragImage image = dragImageFor(mPressPos);
            mPressed = nullptr;
            mRailDrag = false;
            log(QStringLiteral("left the rail -> dock drag"));
            endRailDrag();
            // the tab leaves the strip while it is out, so the gesture reads as
            // carrying that tab rather than as a copy of it appearing elsewhere
            liftTab(tabIndex);
            beginDockPanelDrag(dock, mBar, image);
            restoreTab();
            return true;
        }

        QDockWidget* dock = dockForTab(mBar->tabAt(mPressPos));
        if (!dock) break;
        if (mDockDragArmed && !mDragging) {
            mDockDragArmed = false;
            mDragging = true;
            // the tab the row shows, so the ghost is the part of the row the user
            // has their pointer on rather than a scaled panel snapshot
            const DockDragImage image = dragImageFor(mPressPos);
            const int lifted = pressedTabIndex();
            QMetaObject::invokeMethod(this, [this, dock, image, lifted]() {
                liftTab(lifted);
                beginDockPanelDrag(dock, mBar, image);
                restoreTab();
                mDragging = false;
            }, Qt::QueuedConnection);
            return true;
        }
        if (dock->isFloating() && !mMoving) {
            QWindow* window = dock->windowHandle();
            mMoving = window && window->startSystemMove();
            log(QStringLiteral("title bar drag -> startSystemMove=%1").arg(mMoving));
            if (mMoving) return true;
        }
        break;
    }
    case QEvent::MouseButtonRelease:
        // A reorder Qt performed is already the layout's order (QMainWindowLayout
        // moves the dock when the bar emits tabMoved), so there is nothing to
        // commit here - and nothing to undo either. Read at the release, because a
        // reorder does not necessarily produce a layout pass for anything else to
        // be read from.
        if (qEnvironmentVariableIsSet("AE_DOCK_RAIL_PROBE") && mBar) {
            QStringList order;
            for (int i = 0; i < mBar->count(); ++i) order << mBar->tabText(i);
            log(QStringLiteral("rail order at release: %1").arg(order.join(QStringLiteral(" | "))));
        }
        mDockDragArmed = false;
        mMoving = false;
        mRailDrag = false;
        mTabIndex = -1;
        mPressed = nullptr;
        break;
    case QEvent::MouseButtonDblClick: {
        auto* e = static_cast<QMouseEvent*>(aEvent);
        if (e->button() != Qt::LeftButton) break;
        if (QDockWidget* dock = dockForTab(mBar->tabAt(e->position().toPoint()))) {
            const bool floating = !dock->isFloating();
            log(QStringLiteral("double-click -> setFloating(%1)").arg(floating));
            if (auto* panel = qobject_cast<DockPanel*>(dock)) panel->setPanelFloating(floating);
            else dock->setFloating(floating);
            return true;
        }
        break;
    }
    case QEvent::ContextMenu: {
        auto* e = static_cast<QContextMenuEvent*>(aEvent);
        const int index = mBar->tabAt(e->pos());
        if (QDockWidget* dock = dockForTab(index >= 0 ? index : 0)) {
            showDockPanelMenu(dock, e->globalPos());
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QObject::eventFilter(aObject, aEvent);
}

void DockTabBarFilter::log(const QString& aMessage) const {
    // A drag can outlive the bar it started from (docking the dragged tab
    // elsewhere rebuilds the group), so the guarded pointer may already be null
    // by the time a release path logs.
    dockSpikeLog(mBar ? dockForTab(mBar->tabAt(mPressPos)) : nullptr, aMessage);
}

} // namespace gui
