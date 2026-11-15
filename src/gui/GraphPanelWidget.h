#ifndef GUI_GRAPHPANELWIDGET_H
#define GUI_GRAPHPANELWIDGET_H

// THE HOST IS SWAPPABLE, EXACTLY AS THE TIMELINE'S SURFACE IS (TimeLineEditorWidget): the panel's
// ink is backend-neutral (gui/Ink.h), so this class is a DRAWING SURFACE plus the panel's input
// arbitration, and which base class it has decides who rasterises. Canvas Painter needs a
// QCanvasPainterWidget (an RHI widget); a Qt without the module keeps the Dear ImGui host, which
// needs a QOpenGLWidget.
#if defined(AE_CANVAS_PAINTER) && AE_CANVAS_PAINTER
#include <QtCanvasPainter/QCanvasPainterWidget>
class QCanvasPainter;
#else
#include <QtOpenGLWidgets/QOpenGLWidget>
#endif
#include <QPoint>
#include <QScopedPointer>
#include <QTimer>
#include <QVariant>

#include "core/AbstractCursor.h"
#include "core/CameraInfo.h"
#include "ctrl/AutoScroll.h"
#include "gui/GraphPanelFrame.h"
#include "util/SlotId.h"

class QHideEvent;
class QInputMethodEvent;
class QKeyEvent;
class QMouseEvent;
class QTabletEvent;
class QTouchEvent;
class QWheelEvent;

namespace ctrl {
class Driver;
}
namespace theme {
class Theme;
}

