#include "gui/DockSlots.h"
#include "gui/DockTabBar.h" // temporary: dockSpikeLog

#include <QMainWindow>
#include <QTabBar>
#include <algorithm>

namespace gui {

namespace {

/// Where an area's *rail* sits, recorded on the bar when it is adopted: Qt builds
/// the bar for a group of tabbed panels, so it belongs to the slot rather than to
/// any panel in it, and it sits outside those panels - which is why the slot's
/// extent has to include it.
///
/// Only Qt's own bar is a rail. A lone panel's title row is a DockTabBar, and it
/// is installed as that dock's title bar: it lives *inside* the dock (already
/// counted) and reports its geometry in the dock's coordinates, not the window's.
/// Treating the two as one widget made the slot rect a union of two coordinate
/// spaces - measured with one panel per area, the right slot's left edge came out
/// 0 (the rail's own local x) and the bottom slot's top edge 0. That is what put
/// the right and bottom seams at the window's top-left corner, off screen at
/// box=-11,0 and box=0,-11, so pressing near those edges did nothing.
QTabBar* railFor(const QMainWindow& aWindow, Qt::DockWidgetArea aArea) {
    for (QTabBar* bar : aWindow.findChildren<QTabBar*>()) {
        if (bar->objectName() != QLatin1String("dockTabBar")) continue;
        if (qobject_cast<DockTabBar*>(bar)) continue; // a title row, not a rail
        if (bar->property("aeDockArea").toInt() != int(aArea)) continue;
        if (bar->isVisible()) return bar;
    }
    return nullptr;
}

/// A widget's rect in the window's coordinates. Nothing here may use a bare
/// geometry(): a dock's parent happens to be the window, but a rail's parent is
/// whichever container Qt created for it, and those are not the same space.
QRect rectInWindow(const QWidget* aWidget, const QMainWindow& aWindow) {
    if (!aWidget || !aWindow.isAncestorOf(aWidget)) return QRect();
    return QRect(aWidget->mapTo(&aWindow, QPoint(0, 0)), aWidget->size());
}

bool occupiesScreen(const QWidget* aWidget, const QRect& aWindow) {
    if (!aWidget || !aWidget->isVisible()) return false;
    const QRect rect = aWidget->geometry();
    return !rect.isEmpty() && aWindow.intersects(rect);
}

} // namespace

QVector<DockSlot> dockSlots(const QMainWindow& aWindow) {
    static const Qt::DockWidgetArea kOrder[] = {
        Qt::LeftDockWidgetArea, Qt::RightDockWidgetArea,
        Qt::TopDockWidgetArea, Qt::BottomDockWidgetArea,
    };

    QMap<int, QVector<QDockWidget*>> byArea;
    for (auto* dock : aWindow.findChildren<QDockWidget*>()) {
        if (dock->objectName() == QLatin1String("aeDockPreview")) continue;
        const Qt::DockWidgetArea area = aWindow.dockWidgetArea(dock);
        if (area == Qt::NoDockWidgetArea || dock->isFloating()) continue;
        byArea[int(area)].push_back(dock);
    }

    const QRect windowRect = aWindow.rect();
    QVector<DockSlot> found; // not "slots": Qt defines that as a macro
    for (Qt::DockWidgetArea area : kOrder) {
        DockSlot slot;
        slot.area = area;
        const auto it = byArea.constFind(int(area));
        if (it != byArea.constEnd()) {
        slot.panels = it.value();
        // Creation order, which is the rail's order (see DockTabBar): the tab text
        // is matched by name there, and the order is fixed so a tab does not jump
        // to the front when it is activated.
        std::stable_sort(slot.panels.begin(), slot.panels.end(),
                         [](QDockWidget* a, QDockWidget* b) {
                             return a->property("aeDockOrder").toInt()
                                  < b->property("aeDockOrder").toInt();
                         });
        for (QDockWidget* panel : slot.panels) {
            if (occupiesScreen(panel, windowRect)) slot.onScreen.push_back(panel);
        }
        for (QDockWidget* panel : slot.onScreen) {
            const QRect rect = rectInWindow(panel, aWindow);
            slot.rect = slot.rect.isNull() ? rect : slot.rect.united(rect);
            if (qEnvironmentVariableIsSet("AE_DOCK_SLOTS_PROBE"))
                dockSpikeLog(panel, QStringLiteral("slots: area=%1 panel=%2 rect=%3,%4 %5x%6 parent=%7")
                        .arg(int(area)).arg(panel->objectName())
                        .arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height())
                        .arg(panel->parentWidget() ? panel->parentWidget()->objectName()
                                                   : QStringLiteral("(none)")));
        }
        if (QTabBar* rail = railFor(aWindow, area)) {
            const QRect rect = rectInWindow(rail, aWindow);
            slot.rect = slot.rect.isNull() ? rect : slot.rect.united(rect);
            if (qEnvironmentVariableIsSet("AE_DOCK_SLOTS_PROBE"))
                dockSpikeLog(nullptr, QStringLiteral("slots: area=%1 rail rect=%2,%3 %4x%5 parent=%6 visible=%7")
                        .arg(int(area)).arg(rect.x()).arg(rect.y())
                        .arg(rect.width()).arg(rect.height())
                        .arg(rail->parentWidget() ? rail->parentWidget()->objectName()
                                                  : QStringLiteral("(none)"))
                        .arg(rail->isVisible()));
        }
        }
        // Every area is a slot, empty ones included: it is still a place a panel
        // can be dropped. Its rect stays null, because an empty area has no
        // geometry of its own - dockAreaPlace works that out when one is needed.
        found.push_back(slot);
    }
    return found;
}

