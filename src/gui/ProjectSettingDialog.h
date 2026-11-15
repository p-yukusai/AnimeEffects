#ifndef GUI_PROJECTSETTINGDIALOG_H
#define GUI_PROJECTSETTINGDIALOG_H

#include <QDialog>
#include <QColor>
#include <QSize>

#include "core/Project.h"
#include "util/Range.h"

class QQuickWidget;

namespace gui {

class ProjectSettingViewModel;
class QmlTheme;
class ViaPoint;

// The project-settings form, as a NATIVE framed dialog window hosting a Qt Quick
// scene (qml/ProjectSettingDialog.qml).
//
// Framed and modal on purpose, exactly as the widget form it replaces was: it went
// through EasyDialog and carried a title bar and OK/Cancel, and it edits the
// project the user is looking at, so it must sit over the main window with the
// window manager's move/resize still working. A frameless popup would lose all of
// that for no gain here.
//
// The shell owns the view model and nothing else: every value the form produces
// lives in gui::ProjectSettingViewModel, which is widget-free. What lives here is
// the QQuickWidget, the theme bridge, and the GL device ceiling the model cannot
// ask for - the same shell shape gui::NewProjectDialog and gui::ExportDialog use.
class ProjectSettingDialog : public QDialog {
    Q_OBJECT
public:
    ProjectSettingDialog(ViaPoint& aViaPoint, core::Project& aProject, QWidget* aParent);
    ~ProjectSettingDialog() override;

    // The four facts MainMenuBar::onProjectSettingsTriggered() reads back, with no
    // widget access.
    QSize canvasSize() const;
    util::Range frameRange() const;
    bool loop() const;
    int fps() const;
    // The viewport background override, read back the same way. The empty hex has
    // no colour, so the caller gates on the first before it reads the second.
    bool viewportBackgroundIsThemeDefault() const;
    QColor viewportBackground() const;

private:
    bool confirmFrameRangeUpdating(const util::Range& aNewRange) const;
    // The scene asks for the colour chooser (it has none); this is the shell's
    // answer, the stock QColorDialog the app otherwise never uses.
    void chooseViewportBackground();

    ViaPoint& mViaPoint;
    core::Project& mProject;
    ProjectSettingViewModel* mModel = nullptr;
    QmlTheme* mTheme = nullptr;
    QQuickWidget* mQuick = nullptr;
};

} // namespace gui

#endif // GUI_PROJECTSETTINGDIALOG_H
