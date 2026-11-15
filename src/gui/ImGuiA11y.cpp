#include "gui/ImGuiA11y.h"

#include <QAccessible>
#include <QAccessibleActionInterface>
#include <QAccessibleEvent>
#include <QAccessibleObject>
#include <QDebug>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QWidget>
#include <QWindow>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace gui {
namespace a11y {
namespace {

// ---------------------------------------------------------------------------
// The one object an installation owns. The root interface and every child read
// THIS and keep nothing of their own, which is what makes sync() visible at
// once: there is no second copy of the model to keep in step with the overlay.
// ---------------------------------------------------------------------------
struct Published {
    QPointer<QWidget> viewport;
    std::function<void(int)> onAction;
    Model model;
    // The live root interface, or null when none is alive. The root clears this
    // in its destructor, so a pointer here is always either valid or null -
    // never dangling - including after Qt deletes the interface because the
    // widget went away first.
    QAccessibleInterface* root = nullptr;
    bool installed = false;
};

std::shared_ptr<Published> gPublished;

QWidget* viewportOf(const std::shared_ptr<Published>& aPublished) {
    return aPublished->viewport.data();
}

// AT-SPI's conventional action names for setting a checkable value. Qt offers a
// static for press ("press") and toggle ("toggle") but none for these two
// (QAccessibleActionInterface declares press, increase, decrease, showMenu,
// setFocus, toggle, scroll*, page* - that is the whole set), so they are spelled
// here and handled in doAction below, where the AT-SPI bridge passes an action
// name through verbatim.
const QString& checkActionName() {
    static const QString name = QStringLiteral("check");
    return name;
}
const QString& uncheckActionName() {
    static const QString name = QStringLiteral("uncheck");
    return name;
}

// ---------------------------------------------------------------------------
// One Item. Not a QObject, so it is a plain QAccessibleInterface rather than a
// QAccessibleObject: the panel's content is rebuilt from a model every frame and
// has no object to be found by. object() therefore answers nullptr - which is
// also REQUIRED, not merely tidy: QAccessible::uniqueId() registers an interface
// in the cache under the object it returns, so a child claiming the viewport
// would let a later QAccessible::queryAccessibleInterface(viewport) answer with
// the child and lose the panel.
//
// Children are created on demand by the root and stay alive for as long as it
// does, so a pointer handed to a bridge stays valid across sync() calls. An
// item that the model no longer contains reports isValid() == false instead of
// disappearing under the caller's feet.
//
// OWNERSHIP. object() == nullptr means there is no QObject to own this
// interface, so the root hands it to the QAccessible CACHE at creation
// (QAccessible::registerAccessibleInterface, i.e. exactly the insert(nullptr,
// iface) that the bridge's own uniqueId() would do) and the cache is what
// deletes it from then on. The root keeps only a borrowed pointer together with
// that id, and drops the ENTRY - never `delete`s the interface - so exactly one
// owner frees it in every path. See ~OverlayRoot for why that matters.
// ---------------------------------------------------------------------------
class OverlayItem : public QAccessibleInterface, public QAccessibleActionInterface {
public:
    OverlayItem(std::shared_ptr<Published> aPublished, int aIndex)
        : mPublished(std::move(aPublished)), mIndex(aIndex) {}

    // ---- state of the published model -------------------------------------
    bool isValid() const override {
        return mPublished->installed && viewportOf(mPublished) != nullptr && item() != nullptr;
    }
    QObject* object() const override { return nullptr; } // see the class comment
    QWindow* window() const override {
        QWidget* w = viewportOf(mPublished);
        return w != nullptr ? w->window()->windowHandle() : nullptr;
    }

    // ---- properties --------------------------------------------------------
    QAccessible::Role role() const override {
        const Item* it = item();
        if (it == nullptr) return QAccessible::NoRole;
        switch (it->role) {
        case Item::PushButton: return QAccessible::Button; // Qt 6: PushButton is the deprecated alias
        case Item::CheckBox: return QAccessible::CheckBox;
        case Item::StaticText: return QAccessible::StaticText;
        case Item::Image: return QAccessible::Graphic; // Qt 6 has no QAccessible::Image
        }
        return QAccessible::NoRole;
    }

