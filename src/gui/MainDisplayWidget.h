#ifndef GUI_MAINDISPLAYWIDGET_H
#define GUI_MAINDISPLAYWIDGET_H

#include <QtOpenGLWidgets/QOpenGLWidget>
#include <QOpenGLFramebufferObject>
#include <QScopedPointer>
#include <QTabBar>
#include <QReadWriteLock>
#include <QColor>
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
#include "core/TimeInfo.h"
#include "core/ClippingFrame.h"
#include "core/DestinationTexturizer.h"
#include "core/FilterFrame.h"
#include "ctrl/Driver.h"
#include "ctrl/Painter.h"
#include "gui/MainViewSetting.h"
#include "gui/CanvasMover.h"
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

class MainDisplayWidget: public QOpenGLWidget {
    Q_OBJECT
public:
    MainDisplayWidget(ViaPoint& aViaPoint, QWidget* aParent);
    ~MainDisplayWidget();

    void setProject(core::Project* aProject);
    void setDriver(ctrl::Driver* aDriver);
    void setProjectTabBar(ProjectTabBar* aTabBar);
    // The timeline this viewport's animation graph panel is a SECOND VIEW of.
    // MainWindow wires it once (the timeline widget outlives every project, unlike
    // the driver), and from then on the panel's frame window is the timeline's
    // visible one and its selection is the timeline's live set - pushed in as
    // values, on every timeline view change (see syncGraphPanelSharedState). Null
    // detaches: the panel goes back to owning both its frame axis and its
    // selection, which is the state the unit tests drive it in.
    void setTimeLineWidget(TimeLineWidget* aTimeLine);
    // The timeline wired in above, for callers that need the timeline itself (the
    // panel's right-click reaches the application's easing menu through it; see
    // showGraphKeyMenu).
    TimeLineWidget* timeLineWidget() const { return mTimeLine; }
    // Shows the application's own key EASING MENU for aTargets (the panel's
    // current selection, in the timeline's currency) at aWidgetPos IN THIS
    // WIDGET'S COORDINATES - the panel's own space, so a caller that has a press
    // position needs no mapping. Does nothing when no timeline is wired or the
    // target list is empty.
    void showGraphKeyMenu(const core::TimeLineEvent& aTargets, const QPoint& aWidgetPos);
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

private:
    class GLContextAccessor: public gl::ContextAccessor {
        MainDisplayWidget* mOwner;

    public:
        GLContextAccessor(MainDisplayWidget* aOwner): mOwner(aOwner) {}
        virtual void makeCurrent() { mOwner->makeCurrent(); }
        virtual void doneCurrent() { mOwner->doneCurrent(); }
    };

    // from QOpenGLWidget
    virtual void initializeGL();
    virtual void paintGL();
    virtual void paintEvent(QPaintEvent* event);
    virtual void resizeGL(int w, int h);
    virtual void mouseMoveEvent(QMouseEvent* event);
    virtual void mousePressEvent(QMouseEvent* event);
    virtual void mouseReleaseEvent(QMouseEvent* event);
    virtual void mouseDoubleClickEvent(QMouseEvent* event);
    virtual void wheelEvent(QWheelEvent* event);
    virtual void tabletEvent(QTabletEvent* event);
    virtual void keyPressEvent(QKeyEvent* event);
    virtual void keyReleaseEvent(QKeyEvent* event);
    virtual void inputMethodEvent(QInputMethodEvent* event);
    virtual QVariant inputMethodQuery(Qt::InputMethodQuery query) const;
    virtual void focusInEvent(QFocusEvent* event);
    virtual void focusOutEvent(QFocusEvent* event);
    // DevicePixelRatioChange (the overlay rasterizes at the widget's ratio) and the
    // touch events the overlay translates for ImGui. QWidget has no touchEvent()
    // virtual in Qt 6, so those arrive here.
    virtual bool event(QEvent* event);

    void updateCursor();
    // Sets ctrl::CanvasMover's drag mode from the state that decides it, so "which
    // gesture is in flight" and "the graph panel took the left drag" are applied in
    // one place (see the definition).
    void applyCanvasMoverDragMode();
    // Hands the graph panel the timeline's shared state: the live selection it
    // writes (ctrl::TimeLineEditor::selection()) and the frame window it shows
    // (TimeLineWidget::visibleFrameRange()), plus the two delegates that send the
    // panel's horizontal gestures back to the timeline. Called whenever either end
    // changes - the timeline being wired in, a driver being created for a project,
    // the timeline's view moving - so the panel never holds a stale window or a
    // stale selection. A no-op before either end exists.
    void syncGraphPanelSharedState();
    QSize deviceSize() const { return this->size() * mDevicePixelRatio; }

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
    ProjectTabBar* mProjectTabBar;
    // The timeline the panel shares its frame window and its key selection with
    // (see setTimeLineWidget), and the connection to its view-range signal.
    TimeLineWidget* mTimeLine;
    util::SlotId mTimeLineRangeSlot;
    QColor mViewportBackground;
    bool mUsingTablet;
    MainViewSetting mViewSetting;
    CanvasMover mCanvasMover;
    bool mMovingCanvasByTool;
    bool mMovingCanvasByKey;
    bool mMovingCanvasByMiddleMouseButton;
    // The animation graph panel took the LEFT press that opened the drag in flight
    // (see graphPanelTakesPress and applyCanvasMoverDragMode): the canvas camera
    // must not move with a drag the panel is running. Cleared on that button's
    // release.
    bool mGraphTookLeftPress;
    double mDevicePixelRatio;
};

} // namespace gui

#endif // GUI_MAINDISPLAYWIDGET_H
