#include <QApplication>
#include <QColor>
#include <QClipboard>
#include <QGuiApplication>
#include <QRegularExpression>
#include "GeneralSettingDialog.h"
#include "util/IProgressReporter.h"
#include "gl/Global.h"
#include "ctrl/Exporter.h"
#include "gui/MainWindow.h"

#include "gui/ExportFacts.h"
#include "gui/ExportDialog.h"
#include "gui/CommandLogView.h"
#include "gui/HeaderBar.h"
#include "gui/qml/ExportViewModel.h"
#include "gui/qml/DockSlotsModel.h"
#include "gui/qml/MenuCommand.h"
#include "gui/qml/ViewSettingsModel.h"
#include "gui/DestinationPicker.h"
#include "gui/DockDropOverlay.h"
#include "gui/DockMetrics.h"
#include "gui/DockPanel.h"
#include "gui/DockSash.h"
#include "gui/DockSlots.h"
#include "gui/DockTabBar.h"
#include "gui/NewProjectDialog.h"
#include "gui/ProjectHook.h"
#include "gui/menu/menu_ProgressReporter.h"
#include "util/NetworkUtil.h"
#include <QTabBar>
#include <QTimer>
#include <set>
#include <utility>
#include "media/FFmpegHelpers.h"
#include <QComboBox>
#include <QStatusBar>
// This thing is held by duct tape and OOP hell I swear...
#include "ctrl/ExportParams.h"

