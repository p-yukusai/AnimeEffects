#ifndef GUI_QMLHOST_H
#define GUI_QMLHOST_H

#include <QApplication>
#include <QFont>
#include <QKeyEvent>
#include <QQuickWidget>
#include <QQuickWindow>

#include "gui/LocaleParam.h"

namespace gui {

// The dock's font, from the locale's own family/size setting.
//
// THE DOCK IS NOT THE PROCESS. QApplication::font() is the platform default (9pt
// here) while the docks draw at the locale's 8pt Geist: widgets get that through
// standard.ssa, a Qt Quick scene does not, because QML's Qt.application.font IS
// QApplication::font(). So every QML dock is handed this font as a context property
// (uiFont) and its text sets `font: uiFont`. Measured on the first inspector pass:
// using `Qt.application.font` left every row at 9pt, and 12px literals were neither
// the dock's size nor its family.
inline QFont qmlUiFont(const LocaleParam& aLocale) {
    QFont font(QApplication::font());
    if (!aLocale.fontFamily.isEmpty())
        font.setFamily(aLocale.fontFamily);
    const QString size = aLocale.fontSize.trimmed();
    bool ok = false;
    if (size.endsWith(QStringLiteral("pt"), Qt::CaseInsensitive)) {
        const qreal pt = size.left(size.size() - 2).toDouble(&ok);
        if (ok) font.setPointSizeF(pt);
    } else if (size.endsWith(QStringLiteral("px"), Qt::CaseInsensitive)) {
        const int px = size.left(size.size() - 2).toInt(&ok);
        if (ok) font.setPixelSize(px);
    } else if (!size.isEmpty()) {
        const qreal pt = size.toDouble(&ok);
        if (ok) font.setPointSizeF(pt);
    }
    return font;
}

// A QQuickWidget that hands the application the keys its scene has no use for.
//
// QQuickWidget keeps Qt's default Qt::StrongFocus (QQuickWidgetPrivate::init calls
// setFocusPolicy) and its keyPressEvent does exactly one thing: it sends the SAME
// event to the offscreen QQuickWindow (qquickwidget.cpp, Qt 6.12) - it never calls
// QWidget::keyPressEvent. That is correct while the scene has an active focus item:
// QQuickDeliveryAgentPrivate::deliverKeyEvent gives the key to it. With NO
// activeFocusItem it does nothing at all, and the event is still accepted (the
// offscreen window's event() returns true and the key arrives here accepted), so
// QApplication::notify's propagation walk
//
//     bool def = key->isAccepted();
//     while (w) {
//         if (def) key->accept(); else key->ignore();
//         res = d->notify_helper(w, e);
//         if (res && key->isAccepted()) break;
//         if (!pr || w->isWindow()) break;
//         w = w->parentWidget();
//     }
//
// stops at this widget: the rest of the walk, and MainWindow::keyPressEvent with
// it, is never reached. The effect: the whole rebindable key map is dead while a
// QML dock holds focus - Ctrl+Z, Ctrl+S, every binding - because a key pressed over
// the panel is delivered to an empty scene and dropped.
//
// So the scene gets the event FIRST, and only when it has nothing focused to give it
// to do we mark it ignored and let Qt walk on up to the window. The guard is not
// politeness: with an active focus item the scene's answer stands, and a TextInput's
// own Ctrl+Z (its text undo) leaves the event accepted, so the walk stops at the
// field the user is typing into. A focused item that REJECTS a key still lets it
// through, exactly as it always has - that behaviour is QQuickWidget's, and this
// class neither adds to it nor takes it away.
//
// Measured on the Properties dock (the first host to need it): every rebindable
// command was dead while the inspector held focus, and scripts/gui/probe_undo_key.sh
// is its gate.
class KeyForwardingQuickWidget: public QQuickWidget {
public:
    using QQuickWidget::QQuickWidget;

protected:
    void keyPressEvent(QKeyEvent* aEvent) override {
        QQuickWidget::keyPressEvent(aEvent);
        forwardWhenSceneHasNoFocus(aEvent);
    }
    void keyReleaseEvent(QKeyEvent* aEvent) override {
        QQuickWidget::keyReleaseEvent(aEvent);
        forwardWhenSceneHasNoFocus(aEvent);
    }

private:
    // Both halves of the pair matter: MainWindow::keyReleaseEvent is what releases a
    // command held down through the key map (MoveCanvas, RotateCanvas), so a
    // swallowed release would leave one armed forever.
    void forwardWhenSceneHasNoFocus(QKeyEvent* aEvent) {
        QQuickWindow* window = quickWindow();
        if (window && window->activeFocusItem())
            return;
        aEvent->ignore();
    }
};

} // namespace gui

#endif // GUI_QMLHOST_H
