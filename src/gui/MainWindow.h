#ifndef GUI_MAINWINDOW_H
#define GUI_MAINWINDOW_H

#include <QMainWindow>
#include <QKeyEvent>
#include <QFileInfo>
#include <QScopedPointer>
#include <QTimer>
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
    void onExportImageSeqTriggered(const QString& aSuffix);
    void onQuickExportTriggered(const QString& aFormat);
    void onExportVideoTriggered(const ctrl::VideoFormat& aFormat);
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
    bool processProjectSaving(core::Project& aProject, bool aRename = false);
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
