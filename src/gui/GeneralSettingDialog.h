#ifndef GUI_GENERALSETTINGDIALOG_H
#define GUI_GENERALSETTINGDIALOG_H

#include <QDialog>

#include "gui/theme/Colors.h"

class QQuickWidget;

namespace gui {

class GUIResources;
class GeneralSettingViewModel;
class QmlTheme;
class ViaPoint;

// The settings form, as a NATIVE framed dialog window hosting a Qt Quick scene
// (qml/GeneralSettingDialog.qml).
//
// Framed and modal on purpose: the widget form this replaces was a QDialog (through
// EasyDialog) with a title bar and OK/Cancel. A frameless popup would lose the
// title, the system move/resize and the window-manager affordances for no gain.
//
// The shell owns the view model and nothing else: every value the form produces
// lives in gui::GeneralSettingViewModel, which is widget-free, so the *HasChanged()
// predicates the call site asks after exec() are answered by the model rather than
// by reading controls back. What lives here is the QQuickWidget, the theme bridge,
// the reading of the saved settings that seeds the model, and the settings writes on
// accept - the same shell shape gui::NewProjectDialog and gui::ExportDialog use.
class GeneralSettingDialog : public QDialog {
    Q_OBJECT
public:
    GeneralSettingDialog(GUIResources& aGUIResources, ViaPoint& aViaPoint, QWidget* aParent);
    ~GeneralSettingDialog() override;

    // Show one of the four tabs: General 0, QoL 1, Keyboard 2, Mouse 3. Writes the
    // model's currentTab, which the scene's tab strip is bound to.
    void selectTab(int aIndex);

    bool easingHasChanged();
    bool rangeHasChanged();
    bool languageHasChanged();
    bool timeFormatHasChanged();
    bool autoSaveHasChanged();
    bool autoSaveDelayHasChanged();
    bool resIDCheckHasChanged();
    bool themeHasChanged();
    bool donationHasChanged();
    bool forceSolverLoadHasChanged();
    bool ignoreWarningsHasChanged();
    bool cbCopyHasChanged();
    bool keyDelayHasChanged();
    bool mouseHasChanged();
    QString theme();
    theme::AccentColor accent();
    bool accentHasChanged();

private:
    // Write only the settings whose value actually moved, exactly as the widget form
    // did; MainMenuBar re-reads the *HasChanged() predicates after exec() to decide
    // what to apply live.
    void saveSettings();
    // Resize the window to the scene's own implicitHeight (the shell's size-to-scene
    // rule); called once after the scene loads and again whenever the tab changes,
    // because each tab is a different set of rows.
    void resizeToScene();

    GeneralSettingViewModel* mModel = nullptr;
    QmlTheme* mTheme = nullptr;
    QQuickWidget* mQuick = nullptr;

    GUIResources& mGUIResources;
    ViaPoint& mViaPoint;
};

} // namespace gui

#endif // GUI_GENERALSETTINGDIALOG_H
