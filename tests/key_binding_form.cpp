// The key-bindings dialog, headless: what the model does with a key press, and what
// the scene GIVES it.
//
// The dialog used to be a QTabWidget of QFormLayouts whose rows were guarded
// QLineEdits, so its rules could only be driven through a shown widget. It is a Qt
// Quick scene now, and every rule the form enforces lives on
// gui::KeyBindingViewModel - the same object the scene binds to. So both halves of the
// contract can be asked with no display, no window manager and no widget:
//
//   * the MODEL: the capture rules, one per assertion - Delete/Backspace clears, a
//     bare key after a fresh chord becomes its sub-key, a valid chord replaces, a
//     focus loss ends the sub-key window, an auto-repeat is ignored, two rows that
//     share a chord label each other both ways, and resetAll() restores the shipped
//     defaults;
//   * the SCENE: the REAL KeyBindingDialog.qml compiles, its root is an item, and a
//     key injected into a row's chord field actually reaches the model (the field's
//     Keys.onPressed is the wiring, so poking the model would prove nothing).
//
// The key injection is the picker_keyboard recipe: measured, a key sent straight to a
// Qt Quick item reaches the item's Keys filter with no window and no active focus, so
// the filter consumes it. The field lives in a ListView delegate, though, and a
// delegate only exists once the view has a size - hence the offscreen window, the
// size set on the root, and itemAtIndex(0).
//
// Run: QT_QPA_PLATFORM=offscreen ./build/tests/Debug/key_binding_form
#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QUrl>

#include "ctrl/KeyBinding.h"
#include "gui/qml/KeyBindingViewModel.h"
#include "gui/qml/QmlTheme.h"
#include "gui/theme/Icons.h"

using Model = gui::KeyBindingViewModel;

static int failures = 0;