namespace gui {

class GUIResources;
class ImGuiInputBridge;
class MainDisplayWidget;

// THE PANEL'S HOST: the animation graph panel drawn in the TIMELINE DOCK, as the
// Graph tab's page (design section 5). It is a QOpenGLWidget because the panel is
// drawn with Dear ImGui into a GL framebuffer, and it is a HOST and not a renderer:
// the frame it runs is gui::runPanelFrame (the io setup, the backend's NewFrame,
// gui::buildGraphPanelModel, gui::drawPanel) plus gui::publishPanelAccessibility, and
// the ImGui context, GL3 backend and font atlas are that file's (see the frame driver
// in gui/GraphPanelFrame.h). Nothing about the panel is re-implemented here. It is the
// ONLY host - the viewport overlay that used to draw the panel over the artwork is
// gone.
//
// WHAT IT DRAWS, AND WHAT ELSE IS ON ITS FLOOR. The panel draws no background of its
// own ("the panel must leave the artwork showing through") - and here there is no
// artwork to show through: this widget's floor is the DOCK's, opaque, and it is
// painted by this widget because a QOpenGLWidget is composited over the window and the
// widgets beside it do not show through it. The colour is the theme's panel/dock floor
// (theme::Colors::base, the reference's own _floor: cssVar('--base')), read per frame
// so a theme change needs no signal. NOTHING ELSE is drawn: no title (the tab names
// the page), no control of its own - the panel's ONE control (the channel list) is a
// VIEW action and the design puts it in the DOCK HEADER, beside the zoom group, so
// this page draws no button and HIT-TESTS none. And no scrim (an
// opaque floor has nothing to sit on, so the contrast-outline composite gui::outline
// offers - the pass the removed overlay host ran - is NOT run here).
//
// ONE FRAME AXIS, ONE CURRENT FRAME, TWO VIEWS. The editor this host draws from is the
// ONE ctrl::GraphEditor the driver owns, and it already carries the timeline's shared
// state (MainDisplayWidget::
// syncGraphPanelSharedState wired the frame window, the selection source and the two
// horizontal delegates). What a HOST must still do is pin the mapping to its own
// geometry: the frame window is mapped in pixels, computed from the plot's width, so
// paintGL() calls ctrl::GraphEditor::syncFrameWindowFor() with THIS widget's size
// before the model is built - the same call MainDisplayWidget::paintEvent makes for the
// same reason. Nothing here sets the frame window or the selection.
//
// INTERACTION. Every Qt event goes to the panel's own gui::ImGuiInputBridge first
// (ImGuiIO), and the bridge's answer is the panel's key routing rule: a rebindable
// command must not fire while gui::ImGuiInputBridge::keyboardCaptured() is true, and that
// flag is read off the ONE ImGui context, so a text field anywhere in the application
// holds the keyboard for this page too. The panel's own gestures then go to the EDITOR -
// ctrl::GraphEditor::ownsCursor/updateCursor/updateWheel, reached through the driver's
// graphEditor() - and not through the viewport at all: ctrl::Driver no longer routes a
// panel event (the viewport's cursor, wheel and keys are the tools' alone), and the
// editor has no shown/hidden flag left to gate on, because THIS PAGE is the panel
// whenever its tab is current. The two controls the panel's views open - the channel
// list (now the dock header's button, which hands this page's rows to
// gui::MainDisplayWidget) and the timeline's own key menu (a right-click on a key) -
// are opened from gui::MainDisplayWidget, which is the object that has the timeline
// and its menus; see appDisplay() below.
//
// THE PANEL'S TWO KEYS. Ctrl+A selects every marker of the shown channels and Escape
// clears the selection - ctrl::GraphEditor::selectAllMarkers / clearSelection, the
// keyboard form of the panel's own marquee rule (see GraphEditor.h). Both are WIDGET
// bindings, not application shortcuts: the panel consumes a press only while IT is the
// focus widget (Qt::StrongFocus, so a press inside it makes it the focus widget) and
// only when it acted, and every key it does not consume still propagates to
// gui::MainWindow's key map - the one place the application's own commands fire. The
// full rule (and each condition's reason) is on panelKeyAction().
//
// ACCESSIBILITY. The panel has ONE publication for the process (gui::a11y keeps one
// installation) and this is its only host: the factory is installed for this widget in
// the CONSTRUCTOR (a11y::installFactoryOnly - the half that has to run before anything
// can ask Qt for the widget's interface), the first frame on screen hands the shared
// publisher the action callback (a repeat install for the same widget: one interface,
// one id), and every frame after that publishes the model that was just drawn. While
// the Graph tab is not on screen the empty model is published, so the tree never shows
// a panel that is not there.
//
// INTERFACE. This class is constructed by the dock (gui::TargetWidget) with the
// application's GUIResources and its parent, is handed the live ctrl::Driver when a
// project is set, and is asked to refresh() when the document changed. That is the
// whole of what the dock calls.
// ONE NAME FOR THE BASE, so every explicit base-class call in the .cpp (`PanelHostBase::hideEvent`,
// `::keyPressEvent`, ...) is spelled once and cannot drift per host.
#if defined(AE_CANVAS_PAINTER) && AE_CANVAS_PAINTER
using PanelHostBase = QCanvasPainterWidget;
#else
using PanelHostBase = QOpenGLWidget;
#endif

class GraphPanelWidget: public PanelHostBase {
    Q_OBJECT
public:
    // THE SHOWN CHANNELS CHANGED. refresh() is the dock's one "this page's content has
    // already changed" call - a new project, a tree selection, a key edit from beside the
    // dock - and every one of those can change WHICH channels the panel draws, so the
    // notification is emitted there once. The header's channels control subscribes: it is
    // the panel's one control, but it lives in the dock's bar, so without this it only ever
    // re-read the count when the TAB changed. MEASURED 2026-10-23: selecting a layer with
    // three keyed channels while the Graph tab was already up left the control reading
    // "0 channel(s)" over three drawn curves.
    Q_SIGNAL void channelsChanged(const QVector<PanelChannelRow>& aRows);

    GraphPanelWidget(GUIResources& aResources, QWidget* aParent = nullptr);
    ~GraphPanelWidget() override;

    // The driver that owns the panel's editor (a driver is created per project, so this
    // is called again for every project, exactly like MainDisplayWidget::setDriver).
    // The editor is asked for at every use and never cached, so a new driver is a new
    // panel with nothing stale in this host: the frame is dropped and a repaint asked
    // for, which is what makes the new project's curves appear.
    void setDriver(ctrl::Driver* aDriver);