    QString text(QAccessible::Text aType) const override {
        const Item* it = item();
        if (it == nullptr) return QString();
        switch (aType) {
        case QAccessible::Name:
            return it->name;
        case QAccessible::Value:
            // "shown"/"hidden" for a swatch, the channel for a label. Read as
            // TEXT: these are words, so no QAccessibleValueInterface is offered
            // (the form-accessibility precedent gives that protocol to a numeric
            // field only).
            return it->value;
        case QAccessible::Identifier:
            // The action handle, so a script can match what it just read in the
            // tree against the id an action will carry back.
            return QString::number(it->id);
        default:
            return QString();
        }
    }

    // The panel is rebuilt from the overlay's model every frame; there is
    // nothing here for a client to write into, so a write is ignored.
    void setText(QAccessible::Text, const QString&) override {}

    QAccessible::State state() const override {
        QAccessible::State s;
        const Item* it = item();
        if (it == nullptr) return s;
        const QWidget* viewport = viewportOf(mPublished);
        s.disabled = !it->enabled; // the state bits are the NEGATIVE senses
        s.invisible = viewport == nullptr || !viewport->isVisible();
        s.checkable = it->role == Item::CheckBox;
        s.checked = s.checkable && it->checked;
        s.readOnly = !hasActions();
        // The model carries no focus flag, so nothing here claims focus: see the
        // "focused channel" note in the report/header. focusChild() is null for
        // the same reason.
        s.focusable = it->enabled && hasActions();
        return s;
    }

    // ---- geometry ----------------------------------------------------------
    // The item's viewport-pixel rectangle in SCREEN coordinates: translate the
    // top-left through the widget (which already contains the device pixel ratio
    // and any window decoration offset) and keep the size. Nothing is scaled by
    // devicePixelRatio: these rectangles are device independent, like every
    // other QWidget geometry.
    QRect rect() const override {
        const Item* it = item();
        QWidget* w = viewportOf(mPublished);
        if (it == nullptr || w == nullptr) return QRect();
        return QRect(w->mapToGlobal(it->rect.topLeft()), it->rect.size());
    }

    // ---- hierarchy: a leaf -------------------------------------------------
    int childCount() const override { return 0; }
    QAccessibleInterface* child(int) const override { return nullptr; }
    int indexOfChild(const QAccessibleInterface*) const override { return -1; }
    QAccessibleInterface* childAt(int, int) const override { return nullptr; }
    QAccessibleInterface* focusChild() const override { return nullptr; }
    QAccessibleInterface* parent() const override { return mPublished->root; }

    // ---- protocols ---------------------------------------------------------
    void* interface_cast(QAccessible::InterfaceType aType) override {
        // Only what can be pressed advertises the action protocol; a label or an
        // image that did would offer a screen reader an action that does
        // nothing.
        if (aType == QAccessible::ActionInterface && hasActions())
            return static_cast<QAccessibleActionInterface*>(this);
        return nullptr;
    }

    QStringList actionNames() const override {
        const Item* it = item();
        if (it == nullptr) return {};
        switch (it->role) {
        case Item::CheckBox:
            // Measured from Qt itself: a real QCheckBox advertises "Toggle",
            // "Press" (and SetFocus - which would be a lie here, because the
            // overlay's items are not Qt focus scopes), so those two come first.
            // AT-SPI's "check"/"uncheck" follow because that is the vocabulary a
            // screen reader and this repository's own AT-SPI probes
            // (scripts/gui/probe_exportbg.sh) use for a checkable value. All four
            // are handled below; check/uncheck are offered, and fire, only when
            // they would actually change the value.
            return {QAccessibleActionInterface::toggleAction(),
                    QAccessibleActionInterface::pressAction(),
                    checkActionName(),
                    uncheckActionName()};
        case Item::PushButton:
            return {QAccessibleActionInterface::pressAction()};
        default:
            return {};
        }
    }

