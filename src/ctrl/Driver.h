#ifndef CTRL_DRIVER_H
#define CTRL_DRIVER_H

#include "core/Project.h"
#include "core/AbstractCursor.h"
#include "core/RenderInfo.h"
#include "core/ObjectNode.h"
#include "core/TimeKeyBlender.h"
#include "ctrl/ToolType.h"
#include "ctrl/IEditor.h"
#include "ctrl/SRTEditor.h"
#include "ctrl/FFDEditor.h"
#include "ctrl/BoneEditor.h"
#include "ctrl/PoseEditor.h"
#include "ctrl/MaskEditor.h"
#include "ctrl/MaskParam.h"
#include "ctrl/MeshEditor.h"
#include "ctrl/GraphEditor.h"
#include "ctrl/GraphicStyle.h"
#include "ctrl/DriverResources.h"
#include "ctrl/UILogger.h"
#include "util/Signaler.h"

namespace ctrl {

class Driver {
public:
    Driver(core::Project& aProject, DriverResources& aResources, GraphicStyle& aGraphicStyle, UILogger& aUILogger);

    void setTarget(core::ObjectNode* aNode);
    core::ObjectNode* currentTarget() const { return mCurrentNode; }
    void setTool(ToolType aType);
    bool updateCursor(const core::AbstractCursor& aCursor, const core::CameraInfo& aCamera);
    void updateFrame();
    void updateKey(core::TimeLineEvent& aEvent, bool aUndo);
    // Performs the re-blend a key update asked for. updateKey only MARKS: a drag delivers dozens of
    // updates, and blending in each one starves the event loop until the platform collapses the
    // pointer's motion (the pan had the same shape and the same fix). The viewport calls this once
    // at the start of a paint, and a release calls it too, so the document is never left stale.
    void flushPendingBlend();
    // drains the editor's deferred brush work (see the .cpp): the viewport calls this EVERY frame,
    // before flushPendingBlend()
    void frameTick();
    void updateTree(core::ObjectTreeEvent& aEvent, bool aUndo);
    void updateResource(core::ResourceEvent& aEvent, bool aUndo);
    void updateProjectAttribute();

    void renderGL(const core::RenderInfo& aRenderInfo, core::ObjectNode* aGridTarget) const;
    void renderQt(const core::RenderInfo& aRenderInfo, QPainter& aPainter) const;

    // The animation graph editor. The Driver owns it for the same reason it owns
    // the tools (one owner per project), but nothing of the panel's is routed
    // through here: the panel's dock page (gui::GraphPanelWidget) feeds it its own
    // page's cursor and wheel events, and these setters are the non-event state the
    // window feeds in - the shown targets and the modifier state.
    void setGraphTargets(const QVector<core::ObjectNode*>& aTargets);
    // The modifier state the panel's own gestures read. The widget feeds it in
    // before it routes a cursor or wheel event, because neither
    // core::AbstractCursor nor the panel's own updateCursor signature carries
    // modifiers (see GraphEditor::setModifiers for what each one means).
    void setGraphModifiers(Qt::KeyboardModifiers aModifiers);
    GraphEditor* graphEditor() { return mGraphEditor.data(); }
    MaskEditor* maskEditor() { return mMaskEditor; }
    // The Bone editor the current tool routes to (null unless ToolType_Bone).
    // The paint-weights panel asks it for the selected bone's state and sends it
    // the Bake/Revert/Rebake/Clear actions; the editor owns the selection.
    BoneEditor* boneEditor() { return mBoneEditor; }
    const BoneEditor* boneEditor() const { return mBoneEditor; }
    // A SkinWeights change (a stroke frame, a bake, an undo) needs one thing from
    // the driver: a re-blend before the next frame reads the rebuilt map. The
    // owner's key caches were already rebuilt by BoneKeyUpdater.
    void updateSkinWeights(core::ObjectNode& aOwner);
    // the editor the current tool routes to (the mask editor among them): the viewport asks it for
    // the band a stroke changed, so a frame can repaint only that band
    IEditor* editor() { return mEditor.get(); }
    // the tool this driver routes to: the ONE source of truth (the viewport used to cache it, and a
    // cache that only a tool SWITCH updates is uninitialised until the user switches something)
    ToolType toolType() const { return mToolType; }

    void updateParam(const SRTParam& aParam);
    void updateParam(const FFDParam& aParam);
    void updateParam(const BoneParam& aParam);
    void updateParam(const PoseParam& aParam);
    void updateParam(const MeshParam& aParam);
    void updateParam(const MaskParam& aParam);
    // clears the mask the mask tool is editing (one undo step)
    void clearMask();

    // The PaintWeights panel's actions. Each refuses (and the editor logs why)
    // unless the Bone editor is up with a selected bone and a document key.
    bool bakeBoneWeights(bool aReplace, QString* aError = nullptr);
    bool revertBoneWeights(QString* aError = nullptr);
    bool clearBoneWeights(QString* aError = nullptr);

    // The selected bone or its authored state changed: the dock re-reads
    // boneEditor()->weightsState().
    util::Signaler<void()> onBoneWeightsStateChanged;

private:
    static void drawOutline(const core::RenderInfo& aRenderInfo, QPainter& aPainter) ;
    static void drawBanMark(const core::RenderInfo& aRenderInfo, QPainter& aPainter);
    // Merge an event into the pending blend without notifying the editor (the
    // weight path is not a key edit; recreating the mode mid-stroke would drop
    // the stroke's target set).
    void markPendingBlend(core::TimeLineEvent& aEvent);

    core::Project& mProject;
    DriverResources& mResources;
    GraphicStyle& mGraphicStyle;
    UILogger& mUILogger;
    ToolType mToolType;
    core::TimeKeyBlender mBlender;
    // the merged key update a paint will blend
    core::TimeLineEvent mPendingEvent;
    bool mPendingBlend = false;
    QScopedPointer<IEditor> mEditor;
    // Unlike mEditor this is not owned by the tool: the graph is a HUD that
    // outlives every tool switch, so it is created once and never reset.
    QScopedPointer<GraphEditor> mGraphEditor;
    MaskEditor* mMaskEditor;
    // raw: like mMaskEditor, cleared when the Bone tool's editor is destroyed
    BoneEditor* mBoneEditor;
    core::ObjectNode* mCurrentNode;
    int mOnUpdating;
    bool mRejectedTarget;
};

} // namespace ctrl

#endif // CTRL_DRIVER_H
