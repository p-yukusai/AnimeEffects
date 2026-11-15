#ifndef GUI_KEYBINDINGDIALOG_H
#define GUI_KEYBINDINGDIALOG_H

#include <QDialog>

class QQuickWidget;

namespace gui {

class KeyBindingViewModel;
class KeyCommandMap;
class QmlTheme;

// The key-bindings form, as a NATIVE framed dialog window hosting a Qt Quick scene
// (qml/KeyBindingDialog.qml).
//
// Framed and modal on purpose: the widget form this replaces was a QTabWidget of
// QFormLayouts inside an EasyDialog with a title bar and Ok/Cancel, reachable from
// the Preferences menu, and a frameless popup would lose the title, the system
// move/resize and the window-manager affordances for no gain here.
//
// The shell owns the map and the view model and nothing else: what the form decides
// lives in gui::KeyBindingViewModel, which is widget-free. What lives here is the
// QQuickWidget, the theme bridge, the snapshot of the map into the model's plain
// rows (and the write-back on accept), and the reset CONFIRMATION - a QMessageBox,
// a WIDGET dialog the shell owns, exactly as NewProjectDialog owns its file chooser.
//
// The constructor signature is unchanged: MainMenuBar.cpp builds this and calls
// exec(), and every behaviour the widget form had is preserved by the model.
class KeyBindingDialog: public QDialog {
    Q_OBJECT
public:
    KeyBindingDialog(KeyCommandMap& aMap, QWidget* aParent);
    ~KeyBindingDialog() override;

private:
    // Flush every row back into the map, then persist the map's overrides under the
    // "keybindings" group of the user's settings. Called on accept only; cancel
    // leaves the map untouched (the widget form's setOkCancel(id == 0) split).
    void saveSettings();

    KeyCommandMap& mKeyCommandMap;
    KeyBindingViewModel* mModel = nullptr;
    QmlTheme* mTheme = nullptr;
    QQuickWidget* mQuick = nullptr;
};

} // namespace gui

#endif // GUI_KEYBINDINGDIALOG_H
