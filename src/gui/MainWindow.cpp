#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QRegularExpression>
#include "GeneralSettingDialog.h"
#include "util/IProgressReporter.h"
#include "gl/Global.h"
#include "ctrl/Exporter.h"
#include "gui/MainWindow.h"

#include "gui/ExportFacts.h"
#include "gui/DestinationPicker.h"
#include "gui/DockSash.h"
#include "gui/NewProjectDialog.h"
#include "gui/ProjectHook.h"
#include "gui/menu/menu_ProgressReporter.h"
#include "util/NetworkUtil.h"
#include <set>
#include <utility>
#include <QComboBox>
#include <QStatusBar>
// This thing is held by duct tape and OOP hell I swear...
#include "ctrl/ExportParams.h"

namespace {
class EventSuspender {
    QWriteLocker mRenderingLocker;

public:
    EventSuspender(gui::MainDisplayWidget& aMainDisplay, gui::TargetWidget& aTarget, bool aStopPlayback = true):
        mRenderingLocker(&aMainDisplay.renderingLock()) {
        // Manual saves stop playback so the serialized state is stable. The
        // autosave path keeps playback running: the frame timer is starved
        // for the duration of the save anyway (single UI thread), and
        // stopping would leave the animation paused until the user re-presses
        // play — a surprise on every autosave tick.
        if (aStopPlayback) {
            aTarget.stop();
        }
    }
};

} // namespace

