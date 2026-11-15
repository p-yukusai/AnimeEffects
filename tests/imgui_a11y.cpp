// Standalone contract test for the accessibility bridge of the ImGui-drawn
// viewport overlay (gui::a11y, src/gui/ImGuiA11y.{h,cpp}).
//
// WHY IT EXISTS. The overlay paints the animation-graph panel with ImGui into a
// GL framebuffer, so the panel is not a widget, owns no QObject, and is invisible
// to AT-SPI: the window's accessibility dump is byte-identical with the overlay
// on and off. gui::a11y closes that gap by publishing the overlay's plain,
// per-frame model through a QAccessible factory, the same way
// gui::installQmlFormAccessibility does for the QML dialogs. This file is the
// evidence that the published tree says what the model says - roles, names,
// values, checked state, screen rectangles - and that acting on it reaches the
// overlay with the right id.
//
// It is a UNIT test: no GL, no ImGui, only ImGuiA11y.cpp and Qt. What it cannot
// decide (see the report): whether the AT-SPI bridge forwards this tree, and
// whether a real screen reader sees it - that needs the live overlay under Xvfb
// plus a dump of the real window.
//
// INSTALL ORDER. Step 0 is the order the module requires, and it is a gate of its
// own: installFactoryOnly() runs before anything has asked Qt for the widget's
// interface (the application calls it from the viewport widget's constructor),
// install() from the first frame then ADOPTS that same installation, and step 11
// is the mirror image - a widget something asked about first, where the fix must
// leave the cached interface (and the id a client would be holding) completely
// alone and report the shadowing instead of evicting it. Step 11 prints the
// module's expected qWarning on purpose (it is a Qt warning, so in this Qt build
// it reaches the journal rather than stderr unless QT_FORCE_STDERR_LOGGING=1; the
// assertions below never depend on it - they print and compare the interfaces).
//
// The last steps are a LIFETIME gate rather than a tree assertion: an interface
// this unit creates must not stay in Qt's QAccessible cache after it is gone.
// Children have no QObject, so AT-SPI registers each of them under an id of its
// own (QAccessible::uniqueId from pathForInterface), and that id is the DBus
// path a client later queries - so a stale entry is reachable from outside the
// process. Steps 8 and 10 register children exactly the way the bridge does and
// then ask the cache what their ids mean once the installation is gone (the
// widget destroyed, uninstall() called, and the whole sequence repeated).
//
// Headless and self-contained (Qt widgets + QtGui + QtCore only; no GL, no
// ImGui, no project build system):
//   g++ -std=c++17 -fPIC -I src -I/usr/include/qt6 -I/usr/include/qt6/QtCore -I/usr/include/qt6/QtGui -I/usr/include/qt6/QtWidgets tests/imgui_a11y.cpp src/gui/ImGuiA11y.cpp -lQt6Widgets -lQt6Gui -lQt6Core -o /tmp/imgui_a11y
//   QT_QPA_PLATFORM=offscreen /tmp/imgui_a11y
//   QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=2 /tmp/imgui_a11y
// The second run is not decoration: the offscreen platform then reports
// devicePixelRatio 2, which is how the "rects are device independent" mapping is
// checked without a HiDPI screen. Every rectangle assertion below is written
// against QWidget::mapToGlobal, i.e. against where the widget's top-left really
// is on screen, so both runs must print the same verdict. The offscreen platform
// also puts a frame margin between the widget and the screen (a widget asked to
// sit at 120,64 lands at 122,66), which is why the expected rectangles are
// derived from the live mapping and printed rather than hardcoded as 120,64.
//
// ENVIRONMENT NOTE (measured on this workstation, 2026-10-12): the session sets
// QT_QPA_PLATFORMTHEME=cosmic, which loads libcutecosmictheme.so - a Rust Qt
// plugin - into every Qt process. Its CuteCosmicWatcher thread outlives
// QApplication's teardown and then calls QMetaObject::invokeMethod on QObjects
// the main thread has already destroyed, so a Qt process in this session can die
// AFTER this harness has printed PASS: the core dump shows frame #0 in
// std::atomic<void*>::load under QMetaObject::invokeMethodImpl under
// CuteCosmicWatcher::startWatching()::{lambda(void*)#1}, and the same teardown
// also aborts with "free(): chunks in smallbin corrupted" raised from fontconfig's
// XML parser (expat realloc). Measured: 11 of 30 runs aborted with the ambient
// theme loaded, 0 of 30 with it cleared - and a widget-churn-only Qt program with
// no test code and no accessibility at all crashed 2 of 30 too, so the defect is
// the plugin's, not this harness's. main() below therefore clears the variable
// before QApplication exists: a headless QAccessible unit test does not depend on
// the desktop's Qt platform theme, and letting an unrelated desktop plugin decide
// this harness's exit code would make the phase gate unreadable.
#include <QAccessible>
#include <QAccessibleActionInterface>
#include <QApplication>
#include <QByteArray>
#include <QGuiApplication>
#include <QPoint>
#include <QRect>
#include <QScreen>
#include <QString>
#include <QWidget>
#include <cstdio>

#include "gui/ImGuiA11y.h"