static void check(bool ok, const QString& what) {
    QTextStream(stdout) << (ok ? "PASS  " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

// One key straight at the item. Returns whether it was accepted, which the scene
// asserts (its field consumes everything), never the model claim by itself.
static bool sendKey(QObject* aItem, int aKey, Qt::KeyboardModifiers aModifiers = Qt::NoModifier) {
    QKeyEvent event(QEvent::KeyPress, aKey, aModifiers);
    QCoreApplication::sendEvent(aItem, &event);
    return event.isAccepted();
}

// Pump the event loop so a window's view lays its delegates out before they are read.
static void spin(int aMs = 250) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < aMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // The application's style, so the scene builds the same controls it ships.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // =============================================================== the capture rules
    // A small fixture with the shapes that matter: a single chord (Ctrl+Z), a
    // modified chord, and two rows that a test can drive onto the same chord.
    Model model;
    model.addRow(
        QStringLiteral("Undo"), QStringLiteral("General"), QStringLiteral("Undo last action"),
        ctrl::KeyBinding(Qt::Key_Z, Qt::ControlModifier), ctrl::KeyBinding(Qt::Key_Z, Qt::ControlModifier));
    model.addRow(
        QStringLiteral("Redo"), QStringLiteral("General"), QStringLiteral("Redo last action"),
        ctrl::KeyBinding(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier),
        ctrl::KeyBinding(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier));
    model.addRow(
        QStringLiteral("Copy"), QStringLiteral("Timeline"), QStringLiteral("Copy selected key(s)"),
        ctrl::KeyBinding(Qt::Key_C, Qt::ControlModifier), ctrl::KeyBinding(Qt::Key_C, Qt::ControlModifier));
    model.addRow(
        QStringLiteral("Paste"), QStringLiteral("Timeline"), QStringLiteral("Paste selected key(s)"),
        ctrl::KeyBinding(Qt::Key_V, Qt::ControlModifier), ctrl::KeyBinding(Qt::Key_V, Qt::ControlModifier));

    check(model.count() == 4 && model.rowCount() == 4, "the fixture seeds four rows");
    check(model.groups() == (QStringList{QStringLiteral("General"), QStringLiteral("Timeline")}),
          "the groups are the distinct groups in command order");
    check(model.maxGroupRows() == 2, "the tallest group has two rows (the scene sizes to it)");
    check(model.keyAt(2) == QLatin1String("Copy") && model.groupAt(2) == QLatin1String("Timeline") &&
              model.labelAt(2) == QLatin1String("Copy selected key(s)"),
          "key/group/label round-trip for a row");
    check(model.rowForLabel(QStringLiteral("Copy selected key(s)")) == 2,
          "rowForLabel finds the row by its label");
    check(model.rowForLabel(QStringLiteral("no such command")) == -1, "and misses cleanly");

    // The role names the scene's delegate binds; a missing one renders an empty row.
    const QHash<int, QByteArray> roles = model.roleNames();
    check(roles.values().contains(QByteArrayLiteral("group")) && roles.values().contains(QByteArrayLiteral("label")) &&
              roles.values().contains(QByteArrayLiteral("text")) &&
              roles.values().contains(QByteArrayLiteral("conflict")),
          "the model exposes group/label/text/conflict roles");

    const int undo = model.rowForLabel(QStringLiteral("Undo last action"));
    check(model.textAt(undo) == ctrl::KeyBinding(Qt::Key_Z, Qt::ControlModifier).text(),
          "the chord text is the binding's own text()");
    check(model.conflictAt(undo).isEmpty(), "a lone chord reports no conflict");

    // ---- REPLACE: a valid key with no modifiers replaces the whole chord.
    model.press(undo, Qt::Key_A, Qt::NoModifier, false);
    check(model.bindingAt(undo).keyCode() == Qt::Key_A && !model.bindingAt(undo).hasSubKeyCode() &&
              model.textAt(undo) == ctrl::KeyBinding(Qt::Key_A).text(),
          "a bare key replaces the chord");

    // ---- SUB-KEY: the NEXT bare key extends that fresh chord instead of replacing.
    model.press(undo, Qt::Key_B, Qt::NoModifier, false);
    check(model.bindingAt(undo).keyCode() == Qt::Key_A && model.bindingAt(undo).subKeyCode() == Qt::Key_B,
          "a second bare key becomes the first chord's sub-key");
    check(model.textAt(undo) == ctrl::KeyBinding(Qt::Key_A, Qt::NoModifier, Qt::Key_B).text(),
          "and the text shows both keys");

    // ---- AUTO-REPEAT: holding a key must not walk the chord (the widget form's
    // isAutoRepeat guard). It is the SAME key that would otherwise replace.
    const QString beforeRepeat = model.textAt(undo);
    model.press(undo, Qt::Key_G, Qt::NoModifier, true);
    check(model.textAt(undo) == beforeRepeat, "an auto-repeating key is ignored");

    // ---- A chord that already has a sub-key is REPLACED by the next bare key (the
    // sub-key slot is full, so the widget rule falls through to the replace arm).
    model.press(undo, Qt::Key_F5, Qt::NoModifier, false);
    check(model.bindingAt(undo).keyCode() == Qt::Key_F5 && !model.bindingAt(undo).hasSubKeyCode(),
          "a key after a sub-keyed chord replaces it");

    // ---- An invalid key (a bare modifier) changes nothing at all.
    const QString beforeInvalid = model.textAt(undo);
    model.press(undo, Qt::Key_Shift, Qt::NoModifier, false);
    check(model.textAt(undo) == beforeInvalid, "an invalid key is ignored");

    // ---- FOCUS LOSS ends the sub-key window: the next key replaces instead of
    // extending.
    model.press(undo, Qt::Key_H, Qt::NoModifier, false);
    model.focusOut(undo);
    model.press(undo, Qt::Key_J, Qt::NoModifier, false);
    check(model.bindingAt(undo).keyCode() == Qt::Key_J && !model.bindingAt(undo).hasSubKeyCode(),
          "after a focus loss the next key replaces (no sub-key)");

    // ---- CLEAR: Delete and Backspace empty the chord. The clear key is handled
    // before validity, so a row can still be cleared when it holds nothing.
    model.press(undo, Qt::Key_Delete, Qt::NoModifier, false);
    check(!model.bindingAt(undo).isValidBinding() && model.textAt(undo).isEmpty(),
          "Delete clears the chord");
    model.press(2, Qt::Key_Backspace, Qt::NoModifier, false);
    check(!model.bindingAt(2).isValidBinding() && model.textAt(2).isEmpty(),
          "Backspace clears the chord");

    // ---- CONFLICT: driving two rows onto the same chord labels BOTH with the other.
    model.press(0, Qt::Key_M, Qt::NoModifier, false);
    check(model.conflictAt(0).isEmpty(), "one row on a chord is not a conflict yet");
    model.press(1, Qt::Key_M, Qt::NoModifier, false);
    check(model.conflictAt(0) == QLatin1String("General/Redo last action"),
          "the first row names the second row's group/label as its conflict");
    check(model.conflictAt(1) == QLatin1String("General/Undo last action"),
          "and the second names the first (both ways, as the widget form did)");

    // ---- RESET: every row returns to the chord it shipped with, conflicts and all.
    model.resetAll();
    check(model.bindingAt(0).matchesExactlyWith(model.defaultBindingAt(0)) &&
              model.textAt(0) == ctrl::KeyBinding(Qt::Key_Z, Qt::ControlModifier).text(),
          "resetAll restores the default chord");
    check(model.conflictAt(0).isEmpty() && model.conflictAt(1).isEmpty(),
          "and the conflicts the test created are gone");

    // ---- The footer/confirm verbs are SIGNALS, so the shell (and only the shell)
    // turns them into a QDialog result.
    int resets = 0, accepts = 0, cancels = 0;
    QObject::connect(&model, &Model::resetRequested, [&resets]() { ++resets; });
    QObject::connect(&model, &Model::acceptRequested, [&accepts]() { ++accepts; });
    QObject::connect(&model, &Model::cancelRequested, [&cancels]() { ++cancels; });
    model.requestReset();
    model.accept();
    model.cancel();
    check(resets == 1 && accepts == 1 && cancels == 1,
          "requestReset/accept/cancel each emit their request signal once");

    // =============================================================== the scene
    // A model the scene can drive: one row, so its delegate is the only field.
    Model sceneModel;
    sceneModel.addRow(
        QStringLiteral("Undo"), QStringLiteral("General"), QStringLiteral("Undo last action"),
        ctrl::KeyBinding(Qt::Key_Z, Qt::ControlModifier), ctrl::KeyBinding(Qt::Key_Z, Qt::ControlModifier));

    // The scene's glyphs are read from a scratch dir the application tints on startup
    // (theme::tintIcons -> setIconDir); doing the same here keeps the log readable
    // instead of one "Cannot open: file:///..." per Image.
    gui::QmlTheme theme;
    QTemporaryDir iconDir;
    check(iconDir.isValid(), "a scratch dir for the tinted icons");
    theme::tintIcons(QStringLiteral(AE_ICON_SRC), iconDir.path(), theme::Colors::current());
    theme::setIconDir(iconDir.path());

    QQmlEngine engine;
    // NOT "model": a ListView's own `model` property shadows the context property.
    engine.rootContext()->setContextProperty(QStringLiteral("keyBindingModel"), &sceneModel);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);

    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(AE_QML_DIR "/KeyBindingDialog.qml")));
    check(component.status() == QQmlComponent::Ready, "the dialog's QML is Ready");
    if (component.isError()) {
        QTextStream(stderr) << "the key-bindings dialog's QML does not load:\n";
        for (const QQmlError& error : component.errors())
            QTextStream(stderr) << "  " << error.toString() << "\n";
    }
    check(!component.isError(), "and it reports no error");

    QObject* created = component.create();
    check(created != nullptr, "the scene builds an object");
    auto* root = qobject_cast<QQuickItem*>(created);
    check(root != nullptr, "and its root is an Item the scene can be searched in");
    if (!root) return 1;

    // Host the scene in a real (offscreen) window and give the root a size, so the
    // ListView lays out and instantiates the delegate that holds the chord field.
    QQuickWindow window;
    window.resize(640, 480);
    window.show();
    root->setParentItem(window.contentItem());
    root->setWidth(window.width());
    root->setHeight(window.height());
    spin();

    QQuickItem* list = root->findChild<QQuickItem*>(QStringLiteral("keyBindingList"));
    check(list != nullptr, "the binding list is addressable");
    if (!list) return 1;
    // 0 == QAbstractItemView::PositionAtTop (passed as an int, the way the picker test
    // invokes the same method); the delegate for row 0 is what makes the field exist.
    QMetaObject::invokeMethod(list, "positionViewAtIndex", Q_ARG(int, 0), Q_ARG(int, 0));
    spin();

    // The delegate for row 0 first (the honest route: the ListView's own itemAtIndex),
    // then a tree search as a fallback - either finds the field the same delegate owns.
    QQuickItem* row = nullptr;
    for (int attempt = 0; attempt < 3 && !row; ++attempt) {
        spin();
        QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, row), Q_ARG(int, 0));
    }
    QQuickItem* field =
        row ? row->findChild<QQuickItem*>(QStringLiteral("keyBindingField")) : nullptr;
    if (!field)
        field = root->findChild<QQuickItem*>(QStringLiteral("keyBindingField"));
    check(field != nullptr, "the first row's chord field is in the scene");
    if (!field) return 1;

    check(field->property("accessibleName").toString() == QLatin1String("Undo last action"),
          "the chord field carries its command's label as its accessible name");

    // ---- The wiring: a key pressed in the REAL scene's field reaches the model.
    check(sendKey(field, Qt::Key_F5), "the chord field consumes the key it captures");
    check(sceneModel.bindingAt(0).keyCode() == Qt::Key_F5 &&
              sceneModel.textAt(0) == ctrl::KeyBinding(Qt::Key_F5).text(),
          "a real key press through the scene writes the row");

    check(sendKey(field, Qt::Key_Delete), "and it consumes the clear key too");
    check(!sceneModel.bindingAt(0).isValidBinding() && sceneModel.textAt(0).isEmpty(),
          "Delete pressed in the scene clears the row");

    if (failures == 0) {
        QTextStream(stdout) << "\nall key-binding form checks passed\n";
        return 0;
    }
    QTextStream(stdout) << "\n" << failures << " check(s) FAILED\n";
    return 1;
}