    void doAction(const QString& aActionName) override {
        const Item* it = item();
        if (it == nullptr || !it->enabled) return;
        // Read everything this call needs OUT of the model before handing
        // control to the overlay: aOnAction may call sync() from inside this
        // call, which replaces the model and invalidates `it`.
        const int id = it->id;
        const bool checked = it->checked;
        const bool checkable = it->role == Item::CheckBox;
        if (aActionName == QAccessibleActionInterface::pressAction()
            || aActionName == QAccessibleActionInterface::toggleAction()) {
            activate(id);
        } else if (checkable && aActionName == checkActionName()) {
            if (!checked) activate(id);
        } else if (checkable && aActionName == uncheckActionName()) {
            if (checked) activate(id);
        }
    }

    QStringList keyBindingsForAction(const QString&) const override { return {}; }

private:
    const Item* item() const {
        const QVector<Item>& items = mPublished->model.items;
        return mIndex >= 0 && mIndex < items.size() ? &items.at(mIndex) : nullptr;
    }

    bool hasActions() const {
        const Item* it = item();
        return it != nullptr && (it->role == Item::PushButton || it->role == Item::CheckBox);
    }

    void activate(int aId) const {
        if (!mPublished->installed) return;
        if (mPublished->onAction) mPublished->onAction(aId);
    }

    std::shared_ptr<Published> mPublished;
    int mIndex;
};

// ---------------------------------------------------------------------------
// The viewport itself: the panel's container, and the parent of every item.
// ---------------------------------------------------------------------------
class OverlayRoot : public QAccessibleObject {
public:
    OverlayRoot(QWidget* aViewport, std::shared_ptr<Published> aPublished)
        : QAccessibleObject(aViewport), mPublished(std::move(aPublished)) {
        mPublished->root = this;
    }

    ~OverlayRoot() override {
        // A child may outlive this interface (a bridge holding one when the
        // factory is removed); its parent() must then answer null, not a
        // dangling pointer. FIRST, because dropping the entries below can deliver
        // an ObjectDestroyed event to the bridge (QAccessibleCache::deleteInterface
        // sends one for an interface that has no object), and that bridge asks
        // the dying interface for its parent() - which must not be a root that is
        // already half-destroyed.
        if (mPublished->root == this) mPublished->root = nullptr;
        dropChildren();
    }

    bool isValid() const override { return mPublished->installed && QAccessibleObject::isValid(); }

    QWindow* window() const override {
        QWidget* w = viewportOf(mPublished);
        return w != nullptr ? w->window()->windowHandle() : nullptr;
    }

    // ---- properties --------------------------------------------------------
    // A container role, which is what the panel is: a named region inside the
    // viewport that holds the items a reader walks.
    QAccessible::Role role() const override { return QAccessible::Pane; }

    QString text(QAccessible::Text aType) const override {
        // The panel's summary is the one line that says what the overlay shows
        // ("Graph: Move X, Rotate on 2 layers"), so it is the panel's name -
        // the same rule the form-accessibility port applies to a combo box,
        // whose name is its current value.
        return aType == QAccessible::Name ? mPublished->model.summary : QString();
    }

    void setText(QAccessible::Text, const QString&) override {}

    QAccessible::State state() const override {
        QAccessible::State s;
        QWidget* w = viewportOf(mPublished);
        s.disabled = w == nullptr || !w->isEnabled();
        s.invisible = w == nullptr || !w->isVisible();
        s.focusable = w != nullptr && w->focusPolicy() != Qt::NoFocus;
        s.focused = w != nullptr && w->hasFocus();
        return s;
    }

    // The panel's rectangle, in screen coordinates. Model::panel is the region
    // the overlay actually draws in; an empty or unset panel falls back to the
    // whole viewport, so the interface always has a rectangle that contains its
    // children instead of an empty one.
    QRect rect() const override {
        QWidget* w = viewportOf(mPublished);
        if (w == nullptr) return QRect();
        const QRect panel = mPublished->model.panel;
        const QRect local = panel.isValid() ? panel : w->rect();
        return QRect(w->mapToGlobal(local.topLeft()), local.size());
    }

    // ---- hierarchy ---------------------------------------------------------
    // No structural child: the precedent (QmlFormAccessibility) adds none, and
    // the panel's own summary is already this object's name, so a reader hears
    // it without having to walk into a header item. childCount() is therefore
    // exactly Model::items.size().
    int childCount() const override { return int(mPublished->model.items.size()); }

