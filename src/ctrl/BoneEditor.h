#ifndef CTRL_BONEEDITOR_H
#define CTRL_BONEEDITOR_H

#include "core/Project.h"
#include "core/ObjectNode.h"
#include "core/AbstractCursor.h"
#include "core/BoneKey.h"
#include "ctrl/BoneEditMode.h"
#include "ctrl/BoneParam.h"
#include "ctrl/IEditor.h"
#include "ctrl/GraphicStyle.h"
#include "ctrl/UILogger.h"
#include "ctrl/bone/bone_IMode.h"
#include "ctrl/bone/bone_KeyOwner.h"
#include "ctrl/bone/bone_Target.h"
#include "ctrl/bone/bone_WeightState.h"
#include "util/Signaler.h"

namespace ctrl {

class BoneEditor: public IEditor {
public:
    BoneEditor(core::Project& aProject, GraphicStyle& aStyle, UILogger& aUILogger);
    ~BoneEditor();

    virtual bool setTarget(core::ObjectNode* aTarget);

    void updateParam(const BoneParam& aParam);
    virtual bool updateCursor(const core::CameraInfo& aCamera, const core::AbstractCursor& aCursor);
    virtual void updateEvent(EventType);
    // Drains the PaintWeights stroke's queued stamps once per painted frame.
    virtual void flushPendingWork();

    virtual void renderQt(const core::RenderInfo& aInfo, QPainter& aPainter);

    // The authored-weight panel's state and its actions, forwarded to the mode
    // that owns the selected bone. Outside PaintWeights every query is empty and
    // every action refuses.
    bone::WeightState weightsState() const;
    bool bakeWeights(bool aReplace, QString* aError = nullptr);
    bool revertWeights(QString* aError = nullptr);
    bool clearWeights(QString* aError = nullptr);

    // The selected bone or the field it names changed: the host re-reads
    // weightsState(). Emitted by the mode (right-click selection, an action, the
    // first stroke's implicit selection) and after a target/key reset.
    util::Signaler<void()> onWeightsStateChanged;

private:
    void finalize();
    void resetCurrentTarget(QString* aMessage = nullptr);
    void createMode();
    void initializeKey(core::TimeLine& aLine);
    void notifyWeightsState();

    core::Project& mProject;
    GraphicStyle& mGraphicStyle;
    UILogger& mUILogger;
    BoneParam mParam;
    QScopedPointer<bone::IMode> mCurrent;
    bone::Target mTarget;
    bone::KeyOwner mKeyOwner;
};

} // namespace ctrl

#endif // CTRL_BONEEDITOR_H
