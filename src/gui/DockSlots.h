#ifndef GUI_DOCKSLOTS_H
#define GUI_DOCKSLOTS_H

#include <QDockWidget>
#include <QMap>
#include <QRect>
#include <QVector>

class QMainWindow;

namespace gui {

/**
 * A dock slot: a place a panel can be, and the panels in it.
 *
 * This exists because "the area" was being re-derived, differently, in every
 * place that needed it - the sash's seams took a dock's rect, the layout toggles
 * counted docks by area, the drop preview unioned docks' rects, the collapse hid
 * one dock - and each derivation got a different piece of it wrong. Measured
 * examples of that, all from the same hole: an inactive tab is still visible and
 * still reports a size (Qt parks it off-screen), so a union of docks' rects made
 * an area cover the whole window; a grouped panel's own title row is hidden but
 * still reserves its height, so a group showed a 32px gap under its rail; the
 * rail itself belongs to the slot but to no dock, so the slot's resize seam
 * stopped at the tabs.
 *
 * Qt's API only knows panels (dockWidgetArea, tabifiedDockWidgets, resizeDocks),
 * so a slot is derived - here, once, for everyone who needs one:
 *
 *  - `panels` is every panel in the slot, in tab order, including hidden ones:
 *    hiding a panel does not remove it from the slot it lives in;
 *  - `onScreen` is the panels actually occupying space. An inactive tab of a
 *    group reports visible and a real size while sitting off-screen, so this is
 *    the list to measure anything from;
 *  - `rect` is the slot itself: the on-screen panels plus the group's rail, which
 *    is chrome *of the slot* rather than of any panel in it.
 *
 * An area with nothing in it is a slot too, and it is listed here like any other:
 * an empty sidebar is still a place a panel can be dropped, with a place in the
 * window of its own (see dockAreaPlace). Writing its place down anywhere but here
 * is how the drop preview ended up measured against the canvas - which stops
 * wherever a panel happens to end - instead of against the area's own place.
 */
struct DockSlot {
    Qt::DockWidgetArea area = Qt::NoDockWidgetArea;
    QVector<QDockWidget*> panels;
    QVector<QDockWidget*> onScreen;
    QRect rect;

    bool isEmpty() const { return panels.isEmpty(); }
    bool isVisible() const { return !onScreen.isEmpty(); }
    /// The panel a resize acts through: one dock per slot is enough, and Qt
    /// distributes the rest of the slot's panels around it.
    QDockWidget* lead() const { return onScreen.isEmpty() ? nullptr : onScreen.first(); }
};

/// Every slot, by area, in the order left, right, top, bottom - empty ones
/// included, with a null rect and nothing in them.
QVector<DockSlot> dockSlots(const QMainWindow& aWindow);

/// The place an area takes: the slice of the window a panel of this size gets
/// there. Needed for an area that holds nothing, which has no rect of its own -
/// it is a side of the window and a corner claim, not a rectangle - so its place
/// is derived from the region the areas tile and the corners each area owns.
QRect dockAreaPlace(const QMainWindow& aWindow, const QVector<DockSlot>& aSlots,
                    const QSize& aPanelSize, Qt::DockWidgetArea aArea);

/// The slot a panel lives in, whatever its visibility.
const DockSlot* slotFor(const QVector<DockSlot>& aSlots, const QDockWidget* aPanel);

} // namespace gui

#endif // GUI_DOCKSLOTS_H