namespace {

// The harness fails LOUDLY: a failed precondition aborts the run instead of
// letting the later assertions "pass" on an absent interface.
#define CHECK(cond)                                                                                  \
    do {                                                                                             \
        if (!(cond)) {                                                                               \
            std::printf("FAIL %d: %s\n", __LINE__, #cond);                                            \
            return 1;                                                                                \
        }                                                                                            \
    } while (false)

// ONLY the two events sync() is responsible for are counted; Qt's own lifecycle
// events for the widget (ObjectCreated/ObjectShow/...) are noise here, so they
// land in `other` instead of being mistaken for a published change.
struct EventCounts {
    int reorder = 0;
    int visibleData = 0;
    int other = 0;
    void reset() {
        reorder = 0;
        visibleData = 0;
        other = 0;
    }
    bool any() const { return reorder != 0 || visibleData != 0 || other != 0; }
};
EventCounts gEvents;

void countEvents(QAccessibleEvent* aEvent) {
    switch (aEvent->type()) {
    case QAccessible::ObjectReorder: ++gEvents.reorder; break;
    case QAccessible::VisibleDataChanged: ++gEvents.visibleData; break;
    default: ++gEvents.other; break;
    }
}

gui::a11y::Item makeItem(gui::a11y::Item::Role aRole, int aId, const char* aName, const char* aValue,
                         const QRect& aRect, bool aChecked = false, bool aEnabled = true) {
    gui::a11y::Item item;
    item.role = aRole;
    item.id = aId;
    item.name = QString::fromUtf8(aName);
    item.value = QString::fromUtf8(aValue);
    item.rect = aRect;
    item.checked = aChecked;
    item.enabled = aEnabled;
    return item;
}

// The panel as the overlay would hand it over: two checkable legend swatches, the
// focused channel's line, the fit action, a colour swatch, and a disabled action.
gui::a11y::Model makePanelModel() {
    gui::a11y::Model model;
    model.panel = QRect(0, 0, 400, 300);
    model.summary = QStringLiteral("Graph: Move X, Rotate on 2 layers");
    model.items = {
        makeItem(gui::a11y::Item::CheckBox, 41, "Move X", "shown", QRect(8, 12, 24, 16), true),
        makeItem(gui::a11y::Item::CheckBox, 42, "Rotate", "hidden", QRect(8, 32, 24, 16), false),
        makeItem(gui::a11y::Item::StaticText, 43, "Focused: Rotate", "Rotate", QRect(8, 52, 160, 16)),
        makeItem(gui::a11y::Item::PushButton, 44, "Fit", "", QRect(200, 12, 64, 20)),
        makeItem(gui::a11y::Item::Image, 45, "Rotate swatch", "#ff8800", QRect(220, 40, 16, 16)),
        makeItem(gui::a11y::Item::PushButton, 46, "Auto key", "", QRect(200, 70, 64, 20), false, false),
    };
    return model;
}

const char* roleName(QAccessible::Role aRole) {
    switch (aRole) {
    case QAccessible::Pane: return "Pane";
    case QAccessible::CheckBox: return "CheckBox";
    case QAccessible::Button: return "Button";
    case QAccessible::StaticText: return "StaticText";
    case QAccessible::Graphic: return "Graphic";
    case QAccessible::Client: return "Client";
    case QAccessible::NoRole: return "NoRole";
    default: return "?";
    }
}

void printInterface(const char* aLabel, QAccessibleInterface* aInterface) {
    if (aInterface == nullptr) {
        std::printf("MEASURED %s = <null>\n", aLabel);
        return;
    }
    std::printf("MEASURED %s: role=%s valid=%d childCount=%d name='%s' value='%s' rect=(%d,%d %dx%d)\n",
                aLabel, roleName(aInterface->role()), int(aInterface->isValid()), aInterface->childCount(),
                qUtf8Printable(aInterface->text(QAccessible::Name)),
                qUtf8Printable(aInterface->text(QAccessible::Value)), aInterface->rect().x(),
                aInterface->rect().y(), aInterface->rect().width(), aInterface->rect().height());
}

// The expected screen rectangle of an item: the model's viewport pixels
// translated by the widget's own screen origin. Sizes are NOT scaled by
// devicePixelRatio - the model's pixels, QWidget geometry and QAccessible
// rectangles are all device independent - so this formula is the whole mapping.
QRect expectedScreenRect(const QPoint& aViewportOrigin, const QRect& aViewportRect) {
    return QRect(aViewportOrigin + aViewportRect.topLeft(), aViewportRect.size());
}

int run() {
    QWidget viewport;
    viewport.setObjectName(QStringLiteral("imguiA11yTestViewport"));
    viewport.resize(400, 300);
    viewport.move(120, 64);
    // --- 0: the factory is installed BEFORE anything can query the widget ------
    // This is the order the module requires (ImGuiA11y.h, "WHERE TO CALL IT":
    // installFactoryOnly() is the constructor-time half of install()), and it is
    // measured here on a widget that is not even shown yet, exactly like the
    // application's viewport in its own constructor.
    //
    // WHY "BEFORE" IS EARLIER THAN IT LOOKS. Qt itself asks for the widget's
    // interface as soon as the widget takes part in an accessibility event:
    // QAccessible::updateAccessibility() resolves and CACHES the interface of the
    // event's object (qaccessible.cpp: "Querying for the accessibleInterface below
    // will result in resolving and caching the interface"), and QWidget emits such
    // events on show, on parent changes and on name changes. Accessibility is
    // active in this harness and in a session with a screen reader, so the first
    // show() is enough to have the widget's PLAIN interface cached - and Qt answers
    // a later query from that cache before it ever consults a factory. Installing
    // the factory first is what makes that entry ours; nothing is created OR
    // evicted to get here.
    gui::a11y::installFactoryOnly(viewport);
    viewport.show();
    // One event loop pass so the platform window really exists before any screen
    // coordinate is asked for.
    QCoreApplication::processEvents();
    CHECK(viewport.isVisible()); // precondition: the widget is really shown
    const QPoint origin = viewport.mapToGlobal(QPoint(0, 0));
    std::printf("MEASURED viewport: geometry=(%d,%d %dx%d) screenTopLeft=(%d,%d) windowHandle=%p dpr=%.2f\n",
                viewport.x(), viewport.y(), viewport.width(), viewport.height(), origin.x(), origin.y(),
                (void*)viewport.windowHandle(),
                viewport.screen() != nullptr ? viewport.screen()->devicePixelRatio() : -1.0);

    QAccessibleInterface* early = QAccessible::queryAccessibleInterface(&viewport);
    CHECK(early != nullptr);
    CHECK(early->role() == QAccessible::Pane); // the factory answered, not Qt's widget interface
    CHECK(early->isValid());
    CHECK(early->childCount() == 0);           // no model published yet
    CHECK(early->text(QAccessible::Name).isEmpty());
    // Registered the way the bridge registers what it hands out - AT-SPI's path IS
    // this id (pathForInterface takes QAccessible::uniqueId) - to be asked about
    // again after the second install below.
    const QAccessible::Id earlyId = QAccessible::uniqueId(early);
    CHECK(earlyId != 0);
    CHECK(QAccessible::accessibleInterface(earlyId) == early);
    printInterface("0 installed before the first show", early);

    // --- 0b: install() from the first frame ADOPTS that very interface ---------
    // The overlay's initializeGL-time install() arrives after the widget is already
    // visible, i.e. after a screen reader may have walked it. Re-installing must
    // not end the installation such a client is holding: same interface, same id,
    // and only the action callback is taken over.
    int activations = 0;
    int lastId = -1;
    gui::a11y::install(viewport, [&activations, &lastId](int aId) {
        ++activations;
        lastId = aId;
    });
    CHECK(QAccessible::queryAccessibleInterface(&viewport) == early); // nothing was replaced
    CHECK(QAccessible::accessibleInterface(earlyId) == early);       // the id still resolves
    const gui::a11y::Model model = makePanelModel();
    gui::a11y::sync(model);

    // --- 1: the widget's interface is ours, and describes the panel -----------
    QAccessibleInterface* root = QAccessible::queryAccessibleInterface(&viewport);
    CHECK(root != nullptr);
    CHECK(root == early);                                  // one installation, not two
    CHECK(root->object() == &viewport);                    // ... and it is about our widget
    CHECK(root->isValid());
    CHECK(root->role() == QAccessible::Pane);
    CHECK(root->text(QAccessible::Name) == model.summary); // the panel's summary line
    // No structural child: the form-accessibility precedent adds none, and the
    // summary is the panel's own Name, so childCount() is exactly items.size().
    CHECK(root->childCount() == model.items.size());
    CHECK(root->rect() == expectedScreenRect(origin, model.panel));
    CHECK(root->parent() == nullptr);                      // top-level test widget
    CHECK(root->window() == viewport.windowHandle());
    printInterface("1 root", root);

    // --- 2: every item: role, name, value, identifier, screen rect -------------
    CHECK(model.items.size() == 6);
    for (int i = 0; i < model.items.size(); ++i) {
        const gui::a11y::Item& item = model.items.at(i);
        QAccessibleInterface* child = root->child(i);
        CHECK(child != nullptr);
        CHECK(child->isValid());
        CHECK(child->parent() == root);
        CHECK(root->indexOfChild(child) == i);
        CHECK(child->object() == nullptr); // not a QObject; see the bridge's comment
        CHECK(child->childCount() == 0);

        QAccessible::Role expectedRole = QAccessible::NoRole;
        switch (item.role) {
        case gui::a11y::Item::PushButton: expectedRole = QAccessible::Button; break;
        case gui::a11y::Item::CheckBox: expectedRole = QAccessible::CheckBox; break;
        case gui::a11y::Item::StaticText: expectedRole = QAccessible::StaticText; break;
        case gui::a11y::Item::Image: expectedRole = QAccessible::Graphic; break;
        }
        CHECK(child->role() == expectedRole);
        CHECK(child->text(QAccessible::Name) == item.name);
        CHECK(child->text(QAccessible::Value) == item.value);
        CHECK(child->text(QAccessible::Identifier) == QString::number(item.id));
        CHECK(child->state().disabled == !item.enabled);
        CHECK(!child->state().invisible);
        CHECK(child->rect() == expectedScreenRect(origin, item.rect));
        // The size is the model's size, whatever the device pixel ratio: a
        // scaling bug would multiply it here (dpr 1 and dpr 2 runs both check).
        CHECK(child->rect().size() == item.rect.size());
        std::printf("MEASURED 2 item[%d] id=%d role=%s name='%s' value='%s' rect=(%d,%d %dx%d) expected=(%d,%d %dx%d)\n",
                    i, item.id, roleName(child->role()), qUtf8Printable(item.name),
                    qUtf8Printable(item.value), child->rect().x(), child->rect().y(),
                    child->rect().width(), child->rect().height(),
                    expectedScreenRect(origin, item.rect).x(), expectedScreenRect(origin, item.rect).y(),
                    expectedScreenRect(origin, item.rect).width(), expectedScreenRect(origin, item.rect).height());
    }

    // The exact expected rectangle written out for a known placement (the model's
    // (8,12,24,16) checkable row and its (200,12,64,20) fit button), so a wrong
    // mapping cannot cancel itself out against the helper above.
    CHECK(root->child(0)->rect() == QRect(origin.x() + 8, origin.y() + 12, 24, 16));
    CHECK(root->child(3)->rect() == QRect(origin.x() + 200, origin.y() + 12, 64, 20));
    CHECK(root->child(2)->rect() == QRect(origin.x() + 8, origin.y() + 52, 160, 16));

    // childAt takes SCREEN coordinates, like rect() returns: the middle of a
    // published item is that item, and a point outside every item is nothing.
    const QRect swatchRect = root->child(0)->rect();
    CHECK(root->childAt(swatchRect.center().x(), swatchRect.center().y()) == root->child(0));
    CHECK(root->childAt(origin.x() + 390, origin.y() + 290) == nullptr); // empty panel corner
    CHECK(root->focusChild() == nullptr);                                 // Model has no focus flag
    std::printf("MEASURED 2 childAt(center of child0)=child0: %d; childAt(empty corner)=null: %d\n",
                int(root->childAt(swatchRect.center().x(), swatchRect.center().y()) == root->child(0)),
                int(root->childAt(origin.x() + 390, origin.y() + 290) == nullptr));

    // --- 3: a checkable item reports its value, its state and its actions -------
    QAccessibleInterface* shownSwatch = root->child(0);   // "shown", checked
    QAccessibleInterface* hiddenSwatch = root->child(1);  // "hidden", unchecked
    CHECK(shownSwatch->role() == QAccessible::CheckBox);
    CHECK(shownSwatch->text(QAccessible::Value) == QStringLiteral("shown"));
    CHECK(shownSwatch->state().checkable);
    CHECK(shownSwatch->state().checked);
    CHECK(!shownSwatch->state().disabled);
    CHECK(hiddenSwatch->text(QAccessible::Value) == QStringLiteral("hidden"));
    CHECK(hiddenSwatch->state().checkable);
    CHECK(!hiddenSwatch->state().checked);

    QAccessibleActionInterface* shownActions = shownSwatch->actionInterface();
    QAccessibleActionInterface* hiddenActions = hiddenSwatch->actionInterface();
    CHECK(shownActions != nullptr);
    CHECK(hiddenActions != nullptr);
    const QStringList shownActionNames = shownActions->actionNames();
    std::printf("MEASURED 3 swatch[0] value='%s' checkable=%d checked=%d actions=%s\n",
                qUtf8Printable(shownSwatch->text(QAccessible::Value)), int(shownSwatch->state().checkable),
                int(shownSwatch->state().checked), qUtf8Printable(shownActionNames.join(QLatin1Char(','))));
    CHECK(shownActionNames.contains(QAccessibleActionInterface::pressAction()));
    CHECK(shownActionNames.contains(QAccessibleActionInterface::toggleAction()));
    CHECK(shownActionNames.contains(QStringLiteral("check")));
    CHECK(shownActionNames.contains(QStringLiteral("uncheck")));
    // Qt's own QCheckBox answers "Toggle,Press"; the actions below are that set
    // plus AT-SPI's check/uncheck, and no SetFocus - these items are not Qt focus
    // scopes.
    CHECK(hiddenActions->actionNames() == shownActionNames);

    // A label and a colour swatch have nothing to press, and an image does not
    // advertise an image protocol either.
    CHECK(root->child(2)->actionInterface() == nullptr);
    CHECK(root->child(4)->actionInterface() == nullptr);
    CHECK(root->child(4)->imageInterface() == nullptr);
    CHECK(root->child(4)->valueInterface() == nullptr);
    CHECK(root->child(0)->valueInterface() == nullptr); // a word, not a number
    CHECK(root->child(3)->valueInterface() == nullptr); // a button, not a number

    // --- 4: acting on an item calls back with that item's id -------------------
    CHECK(activations == 0);
    shownActions->doAction(QAccessibleActionInterface::pressAction());
    CHECK(activations == 1);
    CHECK(lastId == 41);
    shownActions->doAction(QAccessibleActionInterface::toggleAction());
    CHECK(activations == 2);
    CHECK(lastId == 41);
    // "check" on a row that is already checked would change nothing, so it does
    // not fire; "uncheck" on it does.
    shownActions->doAction(QStringLiteral("check"));
    CHECK(activations == 2);
    shownActions->doAction(QStringLiteral("uncheck"));
    CHECK(activations == 3);
    CHECK(lastId == 41);
    hiddenActions->doAction(QStringLiteral("check"));
    CHECK(activations == 4);
    CHECK(lastId == 42);
    // A disabled action still advertises itself but must not reach the overlay.
    QAccessibleActionInterface* disabledActions = root->child(5)->actionInterface();
    CHECK(disabledActions != nullptr);
    disabledActions->doAction(QAccessibleActionInterface::pressAction());
    CHECK(activations == 4);
    // An unknown action name is ignored rather than forwarded.
    hiddenActions->doAction(QStringLiteral("activate"));
    CHECK(activations == 4);
    std::printf("MEASURED 4 activations=%d lastId=%d (press,toggle,check,uncheck as asserted; "
                "disabled and unknown names stayed silent)\n",
                activations, lastId);

    // --- 5: sync() publishes a new frame with no re-install --------------------
    gui::a11y::Model renamed = model;
    renamed.summary = QStringLiteral("Graph: Move X on 1 layer");
    renamed.items[0].name = QStringLiteral("Move X (renamed)");
    renamed.items[0].rect = QRect(8, 12, 40, 16); // same slot, wider
    renamed.items[0].value = QStringLiteral("hidden");
    gEvents.reset();
    gui::a11y::sync(renamed);
    QAccessibleInterface* afterSync = QAccessible::queryAccessibleInterface(&viewport);
    CHECK(afterSync == root); // same interface: nothing was re-installed
    CHECK(root->text(QAccessible::Name) == renamed.summary);
    CHECK(root->text(QAccessible::Name) == QStringLiteral("Graph: Move X on 1 layer"));
    CHECK(root->childCount() == renamed.items.size());
    CHECK(root->child(0)->text(QAccessible::Name) == QStringLiteral("Move X (renamed)"));
    CHECK(root->child(0)->text(QAccessible::Value) == QStringLiteral("hidden"));
    CHECK(root->child(0)->rect() == QRect(origin.x() + 8, origin.y() + 12, 40, 16));
    CHECK(root->rect() == expectedScreenRect(origin, renamed.panel));
    CHECK(gEvents.visibleData >= 1);  // a content change is announced ...
    CHECK(gEvents.reorder == 0);      // ... and is not mistaken for a new tree
    std::printf("MEASURED 5 sync(content): events visibleData=%d reorder=%d, child0 rect=(%d,%d %dx%d)\n",
                gEvents.visibleData, gEvents.reorder, root->child(0)->rect().x(), root->child(0)->rect().y(),
                root->child(0)->rect().width(), root->child(0)->rect().height());

    // A child the new frame no longer has: the interface is still safe to hold,
    // it just reports itself invalid instead of answering for a stale item.
    // (The last index, because that is the one the shorter frame drops.)
    QAccessibleInterface* dropped = root->child(renamed.items.size() - 1);
    CHECK(dropped != nullptr);
    CHECK(dropped->isValid());
    gui::a11y::Model fewer = renamed;
    fewer.items.removeLast();
    gEvents.reset();
    gui::a11y::sync(fewer);
    CHECK(root->childCount() == fewer.items.size());
    CHECK(root->child(fewer.items.size()) == nullptr);
    CHECK(!dropped->isValid());
    CHECK(dropped->role() == QAccessible::NoRole);
    CHECK(gEvents.reorder >= 1); // a different tree is announced as such
    std::printf("MEASURED 5 sync(structure): events visibleData=%d reorder=%d, dropped child valid=%d\n",
                gEvents.visibleData, gEvents.reorder, int(dropped->isValid()));

    // While accessibility is off nobody is listening: sync() still publishes (a
    // query sees it) but sends no events.
    QAccessible::setActive(false);
    CHECK(!QAccessible::isActive());
    gui::a11y::Model dormant = fewer;
    dormant.summary = QStringLiteral("Graph: dormant");
    gEvents.reset();
    gui::a11y::sync(dormant);
    CHECK(root->text(QAccessible::Name) == QStringLiteral("Graph: dormant"));
    CHECK(!gEvents.any());
    QAccessible::setActive(true);
    CHECK(QAccessible::isActive());
    std::printf("MEASURED 5 sync(dormant): events reorder=%d visibleData=%d other=%d, name now '%s'\n",
                gEvents.reorder, gEvents.visibleData, gEvents.other,
                qUtf8Printable(root->text(QAccessible::Name)));

    // --- 6: uninstall leaves nothing of ours behind ----------------------------
    root->child(0); // make sure at least one child interface exists before removal
    const QAccessible::Id removedRootId = QAccessible::uniqueId(root); // deliberate: the widget's id
    CHECK(removedRootId != 0);
    gui::a11y::uninstall();
    CHECK(QAccessible::accessibleInterface(removedRootId) == nullptr); // the widget is no longer ours
    gui::a11y::uninstall();          // twice must be safe
    gui::a11y::sync(model);          // and syncing without an installation is a no-op
    // What a QUERY answers once the installation is gone - Qt's ordinary interface
    // for the widget, so a reader gets the widget back instead of nothing - is
    // measured on a widget of its own. Asking Qt for an interface CREATES and
    // caches one, and doing it on the viewport would leave the viewport with a
    // plain entry: because install() no longer evicts (it must not, see step 11),
    // a later install() on that widget would then be the shadowed ordering rather
    // than the intended one the rest of this file measures.
    {
        QWidget fallback;
        fallback.resize(80, 60);
        fallback.move(60, 30);
        gui::a11y::installFactoryOnly(fallback); // before the first show: ours from the start
        fallback.show();
        gui::a11y::install(fallback, [](int) {});
        gui::a11y::uninstall();
        QAccessibleInterface* afterUninstall = QAccessible::queryAccessibleInterface(&fallback);
        CHECK(afterUninstall != nullptr);
        CHECK(afterUninstall->role() != QAccessible::Pane);
        CHECK(afterUninstall->childCount() == 0);
        CHECK(afterUninstall->text(QAccessible::Name).isEmpty());
        printInterface("6 after uninstall", afterUninstall);
    }
    std::printf("MEASURED 6 uninstall() twice + sync() without installation: no crash\n");

    // --- 7: an empty model is safe --------------------------------------------
    gui::a11y::install(viewport, [&activations, &lastId](int aId) {
        ++activations;
        lastId = aId;
    });
    gui::a11y::Model empty;
    gui::a11y::sync(empty);
    QAccessibleInterface* emptyRoot = QAccessible::queryAccessibleInterface(&viewport);
    CHECK(emptyRoot != nullptr);
    CHECK(emptyRoot->role() == QAccessible::Pane);
    CHECK(emptyRoot->isValid());
    CHECK(emptyRoot->childCount() == 0);
    CHECK(emptyRoot->child(0) == nullptr);
    CHECK(emptyRoot->child(-1) == nullptr);
    CHECK(emptyRoot->text(QAccessible::Name).isEmpty());
    // An unset panel falls back to the widget's own rectangle, so the interface
    // still has a rectangle that could contain children.
    CHECK(emptyRoot->rect() == QRect(origin, viewport.size()));
    CHECK(emptyRoot->childAt(emptyRoot->rect().center().x(), emptyRoot->rect().center().y()) == nullptr);
    // ... and an explicitly set panel is honoured.
    gui::a11y::Model emptyWithPanel;
    emptyWithPanel.panel = QRect(10, 20, 100, 50);
    gui::a11y::sync(emptyWithPanel);
    CHECK(emptyRoot->childCount() == 0);
    CHECK(emptyRoot->rect() == QRect(origin.x() + 10, origin.y() + 20, 100, 50));
    printInterface("7 empty model", emptyRoot);
    std::printf("MEASURED 7 empty panel rect=(%d,%d %dx%d) widget rect=(%d,%d %dx%d)\n",
                emptyRoot->rect().x(), emptyRoot->rect().y(), emptyRoot->rect().width(),
                emptyRoot->rect().height(), origin.x(), origin.y(), viewport.width(), viewport.height());

    // --- 8: the viewport dying under an installation ---------------------------
    // The overlay's widget can be destroyed while the bridge is installed (a
    // document closed, a panel torn down). Qt deletes the accessible interface
    // with the widget; the label must not follow it into freed memory.
    QAccessible::Id doomedChildId = 0;
    {
        QWidget doomed;
        doomed.resize(200, 100);
        doomed.move(20, 20);
        gui::a11y::installFactoryOnly(doomed); // before the first show: ours from the start
        doomed.show();
        gui::a11y::install(doomed, [](int) {});
        gui::a11y::Model doomedModel;
        doomedModel.summary = QStringLiteral("doomed");
        doomedModel.items = {makeItem(gui::a11y::Item::PushButton, 7, "Fit", "", QRect(1, 1, 10, 10))};
        gui::a11y::sync(doomedModel);
        QAccessibleInterface* doomedRoot = QAccessible::queryAccessibleInterface(&doomed);
        CHECK(doomedRoot != nullptr);
        CHECK(doomedRoot->childCount() == 1);
        QAccessibleInterface* doomedChild = doomedRoot->child(0);
        CHECK(doomedChild != nullptr);
        // Registered the way the bridge registers it, and kept to be asked about
        // after the widget is gone (below).
        doomedChildId = QAccessible::uniqueId(doomedChild);
        CHECK(doomedChildId != 0);
        CHECK(QAccessible::accessibleInterface(doomedChildId) == doomedChild);
    }
    gui::a11y::sync(model);   // the published viewport is gone: must be inert
    gui::a11y::uninstall();   // and uninstalling must not touch the dead root
    // The widget's death took the installation with it: Qt deletes the accessible
    // interface together with its object, and that destructor drops the cache
    // entries of the children. So the id registered above must resolve to
    // nothing now - COMPARED, never dereferenced, because on the unfixed code it
    // is a dangling pointer.
    CHECK(QAccessible::accessibleInterface(doomedChildId) == nullptr);
    std::printf("MEASURED 8 viewport destroyed while installed: sync + uninstall survived; child id "
                "%u resolves to an interface after the widget died: %d\n",
                doomedChildId, int(QAccessible::accessibleInterface(doomedChildId) != nullptr));

    // The bridge is still usable afterwards: a fresh installation on a fresh
    // widget publishes again (nothing global was left corrupted).
    QWidget replacement;
    replacement.resize(120, 80);
    replacement.move(30, 30);
    gui::a11y::installFactoryOnly(replacement); // before the first show: ours from the start
    replacement.show();
    gui::a11y::install(replacement, [](int) {});
    gui::a11y::Model replacementModel;
    replacementModel.panel = QRect(0, 0, 120, 80);
    replacementModel.summary = QStringLiteral("Graph: replacement");
    replacementModel.items = {makeItem(gui::a11y::Item::StaticText, 91, "Focused: Move X", "Move X",
                                       QRect(4, 4, 100, 14))};
    gui::a11y::sync(replacementModel);
    QAccessibleInterface* replacementRoot = QAccessible::queryAccessibleInterface(&replacement);
    CHECK(replacementRoot != nullptr);
    CHECK(replacementRoot->role() == QAccessible::Pane);
    CHECK(replacementRoot->childCount() == 1);
    CHECK(replacementRoot->child(0)->text(QAccessible::Name) == QStringLiteral("Focused: Move X"));
    CHECK(replacementRoot->child(0)->rect()
          == QRect(replacement.mapToGlobal(QPoint(4, 4)), QSize(100, 14)));
    printInterface("8 replacement root", replacementRoot);
    gui::a11y::uninstall();

    // --- 9: the real shape - the viewport is a CHILD widget inside the window ---
    {
        QWidget container;
        container.resize(300, 220);
        container.move(50, 40);
        container.show();
        // The nested viewport is created WITHOUT a parent, then installed and only
        // then parented into the container. That is not cosmetic: a widget created
        // WITH a parent has its own interface cached while it is still being
        // constructed (QWidget::setParent resolves the interface for its
        // ParentChanged event - qwidget.cpp - and QAccessible::updateAccessibility
        // resolves and caches the interface of the event's object). For a plain
        // QWidget that construction-time entry carries the widget's FINAL
        // metaObject, so it shadows the factory for good; a Q_OBJECT widget's entry
        // carries the base class's metaObject and does not (which is why the
        // application's own viewport, MainDisplayWidget, wins with a
        // constructor-time install - measured separately, see the report). This
        // harness stays moc-free on purpose, so it reaches the same state the other
        // way round: the widget is complete when it is parented, so the entry Qt
        // creates for it then is answered by the factory.
        QWidget* view = new QWidget(nullptr);
        gui::a11y::installFactoryOnly(*view);
        view->setParent(&container);
        view->setGeometry(20, 30, 240, 160); // child geometry is parent-relative
        // A child created after its parent was shown is not shown with it; the
        // precondition below is what the rect assertions stand on.
        view->show();
        QCoreApplication::processEvents();
        CHECK(view->isVisible());
        const QPoint viewOrigin = view->mapToGlobal(QPoint(0, 0));
        CHECK(viewOrigin == container.mapToGlobal(QPoint(20, 30)));

        gui::a11y::install(*view, [](int) {});
        gui::a11y::Model nested;
        nested.panel = QRect(0, 0, 240, 160);
        nested.summary = QStringLiteral("Graph: nested viewport");
        nested.items = {makeItem(gui::a11y::Item::CheckBox, 61, "Move Y", "shown", QRect(5, 6, 18, 12), true)};
        gui::a11y::sync(nested);
        QAccessibleInterface* nestedRoot = QAccessible::queryAccessibleInterface(view);
        CHECK(nestedRoot != nullptr);
        CHECK(nestedRoot->role() == QAccessible::Pane);
        CHECK(nestedRoot->childCount() == 1);
        // A child widget's mapping starts at the CHILD's own screen origin, not
        // the container's: the parent's offset is in mapToGlobal already.
        CHECK(nestedRoot->child(0)->rect() == QRect(viewOrigin.x() + 5, viewOrigin.y() + 6, 18, 12));
        CHECK(nestedRoot->rect() == QRect(viewOrigin, view->size()));
        // The panel hangs off its container in the widget tree, which is how a
        // reader walks into it at all.
        QAccessibleInterface* containerInterface = QAccessible::queryAccessibleInterface(&container);
        CHECK(containerInterface != nullptr);
        CHECK(nestedRoot->parent() == containerInterface);
        // ... and the factory matched the ONE installed viewport, not every widget.
        CHECK(containerInterface->role() != QAccessible::Pane);
        std::printf("MEASURED 9 nested viewport: origin=(%d,%d) child rect=(%d,%d %dx%d) "
                    "panel rect=(%d,%d %dx%d) parent=container(%s)\n",
                    viewOrigin.x(), viewOrigin.y(), nestedRoot->child(0)->rect().x(),
                    nestedRoot->child(0)->rect().y(), nestedRoot->child(0)->rect().width(),
                    nestedRoot->child(0)->rect().height(), nestedRoot->rect().x(), nestedRoot->rect().y(),
                    nestedRoot->rect().width(), nestedRoot->rect().height(), roleName(containerInterface->role()));
        gui::a11y::uninstall();
    }

    // --- 10: no interface this unit created may outlive itself in the cache ----
    // AT-SPI turns every interface it is handed into a DBus path by asking for
    // its id: pathForInterface() calls QAccessible::uniqueId(iface) (qtbase 6.11
    // src/gui/accessible/linux/atspiadaptor.cpp), and for one of our CHILDREN,
    // whose object() is nullptr, that is QAccessibleCache::insert(nullptr, iface)
    // (qaccessible.cpp registerAccessibleInterface): the child is registered in
    // the process-global cache under an id of its own, and that id IS the path a
    // client later sends its queries to (the bridge resolves a path back with
    // QAccessible::accessibleInterface). If uninstall() frees the children while
    // those ids stay in the cache, every such query reaches freed memory, and
    // Qt's own cache teardown walk deletes those interfaces a second time.
    //
    // This step is that client: it walks the tree, registers every child exactly
    // the way the bridge does, keeps the ids, uninstalls, and then asks the cache
    // what each id means now. QAccessible::accessibleInterface(id) is the
    // observable the public API offers (the cache itself is private), and every
    // assertion is on the POINTER it returns, never on what it points to -
    // on the unfixed code that pointer is freed memory. Running the sequence
    // twice, with one accumulated id list, is the "install/uninstall twice
    // leaves nothing behind" check.
    //
    // A widget of its own, because the step is about the ids of ONE installation
    // and nothing else: the steps above ended the main viewport's installation and
    // then churned windows (which is enough for Qt to raise an accessibility event
    // for a shown toplevel - measured: StateChanged while other toplevels appear),
    // and any such event resolves and caches a PLAIN interface for the viewport
    // once the factory is gone. install() will not evict that entry (step 11), so a
    // gate that must reach the factory has to be a widget nothing has queried yet.
    QWidget gate;
    gate.resize(300, 260);
    gate.move(70, 50);
    gui::a11y::installFactoryOnly(gate); // before the first show: ours from the start
    gate.show();
    QCoreApplication::processEvents();

    QVector<QAccessible::Id> childIds;
    for (int round = 1; round <= 2; ++round) {
        gui::a11y::install(gate, [&activations, &lastId](int aId) {
            ++activations;
            lastId = aId;
        });
        gui::a11y::sync(model);
        QAccessibleInterface* gateRoot = QAccessible::queryAccessibleInterface(&gate);
        CHECK(gateRoot != nullptr);
        CHECK(gateRoot->role() == QAccessible::Pane);
        const QAccessible::Id rootId = QAccessible::uniqueId(gateRoot);
        CHECK(rootId != 0);
        CHECK(QAccessible::accessibleInterface(rootId) == gateRoot); // resolvable while installed
        CHECK(gateRoot->childCount() == model.items.size());
        for (int i = 0; i < gateRoot->childCount(); ++i) {
            QAccessibleInterface* child = gateRoot->child(i);
            CHECK(child != nullptr);
            CHECK(child->object() == nullptr); // no object to be registered under: hence its own id
            const QAccessible::Id id = QAccessible::uniqueId(child); // the bridge's own path
            CHECK(id != 0);
            CHECK(QAccessible::accessibleInterface(id) == child); // and it resolves to that child
            childIds.append(id);
        }

        gui::a11y::uninstall();

        // The root's entry goes together with the widget's ordinary interface ...
        CHECK(QAccessible::accessibleInterface(rootId) == nullptr);
        // ... and every child id collected so far - this round's and the previous
        // round's - must resolve to nothing. The values are only COMPARED; on the
        // unfixed code they are dangling, and reading through them is precisely
        // what the fix removes.
        int stale = 0;
        for (QAccessible::Id id : childIds)
            if (QAccessible::accessibleInterface(id) != nullptr) ++stale;
        CHECK(stale == 0);
        std::printf("MEASURED 10 round %d: rootId=%u plus %d child ids registered through the "
                    "bridge's own path; after uninstall() %d of %d ids still resolve to an "
                    "interface\n",
                    round, rootId, int(model.items.size()), stale, int(childIds.size()));
    }

    // --- 11: a widget queried BEFORE install: nothing is deleted ---------------
    // The failure this module must never cause. The AT-SPI bridge meets the
    // viewport while the window is still initialising, and the path it hands a
    // client for an object IS that object's QAccessible id
    // (AtSpiAdaptor::pathForInterface calls QAccessible::uniqueId on the interface
    // it is handed); the client keeps that path. install() used to delete the
    // widget's cached entry to make room for the factory, and deleting an entry
    // erases its id AND frees its interface (QAccessibleCache::deleteInterface:
    // idToInterface.erase, interfaceToId.take, delete iface), so a path a client is
    // already holding resolves to nothing afterwards - AtSpiAdaptor::handleMessage
    // then finds no interface for it and sends no reply at all, which is a panel
    // node that never enumerates a child. The fix leaves the entry entirely alone,
    // so the id must still mean the interface it always meant and the widget must
    // stay reachable.
    QWidget shadowed;
    shadowed.resize(150, 100);
    shadowed.move(40, 40);
    shadowed.show();
    QAccessibleInterface* plain = QAccessible::queryAccessibleInterface(&shadowed);
    CHECK(plain != nullptr);                   // Qt's own interface for the widget
    CHECK(plain->role() != QAccessible::Pane); // evidence it is not ours
    const QAccessible::Id plainId = QAccessible::uniqueId(plain); // the id the bridge would hold
    CHECK(plainId != 0);
    CHECK(QAccessible::accessibleInterface(plainId) == plain);
    printInterface("11 before install", plain);

    gui::a11y::install(shadowed, [](int) {}); // install() must REPORT this, not repair it
    // (a) the held id is not left dangling: it still resolves to the very interface
    //     it resolved to before. The value is only COMPARED - on the old, evicting
    //     code the interface behind it was freed at this point, and reading through
    //     that id is precisely what the fix removes.
    CHECK(QAccessible::accessibleInterface(plainId) == plain);
    // (b) the widget is still reachable, and it is the plain interface that answers:
    //     Qt answers from the cache before any factory, so the panel is NOT
    //     published for this widget. That is the documented consequence of not
    //     evicting, and the reason the factory has to be installed from the
    //     widget's constructor (step 0) rather than from its first frame.
    CHECK(QAccessible::queryAccessibleInterface(&shadowed) == plain);
    CHECK(plain->isValid()); // safe: obtained again on the line above
    CHECK(plain->role() != QAccessible::Pane);
    CHECK(plain->childCount() == 0);
    std::printf("MEASURED 11 queried before install: pre-install id still resolves to the same "
                "interface=%d, panel published for that widget=%d\n",
                int(QAccessible::accessibleInterface(plainId) == plain),
                int(QAccessible::queryAccessibleInterface(&shadowed) != plain));
    // A second install cannot recover it either - and still deletes nothing ...
    gui::a11y::install(shadowed, [](int) {});
    CHECK(QAccessible::accessibleInterface(plainId) == plain);
    // ... and neither does the teardown of that installation: uninstall() drops the
    // ids of the interfaces THIS unit created (step 10) and never anyone else's.
    gui::a11y::uninstall();
    CHECK(QAccessible::accessibleInterface(plainId) == plain);

    // The same shadowing arrives the other way round, and this is the ordering the
    // module cannot repair at all: uninstall() ends an installation and removes the
    // factory, so the NEXT thing that asks Qt for the viewport gets a plain
    // interface - and asking is not something only a client does. Qt's own event
    // path asks whenever the widget takes part in an accessibility event (measured
    // above: ObjectShow when the widget is first shown; StateChanged for a shown
    // toplevel while other windows come and go), so a viewport whose overlay was
    // torn down and is built again later can find a plain entry already cached.
    // install() reports that and publishes nothing rather than freeing an
    // interface a client may hold - the consequence to route around by installing
    // where the application does (step 0), not to delete around.
    {
        QWidget lapsing;
        lapsing.resize(90, 70);
        lapsing.move(50, 50);
        gui::a11y::installFactoryOnly(lapsing); // ours first, as in step 0
        lapsing.show();
        QCoreApplication::processEvents();
        gui::a11y::install(lapsing, [](int) {});
        QAccessibleInterface* before = QAccessible::queryAccessibleInterface(&lapsing);
        CHECK(before != nullptr);
        CHECK(before->role() == QAccessible::Pane); // ours while installed
        gui::a11y::uninstall();                     // ... and gone with the installation
        // The query stands in for the event path's own query: it is answered by
        // Qt's ordinary interface for the widget now.
        QAccessibleInterface* after = QAccessible::queryAccessibleInterface(&lapsing);
        CHECK(after != nullptr);
        CHECK(after->role() != QAccessible::Pane);
        const QAccessible::Id afterId = QAccessible::uniqueId(after);
        CHECK(afterId != 0);
        // Re-installing on that widget reaches nothing: the cached entry answers
        // first, and it is not ours to delete.
        gui::a11y::install(lapsing, [](int) {});
        CHECK(QAccessible::queryAccessibleInterface(&lapsing) == after);
        CHECK(QAccessible::accessibleInterface(afterId) == after);
        std::printf("MEASURED 11 uninstall then one query then install: the panel is published "
                    "for that widget=%d, the interface the query got is still alive=%d\n",
                    int(QAccessible::queryAccessibleInterface(&lapsing) != after),
                    int(QAccessible::accessibleInterface(afterId) == after));
        gui::a11y::uninstall();
        CHECK(QAccessible::accessibleInterface(afterId) == after);
    }

    std::printf("PASS gui::a11y (devicePixelRatio=%.2f)\n",
                viewport.screen() != nullptr ? viewport.screen()->devicePixelRatio() : -1.0);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    // See the ENVIRONMENT NOTE at the top of this file: the ambient desktop's Qt
    // platform theme plugin cannot survive Qt's own teardown in this session, and
    // this harness must not inherit that. Cleared BEFORE QApplication exists,
    // which is the only moment that matters - the plugin is loaded when the
    // application object is constructed.
    const QByteArray ambientTheme = qgetenv("QT_QPA_PLATFORMTHEME");
    if (!ambientTheme.isEmpty()) {
        qunsetenv("QT_QPA_PLATFORMTHEME");
        std::printf("MEASURED environment: cleared QT_QPA_PLATFORMTHEME='%s' before QApplication "
                    "(that ambient plugin crashes Qt processes after teardown; see the file header)\n",
                    ambientTheme.constData());
    }

    QApplication aApp(argc, argv);
    // A screen reader turns accessibility on; without it sync() publishes without
    // announcing (checked in step 5).
    const QAccessible::UpdateHandler previous = QAccessible::installUpdateHandler(countEvents);
    QAccessible::setActive(true);
    std::printf("MEASURED host: platform=%s screens=%d dpr=%.2f accessibilityActive=%d previousUpdateHandler=%p\n",
                qUtf8Printable(QGuiApplication::platformName()), int(QGuiApplication::screens().size()),
                aApp.primaryScreen() != nullptr ? aApp.primaryScreen()->devicePixelRatio() : -1.0,
                int(QAccessible::isActive()), (void*)previous);
    const int result = run();
    QAccessible::installUpdateHandler(previous);
    return result;
}
