#ifndef GUI_MAINDISPLAYWIDGET_H
#define GUI_MAINDISPLAYWIDGET_H

#include <QtOpenGLWidgets/QOpenGLWidget>
#include <QOpenGLFramebufferObject>
#include <QScopedPointer>
#include <QTabBar>
#include <QReadWriteLock>
#include <QColor>
#include <QImage>
#include <QVariant>
#include <QtMath>
#include "util/LinkPointer.h"
#include "util/SlotId.h"
#include "gl/Global.h"
#include "gl/Root.h"
#include "gl/VertexArrayObject.h"
#include "gl/EasyTextureDrawer.h"
#include "core/Project.h"
#include "core/AbstractCursor.h"
#include "gui/ViewportInput.h"
#include "core/TimeInfo.h"
#include "core/ClippingFrame.h"
#include "core/DestinationTexturizer.h"
#include "core/FilterFrame.h"
#include "ctrl/Brush.h"
#include "ctrl/Driver.h"
#include "ctrl/Painter.h"
#include "gui/MainViewSetting.h"
#include "gui/CanvasMover.h"
// The channel list's rows travel from the panel's model to this widget's menus
// (showGraphChannelMenu), so the widget's interface names gui::PanelChannelRow.
#include "gui/GraphPanelModel.h"
namespace gui {
class ProjectTabBar;
}
namespace gui {
class TimeLineWidget;
}
namespace gui {
class ViaPoint;
}
namespace gui {
// The channel list the header's channels button opens (showGraphChannelMenu): one
// instance per viewport, parented to it, so the widget's interface can name it without
// pulling the popup's own header into every translation unit that includes this one.
class GraphChannelPopup;
}

namespace gui {

class MainDisplayWidget: public QOpenGLWidget, public gui::ViewportHost {
    Q_OBJECT
public:
    // The HUD reads the ACTIVE brush from here; MainWindow wires it to the tools dock, which owns
    // the one brush model (ctrl::BrushParam). Without a source the HUD draws nothing.
    void setBrushSource(std::function<const ctrl::BrushParam*()> aSource) { mBrushSource = std::move(aSource); }
    MainDisplayWidget(ViaPoint& aViaPoint, QWidget* aParent);
    ~MainDisplayWidget();

