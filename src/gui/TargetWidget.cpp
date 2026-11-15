#include "gui/TargetWidget.h"
#include "gui/AnimationSettingDialog.h"
#ifdef AE_ENABLE_IMGUI
#include "gui/GraphPanelWidget.h"
#endif
#include "gui/MainMenuBar.h"
#include "gui/TimeLineHeader.h"
#include "cmnd/BasicCommands.h"
#include "cmnd/ScopedMacro.h"
#include "ctrl/CmndName.h"
#include "ctrl/TimeLineEditor.h"
#include "core/ProjectEvent.h"
#include <QDialog>
#include <QHBoxLayout>
#include <QScopedPointer>
#include <QVBoxLayout>

namespace gui {

TargetWidget::TargetWidget(
    ViaPoint& aViaPoint, GUIResources& aResources, const LocaleParam& aLocale, QWidget* aParent,
    const QSize& aSizeHint
):
    QWidget(aParent),
    mProject(),
    mViaPoint(aViaPoint),
    mSizeHint(aSizeHint),
    mHorizontalSplitter(nullptr),
    mObjTree(nullptr),
    mSurfaceStack(nullptr),
    mTimeLine(nullptr),
#ifdef AE_ENABLE_IMGUI
    mGraphPanel(nullptr),
    mFrameSlot(),
    mSelectionSlot(),
    mDocumentSlot(),
    mAttributeSlot(),
    mSelectionChangedSlot(),
    mSelectionSource(nullptr),
#endif
    mHeader(nullptr),
    mSuspendCount(0) {
    mHorizontalSplitter = new ThinSplitter(Qt::Horizontal, this);
    // The hitbox grab area sits 2px into the object tree / 10px into the
    // timeline, so it clears the tree's edge scrollbar; the hairline stays on
    // the boundary.
    mHorizontalSplitter->setHitBoxOffset(4);
    mObjTree = new ObjectTreeWidget(aViaPoint, aResources, mHorizontalSplitter, aLocale);
    // THE ROWS ARE THE LANES. The tree's model owns the row list (gui/AnimationRow.h) and
    // announces every change; the timeline draws exactly what it published, so a row's
    // height and the lane block beside it come from one definition and no view reads the
    // other's geometry.
    mObjTree->onRowsUpdated.connect([this]() {
        if (mTimeLine && mObjTree)
            mTimeLine->setRows(mObjTree->rows());
    });
    // The dock's body, as ONE pane with the views stacked in it (design section 5):
    // the timeline's own surface and, when the graph panel is part of the build, the
    // animation graph panel. The tab in the header is what swaps them; the TREE stays
    // the splitter's first widget in both, so it is on screen either way. A
    // QStackedWidget rather than hide/show by hand: the layout of the hidden page is
    // not computed, and the surface the user sees is the one Qt sizes and shows.
    mSurfaceStack = new QStackedWidget(mHorizontalSplitter);
    mTimeLine = new TimeLineWidget(aResources, aViaPoint, *this, mSurfaceStack);
    // Link/Separate Channels changes lane count (and therefore row heights)
    // without a document edit: re-derive the tree's rows, which re-pushes them
    // through the connection above. The timeline's own paint is refreshed by its
    // last row push.
    mTimeLine->onChannelGroupingChanged.connect([this]() {
        if (mObjTree)
            mObjTree->refreshRows();
    });
#ifdef AE_ENABLE_IMGUI
    // The Graph tab is the SAME panel the viewport's overlay draws, docked: it is the
    // panel's own widget (gui::GraphPanelWidget), pinned over the dock's floor at the
    // dock's size - no scrim, because that floor is already opaque.
    mGraphPanel = new GraphPanelWidget(aResources, mSurfaceStack);
#endif
    mSurfaceStack->addWidget(mTimeLine);
#ifdef AE_ENABLE_IMGUI
    mSurfaceStack->addWidget(mGraphPanel);

    // THE PANEL IS THE SAME CURRENT FRAME'S SECOND VIEW. What a step, a playback tick or
    // a ruler gesture changes is the project's current frame, and the ONE announcement of
    // that is the timeline's own onFrameUpdated - TimeLineWidget::setFrame emits it (after
    // mInner->setFrame, so the frame is already the new one when it fires) and so does the
    // cursor pass that moves the frame, exactly once per move. The panel draws its
    // playhead from core::Project::currentTimeInfo() through the editor, i.e. from a value
    // that changes without touching this widget's pixels, so without this connection the
    // Graph tab's playhead stands still while the transport steps the frame (measured:
    // twenty ctrl+Right presses, zero pixels changed, with the Graph tab current).
    //
    // ONE EMISSION, ONE update(), ONE PAINT: the notification is emitted once per frame
    // move (setFrame and the frame-moving cursor pass each emit it once), refresh() is
    // update(), and Qt coalesces requests; nothing in this path forces a paint out of
    // order, and the frame is already the new one when the notification fires, so the
    // panel can never read the value a step just left. While the timeline tab is up the
    // request lands on a hidden page and is dropped by Qt; the tab switch has its own
    // refresh (onSurfaceChanged).
    mFrameSlot = mTimeLine->onFrameUpdated.connect([this]() {
        if (mGraphPanel)
            mGraphPanel->refresh();
        // a mask chip previews the matte at the CURRENT frame: an animated one changes here
        if (mObjTree)
            mObjTree->frameChanged();
    });

    // AND THE TREE SELECTS WHAT IT DRAWS. The tree is the splitter's first widget, so it
    // is on screen in this tab as well - the design keeps it here because it names the
    // layers and picks whose channels the panel draws. A selection change is a different
    // model (new targets), and nothing about it touches this page's pixels: the graph
    // would keep drawing the previous layer's curves. The same one-request-per-change rule
    // applies (refresh() is update(), Qt coalesces, and a hidden page drops it); the
    // selection is a different event from a frame move, so the two never double up.
    mSelectionSlot = mObjTree->onSelectionChanged.connect([this](core::ObjectNode*) {
        if (mGraphPanel)
            mGraphPanel->refresh();
    });
#endif
    mSurfaceStack->setCurrentWidget(mTimeLine);

    // The transport is a fixed-size group of buttons; it belongs to the header
    // bar's own layout (not to the splitter, and no longer to a column beside
    // it), so it can never be resized and the lanes get the dock's full width.
    mPlayBack = new PlayBackWidget(aResources, this, *mProject);

    mHorizontalSplitter->addWidget(mObjTree);
    mHorizontalSplitter->addWidget(mSurfaceStack);
    mHorizontalSplitter->setCollapsible(0, false);
    mHorizontalSplitter->setCollapsible(1, false);


    // The dock's header: the 40px bar the transport moved into, above the
    // splitter, spanning the dock. The row below it is the one the dock has
    // always had - object tree | surface - now without the transport column.
    mHeader = new TimeLineHeader(aResources, mPlayBack, this);
    mHeader->setTimeLineWidget(mTimeLine);
#ifdef AE_ENABLE_IMGUI
    // The header's tab is the dock's view switch, for the whole life of the dock: the
    // tab names the page, so the panel needs no title of its own and both pages keep
    // the one header's transport, chip, quick easings and zoom.
    connect(mHeader, &TimeLineHeader::surfaceChanged, this, &TargetWidget::onSurfaceChanged);
#endif

    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    row->addWidget(mHorizontalSplitter, 1);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(mHeader);
    lay->addLayout(row, 1);

    mPlayBack->setPushDelegate([=](PlayBackWidget::PushType aType) { this->onPlayBackButtonPushed(aType); });
}

TargetWidget::~TargetWidget() {
    // THE HEADER GOES FIRST, AND THE ORDER IS LOAD-BEARING. The header subscribes to
    // the timeline editor's selection signaller (TimeLineHeader::mSelectionSlot), and
    // the signaller is a plain std::list of subscribers inside the editor with no
    // liveness registry: a disconnect on a freed signaller walks freed memory and
    // crashes. Its dtor has to run while that signaller is alive, so it cannot be
    // left to the child sweep.
    //
    // The sweep is why. A destructor BODY runs before QObject::~QObject deletes the
    // children, but the sweep deletes them in CREATION ORDER - and the dock creates
    // the timeline subtree at the top of its constructor (mSurfaceStack/mTimeLine)
    // while mHeader is created last. So the sweep frees the editor (and the header's
    // own QPointer target) BEFORE the header, which is exactly the order this delete
    // reverses. Measured before it existed: exit 139 and one core per quit, with the
    // stack in util::Signaler::disconnect from TimeLineHeader::~TimeLineHeader.
    //
    // THE INVARIANT THIS ENCODES, for anything else the dock grows: a widget that
    // subscribes to a signaller OWNED BY ANOTHER WIDGET must either be destroyed
    // first - its owner deletes it explicitly, as the header is deleted here - or hold
    // its publisher through a self-nulling handle (QPointer, see
    // TimeLineHeader::mInner). A bare pointer plus "it is destroyed first" is only a
    // promise, and the child sweep does not keep promises.
    //
    // mHeader is initialised to nullptr in the init list, and `delete nullptr` is a
    // no-op, so no separate guard is needed; the store keeps a stale pointer out of
    // the rest of this body.
    delete mHeader;
    mHeader = nullptr;
#ifdef AE_ENABLE_IMGUI
    // The signaler lives in the timeline widget, the receiver is the panel; both are
    // children destroyed after this body, and the connection is dropped here so nothing
    // can be emitted into the panel while the dock is being torn down.
    mTimeLine->onFrameUpdated.disconnect(mFrameSlot);
    mObjTree->onSelectionChanged.disconnect(mSelectionSlot);
    if (mProject) {
        mProject->onTimeLineModified.disconnect(mDocumentSlot);
        mProject->onProjectAttributeModified.disconnect(mAttributeSlot);
    }
    // The selection lives in the timeline's editor, which is a child destroyed after this
    // body: drop the last connection here for the same reason as the others.
    if (mSelectionSource)
        mSelectionSource->onChanged.disconnect(mSelectionChangedSlot);
#endif
}

void TargetWidget::setProject(core::Project* aProject) {
    mPlayBack->pushPauseButton();
#ifdef AE_ENABLE_IMGUI
    // The OUTGOING project's notifications go first: the panel must never be asked to
    // redraw from a document that is being dropped, and the old project may be gone the
    // moment MainWindow finishes this reset.
    if (mProject) {
        mProject->onTimeLineModified.disconnect(mDocumentSlot);
        mProject->onProjectAttributeModified.disconnect(mAttributeSlot);
    }
#endif
    mProject = aProject;
#ifdef AE_ENABLE_IMGUI
    if (mProject) {
        // THE PANEL IS A VIEW OF THIS DOCUMENT. Two of the project's changes alter what it
        // draws without touching this page's pixels, and each is announced by the project
        // AFTER the write that made it - the panel's repaint always reads the new state:
        //
        //  * onTimeLineModified: a key edit made from beside the dock (the property panel,
        //    or the easing picker's commit, which goes through the timeline's notifier).
        //    TimeLineUtil::Notifier is a cmnd::Listener, and the stack executes the command
        //    first and calls onExecuted/onRedone/onUndone afterwards (cmnd::Scalable::
        //    tryExec / Stack::Macro: the command loop, then the listeners), so the keys are
        //    already written when it fires. The graph's OWN gestures fire it too, after
        //    their command landed - the panel has already asked for that repaint from its
        //    cursor pass, so here it is a second update() on the same paint (Qt coalesces).
        //    The dock's header reads the same document through its quick easings, so this
        //    notification re-reads them as well: an undo/redo or a paste can change the
        //    easing a selected key carries WITHOUT moving the selection, which is the only
        //    other state they follow.
        //  * onProjectAttributeModified: fps and the frame range, which move the panel's
        //    frame axis. Project::Attribute's setters assign the member and only then call
        //    the project's notifier (core/Project.cpp: setFrameRange/setFps/... all return
        //    early when the value did not change, so this is one emission per real change).
        //
        // NO LOOP IS POSSIBLE: the panel's refresh() is update(), which writes nothing, and
        // refreshFrame() only updates the header's controls; neither writes the document,
        // so a repaint cannot re-emit either signal, and neither signal is emitted from a
        // paint.
        mDocumentSlot = mProject->onTimeLineModified.connect([this](core::TimeLineEvent&, bool) {
            if (mGraphPanel)
                mGraphPanel->refresh();
            mHeader->refreshFrame();
        });
        mAttributeSlot = mProject->onProjectAttributeModified.connect([this](core::ProjectEvent&, bool) {
            if (mGraphPanel)
                mGraphPanel->refresh();
        });
    }
#endif
#ifdef AE_ENABLE_IMGUI
    // THE PANEL'S DRIVER DIES WITH THE PROJECT, and the bar below asks the panel for its
    // channel rows (the header's channels button names them). When the project is being
    // CLEARED the old driver is already destroyed while this widget still holds the
    // pointer, so the question would reach a dead editor: measured 2026-10-19 as a
    // SIGSEGV inside GraphEditor::isLooping from channelRows via
    // TimeLineHeader::refreshFrame, on every "open a project from the command line".
    // Dropping the driver FIRST makes graphEditor() null, which channelRows() already
    // answers with no rows. The new project's driver arrives in setDriver, in the same
    // resetProjectRefs pass.
    if (!aProject && mGraphPanel)
        mGraphPanel->setDriver(nullptr);
#endif
    // The TIMELINE first, then the tree: the tree's rebuild announces the new row list, and
    // the timeline must already be on the new project (and its editor) to receive it.
    mTimeLine->setProject(aProject);
    mObjTree->setProject(aProject);
    mTimeLine->setRows(mObjTree->rows());
    mPlayBack->aProject = aProject;
    // The bar's timeline-derived state must follow the editor that was just set
    // above (or cleared): refreshFrame() re-reads the quick easings' availability
    // and active type from the timeline's live selection, and (in a build with the
    // panel) the channels button's own words.
    mHeader->refreshFrame();
    // The Graph tab's panel (when the panel is part of the build) is NOT re-read
    // here: it reads the document through the driver, and this call comes BEFORE
    // MainWindow hands the new one over - the previous project's driver is already
    // destroyed at this point, so a refresh here would read a dead editor.
    // setDriver() itself re-reads the panel, in the same resetProjectRefs pass, and a
    // tab switch re-reads it again.
}

void TargetWidget::setDriver(ctrl::Driver* aDriver) {
#ifdef AE_ENABLE_IMGUI
    // The driver owns ctrl::GraphEditor, and it is created and destroyed with the
    // project: MainWindow hands it over from the same call site that hands it to the
    // viewport and the tool widget, so the docked panel and the overlay read the ONE
    // editor of the project on screen (see MainWindow::resetProjectRefs).
    mGraphPanel->setDriver(aDriver);

    // THE SHARED SELECTION. ctrl::GraphEditor writes and reads the TIMELINE editor's
    // one time::Selection (MainDisplayWidget::syncGraphPanelSharedState hands it over),
    // so a selection change is a change to what this panel draws - which channels'
    // markers are at full strength, the hull, "N key(s)", and (with Ctrl+A) every marker
    // of every shown channel - and nothing else announces it. The chip in the dock's
    // header reads the same set through the same notifier (TimeLineEditorWidget:147).
    //
    // ORDERING: Selection::rebuildFocus() is what fires onChanged, and every mutator
    // calls it AFTER writing mItems (ctrl/time/time_Selection.h: "the two always travel
    // together: a mutator that changed the set reaches both, one that did not has already
    // returned false"). So the set the model reads when the repaint happens is the new
    // one, and a press that selected nothing emits nothing. refresh() is only update(),
    // so it cannot re-enter the selection: no loop.
    //
    // LIFECYCLE: the set belongs to the TIMELINE's editor, not to the driver - the editor
    // survives a project change - so the slot is re-wired here, where the driver (and
    // with it the panel's editor) is handed over: disconnected from the old set, connected
    // to the current one, and disconnected again when no project is open. The destructor
    // drops the last one before the timeline is destroyed.
    ctrl::TimeLineEditor* timelineEditor = (aDriver && mTimeLine) ? mTimeLine->editor() : nullptr;
    ctrl::time::Selection* selection = timelineEditor ? &timelineEditor->selection() : nullptr;
    if (selection != mSelectionSource) {
        if (mSelectionSource)
            mSelectionSource->onChanged.disconnect(mSelectionChangedSlot);
        mSelectionSource = selection;
        if (mSelectionSource) {
            mSelectionChangedSlot = mSelectionSource->onChanged.connect([this] {
                if (mGraphPanel)
                    mGraphPanel->refresh();
            });
        }
    }

    // A new editor is a new panel: the view it draws is built from it, so re-read it
    // now rather than on the next frame that happens to repaint.
    mGraphPanel->refresh();
#else
    // No graph surface in this build: the dock has nothing that reads the driver
    // (the viewport's overlay and the tool widget have their own setDriver).
    (void)aDriver;
#endif
}

#ifdef AE_ENABLE_IMGUI
void TargetWidget::onSurfaceChanged(TimeLineHeader::Surface aSurface) {
    const bool graph = aSurface == TimeLineHeader::Surface::Graph;
    mSurfaceStack->setCurrentWidget(graph ? static_cast<QWidget*>(mGraphPanel) : static_cast<QWidget*>(mTimeLine));
    // Becoming visible is the one moment the panel has to re-read the document and
    // the frame it is shown at: it is not repainted while the timeline tab is up
    // (Qt lays out and paints only the current page).
    if (graph)
        mGraphPanel->refresh();

    // THE TAB THAT BECOMES CURRENT TAKES THE KEYBOARD. Both views own keys the
    // timeline's own tab strip is the only way to reach - Ctrl+A selects every key
    // in whichever view is up - and until this line the surface had ClickFocus
    // only, so switching to a tab and pressing Ctrl+A did nothing until the body
    // was clicked. The press that switched the tab is a deliberate move to that
    // view; the keyboard follows it.
    // THE TIMELINE'S KEY HANDLER LIVES ON THE INNER SURFACE, not on the scroll area
    // that hosts it: gui::TimeLineWidget is a QAbstractScrollArea and
    // gui::TimeLineEditorWidget (its viewport child) is the widget whose
    // keyPressEvent owns Ctrl+A and Escape. Focusing the scroll area would give the
    // keys to a widget with no handler - the same "nothing happens" the tab switch
    // was meant to fix - so the lookup is the one the rest of this class uses.
    QWidget* target = mGraphPanel;
    if (!graph) {
        target = mTimeLine;
        if (auto* surface = mTimeLine->findChild<TimeLineEditorWidget*>())
            target = surface;
    }
    target->setFocus(Qt::TabFocusReason);
}
#endif

core::Frame TargetWidget::currentFrame() const { return mTimeLine->currentFrame(); }

void refreshMedia(core::Project* mProject, PlayBackWidget* mPlayBack){
    if(mProject->mediaRefresh){
        mPlayBack->aConf = mProject->pConf;
        mPlayBack->mediaPlayer = *mProject->mediaPlayer;
        mProject->mediaRefresh = false;
    }
}

void TargetWidget::stop() { mPlayBack->pushPauseButton(); }

void TargetWidget::suspend() { ++mSuspendCount; }

void TargetWidget::resume() {
    XC_ASSERT(mSuspendCount > 0);
    --mSuspendCount;
}

bool TargetWidget::isSuspended() const { return mSuspendCount > 0; }

void TargetWidget::onPlayBackButtonPushed(PlayBackWidget::PushType aType) {
    if (!mProject)
        return;
    if (aType == PlayBackWidget::PushType_Play) {
        refreshMedia(mProject, mPlayBack);
        mTimeLine->setPlayBackActivity(true, mPlayBack->aConf, &mPlayBack->mediaPlayer);
    } else if (aType == PlayBackWidget::PushType_Pause) {
        refreshMedia(mProject, mPlayBack);
        mTimeLine->setPlayBackActivity(false, mPlayBack->aConf, &mPlayBack->mediaPlayer);
    } else if (aType == PlayBackWidget::PushType_Step) {
        mTimeLine->setFrame(currentFrame().added(1));
    } else if (aType == PlayBackWidget::PushType_StepBack) {
        mTimeLine->setFrame(currentFrame().added(-1));
    } else if (aType == PlayBackWidget::PushType_Rewind) {
        // Land on the effective playback range's start (the temporary
        // window's start while it overrides) — Frame(0) would stay at 0 when
        // the window starts negative.
        mTimeLine->setFrame(core::Frame(mTimeLine->playbackRange().min()));
    } else if (aType == PlayBackWidget::PushType_Fast) {
        // Land on the effective playback range's end (the temporary window's
        // end while it overrides) — the project range's max would fall short
        // when the window extends past it.
        mTimeLine->setFrame(core::Frame(mTimeLine->playbackRange().max()));
    } else if (aType == PlayBackWidget::PushType_Options) {
        openAnimationSettings();
    } else if (aType == PlayBackWidget::PushType_Loop) {
        mTimeLine->setPlayBackLoop(mPlayBack->isLoopChecked());
    }
}

void TargetWidget::openAnimationSettings() {
    if (!mProject)
        return;

    const int curFPS = mProject->attribute().fps();
    const util::Range curRange = mProject->attribute().frameRange();
    const bool curLoop = mPlayBack->isLoopChecked();

    // Show the settings as a Qt::Popup anchored to the playback options button
    // (the EasingPickWidget idiom). The dialog is a pure view created ONCE and
    // reused for every open: it shows/hides (a popup auto-hides on an outside
    // click) and is never destroyed/recreated, so its QML scene is not torn
    // down per-open. The current project/loop state is passed in to showAt() on
    // each open, and is captured as the undo baseline (mLastAnim*) for the
    // single session command pushed on close.
    if (!mAnimSettings) {
        mAnimSettings = new AnimationSettingDialog(this);
        connect(mAnimSettings, &AnimationSettingDialog::changed,
                this, &TargetWidget::applyAnimLive);
        connect(mAnimSettings, &AnimationSettingDialog::applied,
                this, &TargetWidget::applyAnimCommit);
    }
    mLastAnimFps = curFPS;
    mLastAnimRange = curRange;
    mLastAnimLoop = curLoop;

    const QWidget* btn = mPlayBack->optionsButton();
    const QPoint anchor = btn ? btn->mapToGlobal(QPoint(0, btn->height())) : QPoint();
    mAnimSettings->showAt(anchor, curRange, curFPS, curLoop);
}

void TargetWidget::applyAnimLive() {
    if (!mAnimSettings || !mProject)
        return;
    // Live, non-undoable apply: the attribute setters drive onProjectAttributeModified
    // (bone caches) and refreshAnimationSettings() re-syncs the timeline/display/
    // driver, so the effect is visible while the popup is still open. Fires on
    // every edit, including each tick of a scrub drag.
    const int fps = mAnimSettings->fps();
    const util::Range range = mAnimSettings->frameRange();
    const bool loop = mAnimSettings->isLoopChecked();

    mProject->attribute().setFps(fps);
    mProject->attribute().setFrameRange(range);
    mPlayBack->checkLoop(loop);
    mTimeLine->setPlayBackLoop(loop);
    refreshAnimationSettings();
}

void TargetWidget::applyAnimCommit() {
    if (!mAnimSettings || !mProject)
        return;
    // The whole popup session is ONE undo step: push a single command that
    // transitions the project attribute from the pre-open baseline (mLastAnim*)
    // to the current draft. No net change -> nothing to record.
    const int fps = mAnimSettings->fps();
    const util::Range range = mAnimSettings->frameRange();
    const bool loop = mAnimSettings->isLoopChecked();
    if (fps == mLastAnimFps && range == mLastAnimRange && loop == mLastAnimLoop)
        return;

    cmnd::ScopedMacro macro(mProject->commandStack(), CmndName::tr("Change animation settings"));
    core::Project* projectPtr = mProject;
    auto* command = new cmnd::Delegatable(
        [=]() {
            projectPtr->attribute().setFps(fps);
            projectPtr->attribute().setFrameRange(range);
            mPlayBack->checkLoop(loop);
            mTimeLine->setPlayBackLoop(loop);
            this->refreshAnimationSettings();
        },
        [=]() {
            projectPtr->attribute().setFps(mLastAnimFps);
            projectPtr->attribute().setFrameRange(mLastAnimRange);
            mPlayBack->checkLoop(mLastAnimLoop);
            mTimeLine->setPlayBackLoop(mLastAnimLoop);
            this->refreshAnimationSettings();
        });
    projectPtr->commandStack().push(command);
}

void TargetWidget::refreshAnimationSettings() {
    if (auto* menu = mViaPoint.mainMenuBar()) {
        // Same fan-out as the project attribute dialogs: display, timeline
        // editor and playback driver all re-read the attributes.
        menu->onProjectAttributeUpdated();
        menu->onVisualUpdated();
    }
    // FPS changes the ruler strings (relative-to-FPS / timecode formats).
    mTimeLine->triggerOnTimeFormatChanged();
    // The popup edits the project's attributes, not the key selection, so no
    // selection signal fires: re-read the bar's timeline-derived state here - the
    // quick easings' availability and active type, and (with the panel) the
    // channels button's words - so the header cannot lag the change.
    mHeader->refreshFrame();
}

} // namespace gui
