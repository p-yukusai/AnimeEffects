#ifndef GUI_QML_QMLFORMACCESSIBILITY_H
#define GUI_QML_QMLFORMACCESSIBILITY_H

namespace gui {

// Give the export form's hand-rolled Qt Quick controls the accessibility
// protocols Qt only provides for C++ widgets.
//
// WHY THIS EXISTS. Qt Quick's `Accessible` attached type can name a control, give
// it a role and expose a press action - but it has no `value` property, and Qt
// only exposes AT-SPI's Value interface for C++ value controls. So the port of
// the export dialog REGRESSED three things the deleted widget form had:
//
//   * a numeric field could not be read or SET through the standard value
//     protocol (a screen reader could not adjust it, and scripts/gui/ae_ui.py's
//     `set`/`value` could not reach it);
//   * a combo box's items could not be chosen: Qt exposes a press action for
//     Button/CheckBox/RadioButton/Switch roles, NOT for a list item, so a screen
//     reader could open the list and then not pick from it;
//   * and Qt reports a QML item's screen rectangle in the QQuickWidget's
//     coordinates, so "click where the control says it is" lands beside it.
//
// A QAccessible factory is the documented answer for a non-widget accessible
// object. It recognises three marker items the QML sets objectName on - the value
// field (AeNumberField), the combo field and its options (AeComboBox) - and hands
// back an interface that implements the missing protocols: a Value interface for
// the field, a press action for all three, and a rect() taken from the item's own
// mapToGlobal, which is screen-correct.
//
// Idempotent, and safe to call from any constructor after QApplication exists.
void installQmlFormAccessibility();

// KNOWN UPSTREAM DEFECT, deliberately not worked around: a dialog that hosts a
// QQuickWidget appears TWICE to assistive technology (and to scripts/gui/ae_ui.py,
// which is why an accessibility walk here costs about twice what it should).
//
// A QQuickWidget renders through an internal offscreen QQuickWindow that is never
// presented - no platform window - but it still sits in
// QGuiApplication::topLevelWindows(), and QAccessibleApplication builds its child list
// from the accessible ROOT OF EVERY TOP-LEVEL WINDOW (qtbase
// src/gui/accessible/qaccessibleobject.cpp, topLevelObjects()). Since qtdeclarative
// change I71dc7874 ("QQuickWidgetOffscreenWindow: override accessibleRoot to return
// the widget", in 6.11.2 and every branch since) that window answers with the WIDGET,
// which is already a child of its dialog: one accessible object, two parents.
//
// That is QTBUG-149451 (P1: Critical, Reported, no fix version, unchanged in 6.12 and
// dev). The duplicate is redundant rather than wrong, and the only local fix is to
// parent Qt's private offscreen window to the host - verified to work, and rejected
// because it would tie this dialog to an implementation detail of Qt. Revisit when the
// bug is fixed.

} // namespace gui

#endif // GUI_QML_QMLFORMACCESSIBILITY_H
