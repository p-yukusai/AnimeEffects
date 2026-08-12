#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include "GeneralSettingDialog.h"
#include "util/IProgressReporter.h"
#include "gl/Global.h"
#include "ctrl/Exporter.h"
#include "gui/MainWindow.h"
#include "gui/DockSash.h"
#include "gui/ExportDialog.h"
#include "gui/NewProjectDialog.h"
#include "gui/ProjectHook.h"
#include "gui/menu/menu_ProgressReporter.h"
#include "util/NetworkUtil.h"
#include <set>
#include <utility>
#include <QComboBox>
// This thing is held by duct tape and OOP hell I swear...
#include "ctrl/ExportParams.h"

namespace {
class EventSuspender {
    QWriteLocker mRenderingLocker;

public:
    EventSuspender(gui::MainDisplayWidget& aMainDisplay, gui::TargetWidget& aTarget):
        mRenderingLocker(&aMainDisplay.renderingLock()) {
        // stop animation
        aTarget.stop();
    }
};

} // namespace

namespace gui {

MainWindow::MainWindow(ctrl::System& aSystem, GUIResources& aResources, LocaleParam  aLocaleParam):
    QMainWindow(nullptr),
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
            key->invoker = [=]() { this->onUndoTriggered(); };
    }
    {
        auto key = mKeyCommandMap->get("Redo");
        if (key)
            key->invoker = [=]() { this->onRedoTriggered(); };
    }
    {
        auto key = mKeyCommandMap->get("SaveProject");
        if (key)
            key->invoker = [=]() { this->onSaveProjectTriggered(); };
    }
    {
        auto key = mKeyCommandMap->get("PlayPause");
        if (key)
            key->invoker = [=]() { this->onPlayPauseTriggered(); };
    }
    {
        auto key = mKeyCommandMap->get("ToggleDocks");
        if (key)
            key->invoker = [=]() { this->onDockToggle(); };
    }
    {
        auto key = mKeyCommandMap->get("ToggleRepeat");
        if (key)
            key->invoker = [=]() { this->onLoopToggle(); };
    }
    {
        auto key = mKeyCommandMap->get("MoveRight");
        if (key)
            key->invoker = [=]() { this->onDisplacementTriggered(1); };
    }
    {
        auto key = mKeyCommandMap->get("MoveLeft");
        if (key)
            key->invoker = [=]() { this->onDisplacementTriggered(-1); };
    }
    {
        auto key = mKeyCommandMap->get("MoveToInit");
        if (key)
            key->invoker = [=]() { this->onMovementTriggered("Init"); };
    }
    {
        auto key = mKeyCommandMap->get("MoveToLast");
        if (key)
            key->invoker = [=]() { this->onMovementTriggered("Last"); };
    }
    {
        auto key = mKeyCommandMap->get("MoveFrameLeft");
        if(key)
            key->invoker = [=]() {this->onMoveFrameTriggered(false);};
    }
    {
        auto key = mKeyCommandMap->get("MoveFrameRight");
        if(key)
            key->invoker = [=]() {this->onMoveFrameTriggered(true);};
    }
#endif

    // autosave

    QSettings settings;
    bool autosave = settings.value("generalsettings/projects/enableAutosave").isValid() &&
        settings.value("generalsettings/projects/enableAutosave").toBool();

    if (autosave) {
        autosaveTimer = new QTimer(this);
        connect(autosaveTimer, SIGNAL(timeout()), this, SLOT(autoSave()));
        autosaveTimer->start();
    }
}

MainWindow::~MainWindow() { closeAllProjects(); }

