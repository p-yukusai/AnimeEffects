#include "gui/GraphPanelWidget.h"

#include "ctrl/AutoScroll.h"

#include <QDateTime>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QWheelEvent>

#include <QCanvasPainter>

#include "core/Frame.h"
#include "core/ObjectNode.h"
#include "core/TimeLineEvent.h"
#include "ctrl/Driver.h"
#include "ctrl/GraphEditor.h"
#include "ctrl/graph/graph_Geometry.h" // PickKind: the shared pick the hover repaint compares
#include "gui/GUIResources.h"
#include "gui/InkCanvas.h"
#include "gui/MainDisplayWidget.h"
#include "gui/MouseSetting.h"
#include "gui/PanelA11y.h"
#include "gui/theme/Colors.h"
#include "gui/theme/Graph.h" // the palette channelRows() builds the panel's model with

namespace gui {

//-------------------------------------------------------------------------------------------------
GraphPanelWidget::GraphPanelWidget(GUIResources& aResources, QWidget* aParent):
    QCanvasPainterWidget(aParent),
    mResources(aResources),
    mThemeSlot(),
    mDriver(nullptr),
    mFrame(),
    mCursor(),
    mPointer(-1, -1) {
    this->setObjectName(QStringLiteral("GraphPanelWidget"));
    // THE PANEL CAN OWN THE KEYBOARD. It has two keys of its own (Ctrl+A and Escape,
    // see panelKeyAction) and they are the panel's only while the panel is the focus
    // widget, so a press inside it has to make it one: without a focus policy Qt gives
    // a click focus to nobody (QWidget's default is Qt::NoFocus), the panel never sees
    // a key press, and the two actions are unreachable by construction. Qt::StrongFocus is
    // what the timeline page beside it uses (TimeLineWidget), so the dock's two pages
    // take the keyboard the same way.
    this->setFocusPolicy(Qt::StrongFocus);
    mMarqueeScrollTimer.setInterval(ctrl::autoscroll::kIntervalMs);
    mMarqueeScrollTimer.setTimerType(Qt::PreciseTimer);
    this->connect(&mMarqueeScrollTimer, &QTimer::timeout, this, &GraphPanelWidget::autoScrollMarquee);
    // The panel's own hover states need moves with no button down: the hovered curve
    // gets its chip, the tangent handle nearest the pointer is the one drawn
    // highlighted, and the scroll-bar lane under the pointer paints its pill hot (all
    // read the pointer this host records from these moves).
    this->setMouseTracking(true);

    // The accessible factory goes in HERE, from the constructor, for the reason
    // gui::MainDisplayWidget's constructor does it (gui/PanelA11y.h, "WHERE TO CALL IT"):
    // a widget that is shown or parented while accessibility is active caches its plain
    // interface first, and no factory installed later is reached for it - so the panel's
    // publication (gui::publishPanelAccessibility, from this host's first frame) would
    // find nothing to take over. It needs no GL context and no model.
    a11y::installFactoryOnly(*this);
    // AND THAT IS THE WHOLE INSTALLATION: this widget is the panel's only host, so
    // there is no second widget that could take the factory (and no hand-over to get
    // wrong). The action callback arrives with the first frame (see
    // publishAccessibility).

    // The floor is this host's own fill colour: the Canvas painter clears its texture with
    // it each frame, so it must be right before the first frame (see
    // onThemeUpdated for why it lives here and not in the ink).
    setFillColor(theme::Colors::current().base);
    mThemeSlot = mResources.onThemeChanged.connect(this, &GraphPanelWidget::onThemeUpdated);
}

GraphPanelWidget::~GraphPanelWidget() {
    mResources.onThemeChanged.disconnect(mThemeSlot);
    mFrame = PanelFrameState();
}

void GraphPanelWidget::setDriver(ctrl::Driver* aDriver) {
    mDriver = aDriver;

    // A NEW driver is a NEW panel (the driver owns the editor and is recreated per
    // project), so nothing of the old one may survive in this host: the last frame's
    // model and editor are dropped, or a hit between this call and the next paint would
    // be tested against a panel that is gone.
    mFrame.model = GraphPanelModel();
    mFrame.editor = nullptr;
    refresh();
}

void GraphPanelWidget::refresh() {
    // The page's content has changed, and the channel SET is part of that content: the
    // header's channels control is a widget of another host, so it has to be told.
    this->update();
}

void GraphPanelWidget::onThemeUpdated(theme::Theme&) {
    // THE FLOOR OF AN RHI WIDGET IS ITS FILL COLOUR: the Canvas host clears its texture with it
    // each frame and draws no background of its own, so a palette change has to reach it here -
    // MEASURED: without this the page painted BLACK (the default) under a `base` floor, which read
    // as "the panel lost its floor" in the app screenshot.
    setFillColor(theme::Colors::current().base);
    this->update();
}

//-------------------------------------------------------------------------------------------------

namespace {
// The panel's pointer, as the ink wants it: the widget's own tracked position and buttons.
gui::ink::Pointer inkPointerOf(const QPoint& aPosition, const QPoint& aPressPosition, bool aLeftDown) {
    gui::ink::Pointer pointer;
    pointer.pos = QPointF(aPosition);
    pointer.pressPos = QPointF(aPressPosition);
    pointer.leftDown = aLeftDown;
    return pointer;
}
} // namespace

void GraphPanelWidget::paint(QCanvasPainter* aPainter) {
    // THE FLOOR FIRST, and always: the panel draws no background of its own (there is no fill
    // anywhere in drawPanel), and this page's floor is the dock's opaque panel colour
    // (theme::Colors::base), read per frame so a theme change needs no signal. THE STYLE IS SET
    // HERE, not inherited: a first version filled the rect with whatever fill style was current,
    // which at the top of a frame is Canvas's default - MEASURED in the app: the page painted
    // BLACK under a #262626 dock, which reads as "the panel lost its floor".
    aPainter->setFillStyle(theme::Colors::current().base);
    aPainter->fillRect(QRectF(0, 0, width(), height()));

    ctrl::GraphEditor* editor = graphEditor();
    if (!editor)
        return;

    // THE EDITOR'S GEOMETRY PINNED TO THIS WIDGET: the frame window is mapped in pixels
    // computed from THIS page's width, so a mapping left by the viewport or by a previous
    // size must not be what the panel is built from.
    editor->syncFrameWindowFor(this->size());

    const PanelFrame frame =
        composePanelFrame(editor, true, this->size(), mFrame, this->font(), GraphPanelHost::Dock);
    gui::ink::InkCanvas ink(*aPainter, this->font());
    gui::drawPanel(ink, frame.model, frame.palette, frame.focus, inkPointerOf(mPointer, mPressPointer, mLeftDown));

    publishAccessibility();
    // WHAT THE PAGE DREW IS WHAT THE BAR MAY NAME. The dock header's channels control is a
    // widget of another host, so it cannot read this page's model; it is told here, from the
    // frame that was just composed. Measured 2026-10-23: asking the editor at refresh() time
    // is too EARLY - a tree selection requests the repaint before the dock hands the new
    // targets over, so the count came out 0 over three drawn curves, and only the next tab
    // switch corrected it.
    this->publishChannels(frame.model.channels);
}


void GraphPanelWidget::hideEvent(QHideEvent* aEvent) {
    mMarqueeScrollTimer.stop();
    mMarqueeDwell.reset();
    QCanvasPainterWidget::hideEvent(aEvent);

    // A GESTURE LEFT OPEN GOES NO FURTHER. The tab can be switched away from with a key
    // move, a handle drag, a marquee or a pan in flight (the release usually beats the
    // click, but not always), and a gesture kept open would coalesce the next edit into a
    // command whose geometry nobody can see any more - so the editor drops it here
    // (ctrl::GraphEditor::panelHidden: endDrag(), and nothing else). The keys, the
    // selection and the scale are kept, so the page coming back is the panel as it was,
    // and the command a dropped gesture had already pushed stays on the undo stack -
    // dropping the gesture is not an undo.
    if (ctrl::GraphEditor* editor = graphEditor())
        editor->panelHidden();

    // Another page of the dock is current: the panel is not on screen, and the ONE
    // accessibility publication has to say so rather than leave the tree showing the last
    // frame of a hidden tab - the installation is this widget's for the process, so this
    // empties it. A host that was never shown publishes nothing at all: the shared
    // function tells the two apart.
    PanelFrameState offScreen;
    publishPanelAccessibility(*this, offScreen, &GraphPanelWidget::accessibilityAction, this);
}

//-------------------------------------------------------------------------------------------------
ctrl::GraphEditor* GraphPanelWidget::graphEditor() const {
    return mDriver ? mDriver->graphEditor() : nullptr;
}

core::CameraInfo GraphPanelWidget::panelCamera() const {
    // ctrl::GraphEditor derives its panel rect, and every hit-test and gesture it runs,
    // from aCamera.screenSize() alone (GraphEditor::currentPanelRect reads nothing else),
    // so this is the whole of the camera state the panel's own geometry needs - and it
    // must be THIS widget's size, because the panel drawn here is this widget. The world
    // transform is never read on the graph's cursor path; a default CameraInfo (scale 1,
    // no rotation) is therefore not a stand-in for a scene camera, it is the absence of
    // one, which is what the panel asks for.
    core::CameraInfo camera;
    camera.setScreenSize(this->size());
    camera.setDevicePixelRatio(this->devicePixelRatioF());
    return camera;
}

bool GraphPanelWidget::panelTakesCursor() const {
    ctrl::GraphEditor* editor = graphEditor();
    return editor && editor->ownsCursor(panelCamera(), mCursor);
}

void GraphPanelWidget::routeCursor() {
    ctrl::GraphEditor* editor = graphEditor();
    if (!editor || !panelTakesCursor())
        return; // this point is nobody's (or the panel is gone): nothing acts on it

    // THE EDITOR ITSELF, and not through the viewport: ctrl::Driver routes the viewport's
    // events to the tools and no longer arbitrates the panel at all, and ctrl::GraphEditor
    // has no shown/hidden flag left to gate on - this page IS the panel while its tab is
    // current, which is the only "visible" there is. The editor's own contract is
    // unchanged: ownsCursor above is its arbitration, updateCursor its pass, and the camera
    // it is handed is this widget's (see panelCamera()).
    if (editor->updateCursor(panelCamera(), mCursor))
        this->update(); // the editor reports it changed something (selection, window, document)
}

//-------------------------------------------------------------------------------------------------
void GraphPanelWidget::mouseMoveEvent(QMouseEvent* aEvent) {
    // The pointer is recorded HERE, from the event stream this host already reads, and the
    // frame is handed it: the panel paints when it is asked to, not at the event rate, so
    // the position it inks must be this host's latest, not one cached by a frame.
    mPointer = aEvent->position().toPoint();

    ctrl::GraphEditor* editor = graphEditor();
    if (!editor) {
        aEvent->ignore();
        return;
    }

    // The panel's gestures read the modifier state from here: neither
    // core::AbstractCursor nor the editor's updateCursor carries modifiers, so every
    // move re-reads them, which is what lets Shift constrain a key drag already in
    // flight (see ctrl::GraphEditor::setModifiers).
    editor->setModifiers(aEvent->modifiers());
    mCursor.setMouseMove(aEvent, panelCamera());
    routeCursor();

    // AND THE HOVER'S OWN INK. routeCursor() repaints only when the editor's cursor pass
    // reports a change (a bare move answers false, GraphEditor's own rule), while the
    // panel draws three things from the pointer alone: the marker under it, the nearest
    // handle, and the scroll-bar lane whose pill turns hover-coloured. A move that
    // changed which of those is lit has to be painted, and a move that did not must not
    // be (see updateHoverRepaint).
    this->updateHoverRepaint();
    updateMarqueeAutoScroll();
    aEvent->accept();
}

void GraphPanelWidget::updateMarqueeAutoScroll() {
    ctrl::GraphEditor* editor = graphEditor();
    if (!mLeftDown || !editor || !editor->hasMarquee()) {
        mMarqueeScrollTimer.stop();
        mMarqueeDwell.reset();
        return;
    }
    if (editor->marqueeRect().isEmpty()) {
        // A press arms the marquee but has not dragged yet. It is still the baseline
        // for "did the pointer move TOWARD an edge?", so record it without scrolling.
        mMarqueeScrollTimer.stop();
        mMarqueeDwell.reset();
        mMarqueeDwell.observe(mPointer, editor->scrollViewport(), QDateTime::currentMSecsSinceEpoch());
        return;
    }
    // Direction is part of the gate: a horizontal drag along the top edge does not
    // scroll the value axis. `observe` arms only the axes moving toward their edge.
    mMarqueeDwell.observe(mPointer, editor->scrollViewport(), QDateTime::currentMSecsSinceEpoch());
    if (!mMarqueeDwell.inEdge()) {
        mMarqueeScrollTimer.stop();
        return;
    }
    if (!mMarqueeScrollTimer.isActive())
        mMarqueeScrollTimer.start();
}

void GraphPanelWidget::autoScrollMarquee() {
    ctrl::GraphEditor* editor = graphEditor();
    if (!mLeftDown || !editor || !editor->hasMarquee()) {
        mMarqueeScrollTimer.stop();
        mMarqueeDwell.reset();
        return;
    }
    // THE DWELL: the first ticks in an edge only arm it, so a drag that STARTS on
    // an edge (or merely crosses it) does not jump the view. The graph's own edge
    // test is still the editor's; this gate only decides when it may act.
    const ctrl::autoscroll::Step step =
        mMarqueeDwell.update(mPointer, editor->scrollViewport(), QDateTime::currentMSecsSinceEpoch());
    if (step.isNull()) {
        if (!mMarqueeDwell.inEdge())
            mMarqueeScrollTimer.stop();
        return;
    }
    // The editor owns the two window mappings (shared frame axis included); this
    // host only paints what it changed. False means "already at the content bound".
    if (!editor->autoScrollMarquee()) {
        mMarqueeScrollTimer.stop();
        mMarqueeDwell.reset();
        return;
    }
    this->update();
}

void GraphPanelWidget::mousePressEvent(QMouseEvent* aEvent) {
    mPointer = aEvent->position().toPoint();
    if (aEvent->button() == Qt::LeftButton) {
        mPressPointer = mPointer;
        mLeftDown = true;
        mMarqueeDwell.reset();
    }
    ctrl::GraphEditor* editor = graphEditor();
    if (!editor) {
        aEvent->ignore();
        return;
    }

    const QPoint pos = aEvent->position().toPoint();

    // A right press is the MENU gesture - the panel's only right-button action - and it
    // is taken here because this is the only place this host sees one. On a tangent
    // handle it is the handle-type menu, on a key the key menu, on the plot's
    // background the timeline's own background menu plus the graph's "Fit graph" entry
    // (see GraphPanelWidget::openKeyMenuAt, which owns the one pick that decides).
    // All of them are QMenus (QMenu::exec), so they run their own event loop and
    // return when dismissed, and the press is accepted once they have. (A left press
    // has no control to test any more: the panel's one control is the dock header's
    // channels button now.)
    if (aEvent->button() == Qt::RightButton) {
        openKeyMenuAt(pos);
        aEvent->accept();
        return;
    }

    editor->setModifiers(aEvent->modifiers());
    mCursor.setMousePress(aEvent, panelCamera());
    routeCursor();
    if (editor->hasMarquee())
        updateMarqueeAutoScroll();
    aEvent->accept();
}

void GraphPanelWidget::mouseReleaseEvent(QMouseEvent* aEvent) {
    mMarqueeScrollTimer.stop();
    mMarqueeDwell.reset();
    mPointer = aEvent->position().toPoint();
    if (aEvent->button() == Qt::LeftButton)
        mLeftDown = false;
    ctrl::GraphEditor* editor = graphEditor();
    if (!editor) {
        aEvent->ignore();
        return;
    }

    editor->setModifiers(aEvent->modifiers());
    mCursor.setMouseRelease(aEvent, panelCamera());
    // A gesture in flight owns the release wherever it lands (the editor says so in
    // ownsCursor), which is what ends a drag that left the widget.
    routeCursor();
    aEvent->accept();
}

void GraphPanelWidget::wheelEvent(QWheelEvent* aEvent) {
    ctrl::GraphEditor* editor = graphEditor();
    if (!editor) {
        aEvent->ignore();
        return;
    }

    editor->setModifiers(aEvent->modifiers());

    // THE ZOOM DIRECTION is the dock's own setting. The panel takes the wheel while its
    // tab is current, and it takes the direction of the SURFACE it is drawn on: this page
    // covers the TIMELINE's body, so it zooms the graph the way the timeline's own wheel
    // zooms it (gui::TimeLineEditorWidget reads MouseSetting::invertZoom), and one wheel in
    // one body cannot turn the frame axis two
    // ways. The setting is read from the application's settings here (the same file
    // gui::GeneralSettingDialog writes and gui::ViaPoint's live copy is kept in step
    // with), because this host is handed neither.
    MouseSetting settings;
    settings.load();
    if (editor->updateWheel(
            panelCamera(), aEvent->position().toPoint(), aEvent->angleDelta().y(), aEvent->modifiers(),
            settings.invertZoom
        )) {
        this->update();
    }
    aEvent->accept();
}

void GraphPanelWidget::tabletEvent(QTabletEvent* aEvent) {
    mPointer = aEvent->position().toPoint();

    ctrl::GraphEditor* editor = graphEditor();
    if (editor) {
        editor->setModifiers(aEvent->modifiers());
        if (mCursor.setTabletEvent(aEvent, panelCamera()))
            routeCursor();
    }

    // NOT ACCEPTED: the panel has no text input to claim the pen's stream, and a platform
    // that synthesizes a mouse event from a pen must let this widget's mouse path see it.
    PanelHostBase::tabletEvent(aEvent);
}

void GraphPanelWidget::keyPressEvent(QKeyEvent* aEvent) {
    // THE PANEL'S OWN TWO KEYS, for a press that got this far unconsumed. Consumed
    // exactly when the panel acted on it (see panelKeyAction): a Ctrl+A or an Escape
    // that changed nothing is not this widget's to swallow.
    if (panelKeyAction(*aEvent)) {
        aEvent->accept();
        return;
    }

    // Otherwise the event is ignored, i.e. it propagates to the main window's key map,
    // which is where the application's key routing has always been.
    PanelHostBase::keyPressEvent(aEvent);
}

bool GraphPanelWidget::panelKeyAction(const QKeyEvent& aEvent) {
    // WHO HAS THE KEYBOARD. The two actions edit the panel's own selection, so they
    // belong to the panel only while the panel is what the user is looking at: Ctrl+A
    // here is the panel's Ctrl+A, not a global shortcut, and while some other widget
    // owns the keyboard (the object tree, the timeline, a property field) its Ctrl+A
    // and its Escape are that widget's business. A key this panel did not consume must
    // reach gui::MainWindow's key map exactly as it always has. hasFocus() is also what
    // keeps a press that ARRIVED here by propagation honest: Qt sends a key up the
    // parent chain only after the focus widget ignored it, so a forwarded press would
    // otherwise act on the panel while the user's focus is elsewhere.
    //
    // NOTHING IS NEEDED FOR A QMENU OR THE TIMELINE'S EASING PICKER, which is the
    // other half of "Escape belongs to whoever consumed it": both are Qt popups that
    // hold the keyboard while they are up, and the popup takes the key ON THE WINDOW'S
    // OWN ROUTING - a path QApplication::sendEvent (and every test that injects a
    // QKeyEvent) does not enter. Measured against Qt 6.11 for this binding: with a QMenu
    // popped up, an Escape delivered through the window closed the menu and the focused
    // widget's handler was NOT called, while the very same widget kept hasFocus() true
    // throughout. That is also why aEvent.isAccepted() is not consulted: an event
    // something else consumed is never delivered here, and a delivered key event arrives
    // ACCEPTED by Qt's own default (QEvent's m_accept), the flag QWidget::keyPressEvent
    // is what clears.
    if (!this->hasFocus())
        return false;

    ctrl::GraphEditor* editor = graphEditor();
    if (!editor)
        return false; // no project on screen: the panel has no selection to edit

    // THE TWO BINDINGS, with the application's own rule for what a binding is
    // (ctrl::KeyBinding::matchesExactlyWith): the key and the modifiers exactly -
    // Ctrl+A is not Ctrl+Shift+A, and a bare Escape is not Escape-with-a-modifier.
    // Both actions ANSWER whether they changed the selection, and that answer is what
    // this returns: the panel consumes the press it acted on and leaves the rest.
    if (aEvent.key() == Qt::Key_A && aEvent.modifiers() == Qt::ControlModifier)
        return editor->selectAllMarkers();
    if (aEvent.key() == Qt::Key_Escape && aEvent.modifiers() == Qt::NoModifier)
        return editor->clearSelection();
    return false;
}

//-------------------------------------------------------------------------------------------------
bool GraphPanelWidget::openKeyMenuAt(const QPoint& aPos) {
    ctrl::GraphEditor* editor = graphEditor();
    if (!editor)
        return false; // no panel on screen: nothing to hit

    MainDisplayWidget* display = appDisplay();
    if (!display)
        return false; // no viewport in this window: no menu to open

    // ONE PICK, THE EDITOR'S OWN PRESS RULE. ctrl::GraphEditor::contextTargetAt asks the
    // hit-test a left press obeys - the same nearest-and-normalised comparison between
    // a key's grab radius and a handle's, which is also what the hover ink lights - so
    // the menu names exactly the thing under the pointer, and a HANDLE that is
    // relatively nearer than a key opens the handle menu instead of that key's. (The
    // old right-click asked gui::panelKeyAt, a keys-only scan: a handle inside a key's
    // 10px ring opened the KEY menu, whose items then acted on a key the user had not
    // aimed at.)
    const ctrl::GraphEditor::ContextTarget target = editor->contextTargetAt(panelCamera(), aPos);

    if (target.kind == ctrl::GraphEditor::ContextTarget::Kind_Handle) {
        // THE HANDLE'S OWN MENU: that side's five types. A handle has no key menu,
        // because every entry there (copy, paste, spacing, delete) is about KEYS.
        const bool opened = display->showGraphHandleMenu(target, this->mapToGlobal(aPos));
        this->update();
        return opened;
    }

    if (target.kind == ctrl::GraphEditor::ContextTarget::Kind_Key) {
        core::TimeLine* line = target.node ? target.node->timeLine() : nullptr;
        if (!line)
            return false; // an object with no keys: nothing for a key menu to act on

        // THE MENU IS THE TIMELINE'S OWN, and the key is handed over as the triple the
        // timeline names a key by - (node, type, frame), the same currency
        // ctrl::time::Selection speaks. The timeline applies its own right-click rule to
        // it against the one shared selection: a key already selected keeps the WHOLE
        // selection, so with N keys selected every item acts on all N of them; any other
        // key is lent to the menu for its lifetime and dropped when it closes (see
        // TimeLineEditorWidget::openKeyMenu, which owns both halves of the rule).
        //
        // THE ANCHOR: this widget's own point, in GLOBAL (screen) coordinates. The two
        // widgets are SIBLINGS - MainDisplayWidget owns the viewport, this page lives in
        // the dock's tab stack beside it - so a QWidget::mapFrom on that widget is not a
        // mapping between them at all: it reads this widget's point as one of ITS OWN
        // children's coordinates and translates it out of the wrong parent chain.
        // Screen coordinates are the one space two siblings share.
        const core::TimeLineEvent::Target pressed(
            *target.node, core::TimeKeyPos(*line, target.channel.type(), target.frame.get()), target.frame.get()
        );
        const bool opened = display->showGraphKeyMenu(pressed, this->mapToGlobal(aPos));

        // The menu blocked (the timeline's own menu execs), so the selection it acted on
        // may have changed under the panel's ink by now.
        this->update();
        return opened;
    }

    // NEITHER: the plot's background, and the panel offers what the TIMELINE offers on
    // its own empty lane background - not nothing - plus the graph's own "Fit graph".
    // The route reads the SHARED SELECTION for its items; it no longer re-hit-tests the
    // timeline's lanes with this widget's pixels (that named a different key of another
    // row whenever the two pages' layouts differed, which is what made a menu's actions
    // look dead: they edited a key that was not the one under the pointer).
    const bool opened = display->showGraphBackgroundMenu(this->mapToGlobal(aPos));
    this->update();
    return opened;
}

MainDisplayWidget* GraphPanelWidget::appDisplay() const {
    // The application's QMenus live in the viewport widget, and the panel's host
    // interface carries no reference to it (this widget is constructed with a
    // GUIResources and a parent). There is exactly ONE per window - the dock this page
    // lives in is in the same one - so the window is asked for it. Nothing else about the
    // viewport is used: no editor state, no camera, no GL.
    QWidget* top = this->window();
    return top ? top->findChild<MainDisplayWidget*>() : nullptr;
}

//-------------------------------------------------------------------------------------------------
void GraphPanelWidget::publishAccessibility() {
    publishPanelAccessibility(*this, mFrame, &GraphPanelWidget::accessibilityAction, this);
}

void GraphPanelWidget::accessibilityAction(void* aUser, int aId) {
    static_cast<GraphPanelWidget*>(aUser)->onAccessibilityAction(aId);
}

void GraphPanelWidget::onAccessibilityAction(int aId) {
    if (!mFrame.editor)
        return;

    if (aId == kPanelFitItemId) {
        // Both axes to the shown data - the action the panel's "Fit graph"
        // context-menu entry performs. (It used to be the plot's double-click,
        // removed because it is too easy to trigger by accident.)
        mFrame.editor->fitAll();
        this->update();
        return;
    }

    if (aId == kPanelKeyMenuItemId) {
        // The route the pointer takes, at the key's own position: the menu is anchored
        // where the key is drawn, so an assistive activation puts it exactly where a
        // right-click on that key would. The selection is re-read here because a frame
        // may have passed since the item was published.
        //
        // The key the menu is opened ON is the selected key the dot was found for, and
        // it IS in the selection, so the timeline's rule leaves the selection standing:
        // the menu acts on the whole selection, exactly as it does for a right-click on
        // a selected key. (The anchor is the same screen position the pointer's route
        // hands over - mapToGlobal from this widget.)
        core::TimeLineEvent selection;
        QPoint dot;
        if (!mFrame.editor->retrieveSelection(selection) || selection.targets().isEmpty()
            || !panelSelectedKeyDot(mFrame.model, selection, dot))
            return; // the selection moved on: nothing to open the menu on
        MainDisplayWidget* display = appDisplay();
        if (display)
            display->showGraphKeyMenu(selection.targets().front(), this->mapToGlobal(dot));
        return;
    }
}

//-------------------------------------------------------------------------------------------------
void GraphPanelWidget::publishChannels(const QVector<PanelChannelRow>& aRows) {
    const auto same = [](const QVector<PanelChannelRow>& aLhs, const QVector<PanelChannelRow>& aRhs) {
        if (aLhs.size() != aRhs.size())
            return false;
        for (int i = 0; i < aLhs.size(); ++i) {
            const PanelChannelRow& l = aLhs.at(i);
            const PanelChannelRow& r = aRhs.at(i);
            if (l.layer != r.layer || l.group != r.group || l.name != r.name || l.visible != r.visible ||
                l.focused != r.focused || l.color != r.color)
                return false;
        }
        return true;
    };
    if (same(mPublishedChannels, aRows))
        return;
    mPublishedChannels = aRows;
    emit channelsChanged(aRows);
}

QVector<PanelChannelRow> GraphPanelWidget::channelRows() const {
    // THE PANEL'S OWN MODEL, built for the LIST rather than for a frame: the rows carry
    // (layer, legend group, name, hue, visibility, sticky focus) and NOTHING about
    // geometry, so the viewport size only has to be a real one - the plot it would map
    // is discarded with the rest of the model. Built on demand from the live editor (not
    // read off the last drawn frame) because the dock header may ask before this page
    // has painted: switching to the Graph tab and clicking the button immediately is a
    // normal thing to do, and a stale (or empty) row list would open a list that does
    // not match the panel.
    ctrl::GraphEditor* editor = graphEditor();
    if (!editor)
        return {}; // no project: no channels

    // THE ROWS ARE GEOMETRY-FREE (layer, group, name, hue, visibility, focus), so the
    // viewport handed to the builder only has to be a REAL one: the builder publishes
    // no rows at all when its plot comes out empty, which is exactly the page that has
    // not been laid out yet - tab switched to and the header's button read before the
    // first paint. A fixed viewport makes this path independent of that.
    const QSize viewport(800, 600);
    const theme::Graph palette = theme::Graph::from(theme::Colors::current());
    return buildGraphPanelModel(*editor, viewport, palette, 0, GraphPanelHost::Dock).channels;
}

//-------------------------------------------------------------------------------------------------
void GraphPanelWidget::updateHoverRepaint() {
    // THE BOUNDED RULE: repaint when the target under the pointer CHANGES, and not on a
    // move that keeps the same one. The panel's pointer-driven ink is three marks
    // (drawPanel reads the pointer this host hands it): the marker or
    // tangent handle the pointer lights - gui::panelPickAt, the same query and the same
    // ctrl::graph::pickTarget comparison drawPanel inks and GraphEditor::hitTest
    // presses with, so the key and the handle are ONE answer and never both - and the
    // scroll-bar lane whose pill takes theme::Colors::hover while the pointer is on it
    // (gui::panelBarAt, the same answer drawPanel paints from). Everything else the
    // panel draws is the document's or a gesture's and has its own repaint; a move over
    // empty space changes none of the three and therefore paints nothing.
    HoverTarget now;

    // The bar identity is read off the published frame's own tracks, so it needs no
    // editor (an empty model has no tracks and answers None). It is asked FIRST and
    // independently of the plot hits: the lanes are outside the plot, the very place
    // where no key or handle can change, so without this a pointer entering a bar
    // changed nothing the widget compared and the pill's hover colour never appeared
    // until some other repaint happened to run.
    now.bar = panelBarAt(mFrame.model, mPointer);

    ctrl::GraphEditor* editor = graphEditor();
    if (editor && mFrame.editor) {
        // ONE PICK FOR BOTH MARKS, and it is the drawing layer's own: gui::panelPickAt
        // answers which class of target the pointer names - the key OR the tangent
        // handle a press at this point selects - through ctrl::graph::pickTarget, the
        // comparison GraphEditor::hitTest presses with. The widget therefore schedules
        // a repaint for exactly the target drawPanel would ink; it no longer scans the
        // handles itself and then asks for the nearest key, which is the disagreement
        // that let the ink light a handle a press would not take.
        const PanelPick pick = panelPickAt(mFrame.model, mPointer);
        if (pick.kind == ctrl::graph::PickKind::Key) {
            now.key = true;
            now.keyNode = pick.node;
            now.keyChannel = pick.channel;
            now.keyFrame = pick.frame.get();
        } else if (pick.kind == ctrl::graph::PickKind::Handle) {
            now.handleCurve = pick.curve;
            now.handle = pick.handle;
        }
    }

    if (now != mHover) {
        mHover = now;
        this->update();
    }
}

void GraphPanelWidget::leaveEvent(QEvent* aEvent) {
    PanelHostBase::leaveEvent(aEvent);

    // The hover is the POINTER's and the pointer is gone: the host parks it outside the
    // panel (the same "nothing hovers" point it starts at, see mPointer) and drops the
    // lit target with it, so the next frame paints neither mark. A move that keeps
    // arriving - a drag with the mouse grabbed reports the pointer outside the widget
    // too - re-establishes both through mouseMoveEvent.
    //
    // AND THE POINTER IS PARKED WHATEVER mHover SAYS. mPointer is not only the key-marker
    // and handle hover: drawPanel's CHIPS pass reads it too, and its rule is the curve
    // whose value at the pointer's own column
    // is within kChipHoverDistance of the pointer. A chip is not part of HoverTarget,
    // so gating the park on mHover left the last IN-WIDGET position standing whenever no
    // marker or handle was lit, and any frame painted after the pointer had left kept
    // naming that curve (MEASURED: a 35x16 box at the plot's right edge, 556px, holding
    // the "Opacity" chip, against 0px once a marker had been lit before the pointer
    // left). Parking unconditionally is the whole of "the pointer is gone": nothing
    // pointer-driven survives it, and a repaint is asked for when anything was set.
    const bool wasSet = (mPointer != QPoint(-1, -1));
    const bool hovered = (mHover != HoverTarget());
    mHover = HoverTarget();
    mPointer = QPoint(-1, -1);
    if (wasSet || hovered)
        this->update();
}

} // namespace gui
