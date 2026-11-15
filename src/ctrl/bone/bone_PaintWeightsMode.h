#ifndef CTRL_BONE_PAINTWEIGHTSMODE_H
#define CTRL_BONE_PAINTWEIGHTSMODE_H

#include <QHash>
#include <QString>
#include <functional>
#include "core/Project.h"
#include "core/ObjectNode.h"
#include "core/SkinWeights.h"
#include "ctrl/Brush.h"
#include "ctrl/UILogger.h"
#include "util/Circle.h"
#include "ctrl/bone/bone_Focuser.h"
#include "ctrl/bone/bone_IMode.h"
#include "ctrl/bone/bone_KeyOwner.h"
#include "ctrl/bone/bone_Target.h"
#include "ctrl/bone/bone_WeightCommands.h"
#include "ctrl/bone/bone_WeightState.h"

namespace core {
class LayerMesh;
}

namespace ctrl {
namespace bone {

// PAINT WEIGHTS: the authored field of a BAKED bone.
//
// The mode is deliberately not the envelope brush with a different target. An
// envelope stroke changes a BoneShape (one command per bone); a weight stroke
// writes sparse per-vertex entries on every layer the bone skins (one command
// per stroke across all of them), and it is only legal once the bone is baked.
// What it shares with the other painting tools is the ONE brush model:
// ctrl::Brush.h's BrushStroke, brushFalloff and the Add/Subtract/Replace deposit,
// so a size and a hardness mean the same thing here as in FFD or the mask brush.
//
// The stamp is evaluated in WORLD pixels: the brush ring is drawn in world space
// and the user expects the circle to cover what it covers on screen. Each target
// vertex is first mapped with the exact product the influence build uses
// (target matrix * influence map matrix), then its distance to the stamp center
// picks the falloff. The values are written in the vertex's own index, which is
// the storage key.
//
// One stroke is one AssignSkinWeightsCommand, rewritten while the stack still
// offers it; the cache rebuild is emitted once per painted FRAME (the stamps are
// queued and drained by flushPendingWork), never per stamp.
class PaintWeightsMode: public IMode {
public:
    PaintWeightsMode(
        core::Project& aProject, const Target& aTarget, KeyOwner& aKey, UILogger& aLogger,
        std::function<void()> aStateChanged
    );
    ~PaintWeightsMode() override;

    void updateParam(const BoneParam& aParam) override;
    bool updateCursor(const core::CameraInfo& aCamera, const core::AbstractCursor& aCursor) override;
    void renderQt(const core::RenderInfo& aInfo, QPainter& aPainter) override;
    void flushPendingWork() override;

    WeightState weightState() const override;
    bool bakeWeights(bool aReplace, QString* aError) override;
    bool revertWeights(QString* aError) override;
    bool clearWeights(QString* aError) override;

private:
    // One baked node the current stroke may edit. The world positions and the
    // coarse cell grid are built once per stroke: a stamp then touches the cells
    // its circle overlaps instead of every vertex of a dense mesh.
    struct EditTarget {
        core::ObjectNode* node = nullptr;
        const core::LayerMesh* mesh = nullptr;
        QMatrix4x4 worldMapMtx;
        core::SkinWeights::NodeWeights working;
        QVector<QVector2D> worldPositions;
        float cell = 16.0f;
        QHash<quint64, QVector<int>> cells;
        bool changed = false;
    };

    void selectBone(core::Bone2* aBone);
    void notifyState();
    void fail(const QString& aMessage);
    void clearError();

    bool beginStroke(float aPressure);
    void endStroke();
    void cancelStroke();
    bool prepareTargets();
    void applyStamp(const BrushStamp& aStamp);
    void commitStamps();
    bool strokeCommandOpen() const;
    core::TimeInfo keyTime() const;
    QVector2D targetPos(const core::AbstractCursor& aCursor) const;

    core::Project& mProject;
    core::ObjectNode& mTarget;
    KeyOwner& mKeyOwner;
    UILogger& mUILogger;
    std::function<void()> mStateChanged;

    QMatrix4x4 mTargetMtx;
    QMatrix4x4 mTargetInvMtx;
    Focuser mFocuser;
    core::Bone2* mActiveBone;
    core::Bone2* mHoverBone;

    BoneParam mParam;
    BrushStroke mStroke;
    util::Circle mBrush;
    QVector2D mCursorPos;
    bool mIsBrushDrawing;
    QVector<BrushStamp> mQueuedStamps;

    QVector<EditTarget> mEdits;
    AssignSkinWeightsCommand* mCommand;
    QString mLastError;
    // one mesh-mismatch warning per selected bone, not one per frame
    int mWarnedBoneId;
};

} // namespace bone
} // namespace ctrl

#endif // CTRL_BONE_PAINTWEIGHTSMODE_H
