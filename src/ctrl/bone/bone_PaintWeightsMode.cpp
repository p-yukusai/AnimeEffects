#include "ctrl/bone/bone_PaintWeightsMode.h"

#include <QCoreApplication>
#include <QPainter>
#include <algorithm>
#include <cmath>

#include "cmnd/Stack.h"
#include "core/Bone2.h"
#include "core/BoneKey.h"
#include "core/LayerMesh.h"
#include "core/TimeKeyBlender.h"
#include "ctrl/CmndName.h"
#include "ctrl/bone/bone_Renderer.h"
#include "ctrl/bone/bone_WeightHeatmap.h"

using namespace core;

namespace ctrl {
namespace bone {

namespace {

QString modeName() { return UILog::tr("Bone Editor : "); }

quint64 cellKey(int aX, int aY) {
    return (quint64(quint32(aX)) << 32) | quint64(quint32(aY));
}

} // namespace

PaintWeightsMode::PaintWeightsMode(
    Project& aProject, const Target& aTarget, KeyOwner& aKey, UILogger& aLogger,
    std::function<void()> aStateChanged
):
    mProject(aProject),
    mTarget(*aTarget.node),
    mKeyOwner(aKey),
    mUILogger(aLogger),
    mStateChanged(std::move(aStateChanged)),
    mTargetMtx(aTarget.mtx),
    mTargetInvMtx(aTarget.invMtx),
    mFocuser(),
    mActiveBone(nullptr),
    mHoverBone(nullptr),
    mParam(),
    mStroke(),
    mBrush(),
    mCursorPos(),
    mIsBrushDrawing(false),
    mQueuedStamps(),
    mEdits(),
    mCommand(nullptr),
    mLastError(),
    mWarnedBoneId(0) {
    XC_PTR_ASSERT(mKeyOwner.key);
    mFocuser.setTopBones(mKeyOwner.key->data().topBones());
    mFocuser.setFocusConnector(true);
    mFocuser.setTargetMatrix(mTargetMtx);
    mBrush.setRadius(mParam.brush.radius);
}

PaintWeightsMode::~PaintWeightsMode() = default;

void PaintWeightsMode::updateParam(const BoneParam& aParam) {
    mParam = aParam;
    mBrush.setRadius(mParam.brush.radius);
}

void PaintWeightsMode::notifyState() {
    if (mStateChanged) {
        mStateChanged();
    }
}

void PaintWeightsMode::fail(const QString& aMessage) {
    mLastError = aMessage;
    mUILogger.pushLog(modeName() + aMessage, UILogType_Warn);
    notifyState();
}

void PaintWeightsMode::clearError() { mLastError.clear(); }

TimeInfo PaintWeightsMode::keyTime() const {
    auto time = mProject.currentTimeInfo();
    time.frame.set(mKeyOwner.key ? mKeyOwner.key->frame() : time.frame.get());
    return time;
}

void PaintWeightsMode::selectBone(Bone2* aBone) {
    if (!aBone) {
        return;
    }
    mFocuser.select(*aBone);
    mActiveBone = aBone;
    mLastError.clear();

    if (!mKeyOwner.owns()) {
        const WeightState state = weightState();
        if (state.meshMismatch && mWarnedBoneId != aBone->boneId()) {
            mWarnedBoneId = aBone->boneId();
            mUILogger.pushLog(
                modeName()
                    + UILog::tr(
                        "The mesh changed since this bone's weights were baked; some weights may no "
                        "longer line up. Rebake or clear them."
                    ),
                UILogType_Warn
            );
        }
    }
    notifyState();
}

QVector2D PaintWeightsMode::targetPos(const AbstractCursor& aCursor) const {
    return (mTargetInvMtx.map(QVector3D(aCursor.worldPos()))).toVector2D();
}

bool PaintWeightsMode::updateCursor(const CameraInfo& aCamera, const AbstractCursor& aCursor) {
    mCursorPos = aCursor.worldPos();
    mBrush.setCenter(mCursorPos);
    mBrush.setRadius(mParam.brush.radius);

    Bone2* hovered = mFocuser.update(aCamera, aCursor.screenPos());
    if (hovered) {
        mHoverBone = hovered;
    }

    // RIGHT SELECTS, LEFT PAINTS - the influence brush's rule, so the two bone
    // painting tools use the same hand. With nothing selected, the first press
    // takes the bone under the cursor (or the nearest envelope), and the paint
    // tool then edits exactly that bone.
    if (aCursor.emitsRightPressedEvent()) {
        if (!mIsBrushDrawing && hovered) {
            selectBone(hovered);
        }
        return true;
    }
    if (aCursor.emitsRightDraggedEvent() || aCursor.emitsRightReleasedEvent()) {
        return true;
    }

    if (aCursor.emitsLeftPressedEvent()) {
        if (!mActiveBone) {
            Bone2* chosen = hovered ? hovered : mKeyOwner.key->data().nearestBone(targetPos(aCursor));
            selectBone(chosen);
        }
        if (!mActiveBone) {
            return true;
        }
        beginStroke(aCursor.pressure());
    } else if (aCursor.emitsLeftDraggedEvent()) {
        if (mIsBrushDrawing) {
            mStroke.move(mCursorPos, aCursor.pressure());
        }
    } else if (aCursor.emitsLeftReleasedEvent()) {
        if (mIsBrushDrawing) {
            // The final stamps are applied before the gesture closes: a click
            // without a following frame must still paint.
            mStroke.end();
            flushPendingWork();
            mIsBrushDrawing = false;
            mCommand = nullptr;
            mEdits.clear();
        }
    }

    return true;
}

bool PaintWeightsMode::beginStroke(float aPressure) {
    if (mKeyOwner.owns()) {
        fail(UILog::tr("There is no bone key at this frame; add a bone key before painting weights."));
        return false;
    }
    if (!prepareTargets()) {
        return false;
    }
    // A stroke that can start has no refusal to explain any more; the panel must
    // not keep the previous one beside a live stroke.
    mLastError.clear();
    mIsBrushDrawing = true;
    mCommand = nullptr;
    mQueuedStamps.clear();
    // The stroke owns its own brush copy (BrushStroke::begin takes the param by
    // value), so a slider change mid-stroke cannot change the stamps already
    // committed to the frame's queue.
    mStroke.begin(mParam.brush, mCursorPos, aPressure, [this](const BrushStamp& aStamp) {
        mQueuedStamps.push_back(aStamp);
    });
    return true;
}

bool PaintWeightsMode::prepareTargets() {
    mEdits.clear();
    if (!mKeyOwner.key || mKeyOwner.owns() || !mActiveBone) {
        return false;
    }

    const WeightState state = weightState();
    if (!state.canPaint()) {
        switch (state.kind) {
        case WeightState::Kind_Envelope:
            fail(UILog::tr("Bake before painting."));
            break;
        case WeightState::Kind_NoTargets:
            fail(UILog::tr("No mesh to paint."));
            break;
        case WeightState::Kind_SelectBone:
            fail(UILog::tr("Select a bone first."));
            break;
        case WeightState::Kind_None:
            fail(UILog::tr("No bone key at this frame."));
            break;
        default:
            fail(UILog::tr("No baked weights to paint."));
            break;
        }
        return false;
    }

    const TimeInfo time = keyTime();
    QVector<ObjectNode*> targets;
    mKeyOwner.key->collectInfluenceTargets(time, mTarget, targets);

    const int boneId = mActiveBone->boneId();
    for (ObjectNode* node : targets) {
        if (!node) {
            continue;
        }
        const LayerMesh* mesh = TimeKeyBlender::getAreaMesh(*node, time);
        if (!mesh || mesh->vertexCount() <= 0) {
            continue;
        }
        const SkinWeights::NodeWeights* record = mTarget.skinWeights().find(*node);
        if (!record || !record->isBaked(boneId)) {
            continue;   // a live layer is not part of this brush's target set
        }

        EditTarget edit;
        edit.node = node;
        edit.mesh = mesh;
        edit.working = *record;
        // The exact product the influence build uses (BoneKey::updateCaches):
        // the owner's world matrix times the map matrix. The brush is a WORLD
        // circle, so that is the space the falloff is measured in.
        edit.worldMapMtx =
            mTargetMtx * TimeKeyBlender::getInfluenceMapMatrix(*node, time, &mTarget);

        const int vertices = mesh->vertexCount();
        const gl::Vector3* positions = mesh->positions();
        edit.worldPositions.resize(vertices);
        edit.cell = std::max(mParam.brush.radius, 8.0f);
        for (int i = 0; i < vertices; ++i) {
            const QVector2D world =
                edit.worldMapMtx.map(QVector3D(positions[i].x, positions[i].y, 0.0f)).toVector2D();
            edit.worldPositions[i] = world;
            const int x = int(std::floor(world.x() / edit.cell));
            const int y = int(std::floor(world.y() / edit.cell));
            edit.cells[cellKey(x, y)].push_back(i);
        }
        mEdits.push_back(edit);
    }

    if (mEdits.isEmpty()) {
        fail(UILog::tr("No baked mesh to paint."));
        return false;
    }
    return true;
}

void PaintWeightsMode::applyStamp(const BrushStamp& aStamp) {
    const int boneId = mActiveBone ? mActiveBone->boneId() : 0;
    if (boneId == 0) {
        return;
    }
    const float radius = aStamp.radius;
    if (radius <= 0.0f) {
        return;
    }

    for (EditTarget& edit : mEdits) {
        SkinWeights::BakedBone* baked = edit.working.bakedFor(boneId);
        if (!baked) {
            continue;
        }

        // Only the cells the stamp's square overlaps are visited; a dense mesh's
        // untouched half costs nothing. The cell size is fixed at the stroke's
        // first stamp, so the range is a box in the same grid.
        const int minX = int(std::floor((aStamp.center.x() - radius) / edit.cell));
        const int maxX = int(std::floor((aStamp.center.x() + radius) / edit.cell));
        const int minY = int(std::floor((aStamp.center.y() - radius) / edit.cell));
        const int maxY = int(std::floor((aStamp.center.y() + radius) / edit.cell));

        for (int cy = minY; cy <= maxY; ++cy) {
            for (int cx = minX; cx <= maxX; ++cx) {
                const auto itr = edit.cells.constFind(cellKey(cx, cy));
                if (itr == edit.cells.constEnd()) {
                    continue;
                }
                for (int vertex : itr.value()) {
                    const float distance = (edit.worldPositions[vertex] - aStamp.center).length();
                    const float falloff = brushFalloff(distance, radius, aStamp.hardness);
                    if (falloff <= 0.0f) {
                        continue;
                    }

                    const float amount = falloff * aStamp.amount;
                    const float before = baked->weightAt(quint32(vertex));
                    // THE DEPOSIT RULE lives in ctrl::applyWeightStamp, so Add,
                    // Subtract and Replace are one tested expression rather than a
                    // switch only the live brush can reach.
                    const float after = applyWeightStamp(mParam.weightMode, before, amount);
                    if (baked->setWeight(quint32(vertex), after)) {
                        edit.changed = true;
                    }
                }
            }
        }
    }
}

void PaintWeightsMode::flushPendingWork() {
    if (mQueuedStamps.isEmpty()) {
        return;
    }

    // AN UNDO OR ANOTHER COMMAND MID-STROKE. The command this stroke was
    // rewriting is no longer the stack's editable one, so its document state is
    // not the working state any more. Drop the transient stroke rather than
    // re-commit a partial edit on top of an unrelated undo.
    if (mCommand && !mProject.commandStack().isModifiable(mCommand)) {
        cancelStroke();
        return;
    }

    QVector<BrushStamp> stamps = std::move(mQueuedStamps);
    mQueuedStamps.clear();
    bool changed = false;
    for (const BrushStamp& stamp : stamps) {
        applyStamp(stamp);
    }
    for (const EditTarget& edit : mEdits) {
        changed = changed || edit.changed;
    }
    if (!changed) {
        return;
    }

    cmnd::Stack& stack = mProject.commandStack();
    if (mCommand && stack.isModifiable(mCommand)) {
        // Rewrite the open stroke: one command, one undo step, one rebuild at
        // the frame boundary. The command's before state is still the stroke's
        // first stamp.
        for (EditTarget& edit : mEdits) {
            edit.changed = false;
            mCommand->setAfter(edit.working);
            mTarget.skinWeights().setNodeWeights(edit.working);
        }
        mProject.onSkinWeightsModified(mTarget, false);
    } else {
        QVector<SkinWeights::NodeWeights> before;
        QVector<SkinWeights::NodeWeights> after;
        before.reserve(mEdits.size());
        after.reserve(mEdits.size());
        for (EditTarget& edit : mEdits) {
            edit.changed = false;
            const SkinWeights::NodeWeights* existing = mTarget.skinWeights().find(*edit.node);
            before.push_back(existing ? *existing : SkinWeights::NodeWeights());
            after.push_back(edit.working);
        }

        auto* command = new AssignSkinWeightsCommand(
            mProject, mTarget, before, after, CmndName::tr("Paint bone weights")
        );
        if (command->isUseless()) {
            delete command;
        } else {
            mCommand = command;
            // exec writes the records and emits the owner's rebuild event.
            stack.push(command);
        }
    }
    notifyState();
}

void PaintWeightsMode::cancelStroke() {
    mStroke.end();
    mQueuedStamps.clear();
    mEdits.clear();
    mIsBrushDrawing = false;
    mCommand = nullptr;
    notifyState();
}

void PaintWeightsMode::renderQt(const RenderInfo& aInfo, QPainter& aPainter) {
    Bone2* active = mActiveBone ? mActiveBone : mHoverBone;

    if (mParam.showWeightHeatmap && active) {
        drawWeightHeatmap(aPainter, aInfo, *mKeyOwner.key, *active, mTargetMtx);
    }

    bone::Renderer renderer(aPainter, aInfo);
    renderer.setAntialiasing(true);
    renderer.setFocusConnector(true);
    renderer.setTargetMatrix(mTargetMtx);

    for (auto bone : mKeyOwner.key->data().topBones()) {
        renderer.renderBones(bone);
    }

    // The brush ring: size, then the hard core. Subtract is dotted, the same
    // reading the influence eraser has.
    {
        const QColor idleColor(100, 100, 255, 128);
        const QColor focusColor(255, 255, 255, 255);
        const QBrush ring(mIsBrushDrawing ? focusColor : idleColor);
        const Qt::PenStyle style =
            mParam.weightMode == WeightPaint_Subtract ? Qt::DotLine : Qt::SolidLine;
        aPainter.setBrush(Qt::NoBrush);
        aPainter.setPen(QPen(ring, 1.2f, style));
        const QVector2D center = aInfo.camera.toScreenPos(mCursorPos);
        const float radius = aInfo.camera.toScreenLength(mParam.brush.radius);
        if (radius > 0.0f) {
            aPainter.drawEllipse(center.toPointF(), radius, radius);
            const float core =
                aInfo.camera.toScreenLength(mParam.brush.radius * mParam.brush.hardness);
            if (core > 1.0f) {
                QColor coreColor = ring.color();
                coreColor.setAlpha(96);
                aPainter.setPen(QPen(QBrush(coreColor), 1.0f));
                aPainter.drawEllipse(center.toPointF(), core, core);
            }
        }
    }
}

WeightState PaintWeightsMode::weightState() const {
    WeightState state;
    if (!mKeyOwner.key || mKeyOwner.owns()) {
        return state;
    }
    state = computeWeightState(mProject, mTarget, *mKeyOwner.key, mActiveBone);
    state.error = mLastError;
    return state;
}

bool PaintWeightsMode::bakeWeights(bool aReplace, QString* aError) {
    if (!mActiveBone) {
        if (aError) {
            *aError = UILog::tr("Select a bone first.");
        }
        return false;
    }
    if (mKeyOwner.owns() || !mKeyOwner.key) {
        if (aError) {
            *aError = UILog::tr("There is no bone key at this frame.");
        }
        return false;
    }
    if (mIsBrushDrawing) {
        mStroke.end();
        flushPendingWork();
        mIsBrushDrawing = false;
        mEdits.clear();
        mCommand = nullptr;
    }

    QString error;
    if (!bakeBoneWeights(mProject, mTarget, *mKeyOwner.key, *mActiveBone, aReplace, &error)) {
        if (aError) {
            *aError = error;
        }
        fail(error);
        return false;
    }
    clearError();
    notifyState();
    return true;
}

bool PaintWeightsMode::revertWeights(QString* aError) {
    if (!mActiveBone) {
        if (aError) {
            *aError = UILog::tr("Select a bone first.");
        }
        return false;
    }
    if (mKeyOwner.owns() || !mKeyOwner.key) {
        if (aError) {
            *aError = UILog::tr("There is no bone key at this frame.");
        }
        return false;
    }
    QString error;
    if (!revertBoneWeights(mProject, mTarget, *mActiveBone, &error)) {
        if (aError) {
            *aError = error;
        }
        fail(error);
        return false;
    }
    clearError();
    notifyState();
    return true;
}

bool PaintWeightsMode::clearWeights(QString* aError) {
    if (!mActiveBone) {
        if (aError) {
            *aError = UILog::tr("Select a bone first.");
        }
        return false;
    }
    if (mKeyOwner.owns() || !mKeyOwner.key) {
        if (aError) {
            *aError = UILog::tr("There is no bone key at this frame.");
        }
        return false;
    }
    QString error;
    if (!clearBoneWeights(mProject, mTarget, *mActiveBone, &error)) {
        if (aError) {
            *aError = error;
        }
        fail(error);
        return false;
    }
    clearError();
    notifyState();
    return true;
}

} // namespace bone
} // namespace ctrl