    void setProject(core::Project* aProject);
    void setDriver(ctrl::Driver* aDriver);
    void setProjectTabBar(ProjectTabBar* aTabBar);
    // The timeline the animation graph panel shares its ONE frame axis with (the
    // dock's Graph tab draws it; see gui/GraphPanelWidget). MainWindow wires it once
    // (the timeline widget outlives every project, unlike the driver), and from then on
    // the panel's frame window is the timeline's visible one and its selection is the
    // timeline's live set - pushed in as values, on every timeline view change (see
    // syncGraphPanelSharedState). Null detaches: the panel's editor goes back to owning
    // both its frame axis and its selection.
    void setTimeLineWidget(TimeLineWidget* aTimeLine);
    // The timeline wired in above, for callers that need the timeline itself (the
    // panel's right-click reaches the application's key menu through it; see
    // showGraphKeyMenu).
    TimeLineWidget* timeLineWidget() const { return mTimeLine; }
    // Opens the application's own KEY MENU - the one a right-click in the timeline
    // builds - for the key aPressedKey names, at aGlobalPos, which is a GLOBAL (screen)
    // position: the docked panel (gui::GraphPanelWidget) and this widget are SIBLINGS,
    // so the panel maps its own pointer with its own QWidget::mapToGlobal() and no
    // coordinate of the panel is ever read as a position in this widget. The menu acts
    // on the shared key selection, and aPressedKey's relationship to it decides
    // whether it is lent to the menu (see TimeLineWidget::openKeyMenu - the whole
    // rule, including the loan's drop, is the timeline's). False when nothing opened
    // (no timeline wired, or the key is not one the document holds).
    bool showGraphKeyMenu(const core::TimeLineEvent::Target& aPressedKey, const QPoint& aGlobalPos);
    // Opens the graph's HANDLE menu at aGlobalPos (screen coordinates): the five handle
    // types - Free / Aligned / Vector / Auto / Auto Clamped - for the ONE SIDE aTarget
    // names, with that side's current type checked. The entries are
    // util::Easing::getHandleModeName's, the same vocabulary the picker's two mode
    // combos use, and activating one applies it through
    // ctrl::GraphEditor::setHandleMode: ONE undo step whose name says which side
    // ("Change the outgoing handle to Free"), never the key menu's actions - copy,
    // paste, spacing and delete are about KEYS, and a tangent handle is not one.
    //
    // aTarget is the panel's own pick (ctrl::GraphEditor::contextTargetAt), so the
    // side named here is the one under the pointer. False when nothing opened (no
    // driver, or a target that is not a handle).
    bool showGraphHandleMenu(const ctrl::GraphEditor::ContextTarget& aTarget, const QPoint& aGlobalPos);
    // Opens the timeline's BACKGROUND menu - the one a right-click on empty timeline
    // space builds - at aGlobalPos (screen coordinates), plus the graph's own "Fit
    // graph" entry. The panel's right-click uses this when its pick
    // (ctrl::GraphEditor::contextTargetAt) names neither a key nor a handle.
    //
    // NOT A SECOND MENU: the items are TimeLineEditorWidget::buildKeyMenu's - the same
    // paste anchor, the same selection rule and the same labels - and they act on the
    // SHARED SELECTION (TimeLineEditorWidget::openBackgroundMenu). THE POINT IS NOT
    // HIT-TESTED AGAINST THE TIMELINE'S LANES: this widget's pixels are the panel's,
    // and reading them as timeline model space named a key of some other row - which is
    // how a menu's Edit/Delete could end up editing a key the user was not looking at.
    //
    // With NO selection the menu is paste-only (buildKeyMenu's empty-targets shape):
    // one "Paste" item, enabled when something has been copied. With a selection it
    // offers Copy, Copy to clipboard, Paste, Select spacing between keys and Delete. "Fit graph" is always last and runs ctrl::GraphEditor::fitAll() - the
    // panel's explicit fit, which replaced the plot's double-click. False when there is
    // no timeline wired (or no surface to build the menu on).
    bool showGraphBackgroundMenu(const QPoint& aGlobalPos);
    // Shows the application's own CHANNEL LIST - the animation graph panel's ONE
    // control, which the user's design moved into the DOCK HEADER's channels button -
    // for the rows the panel's model published. aWidgetPos is the ANCHOR's BOTTOM-RIGHT
    // CORNER in this widget's coordinates (like showGraphKeyMenu, the docked host maps
    // its own pixels here first): the list opens just under that point, right-aligned
    // to it, which is where the header's button sits.
    //
    // WHY IT IS HERE. The list is a real popup widget (gui::GraphChannelPopup), the
    // application's own - keyboard navigable, a native window - exactly like the key
    // menu above: the panel's host draws no popup and measures none, it hands the rows
    // over and this class builds and shows the list. One list implementation for the
    // whole application, and the panel's control is the same kind of control as the
    // timeline's own menus. (It is NOT a QMenu: the design's 22px rows, its 8px
    // swatch-to-name gap and its 10px group titles at the swatch's padding are not
    // reachable through gui::AppStyle's menu metrics.)
    //
    // It is a plain list, grouped under each layer: one row per channel, the row's
    // SWATCH is its colour key (filled while the channel is shown, hollow while it is
    // hidden) and the row's activation toggles that channel's visibility - the one
    // action per row the design asks for. The row ALSO focuses its channel when it is
    // hovered, which is the other thing the list is for (the panel draws the focused
    // channel's tangent handles and the accessible summary names it). Rows for a
    // hidden channel are listed like any other: that is how it is turned back on.
    // Nothing happens with no rows, and no list is shown.
    void showGraphChannelMenu(const QVector<PanelChannelRow>& aRows, const QPoint& aWidgetPos);
    void updateRender();
    void resetCamera();
    void setViewportBackground(const QColor& aColor);
    const QColor& viewportBackground() const { return mViewportBackground; }