void MainWindow::autoSave() {
    QSettings settings;
    int autoDelay = settings.value("generalsettings/projects/autosaveDelay").isValid()
        ? settings.value("generalsettings/projects/autosaveDelay").toInt()
        : 5;
    autosaveTimer->setInterval(autoDelay * 60000);
    if (mCurrent && !mCurrent->isNameless() && mCurrent->isModified()) {
        mViaPoint.pushLog(
            "Automatically saved project: " + QFileInfo(mCurrent->fileName()).fileName(), ctrl::UILogType_Info
        );
        // qDebug() << "Interval of " + QString(std::to_string(autoDelay*60000).c_str()) + " milliseconds has elapsed.";
        processProjectSaving(*mCurrent);
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

    QFile stylesheet(aTheme.path() + "/stylesheet/standard.ssa");
    if (stylesheet.open(QIODevice::ReadOnly | QIODevice::Text)) {
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

        this->setStyleSheet(fontOption + QTextStream(&stylesheet).readAll());

        stylesheet.close();
    }

    stylesheet.setFileName(aTheme.path() + "/stylesheet/propertywidget.ssa");
    if (stylesheet.open(QIODevice::ReadOnly | QIODevice::Text)) {
        mDockPropertyWidget->setStyleSheet(QTextStream(&stylesheet).readAll());
        stylesheet.close();
    }


    stylesheet.setFileName(aTheme.path() + "/stylesheet/toolwidget.ssa");
    if (stylesheet.open(QIODevice::ReadOnly | QIODevice::Text)) {
        mDockToolWidget->setStyleSheet(QTextStream(&stylesheet).readAll());
        stylesheet.close();
    }

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
        for (int x = 0; x < aProject.resourceHolder().imageTrees().size(); x += 1) {
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
        mTarget->timeLineWidget().setFrame(core::Frame(mCurrent->attribute().maxFrame()));
    } else {
        mTarget->timeLineWidget().setFrame(core::Frame(0));
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

bool MainWindow::processProjectSaving(core::Project& aProject, bool aRename) {
    // stop animation and main display rendering
    EventSuspender suspender(*mMainDisplay, *mTarget);

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
    auto result = mSystem.saveProject(aProject);
    if (!result) {
        QMessageBox::warning(nullptr, "Saving Error", result.message);
        return false; // failed
    }

    mProjectTabBar->updateTabNames();
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

void exportProject(exportParam& exParam, core::Project* mCurrent, QDialog* widget, const std::vector<audioConfig>& validAudioStreams){
    qDebug("Creating FFmpeg object");
    auto* ffmpeg = new ffmpeg::ffmpegObject();
    projectExporter::Exporter exporter(*mCurrent, widget, exParam, *ffmpeg);
    // If piped build piped argument, TODO is to account for this
    if(exParam.generalParams.forcePipe || !exParam.generalParams.useCustomParam) {
        ffmpeg->argument = ffmpeg::buildArgument(exParam);
    }
    qDebug("FFmpeg object created, rendering...");
    auto exportResult = exporter.renderAndExport(exParam.generalParams.exportWithAudio, validAudioStreams);
    projectExporter::Exporter::generateMessageBox(&exporter.export_obj, &exportResult, &exParam);
}

void MainWindow::onExportTriggered() {
    if (!mCurrent) { return; }
    if (exporting) { exportWidget->showNormal(); return; }
    // Stop animation and main display rendering
    EventSuspender suspender(*mMainDisplay, *mTarget);
    QGuiApplication::setOverrideCursor(QCursor(Qt::BusyCursor));
    QSettings settings;
    // FFmpeg Check
    QString ffmpeg = GeneralSettingDialog::getFFmpeg();
    auto* aDiag = new GeneralSettingDialog(mGUIResources, this);
    bool ffmpegWorks = GeneralSettingDialog::ffmpegCheck(ffmpeg, aDiag);
    if (!ffmpegWorks) {
        GeneralSettingDialog::ffmpegCheckFailed(aDiag);
        return;
    }
    // Initialize export diag
    exporting = true;
    // Avoid weird bugs
    exportWidget = new QDialog;
    exportUI = new ExportWidgetUI;
    // Set up UI
    exportWidget->setParent(this, Qt::Window);
    exportUI->setupUi(exportWidget, mGUIResources, mCurrent->attribute().maxFrame());
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
    fer.lastFrame = mCurrent->attribute().maxFrame();
    genParam.nativeFrameRange = fer;
    // Get generics
    QVariant aspectRatioV = settings.value("export_aspect_ratio");
    if(!aspectRatioV.isValid()) { aspectRatioV.setValue(1); } // Aspect ratio = keep
    QVariant intermediateTypeV = settings.value("export_intermediate_type");
    QVariant allowTransparencyV = settings.value("export_allow_transparency");
    if(!allowTransparencyV.isValid()) {allowTransparencyV.setValue(true); } // Allow transparency = true
    QVariant allowCustomParamV = settings.value("export_allow_params");
    QVariant allowInterParamV = settings.value("export_allow_param_inter");
    QVariant allowPostParamV = settings.value("export_allow_param_post");
    QVariant useCustomPaletteV = settings.value("export_custom_palette");
    QVariant forcePipeV = settings.value("export_force_piped");
    QVariant loopAnimV = settings.value("export_loop_anim");
    QVariant exportWithAudioV = settings.value("export_allow_audio");
    // Initialize pointers to generics
    auto* aspectRatio = new int;
    auto intermediateType = new int;
    auto allowTransparency = new bool;
    auto allowCustomParam = new bool;
    auto allowInterParam = new bool;
    auto allowPostParam = new bool;
    auto useCustomPalette = new bool;
    auto forcePipe = new bool;
    auto loopAnim = new bool;
    auto exportWithAudio = new bool;
    // Gather initialized pointers into a vector
    QVector<QVariant *> intValues {
        &aspectRatioV, &intermediateTypeV
    };
    QVector<int *> intVariables {
        aspectRatio, intermediateType
    };
    QVector<QVariant *> boolValues {
        &allowTransparencyV, &allowCustomParamV, &allowInterParamV, &allowPostParamV, &useCustomPaletteV, &forcePipeV,
        &loopAnimV, &exportWithAudioV
    };
    QVector<bool *> boolVariables {
        allowTransparency, allowCustomParam, allowInterParam, allowPostParam, useCustomPalette, forcePipe, loopAnim,
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
    // Get formats
    QVariant pixelFormatV = settings.value("export_pixel_format");
    QVariant aviEncV = settings.value("export_avi_encoder");
    QVariant mkvEncV = settings.value("export_mkv_encoder");
    QVariant movEncV = settings.value("export_mov_encoder");
    QVariant mp4EncV = settings.value("export_mp4_encoder");
    QVariant webmEncV = settings.value("export_webm_encoder");
    pixelFormats pixelFormat;
    aviEncoders aviEncoder;
    mkvEncoders mkvEncoder;
    movEncoders movEncoder;
    mp4Encoders mp4Encoder;
    webmEncoders webmEncoder;
    // Loop through the vectors and initialize their values
    QVector<QVariant *> formats {
        &pixelFormatV, &aviEncV, &mkvEncV, &movEncV, &mp4EncV, &webmEncV
    };
    for(auto variant: formats){
        if(!variant->isValid()){
            variant->setValue(QString("Auto"));
            settings.sync();
        }
    }
    // Cast the values gathered into an enum for the corresponding value
    pixelFormat = static_cast<pixelFormats>(getFormatAsInt(exportTarget::pxFmt, pixelFormatV.toString()));
    aviEncoder = static_cast<aviEncoders>(getFormatAsInt(exportTarget::aviEnc, aviEncV.toString()));
    mkvEncoder = static_cast<mkvEncoders>(getFormatAsInt(exportTarget::mkvEnc, mkvEncV.toString()));
    movEncoder = static_cast<movEncoders>(getFormatAsInt(exportTarget::movEnc, movEncV.toString()));
    mp4Encoder = static_cast<mp4Encoders>(getFormatAsInt(exportTarget::mp4Enc, mp4EncV.toString()));
    webmEncoder = static_cast<webmEncoders>(getFormatAsInt(exportTarget::webmEnc, webmEncV.toString()));
    // Get custom parameters
    QVariant customParamsV = settings.value("export_custom_params");
    QVariant customParamsStringsV = settings.value("export_custom_params_str");
    QStringList *customParams =
        customParamsV.isValid()? new QStringList(customParamsV.toStringList()) : new QStringList();
    QStringList *customParamsStrings =
        customParamsStringsV.isValid()? new QStringList(customParamsStringsV.toStringList()) : new QStringList();
    // Sync
    settings.sync();
    // Value initialization
    switch(*aspectRatio){
        case 0: exportUI->oneToOneRatio->setChecked(true); break;
        case 1: exportUI->keepAspectRatio->setChecked(true); break;
        case 2: exportUI->customRatio->setChecked(true); break;
        default: break;
    }
    exportUI->intermediateTypeCombo->setCurrentIndex(*intermediateType);
    exportUI->transparencyCheckBox->setChecked(*allowTransparency);
    exportUI->forcePipeCheckBox->setChecked(*forcePipe);
    exportUI->loopCheckBox->setChecked(*loopAnim);
    exportUI->exportWithAudioCheckBox->setChecked(*exportWithAudio);
    exportUI->allowParamsCheckBox->setChecked(*allowCustomParam);
    exportUI->intermediateParamCheckBox->setChecked(*allowInterParam);
    exportUI->postParamCheckBox->setChecked(*allowPostParam);
    exportUI->customPaletteCheckBox->setChecked(*useCustomPalette);
    exportUI->pixelFormatCombo->setCurrentIndex(static_cast<int>(pixelFormat));
    exportUI->aviCombo->setCurrentIndex(static_cast<int>(aviEncoder));
    exportUI->mkvCombo->setCurrentIndex(static_cast<int>(mkvEncoder));
    exportUI->movCombo->setCurrentIndex(static_cast<int>(movEncoder));
    exportUI->mp4Combo->setCurrentIndex(static_cast<int>(mp4Encoder));
    exportUI->webmCombo->setCurrentIndex(static_cast<int>(webmEncoder));
    exportUI->presetCombo->addItems(*customParamsStrings);
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
    exportUI->lastFrameSpinBox->setValue(mCurrent->currentTimeInfo().frameMax);
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
    connect(exportUI->saveIntermediateParamAsPreset, &QToolButton::clicked, [=]() mutable{
        regenerateGeneralPurposeDiag();
        customParams->append(exportUI->intermediateParamTextEdit->toPlainText());
        exportUI->askForTextUI(gpDiag, tr("Select your preset name"));
        gpDiag->exec();
        if(exportUI->askOperationCancelled){
            return;
        }
        customParamsStrings->append(exportUI->textEdit->toPlainText());
        exportUI->presetCombo->addItem(exportUI->textEdit->toPlainText());
        exportUI->presetCombo->setCurrentIndex(customParamsStrings->indexOf(exportUI->textEdit->toPlainText()));
    });
    connect(exportUI->savePostParamAsPreset, &QToolButton::clicked, [=]() mutable{
        regenerateGeneralPurposeDiag();
        customParams->append(exportUI->postParamTextEdit->toPlainText());
        exportUI->askForTextUI(gpDiag, tr("Select your preset name"));
        gpDiag->exec();
        if(exportUI->askOperationCancelled){
            return;
        }
        customParamsStrings->append(exportUI->textEdit->toPlainText());
        exportUI->presetCombo->addItem(exportUI->textEdit->toPlainText());
        exportUI->presetCombo->setCurrentIndex(customParamsStrings->indexOf(exportUI->textEdit->toPlainText()));
    });
    connect(exportUI->removePreset, &QPushButton::clicked, [=]() mutable{
        int index = exportUI->presetCombo->currentIndex();
        customParams->removeAt(index);
        customParamsStrings->removeAt(index);
        exportUI->presetCombo->removeItem(index);
    });
    connect(exportUI->addPresetToIntermediate, &QPushButton::clicked, [=](){
        int index = exportUI->presetCombo->currentIndex();
        if(!customParams->isEmpty() && customParams->length() >= index){
                exportUI->intermediateParamTextEdit->append(customParams->at(index));
            }
    });
    connect(exportUI->addPresetToPost, &QPushButton::clicked, [=](){
        int index = exportUI->presetCombo->currentIndex();
        if(!customParams->isEmpty() && customParams->length() >= index){
            exportUI->postParamTextEdit->append(customParams->at(index));
        }
    });

    // Execute
    QGuiApplication::restoreOverrideCursor();
    exportWidget->exec();

    // Save all settings
    int aspectRatioIndex = 0;
    if (exportUI->oneToOneRatio->isChecked()) { aspectRatioIndex = 0; }
    else if (exportUI->keepAspectRatio->isChecked()) { aspectRatioIndex = 1; }
    else { aspectRatioIndex = 2; }
    {
        updateSettings(&aspectRatioV, &settings, aspectRatioIndex,
                       "export_aspect_ratio");
        updateSettings(&intermediateTypeV, &settings, exportUI->intermediateTypeCombo->currentIndex(),
                       "export_intermediate_type");
        updateSettings(&allowTransparencyV, &settings, exportUI->transparencyCheckBox->isChecked(),
                       "export_allow_transparency");
        updateSettings(&allowCustomParamV, &settings, exportUI->allowParamsCheckBox->isChecked(),
                       "export_allow_params");
        updateSettings(&allowInterParamV, &settings, exportUI->intermediateParamCheckBox->isChecked(),
                       "export_allow_param_inter");
        updateSettings(&allowPostParamV, &settings, exportUI->postParamCheckBox->isChecked(),
                       "export_allow_param_post");
        updateSettings(&useCustomPaletteV, &settings, exportUI->customPaletteCheckBox->isChecked(),
                       "export_custom_palette");
        updateSettings(&forcePipeV, &settings, exportUI->forcePipeCheckBox->isChecked(),
                       "export_force_piped");
        updateSettings(&loopAnimV, &settings, exportUI->loopCheckBox->isChecked(),
                       "export_loop_anim");
        updateSettings(&exportWithAudioV, &settings, exportUI->exportWithAudioCheckBox->isChecked(),
                       "export_allow_audio");
        updateSettings(&pixelFormatV, &settings, exportUI->pixelFormatCombo->currentText(),
                       "export_pixel_format");
        updateSettings(&aviEncV, &settings, exportUI->aviCombo->currentText(),
                       "export_avi_encoder");
        updateSettings(&mkvEncV, &settings, exportUI->mkvCombo->currentText(),
                       "export_mkv_encoder");
        updateSettings(&movEncV, &settings, exportUI->movCombo->currentText(),
                       "export_mov_encoder");
        updateSettings(&mp4EncV, &settings, exportUI->mp4Combo->currentText(),
                       "export_mp4_encoder");
        updateSettings(&webmEncV, &settings, exportUI->webmCombo->currentText(),
                       "export_webm_encoder");
        updateSettings(&customParamsV, &settings, *customParams,
                       "export_custom_params");
        updateSettings(&customParamsStringsV, &settings, *customParamsStrings,
                       "export_custom_params_str");
        settings.sync();
    }
    // Get file name and folder for export
    if (exportUI->operationCancelled) { return; }
    // Initialize the file dialog with the appropriate descriptors
    QFileDialog fileDiag;
    fileDiag.setAcceptMode(QFileDialog::AcceptMode::AcceptSave);
    fileDiag.setFileMode(QFileDialog::AnyFile);
    QStringList vF = videoFormats;
    QStringList videoFormatDescriptor{
        tr("Animated PNG"), tr("AVI"), tr("Flash Video"), tr("Flash Video"),
        tr("GIF"), tr("Matroska"), tr("QuickTime Movie"), tr("MPEG-2"),
        tr("MPEG-4"), tr("Ogg Video"), tr("Shockwave Flash"), tr("WEBM"), tr("WEBP")
    };
    QStringList iF = imageFormats;
    QStringList imageFormatDescriptor{
        tr("BitMap"), tr("JPEG"), tr("JPEG"), tr("PNG"), tr("Portable PixelMap"),
        tr("X11 BitMap"), tr("X11 PixelMap"), tr("Tagged Image"), tr("WEBP")
    };
    x = 0;
    for(auto &format: vF){
        format.push_front(videoFormatDescriptor[x] + " (*.");
        format.push_back(')');
        x++;
    }
    x = 0;
    for(auto &format: iF){
        format.push_front(imageFormatDescriptor[x] + " (*.");
        format.push_back(')');
        x++;
    }
    if(exportUI->exportTypeCombo->currentIndex() == 0) {
        fileDiag.setNameFilters(vF);
        fileDiag.selectNameFilter("MPEG-4");
    }
    else {
        fileDiag.setNameFilters(iF);
        fileDiag.selectNameFilter("PNG");
    }
    // Generate parameters prior to exportUI destructor
    genParam.fileName = mCurrent->fileName().isEmpty()? "New Export" : QString(mCurrent->fileName());
    genParam.nativeWidth = mCurrent->attribute().imageSize().width();
    genParam.nativeHeight = mCurrent->attribute().imageSize().height();
    genParam.exportWidth = exportUI->widthSpinBox->value();
    genParam.exportHeight = exportUI->heightSpinBox->value();
    switch(aspectRatioIndex) {
    case 0:
        genParam.aspectRatio = oneToOne;
        break;
    case 1:
        genParam.aspectRatio = keep;
        break;
    case 2:
        genParam.aspectRatio = custom;
        break;
    }
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
    genParam.imageExportQuality = exportUI->imageQualitySpinbox->value();
    genParam.allowTransparency = exportUI->transparencyCheckBox->isChecked();
    genParam.forcePipe = exportUI->forcePipeCheckBox->isChecked();
    genParam.loop = exportUI->loopCheckBox->isChecked();
    genParam.exportWithAudio = exportUI->exportWithAudioCheckBox->isChecked();
    genParam.useCustomParam = exportUI->allowParamsCheckBox->isChecked();
    genParam.useIntermediate = exportUI->intermediateParamCheckBox->isChecked();
    genParam.usePost = exportUI->postParamCheckBox->isChecked();
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
    genParam.customInterCommand = exportUI->intermediateParamTextEdit->toPlainText();
    genParam.customPostCommand = exportUI->postParamTextEdit->toPlainText();
    if(exportUI->exportTypeCombo->currentIndex() == 0){ exParam->exportType = exportTarget::video; }
    else{ exParam->exportType = exportTarget::image; }
    if(exParam->exportType == exportTarget::video){
        exParam->videoParams.intermediateFormat = static_cast<availableIntermediateFormats>(
            exportUI->intermediateTypeCombo->currentIndex()
            );
        exParam->videoParams.pixelFormat = static_cast<pixelFormats>(
                getFormatAsInt(exportTarget::pxFmt, exportUI->pixelFormatCombo->currentText())
            );
        defaultEncoders encoders;
        encoders.avi = static_cast<aviEncoders>(getFormatAsInt(exportTarget::aviEnc, exportUI->aviCombo->currentText()));
        encoders.mkv = static_cast<mkvEncoders>(getFormatAsInt(exportTarget::mkvEnc, exportUI->mkvCombo->currentText()));
        encoders.mov = static_cast<movEncoders>(getFormatAsInt(exportTarget::movEnc, exportUI->movCombo->currentText()));
        encoders.mp4 = static_cast<mp4Encoders>(getFormatAsInt(exportTarget::mp4Enc, exportUI->mp4Combo->currentText()));
        encoders.webm = static_cast<webmEncoders>(getFormatAsInt(exportTarget::webmEnc, exportUI->webmCombo->currentText()));
        exParam->videoParams.encoders = encoders;
    }
    // Get file via OS diag
    fileDiag.setViewMode(QFileDialog::Detail);
    QString selectedFile;
    if(fileDiag.exec()){ selectedFile = fileDiag.selectedFiles().at(0); }
    else{ return; }
    bool isVideoExport = exParam->exportType == exportTarget::video;
    int suffixIndex = isVideoExport? (int)vF.indexOf(fileDiag.selectedNameFilter()) : (int)iF.indexOf(fileDiag.selectedNameFilter());
    QString sfx = getFormatAsString(exParam->exportType, suffixIndex);
    qDebug() << "Exporting animation to format: " << sfx;
    if(QFileInfo(selectedFile).suffix() == ""){
        auto regex = QRegularExpression("(\\*.)\\w+", QRegularExpression::CaseInsensitiveOption);
        QString suffix = regex.match(fileDiag.selectedNameFilter()).captured(0).remove(0, 1);
        qDebug() << suffix;
        selectedFile.append(suffix);
    }
    qDebug() << selectedFile;
    // Generate export parameters
    genParam.exportName = QFileInfo(selectedFile).fileName();
    genParam.exportDirectory = QFileInfo(selectedFile).absoluteDir();
    genParam.exportFileName = QDir(selectedFile);
    genParam.osExportTarget = selectedFile;
    exParam->generalParams = genParam;

    if(exParam->exportType == exportTarget::video) {
        exParam->videoParams.format = static_cast<availableVideoFormats>(suffixIndex);
    }
    else{
        exParam->imageParams.format = static_cast<availableImageFormats>(suffixIndex);
    }
    // Main export function
    std::vector<audioConfig> validAudioStreams = AudioPlaybackWidget::getValidAudioStreams(*mCurrent->pConf);
    if(isExportParamValid(exParam, exportWidget, &validAudioStreams)){
        exportProject(*exParam, mCurrent, exportWidget, validAudioStreams);
    }
}

void MainWindow::onExportImageSeqTriggered(const QString& aSuffix) {
    if (!mCurrent)
        return;

    // stop animation and main display rendering
    EventSuspender suspender(*mMainDisplay, *mTarget);

    // export directory
    QString dirName = QFileDialog::getExistingDirectory(this, tr("Export Folder"));

    // make sure existing
    if (dirName.isEmpty())
        return;
    if (!QFileInfo::exists(dirName))
        return;

    // export param
    ctrl::Exporter::CommonParam cparam;
    ctrl::Exporter::ImageParam iparam;
    {
        QScopedPointer<ImageExportDialog> dialog(new ImageExportDialog(*mCurrent, dirName, aSuffix, this));

        dialog->exec();
        if (dialog->result() != QDialog::Accepted)
            return;

        cparam = dialog->commonParam();
        iparam = dialog->imageParam();
    }

    // gui for confirm overwrite
    auto overwriteConfirmer = [=](const QString&) -> bool {
        QMessageBox msgBox;
        msgBox.setText(tr("File already exists."));
        msgBox.setInformativeText(tr("Do you want to overwrite the existing file?"));
        msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Ok);
        int ret = msgBox.exec();
        return (ret == QMessageBox::Ok);
    };

    menu::ProgressReporter progress(true, this);
    ctrl::Exporter exporter(*mCurrent);
    exporter.setOverwriteConfirmer(overwriteConfirmer);
    exporter.setProgressReporter(progress);

    // execute
    if (!exporter.execute(cparam, iparam)) {
        progress.cancel();
        if (!exporter.isCanceled()) {
            QMessageBox::warning(nullptr, tr("Export Error"), exporter.log());
        }
    }
}

void MainWindow::onQuickExportTriggered(const QString& aFormat) {
    // General parameters
    GeneralParams genParam;
    genParam.fileName = mCurrent->fileName();
    if (genParam.fileName.isEmpty()) {
        genParam.fileName = "New Export";
    }
    genParam.exportName = QFileInfo(genParam.fileName).baseName();
    genParam.exportDirectory = QFileDialog::getExistingDirectory(this, tr("Export Folder"));
    genParam.exportFileName = QDir(genParam.exportDirectory.absolutePath() + "/" + genParam.exportName + "." +
        QString(aFormat).replace("_o", "").replace("_t", "").replace(".anie", ""));
    genParam.osExportTarget = QFileInfo(genParam.exportName).absoluteFilePath();
    genParam.nativeWidth = mCurrent->attribute().imageSize().width();
    genParam.nativeHeight = mCurrent->attribute().imageSize().height();
    genParam.exportWidth = mCurrent->attribute().imageSize().width();
    genParam.exportHeight = mCurrent->attribute().imageSize().height();
    genParam.aspectRatio = custom;
    genParam.nativeFPS = mCurrent->attribute().fps();
    genParam.fps = mCurrent->attribute().fps();
    genParam.bitrate = 0;
    genParam.imageExportQuality = 100;
    genParam.allowTransparency = aFormat.contains("_t") || aFormat == "png";
    genParam.forcePipe = false;
    genParam.loop = true;
    genParam.exportWithAudio = !mCurrent->pConf->empty();
    genParam.useCustomParam = false;
    frameExportRange fer;
    fer.lastFrame = mCurrent->attribute().maxFrame();
    genParam.nativeFrameRange = fer;
    genParam.exportAllFrames = true;
    genParam.exportRange = {fer};
    genParam.exportToLast = true;
    genParam.imageExportQuality = -1;
    // Get format info
    bool imageExport = false;
    ImageParams img;
    VideoParams vid;
    if (aFormat == "png") {
        imageExport = true;
        img.format = availableImageFormats::png;
    }
    else {
        if (aFormat.contains("gif")) {
            vid.format = availableVideoFormats::gif;
        }
        else if (aFormat.contains("mp4")) {
            vid.format = availableVideoFormats::mp4;
        }
        else if (aFormat.contains("avi")) {
            vid.format = availableVideoFormats::avi;
        }
        else if (aFormat.contains("webm")) {
            vid.format = availableVideoFormats::webm;
        }
        if (!genParam.allowTransparency)
            vid.intermediateFormat = availableIntermediateFormats::ppm;
        else {
            vid.intermediateFormat = availableIntermediateFormats::png;
        }
    }
    // Export parameters
    auto exParam = new exportParam();
    exParam->generalParams = genParam;
    exParam->imageParams = img;
    exParam->videoParams = vid;
    exParam->exportType = imageExport? exportTarget::image : exportTarget::video;

    // Main export function
    std::vector<audioConfig> validAudioStreams = AudioPlaybackWidget::getValidAudioStreams(*mCurrent->pConf);
    if(isExportParamValid(exParam, exportWidget, &validAudioStreams)){
        exportProject(*exParam, mCurrent, exportWidget, validAudioStreams);
    }
}

void MainWindow::onExportVideoTriggered(const ctrl::VideoFormat& aFormat) {
    if (!mCurrent)
        return;
    // FFmpeg Check
    QString ffmpeg = GeneralSettingDialog::getFFmpeg();
    auto* aDiag = new GeneralSettingDialog(mGUIResources, this);
    bool ffmpegWorks = GeneralSettingDialog::ffmpegCheck(ffmpeg, aDiag);
    if (!ffmpegWorks) {
        GeneralSettingDialog::ffmpegCheckFailed(aDiag);
        return;
    }
    // stop animation and main display rendering
    EventSuspender suspender(*mMainDisplay, *mTarget);

    const QString suffix = aFormat.name;
    const QString targetVideos = "Videos (*." + suffix + ")";
    const bool isGif = suffix == "gif";

    // get export file name
    QString fileName = QFileDialog::getSaveFileName(
        this,
        tr("Export File"),
        QString(), // dir
        targetVideos
    );
    const QFileInfo fileInfo(fileName);

    // make sure existing
    if (fileName.isEmpty())
        return;
    if (!fileInfo.dir().exists())
        return;

    if (fileInfo.suffix().isEmpty()) {
        fileName += "." + suffix; // makesure suffix
    } else if (fileInfo.suffix() != suffix) {
        QMessageBox::warning(nullptr, tr("Operation Error"), tr("Invalid extension specified."));
        return;
    }

    // export param
    ctrl::Exporter::CommonParam cparam;
    ctrl::Exporter::VideoParam vparam;
    ctrl::Exporter::GifParam gparam;
    if (isGif) {
        QScopedPointer<GifExportDialog> dialog(new GifExportDialog(*mCurrent, fileName, this));
        dialog->exec();
        if (dialog->result() != QDialog::Accepted)
            return;

        cparam = dialog->commonParam();
        gparam = dialog->gifParam();
    } else {
        QScopedPointer<VideoExportDialog> dialog(new VideoExportDialog(*mCurrent, fileName, aFormat, this));
        dialog->exec();
        if (dialog->result() != QDialog::Accepted)
            return;

        cparam = dialog->commonParam();
        vparam = dialog->videoParam();
    }
    // vparam.codec = codec;

    menu::LoggableProgressReporter progress(true, this);
    ctrl::Exporter exporter(*mCurrent);
    exporter.setOverwriteConfirmer([=](const QString&) -> bool { return true; });
    exporter.setProgressReporter(progress);
    exporter.setUILogger(progress);

    // execute
    auto result = isGif ? exporter.execute(cparam, gparam) : exporter.execute(cparam, vparam);

    if (!result) {
        progress.cancel();

        if (result.code == ctrl::Exporter::ResultCode_Canceled) {
        } else if (result.code == ctrl::Exporter::ResultCode_FFMpegFailedToStart) {
            QMessageBox message;
            message.setIcon(QMessageBox::Warning);
            message.setText(tr("FFmpeg was not found."));
            auto infoText = tr("Video export requires FFmpeg.") + "\n" +
                tr("Install FFmpeg on the system, or place a FFmpeg executable "
                   "under \"/tools\" in the folder where you installed AnimeEffects.");
            message.setInformativeText(infoText);
            message.setStandardButtons(QMessageBox::Ok);
            message.setDefaultButton(QMessageBox::Ok);
            message.exec();
        } else {
            QMessageBox::warning(nullptr, tr("Export Error"), exporter.log());
        }
    }
}
void MainWindow::onMoveFrameTriggered(bool moveRight) {
    int frameDest = moveRight? 1: -1;
    core::Frame dest = mTarget->currentFrame();
    dest.add(frameDest);
    mTarget->timeLineWidget().setFrame(dest);
}

} // namespace gui