namespace gui {

MainWindow::MainWindow(ctrl::System& aSystem, GUIResources& aResources, LocaleParam  aLocaleParam):
    QMainWindow(nullptr),
    autosaveTimer(nullptr),
    mSystem(aSystem),
    mGUIResources(aResources),
    mViaPoint(this),
    mMainMenuBar(),
    mMainDisplay(),
    mProjectTabBar(),
    mTarget(),
    mProperty(),
    mTool(),
    mResourceDialog(),
    mCurrent(),
    mLocaleParam(std::move(aLocaleParam)) {
    // setup default opengl format
    {
        QSurfaceFormat format;
        format.setVersion(gl::Global::kVersion.first, gl::Global::kVersion.second);
        format.setProfile(QSurfaceFormat::CoreProfile);
        format.setSamples(4);
        QSurfaceFormat::setDefaultFormat(format);
    }

    // setup UI
    {
        this->setObjectName(QStringLiteral("MainWindow"));
        this->setWindowIcon(QIcon("../src/AnimeEffects.ico"));
        this->setMouseTracking(true);
        this->setFocusPolicy(Qt::NoFocus);
        this->setAcceptDrops(false);
        this->setTabShape(QTabWidget::Rounded);
        this->setDockOptions(AnimatedDocks);
        this->setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    }

    // initialize via point
    {
        mViaPoint.setMainViewSetting(mMainViewSetting);

        mMouseSetting.load();
        mViaPoint.setMouseSetting(mMouseSetting);
    }

    // key binding
    {
        mKeyCommandMap.reset(new KeyCommandMap(*this));

        QSettings settings(
            QSettings::IniFormat,
            QSettings::UserScope,
            QApplication::organizationName(),
            QApplication::applicationName()
        );
        settings.beginGroup("keybindings");
        mKeyCommandMap->readFrom(settings);
        settings.endGroup();

        mViaPoint.setKeyCommandMap(mKeyCommandMap.data());

        mKeyCommandInvoker.reset(new KeyCommandInvoker(*mKeyCommandMap));
        mViaPoint.setKeyCommandInvoker(mKeyCommandInvoker.data());
    }

    // create main menu bar
    {
        mMainMenuBar = new MainMenuBar(*this, mViaPoint, mGUIResources, this);
        mViaPoint.setMainMenuBar(mMainMenuBar);
        this->setMenuBar(mMainMenuBar);
    }
    // initialize timer
    {
        timeElapsed.start();
        lastPress = 0;
        lastRelease = 0;
    }

    // create main display
    {
#if defined(Q_OS_WIN)
        constexpr float fontScale = 1.5f;
#else
        const float fontScale = 1.3f;
#endif
        auto font = this->font();
        if (font.pixelSize() > 0)
            font.setPixelSize(font.pixelSize() * static_cast<int>(fontScale));
        else
            font.setPointSizeF(font.pointSizeF() * fontScale);
        mMainDisplayStyle.reset(new MainDisplayStyle(font, mGUIResources));
        mMainDisplay = new MainDisplayWidget(mViaPoint, this);
        this->setCentralWidget(mMainDisplay);
        mMainDisplay->setViewportBackground(mGUIResources.viewportBackground());

        mProjectTabBar = new ProjectTabBar(mMainDisplay, mGUIResources);
        mMainDisplay->setProjectTabBar(mProjectTabBar);
        mProjectTabBar->onCurrentChanged.connect(this, &MainWindow::onProjectTabChanged);
        mProjectTabBar->onSaveTab.connect(this, &MainWindow::onTabSaveRequested);
        mProjectTabBar->onSaveTabAs.connect(this, &MainWindow::onTabSaveAsRequested);
        mProjectTabBar->onCloseTab.connect(this, &MainWindow::onTabCloseRequested);
        mProjectTabBar->onCloseOtherTabs.connect(this, &MainWindow::onTabCloseOthersRequested);
        mProjectTabBar->onCloseAllTabs.connect(this, &MainWindow::onTabCloseAllRequested);
        mProjectTabBar->onCopyPath.connect(this, &MainWindow::onTabCopyPathRequested);
    }

    // create targeting widget
    {
        auto* dockWidget = new QDockWidget(this);
        dockWidget->setObjectName("animationDock");
        dockWidget->setWindowTitle(tr("Animation"));
        dockWidget->setFeatures(QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetMovable);
        dockWidget->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
        this->addDockWidget(Qt::BottomDockWidgetArea, dockWidget);

        mTarget = new TargetWidget(mViaPoint, mGUIResources, dockWidget, QSize(256, 256));
        dockWidget->setWidget(mTarget);
    }

    // create property widget
    {
        auto* dockWidget = new QDockWidget(this);
        dockWidget->setObjectName("propertiesDock");
        dockWidget->setWindowTitle(tr("Properties"));
        dockWidget->setFeatures(QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetMovable);
        dockWidget->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        this->addDockWidget(Qt::RightDockWidgetArea, dockWidget);

        mDockPropertyWidget = dockWidget;

        // Panel width budget: min = default = 320 (everything fits; the
        // fields cap at 70% of the column), max = 2x min - a wider dock
        // would just stretch empty space.
        dockWidget->setMinimumWidth(320);
        dockWidget->setMaximumWidth(640);

#if 0
        mProperty = new PropertyWidget(mViaPoint, splitter, &mGUIResources);
        dockWidget->setWidget(mProperty);
#else
        auto splitter = new gui::ThinSplitter(Qt::Vertical, dockWidget);
        splitter->setObjectName("propertysplitter");
        dockWidget->setWidget(splitter);

        mProperty = new PropertyWidget(mViaPoint, splitter, &mGUIResources);
        splitter->addWidget(mProperty);

        auto textEdit = new QPlainTextEdit(splitter);
        textEdit->setUndoRedoEnabled(false);
        textEdit->setMaximumBlockCount(32);
        textEdit->setReadOnly(true);
        mViaPoint.setLogView(textEdit);

        splitter->addWidget(textEdit);
        splitter->setCollapsible(0, false);
        splitter->setCollapsible(1, false);


        QList<int> list;
        list.append(9000);
        list.append(1000);
        splitter->setSizes(list);
#endif
    }

    // create tool widget
    {
        auto* dockWidget = new QDockWidget(this);
        dockWidget->setObjectName("toolsDock");
        dockWidget->setWindowTitle(tr("Tools"));
        dockWidget->setFeatures(QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetMovable);
        this->addDockWidget(Qt::LeftDockWidgetArea, dockWidget);

        mDockToolWidget = dockWidget;

        mTool = new ToolWidget(mViaPoint, mGUIResources, *mKeyCommandMap, QSize(172, 136), dockWidget);
        dockWidget->setWidget(mTool);
    }

    // create resource dialog
    {
        mResourceDialog = new ResourceDialog(mViaPoint, false, this);
        mViaPoint.setResourceDialog(mResourceDialog);
    }

    // dock-edge sashes: 12px invisible hitboxes + 1px hairlines over the
    // zero-width native separators (see standard.ssa); resizes docks through
    // QMainWindow::resizeDocks
    new gui::DockSash(this);

    // create driver holder
    { mDriverHolder.reset(new DriverHolder(mViaPoint)); }

    // connection
    /// @note Maybe a sequence of connections is meaningful.
    {
        DriverHolder& driver = *mDriverHolder;
        MainDisplayWidget& disp = *mMainDisplay;
        PropertyWidget& prop = *mProperty;
        ToolWidget& tool = *mTool;
        ObjectTreeWidget& objTree = mTarget->objectTreeWidget();
        TimeLineWidget& timeLine = mTarget->timeLineWidget();
        MainMenuBar& menu = *mMainMenuBar;
        ViaPoint& via = mViaPoint;

        driver.onVisualUpdated.connect(&disp, &MainDisplayWidget::onVisualUpdated);
        tool.onVisualUpdated.connect(&disp, &MainDisplayWidget::onVisualUpdated);
        prop.onVisualUpdated.connect(&disp, &MainDisplayWidget::onVisualUpdated);
        objTree.onVisibilityUpdated.connect(&disp, &MainDisplayWidget::onVisualUpdated);
        menu.onVisualUpdated.connect(&disp, &MainDisplayWidget::onVisualUpdated);
        via.onVisualUpdated.connect(&disp, &MainDisplayWidget::onVisualUpdated);

        tool.onToolChanged.connect(&disp, &MainDisplayWidget::onToolChanged);
        tool.onFinalizeTool.connect(&disp, &MainDisplayWidget::onFinalizeTool);
        tool.onViewSettingChanged.connect(&disp, &MainDisplayWidget::onViewSettingChanged);

        objTree.onSelectionChanged.connect(&driver, &DriverHolder::onSelectionChanged);
        objTree.onSelectionChanged.connect(mProperty, &PropertyWidget::onSelectionChanged);
        objTree.onSelectionChanged.connect(&timeLine, &TimeLineWidget::onSelectionChanged);

        objTree.onTreeViewUpdated.connect(&timeLine, &TimeLineWidget::onTreeViewUpdated);
        objTree.onScrollUpdated.connect(&timeLine, &TimeLineWidget::onScrollUpdated);
        timeLine.onVerticalScrollRequested.connect(
            [&objTree](int aValue) { objTree.verticalScrollBar()->setValue(aValue); });

        timeLine.onFrameUpdated.connect(&driver, &DriverHolder::onFrameUpdated);
        timeLine.onFrameUpdated.connect(&prop, &PropertyWidget::onFrameUpdated);
        timeLine.onPlayBackStateChanged.connect(&prop, &PropertyWidget::onPlayBackStateChanged);

        menu.onProjectAttributeUpdated.connect(&disp, &MainDisplayWidget::onProjectAttributeUpdated);
        menu.onProjectAttributeUpdated.connect(&timeLine, &TimeLineWidget::onProjectAttributeUpdated);
        menu.onProjectAttributeUpdated.connect(&driver, &DriverHolder::onProjectAttributeUpdated);
        menu.onTimeFormatChanged.connect(&timeLine, &TimeLineWidget::triggerOnTimeFormatChanged);

        mGUIResources.onThemeChanged.connect(this, &MainWindow::onThemeUpdated);

        mSystem.setAnimator(*mTarget);
    }

    this->setFocusPolicy(Qt::StrongFocus);

#if 0
    auto scUndo = new QShortcut(QKeySequence("Ctrl+Z"), this);
    auto scRedo = new QShortcut(QKeySequence("Ctrl+Shift+Z"), this);
    //scUndo->setContext(Qt::WidgetWithChildrenShortcut);
    //scRedo->setContext(Qt::WidgetWithChildrenShortcut);
    scUndo->setContext(Qt::ApplicationShortcut);
    scRedo->setContext(Qt::ApplicationShortcut);

    this->connect(scUndo, &QShortcut::activated, [=](){ this->onUndoTriggered(); });
    this->connect(scRedo, &QShortcut::activated, [=](){ this->onRedoTriggered(); });
#else
    {
        auto key = mKeyCommandMap->get("Undo");
        if (key)
            key->addListener([=]() { this->onUndoTriggered(); });
    }
    {
        auto key = mKeyCommandMap->get("Redo");
        if (key)
            key->addListener([=]() { this->onRedoTriggered(); });
    }
    {
        auto key = mKeyCommandMap->get("SaveProject");
        if (key)
            key->addListener([=]() { this->onSaveProjectTriggered(); });
    }
    {
        auto key = mKeyCommandMap->get("PlayPause");
        if (key)
            key->addListener([=]() { this->onPlayPauseTriggered(); });
    }
    {
        auto key = mKeyCommandMap->get("ToggleDocks");
        if (key)
            key->addListener([=]() { this->onDockToggle(); });
    }
    {
        auto key = mKeyCommandMap->get("ToggleRepeat");
        if (key)
            key->addListener([=]() { this->onLoopToggle(); });
    }
    {
        auto key = mKeyCommandMap->get("MoveRight");
        if (key)
            key->addListener([=]() { this->onDisplacementTriggered(1); });
    }
    {
        auto key = mKeyCommandMap->get("MoveLeft");
        if (key)
            key->addListener([=]() { this->onDisplacementTriggered(-1); });
    }
    {
        auto key = mKeyCommandMap->get("MoveToInit");
        if (key)
            key->addListener([=]() { this->onMovementTriggered("Init"); });
    }
    {
        auto key = mKeyCommandMap->get("MoveToLast");
        if (key)
            key->addListener([=]() { this->onMovementTriggered("Last"); });
    }
    {
        auto key = mKeyCommandMap->get("MoveFrameLeft");
        if(key)
            key->addListener([=]() {this->onMoveFrameTriggered(false);});
    }
    {
        auto key = mKeyCommandMap->get("MoveFrameRight");
        if(key)
            key->addListener([=]() {this->onMoveFrameTriggered(true);});
    }
#endif

    // autosave
    // The settings dialog applies autosave changes live through
    // onGeneralSettingsApplied; the timer is (re)configured centrally here.
    mViaPoint.onGeneralSettingsChanged.connect(this, &MainWindow::onGeneralSettingsApplied);
    updateAutosaveState();
}

MainWindow::~MainWindow() { closeAllProjects(); }

void MainWindow::onGeneralSettingsApplied() { updateAutosaveState(); }

void MainWindow::updateAutosaveState() {
    QSettings settings;
    // Missing key defaults ON, matching the GeneralSettingsDialog checkbox.
    const QVariant enabledVar = settings.value("generalsettings/projects/enableAutosave");
    const bool enabled = !enabledVar.isValid() || enabledVar.toBool();
    const QVariant delayVar = settings.value("generalsettings/projects/autosaveDelay");
    int delayMin = delayVar.isValid() ? delayVar.toInt() : 5;
    if (delayMin < 1) {
        delayMin = 1; // 0 would arm a continuously-firing timer
    }

    if (enabled) {
        if (!autosaveTimer) {
            autosaveTimer = new QTimer(this);
            connect(autosaveTimer, SIGNAL(timeout()), this, SLOT(autoSave()));
        }
        // start() arms a fresh countdown; on a running timer it restarts it.
        autosaveTimer->start(delayMin * 60000);
    } else if (autosaveTimer) {
        autosaveTimer->stop();
    }
}

void MainWindow::autoSave() {
    if (!autosaveTimer) {
        return;
    }

    QSettings settings;
    const QVariant enabledVar = settings.value("generalsettings/projects/enableAutosave");
    if (!enabledVar.isValid() || !enabledVar.toBool()) {
        autosaveTimer->stop();
        return;
    }

    const QVariant delayVar = settings.value("generalsettings/projects/autosaveDelay");
    int autoDelay = delayVar.isValid() ? delayVar.toInt() : 5;
    if (autoDelay < 1) {
        autoDelay = 1;
    }
    // Re-arm so the next autosave is a full interval after this one, not
    // relative to app launch or the last manual save.
    autosaveTimer->start(autoDelay * 60000);

    // Save every modified project — not just the active tab. Background tabs
    // otherwise never get autosaved, losing work on a crash.
    for (int i = 0; i < mSystem.projectCount(); ++i) {
        core::Project* project = mSystem.project(i);
        if (!project || !project->isModified()) {
            continue;
        }

        if (project->isNameless()) {
            // Never-saved projects get a recovery copy in the app cache. The
            // project stays unsaved (dirty flag and Save As prompt remain);
            // the log line names the file so it can be reopened after a crash.
            EventSuspender suspender(*mMainDisplay, *mTarget, false);
            QString recoveryPath;
            auto result = mSystem.saveRecoveryCopy(*project, recoveryPath);
            if (result) {
                mViaPoint.pushLog("Automatically saved unsaved project to: " + recoveryPath, ctrl::UILogType_Info);
            } else {
                mViaPoint.pushLog(
                    "Failed to automatically save unsaved project: " + result.message, ctrl::UILogType_Warn
                );
            }
        } else {
            const QString label = QFileInfo(project->fileName()).fileName();
            if (processProjectSaving(*project, false, true, false)) {
                mViaPoint.pushLog("Automatically saved project: " + label, ctrl::UILogType_Info);
            } else {
                mViaPoint.pushLog("Failed to automatically save project: " + label, ctrl::UILogType_Warn);
            }
        }
    }
}

void MainWindow::showWithSettings() {
#if defined(QT_NO_DEBUG) || 1
    QSettings settings;
    auto winSize = settings.value("mainwindow/size");
    auto isMax = settings.value("mainwindow/ismaximized");

    if (winSize.isValid()) {
        this->resize(winSize.toSize());
    }

    if (!isMax.isValid() || isMax.toBool()) {
        this->showMaximized();
    } else {
        this->show();
    }
#else
    this->showMaximized();
#endif

    // Offer crash-recovery of unsaved projects from a previous session once
    // the window is up; the prompt needs a visible parent.
    QTimer::singleShot(0, this, [this]() { offerRecoveryCopies(); });
}

void MainWindow::saveCurrentSettings(int aResultCode) {
#if defined(QT_NO_DEBUG) || 1
    if (aResultCode == 0) {
        QSettings settings;
        settings.setValue("mainwindow/ismaximized", this->isMaximized());
        if (!this->isMaximized()) {
            settings.setValue("mainwindow/size", this->size());
        }
    }
#else
    (void)aResultCode;
#endif
}

void MainWindow::testNewProject(const QString& aFilePath) {
    resetProjectRefs(nullptr);

    menu::ProgressReporter progress(false, this);

    core::Project::Attribute attribute;
    auto result = mSystem.newProject(aFilePath, attribute, new ProjectHook(), progress, false);

    if (result) {
        resetProjectRefs(result.project);
        mProjectTabBar->pushProject(*result.project);

        mMainDisplay->resetCamera();
    }
}

void MainWindow::closeAllProjects() {
    mProjectTabBar->removeAllProject();
    resetProjectRefs(nullptr);
    mSystem.closeAllProjects();
}

void MainWindow::resetProjectRefs(core::Project* aProject) {
    mCurrent = aProject;

    /// @note Maybe a sequence of connections is meaningful.

    if (aProject) {
        mDriverHolder->create(*aProject, *mMainDisplayStyle);
    } else {
        mDriverHolder->destroy();
    }

    mMainMenuBar->setProject(aProject);
    mMainDisplay->setProject(aProject);
    mTarget->setProject(aProject);
    mProperty->setProject(aProject);
    mResourceDialog->setProject(aProject);
    mViaPoint.setProject(aProject);

    mMainDisplay->setDriver(mDriverHolder->driver());
    mTool->setDriver(mDriverHolder->driver());
}

void MainWindow::onProjectTabChanged(core::Project& aProject) { resetProjectRefs(&aProject); }

void MainWindow::onThemeUpdated(theme::Theme& aTheme) {
    mMainDisplay->setViewportBackground(mGUIResources.viewportBackground());

    const QString standard = aTheme.loadStylesheet("standard.ssa");
    if (!standard.isEmpty()) {
        QString fontOption;
        {
            auto hasFamily = !mLocaleParam.fontFamily.isEmpty();
            auto hasSize = !mLocaleParam.fontSize.isEmpty();
            fontOption = "QWidget {" + (hasFamily ? ("font-family: " + mLocaleParam.fontFamily + ";") : "") +
                (hasSize ? ("font-size: " + mLocaleParam.fontSize + ";") : "") + " }\n";
            // top-level section headers step up one point from the base
            // font (QSS cannot express a relative size, so derive it from
            // the locale's pt value; the rule lives here so it tracks the
            // base font across locale/settings changes)
            if (hasSize) {
                const QString size = mLocaleParam.fontSize.trimmed();
                if (size.endsWith("pt", Qt::CaseInsensitive)) {
                    bool ok = false;
                    const qreal pt = size.left(size.size() - 2).toDouble(&ok);
                    if (ok) {
                        fontOption += QString("QAbstractButton#propertyPanelHeader { font-size: %1pt; }\n")
                                          .arg(QString::number(pt + 1.0, 'f', 0));
                    }
                }
            }
        }

        this->setStyleSheet(fontOption + standard);
    }

    mDockPropertyWidget->setStyleSheet(aTheme.loadStylesheet("propertywidget.ssa"));
    mDockToolWidget->setStyleSheet(aTheme.loadStylesheet("toolwidget.ssa"));

    if (!onStartup && !themeChangeWarned) {
        QMessageBox visualArtifactWarning;
        visualArtifactWarning.setWindowTitle(tr("Theme changed"));
        visualArtifactWarning.setText(
            tr("There may be visual artifacts after changing themes, we recommend you restart "
               "the application.")
        );
        visualArtifactWarning.setDefaultButton(QMessageBox::Ok);
        visualArtifactWarning.exec();
        themeChangeWarned = true;
    }
    else{
        onStartup = false;
    }
}

#if 0
void MainWindow::keyPressEvent(QKeyEvent* aEvent)
{
    //qDebug() << "mainwindow: input key =" << aEvent->key() << "text =" << aEvent->text();
    bool shouldUpdate = false;

    if (mSystem.project())
    {        
        if(aEvent->key() == Qt::Key_Z)
        {
            if (aEvent->modifiers().testFlag(Qt::ControlModifier))
            {
                if (aEvent->modifiers().testFlag(Qt::ShiftModifier))
                {
                    mSystem.project()->commandStack().redo();
                    shouldUpdate = true;
                    qDebug() << "redo";
                }
                else
                {
                    mSystem.project()->commandStack().undo();
                    shouldUpdate = true;
                    qDebug() << "undo";
                }
            }
        }
    }

    if (shouldUpdate)
    {
        mMainDisplay->updateRender();
    }
    /*
    else
    {
        mMainDisplay->throwKeyPress(aEvent);
    }
    */
}
#endif

QList<QString> disallowedRepeats = {"MoveCanvas", "RotateCanvas", "SaveProject"};

void MainWindow::keyPressEvent(QKeyEvent* aEvent) {
    // qDebug() << "input key =" << aEvent->key() << "text =" << aEvent->text();
    if (aEvent->isAutoRepeat()) {
        for (auto command : mKeyCommandMap->commands()) {
            if (disallowedRepeats.contains(command->key)) {
                const ctrl::KeyBinding keyBind(aEvent->key(), aEvent->modifiers());
                if (command->binding.matchesExactlyWith(keyBind)) {
                    return;
                }
            }
        }
        // Default delay of 125ms
        QSettings settings;
        auto delay = settings.value("generalsettings/keybindings/keyDelay");
        qint64 delayInMs = delay.isValid() ? delay.toInt() : 125;
        if (lastPress + delayInMs < timeElapsed.elapsed()) {
            lastPress = timeElapsed.elapsed();
            mKeyCommandInvoker->onKeyPressed(aEvent);
            QMainWindow::keyPressEvent(aEvent);
        } else {
            // qDebug() << "Time elapsed is :" << (lastPress+500)-timeElapsed.elapsed();
            return;
        }
    } else {
        mKeyCommandInvoker->onKeyPressed(aEvent);
        QMainWindow::keyPressEvent(aEvent);
    }
}

void MainWindow::keyReleaseEvent(QKeyEvent* aEvent) {
    // qDebug() << "release key =" << aEvent->key() << "text =" << aEvent->text();
    if (aEvent->isAutoRepeat()) {
        for (auto command : mKeyCommandMap->commands()) {
            if (disallowedRepeats.contains(command->key)) {
                const ctrl::KeyBinding keyBind(aEvent->key(), aEvent->modifiers());
                if (command->binding.matchesExactlyWith(keyBind)) {
                    return;
                }
            }
        }
        // Default delay of 125ms
        QSettings settings;
        auto delay = settings.value("generalsettings/keybindings/keyDelay");
        qint64 delayInMs = delay.isValid() ? delay.toInt() : 125;
        if (lastRelease + delayInMs < timeElapsed.elapsed()) {
            lastRelease = timeElapsed.elapsed();
            mKeyCommandInvoker->onKeyReleased(aEvent);
            QMainWindow::keyPressEvent(aEvent);
        } else {
            // qDebug() << "time elapsed is :" << (lastPress+500)-timeElapsed.elapsed();
            return;
        }
    } else {
        mKeyCommandInvoker->onKeyReleased(aEvent);
        QMainWindow::keyReleaseEvent(aEvent);
    }
}

void MainWindow::closeEvent(QCloseEvent* aEvent) {
    QVector<core::Project*> projects;
    for (int i = 0; i < mSystem.projectCount(); ++i) {
        auto* project = mSystem.project(i);
        if (project) {
            projects.push_back(project);
        }
    }
    if (!closeProjects(projects)) {
        aEvent->ignore();
        return;
    }
    aEvent->accept();
}

bool MainWindow::closeProjects(const QVector<core::Project*>& aProjects) {
    bool anyModified = false;
    for (auto* project : aProjects) {
        if (project && project->isModified()) {
            anyModified = true;
            break;
        }
    }

    if (anyModified) {
        const int result = confirmProjectClosing(aProjects);
        if (result == QMessageBox::Cancel) {
            return false;
        }
        if (result == QMessageBox::Yes) {
            for (auto* project : aProjects) {
                if (project && project->isModified() && !processProjectSaving(*project)) {
                    return false;
                }
            }
        }
    }

    for (auto* project : aProjects) {
        if (project) {
            closeProjectTab(*project);
        }
    }
    return true;
}

void MainWindow::closeProjectTab(core::Project& aProject) {
    // Sneaky potential crash
    if (QFileSystemWatcher* watcher = getWatcher()) {
        // imageTrees() is a std::list, so its count is unsigned (size_type);
        // the index has to be that same unsigned type. It starts at 0 and
        // only counts up, so an unsigned index cannot wrap here, while the
        // old `int x` would have overflowed on a list longer than INT_MAX.
        for (size_t x = 0; x < aProject.resourceHolder().imageTrees().size(); x += 1) {
            if (watcher->files().contains(aProject.resourceHolder().findAbsoluteFilePath(
                    *aProject.resourceHolder().imageTree(x).topNode
                ))) {
                watcher->removePath(aProject.resourceHolder().findAbsoluteFilePath(
                    *aProject.resourceHolder().imageTree(x).topNode
                ));
            }
        }
    }

    // Only a closed ACTIVE project hands its playback state to the next tab
    // (continuity when closing what you're working on). Closing an inactive tab
    // must not overwrite the active project's mediaPlayer/config.
    const bool wasCurrent = (mProjectTabBar->currentProject() == &aProject);

    const auto playerBackup = *aProject.mediaPlayer;
    const auto configBackup = *aProject.pConf;

    const auto closeProject = &aProject;
    mProjectTabBar->removeProject(*closeProject);
    resetProjectRefs(nullptr); ///@note update mCurrent
    // A closed tab's autosave copy is stale — saved projects already cleared
    // it, discarded ones should not linger as litter in the app cache.
    mSystem.clearRecoveryCopy(*closeProject);
    mSystem.closeProject(*closeProject);

    if (mProjectTabBar->currentProject()) {
        if (wasCurrent) {
            mProjectTabBar->currentProject()->mediaPlayer = new mediaState(playerBackup);
            mProjectTabBar->currentProject()->pConf = new std::vector(configBackup);
        }
        resetProjectRefs(mProjectTabBar->currentProject());
    }
}

int MainWindow::confirmProjectClosing(const QVector<core::Project*>& aProjects) {
    // Scoped to the close-set: a single modified project is named, several use
    // the generic message. Matches the save loop in closeProjects, which also
    // only touches aProjects.
    QString singleName;
    bool found = false;
    for (auto* project : aProjects) {
        if (!project || !project->isModified()) {
            continue;
        }
        if (found) {
            singleName.clear();
            break;
        }
        singleName = mProjectTabBar->getTabName(*project);
        found = true;
    }

    if (!singleName.isEmpty()) {
        return confirmProjectClosing(singleName + tr(" has been modified. Save changes?"));
    }
    return confirmProjectClosing(tr("Some projects have been modified. Save changes?"));
}

int MainWindow::confirmProjectClosing(const QString& aMessage) {
    QMessageBox msgBox;

    msgBox.setText(aMessage);

    msgBox.addButton(tr("Save Changes"), QMessageBox::YesRole);
    msgBox.addButton(tr("Discard Changes"), QMessageBox::NoRole);
    auto cancel = msgBox.addButton(tr("Cancel"), QMessageBox::RejectRole);
    msgBox.setDefaultButton(cancel);
    msgBox.exec();
    auto clicked = msgBox.clickedButton();
    if (clicked) {
        auto role = msgBox.buttonRole(clicked);
        if (role == QMessageBox::YesRole)
            return QMessageBox::Yes;
        else if (role == QMessageBox::NoRole)
            return QMessageBox::No;
        else if (role == QMessageBox::RejectRole)
            return QMessageBox::Cancel;
    }
    return QMessageBox::Cancel;
}

void MainWindow::onUndoTriggered() {
    if (mCurrent) {
        bool undone = false;
        auto ret = mCurrent->commandStack().undo(&undone);
        if (undone) {
            mViaPoint.pushUndoneLog(tr("Undone : ") + ret);
            mMainDisplay->updateRender();
        }
    }
}

void MainWindow::onDisplacementTriggered(int frameDisplacement) {
    mTarget->timeLineWidget().setFrame(mTarget->currentFrame().added(frameDisplacement));
}

void MainWindow::onPlayPauseTriggered() {
    mTarget->playBackWidget().PlayPause(); // If it's not playing then it will begin playback
}

void MainWindow::onMovementTriggered(const QString& frameMovement) {
    if (frameMovement == "Last") {
        // Land on the effective playback range's end (the temporary window's
        // end while it overrides) — the project range's max would fall short
        // when the window extends past it.
        mTarget->timeLineWidget().setFrame(core::Frame(mTarget->timeLineWidget().playbackRange().max()));
    } else {
        // Land on the effective playback range's start (the temporary
        // window's start while it overrides); the playhead clamps into it.
        mTarget->timeLineWidget().setFrame(core::Frame(mTarget->timeLineWidget().playbackRange().min()));
    }
}

void MainWindow::onDockToggle() // For some reason it does not work without this travesty
{
    if (mDockPropertyWidget->isHidden()) {
        mDockPropertyWidget->show();
    } else if (!mDockPropertyWidget->isHidden()) {
        mDockPropertyWidget->hide();
    }

    if (mDockToolWidget->isHidden()) {
        mDockToolWidget->show();
    } else if (!mDockToolWidget->isHidden()) {
        mDockToolWidget->hide();
    }

    if (mTarget->isHidden()) {
        mTarget->show();
    } else if (!mTarget->isHidden()) {
        mTarget->hide();
    }
}

void MainWindow::onLoopToggle() {
    if (mTarget->playBackWidget().isLoopChecked()) {
        mTarget->timeLineWidget().setPlayBackLoop(false);
        mTarget->playBackWidget().checkLoop(false);
    } else {
        mTarget->timeLineWidget().setPlayBackLoop(true);
        mTarget->playBackWidget().checkLoop(true);
    }
}

void MainWindow::onRedoTriggered() {
    if (mCurrent) {
        bool redone = false;
        auto ret = mCurrent->commandStack().redo(&redone);
        if (redone) {
            mViaPoint.pushRedoneLog(tr("Redone : ") + ret);
            mMainDisplay->updateRender();
        }
    }
}

void MainWindow::onNewProjectTriggered() {
    // stop animation and main display rendering
    EventSuspender suspender(*mMainDisplay, *mTarget);

    // input attribute
    core::Project::Attribute attribute;
    QString fileName;
    bool specifiesCanvasSize = false;
    {
        QScopedPointer<NewProjectDialog> dialog(new NewProjectDialog(this));

        dialog->exec();
        if (dialog->result() != QDialog::Accepted) {
            return;
        }
        attribute = dialog->attribute();
        fileName = dialog->fileName();
        specifiesCanvasSize = dialog->specifiesCanvasSize();
    }

    // clear old project
    resetProjectRefs(nullptr);

    // try loading
    ctrl::System::LoadResult result;
    {
        menu::ProgressReporter progress(false, this);

        result = mSystem.newProject(fileName, attribute, new ProjectHook(), progress, specifiesCanvasSize);
    }

    if (result.project) {
        resetProjectRefs(result.project);
        mProjectTabBar->pushProject(*result.project);

        mMainDisplay->resetCamera();
    } else {
        QMessageBox::warning(nullptr, tr("Loading Error"), result.messages());

        if (mProjectTabBar->currentProject()) {
            resetProjectRefs(mProjectTabBar->currentProject());
        }
    }
}

QFileSystemWatcher* globalWatcher = new QFileSystemWatcher();

QFileSystemWatcher* MainWindow::getWatcher() { return globalWatcher; }

void MainWindow::showInfoPopup(
    const QString& aTitle, const QString& aDetailText, const QString& aIcon, const QString& aDetailed
) {
    QMessageBox box;
    if (aIcon == "Info") {
        box.setIcon(QMessageBox::Information);
    } else if (aIcon == "Warn") {
        box.setIcon(QMessageBox::Warning);
    } else if (aIcon == "Critical") {
        box.setIcon(QMessageBox::Critical);
    }
    box.setStandardButtons(QMessageBox::Ok);
    box.setDefaultButton(QMessageBox::Ok);
    box.setWindowTitle(aTitle);
    box.setText(aDetailText);
    if (aDetailed != "nullptr") {
        box.setDetailedText(aDetailed);
    }
    box.exec();
}

void MainWindow::onOpenProjectTriggered() {
    // stop animation and main display rendering
    EventSuspender suspender(*mMainDisplay, *mTarget);

    QString fileName = QFileDialog::getOpenFileName(this, tr("Open File"), "", "ProjectFile (*.anie)");
    if (fileName.isEmpty())
        return;

    // clear old project
    resetProjectRefs(nullptr);

    // try loading
    ctrl::System::LoadResult result;
    {
        menu::ProgressReporter progress(false, this);
        result = mSystem.openProject(fileName, new ProjectHook(), progress);
    }

    if (result) {
        resetProjectRefs(result.project);
        mProjectTabBar->pushProject(*result.project);

        mMainDisplay->resetCamera();
        mDriverHolder->driver()->updateFrame();
        mDriverHolder->driver()->updateProjectAttribute();

    } else {
        QMessageBox::warning(nullptr, tr("Loading Error"), result.messages());

        if (mProjectTabBar->currentProject()) {
            resetProjectRefs(mProjectTabBar->currentProject());
        }
    }
}

void MainWindow::onOpenRecentTriggered(QString aFileName) {
    // stop animation and main display rendering
    EventSuspender suspender(*mMainDisplay, *mTarget);

    QString fileName = std::move(aFileName);

    // clear old project
    resetProjectRefs(nullptr);

    // try loading
    ctrl::System::LoadResult result;
    {
        menu::ProgressReporter progress(false, this);
        result = mSystem.openProject(fileName, new ProjectHook(), progress);
    }

    if (result) {
        resetProjectRefs(result.project);
        mProjectTabBar->pushProject(*result.project);
        mMainDisplay->resetCamera();
    } else {
        QMessageBox::warning(nullptr, tr("Loading Error"), result.messages());

        if (mProjectTabBar->currentProject()) {
            resetProjectRefs(mProjectTabBar->currentProject());
        }
    }
}

void MainWindow::offerRecoveryCopies() {
    // Drop incomplete recovery writes first so they are never offered or
    // re-swept each launch; nothing here is a real project file.
    mSystem.sweepStaleRecoveryTmp();
    const QStringList paths = mSystem.recoveryCopyPaths();
    if (paths.isEmpty()) {
        return;
    }

    for (const QString& path : paths) {
        const auto answer = QMessageBox::question(
            this,
            tr("Recover unsaved project?"),
            tr("AnimeEffects found an unsaved project from a previous session:\n%1\n\nOpen it?").arg(
                QDir::toNativeSeparators(path)
            ),
            QMessageBox::Open | QMessageBox::Discard,
            QMessageBox::Open
        );
        if (answer == QMessageBox::Open) {
            openRecoveryCopy(path);
        } else if (answer == QMessageBox::Discard) {
            // Only an explicit Discard deletes the copy. Closing the prompt
            // (window X / Alt-F4) yields NoButton, and that must leave the
            // file so the work is still offered at the next launch.
            mSystem.removeRecoveryCopy(path);
        }
    }
}

void MainWindow::openRecoveryCopy(const QString& aFilePath) {
    // stop animation and main display rendering
    EventSuspender suspender(*mMainDisplay, *mTarget);

    // clear old project
    resetProjectRefs(nullptr);

    // try loading
    ctrl::System::LoadResult result;
    {
        menu::ProgressReporter progress(false, this);
        result = mSystem.openProject(aFilePath, new ProjectHook(), progress, false);
    }

    if (result) {
        // The recovery file is a crash artifact: keep the project unsaved so
        // Save prompts for a real location instead of writing into the cache
        // (the tab shows "New Project" and Save As asks for a path). The
        // images themselves are embedded in the file, so the copy reopens
        // fully; only the file-watch/re-import path metadata degrades.
        result.project->setFileName("");
        resetProjectRefs(result.project);
        mProjectTabBar->pushProject(*result.project);

        mMainDisplay->resetCamera();
        mDriverHolder->driver()->updateFrame();
        mDriverHolder->driver()->updateProjectAttribute();

        // Recovered: the copy has served its purpose.
        mSystem.removeRecoveryCopy(aFilePath);
    } else {
        QMessageBox::warning(nullptr, tr("Loading Error"), result.messages());

        if (mProjectTabBar->currentProject()) {
            resetProjectRefs(mProjectTabBar->currentProject());
        }
        // Keep the recovery file: opening failed, the user may retry.
    }
}

bool MainWindow::processProjectSaving(core::Project& aProject, bool aRename, bool aSilent, bool aStopPlayback) {
    // stop animation and main display rendering
    EventSuspender suspender(*mMainDisplay, *mTarget, aStopPlayback);

    if (aProject.isNameless() || aRename) {
        QString fileName = QFileDialog::getSaveFileName(this, tr("Save File"), "", "ProjectFile (*.anie)");

        // check cancel
        if (fileName.isEmpty()) {
            return false;
        }

        if (QFileInfo(fileName).suffix().isEmpty()) {
            fileName += ".anie";
        }
        aProject.setFileName(fileName);
    }

    // save
    auto result = mSystem.saveProject(aProject, !aSilent);
    if (!result) {
        if (aSilent) {
            // Autosave must never block the user with a modal dialog.
            mViaPoint.pushLog("Failed to save project: " + result.message, ctrl::UILogType_Warn);
        } else {
            QMessageBox::warning(nullptr, "Saving Error", result.message);
        }
        return false; // failed
    }

    mProjectTabBar->updateTabNames();
    // The project now has (or keeps) a real file — its recovery copy is stale.
    mSystem.clearRecoveryCopy(aProject);
    return true;
}

void MainWindow::onSaveProjectTriggered() {
    if (mCurrent) {
        processProjectSaving(*mCurrent);
    }
}

void MainWindow::onSaveProjectAsTriggered() {
    if (mCurrent) {
        processProjectSaving(*mCurrent, true);
    }
}

void MainWindow::onCloseProjectTriggered() {
    if (mCurrent) {
        closeProjects(QVector<core::Project*>(1, mCurrent));
    }
}

void MainWindow::onTabSaveRequested(core::Project& aProject) {
    processProjectSaving(aProject);
}

void MainWindow::onTabSaveAsRequested(core::Project& aProject) {
    processProjectSaving(aProject, true);
}

void MainWindow::onTabCloseRequested(core::Project& aProject) {
    closeProjects(QVector<core::Project*>(1, &aProject));
}

void MainWindow::onTabCloseOthersRequested(core::Project& aProject) {
    QVector<core::Project*> others;
    for (int i = 0; i < mSystem.projectCount(); ++i) {
        auto* project = mSystem.project(i);
        if (project && project != &aProject) {
            others.push_back(project);
        }
    }
    closeProjects(others);
}

void MainWindow::onTabCloseAllRequested() {
    QVector<core::Project*> projects;
    for (int i = 0; i < mSystem.projectCount(); ++i) {
        auto* project = mSystem.project(i);
        if (project) {
            projects.push_back(project);
        }
    }
    closeProjects(projects);
}

void MainWindow::onTabCopyPathRequested(core::Project& aProject) {
    const QString path = aProject.fileName();
    if (!path.isEmpty()) {
        QGuiApplication::clipboard()->setText(QFileInfo(path).absoluteFilePath());
    }
}

// Variable is set both locally and in the settings, a bit overkill...
void updateSettings(QVariant *var, QSettings *settings, int value, const QString& key){
    if(var->toInt() != value){
        var->setValue(value);
        settings->setValue(key, value);
    }
}
void updateSettings(QVariant *var, QSettings *settings, bool value, const QString& key){
    if(var->toBool() != value){
        var->setValue(value);
        settings->setValue(key, value);
    }
}
void updateSettings(QVariant *var, QSettings *settings, const QString& value, const QString& key){
    if(var->toString() != value){
        var->setValue(value);
        settings->setValue(key, value);
    }
}
void updateSettings(QVariant *var, QSettings *settings, const QStringList& value, const QString& key){
    if(QStringList(var->toStringList()) != value){
        var->setValue(value);
        settings->setValue(key, value);
    }
}

// Convert the app's audio config vector into media::AudioMixer tracks.
static QVector<media::AudioMixer::Track> toAudioTracks(const std::vector<audioConfig>& aStreams) {
    QVector<media::AudioMixer::Track> tracks;
    tracks.reserve(int(aStreams.size()));
    for (const auto& ac : aStreams) {
        // The mapping lives on the mixer's own type so it can be tested without the
        // GUI; the volume curve stays here because it belongs to the widget.
        tracks.push_back(media::AudioMixer::Track::fromSettings(
            ac.audioPath.absoluteFilePath(), AudioPlaybackWidget::getVol(ac.volume),
            ac.startFrame, ac.endFrame, ac.exportEnable));
    }
    return tracks;
}

// Map the dialog's per-container encoder combo to the raw friendly encoder name
// (e.g. "x264"), or "" for Auto so the exporter picks a muxer-aware default.

// Fill the video-specific bits of a ctrl::Exporter::VideoParam from an exportParam.
//
// The container and the codec are names here, not indices into a table: that
// table (and the getFormatAsInt() that could not read it back) is gone. An empty
// codec means "the container's probed default", which is what the exporter
// resolves, so "Auto" is no longer a value pretending to be a choice.
static void translateVideoExport(exportParam& exParam, const std::vector<audioConfig>& aValidStreams,
                                 ctrl::Exporter::VideoParam& aVideo,
                                 QVector<media::AudioMixer::Track>& aAudio) {
    auto& g = exParam.generalParams;
    aVideo.container = exParam.videoParams.container;
    aVideo.codec = exParam.videoParams.codec;
    // No pixel format: it follows from the codec and from whether alpha is
    // wanted, and the encoder picks between the two by asking.
    aVideo.pixfmt.clear();
    aVideo.transparent = g.allowTransparency;
    aVideo.bps = g.bitrate;              // kbps (the exporter converts to bits/sec)
    aAudio = g.exportWithAudio ? toAudioTracks(aValidStreams) : QVector<media::AudioMixer::Track>();

    // The quality intent, through the codec's OWN knob - the same table
    // ctrl::planExport() uses for a destination, so "Balanced" cannot mean one
    // thing here and another thing there. Before this the intent reached only the
    // destination path: in manual mode the Quality control set nothing, and the
    // file went out at whatever the Bitrate field said (or at the encoder's own
    // default, which is what "Auto" means). A destination overwrites encOptions
    // afterwards, so this cannot fight the plan.
    //
    // An empty codec means "the container's probed default", which is what the
    // exporter resolves, so it is resolved here too: the knob differs per codec
    // ('crf' for H.264, 'quality' for WebP) and picking the wrong one would hand
    // avcodec_open2 an option it rejects.
    const QString codec = aVideo.codec.isEmpty()
                              ? media::ffmpeg::defaultCodecForContainer(aVideo.container)
                              : aVideo.codec;
    const QString knob = ctrl::qualityKnobFor(codec);
    if (!knob.isEmpty()) {
        // The dialog states "no override" as the codec's own sentinel (one below the
        // span it accepts), because that is the only value a box ranged to that span
        // can still hold. Reading it as "any negative number" was fine while the
        // sentinel was -1; what was NOT fine is that the range clamped -1 away, so this
        // branch always took the override and the Quality setting never decided.
        const ctrl::QualityControl control =
            ctrl::ExportTargets::loadQualityControl().value(codec);
        if (g.qualityOverride > control.autoValue()) {
            aVideo.encOptions.insert(knob, QString::number(g.qualityOverride));
        } else {
            ctrl::applyQualityIntent(codec, exParam.qualityBucket, aVideo.encOptions);
        }
    }
}

// A non-selectable group heading inside a QComboBox. Qt's own idiom for this is
// to clear the item's flags through the model.

// The label a user picks from, which has to say when a row cannot be produced
// here. The table declares `platform` for exactly this and nothing read it: on
// Linux the list offered "Transparent video (Safari / iOS)", and selecting it
// produced "encoder not available in this build: hevc_videotoolbox" — a message
// that reads like a broken installation rather than a platform requirement.
static QString targetLabel(const ctrl::ExportTarget& aTarget) {
    if (aTarget.platform.isEmpty() || aTarget.platform == QLatin1String("any"))
        return aTarget.label;
    // QStringLiteral, not QLatin1String: the em dash is UTF-8 in the source, and
    // Latin-1 is the wrong decoder for it. Getting this wrong is how
    // "— needs macOS" became "â€” needs macOS" in the destination list the first
    // time this label was written.
    return aTarget.label + QStringLiteral(" (") + QObject::tr("needs %1").arg(ctrl::platformName(aTarget.platform)) + QStringLiteral(")");
}

// The Quality row's index <-> the bucket the plan uses. ONE mapping, because the
// dialog and the quick-export menu both need it and they disagreed: the menu
// hardcoded Balanced while the dialog stored the user's choice.
static ctrl::QualityBucket bucketFromSettingsIndex(int aIndex) {
    switch (aIndex) {
    case 0: return ctrl::QualityBucket::Smaller;
    case 2: return ctrl::QualityBucket::Best;
    case 3: return ctrl::QualityBucket::Lossless;
    default: return ctrl::QualityBucket::Balanced;
    }
}

// Returns true only when something was actually written. The callers need that:
// "export again" and the status line both have to tell a completed export from a
// refused one, and an export that reports success without a file is the failure
// mode this whole area keeps producing.
bool exportProject(exportParam& exParam, core::Project* mCurrent, QDialog* widget, const std::vector<audioConfig>& validAudioStreams){
    // All full-export dialog exports now run through the single in-process
    // ctrl::Exporter (linked libav*): video, image sequence, and GIF. The legacy
    // projectExporter::Exporter + ffmpeg:: CLI machinery is no longer used.
    auto& g = exParam.generalParams;
    const bool isImage = exParam.exportType == exportTarget::image;

    // Common parameters (path differs per target below).
    ctrl::Exporter::CommonParam cparam;
    cparam.size = QSize(g.exportWidth, g.exportHeight);
    cparam.fps = g.fps > 0 ? g.fps : (g.nativeFPS > 0 ? g.nativeFPS : 24);
    // Every range the user selected, in the order they are listed. This used to
    // collapse them to [first of the first .. last of the last], so a gap was
    // exported as if it had been selected: ranges 0-10 + 50-60 wrote 61 frames
    // instead of 22. The exporter now takes the list.
    if (!g.exportRange.isEmpty()) {
        ctrl::FrameSelection selection;
        for (const frameExportRange& r : g.exportRange) {
            const int a = std::min(r.firstFrame, r.lastFrame);
            const int b = std::max(r.firstFrame, r.lastFrame);
            selection.ranges.append(ctrl::FrameRange{a, b});
        }
        cparam.selection = selection;
    } else {
        cparam.selection = ctrl::FrameSelection::fromRange(g.nativeFrameRange.firstFrame,
                                                          g.nativeFrameRange.lastFrame);
    }

    // A chosen destination is AUTHORITATIVE: it decides the container, the codec,
    // that codec's private options and stream tag, transparency, the exact output
    // size (art fitted and padded, never stretched), the frame caps, the audio
    // parameters and the frame selection. Resolved before the branches because it
    // also decides WHICH branch runs (a GIF destination is not a VideoEncoder
    // target). This is what replaces the per-container codec combos, which could
    // not work at all: getFormatAsInt() returns 0 for every input, so every codec
    // and pixel format chosen in the dialog was silently discarded.
    ctrl::ExportPlan plan;
    bool havePlan = false;
    // The frame stem the image branch must use, resolved with the plan (see below).
    QString imageStem;
    // Resolved for EVERY destination, including one that produces stills: the plan
    // is what says whether the artifact is a video, one image or a frame sequence,
    // so it cannot be skipped for the image case it is asked to decide. It used to
    // run only when the artifact was not an image, which meant an image destination
    // could not be planned at all.
    if (!exParam.destinationId.isEmpty()) {
        const ctrl::ExportTarget target =
            ctrl::ExportTargets::find(ctrl::ExportTargets::load(), exParam.destinationId);
        if (target.id.isEmpty()) {
            QMessageBox::warning(widget, QObject::tr("Export Error"),
                                 QObject::tr("Unknown export destination: %1").arg(exParam.destinationId));
            return false;
        }
        ctrl::ExportRequest request;
        request.outputPath = g.osExportTarget;
        request.sourceSize = mCurrent->attribute().imageSize();
        // The user's own size is an input only where the destination does not
        // impose one; for every other rule the plan's size wins and this is
        // ignored (see ExportRequest::requestedSize).
        request.requestedSize = QSize(g.exportWidth, g.exportHeight);
        request.fps = cparam.fps;
        request.selection = cparam.selection;
        // Audio is derived, not requested: how many tracks are actually meant for
        // the file, and whether the user asked for silence.
        int exportableTracks = 0;
        for (const audioConfig& ac : validAudioStreams) {
            if (ac.exportEnable) ++exportableTracks;
        }
        request.exportableAudioTracks = exportableTracks;
        request.mute = !g.exportWithAudio;
        // A still destination exports the frame on screen; the plan takes it from
        // the request rather than the range (see ExportRequest::playhead).
        request.playhead = exParam.playhead;
        // The same stem the exporter would use for a frame sequence, so a fitted
        // set has the same file names as an unfitted one.
        request.imageStem = QFileInfo(g.fileName).baseName();
        if (request.imageStem.isEmpty() && target.artifact == ctrl::ArtifactKind::Sequence) {
            // The stem for numbered frames. The exporter's own rule prefers the
            // project name, which the dialog does not always pass down; falling back
            // to the name the user typed keeps a fitted set and an unfitted one
            // identically named instead of "0001.png" in one case and
            // "ugoira0001.png" in the other.
            request.imageStem = QFileInfo(g.osExportTarget).completeBaseName();
        }
        imageStem = request.imageStem;
        // GIF parameters, so a fitted GIF is the same file a plain export writes.
        request.gifPalettePath = (g.useCustomPalette && g.palettePath.exists())
                                     ? g.palettePath.absolutePath()
                                     : QString();
        request.gifLoop = g.loop ? 0 : -1;

        plan = ctrl::planExport(target, request, exParam.qualityBucket);
        if (!plan.ok()) {
            QMessageBox::warning(widget, QObject::tr("Export Error"), plan.error);
            return false;
        }
        havePlan = true;
        cparam.size = plan.size;
        cparam.renderSize = plan.renderSize;
        cparam.fit = plan.config.fit;
        cparam.fps = plan.fps;
        cparam.selection = plan.selection;
    }

    // GIF is the one artifact media::GifEncoder owns rather than libav, so which
    // branch runs follows from the container itself.
    const bool isGif = havePlan ? plan.gifPath
                                : (exParam.exportType == exportTarget::video &&
                                   exParam.videoParams.container == QLatin1String("gif"));

    ctrl::Exporter exporter(*mCurrent);
    exporter.setOverwriteConfirmer([widget](const QString& aPath) -> bool {
        QMessageBox m(QMessageBox::Question, QObject::tr("File exists"),
                      QObject::tr("The file \"%1\" already exists. Overwrite?").arg(aPath),
                      QMessageBox::Ok | QMessageBox::Cancel, widget);
        return m.exec() == QMessageBox::Ok;
    });
    menu::LoggableProgressReporter progress(true, widget);
    exporter.setProgressReporter(progress);

    // A destination with a hard byte cap is the one case where a single encode is
    // not enough. Telegram rejects a sticker over 256 KB, WeChat over 500 KB,
    // Twitch over 1 MB, and so on — those are upload rejections, not preferences
    // — and the only way to meet them is to encode, MEASURE, and encode again
    // lower if the file is over.
    //
    // This is set up BEFORE the artifact branches, because a cap is a property of
    // the destination and not of the video path: GIF, APNG and WebP destinations
    // are capped too, and this block used to sit inside the video branch alone, so
    // every capped GIF (Twitch emote, WeChat sticker, QQ sticker) was written at
    // whatever size it came out while the dialog stated the limit it had to meet.
    //
    // exportWithFit() has done exactly that since it was written, and nothing in
    // the app ever called it: the caps were stated in the dialog and enforced
    // nowhere, so the user got a file the platform refused with no warning. So for
    // a capped destination the exporter renders the frames and hands them over
    // instead of encoding.
    bool fitFailed = false;
    if (havePlan && plan.target.caps.maxBytes > 0) {
        ctrl::ExportTarget fitTarget = plan.target;
        ctrl::ExportRequest fitRequest;
        fitRequest.outputPath = g.osExportTarget;
        fitRequest.sourceSize = mCurrent->attribute().imageSize();
        fitRequest.requestedSize = QSize(g.exportWidth, g.exportHeight);
        fitRequest.fps = cparam.fps;
        fitRequest.selection = cparam.selection;
        fitRequest.gifPalettePath = (g.useCustomPalette && g.palettePath.exists())
                                        ? g.palettePath.absolutePath()
                                        : QString();
        fitRequest.gifLoop = g.loop ? 0 : -1;

        // Fitting needs every frame in memory at once. Bounded by the row's own
        // caps (a 512px, 3-second sticker is ~70 MB), but checked rather than
        // assumed, so a future row with a byte cap on a long video is refused
        // with a reason instead of exhausting memory.
        const qint64 bytesEach = qint64(plan.renderSize.width()) * plan.renderSize.height() * 4;
        const qint64 needed = bytesEach * plan.frames();
        const qint64 kBudget = 512LL * 1024 * 1024;
        if (needed > kBudget) {
            QMessageBox::warning(widget, QObject::tr("Export Error"),
                                 QObject::tr("%1 caps the file size, which means encoding it "
                                             "more than once, and that needs all %2 frames in "
                                             "memory (~%3 MB). Export fewer frames, or choose a "
                                             "destination without a size limit.")
                                     .arg(plan.target.label)
                                     .arg(plan.frames())
                                     .arg(needed / (1024 * 1024)));
            return false;
        }

        exporter.setFrameSink([&](const QList<QImage>& frames) {
            // The fit phase is encoding, not rendering: saying "Rendering..." while
            // it re-encodes a dozen times leaves the user watching a stuck 98%. So
            // the section says what is happening and every attempt is logged as it
            // happens (pushLog pumps events, so the dialog also stays alive).
            progress.setSection(QObject::tr("Fitting under %1's %2 KB limit...")
                                    .arg(plan.target.label)
                                    .arg(plan.target.caps.maxBytes / 1024));
            const ctrl::FitResult fit = ctrl::exportWithFit(
                fitTarget, fitRequest, exParam.qualityBucket, frames,
                [&progress](const QString& aStep) {
                    progress.pushLog(aStep, ctrl::UILogType_Info);
                },
                [&progress] { return progress.wasCanceled(); });
            if (fit.canceled) {
                // Say what is on disk rather than deleting it: the last attempt is
                // a complete, playable file, and for an already-fitted attempt it
                // is a good one. Which attempt it was is exactly what the log says.
                QMessageBox::information(widget, QObject::tr("Export Canceled"),
                                         QObject::tr("The export was stopped while fitting under "
                                                     "%1's size limit. The file already written is "
                                                     "the last attempt, and may exceed the limit.")
                                             .arg(plan.target.label));
                return;
            }
            if (!fit.ok) {
                fitFailed = true;
                QMessageBox::warning(widget, QObject::tr("Export Error"),
                                     QObject::tr("%1 caps the file at %2 KB and even the "
                                                 "smallest setting did not fit it (%3 KB).\n%4")
                                         .arg(plan.target.label)
                                         .arg(plan.target.caps.maxBytes / 1024)
                                         .arg(fit.bytes / 1024)
                                         .arg(fit.error));
                return;
            }
            progress.pushLog(QObject::tr("Fitted to %1 bytes in %2 attempts (%3x%4).")
                                 .arg((qlonglong)fit.bytes)
                                 .arg(fit.attempts)
                                 .arg(fit.finalSize.width())
                                 .arg(fit.finalSize.height()),
                             ctrl::UILogType_Info);
        });
    }

    // Image artifacts: one file (a still destination) or a directory of numbered
    // stills (a frame-sequence destination, or the escape hatch's PNG/JPEG
    // sequence). Both are written by Qt's image writers inside the exporter, which
    // is why they share a branch.
    const bool stillArtifact = havePlan && plan.artifact == ctrl::ArtifactKind::Still;
    if (isImage || stillArtifact) {
        ctrl::Exporter::ImageParam iparam;
        iparam.suffix = exParam.imageParams.suffix.isEmpty() ? QStringLiteral("png")
                                                            : exParam.imageParams.suffix;
        if (havePlan && plan.artifact != ctrl::ArtifactKind::Video) {
            iparam.suffix = plan.imageSuffix;
        }
        iparam.quality = g.imageExportQuality;
        iparam.singleFile = stillArtifact;
        if (stillArtifact) {
            // The save dialog asked for a FILE, so the file it was given is what
            // gets written: directory plus the name the user typed, extension from
            // the destination.
            const QFileInfo chosen(g.osExportTarget);
            cparam.path = chosen.absolutePath();
            iparam.name = chosen.completeBaseName();
        } else {
            // A sequence is a directory of numbered frames; the name comes from the
            // project, which is what the dialog's directory-only chooser implies.
            cparam.path = g.exportDirectory.absolutePath();
            iparam.name = imageStem.isEmpty() ? QFileInfo(g.fileName).baseName() : imageStem;
        }
        auto res = exporter.execute(cparam, iparam);
        if (!res && !exporter.isCanceled())
            QMessageBox::warning(widget, QObject::tr("Export Error"), exporter.log());
        if (!res) return false;
        // A still wrote the path the user named; a sequence wrote numbered files in
        // the chosen directory. "Did it write?" has to be asked of the right one.
        const QString produced =
            stillArtifact ? cparam.path + QLatin1Char('/') + iparam.name + QLatin1Char('.')
                                + iparam.suffix
                          : cparam.path;
        return QFileInfo::exists(produced);
    }

    if (isGif) {
        ctrl::Exporter::GifParam gparam;
        cparam.path = g.osExportTarget;
        // Custom palette: the UI collects a PNG whose pixels ARE the palette
        // (the paletteuse convention). Feed it to the encoder; otherwise it
        // builds an adaptive palette from the rendered frames.
        gparam.transparent = g.allowTransparency;
        gparam.palettePath = (g.useCustomPalette && g.palettePath.exists())
                                 ? g.palettePath.absolutePath()
                                 : QString();
        auto res = exporter.execute(cparam, gparam);
        if (!res && !exporter.isCanceled())
            QMessageBox::warning(widget, QObject::tr("Export Error"), exporter.log());
        // Same rule as the video path: in sink mode the fit loop wrote the file,
        // so a reported fit failure must not be turned into a success by the file
        // the last attempt left behind.
        return bool(res) && !fitFailed && QFileInfo(g.osExportTarget).exists();
    }

    // Video.
    cparam.path = g.osExportTarget;
    ctrl::Exporter::VideoParam vparam;
    QVector<media::AudioMixer::Track> audioTracks;
    translateVideoExport(exParam, validAudioStreams, vparam, audioTracks);

    if (havePlan) {
        vparam.container = plan.config.container;
        vparam.codec = plan.config.codec;   // named explicitly by the destination
        vparam.pixfmt = plan.config.pixFmt;
        vparam.transparent = plan.alphaEnabled;
        vparam.encOptions = plan.config.encOptions;
        vparam.codecTag = plan.config.codecTag;
        vparam.bps = 0;                    // quality comes from the codec's own controls
        vparam.audioCodec = plan.config.audioCodec;
        vparam.audioBitrate = plan.config.audioBitrate;
        vparam.audioChannels = plan.config.audioChannels;
        vparam.audioSampleRate = plan.config.audioSampleRate;
        if (!plan.audioEnabled) audioTracks.clear();
    }
    exporter.setAudioTracks(audioTracks);

    auto res = exporter.execute(cparam, vparam);
    if (!res && !exporter.isCanceled())
        QMessageBox::warning(widget, QObject::tr("Export Error"), exporter.log());
    // Sink mode returns success from the exporter but writes the file itself (the
    // fit loop owns it), so success is "the exporter says so AND the file is there
    // AND the fit did not report that it could not meet the cap". A failed fit
    // leaves the last attempt on disk, so the existence check alone would call a
    // rejected export a success.
    return bool(res) && !fitFailed && QFileInfo(g.osExportTarget).exists();
}

// Where a project's remembered export inputs live. PER PROJECT: a frame range
// belongs to the animation it was chosen in, and inheriting another project's
// range would export frames that project does not have.
QString MainWindow::exportInputsKey() const {
    const QString name =
        (mCurrent && !mCurrent->fileName().isEmpty()) ? mCurrent->fileName()
                                                     : QStringLiteral("untitled");
    return QStringLiteral("export_inputs/") + name;
}

// Put the last-used frame ranges, size and fps back into a freshly built dialog.
//
// They are per-run inputs, and they used to reset to the project's own values every
// time the dialog opened: changing one checkbox meant re-selecting the range, the
// size and the rate first. Applied BEFORE the plan is refreshed, so a destination
// with a Free size rule reads the size the user last chose.
void MainWindow::restoreExportInputs() {
    QSettings settings;
    const QString key = exportInputsKey();
    const int projectFirst = mCurrent->currentTimeInfo().frameRange.min();
    const int projectLast = mCurrent->currentTimeInfo().frameRange.max();

    // JSON, not a QVariantList of QVariantList: QSettings flattens nested lists of
    // simple types into one comma-separated string ("0, 3"), so reading the ranges
    // back as pairs found single values, skipped every one of them, and restored
    // nothing while the values sat right there in the file.
    const QJsonArray ranges =
        QJsonDocument::fromJson(settings.value(key + QStringLiteral("/ranges")).toString().toUtf8())
            .array();
    if (!ranges.isEmpty()) {
        int row = 0;
        for (const QJsonValue& v : ranges) {
            const QJsonArray pair = v.toArray();
            if (pair.size() != 2) continue;
            // Extra rows are ADDED (the dialog starts with one); the values are
            // clamped to this project's own range, because a remembered range can
            // outlive the animation it was chosen for.
            if (row >= exportUI->initialFrames->size()) {
                exportUI->addRange(projectLast);
            }
            exportUI->initialFrames->at(row)->setValue(
                std::clamp(pair.at(0).toInt(), projectFirst, projectLast));
            exportUI->lastFrames->at(row)->setValue(
                std::clamp(pair.at(1).toInt(), projectFirst, projectLast));
            ++row;
        }
    }

    const int w = settings.value(key + QStringLiteral("/width"), 0).toInt();
    const int h = settings.value(key + QStringLiteral("/height"), 0).toInt();
    if (w > 0 && h > 0) {
        QSignalBlocker wBlock(exportUI->widthSpinBox);
        QSignalBlocker hBlock(exportUI->heightSpinBox);
        exportUI->widthSpinBox->setValue(w);
        exportUI->heightSpinBox->setValue(h);
        exportUI->latestWidth = w;
        exportUI->latestHeight = h;
    }
    const int fps = settings.value(key + QStringLiteral("/fps"), 0).toInt();
    if (fps > 0) exportUI->fpsSpinBox->setValue(fps);
}

// Remember where an export went, so the next one does not start from nowhere.
//
// Two levels: the directory+name the dialogs should open at, and the directory
// used for a given DESTINATION, which is what makes a repeat quick export a
// single click instead of a folder prompt every time.
void MainWindow::rememberExportLocation(const exportParam& aParam) {
    const QFileInfo info(aParam.generalParams.osExportTarget);
    const QString dir = info.absolutePath();
    if (dir.isEmpty())
        return;
    QSettings settings;
    settings.setValue(QStringLiteral("export_last_dir"), dir);
    if (!info.fileName().isEmpty())
        settings.setValue(QStringLiteral("export_last_name"), info.fileName());
    if (!aParam.destinationId.isEmpty()) {
        settings.setValue(QStringLiteral("export_dir/%1").arg(aParam.destinationId), dir);
    }
}

// Tell the user what was written, and make it repeatable.
//
// There was no completion feedback at all: the progress dialog closed and the user
// was left to find the file. The size matters as much as the path — for a
// destination with a byte cap it is the difference between an upload that lands
// and one the platform refuses.
void MainWindow::reportExportOutcome(const exportParam& aParam, bool aWrote) {
    if (!aWrote)
        return;

    rememberExportLocation(aParam);

    const QFileInfo info(aParam.generalParams.osExportTarget);
    const qint64 bytes = info.exists() ? info.size() : 0;
    QString msg = tr("Exported %1 (%2 KB)").arg(info.fileName()).arg((bytes + 512) / 1024);

    const ctrl::ExportTarget target =
        ctrl::ExportTargets::find(ctrl::ExportTargets::load(), aParam.destinationId);
    if (target.caps.maxBytes > 0) {
        const bool within = bytes <= target.caps.maxBytes;
        msg += tr(" (%1 of %2 KB allowed for %3)")
                   .arg((bytes + 512) / 1024)
                   .arg(target.caps.maxBytes / 1024)
                   .arg(target.label);
        if (!within) {
            msg += tr("; OVER THE LIMIT, it will be rejected");
        }
    }
    msg += QLatin1String("  ") + info.absolutePath();
    statusBar()->showMessage(msg, 20000);

    // Kept as a copy so "Export again" repeats THIS export rather than
    // re-deriving one from whatever the dialogs currently hold.
    mLastExport.reset(new exportParam(aParam));
    if (mMainMenuBar)
        mMainMenuBar->setExportAgainEnabled(true);
}

void MainWindow::onExportAgainTriggered() {
    if (!mCurrent || !mLastExport)
        return;

    // Everything is repeated — destination, quality, ranges, size, fps, artifact —
    // EXCEPT the name, which is asked for, pre-filled with the last one.
    //
    // The first version of this repeated the path too, and defended itself with the
    // exporter's overwrite confirmer: a yes/no on the SAME name, so "Export again"
    // could never produce a second file, and answering "yes" destroyed the previous
    // export with no way back. For a tool whose output IS the deliverable, and where
    // one export can be minutes of encoding, that is the wrong default. Pre-filling
    // keeps it to one keystroke for "same name, I mean it", and typing a new name
    // gives the new file that "again" usually means.
    auto& g = mLastExport->generalParams;
    const QFileInfo last(g.osExportTarget);

    QString path;
    if (mLastExport->exportType == exportTarget::image) {
        // A sequence's target is a directory; there is no file to name.
        path = QFileDialog::getExistingDirectory(this, tr("Export Folder"), last.absoluteFilePath());
        if (path.isEmpty())
            return;
    } else {
        const QString suffix = last.suffix();
        const QString filter = suffix.isEmpty()
                                   ? tr("All files (*)")
                                   : tr("%1 files (*.%2)").arg(suffix.toUpper(), suffix);
        path = QFileDialog::getSaveFileName(this, tr("Export File"), last.absoluteFilePath(), filter);
        if (path.isEmpty())
            return;
        if (QFileInfo(path).suffix().isEmpty() && !suffix.isEmpty())
            path += QLatin1Char('.') + suffix;
    }

    const QFileInfo chosen(path);
    g.osExportTarget = chosen.absoluteFilePath();
    g.exportDirectory = chosen.isDir() ? QDir(chosen.absoluteFilePath()) : chosen.absoluteDir();
    g.exportFileName = QDir(chosen.absoluteFilePath());
    g.fileName = chosen.fileName().isEmpty() ? g.fileName : chosen.fileName();
    g.exportName = chosen.isDir() ? g.exportName : chosen.baseName();

    std::vector<audioConfig> validAudioStreams =
        AudioPlaybackWidget::getValidAudioStreams(*mCurrent->pConf);
    // "Export again" repeats the settings, not the frame: a still destination
    // exports what is on screen now.
    mLastExport->playhead = mTarget->currentFrame().get();
    const bool wrote = exportProject(*mLastExport, mCurrent, exportWidget, validAudioStreams);
    reportExportOutcome(*mLastExport, wrote);
}

// The File menu's "Quickly export as..." : the destination is chosen in the same
// picker the export dialog uses, and then the export runs with no further dialogs.
void MainWindow::onQuickExportTriggered() {
    if (!mCurrent) return;
    DestinationPicker picker(mGUIResources,
                             QSettings().value(QStringLiteral("export_destination")).toString(),
                             this);
    if (picker.exec() != QDialog::Accepted) return;
    const QString id = picker.chosenId();
    if (id.isEmpty()) {
        // "My platform isn't listed": that answer needs the form, because the
        // format and codec are the user's to state. It opens the form directly -
        // asking for a destination again would be asking the question the user
        // just answered.
        openExportDialog(QString());
        return;
    }
    QSettings().setValue(QStringLiteral("export_destination"), id);
    onQuickExportDestinationTriggered(id);
}

void MainWindow::onQuickExportDestinationTriggered(const QString& aDestinationId) {
    if (!mCurrent) {
        return;
    }
    const ctrl::ExportTarget target =
        ctrl::ExportTargets::find(ctrl::ExportTargets::load(), aDestinationId);
    if (target.id.isEmpty()) {
        return;
    }

    // One click, and the destination decides the container, the codec, its options,
    // transparency, the exact size, the caps and the audio.
    //
    // The folder is asked for ONCE PER DESTINATION and then remembered. Exporting a
    // Telegram sticker, adjusting the art and exporting again is the normal
    // workflow, and re-navigating a folder dialog every time is what made "quick
    // export" not actually quick.
    QSettings settings;
    const QString rememberedDir =
        settings.value(QStringLiteral("export_dir/%1").arg(aDestinationId)).toString();
    QString dirPath = rememberedDir;
    if (dirPath.isEmpty() || !QFileInfo(dirPath).isDir()) {
        dirPath = QFileDialog::getExistingDirectory(
            this, tr("Export Folder"),
            settings.value(QStringLiteral("export_last_dir")).toString());
        if (dirPath.isEmpty()) {
            return;
        }
    }
    QString base = QFileInfo(mCurrent->fileName()).baseName();
    if (base.isEmpty()) {
        base = QStringLiteral("New Export");
    }
    const QString fileName = base + "." + target.container;

    GeneralParams genParam;
    genParam.fileName = base;
    genParam.exportName = base;
    genParam.exportDirectory = QDir(dirPath);
    genParam.exportFileName = QDir(dirPath + "/" + fileName);
    genParam.osExportTarget = QDir(dirPath + "/" + fileName).absolutePath();
    genParam.nativeWidth = mCurrent->attribute().imageSize().width();
    genParam.nativeHeight = mCurrent->attribute().imageSize().height();
    genParam.exportWidth = genParam.nativeWidth;
    genParam.exportHeight = genParam.nativeHeight;
    genParam.aspectRatio = custom;
    genParam.nativeFPS = mCurrent->attribute().fps();
    genParam.fps = mCurrent->attribute().fps();
    genParam.allowTransparency = true; // the destination decides, in the plan
    genParam.loop = true;
    genParam.imageExportQuality = -1;
    const frameExportRange fer{mCurrent->attribute().frameRange().min(),
                               mCurrent->attribute().frameRange().max()};
    genParam.nativeFrameRange = fer;
    genParam.exportRange = {fer};

    auto exParam = new exportParam();
    exParam->generalParams = genParam;
    exParam->exportType = exportTarget::video;
    exParam->destinationId = aDestinationId;
    // The quality the user last chose in the dialog, not a silent Balanced. The
    // quick path used to pin Balanced, so a user who had set Telegram stickers to
    // "Best" got Balanced again from the menu, with nothing anywhere saying why the
    // file was smaller than the one the dialog produced for the same destination.
    exParam->qualityBucket = bucketFromSettingsIndex(
        settings.value(QStringLiteral("export_quality_bucket"), 1).toInt());

    std::vector<audioConfig> validAudioStreams =
        AudioPlaybackWidget::getValidAudioStreams(*mCurrent->pConf);
    if (isExportParamValid(exParam, exportWidget, &validAudioStreams)) {
        exParam->playhead = mTarget->currentFrame().get();
        const bool wrote = exportProject(*exParam, mCurrent, exportWidget, validAudioStreams);
        reportExportOutcome(*exParam, wrote);
    }
}

// The destination is asked for FIRST, because everything the form asks afterwards
// depends on it: the size, the codec, the byte cap, the duration limit, whether
// transparency or audio can survive at all. The form used to open with the whole
// legacy parameter sheet and hide the chooser behind a button on it, so the first
// thing a user saw was a decision they could not make yet (a codec, a bitrate) and
// the one decision they could make was two clicks in.
void MainWindow::onExportTriggered() {
    if (!mCurrent) { return; }
    if (exporting) { exportWidget->showNormal(); return; }

    DestinationPicker picker(mGUIResources,
                             QSettings().value(QStringLiteral("export_destination")).toString(),
                             this);
    if (picker.exec() != QDialog::Accepted) return; // no destination, nothing to export yet
    const QString id = picker.chosenId();
    if (!id.isEmpty())
        QSettings().setValue(QStringLiteral("export_destination"), id);
    openExportDialog(id);
}

// aDestinationId empty means "my platform isn't listed": the form then states the
// artifact itself, which is the only case where the encoder controls are the user's
// to set.
void MainWindow::openExportDialog(const QString& aDestinationId) {
    if (!mCurrent) { return; }
    if (exporting) { exportWidget->showNormal(); return; }
    // Stop animation and main display rendering
    EventSuspender suspender(*mMainDisplay, *mTarget);
    QGuiApplication::setOverrideCursor(QCursor(Qt::BusyCursor));
    QSettings settings;
    // Initialize export diag
    exporting = true;
    // Avoid weird bugs
    exportWidget = new QDialog;
    exportUI = new ExportWidgetUI;
    // Set up UI
    exportWidget->setParent(this, Qt::Window);
    exportUI->setupUi(exportWidget, mGUIResources, mCurrent->attribute().frameRange().max(),
                      mTarget->currentFrame().get());

    // Destinations come from the data table — the same rows the exporter plans
    // from — so what the dialog STATES is what the export will actually do. The
    // per-container codec combos could not do that (getFormatAsInt() returns 0 for
    // every input, so every codec and pixel format chosen there was discarded).
    {
        // The chooser is a modal with a search field (see gui/DestinationPicker).
        // It has already been answered once to get here; the row states which
        // destination the form is working under and "Change..." reopens it, because
        // a user who picked the wrong platform should not have to cancel the export.
        QSettings settings;
        exportUI->setDestinationId(aDestinationId);
        auto showDestinationButtonText = [this]() {
            const QString id = exportUI->destinationId();
            if (id.isEmpty()) {
                exportUI->destinationButton->setText(
                    tr("My platform isn't listed (I'll choose the format)"));
                exportUI->destinationButton->setIcon(QIcon());
                return;
            }
            const ctrl::ExportTarget t =
                ctrl::ExportTargets::find(ctrl::ExportTargets::load(), id);
            exportUI->destinationButton->setText(t.id.isEmpty() ? id : targetLabel(t));
            exportUI->destinationButton->setIcon(t.id.isEmpty() ? QIcon()
                                                               : mGUIResources.icon(t.icon));
        };
        showDestinationButtonText();
        const int savedQuality = settings.value("export_quality_bucket", 1).toInt();
        exportUI->qualityCombo->setCurrentIndex(savedQuality >= 0 && savedQuality <= 3 ? savedQuality : 1);

        auto currentBucket = [this]() {
            switch (exportUI->qualityCombo->currentIndex()) {
            case 0: return ctrl::QualityBucket::Smaller;
            case 2: return ctrl::QualityBucket::Best;
            case 3: return ctrl::QualityBucket::Lossless;
            default: return ctrl::QualityBucket::Balanced;
            }
        };

        // The plan is computed on every change and shown in words: which codec was
        // chosen and why, what size the destination forces, and every capability it
        // declares. This is the whole point of the redesign — the user states a
        // fact they possess ("it is for Telegram") and the system applies the
        // consequences.
        auto refreshFacts = [this, currentBucket]() {
            const QString id = exportUI->destinationId();
            if (id.isEmpty()) {
                exportUI->clearPlan();
                // Manual mode states its own facts, for the same reason a
                // destination does: the controls cannot say everything. A ticked
                // "Export with transparency" beside H.264 is the clearest case —
                // libx264 has no alpha pixel format at all, so the export will be
                // opaque however the box is set, and only words can say so.
                QStringList manual;
                if (exportUI->selectedFormatIsSequence()) {
                    // Labelled lines, not one dash-joined sentence: a dash has to be
                    // re-invented by every translator, and a label can just be
                    // translated.
                    manual << tr("%1 of %2 frames at %3 x %4")
                                  .arg(exportUI->selectedFormatLabel())
                                  .arg(exportUI->lastFrameSpinBox->value()
                                       - exportUI->firstFrameSpinBox->value() + 1)
                                  .arg(exportUI->widthSpinBox->value())
                                  .arg(exportUI->heightSpinBox->value());
                    manual << tr("Frame rate: %1 fps").arg(exportUI->fpsSpinBox->value());
                } else {
                    // A container with exactly one codec (GIF, APNG, WebP) has no
                    // codec label to print, and " —  in GIF animation" is not a
                    // fact, it is a gap where one should be.
                    const QString codecLabel = exportUI->selectedCodecLabel();
                    manual << tr("%1 x %2, %3 fps")
                                  .arg(exportUI->widthSpinBox->value())
                                  .arg(exportUI->heightSpinBox->value())
                                  .arg(exportUI->fpsSpinBox->value());
                    if (!codecLabel.isEmpty()) {
                        manual << tr("Codec: %1 in %2")
                                      .arg(codecLabel, exportUI->selectedFormatLabel());
                    }
                }
                // One string per note: QStringList's operator<< appends each
                // argument as its own element, so streaming a literal bullet and
                // then the sentence would put them on separate lines.
                if (exportUI->transparencyCheckBox->isChecked()
                    && !exportUI->selectedCodecCarriesAlpha()) {
                    manual << QStringLiteral("• ") + tr("%1 cannot carry transparency, so this "
                                                       "export will be opaque.")
                                                     .arg(exportUI->selectedCodecLabel());
                }
                if (exportUI->selectedFormatIsGif()) {
                    manual << QStringLiteral("• ")
                                  + tr("A GIF loops forever, so the Loop animation box is fixed on.");
                }
                exportUI->setFacts(manual.join(QLatin1Char('\n')));
                return;
            }
            const ctrl::ExportTarget target =
                ctrl::ExportTargets::find(ctrl::ExportTargets::load(), id);

            // Phase one: the destination's own declarations go into the controls
            // BEFORE the plan is asked, because the plan is computed from them.
            // A row that says "this loops" has to have ticked the box first, or
            // the answer would contradict the declaration it came from.
            exportUI->applyDestinationDefaults(target);

            const int first = exportUI->firstFrameSpinBox->value();
            const int last = exportUI->lastFrameSpinBox->value();
            int exportableTracks = 0;
            if (mCurrent->pConf) {
                for (const audioConfig& ac : *mCurrent->pConf) {
                    if (ac.exportEnable) ++exportableTracks;
                }
            }

            ctrl::ExportRequest request;
            request.sourceSize = mCurrent->attribute().imageSize();
            request.requestedSize =
                QSize(exportUI->widthSpinBox->value(), exportUI->heightSpinBox->value());
            request.fps = exportUI->fpsSpinBox->value();
            request.selection = ctrl::FrameSelection::fromRange(std::min(first, last), std::max(first, last));
            request.exportableAudioTracks = exportableTracks;
            request.mute = !exportUI->exportWithAudioCheckBox->isChecked();
            request.playhead = exportUI->playhead();

            const ctrl::ExportPlan plan = ctrl::planExport(target, request, currentBucket());
            const QStringList lines = gui::exportFacts(plan, target, request);
            exportUI->setFacts(lines.join(QLatin1Char('\n')));
            // State the facts AND make them true in the controls the
            // destination governs, so the form cannot contradict itself.
            exportUI->applyPlan(plan, target);
        };

        QObject::connect(exportUI->destinationButton, &QPushButton::clicked,
                         [this, refreshFacts, showDestinationButtonText]() {
                             DestinationPicker picker(mGUIResources,
                                                      exportUI->destinationId(), exportWidget);
                             if (picker.exec() != QDialog::Accepted) return;
                             exportUI->setDestinationId(picker.chosenId());
                             showDestinationButtonText();
                             refreshFacts();
                         });
        QObject::connect(exportUI->qualityCombo,
                         util::SelectArgs<int>::from(&QComboBox::currentIndexChanged),
                         [refreshFacts](int) { refreshFacts(); });

        // The escape hatch: only meaningful with no destination, which is the
        // only case where the user has to state the artifact themselves.
        exportUI->populateFormats();
        QObject::connect(exportUI->formatCombo,
                         util::SelectArgs<int>::from(&QComboBox::currentIndexChanged),
                         [this, refreshFacts](int) {
                             exportUI->refreshCodecs();
                             refreshFacts();
                         });
        QObject::connect(exportUI->codecCombo,
                         util::SelectArgs<int>::from(&QComboBox::currentIndexChanged),
                         [refreshFacts](int) { refreshFacts(); });
        // NOTE: no initial refreshFacts() call here. The widgets are still at
        // their constructed values at this point — the project's own size, fps
        // and the saved checkboxes are applied further down. Applying the plan
        // now would freeze the size boxes at the wrong numbers and then have
        // those numbers overwritten, which is the self-contradiction this whole
        // change exists to remove. It is called once, after initialisation.
        mRefreshExportFacts = refreshFacts;
    }
    // Initialize gpDiag
    gpDiag->setAttribute(Qt::WA_DeleteOnClose,true);
    gpDiag->setParent(exportWidget, Qt::Window);
    // Initialize export and general parameters
    auto* exParam = new exportParam();
    GeneralParams genParam;
    genParam.nativeWidth = mCurrent->attribute().imageSize().width();
    genParam.nativeHeight = mCurrent->attribute().imageSize().height();
    genParam.nativeFPS = mCurrent->attribute().fps();
    // I hate you gcc
    frameExportRange fer;
    fer.firstFrame = mCurrent->attribute().frameRange().min();
    fer.lastFrame = mCurrent->attribute().frameRange().max();
    genParam.nativeFrameRange = fer;
    // Get generics
    QVariant aspectRatioV = settings.value("export_aspect_ratio");
    if(!aspectRatioV.isValid()) { aspectRatioV.setValue(1); } // Aspect ratio = keep
    QVariant allowTransparencyV = settings.value("export_allow_transparency");
    if(!allowTransparencyV.isValid()) {allowTransparencyV.setValue(true); } // Allow transparency = true
    QVariant useCustomPaletteV = settings.value("export_custom_palette");
    QVariant loopAnimV = settings.value("export_loop_anim");
    QVariant exportWithAudioV = settings.value("export_allow_audio");
    // Initialize pointers to generics
    auto* aspectRatio = new int;
    auto allowTransparency = new bool;
    auto useCustomPalette = new bool;
    auto loopAnim = new bool;
    auto exportWithAudio = new bool;
    // Gather initialized pointers into a vector
    QVector<QVariant *> intValues {
        &aspectRatioV
    };
    QVector<int *> intVariables {
        aspectRatio
    };
    QVector<QVariant *> boolValues {
        &allowTransparencyV, &useCustomPaletteV,
        &loopAnimV, &exportWithAudioV
    };
    QVector<bool *> boolVariables {
        allowTransparency, useCustomPalette, loopAnim,
        exportWithAudio
    };
    // Loop through the vectors and initialize their values
    int x = 0;
    for(auto variant : intValues){
        if(!variant->isValid()){
            variant->setValue(0);
            settings.sync();
        }
        *intVariables[x] = variant->toInt();
        x++;
    }
    x= 0;
    for(auto variant : boolValues){
        if(!variant->isValid()){
            variant->setValue(false);
            settings.sync();
        }
        *boolVariables[x] = variant->toBool();
        x++;
    }
    // The per-container codec and pixel-format settings are gone with the Format
    // tab: a destination decides the codec now. The old values were never applied
    // in the first place — getFormatAsInt() returns 0 for every input, so every
    // choice they stored was read back as "Auto".
    // Get custom parameters
    // Sync
    settings.sync();
    // Value initialization
    // One question ("does editing Width move Height?") instead of three legacy
    // radio buttons: One-to-one and Custom were size-LINK modes from the original
    // dialog, and ctrl::ExportParams.h's `aspectRatio` — the only thing they ever
    // set — is written here and read by nothing. A stored 0 (one-to-one) now means
    // "keep", which is what it did in every export.
    exportUI->keepAspectRatio->setChecked(*aspectRatio != 2);
    exportUI->transparencyCheckBox->setChecked(*allowTransparency);
    exportUI->loopCheckBox->setChecked(*loopAnim);
    exportUI->exportWithAudioCheckBox->setChecked(*exportWithAudio);
    // The palette path is restored WITH the preference. Without it, a remembered
    // "use a custom palette" could only ever produce a blocking validation error
    // ("Selected palette does not exist") with no way to see which file it meant:
    // the checkbox was persisted, the file it referred to was not. A preference
    // the app cannot honour is not a preference, so the box is only ticked when
    // the file is still there.
    {
        const QString savedPalette = settings.value("export_palette_path").toString();
        const QFileInfo paletteInfo(savedPalette);
        // A FILE, checked as a file: QDir() is the current directory and
        // "exists", so testing a QDir for existence made an unset palette look
        // like a valid one and kept the box ticked.
        const bool paletteRestored =
            !savedPalette.isEmpty() && paletteInfo.isFile() && paletteInfo.isReadable();
        if (paletteRestored) {
            exportUI->paletteDir = QDir(savedPalette);
            exportUI->selectPalettePathButton->setText(paletteInfo.fileName());
        }
        exportUI->customPaletteCheckBox->setChecked(*useCustomPalette && paletteRestored);
    }
    exportUI->nativeH = genParam.nativeHeight;
    exportUI->nativeW = genParam.nativeWidth;
    exportUI->latestHeight = exportUI->nativeH;
    exportUI->latestWidth = exportUI->nativeW;
    // Project specific values
    QSignalBlocker wSB(exportUI->widthSpinBox);
    QSignalBlocker hSB(exportUI->heightSpinBox);
    wSB.reblock(); hSB.reblock();
    exportUI->widthSpinBox->setValue(genParam.nativeWidth);
    exportUI->heightSpinBox->setValue(genParam.nativeHeight);
    wSB.unblock(); hSB.unblock();
    exportUI->fpsSpinBox->setValue(genParam.nativeFPS);
    exportUI->lastFrameSpinBox->setValue(mCurrent->currentTimeInfo().frameRange.max());
    // The suggested export range starts at the project range's first frame
    // (the last frame box was set above); frames before it are pre-roll the
    // animation doesn't own.
    exportUI->firstFrameSpinBox->setValue(mCurrent->currentTimeInfo().frameRange.min());
    // The user's own inputs, over the project defaults, BEFORE the plan is stated:
    // a destination with a Free size rule takes the size from these boxes.
    restoreExportInputs();
    // Now that the widgets hold the project's real values, state the
    // destination's facts and freeze whatever it decides. This is the one place
    // the plan is applied to the freshly built dialog.
    if (mRefreshExportFacts) {
        mRefreshExportFacts();
    }
    // Connections, these below are to force a refresh and to avoid any weird bugs or memory leaks
    connect(exportWidget, &QDialog::destroyed, [=](){
        regenerateWidget();
    });
    connect(exportWidget, &QDialog::finished, [=](){
        exportWidget->deleteLater();
    });
    connect(gpDiag, &QDialog::destroyed, [=](){
        regenerateGeneralPurposeDiag();
    });
    // ---------------------------------------------------------------------- //
    connect(exportUI->cancelButton, &QPushButton::clicked, [=]() mutable{
        exportUI->operationCancelled = true; exportWidget->close();
    });
    connect(exportUI->exportButton, &QPushButton::clicked, [=]() mutable{
        exportUI->operationCancelled = false; exportWidget->close();
    });
    connect(exportUI->setWidthNative, &QPushButton::clicked, [=](){
        exportUI->widthSpinBox->setValue(genParam.nativeWidth);
    });
    connect(exportUI->setHeightNative, &QPushButton::clicked, [=](){
        exportUI->heightSpinBox->setValue(genParam.nativeHeight);
    });
    connect(exportUI->setFPSNative, &QPushButton::clicked, [=](){
        exportUI->fpsSpinBox->setValue(genParam.nativeFPS);
    });

    // Execute
    QGuiApplication::restoreOverrideCursor();
    exportWidget->exec();

    // Save all settings
    const int aspectRatioIndex = exportUI->keepAspectRatio->isChecked() ? 1 : 2;
    {
        updateSettings(&aspectRatioV, &settings, aspectRatioIndex,
                       "export_aspect_ratio");
        updateSettings(&allowTransparencyV, &settings, exportUI->transparencyCheckBox->isChecked(),
                       "export_allow_transparency");
        updateSettings(&useCustomPaletteV, &settings, exportUI->customPaletteCheckBox->isChecked(),
                       "export_custom_palette");
        settings.setValue("export_palette_path", exportUI->paletteDir.absolutePath());
        updateSettings(&loopAnimV, &settings, exportUI->loopCheckBox->isChecked(),
                       "export_loop_anim");
        updateSettings(&exportWithAudioV, &settings, exportUI->exportWithAudioCheckBox->isChecked(),
                       "export_allow_audio");
        settings.sync();
    }
    // Get file name and folder for export
    if (exportUI->operationCancelled) { return; }
    // One filter: the artifact the user already chose in the dialog's Format row.
    //
    // This used to be thirteen video filters and nine image filters, and the
    // EXPORT FORMAT was then read back out of whichever filter was selected — so
    // the format was chosen in the OS file dialog by a name like "Flash Video",
    // "MPEG-2" or "X11 PixelMap" that nothing downstream could produce. The
    // artifact is decided in one place now; the file dialog only asks where to
    // put it and what to call it.
    const QString artifactId = exportUI->formatCombo->currentData().toString();
    const bool sequenceExport = artifactId.startsWith(QLatin1String("image/"));
    const QString artifactSuffix = sequenceExport ? artifactId.mid(6)
                                                 : exportUI->containerForFormat(artifactId);
    QFileDialog fileDiag;
    fileDiag.setAcceptMode(QFileDialog::AcceptMode::AcceptSave);
    fileDiag.setFileMode(QFileDialog::AnyFile);
    // Open where the last export went, with its name preselected. Repeating an
    // export to the same place is the common case, and this dialog used to open
    // with no location at all.
    {
        QSettings s2;
        const QString lastDir = s2.value(QStringLiteral("export_last_dir")).toString();
        const QString lastName = s2.value(QStringLiteral("export_last_name")).toString();
        if (!lastDir.isEmpty() && QFileInfo(lastDir).isDir()) {
            fileDiag.setDirectory(lastDir);
        }
        // ALWAYS pre-fill a name, and take its extension from the artifact.
        //
        // Two things went wrong without this. The remembered name could contradict
        // the artifact (APNG after a WEBM export pre-filled "test.webm", and the
        // suffix is only appended when a name has none, so APNG bytes were written
        // into a .webm file) — that is why the base name is kept and the extension
        // replaced. And with nothing to remember, the dialog opened with an empty
        // name and focus in its SEARCH box: you had to click the name field before
        // you could type a file name at all. Falling back to the project's own name
        // fixes both, and it is the name most exports want anyway.
        const QString rememberedBase = QFileInfo(lastName).completeBaseName();
        // The project's own name is the default: this is a MainWindow member
        // function, so the export parameter bag does not exist yet here.
        const QString fallbackBase =
            mCurrent ? QFileInfo(mCurrent->fileName()).completeBaseName() : QString();
        const QString base =
            !rememberedBase.isEmpty() ? rememberedBase : (fallbackBase.isEmpty() ? QStringLiteral("Export") : fallbackBase);
        fileDiag.selectFile(base + QLatin1Char('.') + artifactSuffix);
    }
    fileDiag.setNameFilters({exportUI->labelForFormat(artifactId) + " (*." + artifactSuffix + ")"});
    fileDiag.selectNameFilter(exportUI->labelForFormat(artifactId) + " (*." + artifactSuffix + ")");

    // Generate parameters prior to exportUI destructor
    genParam.fileName = mCurrent->fileName().isEmpty()? "New Export" : QString(mCurrent->fileName());
    genParam.nativeWidth = mCurrent->attribute().imageSize().width();
    genParam.nativeHeight = mCurrent->attribute().imageSize().height();
    genParam.exportWidth = exportUI->widthSpinBox->value();
    genParam.exportHeight = exportUI->heightSpinBox->value();
    genParam.aspectRatio = aspectRatioIndex == 1 ? keep : custom;
    genParam.nativeFPS = mCurrent->attribute().fps();
    genParam.fps = exportUI->fpsSpinBox->value();
    genParam.bitrate =
        // Is it empty?
        exportUI->bitrateLineEdit->text().trimmed().isEmpty()
        ? 0 :
        // Does it contain only the word auto?
        exportUI->bitrateLineEdit->text().trimmed().contains(QRegularExpression("^(?i)(auto(matic)?)$"))
        ? 0 :
        // Does it contain only a positive number? If not then set to -1 for error handling.
        exportUI->bitrateLineEdit->text().trimmed().contains(QRegularExpression("^(?!0\\d+)\\d+$"))
            ? exportUI->bitrateLineEdit->text().toInt() : -1;
    genParam.qualityOverride = exportUI->qualityOverrideSpin->value();
    genParam.imageExportQuality = exportUI->imageQualitySpinbox->value();
    genParam.allowTransparency = exportUI->transparencyCheckBox->isChecked();
    genParam.loop = exportUI->loopCheckBox->isChecked();
    genParam.exportWithAudio = exportUI->exportWithAudioCheckBox->isChecked();
    genParam.useCustomPalette = exportUI->customPaletteCheckBox->isChecked();
    genParam.palettePath = exportUI->paletteDir;
    auto* exportRanges = new QVector<frameExportRange>;
    for(int ferIndex = 0; ferIndex < exportUI->initialFrames->size(); ferIndex++){
        frameExportRange frame_export_range;
        frame_export_range.firstFrame = exportUI->initialFrames->at(ferIndex)->value();
        frame_export_range.lastFrame = exportUI->lastFrames->at(ferIndex)->value();
        exportRanges->append(frame_export_range);
    }
    genParam.exportRange = *exportRanges;
    exParam->destinationId = exportUI->destinationId();
    exParam->qualityBucket = bucketFromSettingsIndex(exportUI->qualityCombo->currentIndex());
    {
        QSettings settings;
        settings.setValue("export_destination", exParam->destinationId);
        settings.setValue("export_quality_bucket", exportUI->qualityCombo->currentIndex());
        // The per-run inputs, so the next open starts where this one left off.
        const QString inputsKey = exportInputsKey();
        QJsonArray savedRanges;
        for (int i = 0; i < exportUI->initialFrames->size(); ++i) {
            QJsonArray pair;
            pair << exportUI->initialFrames->at(i)->value()
                 << exportUI->lastFrames->at(i)->value();
            savedRanges << pair;
        }
        settings.setValue(inputsKey + QStringLiteral("/ranges"),
                          QString::fromUtf8(QJsonDocument(savedRanges).toJson(QJsonDocument::Compact)));
        settings.setValue(inputsKey + QStringLiteral("/width"), exportUI->widthSpinBox->value());
        settings.setValue(inputsKey + QStringLiteral("/height"), exportUI->heightSpinBox->value());
        settings.setValue(inputsKey + QStringLiteral("/fps"), exportUI->fpsSpinBox->value());
    }
    // The artifact decides the type: a sequence writes numbered stills, anything
    // else writes one video file. This used to be an independent combo, which let
    // the two disagree ("Animation" plus a .png filter) and left the format to be
    // reconstructed from the disagreement.
    exParam->exportType = sequenceExport ? exportTarget::image : exportTarget::video;
    if (exParam->exportType == exportTarget::video) {
        exParam->videoParams.container = artifactSuffix;
        // Empty means "the container's probed default", which is the only honest
        // answer when the codec was not a choice (one candidate, or a GIF).
        exParam->videoParams.codec = exportUI->codecCombo->currentData().toString();
    } else {
        exParam->imageParams.suffix = artifactSuffix;
    }
    // Get file via OS diag
    fileDiag.setViewMode(QFileDialog::Detail);
    QString selectedFile;
    if(fileDiag.exec()){ selectedFile = fileDiag.selectedFiles().at(0); }
    else{ return; }
    if (QFileInfo(selectedFile).suffix().isEmpty()) {
        selectedFile.append(QLatin1Char('.') + artifactSuffix);
    }
    qDebug() << "Exporting animation to" << selectedFile << "as" << artifactSuffix;
    // Generate export parameters
    genParam.exportName = QFileInfo(selectedFile).fileName();
    genParam.exportDirectory = QFileInfo(selectedFile).absoluteDir();
    genParam.exportFileName = QDir(selectedFile);
    genParam.osExportTarget = selectedFile;
    exParam->generalParams = genParam;

    // Main export function
    std::vector<audioConfig> validAudioStreams = AudioPlaybackWidget::getValidAudioStreams(*mCurrent->pConf);
    if (isExportParamValid(exParam, exportWidget, &validAudioStreams)) {
        exParam->playhead = mTarget->currentFrame().get();
        const bool wrote = exportProject(*exParam, mCurrent, exportWidget, validAudioStreams);
        reportExportOutcome(*exParam, wrote);
    }
}

void MainWindow::onMoveFrameTriggered(bool moveRight) {
    int frameDest = moveRight? 1: -1;
    core::Frame dest = mTarget->currentFrame();
    dest.add(frameDest);
    mTarget->timeLineWidget().setFrame(dest);
}

} // namespace gui