    QReadWriteLock& renderingLock() { return mRenderingLock; }
    const QReadWriteLock& renderingLock() const { return mRenderingLock; }


    // boostlike signals
public:
    void onVisualUpdated();
    void onToolChanged(ctrl::ToolType);
    void onFinalizeTool(ctrl::ToolType);
    void onViewSettingChanged(const MainViewSetting&);
    void onProjectAttributeUpdated();

    // THE TEMPORARY EYEDROPPER'S RESULT: the mask tool sampled the field under the
    // cursor (Alt + left click) and hands the value here. The modifier state lives in
    // this widget, so this is where the gesture is recognised; the panel is the one
    // that owns the value.
    util::Signaler<void(float)> onMaskValueSampled;

    // THE BRUSH SIZE/HARDNESS DRAG (Alt + RIGHT over the viewport): horizontal is
    // the size, vertical the hardness, in whole steps of one notch. Right button on
    // purpose - the compositor owns Alt + LEFT drag to move windows (measured), so
    // the familiar gesture survives on this platform. Photoshop's own version is a
    // 2D PAD (both values follow the pointer, no snap); ctrl::BrushDrag adds a
    // dominant-axis lock after 4px because the old handler moved both values on a
    // drag aimed at one, and the comment there states why.
    util::Signaler<void(int, int)> onBrushDragAdjusted;
    // the Alt drag's CONTINUOUS form: fractional steps of the same units the notch keys use
    util::Signaler<void(double, double)> onBrushNudged;

private:
    class GLContextAccessor: public gl::ContextAccessor {
        MainDisplayWidget* mOwner;

    public:
        GLContextAccessor(MainDisplayWidget* aOwner): mOwner(aOwner) {}
        virtual void makeCurrent() { mOwner->makeCurrent(); }
        virtual void doneCurrent() { mOwner->doneCurrent(); }
    };

    // from QOpenGLWidget
    void initializeGL() override;
    void paintGL() override;
    void paintEvent(QPaintEvent* event) override;
    void resizeGL(int w, int h) override;

    // gui::ViewportHost - the services the input router uses. Nothing else routes the pointer.
    core::RenderInfo& renderInfo() override;
    core::CameraInfo& camera() override;
    core::AbstractCursor& cursor() override;
    ctrl::MaskEditor* maskEditor() override;
    void forwardModifiers(Qt::KeyboardModifiers aModifiers) override;
    ctrl::ToolType toolType() const override;
    const ctrl::BrushParam* activeBrush() const override;
    void updateCursorNow() override;
    void requestFrame() override;
    void sampleMaskValue(float aValue) override;
    void beginBrushDial(const QPointF& aPos) override;
    void stepBrushDial(const QPointF& aPos) override;
    bool brushDialActive() const override;
    void endBrushDial() override;
    QPoint mapToGlobal(const QPoint& aPos) override;
    void setGrabCursor(bool aClosed) override;
    // one repaint request per 16ms while a stroke is being fed
    void requestStrokeFrame();

    // the size/hardness dial's readout (see the resize render in the .cpp)
    void paintBrushHud(QPainter& aPainter, float aSizeSteps, float aHardnessSteps);
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    bool updateCursor();
    // Sets ctrl::CanvasMover's drag mode from the state that decides it, so "which
    // gesture is in flight" is applied in one place (see the definition). The
    // animation graph panel used to hold one of these flags while it was drawn over
    // this widget; it is the dock's own page now and never sees this camera.
    void applyCanvasMoverDragMode();
    // Hands the panel's editor the timeline's shared state: the live selection it
    // writes (ctrl::TimeLineEditor::selection()) and the frame window it shows
    // (TimeLineWidget::visibleFrameRange()), plus the two delegates that send the
    // panel's horizontal gestures back to the timeline. The panel itself is drawn in
    // the dock (gui::GraphPanelWidget), which pins its own plot width per frame; the
    // state wired here is the editor's and is shared by whoever draws it. This widget
    // keeps the wiring because it is the one that has the timeline. Called whenever either end
    // changes - the timeline being wired in, a driver being created for a project,
    // the timeline's view moving - so the panel never holds a stale window or a
    // stale selection. A no-op before either end exists.
    void syncGraphPanelSharedState();
    // Asks the DOCK's Graph page for a repaint. The panel is a QOpenGLWidget of its
    // own, so nothing this widget paints shows it: every state this widget pushes into
    // the panel's editor - the frame window and the shared selection
    // (syncGraphPanelSharedState) - has to ask the page itself. A no-op in a build
    // without the panel and in a window without the dock.
    void refreshGraphPanel();
    QSize deviceSize() const { return this->size() * mDevicePixelRatio; }
    // true while a tool that owns the shared brush is active (the brush drags)
    bool brushToolActive() const;

