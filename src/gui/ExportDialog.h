#ifndef GUI_EXPORTDIALOG_H
#define GUI_EXPORTDIALOG_H

#include <QDialog>

#include "gui/qml/ExportViewModel.h"

class QQuickWidget;

namespace gui {

class GUIResources;
class QmlTheme;

// The export parameter form, as a NATIVE framed dialog window hosting a Qt Quick
// scene (qml/ExportDialog.qml).
//
// Framed and modal on purpose: the widget form this replaces was a QDialog with a
// title bar and a resize grip, and it is the last thing between the user and a file
// they are about to write. A frameless popup (which is what the animation-settings
// dialog is) would lose the title, the system move/resize and the window-manager
// affordances for no gain here.
//
// The shell owns the view model and nothing else: every rule the form applies is in
// gui::ExportViewModel, which is widget-free and is what tests/export_form.cpp
// drives. What lives here is the QQuickWidget, the theme bridge, the destination
// picker (a widget dialog, unchanged) and the palette file chooser.
class ExportDialog : public QDialog {
    Q_OBJECT
public:
    // aInit carries the project facts and the remembered preferences; see
    // ExportViewModel::Init. aResources is only for the destination picker (its icons
    // and its own resource lookups), which stays a widget dialog.
    ExportDialog(const ExportViewModel::Init& aInit, GUIResources& aResources,
                 QWidget* aParent = nullptr);
    ~ExportDialog() override;

    // Everything MainWindow needs to build exportParam/GeneralParams, with no
    // widget access.
    ExportViewModel* viewModel() const { return mModel; }

private:
    void choosePalette();
    // A5: every close saves the dialog's size (the restore is in the constructor).
    void done(int aResult) override;

    GUIResources& mResources;
    ExportViewModel* mModel = nullptr;
    QmlTheme* mTheme = nullptr;
    QQuickWidget* mQuick = nullptr;
};

} // namespace gui

#endif // GUI_EXPORTDIALOG_H
