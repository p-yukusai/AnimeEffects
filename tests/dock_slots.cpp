// THE DOCK SLOT MODEL AND THE DROP RULE (src/gui/DockSlots.h, DockDropOverlay.h).
//
// WHAT IS DRIVEN. A real QMainWindow with real QDockWidgets and this app's DockPanel
// title rows, laid out by Qt at a real size. The slot model and the drop resolver are
// the two places every docking gesture reads: the seams, the layout toggles, the
// collapse and the preview all ask them "what is a slot" and "where would this land".
// Both used to be re-derived at each call site, differently, which is how an inactive
// tab's off-screen geometry made an area cover the whole window.
//
// WHAT IT PINS:
//   1 THE SLOTS. One per area in a fixed order, occupied ones with a rect that holds
//     their dock, and an EMPTY area still listed (it is a place a panel can be
//     dropped) with a null rect.
//   2 HIDDEN IS NOT EMPTY. Hiding the only panel in an area leaves it in `panels`
//     (the slot still owns it) but takes it out of `onScreen` and leaves the rect
//     null; that is the difference the collapse and the seam depend on.
//   3 THE TAB GROUP IS ONE SLOT. Tabifying two panels leaves both in `panels`, only
//     the visible one in `onScreen`, and the slot rect equal to the visible one's -
//     the parked tab's off-screen geometry must not widen the area.
//   4 THE EMPTY-AREA PLACE. An area with nothing in it still has a place, derived from
//     the region the areas tile, so a drop can target it.
//   5 THE DROP RULE. The middle of an occupied slot is a Tabify at that group; the
//     panel's own slot is no target; an empty area is an Area target; a point over
//     the canvas is an Area whose rect the placement rule produced; outside the
//     window is no target at all.
//
// Run: QT_QPA_PLATFORM=offscreen ./build/tests/Debug/dock_slots
#include <QApplication>
#include <QDockWidget>
#include <QMainWindow>
#include <cstdio>

#include "gui/DockDropOverlay.h"
#include "gui/DockPanel.h"
#include "gui/DockSlots.h"

