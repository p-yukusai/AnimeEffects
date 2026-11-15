#ifndef GUI_IMGUIA11Y_H
#define GUI_IMGUIA11Y_H

#include <QRect>
#include <QString>
#include <QVector>
#include <functional>

class QWidget;

namespace gui {
namespace a11y {

// Publish the ImGui-drawn viewport overlay to Qt's accessibility layer.
//
// WHY THIS EXISTS. The overlay is not made of QWidgets: ImGui rasterises its
// triangles into a GL framebuffer, so the animation-graph panel it draws over
// the canvas is not a widget, owns no QObject, and therefore does not exist for
// assistive technology at all - the AT-SPI dump of the window is byte-identical
// with the overlay on and off. Qt only builds accessible objects for widgets,
// and only understands the accessibility protocols it implements itself; the
// documented answer for content it cannot see is a QAccessible FACTORY, which is
// what this file installs (same mechanism as
// gui::installQmlFormAccessibility, src/gui/qml/QmlFormAccessibility.cpp).
//
// HOW IT WORKS. The overlay hands its content over once per frame as the plain
// Model below - no QObject, no signals, nothing that ties the panel's drawing
// code to Qt's object tree. This module turns that model into one accessible
// object for the viewport widget plus one child object per Item, and reports
// every action the user (or a script) performs back through aOnAction(item.id).
// Item::id is the only handle that crosses the boundary in either direction.
//
// Because the interfaces read the published model directly (they keep no copy),
// sync() needs no per-object bookkeeping: the next query - by AT-SPI, by a
// magnifier, by a test - sees the frame that was published last. sync() also
// notifies AT-SPI that the panel changed, but only when the model really
// changed and only while accessibility is active.
//
// WHAT IS PUBLISHED, and under which role:
//   * Item::CheckBox -> QAccessible::CheckBox - a legend row's swatch: its
//     value ("shown"/"hidden") reads as the Value text, its checked state as
//     the state bits, and it carries the press and toggle actions a real
//     QCheckBox advertises (measured: a Qt QCheckBox answers "Toggle","Press")
//     plus AT-SPI's "check" and "uncheck" (Qt ships no static for those two
//     names, so they are spelled out). Press and toggle activate; check and
//     uncheck activate only when they would change the value. There is
//     deliberately NO QAccessibleValueInterface: the value here is a word, and
//     the form-accessibility precedent adds the value protocol only to a numeric
//     field, because a tool that speaks that protocol reads numbers through it.
//   * Item::PushButton -> QAccessible::Button (QAccessible::PushButton is the
//     deprecated alias of Button in Qt 6) - the panel's fit action; press.
//   * Item::StaticText -> QAccessible::StaticText - a legend row's label or the
//     focused channel's line; no actions.
//   * Item::Image -> QAccessible::Graphic - a colour swatch. Qt 6 has no
//     QAccessible::Image role; Graphic is what its own image roles map to. No
//     QAccessibleImageInterface is added: the rect plus the item's name/value
//     already say what the swatch is, and the precedent adds a protocol only
//     where Qt has none or cannot express it.
//   * the Model itself -> the viewport widget's interface: QAccessible::Pane
//     (a container AT-SPI can name and walk), its Name is Model::summary, and
//     its rect is Model::panel.
//
// COORDINATE MAPPING. Item::rect and Model::panel are in VIEWPORT PIXELS, i.e.
// QWidget coordinates of the widget passed to install(). Everything Qt asks for
// through QAccessibleInterface::rect() is in SCREEN coordinates, so each rect is
// pushed through QWidget::mapToGlobal(topLeft) and keeps its size - the same
// mapping QAccessibleWidget uses for a widget's own rectangle, and the reason a
// "click where the control says it is" probe lands on the control.
//   * devicePixelRatio is deliberately NOT applied. QWidget geometry, the
//     model's rects and QAccessible rects are all in device-INDEPENDENT pixels;
//     multiplying by the ratio would double-count a scaling that Qt already
//     folded into the widget's coordinates. The mapping is identical at
//     dpr = 1 and dpr = 2 (tests/imgui_a11y.cpp checks both).
//   * A window decoration offset is NOT added by hand either: mapToGlobal
//     already includes whatever the platform puts between the widget and the
//     screen (visible on the offscreen platform, whose frame margin moves a
//     widget asked to sit at 120,64 to 122,66).
//   * There is no QQuickWidget-style offset to handle here: the overlay draws
//     into the widget's own framebuffer, and child interfaces are not QML items
//     reporting window-relative rectangles into a second, offscreen window. That
//     trap - and its lack here - is the reason the form-accessibility rect()
//     cannot be copied verbatim.
//
// LIFETIME. install() is a process-wide singleton (one overlay viewport, one
// installation). A repeat install for the SAME widget keeps the installation -
// and therefore the interface and its id - and only takes over the callback; an
// install for a DIFFERENT widget ends the previous installation (like
// uninstall()) and starts a new one. uninstall() removes the factory AND drops
// the cached interface, so a query afterwards falls back to Qt's ordinary
// behaviour for the widget instead of answering with a stale panel. The widget
// may also be destroyed first; the interfaces then report themselves invalid
// instead of pointing at it.
//
// WHERE TO CALL IT, and why the ORDER matters. Qt keeps ONE accessible
// interface per QObject in a process-global cache, and queryAccessibleInterface()
// answers from that cache before it consults any factory. Nothing has to ask
// about the viewport for that entry to appear: QAccessible::updateAccessibility()
// resolves and caches the interface of an event's object, and QWidget raises such
// events on its own - ObjectShow when a widget is first shown, ParentChanged when
// it is (re)parented, StateChanged while windows come and go - as soon as
// accessibility is active, which is exactly the state a screen reader puts the
// application in. From that moment Qt's cache answers for the widget and no later
// factory install can be reached for it. This module will not delete that entry
// to make room (see below), so the panel would simply not be published for that
// window.
//
// installFactoryOnly() exists for that reason: call it from the viewport widget's
// CONSTRUCTOR (it needs no GL context and no model), so the entry Qt creates for
// the viewport is ours, and let install() - from the first frame - take over the
// action callback. That pairing is one installation: same interface, same id,
// nothing a client attached in between is holding gets invalidated. A constructor
// is early enough because the entry Qt makes while the widget is still being
// constructed carries the base class's metaObject and Qt keeps the two entries
// apart (that is the "not fully constructed object" case qaccessiblecache.cpp
// documents), so the interface for the finished widget is the factory's. The one
// shape this cannot cover is a widget whose accessible class is its base class -
// no Q_OBJECT - because then the construction-time entry IS the entry for the
// finished widget; the application's viewport declares Q_OBJECT.
//
// WHAT IS NOT ACHIEVED, deliberately. A viewport that something already queried
// before installFactoryOnly()/install() ran keeps answering with Qt's own
// interface: the factory is shadowed for it, the panel is not published for it,
// and install() reports that with a qWarning instead of fixing it. An id is an
// AT-SPI path (see the per-item note below), so evicting the stale entry would
// free an interface a client may already be addressing - that is the bug this
// order-of-installation requirement replaces. The same shadowing is reached from
// the other side when an installation ENDS: uninstall() removes the factory, so
// the next accessibility event Qt raises for the viewport caches the widget's own
// interface again, and installing on that widget afterwards reports the shadowing
// and publishes nothing. An overlay that is torn down and built again therefore
// relies on the constructor-time install still being in place, not on a
// re-install after uninstall().
//
// Every interface this unit creates leaves Qt's QAccessible cache - including
// the per-item children, which have no QObject and are therefore registered
// under an id of their own (that id is the AT-SPI path a client later queries,
// so a stale entry is reachable from outside the process). They leave it on
// uninstall(), when install() replaces a previous installation for a DIFFERENT
// viewport, when the viewport is destroyed first, and in Qt's own cache teardown
// walk; the same must hold for a viewport that is installed and uninstalled
// repeatedly. (A client holding such an id sees that path stop resolving once
// the installation that owned it ends - the teardown the overlay itself asks
// for, not a silent eviction of someone else's interface.)
//
// Threading: the overlay's UI thread only; the model is not copied or locked.

struct Item {
    enum Role { PushButton, CheckBox, StaticText, Image };
    Role role = StaticText;
    int id = 0;              // stable identity, handed back on activation
    QString name;            // translated, user-visible
    QString value;           // e.g. "shown"/"hidden", or a channel name
    QRect rect;              // viewport pixels
    bool checked = false;
    bool enabled = true;
};

struct Model {
    QRect panel;
    QString summary;         // e.g. "Graph: Move X, Rotate on 2 layers"
    QVector<Item> items;
};

// Install the factory for aViewport WITHOUT a model and WITHOUT GL: the
// constructor-time half of install(), for the one call site that must run before
// anything can ask Qt for the widget's interface (see "WHERE TO CALL IT" above).
//
// Publishing starts when sync() is first called; until then the viewport answers
// as an empty Pane, and no item can be activated (the model has no items, so
// there is no action to name). Calling it twice for the same widget, or calling
// install() for a widget it already covers, keeps the installation and changes
// nothing but the callback install() supplies.
void installFactoryOnly(QWidget& aViewport);

// Registers the accessible factory for aViewport and installs the model. Any
// activation of an item calls aOnAction(item.id).
//
// aOnAction is invoked synchronously from the accessibility call that performed
// the action (QAccessibleActionInterface::doAction) - a screen reader waits for
// the action to have happened. The overlay is free to queue internally; it must
// not assume the frame is unchanged when it returns.
void install(QWidget& aViewport, std::function<void(int aId)> aOnAction);

// Publish aModel as the current content. Safe to call every frame, before any
// install (does nothing), and after uninstall() (does nothing).
void sync(const Model& aModel);

// Unregister the factory and delete the interface, if any. Safe to call twice,
// and safe when the viewport was destroyed first.
void uninstall();

} // namespace a11y
} // namespace gui

#endif // GUI_IMGUIA11Y_H