    ViaPoint& mViaPoint;
    gl::DeviceInfo mGLDeviceInfo;
    util::LinkPointer<core::Project> mProject;
    gl::Root mGLRoot;
    GLContextAccessor mGLContextAccessor;
    QScopedPointer<gl::VertexArrayObject> mDefaultVAO;
    QScopedPointer<QOpenGLFramebufferObject> mFramebuffer;
    QScopedPointer<core::ClippingFrame> mClippingFrame;
    QScopedPointer<core::DestinationTexturizer> mDestinationTexturizer;
    QScopedPointer<core::FilterFrame> mFilterFrame;
    QScopedPointer<gl::EasyTextureDrawer> mTextureDrawer;
    QScopedPointer<ctrl::PainterHandle> mPainterHandle;
    QReadWriteLock mRenderingLock;
    core::RenderInfo* mRenderInfo;
    core::AbstractCursor mAbstractCursor;
    ctrl::Driver* mDriver;
    // only the pre-driver fallback: with a driver, toolType() asks IT (see the .cpp)
    ctrl::ToolType mToolType = ctrl::ToolType_Cursor;
    ProjectTabBar* mProjectTabBar;
    // The ONE channel list this widget owns (showGraphChannelMenu): created on the
    // first open, parented here, hidden and re-filled for every open after that.
    GraphChannelPopup* mChannelPopup;
    // The timeline the panel shares its frame window and its key selection with
    // (see setTimeLineWidget), and the connection to its view-range signal.
    TimeLineWidget* mTimeLine;
    util::SlotId mTimeLineRangeSlot;
    // the Alt+right-drag gesture in flight, owned by ctrl::BrushDrag: the latch,
    // the slop and the fractional banks all live in the brush model (ctrl/Brush.h),
    // so the gesture is one decision instead of three literals in this event handler
    ctrl::BrushDrag mBrushDrag;
    // The eyedropper's live sample: down while Alt+left is held and the pointer is being read, so
    // the panel keeps tracking the field under the pointer instead of one reading at the press.
    bool mAltSampling = false;
    // A state-only gesture (SRT, the pan) is fed once per frame; a path gesture (a brush) per event.
    gui::ViewportInput mInput;

    QTimer mStrokeFrame;
    // the band this frame must cover, and whether anything invalidated the whole surface
    // THE ADJUSTMENT HUD'S SOURCE: the active tool's brush, asked fresh on every paint (the values
    // change during the drag). Null when the current tool has none.
    std::function<const ctrl::BrushParam*()> mBrushSource;
    // The timeline surface's playWindowChanged -> syncGraphPanelSharedState connection
    // (see setTimeLineWidget). Dropped with the old timeline; auto-dropped when this
    // widget dies.
    QMetaObject::Connection mPlayWindowConnection;
    QColor mViewportBackground;
    bool mUsingTablet;
    MainViewSetting mViewSetting;
    CanvasMover mCanvasMover;
    bool mMovingCanvasByTool;
    bool mMovingCanvasByKey;
    bool mMovingCanvasByMiddleMouseButton;
    double mDevicePixelRatio;
};

} // namespace gui

#endif // GUI_MAINDISPLAYWIDGET_H
