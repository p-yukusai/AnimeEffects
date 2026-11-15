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

class QDockWidget;

namespace gui {

class ExportDialog;
class ExportViewModel;
struct DockDropTarget;
class DockDropOverlay;
class HeaderBar;
class MenuCommand;

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
    virtual void keyPressEvent(QKeyEvent* aEvent) override;
    virtual void keyReleaseEvent(QKeyEvent* aEvent) override;
    virtual void closeEvent(QCloseEvent* aEvent) override;
    // Docks are added, split, tabified and torn down by the layout itself, so
    // the panel chrome (tab rows, menus, slot buttons) is re-synchronised from
    // the layout events rather than from each operation that could change it.
    bool eventFilter(QObject* aObject, QEvent* aEvent) override;

    void resetProjectRefs(core::Project* aProject);
    // A successful load can still carry limitations (the importer's degraded
    // layers, a skeleton past the deformable bone cap). Show them once, in the
    // log and in a popup, on every entry point that opens a project.
    void announceLoadWarnings(const ctrl::System::LoadResult& aResult);
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
    // ONE place for the viewport background's decision: the project's own override
    // when it has one, the theme's recessed canvas token (GUIResources::
    // viewportBackground()) otherwise. Called on project switch (resetProjectRefs),
    // on a theme change (onThemeUpdated) and after a project-attribute command (the
    // menu's onProjectAttributeUpdated fan-out), so the rule is written once.
    void applyViewportBackground();

    // The dock layout's own bookkeeping. Docking itself stays Qt's (areas,
    // splitters, tabify, resizeDocks); these functions keep the app's chrome and
    // the drop gesture's promises in step with it.
    /// Applies the area's size range (DockMetrics.h) to a panel.
    void applyPanelMetrics(QDockWidget* aDock);
    /// Performs the docking a drop asked for (declared in DockDropOverlay.h).
    void applyDockDrop(QDockWidget* aPanel, const DockDropTarget& aTarget);
    /// The placement alone, shared by every drop kind.
    void placeDock(QDockWidget* aPanel, const DockDropTarget& aTarget);
    /// The dock in an area closest to the central widget (the split anchor).
    QDockWidget* innermostDock(Qt::DockWidgetArea aArea, QDockWidget* aSkip) const;
    /// Adopts Qt's own bar for a tab group: names it for the theme, gives it the
    /// panel menu/gestures, and records the area its slot belongs to.
    void updateDockTabMenus();
    /// Shows each panel's own title row only while the panel is alone in its area.
    void updateTitleRows();
    /// A slot button's request; the window performs the dock toggles.
    void onDockSlotToggleRequested(int aArea);
    /// Synchronises the slot buttons with the layout.
    void updateLayoutButtons();
    /// Queues one chrome re-sync for after the current event. Never called
    /// directly from a dock signal: a drop arrives while the drag is still in
    /// flight, and moving a title row then breaks the drag (measured: the drag
    /// ended in a leave with action=0 once the sync ran on every preview).
    void scheduleDockChromeSync();
    /// Drops the opening-size floors (see the constructor): they exist only so
    /// the panels open at their design sizes instead of their area minimum, and
    /// must not stop a slot from ever reaching DockMetrics' range.
    void clearDockOpeningFloors();
    /// The panel a tab title names, if any.
    QDockWidget* dockForTabTitle(const QString& aTitle) const;

    // The colour that decision resolves to. applyViewportBackground() shows it in
    // the canvas; the export carries it too (the artifact's background when
    // transparency is off), so the two cannot disagree about what colour the
    // viewport is. The theme is a GUI concept, which is why it is resolved here
    // and handed to ctrl::/media:: as a plain QColor.
    QColor effectiveViewportBackground() const;

    bool onStartup = true;
    bool themeChangeWarned = false;
    bool exporting = false;
    // The export form. It is a Qt Quick dialog now (gui/ExportDialog) whose every
    // rule lives in gui::ExportViewModel, which is widget-free and is what
    // tests/export_form.cpp drives. Held only while it is open.
    QPointer<ExportDialog> mExportDialog;

    // The last export that actually wrote something, kept so it can be repeated.
    // A copy of the parameters, not a re-derivation: repeating an export must
    // reproduce it, including the manual-mode artifact and the frame ranges.
    std::unique_ptr<exportParam> mLastExport;

    // Where the last export went, so the next dialog opens there instead of
    // nowhere, and so a quick export to a known destination needs no prompt at all.
    void rememberExportLocation(const exportParam& aParam);
    // The dialog's per-run inputs (frame ranges, size, fps) live in QSettings, per
    // project, so reopening the dialog to change one thing does not mean selecting
    // the range again. The remembered values are loaded into the model at
    // construction, and written back after it closes.
    QString exportInputsKey() const;
    void rememberExportInputs(const ExportViewModel& aModel);
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
    /// One entry per layout toggle: the slot it stands for, its glyph and the
    /// slot's translated name. The button itself is QML (qml/HeaderBar.qml); the
    /// window only feeds the slot model and performs the toggle.
    struct LayoutSlot {
        Qt::DockWidgetArea area;
        QString glyph;
        QString name;
    };
    QVector<LayoutSlot> mLayoutSlots;
    HeaderBar* mHeaderBar = nullptr;
    /// The Panels menu's three toggles, paired with the dock each one shows or
    /// hides. The commands are created once, after the docks; updateLayoutButtons
    /// reads the docks' visibility back into them.
    struct PanelToggle {
        QDockWidget* dock;
        MenuCommand* command;
    };
    QVector<PanelToggle> mPanelToggles;

    DockDropOverlay* mDockDropOverlay = nullptr;
    QDockWidget* mDockAnimationWidget;
    QDockWidget* mDockPropertyWidget;
    ToolWidget* mTool;
    QDockWidget* mDockToolWidget;
    /// One deferred chrome re-sync at a time: the sync itself changes widget
    /// properties, which can emit the layout events that asked for it.
    bool mDockSyncQueued = false;
    /// True while the panels still carry their opening-size minimums.
    bool mDockOpeningFloors = false;
    ResourceDialog* mResourceDialog;
    QScopedPointer<DriverHolder> mDriverHolder;
    core::Project* mCurrent;
    LocaleParam mLocaleParam;
    void onMoveFrameTriggered(bool moveRight);
};

} // namespace gui

#endif // GUI_MAINWINDOW_H
