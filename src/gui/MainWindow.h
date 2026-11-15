#ifndef GUI_MAINWINDOW_H
#define GUI_MAINWINDOW_H

#include <QMainWindow>
#include <QKeyEvent>
#include <QFileInfo>
#include <QScopedPointer>
#include <QTimer>
#include <functional>
#include "ctrl/System.h"
#include "gui/MainMenuBar.h"
#include "gui/MainDisplayWidget.h"
#include "gui/MainViewSetting.h"
#include "gui/MainDisplayStyle.h"
#include "gui/TargetWidget.h"
#include "gui/PropertyWidget.h"
#include "gui/ToolWidget.h"
#include "gui/DriverHolder.h"
#include "gui/GUIResources.h"
#include "gui/ViaPoint.h"
#include "gui/ProjectTabBar.h"
#include "gui/KeyCommandMap.h"
#include "gui/KeyCommandInvoker.h"
#include "gui/ResourceDialog.h"
#include "gui/LocaleParam.h"
#include "gui/MouseSetting.h"
#include "qfilesystemwatcher.h"
#include "res/res_ResourceUpdater.h"
#include "ctrl/ExportParams.h"
#include "exportdiag.h"


namespace gui {

class MainWindow: public QMainWindow {
    Q_OBJECT

public:
    MainWindow(ctrl::System& aSystem, GUIResources& aResources, LocaleParam  aLocaleParam);
    ~MainWindow();

    void showWithSettings();
    void saveCurrentSettings(int aResultCode);
    void testNewProject(const QString& aFilePath);
    void closeAllProjects();
    static QFileSystemWatcher* getWatcher();
    QElapsedTimer timeElapsed;
    qint64 lastPress;
    qint64 lastRelease;
    static void showInfoPopup(
        const QString& aTitle, const QString& aDetailText, const QString& aIcon, const QString& aDetailed = "nullptr"
    );
    QTimer* autosaveTimer;
public slots:
    void autoSave();
    void onGeneralSettingsApplied();

public:
    void onNewProjectTriggered();
    void onOpenProjectTriggered();
    void onOpenRecentTriggered(QString aFileName);
    void onSaveProjectTriggered();
    void onSaveProjectAsTriggered();
    void onCloseProjectTriggered();
    void onTabSaveRequested(core::Project&);
    void onTabSaveAsRequested(core::Project&);
    void onTabCloseRequested(core::Project&);
    void onTabCloseOthersRequested(core::Project&);
    void onTabCloseAllRequested();
    void onTabCopyPathRequested(core::Project&);
    void onExportTriggered();
    // The export form itself, after the destination question has been answered.
    // An empty aDestinationId is "my platform isn't listed", which is the only mode
    // where the container and codec are the user's to choose.
    void openExportDialog(const QString& aDestinationId);
    // "Quickly export as...": choose a destination, then export with no further
    // dialogs (the output folder is remembered per destination).
    void onQuickExportTriggered();
    // Quick export BY DESTINATION: the user names where the file is going and the
    // destination table decides the rest. This is what the quick menu offers now —
    // a format name is not a destination, and the table's rows bundle intent
    // (small, loops, transparent, silent) that a container name cannot express.
    void onQuickExportDestinationTriggered(const QString& aDestinationId);
    // Repeat the last export exactly, with no dialogs: the iteration loop. Export
    // is not a one-shot in this tool — an animator exports the same sticker a dozen
    // times while adjusting the art — and re-walking the menu, the destination and
    // the file dialog every time is the cost that made this worth having.
    void onExportAgainTriggered();
    void onPlayPauseTriggered();
    void onDockToggle();
    void onDisplacementTriggered(int frameDisplacement);
    void onMovementTriggered(const QString& frameMovement);
    void onUndoTriggered();
    void onLoopToggle();
    void onRedoTriggered();

private:
    virtual void keyPressEvent(QKeyEvent* aEvent);
    virtual void keyReleaseEvent(QKeyEvent* aEvent);
    virtual void closeEvent(QCloseEvent* aEvent);

    void resetProjectRefs(core::Project* aProject);
    bool processProjectSaving(core::Project& aProject, bool aRename = false, bool aSilent = false, bool aStopPlayback = true);
    void updateAutosaveState();
    void offerRecoveryCopies();
    void openRecoveryCopy(const QString& aFilePath);
    int confirmProjectClosing(const QVector<core::Project*>& aProjects);
    int confirmProjectClosing(const QString& aMessage);
    bool closeProjects(const QVector<core::Project*>& aProjects);
    void closeProjectTab(core::Project& aProject);
    void onProjectTabChanged(core::Project&);
    void onThemeUpdated(theme::Theme&);

    bool onStartup = true;
    bool themeChangeWarned = false;
    bool exporting = false;
    // For some reason you need these here or the widget will only show properly once :/
    ExportWidgetUI *exportUI = new ExportWidgetUI;
    QDialog* exportWidget = new QDialog;
    QDialog* gpDiag = new QDialog;
    void regenerateWidget(){ exportWidget = new QDialog; exporting = false; }
    void regenerateGeneralPurposeDiag(){ gpDiag = new QDialog; }

    // Recompute the destination's facts and re-apply them to the dialog's
    // controls. Held as a member only so it can be invoked once AFTER the
    // project's own size/fps/saved options have been written into the widgets:
    // running it earlier would state the plan's numbers and then have the
    // initialisation overwrite them.
    std::function<void()> mRefreshExportFacts;

    // The last export that actually wrote something, kept so it can be repeated.
    // A copy of the parameters, not a re-derivation: repeating an export must
    // reproduce it, including the manual-mode artifact and the frame ranges.
    std::unique_ptr<exportParam> mLastExport;

    // Where the last export went, so the next dialog opens there instead of
    // nowhere, and so a quick export to a known destination needs no prompt at all.
    void rememberExportLocation(const exportParam& aParam);
    // The dialog's per-run inputs (frame ranges, size, fps) live in QSettings, per
    // project, so reopening the dialog to change one thing does not mean selecting
    // the range again.
    QString exportInputsKey() const;
    void restoreExportInputs();
    void reportExportOutcome(const exportParam& aParam, bool aWrote);


    ctrl::System& mSystem;
    GUIResources& mGUIResources;
    ViaPoint mViaPoint;
    QScopedPointer<KeyCommandMap> mKeyCommandMap;
    QScopedPointer<KeyCommandInvoker> mKeyCommandInvoker;
    MouseSetting mMouseSetting;
    MainMenuBar* mMainMenuBar;
    MainViewSetting mMainViewSetting;
    QScopedPointer<MainDisplayStyle> mMainDisplayStyle;
    MainDisplayWidget* mMainDisplay;
    ProjectTabBar* mProjectTabBar;
    QVector<core::Project*> mProjects;
    TargetWidget* mTarget;
    PropertyWidget* mProperty;
    QDockWidget* mDockPropertyWidget;
    ToolWidget* mTool;
    QDockWidget* mDockToolWidget;
    ResourceDialog* mResourceDialog;
    QScopedPointer<DriverHolder> mDriverHolder;
    core::Project* mCurrent;
    LocaleParam mLocaleParam;
    void onMoveFrameTriggered(bool moveRight);
};

} // namespace gui

#endif // GUI_MAINWINDOW_H