namespace {

// A Shift binding is SPELLED on the unshifted key ("Shift + ]" is a binding on
// BracketRight), while Qt DELIVERS the shifted symbol (BraceRight + Shift). Without
// this mapping every Shift+punctuation binding silently never fires - measured:
// Shift+] did nothing while ] worked, because the matcher compares the key exactly.
int unshiftedKeyForShift(int aKey) {
    switch (aKey) {
    case Qt::Key_BraceLeft: return Qt::Key_BracketLeft;
    case Qt::Key_BraceRight: return Qt::Key_BracketRight;
    case Qt::Key_Exclam: return Qt::Key_1;
    case Qt::Key_At: return Qt::Key_2;
    case Qt::Key_NumberSign: return Qt::Key_3;
    case Qt::Key_Dollar: return Qt::Key_4;
    case Qt::Key_Percent: return Qt::Key_5;
    case Qt::Key_AsciiCircum: return Qt::Key_6;
    case Qt::Key_Ampersand: return Qt::Key_7;
    case Qt::Key_Asterisk: return Qt::Key_8;
    case Qt::Key_ParenLeft: return Qt::Key_9;
    case Qt::Key_ParenRight: return Qt::Key_0;
    case Qt::Key_Underscore: return Qt::Key_Minus;
    case Qt::Key_Plus: return Qt::Key_Equal;
    case Qt::Key_Colon: return Qt::Key_Semicolon;
    case Qt::Key_QuoteDbl: return Qt::Key_Apostrophe;
    case Qt::Key_Less: return Qt::Key_Comma;
    case Qt::Key_Greater: return Qt::Key_Period;
    case Qt::Key_Question: return Qt::Key_Slash;
    case Qt::Key_AsciiTilde: return Qt::Key_QuoteLeft;
    case Qt::Key_Bar: return Qt::Key_Backslash;
    default: return aKey;
    }
}

// The event as a binding, with Shift's symbol folded back onto its key.
ctrl::KeyBinding eventBinding(const QKeyEvent& aEvent) {
    const int key = (aEvent.modifiers() & Qt::ShiftModifier) ? unshiftedKeyForShift(aEvent.key()) : aEvent.key();
    return ctrl::KeyBinding(key, aEvent.modifiers());
}

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
    mDockAnimationWidget(),
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
        // Qt's own dock drag is a drag-and-drop, so the window has to accept
        // drops for it to have a target at all: with drops off the drag runs
        // with a "no drop" cursor and nothing ever docks. QMainWindow turns
        // this on itself; the line here used to turn it off. Foreign drags are
        // still ignored - the dock overlay only accepts this app's panel MIME
        // type, and there is no other drop handler on this window.
        this->setAcceptDrops(true);
        this->setTabShape(QTabWidget::Rounded);
        // AllowTabbedDocks is the piece of Qt's docking this app actually wants:
        // two panels sharing an area become tabs at the top of that area, and
        // that tab strip is the same title UI a lone panel shows as its header -
        // minimal, in the same visual language as the project tabs (the theme's
        // QTabBar rules already carry the rest/hover/selected dressing). The
        // default is AnimatedDocks|AllowTabbedDocks; this line used to drop the
        // tabbing half.
        // AllowNestedDocks is what lets an area hold two columns: without it a
        // dock area is a single row, so a second dock added to an occupied side
        // area is *appended to that area's stack* rather than taking the slice
        // beside the canvas that the drop promised. Qt's warning about nesting
        // (more ways to place a dropped dock) does not apply: the drop rules are
        // ours (DockDropOverlay), and this only widens what they can express.
        this->setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks
                             | QMainWindow::AllowNestedDocks);
        this->setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
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

    // The menu bar's command registry. The header scene is created AFTER the docks
    // and their Panels commands, because its Actions bind to a complete registry on
    // load; MainMenuBar itself owns no widget.
    {
        mMainMenuBar = new MainMenuBar(*this, mViaPoint, mGUIResources, this);
        mViaPoint.setMainMenuBar(mMainMenuBar);
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
        this->applyViewportBackground();

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
        auto* dockWidget = new gui::DockPanel(this);
        dockWidget->setObjectName("animationDock");
        dockWidget->setWindowTitle(tr("Animation"));
        // Closable is not for a close button: Qt hides its buttons as soon as a
        // title bar widget exists. It is required for toggleViewAction(), which
        // Qt leaves disabled on a dock that cannot close - and that action is
        // how a hidden panel gets back (see the Panels menu below).
        dockWidget->setFeatures(QDockWidget::DockWidgetFloatable
                                | QDockWidget::DockWidgetMovable
                                | QDockWidget::DockWidgetClosable);
        dockWidget->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
        this->addDockWidget(Qt::BottomDockWidgetArea, dockWidget);
        mDockAnimationWidget = dockWidget;

        // 296 rather than the 256 this dock used before the header bar: the bar
        // is 40px of fixed height above the surface, and the design's own budget
        // is that the DOCK gains that height (the ruler, the lanes and the tree
        // keep theirs) rather than the timeline losing it.
        //
        // The Graph tab does NOT raise it again: the tab is a second view of the
        // SAME surface pane (TargetWidget stacks the timeline's surface and the
        // panel in the one pane beside the tree), so both tabs are offered the
        // 40px bar's old height - 256px. That is more than the design asks of the
        // docked panel (~216px against the overlay's 430px, section 5's table), and
        // the timeline's own ruler-and-lanes budget is exactly those 256: one hint
        // serves both views, with the panel's extra height going to its plot rather
        // than to a taller dock.
        mTarget = new TargetWidget(mViaPoint, mGUIResources, mLocaleParam, dockWidget, QSize(256, 296));
        dockWidget->setWidget(mTarget);
    }

    // create property widget
    {
        auto* dockWidget = new gui::DockPanel(this);
        dockWidget->setObjectName("propertiesDock");
        dockWidget->setWindowTitle(tr("Properties"));
        dockWidget->setFeatures(QDockWidget::DockWidgetFloatable
                                | QDockWidget::DockWidgetMovable
                                | QDockWidget::DockWidgetClosable);
        dockWidget->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        this->addDockWidget(Qt::RightDockWidgetArea, dockWidget);

        mDockPropertyWidget = dockWidget;

        // No size range is set here: the panel's range is its *area's*, applied by
        // applyPanelMetrics, which clears and re-applies it when the panel changes
        // area or floats. The 320/640 that used to sit here was overwritten at the
        // first pass and only misled: every slot is 200..640 wide (DockMetrics.h),
        // and the rank of panels in it cannot change that.

#if 0
        mProperty = new PropertyWidget(mViaPoint, splitter, &mGUIResources, mLocaleParam);
        dockWidget->setWidget(mProperty);
#else
        auto splitter = new gui::ThinSplitter(Qt::Vertical, dockWidget);
        splitter->setObjectName("propertysplitter");
        dockWidget->setWidget(splitter);

        mProperty = new PropertyWidget(mViaPoint, splitter, &mGUIResources, mLocaleParam);
        splitter->addWidget(mProperty);

        // The command log: one text document, so a selection can cross entries
        // (see CommandLogView for why a delegate list would lose that). It owns
        // the cap, the read-only flags and the object/accessibility names; the
        // splitter owns the widget, ViaPoint is what everyone reports through.
        auto* commandLog = new CommandLogView(splitter);
        mViaPoint.setLogView(commandLog);
        splitter->addWidget(commandLog);
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
        auto* dockWidget = new gui::DockPanel(this);
        dockWidget->setObjectName("toolsDock");
        dockWidget->setWindowTitle(tr("Tools"));
        dockWidget->setFeatures(QDockWidget::DockWidgetFloatable
                                | QDockWidget::DockWidgetMovable
                                | QDockWidget::DockWidgetClosable);
        this->addDockWidget(Qt::LeftDockWidgetArea, dockWidget);

        mDockToolWidget = dockWidget;

        mTool = new ToolWidget(mViaPoint, *mKeyCommandMap, mLocaleParam, QSize(236, 340), dockWidget);
        dockWidget->setWidget(mTool);
    }

    // create resource dialog
    {
        mResourceDialog = new ResourceDialog(mViaPoint, false, this);
        mViaPoint.setResourceDialog(mResourceDialog);
    }

    // panels are hidden from their own header menu, so the menu bar has to be
    // able to bring them back: one checkable command per panel, in the order they
    // sit in the window (left, right, bottom). The command's checked state is read
    // back from the dock after every layout pass, never assumed from the click.
    {
        for (QDockWidget* dock : {mDockToolWidget, mDockPropertyWidget, mDockAnimationWidget}) {
            const QString id = QStringLiteral("panel")
                               + (dock == mDockToolWidget ? QStringLiteral("Tools")
                                 : dock == mDockPropertyWidget ? QStringLiteral("Properties")
                                                               : QStringLiteral("Animation"));
            MenuCommand* command = mMainMenuBar->addCommand(id, dock->windowTitle(), {}, true);
            connect(command, &MenuCommand::triggered, this, [this, dock]() {
                dock->setVisible(!dock->isVisible());
                updateLayoutButtons();
            });
            mPanelToggles.push_back({dock, command});
        }
    }

    // A grouped panel hides its own header, because the group's tab strip is its
    // title row - and the header's context menu goes into hiding with it. The tab
    // is that panel's title UI in this state, so the menu moves to the tab; the
    // tab bars themselves are Qt's, so they are found and adopted rather than
    // built (see updateDockTabMenus).
    this->installEventFilter(this);

    // The drop side of the dock gesture: one overlay over the window that
    // decides the target, draws it, and performs it (see DockDropOverlay).
    mDockDropOverlay = new DockDropOverlay(this);
    mDockDropOverlay->onDropped = [this](QDockWidget* aPanel, const DockDropTarget& aTarget) {
        applyDockDrop(aPanel, aTarget);
    };

    // One size range for every panel, and one set of allowed areas: a tab group
    // can only be as narrow as its strictest member and as wide as its most-capped
    // one, and a panel that is only allowed in one area can never be tabbed with a
    // panel that lives on another side. See DockMetrics.h.
    int order = 0;
    for (QDockWidget* dock : {mDockToolWidget, mDockPropertyWidget, mDockAnimationWidget}) {
        dock->setAllowedAreas(Qt::AllDockWidgetAreas);
        // The tab order of a group: creation order, fixed, so a tab does not jump
        // to the front when it is activated (the strip reads this).
        dock->setProperty("aeDockOrder", order++);
        if (auto* panel = qobject_cast<DockPanel*>(dock)) panel->setMainWindow(this);
        // Every title row understands the same gestures, whether it is this panel's
        // own (one tab) or Qt's bar for a group it joins.
        if (auto* titleBar = qobject_cast<QTabBar*>(dock->titleBarWidget()))
            new DockTabBarFilter(titleBar, this);
        applyPanelMetrics(dock);
        // the range depends on the area, so it is re-applied whenever the panel
        // changes area or floats; the chrome and the slot buttons follow the same
        // moves (a tabified/hidden panel is still a member of its slot). The
        // chrome pass is deferred: a drop sends these signals while the drag is
        // still in flight, and moving a title row mid-drag ends the drag.
        connect(dock, &QDockWidget::dockLocationChanged, this,
                [this, dock](Qt::DockWidgetArea) {
                    clearDockOpeningFloors();
                    applyPanelMetrics(dock);
                    scheduleDockChromeSync();
                });
        connect(dock, &QDockWidget::topLevelChanged, this,
                [this, dock](bool) {
                    clearDockOpeningFloors();
                    applyPanelMetrics(dock);
                    scheduleDockChromeSync();
                });
        connect(dock, &QDockWidget::visibilityChanged, this,
                [this](bool) { scheduleDockChromeSync(); });
    }

    // THE PANELS' OPENING SIZES. applyPanelMetrics keeps the content's hint from
    // constraining the slot (the area owns the range), so Qt opens every panel at
    // its area's MINIMUM: a 200px Properties panel with clipped fields and a
    // 160px timeline. These explicit minimums are the former design sizes: they
    // shape the first layout pass only, and are dropped the moment the user first
    // resizes or re-docks a panel, because from then on the range is DockMetrics'
    // (a floor that stayed would make 200px slots unreachable).
    if (mDockToolWidget) mDockToolWidget->setMinimumWidth(236);
    if (mDockPropertyWidget) mDockPropertyWidget->setMinimumWidth(320);
    if (mDockAnimationWidget) mDockAnimationWidget->setMinimumHeight(296);
    mDockOpeningFloors = true;

    // The header itself: one Qt Quick scene for the menu bar, the view settings and
    // the slot toggles. It is created here, after the Panels commands, so the QML
    // Actions bind to a complete command registry the first time they evaluate.
    {
        mHeaderBar = new HeaderBar(*mMainMenuBar, mViaPoint, *mKeyCommandMap, mLocaleParam, this);
        this->setMenuWidget(mHeaderBar);
        connect(mHeaderBar, &HeaderBar::dockSlotToggleRequested,
                this, &MainWindow::onDockSlotToggleRequested);
        connect(mHeaderBar, &HeaderBar::viewSettingChanged, this, [this]() {
            if (mMainDisplay)
                mMainDisplay->onViewSettingChanged(mViaPoint.mainViewSetting());
        });
    }

    // Layout toggles in the header, where VS Code keeps them - one per *slot*, not
    // one per panel. A slot is a place a panel can be (left side, right side, bottom),
    // it exists whether or not anything is in it, and its glyph is the slot's shape
    // and never changes: a button whose picture followed a panel around the window was
    // describing the panel, which the tab already does. A slot with nothing in it is
    // simply empty, and its button says so by being disabled.
    //
    // The buttons themselves are HeaderSlotButton instances in qml/HeaderBar.qml;
    // this window owns only the model's contents and the toggle. The labels stay here
    // because their context is the window's, where the docking code lives.
    {
        struct Slot {
            Qt::DockWidgetArea area;
            const char* glyph;
            const char* name;
        };
        static const Slot kSlots[] = {
            {Qt::LeftDockWidgetArea, "sidebar-left", QT_TR_NOOP("left side")},
            {Qt::RightDockWidgetArea, "sidebar-right", QT_TR_NOOP("right side")},
            {Qt::BottomDockWidgetArea, "sidebar-bottom", QT_TR_NOOP("bottom")},
        };

        QVector<DockSlotSpec> specs;
        for (const Slot& slot : kSlots) {
            // tr() the *fragment* from the QT_TR_NOOP above, so the whole tooltip is
            // in one language rather than English inside a translated sentence.
            const QString name = tr(slot.name);
            specs.push_back({slot.area, QString::fromLatin1(slot.glyph), tr("Show or hide the %1").arg(name)});
            mLayoutSlots.push_back({slot.area, QString::fromLatin1(slot.glyph), name});
        }
        mHeaderBar->dockSlots()->setSlots(specs);
        updateLayoutButtons();
    }

    // dock-edge sashes: 12px invisible hitboxes + 1px hairlines over the
    // zero-width native separators (see standard.ssa); resizes docks through
    // QMainWindow::resizeDocks
    auto* dockSash = new gui::DockSash(this);
    connect(dockSash, &gui::DockSash::resizeStarted,
            this, &MainWindow::clearDockOpeningFloors);

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
        // THE CARD'S PAINT BUTTON IS ARMED, NOT ARMED AGAIN: with the Mask tool already painting
        // THIS LAYER's matte, pressing Paint would re-arm what is already armed, so the card
        // disables it. The host is the only place that knows the tool AND its mask target, and
        // the target can move without the tool moving (a filter card's Paint).
        const auto updateMaskPaintState = [this, &prop]() {
            auto* driver = mDriverHolder->driver();
            core::ObjectNode* node = driver ? driver->currentTarget() : nullptr;
            const bool active = mTool && mTool->maskToolActive();
            prop.setMaskPaintState(
                node, active && mTool->maskPaintingLayerMask(),
                active && !mTool->maskPaintingLayerMask(),
                mTool ? mTool->maskLane() : core::TimeKeyType_TERM,
                mTool ? mTool->maskChannel() : core::FilterMaskChannel_Amount
            );
        };
        tool.onToolChanged.connect([updateMaskPaintState](ctrl::ToolType) { updateMaskPaintState(); });
        tool.onMaskTargetChanged.connect(updateMaskPaintState);

        // the temporary eyedropper: the viewport recognises it (it owns the modifiers)
        // and the mask panel takes the value
        prop.onMaskPaintRequested.connect([this](core::TimeKeyType aLane, core::FilterMaskChannel aChannel) {
            if (mTool)
                mTool->beginMaskPaint(aLane, aChannel);
        });
        prop.onLayerMaskPaintRequested.connect([this]() {
            if (mTool)
                mTool->beginLayerMaskPaint();
        });
        // The Properties mesh-spacing field's transient wireframe preview. The
        // viewport owns the target; the field only says when it wants it.
        prop.onMeshPreviewRequested.connect(
            [&disp](core::ObjectNode* aNode, bool aActive) {
                disp.setMeshPreviewTarget(aActive ? aNode : nullptr);
            }
        );
        disp.onBrushDragAdjusted.connect([this](int aSizeSteps, int aHardnessSteps) {
            if (mTool)
                mTool->adjustBrush(aSizeSteps, aHardnessSteps);
        });
        // the drag's continuous form: the fraction of a notch is applied as a fraction
        disp.onBrushNudged.connect([this](double aSizeSteps, double aHardnessSteps) {
            if (mTool)
                mTool->nudgeBrush(aSizeSteps, aHardnessSteps);
        });
        // the viewport's adjustment HUD reads the tools dock's ONE brush model
        disp.setBrushSource([this]() -> const ctrl::BrushParam* {
            return mTool ? mTool->activeBrush() : nullptr;
        });
        disp.onMaskValueSampled.connect([this](float aValue) {
            if (mTool)
                mTool->setMaskValue(aValue);
        });
        tool.onInvertMaskRequested.connect([this]() {
            if (mProperty)
                mProperty->invertMask();
        });
        tool.onFinalizeTool.connect(&disp, &MainDisplayWidget::onFinalizeTool);
        // The FFD tool's autoshow-mesh convenience writes the same view setting the
        // header's buttons write; both end at the viewport's one consumer. The
        // header's Show mesh button is a binding to ViewSettingsModel, so a direct
        // write from the tool also has to re-announce the setting or the button
        // would keep the old check state.
        tool.onViewSettingChanged.connect([this, &disp](const MainViewSetting& aSetting) {
            if (mHeaderBar)
                mHeaderBar->viewSettings()->refresh();
            disp.onViewSettingChanged(aSetting);
        });

        objTree.onSelectionChanged.connect(&driver, &DriverHolder::onSelectionChanged);
        objTree.onSelectionChanged.connect(mProperty, &PropertyWidget::onSelectionChanged);
        objTree.onSelectionChanged.connect(&timeLine, &TimeLineWidget::onSelectionChanged);
        // AFTER DriverHolder: the tools model derives its mask chooser from the
        // driver's target, which the first connection above has just moved.
        objTree.onSelectionChanged.connect(mTool, &ToolWidget::onSelectionChanged);
        objTree.onSelectionChanged.connect([updateMaskPaintState](core::ObjectNode*) {
            updateMaskPaintState();
        });
        prop.onMaskTargetsChanged.connect(mTool, &ToolWidget::refreshMaskTargets);

        // The graph works on every selected layer, but the signal above only
        // carries a representative node (null when the selection is empty or
        // mixed), so collect the whole tree selection here.
        objTree.onSelectionChanged.connect([this](core::ObjectNode*) {
            if (auto* drv = mDriverHolder->driver())
                drv->setGraphTargets(mTarget->objectTreeWidget().selectedNodes());
        });

        // A driver that already exists when the connections are made never saw
        // the selection, so hand it the current one (possibly empty).
        if (auto* drv = mDriverHolder->driver())
            drv->setGraphTargets(mTarget->objectTreeWidget().selectedNodes());
        updateMaskPaintState();

        // The tree's rows ARE the lanes (gui/AnimationRow.h): one row list, one height rule.
        // TargetWidget already pushes them on every change; this covers the case where a
        // restructure happened while this connection was being rewired.
        objTree.onRowsUpdated.connect([&timeLine, &objTree]() { timeLine.setRows(objTree.rows()); });
        objTree.onScrollUpdated.connect(&timeLine, &TimeLineWidget::onScrollUpdated);
        timeLine.onVerticalScrollRequested.connect([&objTree](int aValue) { objTree.setScrollOffset(aValue); });

        // The animation graph panel is a SECOND VIEW of this timeline: it shows the
        // timeline's own visible frame window and writes the timeline's own live key
        // selection (see MainDisplayWidget::setTimeLineWidget and
        // syncGraphPanelSharedState). Handed over here, once, because the timeline
        // widget outlives every project while the graph editor does not - it lives in
        // the driver, which is created and destroyed per project, so
        // MainDisplayWidget::setDriver re-hands the new editor this same state.
        disp.setTimeLineWidget(&timeLine);

        timeLine.onFrameUpdated.connect(&driver, &DriverHolder::onFrameUpdated);
        timeLine.onFrameUpdated.connect(&prop, &PropertyWidget::onFrameUpdated);
        timeLine.onFrameUpdated.connect(mTool, &ToolWidget::onFrameUpdated);
        timeLine.onPlayBackStateChanged.connect(&prop, &PropertyWidget::onPlayBackStateChanged);

        menu.onProjectAttributeUpdated.connect(&disp, &MainDisplayWidget::onProjectAttributeUpdated);
        menu.onProjectAttributeUpdated.connect(&timeLine, &TimeLineWidget::onProjectAttributeUpdated);
        menu.onProjectAttributeUpdated.connect(&driver, &DriverHolder::onProjectAttributeUpdated);
        menu.onProjectAttributeUpdated.connect(this, &MainWindow::applyViewportBackground);
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
    // The dock's Graph tab draws the SAME panel the viewport's overlay does, so it
    // needs the same editor: the driver owns ctrl::GraphEditor and is created and
    // destroyed with the project, so the docked page is handed it here, from this one
    // call site, rather than reading it off the viewport.
    mTarget->setDriver(mDriverHolder->driver());

    // The driver is created here (and later destroyed with the project), which
    // is the first moment the tree selection can be handed to the graph.
    if (auto* drv = mDriverHolder->driver())
        drv->setGraphTargets(mTarget->objectTreeWidget().selectedNodes());

    // The viewport background follows the project that is now in front (its override,
    // or the theme colour when it has none) - a switch must not leave the previous
    // project's colour on screen.
    this->applyViewportBackground();
}

