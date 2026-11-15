#ifndef GUI_MAINMENUBAR_H
#define GUI_MAINMENUBAR_H

#include <QCheckBox>
#include <QMenu>
#include <QSpinBox>
#include "core/Project.h"
#include "ctrl/ExportTarget.h"
#include "gui/EasyDialog.h"
#include "gui/GUIResources.h"
#include "qprocess.h"

namespace gui {
class MainWindow;
}
namespace gui {
class ViaPoint;
}
namespace gui {

//-------------------------------------------------------------------------------------------------
class MainMenuBar: public QMenuBar {
    Q_OBJECT
public:
    MainMenuBar(MainWindow& aMainWindow, ViaPoint& aViaPoint, GUIResources& aGUIResources, QWidget* aParent);
    void setProject(core::Project* aProject);
    void setShowResourceWindow(bool aShow) const;
    QStringList recentfiles;

public:
    // signals
    util::Signaler<void()> onVisualUpdated;
    util::Signaler<void()> onProjectAttributeUpdated;
    util::Signaler<void()> onTimeFormatChanged;
    QString system_info = "Unknown";

private:
    QScopedPointer<QProcess> mProcess;
    void onProjectSettingsTriggered();

    ViaPoint& mViaPoint;
    core::Project* mProject;
    QVector<QAction*> mProjectActions;
    // "Export again": enabled by the main window once an export has succeeded.
    QAction* mExportAgainAction = nullptr;
    // "Quickly export as": rebuilt on every show, because its first entries are the
    // destinations this user has actually exported to.
    // The window the quick-export entries call into. Held as a member because the
    // menu is REBUILT (on every show), so the entries cannot capture a local.
    MainWindow* mMainWindow = nullptr;
    void addQuickExportEntry(QMenu* aMenu, const ctrl::ExportTarget& aTarget);
public:
    // Enabled by the main window once an export has succeeded: "again" means the
    // same export, and there is nothing to repeat until there has been one.
    void setExportAgainEnabled(bool aOn);
private:
    GUIResources& mGUIResources;
};

//-------------------------------------------------------------------------------------------------
class ProjectSettingDialog: public EasyDialog {
    Q_OBJECT
public:
    ProjectSettingDialog(ViaPoint& aViaPoint, core::Project& aProject, QWidget* aParent);
    QSize canvasSize() const { return {mWidthBox->value(), mHeightBox->value()}; }
    util::Range frameRange() const { return util::Range(mStartBox->value(), mEndBox->value()); }
    bool loop() const { return mLoopBox->isChecked(); }
    int fps() const { return mFPSBox->value(); }

private:
    bool confirmFrameRangeUpdating(const util::Range& aNewRange) const;
    ViaPoint& mViaPoint;
    core::Project& mProject;
    QSpinBox* mWidthBox;
    QSpinBox* mHeightBox;
    QSpinBox* mStartBox;
    QSpinBox* mEndBox;
    QCheckBox* mLoopBox;
    QSpinBox* mFPSBox;
};


} // namespace gui

#endif // GUI_MAINMENUBAR_H