    QAccessibleInterface* child(int aIndex) const override {
        if (aIndex < 0 || aIndex >= mPublished->model.items.size()) return nullptr;
        while (int(mChildren.size()) <= aIndex) mChildren.push_back(Child());
        Child& slot = mChildren[size_t(aIndex)];
        if (slot.iface == nullptr) {
            slot.iface = new OverlayItem(mPublished, aIndex);
            // QAccessible::registerAccessibleInterface is the documented call for
            // exactly this shape - "when it is necessary to reimplement
            // QAccessibleInterface::child() and return the child after
            // constructing it" - and it "ensures that manually created interfaces
            // are properly memory managed": it puts the interface in the cache
            // (insert(nullptr, iface), since object() is null) and from then on
            // the CACHE is what deletes it, which is what ~OverlayRoot relies on.
            // Called exactly once per interface, as documented. A bridge that
            // later asks for the id (QAccessible::uniqueId, which is what AT-SPI's
            // pathForInterface does) gets this same id back instead of adding a
            // second entry for the same interface.
            slot.id = QAccessible::registerAccessibleInterface(slot.iface);
        }
        return slot.iface;
    }

    int indexOfChild(const QAccessibleInterface* aChild) const override {
        for (size_t i = 0; i < mChildren.size(); ++i)
            if (mChildren[i].iface == aChild) return int(i);
        return -1;
    }

    // QAccessible's childAt takes SCREEN coordinates, like rect() returns.
    QAccessibleInterface* childAt(int x, int y) const override {
        QWidget* w = viewportOf(mPublished);
        if (w == nullptr) return nullptr;
        const QPoint local = w->mapFromGlobal(QPoint(x, y));
        const QVector<Item>& items = mPublished->model.items;
        // Backwards: the panel is drawn in model order, so a later item lies on
        // top of an earlier one.
        for (int i = int(items.size()) - 1; i >= 0; --i)
            if (items.at(i).rect.contains(local)) return child(i);
        return nullptr;
    }

    QAccessibleInterface* focusChild() const override { return nullptr; } // no focus flag in Model

    QAccessibleInterface* parent() const override {
        // The widget hierarchy, as QAccessibleWidget does it. A top-level
        // viewport has no widget parent and answers null; Qt reaches it through
        // the window instead.
        QWidget* w = viewportOf(mPublished);
        if (w == nullptr) return nullptr;
        QWidget* p = w->parentWidget();
        return p != nullptr ? QAccessible::queryAccessibleInterface(p) : nullptr;
    }

private:
    // One child interface plus the id it lives under in the QAccessible cache.
    // The CACHE owns the interface; this slot only borrows the pointer, so it
    // must never be deleted from here (see dropChildren()).
    struct Child {
        OverlayItem* iface = nullptr;
        QAccessible::Id id = 0;
    };

    // Drop every child's cache entry. This is the one place where the interfaces
    // this unit created leave the cache, and it runs in every path that ends an
    // installation - uninstall(), install() for a different viewport (which ends
    // the previous installation first), the viewport widget being destroyed first
    // (Qt's cache deletes the root along with the widget) and Qt's own cache
    // teardown walk - because all of them destroy this root.
    //
    // WHY IT MUST HAPPEN HERE. A child's id IS the AT-SPI path of that child
    // (atspiadaptor's pathForInterface hands the id out as the path, and the
    // bridge resolves a path back with QAccessible::accessibleInterface), so an
    // id that outlives its interface is reachable from outside the process:
    // every later query on it lands on freed memory, and Qt's own cache
    // destructor - which deletes whatever ids are left, in hash order - deletes
    // that interface a second time.
    //
    // Dropping the entry is what deletes the interface - the cache is its memory
    // manager (that is what QAccessible::registerAccessibleInterface promises) -
    // so the pointer must not be freed here as well. Only the id is read, never
    // the pointer: a slot whose interface Qt's teardown walk already deleted (it
    // can reach a child before this root) resolves to nothing, and
    // deleteAccessibleInterface() ignores an id the cache no longer has. So the
    // order that walk happens to pick cannot produce a double free.
    void dropChildren() {
        for (Child& child : mChildren) {
            const QAccessible::Id id = child.id;
            child = Child(); // the cache owns it from here; never hand this slot out
            if (id != 0) QAccessible::deleteAccessibleInterface(id);
        }
    }