namespace {

#define CHECK(cond)                                                                                    \
    do {                                                                                               \
        if (!(cond)) {                                                                                 \
            std::printf("FAIL %d: %s\n", __LINE__, #cond);                                             \
            return 1;                                                                                  \
        }                                                                                              \
    } while (false)

gui::DockPanel* addPanel(QMainWindow& aWindow, Qt::DockWidgetArea aArea, const char* aName) {
    auto* panel = new gui::DockPanel(&aWindow);
    panel->setObjectName(QString::fromLatin1(aName));
    panel->setWindowTitle(QString::fromLatin1(aName));
    panel->setMainWindow(&aWindow);
    panel->setWidget(new QWidget(panel));
    aWindow.addDockWidget(aArea, panel);
    return panel;
}

int run() {
    QMainWindow window;
    window.setCentralWidget(new QWidget(&window));
    window.resize(1200, 800);

    gui::DockPanel* left = addPanel(window, Qt::LeftDockWidgetArea, "leftDock");
    gui::DockPanel* right = addPanel(window, Qt::RightDockWidgetArea, "rightDock");
    gui::DockPanel* bottom = addPanel(window, Qt::BottomDockWidgetArea, "bottomDock");

    window.show();
    QApplication::processEvents();
    QApplication::processEvents();

    // --- 1: the slots, one per area, the occupied ones measured, the empty one listed.
    const QVector<gui::DockSlot> areas = gui::dockSlots(window);
    CHECK(areas.size() == 4);
    CHECK(areas[0].area == Qt::LeftDockWidgetArea);
    CHECK(areas[1].area == Qt::RightDockWidgetArea);
    CHECK(areas[2].area == Qt::TopDockWidgetArea);
    CHECK(areas[3].area == Qt::BottomDockWidgetArea);
    CHECK(areas[0].panels.size() == 1 && areas[0].panels.first() == left);
    CHECK(areas[0].onScreen.size() == 1);
    CHECK(!areas[0].rect.isNull() && areas[0].rect.width() > 0);
    CHECK(areas[1].panels.size() == 1 && areas[1].panels.first() == right);
    CHECK(!areas[1].rect.isNull() && areas[1].rect.width() > 0);
    CHECK(areas[2].isEmpty() && areas[2].rect.isNull());
    CHECK(areas[3].panels.size() == 1 && areas[3].panels.first() == bottom);
    CHECK(!areas[3].rect.isNull() && areas[3].rect.height() > 0);
    CHECK(gui::slotFor(areas, left) == &areas[0]);
    CHECK(gui::slotFor(areas, bottom) == &areas[3]);

    // --- 4: an empty area still has a place, and it is inside the window.
    const QRect emptyTopPlace = gui::dockAreaPlace(window, areas, QSize(200, 160),
                                                   Qt::TopDockWidgetArea);
    CHECK(!emptyTopPlace.isNull());
    CHECK(emptyTopPlace.height() == 160);
    CHECK(window.rect().contains(emptyTopPlace));

    // --- 5: the drop rule, against a real overlay and the real layout.
    gui::DockDropOverlay overlay(&window);
    overlay.beginDrag(bottom);

    // The middle of the left slot: a tab there.
    gui::DockDropTarget target = overlay.resolve(areas[0].rect.center());
    CHECK(target.kind == gui::DockDropKind::Tabify);
    CHECK(target.area == Qt::LeftDockWidgetArea);
    CHECK(target.group == left);
    CHECK(!target.rect.isEmpty());

    // An empty area is an area target, and its rect is the place dockAreaPlace gave.
    target = overlay.resolve(emptyTopPlace.center());
    CHECK(target.kind == gui::DockDropKind::Area);
    CHECK(target.area == Qt::TopDockWidgetArea);
    CHECK(target.rect == emptyTopPlace);

    // The canvas gives an area by the nearest side, with a placement-derived rect.
    const QRect canvas = window.centralWidget()->geometry();
    CHECK(!canvas.isEmpty());
    target = overlay.resolve(QPoint(canvas.center().x(), canvas.top() + 2));
    CHECK(target.kind == gui::DockDropKind::Area);
    CHECK(target.area != Qt::NoDockWidgetArea);
    CHECK(!target.rect.isEmpty());

    // Outside the window there is no target: the panel stays where it is.
    target = overlay.resolve(QPoint(-4, window.height() / 2));
    CHECK(target.kind == gui::DockDropKind::None);
    target = overlay.resolve(QPoint(window.width() / 2, window.height() + 20));
    CHECK(target.kind == gui::DockDropKind::None);

    // A docked panel over its own slot is not a target (the drop would be a no-op).
    overlay.beginDrag(left);
    target = overlay.resolve(areas[0].rect.center());
    CHECK(target.kind == gui::DockDropKind::None);
    overlay.endDrag();

    // --- 2: hidden is not empty. The slot still owns the panel, but nothing occupies
    // it and it has no rect - the collapse and the seam read exactly this.
    left->hide();
    QApplication::processEvents();
    QApplication::processEvents();
    const QVector<gui::DockSlot> hidden = gui::dockSlots(window);
    CHECK(hidden[0].panels.size() == 1 && hidden[0].panels.first() == left);
    CHECK(hidden[0].onScreen.isEmpty());
    CHECK(hidden[0].rect.isNull());
    const QRect leftPlace = gui::dockAreaPlace(window, hidden, QSize(200, 160),
                                               Qt::LeftDockWidgetArea);
    CHECK(!leftPlace.isNull());
    overlay.beginDrag(bottom);
    target = overlay.resolve(leftPlace.center());
    CHECK(target.kind == gui::DockDropKind::Area);
    CHECK(target.area == Qt::LeftDockWidgetArea);
    overlay.endDrag();
    left->show();
    QApplication::processEvents();
    QApplication::processEvents();

    // --- 3: a tab group is one slot. Both panels belong to it; only the visible one
    // occupies space, and the parked tab's off-screen geometry does not widen the rect.
    window.tabifyDockWidget(left, right);
    left->raise();
    QApplication::processEvents();
    QApplication::processEvents();
    CHECK(left->isTabGrouped());
    CHECK(right->isTabGrouped());
    const QVector<gui::DockSlot> grouped = gui::dockSlots(window);
    CHECK(grouped[0].panels.size() == 2);
    CHECK(grouped[0].onScreen.size() == 1);
    CHECK(grouped[0].onScreen.first() == left);
    CHECK(grouped[1].panels.isEmpty());
    CHECK(grouped[0].rect.width() < window.width());
    CHECK(gui::slotFor(grouped, right) == &grouped[0]);

    std::printf("PASS dock_slots\n");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    return run();
}