    // ASK FOR ONE REPAINT. This page draws from the document and from the project's
    // current frame, and neither touches this widget's pixels on its own, so the dock
    // (gui::TargetWidget) calls this at every moment the panel's content has already
    // changed - one call per change, each after the change's own write:
    //
    //   * TargetWidget::setDriver - a new driver is a new editor (one per project), so
    //     the panel is a new panel; setDriver() refreshes here itself;
    //   * TargetWidget::onSurfaceChanged - the Graph tab became the current page (Qt laid
    //     the page out and painted nothing of it while the timeline was up);
    //   * TimeLineWidget::onFrameUpdated, connected by the dock - the CURRENT FRAME moved
    //     (a transport step, a playback tick, a ruler or playhead gesture); the panel is
    //     the second view of that one frame, so the playhead has to be redrawn;
    //   * ObjectTreeWidget::onSelectionChanged, connected by the dock - the tree picks the
    //     layers the panel draws, and it is on screen in this tab as well;
    //   * core::Project::onTimeLineModified and ::onProjectAttributeModified, connected by
    //     the dock while a project is set - a key edit from beside the dock (the property
    //     panel, or the easing picker's commit) changes the curves, and an fps or frame
    //     range change moves the frame axis.
    //
    // The two properties that make that safe: every one of those notifications is emitted
    // AFTER the state it reports was written (the frame is set before setFrame emits, the
    // command stack executes before its listeners run, an attribute setter assigns before
    // it notifies), so a paint this call schedules never reads a stale value; and this
    // call is only update() - it writes nothing, so a repaint cannot feed
    // any of those notifications back. Duplicate requests (the panel's own gesture already
    // asks for the repaint its command's notification would ask for again) are free: Qt
    // coalesces them into one paint, and a request for a hidden page is dropped.
    void refresh();

    // THE ROWS THE PAGE LAST DREW, published when they change: the header's control names
    // what the panel shows, and the panel is the only place that knows it before the dock
    // hands a new target over (see publishChannels).
    void publishChannels(const QVector<PanelChannelRow>& aRows);

    // THE CHANNEL LIST'S ROWS, for the dock header's channels control: the panel's OWN
    // model rows (gui::PanelChannelRow - layer, legend group, name, hue, visibility and
    // the sticky focus), built on demand from the live editor so the list and the
    // curves cannot disagree about a channel's name, its colour or its state. Empty
    // before a project (no editor, so no channels). The header is the list's host now
    // - the design puts the panel's one control with the dock's view controls - and
    // this is the one thing it needs from the page.
    QVector<PanelChannelRow> channelRows() const;

protected:
#if defined(AE_CANVAS_PAINTER) && AE_CANVAS_PAINTER
    // THE CANVAS PAINT: compose the frame (gui::composePanelFrame) and ink it with
    // gui/InkCanvas. No ImGui anywhere in this path - an RHI widget has no raw GL context for
    // the ImGui backend, and this host needs none: the panel's ink is a vocabulary now.
    void paint(QCanvasPainter* aPainter) override;
#else
    void initializeGL() override;
    void paintGL() override;
#endif
    void hideEvent(QHideEvent* aEvent) override;
    // The pointer left the page: the hover it was drawing goes with it (see
    // updateHoverRepaint).
    void leaveEvent(QEvent* aEvent) override;
    void mouseMoveEvent(QMouseEvent* aEvent) override;
    void mousePressEvent(QMouseEvent* aEvent) override;
    void mouseReleaseEvent(QMouseEvent* aEvent) override;
    void wheelEvent(QWheelEvent* aEvent) override;
    void tabletEvent(QTabletEvent* aEvent) override;
    void keyPressEvent(QKeyEvent* aEvent) override;
    void keyReleaseEvent(QKeyEvent* aEvent) override;
    void inputMethodEvent(QInputMethodEvent* aEvent) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery aQuery) const override;
    void focusInEvent(QFocusEvent* aEvent) override;
    void focusOutEvent(QFocusEvent* aEvent) override;
    // DevicePixelRatioChange (the frame rasterizes at the widget's ratio) and the touch
    // events the input bridge translates. QWidget has no touchEvent() virtual in Qt 6,
    // so those arrive here.
    bool event(QEvent* aEvent) override;

private:
    // The panel's editor: the driver owns it and a new driver is a new panel, so it is
    // asked for at every use rather than cached. Null before a project is set.
    ctrl::GraphEditor* graphEditor() const;
    // The camera the editor's own geometry is pinned to. ctrl::GraphEditor derives its
    // panel rect and every hit-test from aCamera.screenSize() alone, so this is the one
    // piece of camera state the panel's gestures need - and it has to be THIS widget's
    // size, because the panel drawn here is this widget. (The world transform is never
    // read on the graph's cursor path.)
    core::CameraInfo panelCamera() const;
    // Whether the editor owns the point the cursor is at (a gesture in flight, a key or
    // a handle, a left press inside the plot, the middle button): the editor's own
    // arbitration, asked before its cursor pass exactly as ctrl::Driver asks it.
    bool panelTakesCursor() const;
    // The editor's cursor pass for the cursor's current state, when the editor owns it.
    // Repaints when the editor reports that something changed.
    void routeCursor();
    // The panel's two keyboard actions, for a key press that has reached this widget
    // unconsumed: Ctrl+A selects every marker of the shown channels
    // (ctrl::GraphEditor::selectAllMarkers) and Escape clears the selection
    // (ctrl::GraphEditor::clearSelection). Returns whether the panel ACTED, and the
    // caller consumes the event exactly then - a press that changed nothing is left to
    // propagate to the application's own key routing, as every unconsumed key here
    // always was. The conditions (this widget is the focus widget; Dear ImGui has not
    // taken the keyboard) are stated at the definition.
    bool panelKeyAction(const QKeyEvent& aEvent);

