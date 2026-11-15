#ifndef GUI_CONTROLWIDGET_H
#define GUI_CONTROLWIDGET_H

#include <QWidget>
#include <QStackedWidget>
#include "gui/ThinSplitter.h"
#include "gui/ObjectTreeWidget.h"
#include "gui/TimeLineWidget.h"
#include "gui/GUIResources.h"
#include "gui/PlayBackWidget.h"
#include "gui/TimeLineHeader.h"
#include "gui/ViaPoint.h"
#include "core/Project.h"
#include "core/Animator.h"
#include "util/Range.h"

namespace ctrl {
class Driver;
namespace time {
class Selection;
}
}

namespace gui {

class AnimationSettingDialog;
#ifdef AE_ENABLE_IMGUI
class GraphPanelWidget;
#endif

class TargetWidget: public QWidget, public core::Animator {
public:
    TargetWidget(
        ViaPoint& aViaPoint, GUIResources& aResources, const LocaleParam& aLocale, QWidget* aParent,
        const QSize& aSizeHint
    );
    // Drops the frame-move connection into the dock's panel before the children (the
    // timeline that emits it, the panel that receives it) are destroyed.
    ~TargetWidget() override;
    void setProject(core::Project* aProject);

    // The project's driver - the object that owns ctrl::GraphEditor - or null while no
    // project is open. MainWindow hands it over beside the viewport's and the tool
    // widget's own setDriver, from the same per-project creation, so the dock's Graph
    // tab (when the graph panel is built) and the viewport's overlay read the ONE
    // editor of the project being edited.
    void setDriver(ctrl::Driver* aDriver);

    ObjectTreeWidget& objectTreeWidget() { return *mObjTree; }
    const ObjectTreeWidget& objectTreeWidget() const { return *mObjTree; }

    TimeLineWidget& timeLineWidget() { return *mTimeLine; }
    const TimeLineWidget& timeLineWidget() const { return *mTimeLine; }

#ifdef AE_ENABLE_IMGUI
    // The Graph tab: the animation graph panel, docked (design section 5). The SAME
    // component as the viewport's overlay, drawn on the dock's floor at the dock's
    // size, with no scrim - the tree beside it stays, because the tree is what names
    // the layers and picks whose channels the panel draws. Not declared in a build
    // without the panel (AE_ENABLE_IMGUI off): there is no graph surface to ask for.
    GraphPanelWidget& graphPanelWidget() { return *mGraphPanel; }
    const GraphPanelWidget& graphPanelWidget() const { return *mGraphPanel; }
#endif

    PlayBackWidget& playBackWidget() { return *mPlayBack; }
    const PlayBackWidget& playBackWidget() const { return *mPlayBack; }

    // The dock's 40px header bar (the dock's tabs when the graph panel is built, the
    // transport, the easing chip and its four quick-easing buttons, the channels button
    // while the graph is current, and the zoom group). The chip's own unit reaches the
    // slot and the header's signal through this, rather than through the dock's layout,
    // and the header's tab signal is what this dock's surface stack follows.
    TimeLineHeader& timeLineHeader() { return *mHeader; }
    const TimeLineHeader& timeLineHeader() const { return *mHeader; }

    // from Animator
    core::Frame currentFrame() const override;
    void stop() override;
    void suspend() override;
    void resume() override;
    bool isSuspended() const override;

private:
    QSize sizeHint() const override { return mSizeHint; }

    void onPlayBackButtonPushed(PlayBackWidget::PushType aType);
#ifdef AE_ENABLE_IMGUI
    // The header's tab: the dock's body shows that view of the ONE dock. The tree
    // is not switched - it is the splitter's first widget and neither tab touches it
    // - and the ruler and the playhead stay the timeline's own either way. A lean
    // build has one surface and no tab bar, so there is nothing to switch.
    void onSurfaceChanged(TimeLineHeader::Surface aSurface);
#endif
    void openAnimationSettings();
    // Live, non-undoable re-sync: apply the current dialog state to the project
    // attribute (which drives onProjectAttributeModified) and let the timeline
    // react immediately. Fires on every edit, including each drag tick.
    void applyAnimLive();
    // One undoable command for the whole popup session, pushed when it closes.
    void applyAnimCommit();
    void refreshAnimationSettings();

    core::Project* mProject;
    ViaPoint& mViaPoint;
    const QSize mSizeHint;
    ThinSplitter* mHorizontalSplitter;
    ObjectTreeWidget* mObjTree;
    // The dock's views, stacked in the splitter's second pane: the timeline's own
    // surface and, when the graph panel is part of the build, the animation graph
    // panel. The splitter keeps the SHAPE it has always had - tree | surface - so the
    // tree is on screen whichever view is up, and the stack is what swaps the surface
    // under it. Without the panel the stack holds the one (timeline) page.
    QStackedWidget* mSurfaceStack;
    TimeLineWidget* mTimeLine;
#ifdef AE_ENABLE_IMGUI
    GraphPanelWidget* mGraphPanel;
    // The dock's SECOND VIEW OF THE CURRENT FRAME: the timeline's own frame-move
    // notification, connected to the panel's refresh() in the constructor (see the
    // definition for why one emission per move is the whole story).
    util::SlotId mFrameSlot;
    // The tree's selection, connected to the same refresh: the tree picks the layers the
    // panel draws, and it is on screen in the Graph tab too.
    util::SlotId mSelectionSlot;
    // The PROJECT's own two change notifications, connected to the same refresh while a
    // project is set (setProject re-wires them): a timeline edit changes the curves, and an
    // attribute change (fps, frame range) moves the frame axis. Both are emitted after their
    // write, and refresh() writes nothing, so neither can feed back into the other.
    util::SlotId mDocumentSlot;
    util::SlotId mAttributeSlot;
    // The shared KEY SELECTION's change (ctrl::time::Selection::onChanged). The panel and
    // the timeline are two views of ONE set of keys, so a selection made in either view
    // changes what the panel draws - the channels at full strength, the hull, the count -
    // and nothing else announces it. The set belongs to the timeline's editor, so it is
    // connected where that editor is handed over (setDriver), re-wired when the editor is
    // replaced, and disconnected in the destructor and when no project is open.
    util::SlotId mSelectionChangedSlot;
    ctrl::time::Selection* mSelectionSource;
#endif
    TimeLineHeader* mHeader;
    PlayBackWidget* mPlayBack;
    AnimationSettingDialog* mAnimSettings = nullptr;
    int mLastAnimFps = 0;
    util::Range mLastAnimRange;
    bool mLastAnimLoop = false;
    int mSuspendCount;
};

} // namespace gui

#endif // GUI_CONTROLWIDGET_H
