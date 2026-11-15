#include <functional>
#include <QInputMethodEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QOpenGLFunctions>
#include <QGuiApplication>
#include "XC.h"
#include "util/Finally.h"
#include "gl/Util.h"
#include "gl/Framebuffer.h"
#include "gl/Texture.h"
#include "core/ClippingFrame.h"
#include "gui/MainDisplayWidget.h"
#include "gui/GraphChannelPopup.h"
#include "gui/ProjectHook.h"
#include "gui/TimeLineWidget.h"
#include "gui/TimeLineEditorWidget.h"
#include "gui/ViaPoint.h"
#include "gui/ProjectTabBar.h"
#include "gui/KeyCommandMap.h"
#include "gui/MouseSetting.h"
#ifdef AE_ENABLE_IMGUI
// The dock's Graph page: syncGraphPanelSharedState (and every list action that changes
// what it draws) asks it for a repaint, because the panel is a WIDGET of its own and
// the viewport's updateRender() no longer touches it.
#include "gui/GraphPanelWidget.h"
#endif

namespace gui {

namespace {
// The panel's editor, or null when the driver has none (before the driver is set, or
// after it is gone). The panel itself is DRAWN elsewhere (gui::GraphPanelWidget, the
// dock's Graph tab); this widget keeps the editor's applications-side connections:
// the CHANNEL LIST (showGraphChannelMenu) and the KEY MENU (showGraphKeyMenu) are
// opened from here, and the panel's accessibility items reach them through this class
// too. Both read the editor HERE, at the moment they fire, rather than capturing one:
// a list can be open across a project change. Independent of the ImGui build flag,
// like the two panel questions below: the editor is the driver's either way.
ctrl::GraphEditor* graphEditorOrNull(ctrl::Driver* aDriver) {
    return aDriver ? aDriver->graphEditor() : nullptr;
}

// Whether a row's node is still one the live editor knows: the list's rows are built
// from exactly that set (GraphPanelModel.cpp's buildChannels walks aEditor.targets()),
// so a node the document has dropped since the list was opened - or one a project
// change took out of the editor's targets - is no longer in it. Only POINTERS are
// compared, never dereferenced, so a stale node cannot be touched by the test itself.
bool channelRowNodeAlive(const ctrl::GraphEditor* aEditor, core::ObjectNode* aNode) {
    return aEditor && aNode && aEditor->targets().contains(aNode);
}
}

//-------------------------------------------------------------------------------------------------
MainDisplayWidget::MainDisplayWidget(ViaPoint& aViaPoint, QWidget* aParent):
    QOpenGLWidget(aParent),
    mViaPoint(aViaPoint),
    mGLDeviceInfo(),
    mProject(),
    mGLRoot(),
    mGLContextAccessor(this),
    mDefaultVAO(),
    mFramebuffer(),
    mClippingFrame(),
    mDestinationTexturizer(),
    mTextureDrawer(),
    mPainterHandle(),
    mRenderingLock(),
    mRenderInfo(),
    mAbstractCursor(),
    mDriver(),
    mProjectTabBar(),
    mChannelPopup(nullptr),
    mTimeLine(),
    mTimeLineRangeSlot(),
    mViewportBackground(),
    mUsingTablet(false),
    mViewSetting(),
    mCanvasMover(),
    mMovingCanvasByTool(false),
    mMovingCanvasByKey(false),
    mMovingCanvasByMiddleMouseButton(false),
    mDevicePixelRatio(1.0) {
    this->setObjectName(QStringLiteral("MainDisplayWidget"));
    this->setMouseTracking(true);
    this->setAutoFillBackground(false); // avoid autofill on QPainter::begin()

    // key binding
    if (mViaPoint.keyCommandMap()) {
        // move canvas
        {
            auto key = mViaPoint.keyCommandMap()->get("MoveCanvas");
            if (key) {
                key->addListener(
                    [=]() {
                        mAbstractCursor.suspendEvent([=]() { updateCursor(); });
                        mMovingCanvasByKey = true;
                        applyCanvasMoverDragMode();
                    },
                    [=]() {
                        mMovingCanvasByKey = false;
                        applyCanvasMoverDragMode();
                        mAbstractCursor.resumeEvent();
                    }
                );
            }
        }

        // rotate canvas
        {
            auto key = mViaPoint.keyCommandMap()->get("RotateCanvas");
            if (key) {
                key->addListener(
                    [=]() {
                        mAbstractCursor.suspendEvent([=]() { updateCursor(); });
                        mCanvasMover.setDragAndRotate(true);
                    },
                    [=]() {
                        mCanvasMover.setDragAndRotate(false);
                        mAbstractCursor.resumeEvent();
                    }
                );
            }
        }

        // rotate canvas clockwise
        {
            auto key = mViaPoint.keyCommandMap()->get("RotateCanvas15Clockwise");
            if (key) {

                key->addListener([=]() {
                    mCanvasMover.rotate(qDegreesToRadians(15.0f));
                    updateRender();
                });
            }
        }

        // rotate canvas anti-clockwise
        {
            auto key = mViaPoint.keyCommandMap()->get("RotateCanvas15AntiClockwise");
            if (key) {

                key->addListener([=]() {
                    mCanvasMover.rotate(qDegreesToRadians(-15.0f));
                    updateRender();
                });
            }
        }

        // reset canvas angle
        {
            auto key = mViaPoint.keyCommandMap()->get("ResetCanvasAngle");
            if (key) {
                key->addListener([=]() {
                    mViaPoint.mainViewSetting().resetRotateView = true;
                    onViewSettingChanged(mViaPoint.mainViewSetting());
                    mViaPoint.mainViewSetting().resetRotateView = false;
                });
            }
        }
    }
}

MainDisplayWidget::~MainDisplayWidget() {
    mPainterHandle.reset();
    mTextureDrawer.reset();
    mDestinationTexturizer.reset();
    mClippingFrame.reset();
    mFilterFrame.reset();
    mFramebuffer.reset();
    mDefaultVAO.reset();

    gl::DeviceInfo::setInstance(nullptr);
    gl::Global::clearFunctions();
}

void MainDisplayWidget::setProject(core::Project* aProject) {
    mProject.reset();

    mRenderInfo = nullptr;
    mAbstractCursor = core::AbstractCursor();

    if (aProject) {
        mProject = aProject->pointee();
        mRenderInfo = &(static_cast<ProjectHook*>(mProject->hook())->renderInfo());
        mRenderInfo->camera.setDevicePixelRatio(this->devicePixelRatioF());
        mRenderInfo->camera.setScreenSize(this->size());
        mRenderInfo->camera.setImageSize(mProject->attribute().imageSize());
        mCanvasMover.setCamera(&(mRenderInfo->camera));
    }

    updateRender();
}

void MainDisplayWidget::setDriver(ctrl::Driver* aDriver) {
    mDriver = aDriver;

    // The driver owns the graph editor (and is created per project), so a NEW
    // driver is a new panel with no shared state: hand it the timeline's selection
    // and frame window here, which is also the moment a project's timeline is set
    // up (MainWindow::resetProjectRefs sets the timeline before it sets the
    // driver).
    syncGraphPanelSharedState();
}

void MainDisplayWidget::setTimeLineWidget(TimeLineWidget* aTimeLine) {
    if (mTimeLine == aTimeLine)
        return;

    if (mTimeLine) {
        mTimeLine->onViewRangeChanged.disconnect(mTimeLineRangeSlot);
        QObject::disconnect(mPlayWindowConnection);
    }

    mTimeLine = aTimeLine;
    mPlayWindowConnection = QMetaObject::Connection();

    if (mTimeLine) {
        mTimeLineRangeSlot =
            mTimeLine->onViewRangeChanged.connect(this, &MainDisplayWidget::syncGraphPanelSharedState);
        // THE TEMPORARY PLAYBACK WINDOW: the timeline's ruler gesture is not a frame
        // move and not a view-range change (a shift-drag can set a window and leave
        // both alone), so its own signal is what keeps the graph's overlay in step.
        // The surface is the widget's only TimeLineEditorWidget, the same lookup the
        // background menu uses.
        if (auto* surface = mTimeLine->findChild<TimeLineEditorWidget*>()) {
            mPlayWindowConnection =
                connect(surface, &TimeLineEditorWidget::playWindowChanged,
                        this, &MainDisplayWidget::syncGraphPanelSharedState);
        }
    }

    syncGraphPanelSharedState();
}

void MainDisplayWidget::syncGraphPanelSharedState() {
    ctrl::GraphEditor* editor = graphEditorOrNull(mDriver);
    if (!editor)
        return; // no panel yet (no driver, so no editor): nothing to hand it to

    ctrl::TimeLineEditor* timelineEditor = mTimeLine ? mTimeLine->editor() : nullptr;
    // THE RULER BAND'S SEEK is the timeline's own gesture: the graph hands its band's
    // frame to this same setter, so a press in either view's ruler moves ONE frame -
    // the project's - and both rulers' playheads follow it.
    if (timelineEditor) {
        editor->setFrameSeekDelegate([this](core::Frame aFrame) {
            if (mTimeLine)
                mTimeLine->setFrame(aFrame);
        });
        // THE RULER BAND'S SHIFT GESTURE is the SAME temporary playback window the
        // timeline's ruler defines: the graph hands its band's three steps to the
        // timeline widget, which applies them through the surface (which emits
        // playWindowChanged, the very notification below that pushes the window back
        // into this editor). So a Shift-drag in either ruler moves ONE window, and the
        // panel's overlay follows it on the same pass.
        editor->setPlayWindowDelegate(
            [this](ctrl::TimeLineEditor::PlayWindowStep aStep, core::Frame aFrame) {
                if (mTimeLine)
                    mTimeLine->updatePlayWindow(aStep, aFrame);
            }
        );
    }
    if (!mTimeLine || !timelineEditor) {
        // No timeline attached: the panel owns its frame axis and its selection
        // (its own private instance of the same set), which is the state the unit
        // tests drive it in.
        editor->clearFrameAxis();
        editor->setSelectionSource(nullptr);
        // No timeline, no temporary playback window: its window is the timeline's
        // state, and a detached panel must not keep drawing a stale one.
        editor->setPlayWindow(ctrl::TimeLineEditor::PlayWindow());
        // The panel draws that state, and it is a widget of its own.
        this->refreshGraphPanel();
        return;
    }

    // ONE selection: the panel writes the timeline's live set, which is what the
    // timeline renders its highlights from and what its own gestures (and its
    // Delete, its key menu) read - so a key selected in either view is
    // selected and highlighted in both.
    editor->setSelectionSource(&timelineEditor->selection());

    // ONE temporary playback window: the ruler gesture's window is the timeline's
    // state, and the panel's ruler overlays the same band, brackets and pills and
    // recesses by the same ACTIVE range (the window while it is active). Pushing it
    // as a VALUE means the graph's editor also derives the recession basis from the
    // project range through ctrl::GraphEditor::activeRange(), so the two views cannot
    // disagree about which frames are "outside".
    editor->setPlayWindow(timelineEditor->playWindow());

    // ONE frame window, pushed as VALUES: the frames the timeline is showing.
    // The panel maps them onto its own plot; it never fits, zooms or pans its own
    // X while this is in force, and the two gestures below send the horizontal
    // half of a wheel or a pan back HERE, so the panel scrolls and zooms the
    // timeline instead of drifting beside it. A sub-pixel pan therefore moves the
    // timeline, its new window comes back through onViewRangeChanged -> this
    // method, and the panel's X follows: one window, one owner.
    editor->setFrameGestureDelegates(
        [this](double aFrameDelta) {
            if (mTimeLine)
                mTimeLine->scrollFramesBy(aFrameDelta);
        },
        [this](int aWheelDelta, double aAnchorFrame) {
            if (mTimeLine)
                mTimeLine->zoomFramesAbout(aWheelDelta, aAnchorFrame);
        }
    );

    // The window itself: fractional frames, because that is what the timeline's
    // content-pixel viewport is. A whole-frame carrier (util::Range) made every pan
    // land on a frame and left the panel's zoom unable to hold its anchor.
    double lo = 0.0;
    double hi = 0.0;
    mTimeLine->visibleFrameWindow(lo, hi);
    if (hi > lo)
        editor->setFrameWindow(lo, hi);

    // The panel draws that window, so its own frame is now stale. TWO SURFACES READ
    // THIS STATE: the viewport (which no longer draws the panel) and the dock's Graph
    // page, which does - and that page is a QOpenGLWidget of its own, so the viewport's
    // update() does not paint it. This is the call that keeps the header's zoom and fit
    // buttons honest: they drive the timeline's view transform (TimeLineWidget::
    // zoomFramesAbout/scrollFramesBy), the new window comes back through
    // onViewRangeChanged -> HERE, and the page is asked for its frame.
    this->updateRender();
    this->refreshGraphPanel();
}

void MainDisplayWidget::refreshGraphPanel() {
#ifdef AE_ENABLE_IMGUI
    // The dock's Graph page, found through the window (this widget is the viewport and
    // holds no reference to the dock, exactly as gui::GraphPanelWidget finds this one
    // for its own menus). It is one widget per window, and a no-op elsewhere: with no
    // dock (a harness) or with the page not constructed (no ImGui build) there is
    // nothing to ask, and the request is dropped.
    if (QWidget* top = this->window()) {
        if (auto* panel = top->findChild<GraphPanelWidget*>())
            panel->refresh();
    }
#endif
}

bool MainDisplayWidget::showGraphKeyMenu(const core::TimeLineEvent::Target& aPressedKey, const QPoint& aGlobalPos) {
    if (!mTimeLine)
        return false; // no timeline to open its menu from (and no document to act on)

    // THE TIMELINE'S OWN MENU, opened where the press was: the panel maps its own
    // pointer to a screen position (QWidget::mapToGlobal) and that is what arrives
    // here, because the two widgets are siblings - this widget cannot map a point out
    // of the panel's space, and mapping one INTO its own space would put the menu
    // wherever the two widgets' origins happen to differ by. Which keys the menu acts
    // on, and whether the pressed one is lent to it, is the timeline's rule and stays
    // there (TimeLineWidget::openKeyMenu).
    return mTimeLine->openKeyMenu(aPressedKey, aGlobalPos);
}

bool MainDisplayWidget::showGraphHandleMenu(
    const ctrl::GraphEditor::ContextTarget& aTarget, const QPoint& aGlobalPos
) {
    ctrl::GraphEditor* editor = graphEditorOrNull(mDriver);
    if (!editor || aTarget.kind != ctrl::GraphEditor::ContextTarget::Kind_Handle)
        return false; // no panel, or the point names no handle: no handle menu

    // THE HANDLE'S OWN MENU, and it says WHICH SIDE it is about: a key owns two
    // tangents, and "Free" alone would not tell the user (or the undo history)
    // which one the entry changes. The section header names the owner side; the
    // entries are the five modes, with the one the side reads now checked.
    QMenu menu(this);
    menu.addSection(
        aTarget.handle == 1 ? tr("Incoming handle") : tr("Outgoing handle")
    );

    for (int i = util::Easing::HandleMode_Free; i < util::Easing::HandleMode_TERM; ++i) {
        const auto mode = static_cast<util::Easing::HandleMode>(i);
        QAction* action = menu.addAction(util::Easing::getHandleModeName(mode));
        action->setCheckable(true);
        action->setChecked(aTarget.mode == mode);
        // The editor applies ONE side of ONE segment as ONE undo step, named for
        // the side and the mode (ctrl::GraphEditor::setHandleMode). The document
        // notification is what repaints the panel and the timeline; repainting
        // here too is what makes the change visible the moment the menu closes
        // when the page had no other reason to redraw.
        action->connect(action, &QAction::triggered, this, [this, editor, aTarget, mode]() {
            editor->setHandleMode(aTarget, mode);
            this->updateRender();
            this->refreshGraphPanel();
        });
    }

    menu.exec(aGlobalPos);
    return true;
}

bool MainDisplayWidget::showGraphBackgroundMenu(const QPoint& aGlobalPos) {
    if (!mTimeLine)
        return false; // no timeline: its menu is the one there is

    // THE TIMELINE'S OWN SURFACE, found through the widget MainWindow wired in (the
    // inner editor is that widget's only TimeLineEditorWidget). It is hidden while the
    // dock's Graph tab is current, which is exactly when this route is used: the menu
    // reads the editor and the shared selection, not the visible pixels.
    auto* surface = mTimeLine->findChild<TimeLineEditorWidget*>();
    if (!surface)
        return false;

    // THE SAME ITEMS a right-click on empty timeline space offers (buildKeyMenu over
    // the shared selection), opened at the pointer, plus the graph's own "Fit graph".
    // The surface does NOT hit-test the timeline's lanes with this widget's pixels:
    // the panel and the timeline are two different geometries, and reading one as the
    // other named a key of whatever row those pixels happened to land in - the reason
    // a menu's actions could edit a key the user was not looking at.
    //
    // "Fit graph" is the panel's explicit fit (ctrl::GraphEditor::fitAll), the action
    // the plot's double-click used to run and now the ONLY place the panel offers it.
    return surface->openBackgroundMenu(
        aGlobalPos, tr("Fit graph"), [this]() {
            if (ctrl::GraphEditor* editor = graphEditorOrNull(mDriver)) {
                editor->fitAll();
                this->updateRender();
                this->refreshGraphPanel();
            }
        }
    );
}

void MainDisplayWidget::showGraphChannelMenu(const QVector<PanelChannelRow>& aRows, const QPoint& aWidgetPos) {
    if (aRows.isEmpty())
        return; // no channels: no list to show

    // ONE LIST, built fresh from the rows the panel's model published for the frame
    // the header's button was pressed in, and REUSED for the life of this widget: the
    // popup is a child of this one (so it dies with it) and hiding it on an outside
    // click leaves it ready to show again, while a QMenu-style WA_DeleteOnClose would
    // leak under a plain Qt::Popup (Qt's own dismissal HIDES a popup; it does not
    // close it). The rows are re-set on every open, so the state can never be stale.
    //
    // THE WIDGET IS gui::GraphChannelPopup, NOT A QMenu: the design's list has 22px
    // rows, an 8px swatch-to-name gap and 10px group titles at the swatch's own
    // padding, and none of those are reachable through gui::AppStyle's menu metrics
    // (24px rows, an icon margin and a QSS item padding). The row DATA is the panel
    // model's own (gui::PanelChannelRow), so the list, the curves and the timeline's
    // keys still name one channel one way.
    if (!mChannelPopup) {
        mChannelPopup = new GraphChannelPopup(this);

        // THE ROW'S ONE ACTION is its channel's visibility switch - the swatch beside
        // it IS its colour key, filled while the channel is shown and hollow while it
        // is hidden. The editor is read from the DRIVER at the moment the row is hit,
        // not captured: the list can be open across a project change, and the driver is
        // what owns the live editor. The popup hands back the row with its NEW
        // visibility (it flipped its own copy already, which is why the swatch follows
        // the click); a false answer means the row names a node the document no longer
        // holds, and the list closes rather than writing through it.
        mChannelPopup->onRowToggled = [this](const PanelChannelRow& aRow) {
            ctrl::GraphEditor* editor = graphEditorOrNull(mDriver);
            if (!channelRowNodeAlive(editor, aRow.node))
                return false; // the node is not one the live editor holds any more
            editor->setChannelVisible(*aRow.node, aRow.channel, aRow.visible);
            this->updateRender();
            this->refreshGraphPanel();
            return true;
        };

        // HIGHLIGHTING A ROW FOCUSES ITS CHANNEL - the other half of what the list is
        // for ("the row itself focuses the type"): the panel then draws that channel's
        // tangent handles and the panel's own summary names it. Hover, not activation,
        // because activation is already spent on the visibility switch. The DOCKED
        // panel is the surface that has to show it, hence the panel refresh beside the
        // viewport's.
        mChannelPopup->onRowHovered = [this](const PanelChannelRow& aRow) {
            ctrl::GraphEditor* editor = graphEditorOrNull(mDriver);
            if (!channelRowNodeAlive(editor, aRow.node))
                return; // the node is not one the live editor holds any more
            editor->setFocusedChannel(*aRow.node, aRow.channel);
            this->refreshGraphPanel();
        };
    }

    mChannelPopup->setRows(aRows);

    // aWidgetPos is the ANCHOR's bottom-right corner in this widget's coordinates: the
    // list opens just under it, right-aligned to it, which is where the dock header's
    // channels button sits.
    mChannelPopup->openAt(this->mapToGlobal(aWidgetPos));
}

void MainDisplayWidget::resetCamera() {
    if (mRenderInfo) {
        auto& camera = mRenderInfo->camera;
        auto scrSize = this->size();
        auto imgSize = camera.imageSize();
        camera.setCenter(QVector2D(scrSize.width() * 0.5f, scrSize.height() * 0.5f));
        if (scrSize.width() > 0 && scrSize.height() > 0 && imgSize.width() > 0 && imgSize.height() > 0) {
            auto scaleX = (float)scrSize.width() / imgSize.width();
            auto scaleY = (float)scrSize.height() / imgSize.height();
            auto minScale = scaleX < scaleY ? scaleX : scaleY;
            camera.setScale(minScale);
        }

        mCanvasMover.setCamera(&camera);
    }
}

void MainDisplayWidget::setProjectTabBar(ProjectTabBar* aTabBar) { mProjectTabBar = aTabBar; }

void MainDisplayWidget::setViewportBackground(const QColor& aColor) {
    if (mViewportBackground != aColor) {
        mViewportBackground = aColor;
        this->updateRender();
    }
}

void MainDisplayWidget::updateRender() { this->update(); }

void MainDisplayWidget::initializeGL() {
    if (!this->context()->isValid()) {
        XC_FATAL_ERROR("OpenGL Error", "Invalid opengl context.", "");
    }
    // check version
    {
        auto version = this->format().version();
        // fallback
        if (version < gl::Global::kVersion) {
            const QString v = (const char*)this->context()->functions()->glGetString(GL_VERSION);
            version = {v.at(0).digitValue(), v.at(2).digitValue()};
            if (version < gl::Global::kVersion) {
                // we ball
                QMessageBox::warning(this, "OpenGL version check fail", "The minimum OpenGL version for AnimeEffects is "
                    + QString::number(gl::Global::kVersion.first) + "." + QString::number(gl::Global::kVersion.second) +
                    " but we have detected version " + QString::number(this->format().version().first) + "." + QString::number(this->format().version().second) +
                    ". AnimeEffects will attempt to run regardless but the app may crash unexpectedly.");
                version = {gl::Global::kVersion};
            }
        }
        if (version < gl::Global::kVersion) {
            auto obtainedVersion = QString::number(version.first) + "." + QString::number(version.second);
            auto requiredVersion =
                QString::number(gl::Global::kVersion.first) + "." + QString::number(gl::Global::kVersion.second);
            XC_FATAL_ERROR(
                "OpenGL Error",
                QString("Failed to get the correct OpenGL version.\n"
                        "The obtained version is %1,\n"
                        "but the required version is %2.")
                    .arg(obtainedVersion)
                    .arg(requiredVersion),
                ""
            );
        }
    }
    // initialize opengl functions
    auto functions = QOpenGLVersionFunctionsFactory::get<gl::Global::Functions>(this->context());
    if (!functions) {
        XC_FATAL_ERROR("OpenGL Error", "Failed to get opengl functions.", "");
    }
    if (!functions->initializeOpenGLFunctions()) {
        XC_FATAL_ERROR("OpenGL Error", "Failed to initialize opengl functions.", "");
    }

    // setup global info
    gl::Global::setContext(*this);
    gl::Global::setFunctions(*functions);

    // setup gl root
    mGLRoot.setContextAccessor(mGLContextAccessor);
    mGLRoot.setFunctions(*functions);

    // initialize opengl device info
    mGLDeviceInfo.load();
    gl::DeviceInfo::setInstance(&mGLDeviceInfo);
    mViaPoint.setGLDeviceInfo(mGLDeviceInfo);

#ifdef USE_GL_CORE_PROFILE
    // initialize default vao
    mDefaultVAO.reset(new gl::VertexArrayObject());
    mDefaultVAO->bind(); // keep binding
#endif

    mDevicePixelRatio = this->devicePixelRatioF();

    // create framebuffer for display
    mFramebuffer.reset(new QOpenGLFramebufferObject(deviceSize()));

    // create clipping buffer
    mClippingFrame.reset(new core::ClippingFrame());
    mClippingFrame->resize(deviceSize());

    // create filter frame for folder composite filters
    mFilterFrame.reset(new core::FilterFrame());
    mFilterFrame->resize(deviceSize());

    // create texturizer for destination colors of the framebuffer
    mDestinationTexturizer.reset(new core::DestinationTexturizer());
    mDestinationTexturizer->resize(deviceSize());

    // create texture drawer for copying framebuffer to display
    mTextureDrawer.reset(new gl::EasyTextureDrawer());
    if (!mTextureDrawer->init()) {
        XC_FATAL_ERROR("OpenGL Error", "Failed to initialize texture drawer.", "");
    }

    mPainterHandle.reset(new ctrl::PainterHandle());

    GL_CHECK_ERROR();
}

void MainDisplayWidget::paintGL() {
    gl::Global::Functions& ggl = gl::Global::functions();

    // clear clipping
    mClippingFrame->clearTexture();
    mClippingFrame->resetClippingId();
    GL_CHECK_ERROR();

    mDestinationTexturizer->clearTexture();
    GL_CHECK_ERROR();

    mFilterFrame->clearAll();
    GL_CHECK_ERROR();

    if (!mFramebuffer->bind()) {
        XC_FATAL_ERROR("OpenGL Error", "Failed to bind framebuffer.", "");
    }

    // setup
    gl::Util::setViewportAsActualPixels(deviceSize());
    gl::Util::clearColorBuffer(mViewportBackground.redF(), mViewportBackground.greenF(), mViewportBackground.blueF(), 1.0f);
    gl::Util::resetRenderState();
    GL_CHECK_ERROR();

    // setup renderinfo
    if (mProject) {
        XC_PTR_ASSERT(mRenderInfo);
        mRenderInfo->time = mProject->currentTimeInfo();
        mRenderInfo->framebuffer = mFramebuffer->handle();
        mRenderInfo->dest = mFramebuffer->texture();
        mRenderInfo->isGrid = false;
        mRenderInfo->nonPosed = false;
        mRenderInfo->clippingId = 0;
        mRenderInfo->clippingFrame = mClippingFrame.data();
        mRenderInfo->destTexturizer = mDestinationTexturizer.data();
        mRenderInfo->filterFrame = mFilterFrame.data();
        mRenderInfo->opacityScale = 1.0f;
        XC_ASSERT(mRenderInfo->framebuffer != 0);
        XC_ASSERT(mRenderInfo->dest != 0);
        mRenderInfo->camera.setFlip(mViewSetting.flipCanvas);
    }

    if (mDriver) {
        XC_PTR_ASSERT(mRenderInfo);
        auto gridTarget = mViewSetting.showLayerMesh ? mDriver->currentTarget() : nullptr;
        mDriver->renderGL(*mRenderInfo, gridTarget);
        GL_CHECK_ERROR();
    }

    if (!mFramebuffer->release()) {
        XC_FATAL_ERROR("OpenGL Error", "Failed to unbind framebuffer.", "");
    }

    ggl.glBindFramebuffer(GL_FRAMEBUFFER, this->defaultFramebufferObject());

    if (mViewSetting.cutImagesByTheFrame && mProject) {
        XC_PTR_ASSERT(mRenderInfo);
        gl::Util::clearColorBuffer(mViewportBackground.redF(), mViewportBackground.greenF(), mViewportBackground.blueF(), 1.0f);

        auto imageQuad = mRenderInfo->camera.screenImageQuadangle();
        const QSize screenSize = mRenderInfo->camera.screenSize();

        mTextureDrawer->draw(mFramebuffer->texture(), imageQuad, screenSize, imageQuad, screenSize);
    } else {
        mTextureDrawer->draw(mFramebuffer->texture());
    }

    ggl.glFlush();
    GL_CHECK_ERROR();
}

void MainDisplayWidget::paintEvent(QPaintEvent* aEvent) {
    // try lock rendering!!
    if (!mRenderingLock.tryLockForRead())
        return;
    util::Finally unlocker([=]() { this->mRenderingLock.unlock(); });

    QOpenGLWidget::paintEvent(aEvent);

    QPainter* painter = mPainterHandle->begin(*this);
    painter->setRenderHint(QPainter::Antialiasing);

    if (mDriver) {
        XC_PTR_ASSERT(mRenderInfo);
        mDriver->renderQt(*mRenderInfo, *painter);
        GL_CHECK_ERROR();
    }
    // we must call end function
    mPainterHandle->end();

    // NOTHING ELSE IS PAINTED HERE. The animation graph panel used to be drawn into
    // this widget's framebuffer right here (a Dear ImGui overlay over the artwork, with
    // the contrast-outline composite). It is drawn in the TIMELINE DOCK now
    // (gui::GraphPanelWidget, the Graph tab's page, design section 5), on a floor of its
    // own, and it pins the editor's frame window to ITS plot size in its own paint - so
    // this paint leaves the editor's scale alone (the old call here re-derived the shared
    // frame window for this widget's geometry, which no longer draws the panel).
}

void MainDisplayWidget::resizeGL(int w, int h) {
    // currrent device pixel ratio (Attention that it is variable.)
    mDevicePixelRatio = this->devicePixelRatioF();
    const QSize devSize(mDevicePixelRatio * w, mDevicePixelRatio * h); // device pixel size
    const QSize absSize(w, h); // abstract pixel size

    if (mRenderInfo) {
        mRenderInfo->camera.setDevicePixelRatio(mDevicePixelRatio);
        mRenderInfo->camera.setScreenSize(absSize);
        mCanvasMover.onScreenResized();
    }
    mFramebuffer.reset();
    mFramebuffer.reset(new QOpenGLFramebufferObject(devSize.width(), devSize.height()));

    mClippingFrame.reset();
    mClippingFrame.reset(new core::ClippingFrame());
    mClippingFrame->resize(devSize);

    mDestinationTexturizer->resize(devSize);

    mFilterFrame->resize(devSize);

    if (mProjectTabBar) {
        mProjectTabBar->updateTabPosition(absSize);
    }
    GL_CHECK_ERROR();
}

void MainDisplayWidget::applyCanvasMoverDragMode() {
    // ONE OWNER PER DRAG, written once: the flags are the gestures the user is making
    // (the Cursor tool, the MoveCanvas key, a middle press this widget began), and
    // folding them into one expression is what stops a tool change or a key press
    // mid-drag from re-enabling a pan the user has not asked for. The animation graph
    // panel used to hold ONE of these flags (it took the left drag while it was drawn
    // over this widget); it lives in the dock's own page now, where its gestures never
    // reach this camera.
    const bool dragging = mMovingCanvasByKey || mMovingCanvasByTool || mMovingCanvasByMiddleMouseButton;
    mCanvasMover.setDragAndMove(dragging);
}

void MainDisplayWidget::mouseMoveEvent(QMouseEvent* aEvent) {
    if (mRenderInfo) {
        // The panel's own gestures read the modifier state from here: neither
        // core::AbstractCursor nor the editor's updateCursor carries modifiers, so
        // the widget hands them over before it routes the event (see
        // ctrl::Driver::setGraphModifiers). Every move re-reads them, which is what
        // lets Shift constrain a key drag that is already in flight.
        if (mDriver) {
            mDriver->setGraphModifiers(aEvent->modifiers());
        }
        if (mAbstractCursor.setMouseMove(aEvent, mRenderInfo->camera)) {
            updateCursor();
            // if (!mUsingTablet) qDebug() << "move" << aEvent->pos();
        }

        if (mCanvasMover.updateByMove(
                mAbstractCursor.screenPos(),
                mAbstractCursor.screenVel(),
                mAbstractCursor.isPressedLeft(),
                mAbstractCursor.isPressedMiddle(),
                mAbstractCursor.isPressedRight()
            )) {
            updateRender();
        }
    }
}

void MainDisplayWidget::mousePressEvent(QMouseEvent* aEvent) {
    if (mRenderInfo) {
        if (mDriver) {
            mDriver->setGraphModifiers(aEvent->modifiers());
        }
        if (mAbstractCursor.setMousePress(aEvent, mRenderInfo->camera)) {
            updateCursor();
            // if (!mUsingTablet) qDebug() << "press";

            // The MIDDLE button pans the canvas (the graph panel used to own this
            // drag while it was up over this widget; it is in the dock now and owns
            // the cursor inside its own page).
            if (mViaPoint.mouseSetting().middleMouseMoveCanvas &&
                aEvent->button() == Qt::MouseButton::MiddleButton) {
                mMovingCanvasByMiddleMouseButton = true;
                applyCanvasMoverDragMode();
            }

            // A canvas grab shows the closed hand; the resting pan cursor
            // (open hand) only shows before the button goes down.
            if ((mMovingCanvasByTool || mMovingCanvasByKey || mMovingCanvasByMiddleMouseButton) &&
                (aEvent->button() == Qt::MouseButton::LeftButton ||
                 aEvent->button() == Qt::MouseButton::MiddleButton)) {
                this->setCursor(Qt::ClosedHandCursor);
            }
        }
    }
}

void MainDisplayWidget::mouseReleaseEvent(QMouseEvent* aEvent) {
    if (mRenderInfo) {
        if (mDriver) {
            mDriver->setGraphModifiers(aEvent->modifiers());
        }
        if (mAbstractCursor.setMouseRelease(aEvent, mRenderInfo->camera)) {
            updateCursor();
            // if (!mUsingTablet) qDebug() << "release";

            if (aEvent->button() == Qt::MouseButton::MiddleButton) {
                // A canvas middle pan this widget began ends here.
                mMovingCanvasByMiddleMouseButton = false;
                applyCanvasMoverDragMode();
            }

            // back to the resting cursor once every button is up (a
            // right-button release mid-drag must not clear the closed hand)
            if (aEvent->buttons() == Qt::NoButton) {
                this->setCursor(mMovingCanvasByTool ? Qt::OpenHandCursor : Qt::ArrowCursor);
            }
        }
    }
}

void MainDisplayWidget::wheelEvent(QWheelEvent* aEvent) {
    if (mCanvasMover.updateByWheel(
            QVector2D(aEvent->position()), aEvent->angleDelta().y(), mViaPoint.mouseSetting().invertMainViewScaling
        )) {
        updateRender();
    }
}

void MainDisplayWidget::tabletEvent(QTabletEvent* aEvent) {
    if (mRenderInfo) {
        if (mDriver) {
            mDriver->setGraphModifiers(aEvent->modifiers());
        }
        if (mAbstractCursor.setTabletEvent(aEvent, mRenderInfo->camera)) {
            updateCursor();
        }
        if (mAbstractCursor.shouldResetCanvas) {
            mCanvasMover.resetRotation();
            updateRender();
        }
    }

    if (aEvent->modifiers().testFlag(Qt::ShiftModifier)) {
        mCanvasMover.rotate(qDegreesToRadians(1.0 * mAbstractCursor.screenVel().x()));
        mCanvasMover.updateByWheel(mAbstractCursor.screenPos(), 2.0 * mAbstractCursor.screenVel().y(), false);
        updateRender();
    }
    else if (mCanvasMover.updateByMove(
            mAbstractCursor.screenPos(),
            mAbstractCursor.screenVel(),
            mAbstractCursor.isPressedLeft(),
            mAbstractCursor.isPressedMiddle(),
            mAbstractCursor.isPressedRight()
        )) {
        updateRender();
    }
    aEvent->accept();
}

void MainDisplayWidget::mouseDoubleClickEvent(QMouseEvent* aEvent) {
    // QWidget's own double-click behaviour is "a press" (its default implementation
    // calls mousePressEvent), and that is what the viewport wants here: the second
    // click still drags the canvas and the tools' own gestures. (The panel's fit
    // gesture used to be taken here; it is the docked panel's, on its own page.)
    QOpenGLWidget::mouseDoubleClickEvent(aEvent);
}

void MainDisplayWidget::keyPressEvent(QKeyEvent* aEvent) {
    // The event is ignored, i.e. it propagates to the main window's key map, which is
    // where the application's key routing has always been. (A Dear ImGui text field
    // used to hold the keyboard while the panel was drawn over this widget; the panel
    // is the dock's page now and its own widget forwards its keys.)
    QOpenGLWidget::keyPressEvent(aEvent);
}

void MainDisplayWidget::keyReleaseEvent(QKeyEvent* aEvent) { QOpenGLWidget::keyReleaseEvent(aEvent); }

void MainDisplayWidget::inputMethodEvent(QInputMethodEvent* aEvent) { QOpenGLWidget::inputMethodEvent(aEvent); }

QVariant MainDisplayWidget::inputMethodQuery(Qt::InputMethodQuery aQuery) const {
    // The ImGui caret an input context used to ask about lives in the panel's own page
    // now (gui::GraphPanelWidget forwards the same queries).
    return QOpenGLWidget::inputMethodQuery(aQuery);
}

void MainDisplayWidget::focusInEvent(QFocusEvent* aEvent) { QOpenGLWidget::focusInEvent(aEvent); }

void MainDisplayWidget::focusOutEvent(QFocusEvent* aEvent) { QOpenGLWidget::focusOutEvent(aEvent); }

void MainDisplayWidget::onVisualUpdated() { updateRender(); }

void MainDisplayWidget::onToolChanged(ctrl::ToolType aType) {
    if (aType == ctrl::ToolType_Cursor) {
        this->setCursor(Qt::OpenHandCursor);
        mMovingCanvasByTool = true;
        applyCanvasMoverDragMode();
    } else {
        this->setCursor(Qt::ArrowCursor);
        mMovingCanvasByTool = false;
        applyCanvasMoverDragMode();
    }
}

void MainDisplayWidget::onFinalizeTool(ctrl::ToolType) {
    mAbstractCursor.suspendEvent([=]() { updateCursor(); });
    mAbstractCursor.resumeEvent();
}

void MainDisplayWidget::onViewSettingChanged(const MainViewSetting& aSetting) {
    mViewSetting = aSetting;

    if (mViewSetting.resetRotateView) {
        mCanvasMover.resetRotation();
    } else if (mViewSetting.rotateViewACW) {
        mCanvasMover.rotate(qDegreesToRadians(-15.0f));
    } else if (mViewSetting.rotateViewCW) {
        mCanvasMover.rotate(qDegreesToRadians(15.0f));
    }

    updateRender();
}

void MainDisplayWidget::onProjectAttributeUpdated() {
    if (mProject && mRenderInfo) {
        const QSize newSize = mProject->attribute().imageSize();
        if (newSize != mRenderInfo->camera.imageSize()) {
            mRenderInfo->camera.setImageSize(newSize);
        }
    }
}

void MainDisplayWidget::updateCursor() {
    if (mDriver) {
        XC_PTR_ASSERT(mRenderInfo);
        if (mDriver->updateCursor(mAbstractCursor, mRenderInfo->camera)) {
            // The panel changed something: its selection, its focus, its window,
            // or the document under a drag. The TIMELINE draws the same selection
            // (they share one set) and reads the same document, and nothing else
            // would ask it to paint while the pointer is over the viewport - so a
            // key selected here would keep an unhighlighted square there, and a
            // key the panel drags would be drawn at its old frame. One repaint
            // request, coalesced by Qt with any other.
            if (mTimeLine) {
                mTimeLine->update();
            }
            updateRender();
        }
    }
}

} // namespace gui