    // The panel's right-click, at aPos in this widget's pixels. ONE PICK decides
    // which menu opens, and it is the editor's own press rule
    // (ctrl::GraphEditor::contextTargetAt - the same nearest-and-normalised
    // comparison a left press obeys, and the one the hover ink lights):
    //   * on a TANGENT HANDLE it opens the HANDLE MENU - that side's Free / Aligned
    //     / Vector / Auto / Auto Clamped - through
    //     gui::MainDisplayWidget::showGraphHandleMenu. A handle therefore never
    //     opens the key menu, even when a key sits inside the handle's wider grab
    //     ring (it is the handle that is relatively nearer);
    //   * on a KEY it opens the TIMELINE'S OWN KEY MENU - the one a right-click on
    //     a timeline key builds, through gui::MainDisplayWidget::showGraphKeyMenu -
    //     anchored at the key in screen coordinates. The timeline applies its own
    //     right-click rule to the pressed key against the one shared selection, so
    //     with several keys selected the menu acts on all of them and a key that is
    //     not selected is lent to the menu for its lifetime;
    //   * on NEITHER it opens the TIMELINE'S BACKGROUND menu
    //     (gui::MainDisplayWidget::showGraphBackgroundMenu), anchored at the
    //     pointer: the same items a right-click on empty timeline space offers,
    //     plus the graph's own "Fit graph" entry.
    // Returns true when a menu opened.
    bool openKeyMenuAt(const QPoint& aPos);
    // THE HOVER REPAINT RULE (see updateHoverRepaint): one repaint whenever the target
    // under the pointer CHANGES, and none for a move that keeps the same target. The
    // two plot marks the panel draws from the pointer's position alone are ONE answer -
    // gui::panelPickAt, the same query drawPanel inks and ctrl::graph::pickTarget is
    // the comparison a press obeys, so the marker and the tangent handle are never lit
    // together and the ink names exactly what a press would select - plus the
    // scroll-bar LANE under the pointer (gui::panelBarAt - its pill turns
    // theme::Colors::hover), so those are what this compares.
    void updateHoverRepaint();
    // The marquee's edge autoscroll: the shared ctrl::autoscroll rule is applied by
    // ctrl::GraphEditor::autoScrollMarquee (the panel owns the frame/value windows),
    // while this host owns the 60 Hz timer and the repaint. Started on a marquee
    // drag's movement, stopped on release, hide, or a tick that moved nothing.
    void updateMarqueeAutoScroll();
    void autoScrollMarquee();
    // The viewport widget the application's menus live in (see the class comment):
    // MainDisplayWidget owns the channel list, and it is the handle on the timeline
    // (timeLineWidget()), so the key menu is opened through it. Asked for through the
    // window rather than held, because the panel's host interface carries no reference
    // to it (the dock constructs this widget with a GUIResources and a parent). This
    // widget maps its own points to screen coordinates itself (mapToGlobal): the two are
    // siblings, so the display cannot map a point out of this widget's space.
    MainDisplayWidget* appDisplay() const;