QRect dockAreaPlace(const QMainWindow& aWindow, const QVector<DockSlot>& aSlots,
                    const QSize& aPanelSize, Qt::DockWidgetArea aArea) {
    QWidget* central = aWindow.centralWidget();
    if (!central) return QRect();

    // The region the areas tile: the canvas united with everything the slots hold.
    // Measured against the canvas alone an area's slice stops wherever a panel
    // happens to end, and an area takes the window's own edge - the timeline across
    // the bottom of the window is the case that showed it: the preview was only as
    // tall as the canvas while the drop gave the column the window's full height.
    QRect dockRegion = central->geometry();
    QRect eachArea[int(Qt::BottomDockWidgetArea) + 1];
    for (const DockSlot& slot : aSlots) {
        if (slot.rect.isNull()) continue;
        eachArea[int(slot.area)] = eachArea[int(slot.area)].isNull()
                ? slot.rect : eachArea[int(slot.area)].united(slot.rect);
        dockRegion = dockRegion.united(slot.rect);
    }

    const bool vertical = aArea == Qt::LeftDockWidgetArea || aArea == Qt::RightDockWidgetArea;
    int low = vertical ? dockRegion.top() : dockRegion.left();
    int high = vertical ? dockRegion.bottom() : dockRegion.right();

    // An area gives part of that span up to a neighbour that owns the corner there,
    // and only while that neighbour actually holds something: an empty area's corner
    // region collapses and the areas beside it grow into it.
    const bool leftOwnsTop = aWindow.corner(Qt::TopLeftCorner) == Qt::LeftDockWidgetArea;
    const bool rightOwnsTop = aWindow.corner(Qt::TopRightCorner) == Qt::RightDockWidgetArea;
    const bool leftOwnsBottom = aWindow.corner(Qt::BottomLeftCorner) == Qt::LeftDockWidgetArea;
    const bool rightOwnsBottom = aWindow.corner(Qt::BottomRightCorner) == Qt::RightDockWidgetArea;
    const bool neighbourAtLow = (aArea == Qt::LeftDockWidgetArea) ? !leftOwnsTop
                             : (aArea == Qt::RightDockWidgetArea) ? !rightOwnsTop
                             : (aArea == Qt::TopDockWidgetArea) ? !leftOwnsTop
                                                                : !leftOwnsBottom;
    const bool neighbourAtHigh = (aArea == Qt::LeftDockWidgetArea) ? !leftOwnsBottom
                              : (aArea == Qt::RightDockWidgetArea) ? !rightOwnsBottom
                              : (aArea == Qt::TopDockWidgetArea) ? !rightOwnsTop
                                                                 : !rightOwnsBottom;
    const QRect lowBand = eachArea[int(vertical ? Qt::TopDockWidgetArea : Qt::LeftDockWidgetArea)];
    const QRect highBand = eachArea[int(vertical ? Qt::BottomDockWidgetArea : Qt::RightDockWidgetArea)];
    if (neighbourAtLow && !lowBand.isNull())
        low = qMax(low, vertical ? lowBand.bottom() + 1 : lowBand.right() + 1);
    if (neighbourAtHigh && !highBand.isNull())
        high = qMin(high, vertical ? highBand.top() - 1 : highBand.left() - 1);
    high = qMax(low, high);

    const int thickness = vertical ? qMax(1, aPanelSize.width()) : qMax(1, aPanelSize.height());
    switch (aArea) {
    case Qt::LeftDockWidgetArea:
        return QRect(dockRegion.left(), low, thickness, high - low + 1);
    case Qt::RightDockWidgetArea:
        return QRect(dockRegion.right() + 1 - thickness, low, thickness, high - low + 1);
    case Qt::TopDockWidgetArea:
        return QRect(low, dockRegion.top(), high - low + 1, thickness);
    default:
        return QRect(low, dockRegion.bottom() + 1 - thickness, high - low + 1, thickness);
    }
}

const DockSlot* slotFor(const QVector<DockSlot>& aSlots, const QDockWidget* aPanel) {
    for (const DockSlot& candidate : aSlots) {
        if (candidate.panels.contains(const_cast<QDockWidget*>(aPanel))) return &candidate;
    }
    return nullptr;
}

} // namespace gui