    std::shared_ptr<Published> mPublished;
    // Created on demand and kept: a bridge may hold a child across sync() calls,
    // so a child is never dropped while the root lives. A child the model no
    // longer has reports itself invalid instead.
    mutable std::vector<Child> mChildren;
};

// ---------------------------------------------------------------------------
// The factory. It sees every object in the process, so the test is an identity
// compare against the one installed viewport and everything else falls through
// to Qt.
// ---------------------------------------------------------------------------
QAccessibleInterface* overlayFactory(const QString&, QObject* aObject) {
    const std::shared_ptr<Published>& published = gPublished;
    if (!published || !published->installed) return nullptr;
    if (aObject == nullptr || aObject != published->viewport) return nullptr;
    // Qt caches one interface per object, so this normally runs once; returning
    // the live root rather than a second one keeps Published::root honest.
    if (published->root != nullptr) return published->root;
    return new OverlayRoot(static_cast<QWidget*>(aObject), published);
}

// ---------------------------------------------------------------------------
// The one thing install() does about the viewport's cache entry: ASK for it.
//
// Qt answers from its cache BEFORE it consults any factory
// (QAccessible::queryAccessibleInterface, qaccessible.cpp: queryAccessibleInterface()
// returns QAccessibleCache::interfaceForId(idForObject(object)) the moment the
// object has an entry), so this call is both
//   * the point where OUR root interface comes into existence and is registered
//     under the widget - the factory is what answers for a widget nothing has
//     asked about before, and
//   * the only way to notice the case where someone asked first and Qt answered
//     with the widget's ordinary interface instead.
//
// That second case is REPORTED, never "fixed" by deleting the entry. The reason
// is the whole point of this module's history: an id is an AT-SPI path. The
// bridge turns every interface it hands out into a path by taking its id
// (AtSpiAdaptor::pathForInterface calls QAccessible::uniqueId, atspiadaptor.cpp),
// and a client can be holding that path - the bridge itself, when it meets an
// object, and the a11y registry, which learns the tree from the bridge's signals
// and keeps answering from it. QAccessibleCache::deleteInterface() erases the id
// AND frees the interface (qaccessiblecache.cpp, deleteInterface: idToInterface.erase,
// interfaceToId.take, delete iface), so deleting a stale entry under a client that
// already knows that id leaves the client addressing a path Qt can no longer
// resolve: AtSpiAdaptor::handleMessage() finds no interface for it and returns
// without a reply ("Could not find accessible on path"), and the node the client
// believes holds the panel never enumerates a child again. Losing the panel for a
// widget that was queried too early is a bad outcome; deleting memory a client
// holds is a worse one, and it is also unfixable from here.
//
// So: leave the entry alone and say so. The caller avoids the situation entirely
// by installing the factory before anything can query the viewport.
void adoptViewportInterface(QWidget* aViewport) {
    QAccessibleInterface* cached = QAccessible::queryAccessibleInterface(aViewport);
    if (cached == nullptr || dynamic_cast<OverlayRoot*>(cached) != nullptr) return;
    qWarning("gui::a11y: the viewport was queried for accessibility before install(), so "
             "Qt's cache answers for it with the widget's own interface; the overlay factory "
             "cannot be reached for that widget and the ImGui panel is not published for it. "
             "Install the factory from the widget's constructor (gui::a11y::installFactoryOnly) "
             "instead of from its first frame. The stale entry is deliberately left in place: "
             "a client may be holding the id it lives under.");
}

// ---------------------------------------------------------------------------
// Change detection for sync(): every frame goes through this, so it allocates
// nothing and stops at the first structural difference.
// ---------------------------------------------------------------------------
enum class Change { None, Content, Structure };

Change compare(const Model& aBefore, const Model& aAfter) {
    Change change = Change::None;
    if (aBefore.summary != aAfter.summary || aBefore.panel != aAfter.panel) change = Change::Content;
    if (aBefore.items.size() != aAfter.items.size()) return Change::Structure;
    for (int i = 0; i < aBefore.items.size(); ++i) {
        const Item& before = aBefore.items.at(i);
        const Item& after = aAfter.items.at(i);
        if (before.id != after.id || before.role != after.role) return Change::Structure;
        if (before.name != after.name || before.value != after.value || before.rect != after.rect
            || before.checked != after.checked || before.enabled != after.enabled)
            change = Change::Content;
    }
    return change;
}

} // namespace

void installFactoryOnly(QWidget& aViewport) {
    // Already this widget's installation: nothing to do, and nothing to end.
    if (gPublished && viewportOf(gPublished) == &aViewport) return;
    if (gPublished) uninstall(); // a DIFFERENT viewport: that installation ends here
    auto published = std::make_shared<Published>();
    published->viewport = &aViewport;
    published->installed = true; // no callback yet; the model is empty until the first sync()
    gPublished = published;
    QAccessible::installFactory(overlayFactory);
    // Deliberately NO query for the interface here, unlike install(). This call's
    // whole purpose is to run from the widget's CONSTRUCTOR, and Qt's cache keys
    // an entry by the metaObject the object had when the entry was made: an
    // interface created while the widget is still being constructed is stored
    // with the base class's metaObject, and after construction the same object
    // no longer matches its own entry, so Qt makes a SECOND one (that is exactly
    // the case QAccessibleCache::objectDestroyed documents, qaccessiblecache.cpp).
    // Two live roots for one widget is a state this module must not create, so
    // the root is left to the first real query - which reaches this factory
    // because a widget that is not shown yet has not been asked about.
}

void install(QWidget& aViewport, std::function<void(int)> aOnAction) {
    // A second install for the SAME widget must NOT tear the live installation
    // down: the root interface it made, and the id that interface lives under,
    // are the AT-SPI path a client may already be holding (see
    // adoptViewportInterface above), so replacing them here is the very failure
    // this path must not cause. A repeat install only takes over the callback -
    // which is what makes installFactoryOnly() (the widget's constructor) and
    // install() (its first frame) one installation, with one interface and one
    // id, no matter who attached in between.
    if (gPublished) {
        if (viewportOf(gPublished) == &aViewport) {
            gPublished->onAction = std::move(aOnAction);
            gPublished->installed = true;
            return;
        }
        uninstall(); // a DIFFERENT viewport: that installation ends here
    }
    auto published = std::make_shared<Published>();
    published->viewport = &aViewport;
    published->onAction = std::move(aOnAction);
    published->installed = true;
    gPublished = published;
    QAccessible::installFactory(overlayFactory);
    adoptViewportInterface(&aViewport);
}

void sync(const Model& aModel) {
    const std::shared_ptr<Published> published = gPublished;
    if (!published) return;
    if (!QAccessible::isActive()) {
        // Nothing is listening: publishing the frame through AT-SPI would be
        // work for nobody, and a query that arrives later reads the model below.
        published->model = aModel;
        return;
    }
    const Change change = compare(published->model, aModel);
    published->model = aModel;
    if (change == Change::None) return;
    if (QAccessibleInterface* root = published->root) {
        // A changed child set is a different tree (ObjectReorder, which tells a
        // reader to re-read the children); anything else is the same tree with
        // different content.
        QAccessibleEvent event(root, change == Change::Structure ? QAccessible::ObjectReorder
                                                                : QAccessible::VisibleDataChanged);
        QAccessible::updateAccessibility(&event);
    }
}

void uninstall() {
    if (!gPublished) return;
    const std::shared_ptr<Published> published = gPublished;
    gPublished.reset();
    published->installed = false;   // children of any surviving interface report invalid
    published->onAction = nullptr;  // nothing may activate after this point
    published->model = Model();
    QAccessible::removeFactory(overlayFactory);
    if (QAccessibleInterface* root = published->root) {
        // Also removes the widget's cache entry (verified: the next query gets
        // Qt's ordinary interface for the widget, not this one), and deletes the
        // interface - whose destructor clears published->root AND drops every
        // child's cache entry before those children are freed, so no id in the
        // cache is left pointing at freed memory. Nothing here points at freed
        // memory afterwards either.
        const QAccessible::Id id = QAccessible::uniqueId(root);
        if (id != 0) QAccessible::deleteAccessibleInterface(id);
    }
}

} // namespace a11y
} // namespace gui