    // Runs the action an accessible item advertises, i.e. the one the pointer performs
    // on the same control. The thunk gui::publishPanelAccessibility is handed
    // (kPanelFitItemId, kPanelKeyMenuItemId).
    void onAccessibilityAction(int aId);
    static void accessibilityAction(void* aUser, int aId);

    // Publishes the panel's accessibility model for the state this host is in (an
    // editor: the frame; none: "not on screen").
    void publishAccessibility();

    // Drops this host's GL objects and its hold on the shared context (the context, the
    // backend and the atlas go only when no host holds them). Called from the destructor
    // and from initializeGL when this widget is given a NEW GL context.
    void shutdownHost();

    void onThemeUpdated(theme::Theme& aTheme);

    GUIResources& mResources;
    // The theme change that repaints the floor and the panel's palette.
    util::SlotId mThemeSlot;
    ctrl::Driver* mDriver;
    QScopedPointer<ImGuiInputBridge> mBridge;
    // The frame this host drew last: its model (which the hits below are tested
    // against), the editor, the palette and this host's own clock. Its topInset stays 0:
    // nothing floats over the dock's page.
    PanelFrameState mFrame;
    // The Qt mouse stream in the editor's own currency (ctrl::GraphEditor reads
    // screenPoint()), fed from this page's events and pinned to panelCamera().
    core::AbstractCursor mCursor;
    // THE POINTER, as this host last received it in an event: this widget's logical
    // pixels, which is the space the model's rects and the draw list are in. The frame
    // driver is handed this rather than reading io.MousePos, because this page paints
    // when it is asked to and Dear ImGui trickles its input queue across frames, so its
    // position can lag the pointer (see runPanelFrame - the measured readout defect).
    // Starts outside the panel so no hover is painted before the pointer arrives.
    // The pointer as the INK wants it (gui/Ink.h's ink::Pointer): the position, the press's own
    // position and whether the left button is still down. A backend never reads a pointer, so
    // the host is the only place these three facts exist - and they are tracked for EVERY host,
    // because the ImGui one feeds them from the same events.
    QPoint mPointer;
    QPoint mPressPointer;
    bool mLeftDown = false;
    QTimer mMarqueeScrollTimer;
    ctrl::autoscroll::EdgeDwell mMarqueeDwell;

    // THE HOVERED TARGET, in the panel's pointer-driven marks: the key under the
    // pointer, the tangent handle nearest it, and the scroll-bar lane the pointer is
    // on. It is a VALUE, so the hover repaint rule is one compare - "did the target
    // change?" - rather than a repaint per move (see updateHoverRepaint).
    struct HoverTarget {
        // The key the shared pick names (gui::panelPickAt), false when the point
        // selects a handle or nothing.
        bool key = false;
        core::ObjectNode* keyNode = nullptr;
        ctrl::graph::Channel keyChannel;
        int keyFrame = 0;
        // The handle the same pick names, as (curve index into the model, handle
        // index), -1 when the point selects a key or nothing.
        int handleCurve = -1;
        int handle = -1;
        // The scroll-bar lane under the pointer (gui::panelBarAt): the drawing layer
        // paints that lane's pill in theme::Colors::hover, so entering a lane, leaving
        // it, or crossing from one lane to the other is a change that must repaint.
        PanelBar bar = PanelBar::None;

        bool operator==(const HoverTarget& aRhs) const {
            if (key != aRhs.key || handleCurve != aRhs.handleCurve || handle != aRhs.handle
                || bar != aRhs.bar)
                return false;
            return !key || (keyNode == aRhs.keyNode && keyChannel == aRhs.keyChannel && keyFrame == aRhs.keyFrame);
        }
        bool operator!=(const HoverTarget& aRhs) const { return !(*this == aRhs); }
    };
    HoverTarget mHover;
    // What channelsChanged last published: the page draws a frame per repaint, so the
    // emission is gated on the ROW SET actually changing (a playback tick repaints 60
    // times a second and must not rewrite the bar's label each time).
    QVector<PanelChannelRow> mPublishedChannels;
    bool mReady;
};

} // namespace gui

#endif // GUI_GRAPHPANELWIDGET_H