void MainWindow::onProjectTabChanged(core::Project& aProject) { resetProjectRefs(&aProject); }

void MainWindow::applyViewportBackground() {
    mMainDisplay->setViewportBackground(effectiveViewportBackground());
}

QColor MainWindow::effectiveViewportBackground() const {
    return (mCurrent && mCurrent->attribute().hasViewportBackground())
               ? mCurrent->attribute().viewportBackground()
               : mGUIResources.viewportBackground();
}

void MainWindow::onThemeUpdated(theme::Theme& aTheme) {
    this->applyViewportBackground();

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

    // The header scene re-reads the palette and reloads its tinted glyphs from the
    // generation in QmlTheme::iconUrl.
    if (mHeaderBar) mHeaderBar->refreshTheme();

    mDockPropertyWidget->setStyleSheet(aTheme.loadStylesheet("propertywidget.ssa"));
    if (mProperty) mProperty->refreshTheme();
    // The log's colours are per-run formats, not QSS: a stylesheet change cannot
    // reach them, so the view re-inks its own document in place from the palette
    // GUIResources just activated (text and selection survive).
    if (auto* logView = mViaPoint.logView()) logView->refreshTheme();
    mDockToolWidget->setStyleSheet(aTheme.loadStylesheet("toolwidget.ssa"));
    // The dock's QML scene re-reads the palette and re-loads its tinted glyphs (the icon
    // URL carries the tint generation), so a theme switch reaches the tools dock too.
    if (mTool) mTool->refreshTheme();

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
                const ctrl::KeyBinding keyBind = eventBinding(*aEvent);
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
                const ctrl::KeyBinding keyBind = eventBinding(*aEvent);
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
            mViaPoint.pushCommandLog(false, ret);
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

    if (mDockAnimationWidget->isHidden()) {
        mDockAnimationWidget->show();
    } else if (!mDockAnimationWidget->isHidden()) {
        mDockAnimationWidget->hide();
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
            mViaPoint.pushCommandLog(true, ret);
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
    bool liveLink = false;
    {
        QScopedPointer<NewProjectDialog> dialog(new NewProjectDialog(this));

        dialog->exec();
        if (dialog->result() != QDialog::Accepted) {
            return;
        }
        attribute = dialog->attribute();
        fileName = dialog->fileName();
        specifiesCanvasSize = dialog->specifiesCanvasSize();
        liveLink = dialog->liveLink();
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

        // A successful load can still have degraded layers (see
        // ImageFileLoader::warnings) or a skeleton past the deformable cap:
        // say so now, once, while the user is looking at the file they just opened.
        announceLoadWarnings(result);

        // The project is live now, so its assets are the ones to monitor. The
        // resource dialog was just handed the new project by resetProjectRefs,
        // and its tree already lists the asset the new project was created from.
        if (liveLink && mResourceDialog) {
            mResourceDialog->watchAllAssets();
        }
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

void MainWindow::announceLoadWarnings(const ctrl::System::LoadResult& aResult) {
    if (!aResult.project || aResult.message.isEmpty()) {
        return;
    }
    // The log keeps each line for later; the popup is what stops the silence.
    for (const QString& note : aResult.message) {
        mViaPoint.pushLog(note, ctrl::UILogType_Warn);
    }
    showInfoPopup(
        ctrl::System::LoadResult::warningTitle(),
        ctrl::System::LoadResult::warningSummary(),
        "Warn",
        aResult.message.join("\n")
    );
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

        announceLoadWarnings(result);
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

        announceLoadWarnings(result);
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

        announceLoadWarnings(result);

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
        // The dialog states "no override" as -1 (ExportParams::qualityOverride), and
        // any number at or above zero is a value the user named. The old test compared
        // against the codec's own sentinel - one below its span - because the box could
        // not hold anything else; the check is now simply "did the user name one".
        if (g.qualityOverride >= 0) {
            aVideo.encOptions.insert(knob, QString::number(g.qualityOverride));
        } else {
            ctrl::applyQualityIntent(codec, exParam.qualityBucket, aVideo.encOptions);
        }
    }
}

// A non-selectable group heading inside a QComboBox. Qt's own idiom for this is
// to clear the item's flags through the model.

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
bool exportProject(exportParam& exParam, core::Project* mCurrent, QWidget* widget, const std::vector<audioConfig>& validAudioStreams){
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
    //
    // The request is declared HERE, at function scope, and not inside the block
    // below: the fit path further down must plan from the SAME request the plain
    // path planned from, and it can only copy what it can see. A hand-built
    // second request is exactly how the two paths drifted apart.
    ctrl::ExportRequest request;
    if (!exParam.destinationId.isEmpty()) {
        const ctrl::ExportTarget target =
            ctrl::ExportTargets::find(ctrl::ExportTargets::load(), exParam.destinationId);
        if (target.id.isEmpty()) {
            QMessageBox::warning(widget, QObject::tr("Export Error"),
                                 QObject::tr("Unknown export destination: %1").arg(exParam.destinationId));
            return false;
        }
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
        // The stem for numbered frames is the name the USER TYPED ("one file per
        // frame, numbered from 0001, at the name the user typed" — the save
        // dialog's promise, probe_still's assertion), so a fitted set and an
        // unfitted one are identically named. Measured regression (2026-10-01):
        // the priority below was reversed — the PROJECT name won, and a sequence
        // came out as "nene0000.png" beside a status bar that said
        // probe_frames.png. It stayed invisible while every capped sequence rode
        // the fit path, which built its own request that never carried a stem and
        // fell back to the typed name; I43's opt-in cap gate moved sequences to
        // the direct path and the two writers' disagreement appeared. The fit
        // path now copies this request instead of building one, so the stem is
        // structural there too.
        request.imageStem = QFileInfo(g.osExportTarget).completeBaseName();
        if (request.imageStem.isEmpty()) {
            request.imageStem = QFileInfo(g.fileName).baseName();
        }
        imageStem = request.imageStem;
        // GIF parameters, so a fitted GIF is the same file a plain export writes.
        request.gifPalettePath = (g.useCustomPalette && g.palettePath.exists())
                                     ? g.palettePath.absolutePath()
                                     : QString();
        request.gifLoop = g.loop ? 0 : -1;
        // I44: the dialog's codec row travels here - the user's pick where they
        // made one, the plan's own resolution where they did not (empty means
        // "the destination's default"). planExport re-validates it against the
        // destination's accepted list either way.
        request.codec = exParam.videoParams.codec;
        // The GUI's transparency answer. THIS is the live export path (the view
        // model's own buildRequest() feeds the facts panel, not the export), so
        // the toggle has to land here or planExport resolves a `choice` row to
        // the request's `true` default over the user's answer. The plan then
        // applies the per-target rule (alpha required/forbidden override it) and
        // the export reads the RESOLVED value (`plan.alphaEnabled`) below.
        request.allowTransparency = g.allowTransparency;
        // The effective viewport colour, carried to the fit path too: its
        // re-encodes pad their own margins and need the same background.
        request.viewportBackground = g.viewportBackground;

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

    // The export's BACKGROUND. When the artifact is OPAQUE — the PLAN's resolved
    // transparency is off, so a destination that forbids alpha gets the colour
    // even with the box checked — the art composites over the viewport colour and
    // any pad margin is filled with it; when transparency is on the canvas stays
    // transparent, exactly as before. Resolved ONCE here, where the theme lives:
    // ctrl::Exporter and media:: receive a plain QColor (an invalid one means
    // "transparent") and read no theme.
    cparam.background =
        (havePlan ? !plan.alphaEnabled : !g.allowTransparency) ? g.viewportBackground : QColor();

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
    // I43 (decided 2026-09-30): the size cap is OPT-IN. This block used to run
    // whenever the DESTINATION declared caps.maxBytes — silently fitting every
    // export under a number the user never chose (the review's "Discord 20 MB
    // enforced"). It runs only when the USER set a cap (GeneralParams::
    // sizeCapBytes; "Auto" = 0 = off) and then fits under the USER's number.
    // The destination's own cap stays a stated fact (ExportFacts, the picker
    // badge). At the limit the fit refuses with a visible reason (fitFailed
    // below) — that part is unchanged, it is merely no longer silent.
    if (havePlan && g.sizeCapBytes > 0) {
        ctrl::ExportTarget fitTarget = plan.target;
        fitTarget.caps.maxBytes = g.sizeCapBytes;
        // The fit re-plans from the SAME request the plain path above just
        // planned from — a copy, not a second request built by hand. The
        // hand-built one carried six fields and dropped the rest, so the fit
        // planned with playhead 0, no audio and the default alpha state whatever
        // the plain path had sent: a capped Still destination re-planned from
        // frame 0 instead of the frame on screen, and a muted or alpha-off
        // export was fitted under different audio/alpha assumptions than the
        // plain path next to it was written with. Copying removes the drift by
        // construction, and a request field added later cannot miss the fit path
        // again.
        // Pins: the fields a hand-built copy dropped — playhead,
        // exportableAudioTracks, mute, audioChannels, audioSampleRate and
        // allowTransparency — must reach ctrl::exportWithFit, so a capped Still
        // writes the playhead frame and a capped sequence the same-named set a
        // plain export writes.
        ctrl::ExportRequest fitRequest = request;
        // Fit-specific: the two inputs the plan above RESOLVED, which the frames
        // the exporter is about to render are the result of. The raw request
        // values would make the fit's internal re-plan disagree with that render:
        // plan.selection is the selection after the row's own frame cap, and the
        // fit refuses when its plan's frame count differs from the frames
        // supplied (ctrl::exportWithFit's guard); plan.fps is the row-capped rate
        // the frames were rendered at.
        fitRequest.fps = cparam.fps;
        fitRequest.selection = cparam.selection;

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
            // Pins: the number is the cap the fit ENFORCES (fitTarget.caps.maxBytes
            // is the user's, set above), not the destination's stated one, so the
            // two differ exactly when this string would have lied.
            progress.setSection(QObject::tr("Fitting under %1's %2 KB limit...")
                                    .arg(plan.target.label)
                                    .arg(fitTarget.caps.maxBytes / 1024));
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
                // Pins: the cap named is the one enforced (the user's, via
                // fitTarget.caps.maxBytes above), not the destination's stated
                // one — the failure is against the enforced number, so the
                // message must quote the enforced number.
                QMessageBox::warning(widget, QObject::tr("Export Error"),
                                     QObject::tr("%1 caps the file at %2 KB and even the "
                                                 "smallest setting did not fit it (%3 KB).\n%4")
                                         .arg(plan.target.label)
                                         .arg(fitTarget.caps.maxBytes / 1024)
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
            // A sequence is a directory of numbered frames; the stem is the name
            // the user typed (see the imageStem computation above — 2026-10-01:
            // "the name comes from the project" was measured wrong and produced
            // nene0001.png files under a probe_frames.png status).
            cparam.path = g.exportDirectory.absolutePath();
            iparam.name = imageStem.isEmpty() ? QFileInfo(g.osExportTarget).completeBaseName()
                                             : imageStem;
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
        // The RESOLVED alpha state: a destination's own rule decides (a row that
        // forbids transparency writes none even with the box checked), and manual
        // mode is the toggle. Not the raw checkbox — that is the answer the plan
        // was given, not the one it resolved.
        gparam.transparent = havePlan ? plan.alphaEnabled : g.allowTransparency;
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

// Write the dialog's per-run inputs (frame ranges, size, fps) back to QSettings.
//
// They are per-run inputs, not project properties: changing one checkbox must not
// mean re-selecting the range, the size and the rate. Read back on the next open BY
// THE MODEL, so a destination with a Free size rule reads the size the user last
// chose.
void MainWindow::rememberExportInputs(const ExportViewModel& aModel) {
    QSettings settings;
    const QString key = exportInputsKey();
    // JSON, not a QVariantList of QVariantList: QSettings flattens nested lists of
    // simple types into one comma-separated string ("0, 3"), so reading the ranges
    // back as pairs found single values, skipped every one of them, and restored
    // nothing while the values sat right there in the file.
    QJsonArray savedRanges;
    for (const ctrl::FrameRange& range : aModel.ranges()->ranges()) {
        QJsonArray pair;
        pair << range.first << range.last;
        savedRanges << pair;
    }
    settings.setValue(key + QStringLiteral("/ranges"),
                      QString::fromUtf8(QJsonDocument(savedRanges).toJson(QJsonDocument::Compact)));
    settings.setValue(key + QStringLiteral("/width"), aModel.width());
    settings.setValue(key + QStringLiteral("/height"), aModel.height());
    settings.setValue(key + QStringLiteral("/fps"), aModel.fps());
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
    // This clause is the GUIDANCE, stated — I43: "a fact the user may apply". It
    // reports where the file landed against the destination's own limit and warns
    // of the platform's rejection; it is NOT enforcement. Enforcement (fitting)
    // happens only when the user sets a size cap (GeneralParams::sizeCapBytes).
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
    // The export's OPAQUE background (transparency off) is what the viewport is
    // showing right now. Resolved here, where the theme lives; exportProject turns
    // it into the render clear and the pad fill.
    mLastExport->generalParams.viewportBackground = effectiveViewportBackground();
    const bool wrote = exportProject(*mLastExport, mCurrent, this, validAudioStreams);
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
    genParam.sizeCapBytes = 0; // I43: this path never caps (no dialog chose one)
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
    if (isExportParamValid(exParam, this, &validAudioStreams)) {
        exParam->playhead = mTarget->currentFrame().get();
        exParam->generalParams.viewportBackground = effectiveViewportBackground();
        const bool wrote = exportProject(*exParam, mCurrent, this, validAudioStreams);
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
    if (exporting) {
        if (mExportDialog) {
            mExportDialog->showNormal();
            mExportDialog->raise();
        }
        return;
    }

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
//
// The dialog is gui::ExportDialog: a native framed QDialog hosting a Qt Quick scene,
// whose every rule lives in gui::ExportViewModel. Nothing below reaches into a
// widget: the plan, the capabilities and the facts are the model's, and this
// function only decides what the project knows (its size, its frames, its audio) and
// what to do with the answer.
void MainWindow::openExportDialog(const QString& aDestinationId) {
    if (!mCurrent) { return; }
    if (exporting) {
        if (mExportDialog) {
            mExportDialog->showNormal();
            mExportDialog->raise();
        }
        return;
    }
    // Stop animation and main display rendering
    EventSuspender suspender(*mMainDisplay, *mTarget);
    QGuiApplication::setOverrideCursor(QCursor(Qt::BusyCursor));
    QSettings settings;
    exporting = true;

    // ---- what the project knows -------------------------------------------
    ExportViewModel::Init init;
    init.nativeSize = mCurrent->attribute().imageSize();
    init.nativeFps = mCurrent->attribute().fps();
    init.projectFirstFrame = mCurrent->attribute().frameRange().min();
    init.projectLastFrame = mCurrent->attribute().frameRange().max();
    init.playhead = mTarget->currentFrame().get();
    init.exportableAudioTracks = 0;
    if (mCurrent->pConf) {
        for (const audioConfig& ac : *mCurrent->pConf) {
            if (ac.exportEnable) ++init.exportableAudioTracks;
        }
    }
    // The remembered per-run inputs, so reopening the dialog to change one thing does
    // not mean re-selecting the range, the size and the rate. Clamped to THIS
    // project's own extent: a remembered range can outlive the animation it was
    // chosen for.
    {
        const QString key = exportInputsKey();
        const QJsonArray ranges =
            QJsonDocument::fromJson(settings.value(key + QStringLiteral("/ranges")).toString().toUtf8())
                .array();
        for (const QJsonValue& v : ranges) {
            const QJsonArray pair = v.toArray();
            if (pair.size() != 2) continue;
            init.ranges.append(ctrl::FrameRange{
                std::clamp(pair.at(0).toInt(), init.projectFirstFrame, init.projectLastFrame),
                std::clamp(pair.at(1).toInt(), init.projectFirstFrame, init.projectLastFrame)});
        }
        const int w = settings.value(key + QStringLiteral("/width"), 0).toInt();
        const int h = settings.value(key + QStringLiteral("/height"), 0).toInt();
        if (w > 0 && h > 0) {
            init.width = w;
            init.height = h;
        }
        init.fps = settings.value(key + QStringLiteral("/fps"), 0).toInt();
    }
    // The remembered preferences, exactly the ones the widget form stored. The
    // palette path travels WITH its preference, because a remembered "use a custom
    // palette" whose file is gone can only ever produce a blocking validation error
    // ("Selected palette does not exist") with no way to see which file it meant. A
    // FILE, checked as a file: QDir() is the current directory and "exists", so
    // testing a QDir made an unset palette look like a valid one.
    {
        const QString savedPalette = settings.value(QStringLiteral("export_palette_path")).toString();
        const QFileInfo paletteInfo(savedPalette);
        const bool paletteRestored =
            !savedPalette.isEmpty() && paletteInfo.isFile() && paletteInfo.isReadable();
        init.palettePath = paletteRestored ? paletteInfo.absoluteFilePath() : QString();
        init.customPalette = settings.value(QStringLiteral("export_custom_palette"), false).toBool()
                             && paletteRestored;
        // One question ("does editing Width move Height?") instead of three legacy
        // radio buttons: a stored 0 (one-to-one) means "keep", which is what it did in
        // every export.
        init.keepAspectRatio = settings.value(QStringLiteral("export_aspect_ratio"), 1).toInt() != 2;
        // Off for a fresh profile (ruled 2026-10-03: the toggle lives on the regular
        // surface because turning it on pushes the encoder onto a codec that keeps
        // alpha but loses quality). A value saved by an earlier run is read here as
        // the dialog's starting point; the target's own alpha rule overrides it (see
        // item 42 of docs/export-dialog-behavior.md).
        init.transparency = settings.value(QStringLiteral("export_allow_transparency"), false).toBool();
        init.loop = settings.value(QStringLiteral("export_loop_anim"), false).toBool();
        init.audio = settings.value(QStringLiteral("export_allow_audio"), false).toBool();
        // The quality the user last chose, not a silent Balanced: the quick-export
        // menu reads the same key, so the two cannot disagree about what "Best" means.
        const int savedQuality = settings.value(QStringLiteral("export_quality_bucket"), 1).toInt();
        init.qualityIndex = (savedQuality >= 0 && savedQuality <= 3) ? savedQuality : 1;
    }
    init.destinationId = aDestinationId;

    // ---- the form ----------------------------------------------------------
    // Destinations come from the data table - the same rows the exporter plans from -
    // so what the dialog STATES is what the export will actually do.
    ExportDialog dialog(init, mGUIResources, this);
    mExportDialog = &dialog;
    QGuiApplication::restoreOverrideCursor();
    dialog.exec();
    exporting = false;

    // ---- what the user answered -------------------------------------------
    ExportViewModel* model = dialog.viewModel();
    const ExportViewModel::ReadBack back = model->readBack();
    // The destination the user left the dialog with: they may have changed it inside,
    // and THAT is what the export is for.
    settings.setValue(QStringLiteral("export_destination"), back.destinationId);
    settings.setValue(QStringLiteral("export_quality_bucket"), model->qualityIndex());
    {
        // The per-run inputs, so the next open starts where this one left off.
        rememberExportInputs(*model);
        // The remembered preferences, written straight from the read-back: the
        // QVariant dance updateSettings() did existed to compare against the widgets'
        // current values, and there are no widgets any more.
        settings.setValue(QStringLiteral("export_aspect_ratio"), back.keepAspectRatio ? 1 : 2);
        settings.setValue(QStringLiteral("export_allow_transparency"), back.allowTransparency);
        settings.setValue(QStringLiteral("export_custom_palette"), back.useCustomPalette);
        settings.setValue(QStringLiteral("export_palette_path"), back.palettePath);
        settings.setValue(QStringLiteral("export_loop_anim"), back.loop);
        settings.setValue(QStringLiteral("export_allow_audio"), back.exportWithAudio);
        settings.sync();
    }
    // The model carries the outcome, so the two cannot disagree; the dialog result is
    // the same answer and is checked as well because the window can be closed.
    if (model->operationCancelled() || dialog.result() != QDialog::Accepted) {
        mExportDialog = nullptr;
        return;
    }

    // Get file name and folder for export. ONE filter: the artifact the user already
    // chose in the dialog's Format row.
    //
    // This used to be thirteen video filters and nine image filters, and the EXPORT
    // FORMAT was then read back out of whichever filter was selected - so the format
    // was chosen in the OS file dialog by a name like "Flash Video", "MPEG-2" or "X11
    // PixelMap" that nothing downstream could produce. The artifact is decided in one
    // place now; the file dialog only asks where to put it and what to call it.
    const bool sequenceExport = back.sequence;
    const QString artifactSuffix = back.artifactSuffix;
    QFileDialog fileDiag;
    fileDiag.setAcceptMode(QFileDialog::AcceptMode::AcceptSave);
    fileDiag.setFileMode(QFileDialog::AnyFile);
    // Open where the last export went, with its name preselected. Repeating an export
    // to the same place is the common case, and this dialog used to open with no
    // location at all.
    {
        QSettings s2;
        const QString lastDir = s2.value(QStringLiteral("export_last_dir")).toString();
        const QString lastName = s2.value(QStringLiteral("export_last_name")).toString();
        if (!lastDir.isEmpty() && QFileInfo(lastDir).isDir()) {
            fileDiag.setDirectory(lastDir);
        }
        // ALWAYS pre-fill a name, and take its extension from the artifact.
        //
        // Two things went wrong without this. The remembered name could contradict the
        // artifact (APNG after a WEBM export pre-filled "test.webm", and the suffix is
        // only appended when a name has none, so APNG bytes were written into a .webm
        // file) - that is why the base name is kept and the extension replaced. And
        // with nothing to remember, the dialog opened with an empty name and focus in
        // its SEARCH box.
        const QString rememberedBase = QFileInfo(lastName).completeBaseName();
        const QString fallbackBase =
            mCurrent ? QFileInfo(mCurrent->fileName()).completeBaseName() : QString();
        const QString base = !rememberedBase.isEmpty()
                                 ? rememberedBase
                                 : (fallbackBase.isEmpty() ? QStringLiteral("Export") : fallbackBase);
        fileDiag.selectFile(base + QLatin1Char('.') + artifactSuffix);
    }
    const QString formatLabel = model->formatDisplayLabel();
    fileDiag.setNameFilters({formatLabel + " (*." + artifactSuffix + ")"});
    fileDiag.selectNameFilter(formatLabel + " (*." + artifactSuffix + ")");

    // Generate parameters
    GeneralParams genParam;
    genParam.fileName = mCurrent->fileName().isEmpty() ? "New Export" : QString(mCurrent->fileName());
    genParam.nativeWidth = mCurrent->attribute().imageSize().width();
    genParam.nativeHeight = mCurrent->attribute().imageSize().height();
    genParam.exportWidth = back.width;
    genParam.exportHeight = back.height;
    genParam.aspectRatio = back.keepAspectRatio ? keep : custom;
    genParam.nativeFPS = mCurrent->attribute().fps();
    genParam.fps = back.fps;
    genParam.bitrate = back.bitrate;
    genParam.qualityOverride = back.qualityOverride;
    genParam.imageExportQuality = back.imageExportQuality;
    genParam.sizeCapBytes = back.sizeCapBytes; // I43: 0 = Auto = no cap
    genParam.allowTransparency = back.allowTransparency;
    genParam.loop = back.loop;
    genParam.exportWithAudio = back.exportWithAudio;
    genParam.useCustomPalette = back.useCustomPalette;
    genParam.palettePath = QDir(back.palettePath);
    for (const ctrl::FrameRange& range : back.ranges) {
        frameExportRange fer;
        fer.firstFrame = range.first;
        fer.lastFrame = range.last;
        genParam.exportRange.append(fer);
    }
    auto* exParam = new exportParam();
    exParam->generalParams = genParam;
    exParam->destinationId = back.destinationId;
    exParam->qualityBucket = back.qualityBucket;
    // The artifact decides the type: a sequence writes numbered stills, anything else
    // writes one video file. This used to be an independent combo, which let the two
    // disagree ("Animation" plus a .png filter) and left the format to be
    // reconstructed from the disagreement.
    exParam->exportType = sequenceExport ? exportTarget::image : exportTarget::video;
    if (exParam->exportType == exportTarget::video) {
        exParam->videoParams.container = artifactSuffix;
        // Empty means "the container's probed default", which is the only honest
        // answer when the codec was not a choice (one candidate, or a GIF).
        exParam->videoParams.codec = back.codec;
    } else {
        exParam->imageParams.suffix = artifactSuffix;
    }

    // Get file via OS diag
    fileDiag.setViewMode(QFileDialog::Detail);
    QString selectedFile;
    if (fileDiag.exec()) {
        selectedFile = fileDiag.selectedFiles().at(0);
    } else {
        delete exParam;
        mExportDialog = nullptr;
        return;
    }
    if (QFileInfo(selectedFile).suffix().isEmpty()) {
        selectedFile.append(QLatin1Char('.') + artifactSuffix);
    }
    qDebug() << "Exporting animation to" << selectedFile << "as" << artifactSuffix;
    genParam.exportName = QFileInfo(selectedFile).fileName();
    genParam.exportDirectory = QFileInfo(selectedFile).absoluteDir();
    genParam.exportFileName = QDir(selectedFile);
    genParam.osExportTarget = selectedFile;
    exParam->generalParams = genParam;

    // Main export function. exportProject() and isExportParamValid() still take a
    // QDialog* for their own message boxes; the form itself is done with, so the file
    // dialog's parent stands in for it.
    std::vector<audioConfig> validAudioStreams =
        AudioPlaybackWidget::getValidAudioStreams(*mCurrent->pConf);
    if (isExportParamValid(exParam, &fileDiag, &validAudioStreams)) {
        exParam->playhead = mTarget->currentFrame().get();
        exParam->generalParams.viewportBackground = effectiveViewportBackground();
        const bool wrote = exportProject(*exParam, mCurrent, &fileDiag, validAudioStreams);
        reportExportOutcome(*exParam, wrote);
    }
    mExportDialog = nullptr;
}


void MainWindow::onMoveFrameTriggered(bool moveRight) {
    int frameDest = moveRight? 1: -1;
    core::Frame dest = mTarget->currentFrame();
    dest.add(frameDest);
    mTarget->timeLineWidget().setFrame(dest);
}

/// The panel a tab title names, if any.
///
/// Qt's dock tab bar owns no mapping back to the docks, and the titles are the
/// panel names, so matching by title is the available answer. The three titles
/// are distinct, and a dock's title is set once at construction.
QDockWidget* MainWindow::dockForTabTitle(const QString& aTitle) const {
    for (QDockWidget* dock : {mDockToolWidget, mDockPropertyWidget, mDockAnimationWidget}) {
        if (dock && dock->windowTitle() == aTitle) return dock;
    }
    return nullptr;
}

/// Adopts Qt's own bar for a tab group, once.
///
/// A group's bar is the group's title row - that is Qt's model - and taking it
/// out (to give the room to each panel's own title bar) leaves the layout
/// reserving its height regardless (measured: a 32px band above the group). So
/// one QSS rule dresses both this bar and a lone panel's title row (both are
/// QTabBars named dockTabBar), and one filter gives both the same gestures.
void MainWindow::updateDockTabMenus() {
    QWidget* central = centralWidget();
    for (QTabBar* bar : findChildren<QTabBar*>()) {
        if (bar->count() < 2)
            continue;
        // A lone panel's title row is one of ours: it already carries its name
        // and its filter, and it is not a slot's rail. Adopting it again would
        // install a second filter and make the same press start two gestures.
        if (qobject_cast<DockTabBar*>(bar) != nullptr)
            continue;
        // The project tab bar is a QTabBar too, and it lives inside the canvas.
        // Without this a project saved as "Animation.anie" would be adopted as a
        // panel rail and re-dressed with the dock rule.
        if (central && central->isAncestorOf(bar))
            continue;

        // A rail belongs to the slot rather than to any panel in it, so it says
        // which area it is the chrome of - the slot model reads this to include
        // the rail in the slot's extent (without it the seam stopped at the tabs).
        // Refreshed on every pass, including for bars adopted earlier: moving a
        // whole tab group to another area keeps the bar, so the area it named
        // would otherwise stay the old one and the new slot's seam would stop at
        // its tabs.
        QDockWidget* first = dockForTabTitle(bar->tabText(0));
        if (bar->property("aeDockTabBar").toBool()) {
            bar->setProperty("aeDockArea", first ? int(this->dockWidgetArea(first)) : -1);
            continue;
        }

        bool allPanels = true;
        for (int i = 0; i < bar->count(); ++i) {
            if (!dockForTabTitle(bar->tabText(i))) {
                allPanels = false;
                break;
            }
        }
        if (!allPanels) continue;

        bar->setProperty("aeDockTabBar", true);
        bar->setObjectName(QStringLiteral("dockTabBar"));
        bar->setProperty("aeDockArea", first ? int(this->dockWidgetArea(first)) : -1);
        // Naming it is not enough: a style sheet rule is matched when the widget
        // is polished, so a bar that Qt creates mid-session (a group made by
        // dragging a panel onto another) kept Qt's own look while the same bar at
        // startup was dressed - measured, the group created by a drop showed the
        // generic tab rules until the next theme change.
        bar->style()->unpolish(bar);
        bar->style()->polish(bar);
        bar->update();
        new DockTabBarFilter(bar, this);
    }
}

bool MainWindow::eventFilter(QObject* aObject, QEvent* aEvent) {
    // The dock tab bars are created and destroyed by the layout as panels are
    // tabified and split apart again, so adoption is driven from the layout
    // events rather than from the one place that tabifies today.
    if (aObject == this) {
        switch (aEvent->type()) {
        case QEvent::LayoutRequest:
        case QEvent::ChildAdded:
        case QEvent::ChildRemoved:
            scheduleDockChromeSync();
            break;
        default:
            break;
        }
    }
    return QMainWindow::eventFilter(aObject, aEvent);
}

/// One deferred chrome pass at a time. The pass re-polishes Qt's tab bars and
/// changes title-row heights, both of which can emit the layout events that would
/// ask for another pass; coalescing also keeps a drop's own signals from running
/// it while the drag is still in flight.
void MainWindow::scheduleDockChromeSync() {
    if (mDockSyncQueued) return;
    mDockSyncQueued = true;
    QTimer::singleShot(0, this, [this]() {
        mDockSyncQueued = false;
        updateDockTabMenus();
        updateTitleRows();
        updateLayoutButtons();
    });
}

void MainWindow::clearDockOpeningFloors() {
    if (!mDockOpeningFloors) return;
    mDockOpeningFloors = false;
    for (QDockWidget* dock : {mDockToolWidget, mDockPropertyWidget, mDockAnimationWidget}) {
        if (dock) applyPanelMetrics(dock);
    }
}

/// The size range a panel is held to is the *area's*, not its own: a side area
/// is a width-constrained full-height column, a top/bottom area is a
/// height-constrained full-width row, and a floating panel is a window that the
/// compositor sizes. Applying both ranges everywhere is what gave the bottom
/// panel a 640px maximum width.
void MainWindow::applyPanelMetrics(QDockWidget* aDock) {
    if (!aDock) return;
    const Qt::DockWidgetArea area = this->dockWidgetArea(aDock);
    const bool floating = aDock->isFloating();
    const bool side = (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea);

    // clear first: the panel may have just left the other kind of area
    aDock->setMinimumSize(0, 0);
    aDock->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    if (floating) return;

    // A slot has one size range, and the panels in it must not be able to widen
    // that range through their contents. Setting the dock's minimum is not enough:
    // a widget's effective minimum is max(its explicit minimum, its
    // minimumSizeHint) - qSmartMinSize - so an explicit 200 only raises a floor the
    // content can still set. Measured, one panel per area: the animation panel's
    // content reported a 278px minimum width, so any slot holding that panel could
    // never be dragged below 278 however the slot's own range was set, which is
    // exactly "the slot min/max seems to be based on the tabs in it".
    //
    // Ignoring the content's size hint is Qt's way of saying the hint must not
    // constrain the sizes at all: a docked panel is a tiled pane, its range comes
    // from the area (below) and the layout, and its content is expected to fit
    // itself or be clipped. Leaving one axis to the content is what lets a panel
    // drag a floor into the window's own minimum - an 800px-wide timeline would
    // stop the window from ever being narrowed.
    if (QWidget* content = aDock->widget())
        content->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    if (side) {
        aDock->setMinimumWidth(dockMetrics::kMinWidth);
        aDock->setMaximumWidth(dockMetrics::kMaxWidth);
    } else if (area != Qt::NoDockWidgetArea) {
        aDock->setMinimumHeight(dockMetrics::kMinHeight);
        aDock->setMaximumHeight(dockMetrics::kMaxHeight);
    }
}

/// Performs the drop the overlay resolved. The overlay and this function are the
/// two halves of one decision - what the preview drew is what this does - which
/// is why the target is passed through rather than recomputed.
void MainWindow::applyDockDrop(QDockWidget* aPanel, const DockDropTarget& aTarget) {
    if (!aPanel || aTarget.kind == DockDropKind::None) return;

    // the panel stops being a window before it is re-docked, so the floating
    // chrome (translucent attributes, edge bands) is torn down first
    if (auto* panel = qobject_cast<DockPanel*>(aPanel)) panel->setPanelFloating(false);
    aPanel->show();

    placeDock(aPanel, aTarget);
    applyPanelMetrics(aPanel);
    scheduleDockChromeSync();
}

/// The placement alone: which area, split or tab a target asks for. Shared by
/// every drop kind; the preview's rectangle is derived from the same slot model
/// and area rule (DockDropOverlay::targetRect), so the two describe one decision.
void MainWindow::placeDock(QDockWidget* aPanel, const DockDropTarget& aTarget) {
    if (!aPanel || aTarget.kind == DockDropKind::None) return;

    switch (aTarget.kind) {
    case DockDropKind::Tabify: {
        if (!aTarget.group) return;
        // in the target area first, so this arranges two docks of one area
        // rather than pulling the group towards the panel's remembered area
        addDockWidget(aTarget.area, aPanel);
        tabifyDockWidget(aTarget.group, aPanel);
        aPanel->raise();
        break;
    }
    case DockDropKind::Split: {
        if (!aTarget.group) return;
        // Both docks have to be in the target's area before the split:
        // splitDockWidget arranges two docks *within one area*, so a panel that
        // has just come back from floating (and therefore landed in whatever
        // area it remembered) would be split over there instead of where the
        // drop pointed. Docking it into the target area first, then splitting,
        // puts the arrangement where the preview drew it.
        addDockWidget(aTarget.area, aPanel);
        // Qt's orientation is about where the *second* dock goes: Horizontal
        // puts it to the right of the first, Vertical below it. The preview
        // drew the half the panel would take, so the orientation and the order
        // follow from which half that was.
        const QRect group = aTarget.group->geometry();
        const bool leftHalf = aTarget.rect.center().x() < group.center().x();
        const bool topHalf = aTarget.rect.center().y() < group.center().y();
        const bool horizontal = qAbs(aTarget.rect.center().x() - group.center().x())
                              > qAbs(aTarget.rect.center().y() - group.center().y());
        if (horizontal) {
            if (leftHalf) splitDockWidget(aPanel, aTarget.group, Qt::Horizontal);
            else splitDockWidget(aTarget.group, aPanel, Qt::Horizontal);
        } else {
            if (topHalf) splitDockWidget(aPanel, aTarget.group, Qt::Vertical);
            else splitDockWidget(aTarget.group, aPanel, Qt::Vertical);
        }
        break;
    }
    case DockDropKind::Area: {
        // A slice of the target area, on the canvas side of whatever is already
        // there. Plain addDockWidget would append the panel to the end of that
        // area's stack instead - measured: a canvas-left drop with the sidebar
        // occupied landed as a 153px strip at the bottom of the sidebar, while
        // the preview had drawn a full-height column beside the canvas. The
        // split is what puts the panel where the preview drew it, and the
        // preview is computed from this same rule (see DockDropOverlay).
        QDockWidget* anchor = innermostDock(aTarget.area, aPanel);
        if (!anchor) {
            addDockWidget(aTarget.area, aPanel);
        } else {
            // The panel has to be in the target area before the split, for the
            // same reason as the Split case: splitDockWidget arranges two docks
            // within one area, and a panel that has just come back from floating
            // lands in the area it remembers - measured: a canvas-bottom drop
            // over the animation panel moved the *animation panel* into the right
            // column instead.
            addDockWidget(aTarget.area, aPanel);
            if (aTarget.area == Qt::LeftDockWidgetArea) {
                splitDockWidget(anchor, aPanel, Qt::Horizontal);   // panel to its right
            } else if (aTarget.area == Qt::RightDockWidgetArea) {
                splitDockWidget(aPanel, anchor, Qt::Horizontal);   // panel to its left
            } else if (aTarget.area == Qt::TopDockWidgetArea) {
                splitDockWidget(anchor, aPanel, Qt::Vertical);     // panel below it
            } else {
                splitDockWidget(aPanel, anchor, Qt::Vertical);     // panel above it
            }
        }
        break;
    }
    default:
        break;
    }
}

/// The dock in an area that sits closest to the central widget: the one a new
/// panel has to be split against to land between it and the canvas. This is the
/// anchor both the drop and the preview work from.
QDockWidget* MainWindow::innermostDock(Qt::DockWidgetArea aArea, QDockWidget* aSkip) const {
    QDockWidget* best = nullptr;
    for (QDockWidget* dock : findChildren<QDockWidget*>()) {
        if (dock == aSkip || dock->property("aeDockPreview").toBool()) continue;
        if (!dock->isVisible() || dock->isFloating()) continue;
        if (this->dockWidgetArea(dock) != aArea) continue;
        if (!best) {
            best = dock;
            continue;
        }
        switch (aArea) {
        case Qt::LeftDockWidgetArea:  if (dock->x() > best->x()) best = dock; break;
        case Qt::RightDockWidgetArea: if (dock->x() < best->x()) best = dock; break;
        case Qt::TopDockWidgetArea:   if (dock->y() > best->y()) best = dock; break;
        default:                      if (dock->y() < best->y()) best = dock; break;
        }
    }
    return best;
}

/// Shows each panel's own title row only while that panel is alone in its area.
///
/// A grouped panel's titles are the group's tab bar - that is Qt's model, and it
/// gives a tabbed dock's title bar space as well if it is left visible, which is
/// the two rows a group used to show. Both rows are the same widget class dressed
/// by the same rule (DockTabBar), so which one draws is a question of *where*,
/// not of what: one tab when alone, the group's tabs when stacked.
void MainWindow::updateTitleRows() {
    for (QDockWidget* dock : {mDockToolWidget, mDockPropertyWidget, mDockAnimationWidget}) {
        QWidget* row = dock ? dock->titleBarWidget() : nullptr;
        if (!row) continue;
        auto* panel = qobject_cast<DockPanel*>(dock);

        // A floating panel is its own window and always shows its own row. A docked
        // panel shows it unless it shares a *tab group*, because then the group's
        // rail holds the titles. Sharing an area is not sharing a group: two panels
        // split above each other in one area have no rail at all, so asking the slot
        // (which is one per area) hid both of their rows and left the area with no
        // titles anywhere - measured, docking Properties above the timeline made
        // "Properties" and "Animation" disappear, since the bottom area then held
        // two panels and neither was tabbed.
        const bool sharedGroup = panel && panel->isTabGrouped();
        const bool ownRow = dock->isFloating() || !sharedGroup;

        row->setVisible(ownRow);
        if (ownRow) {
            row->setMinimumHeight(0);
            row->setMaximumHeight(QWIDGETSIZE_MAX);
        } else {
            // Not merely hidden: a hidden title row still reserves its height
            // inside the dock, which is the 32px band that appeared under a rail
            // (measured: titleBar 256x32 visible=0, widget top=32) and never under
            // a lone panel, where that row *is* the title.
            row->setFixedHeight(0);
        }
    }
}

/// Synchronises the slot buttons with the layout: enabled while the slot holds
/// anything, lit while any panel in it is showing. Driven from the layout events
/// rather than from each dock's visibility signal, because what the button
/// describes is the *slot*, which no single panel owns.
void MainWindow::updateLayoutButtons() {
    if (!mHeaderBar)
        return;
    // The Panels rows describe the same docks as the slot buttons, but one dock at a
    // time: a panel hidden inside a visible slot leaves the slot lit and its own row
    // unchecked.
    for (const PanelToggle& entry : mPanelToggles)
        entry.command->setChecked(entry.dock && entry.dock->isVisible());
    for (const LayoutSlot& entry : mLayoutSlots) {
        QVector<QDockWidget*> inSlot;
        bool anyVisible = false;
        for (QDockWidget* dock : {mDockToolWidget, mDockPropertyWidget, mDockAnimationWidget}) {
            if (!dock || dockWidgetArea(dock) != entry.area) continue;
            inSlot.push_back(dock);
            anyVisible = anyVisible || dock->isVisible();
        }
        // An icon-only button has no text for assistive technology to read, so
        // the slot's name goes in the accessible name as well as the tooltip. The
        // tooltip is the state-sensitive half: an empty slot explains why the button
        // is disabled instead of repeating "Show or hide".
        mHeaderBar->dockSlots()->setSlotState(
            entry.area,
            anyVisible,
            !inSlot.isEmpty(),
            tr("Show or hide the %1").arg(entry.name),
            inSlot.isEmpty() ? tr("Nothing is docked on the %1").arg(entry.name)
                             : tr("Show or hide the %1").arg(entry.name));
    }
}

/// The slot button's request: whatever is in the slot, shown or hidden, one button
/// and one slot. The checked state is not assumed here; updateLayoutButtons reads it
/// back from the docks after the visibility change.
void MainWindow::onDockSlotToggleRequested(int aArea) {
    const auto area = Qt::DockWidgetArea(aArea);
    QVector<QDockWidget*> inSlot;
    bool anyVisible = false;
    for (QDockWidget* dock : {mDockToolWidget, mDockPropertyWidget, mDockAnimationWidget}) {
        if (!dock || dockWidgetArea(dock) != area) continue;
        inSlot.push_back(dock);
        anyVisible = anyVisible || dock->isVisible();
    }
    for (QDockWidget* dock : inSlot)
        dock->setVisible(!anyVisible);
    updateLayoutButtons();
}

} // namespace gui
