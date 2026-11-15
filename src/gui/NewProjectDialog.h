#ifndef GUI_NEWPROJECTDIALOG_H
#define GUI_NEWPROJECTDIALOG_H

#include <QDialog>

#include "core/Project.h"

class QQuickWidget;

namespace gui {

class NewProjectViewModel;
class QmlTheme;

// The new-project form, as a NATIVE framed dialog window hosting a Qt Quick
// scene (qml/NewProjectDialog.qml).
//
// Framed and modal on purpose: the widget form this replaces was a QDialog
// (through EasyDialog) with a title bar and OK/Cancel, and it is the first thing
// a user meets in a session. A frameless popup would lose the title, the system
// move/resize and the window-manager affordances for no gain here.
//
// The shell owns the view model and nothing else: every value the form produces
// lives in gui::NewProjectViewModel, which is widget-free. What lives here is the
// QQuickWidget, the theme bridge, and the asset file chooser (a WIDGET dialog,
// unchanged) - the same shell shape gui::ExportDialog uses.
class NewProjectDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewProjectDialog(QWidget* aParent);
    ~NewProjectDialog() override;

    // Everything MainWindow needs to start the project, with no widget access.
    QString fileName() const;
    // The frame range (1..frameCount, 1-indexed), the fps, and the canvas size the
    // form's fields hold.
    core::Project::Attribute attribute() const;
    bool specifiesCanvasSize() const;
    // Whether the new project should monitor its asset for on-disk changes and
    // reload it.
    bool liveLink() const;

private:
    NewProjectViewModel* mModel = nullptr;
    QmlTheme* mTheme = nullptr;
    QQuickWidget* mQuick = nullptr;
};

} // namespace gui

#endif // GUI_NEWPROJECTDIALOG_H
