#include "gui/qml/QmlFormAccessibility.h"

#include <QAccessible>
#include <QAccessibleActionInterface>
#include <QAccessibleObject>
#include <QAccessibleValueInterface>
#include <QMetaObject>
#include <QPointF>
#include <QQuickItem>
#include <QRect>
#include <QSize>
#include <QStringList>
#include <QVariant>
#include <QtGlobal>

namespace gui {
namespace {

// The marker objectNames the QML sets on the items that need a protocol Qt Quick
// cannot express. A factory sees EVERY object in the process, so the checks have
// to be cheap identity tests and everything else has to fall through to Qt.
constexpr auto kValueMarker = "aeAccessibleValue";
constexpr auto kComboMarker = "aeAccessibleCombo";
constexpr auto kOptionMarker = "aeAccessibleOption";
// A disclosure (the filter card's header): press toggles the card. It exists because
// the header is a MouseArea - not a keyboard stop, and with no accessibility protocol
// of its own - so without this marker a card could not be expanded by anything but a
// mouse, and no probe could expand one either.
constexpr auto kDisclosureMarker = "aeAccessibleDisclosure";
// A TREE ROW: the animation dock's object tree is a Qt Quick ListView, and a Qt Quick item's
// own accessible object reports itself INVISIBLE in this application (the reason the inspector
// needed this factory at all), which would hide every layer from a screen reader and from the
// probe suite. The row publishes role Cell - AT-SPI's "table cell", the role the widget tree
// published - with the states a layer list has (selected, checkable/checked for visibility,
// expandable/expanded) and Press.
constexpr auto kRowMarker = "aeAccessibleRow";

enum class Kind { Value, Combo, Option, Disclosure, Row };

class FormAccessible : public QAccessibleObject,
                       public QAccessibleValueInterface,
                       public QAccessibleActionInterface {
public:
    FormAccessible(QQuickItem* aItem, Kind aKind)
        : QAccessibleObject(aItem), mKind(aKind) {}

    QQuickItem* item() const { return static_cast<QQuickItem*>(object()); }

    // ---- QAccessibleInterface -------------------------------------------------
    QAccessible::Role role() const override {
        switch (mKind) {
        case Kind::Value: return QAccessible::SpinBox;
        case Kind::Combo: return QAccessible::ComboBox;
        case Kind::Option: return QAccessible::ListItem;
        case Kind::Disclosure: return QAccessible::Button;
        case Kind::Row: return QAccessible::Cell;
        }
        return QAccessible::NoRole;
    }

    QString text(QAccessible::Text aType) const override {
        const QString label = item()->property("accessibleName").toString();
        const QString value = item()->property("accessibleValue").toString();
        switch (aType) {
        case QAccessible::Name:
            // A combo reports its current VALUE as its name - Qt's own rule, and
            // what `ae value` on a combo reads. A field and an option are named by
            // their words.
            return mKind == Kind::Combo ? value : label;
        case QAccessible::Description:
            // The identity lives here for a combo, whose name is its value.
            return mKind == Kind::Combo ? label : QString();
        case QAccessible::Value:
            return value;
        default:
            return QString();
        }
    }

    QAccessible::State state() const override {
        QAccessible::State s;
        s.focusable = true;
        s.focused = item()->hasActiveFocus();
        // The state bits are the NEGATIVE senses, as QAccessible defines them.
        s.invisible = !item()->isVisible();
        s.disabled = !item()->isEnabled();
        if (mKind == Kind::Row) {
            // A row's own facts, from the QML properties the delegate binds to its model:
            // the ListView is the keyboard holder, so the row is selectable but never the
            // focused object.
            s.focused = false;
            // a row is selectable unless the SCENE says otherwise: the tree's document row is
            // the document, not a layer, and selecting it emptied the inspector
            s.selectable = item()->property("accessibleSelectable").toBool();
            s.selected = item()->property("accessibleSelected").toBool();
            s.expandable = item()->property("accessibleHasChildren").toBool();
            s.expanded = s.expandable && item()->property("accessibleExpanded").toBool();
            s.checkable = item()->property("accessibleCheckable").toBool();
            s.checked = s.checkable && item()->property("accessibleChecked").toBool();
        }
        return s;
    }

    int childCount() const override { return 0; }
    QAccessibleInterface* child(int) const override { return nullptr; }
    int indexOfChild(const QAccessibleInterface*) const override { return -1; }
    QAccessibleInterface* focusChild() const override { return nullptr; }

    QAccessibleInterface* parent() const override {
        return QAccessible::queryAccessibleInterface(item()->parent());
    }

    // QAccessibleObject::rect() is empty for a plain QObject, and Qt's QML items
    // report a WINDOW-relative rectangle even for the SCREEN coordinate type. A
    // QQuickItem knows its true global position, so that is what a probe clicks and
    // a magnifier frames.
    QRect rect() const override {
        const QPointF topLeft = item()->mapToGlobal(QPointF(0, 0));
        return QRect(topLeft.toPoint(),
                     QSize(qRound(item()->width()), qRound(item()->height())));
    }

    void* interface_cast(QAccessible::InterfaceType aType) override {
        // Only the numeric field has a value: a combo reports its value as its
        // NAME, and advertising a Value interface for it would make a tool read a
        // string through a numeric protocol.
        if (aType == QAccessible::ValueInterface && mKind == Kind::Value)
            return static_cast<QAccessibleValueInterface*>(this);
        if (aType == QAccessible::ActionInterface)
            return static_cast<QAccessibleActionInterface*>(this);
        return QAccessibleObject::interface_cast(aType);
    }

    // ---- QAccessibleValueInterface (Kind::Value only) -------------------------
    QVariant currentValue() const override {
        return item()->property("accessibleValue");
    }
    QVariant maximumValue() const override {
        return item()->property("accessibleMaximum");
    }
    QVariant minimumValue() const override {
        return item()->property("accessibleMinimum");
    }
    QVariant minimumStepSize() const override {
        return item()->property("accessibleStep");
    }
    void setCurrentValue(const QVariant& aValue) override {
        // Through the item's own QML entry point, so the field's "value was
        // edited" path (and the model behind it) is the same one a typed edit
        // uses. Writing the property directly would update the pixels and nothing
        // else.
        QMetaObject::invokeMethod(item(), "accessibleSetValue",
                                  Q_ARG(QVariant, aValue));
    }

    // ---- QAccessibleActionInterface -------------------------------------------
    // "Press" means what a click means, per control: edit the number, open/close
    // the list, choose the item.
    QStringList actionNames() const override { return {pressAction()}; }
    void doAction(const QString& aActionName) override {
        if (aActionName != pressAction()) return;
        switch (mKind) {
        case Kind::Value:
            QMetaObject::invokeMethod(item(), "accessibleBeginEdit");
            break;
        case Kind::Combo:
            QMetaObject::invokeMethod(item(), "accessiblePress");
            break;
        case Kind::Option:
            QMetaObject::invokeMethod(item(), "accessibleChoose");
            break;
        case Kind::Disclosure:
            QMetaObject::invokeMethod(item(), "accessiblePress");
            break;
        case Kind::Row:
            QMetaObject::invokeMethod(item(), "accessiblePress");
            break;
        }
    }
    QStringList keyBindingsForAction(const QString&) const override {
        return {};
    }

private:
    Kind mKind;
};

QAccessibleInterface* formFactory(const QString&, QObject* aObject) {
    auto* item = qobject_cast<QQuickItem*>(aObject);
    if (item == nullptr) return nullptr;
    const QString name = item->objectName();
    if (name == QLatin1String(kValueMarker)) return new FormAccessible(item, Kind::Value);
    if (name == QLatin1String(kComboMarker)) return new FormAccessible(item, Kind::Combo);
    if (name == QLatin1String(kOptionMarker)) return new FormAccessible(item, Kind::Option);
    if (name == QLatin1String(kDisclosureMarker))
        return new FormAccessible(item, Kind::Disclosure);
    if (name == QLatin1String(kRowMarker)) return new FormAccessible(item, Kind::Row);
    return nullptr;
}

} // namespace

void installQmlFormAccessibility() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    QAccessible::installFactory(formFactory);
}

} // namespace gui
