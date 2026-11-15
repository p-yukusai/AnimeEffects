#include "cmnd/ScopedMacro.h"
#include "cmnd/BasicCommands.h"
#include "gl/Global.h"
#include "ctrl/TimeLineUtil.h"
#include "ctrl/CmndName.h"
#include "ctrl/ffd/ffd_BrushMode.h"
#include "core/FFDKey.h"

using namespace core;

namespace ctrl {
namespace ffd {

    //-------------------------------------------------------------------------------------------------
    BrushMode::Status::Status(): state(State_Idle), commandRef() {}

    void BrushMode::Status::clear() {
        state = State_Idle;
        commandRef = nullptr;
    }

    bool BrushMode::Status::isDrawing() const { return state == State_Draw; }

    //-------------------------------------------------------------------------------------------------
    BrushMode::BrushMode(core::Project& aProject, Targets& aTargets):
        mProject(aProject), mTargets(aTargets), mParam(), mStatus(), mStroke() {}

    void BrushMode::updateParam(const FFDParam& aParam) {
        mParam = aParam;
        mParam.setType(aParam.type); // keeps brush.erase in step with the mode
    }

    bool BrushMode::updateCursor(const core::CameraInfo&, const core::AbstractCursor& aCursor) {
        if (mStatus.state == State_Idle) {
            mCursorPos = aCursor.worldPos();

            if (aCursor.emitsLeftPressedEvent()) {
                mStatus.commandRef = nullptr;
                if (mParam.brush.radius > 0.0f && mTargets.hasValidTarget()) {
                    mStatus.state = State_Draw;
                    // The shared stroke (ctrl/Brush.h) turns the press, the drags and
                    // the release into spaced stamps. One stamp here is one task, so a
                    // fast stroke is a continuous band instead of a row of dots.
                    mStroke.begin(
                        mParam.brush, aCursor.worldPos(), aCursor.pressure(),
                        [this](const BrushStamp& aStamp) { executeStamp(aStamp); }
                    );
                }
            }
        } else if (mStatus.state == State_Draw) {
            mCursorPos = aCursor.worldPos();

            if (aCursor.emitsLeftDraggedEvent()) {
                mStroke.move(aCursor.worldPos(), aCursor.pressure());
            } else if (aCursor.emitsLeftReleasedEvent()) {
                mStroke.end();
                mStatus.clear();
            }
        }

        return true;
    }

    void BrushMode::renderQt(const core::RenderInfo& aInfo, QPainter& aPainter) {
        // The brush: the outer circle is the SIZE, the inner ring the HARD CORE
        // (radius * hardness) - so the tool shows the same two numbers the panel
        // does, in the same units, while painting.
        {
            const QColor idleColor(100, 100, 255, 128);
            const QColor focusColor(255, 255, 255, 255);

            const QBrush centerBrush(mStatus.isDrawing() ? focusColor : idleColor);
            const Qt::PenStyle style = mParam.brush.erase ? Qt::DotLine : Qt::SolidLine;
            aPainter.setPen(QPen(centerBrush, 1.2f, style));
            aPainter.setBrush(Qt::NoBrush);

            const QVector2D center = aInfo.camera.toScreenPos(mCursorPos);
            const float radius = aInfo.camera.toScreenLength(mParam.brush.radius);
            aPainter.drawEllipse(center.toPointF(), radius, radius);

            const float core = aInfo.camera.toScreenLength(mParam.brush.radius * mParam.brush.hardness);
            if (core > 1.0f) {
                QColor coreColor = centerBrush.color();
                coreColor.setAlpha(96);
                aPainter.setPen(QPen(QBrush(coreColor), 1.0f));
                aPainter.drawEllipse(center.toPointF(), core, core);
            }
        }
    }

    void BrushMode::flushPendingWork() {
        // THE BATCH: every stamp the frame's samples produced is executed here, in one pass over the
        // queue, after the pointer has been drained. Per-stamp execution inside the event handler
        // meant one transform-feedback pass (and one command rewrite) per sample, which is what made
        // a fast warping stroke collapse - the loop was busy in the GPU task, not reading the pointer.
        if (mQueuedStamps.isEmpty())
            return;
        QVector<BrushStamp> stamps = std::move(mQueuedStamps);
        mQueuedStamps.clear();
        for (const BrushStamp& stamp : stamps)
            executeStampNow(stamp);
    }


    bool BrushMode::executeStamp(const BrushStamp& aStamp) {
        mQueuedStamps.push_back(aStamp);
        return true;
    }


    bool BrushMode::executeStampNow(const BrushStamp& aStamp) {
        static const size_t kCopySize = 1024;

        // setup input buffers
        gl::Global::makeCurrent();

        // request gl task
        for (int i = 0; i < mTargets.size(); ++i) {
            auto task = mTargets[i]->task.data();
            ObjectNode* node = mTargets[i]->node;
            LayerMesh* mesh = mTargets[i]->keyOwner.getParentMesh(node);
            ffd::KeyOwner& owner = mTargets[i]->keyOwner;
            FFDKey* key = owner.key;
            XC_PTR_ASSERT(node);
            XC_PTR_ASSERT(mesh);
            XC_PTR_ASSERT(key);
            if (mesh->vertexCount() != key->data().count()) {
                key->data().allocAndWrite(key->data().positions(), mesh->vertexCount());
            }
            XC_ASSERT(mesh->vertexCount() == key->data().count());

            // set task type
            task->setType(aStamp.erase ? Task::Type_Eraser : Task::Type_Deformer);

            // write vertex positions
            task->writeSrc(node->timeLine()->current(), key->data().positions(), *mesh, mParam);

            // set brush
            task->setBrush(aStamp);

            // execute
            task->request();
        }

        // make commands
        {
            cmnd::Stack& stack = mProject.commandStack();
            const int frame = mProject.animator().currentFrame().get();

            if (!mStatus.commandRef) {
                cmnd::ScopedMacro macro(stack, CmndName::tr("Update FFD"));

                // set notifier
                auto notifier = new TimeLineUtil::Notifier(mProject);
                macro.grabListener(notifier);
                notifier->event().setType(TimeLineEvent::Type_PushKey);
                for (int i = 0; i < mTargets.size(); ++i) {
                    XC_PTR_ASSERT(mTargets[i]->node->timeLine());
                    notifier->event().pushTarget(*mTargets[i]->node, TimeKeyType_FFD, frame);
                }

                // push owns keys
                for (int i = 0; i < mTargets.size(); ++i) {
                    XC_PTR_ASSERT(mTargets[i]->keyOwner.key);

                    if (mTargets[i]->keyOwner.owns()) {
                        mTargets[i]->keyOwner.pushOwnsKey(stack, *mTargets[i]->node->timeLine(), frame);
                    }
                }

                // create deform command
                mStatus.commandRef = new ffd::MoveVertices();

                // push memory assign commands
                for (int i = 0; i < mTargets.size(); ++i) {
                    FFDKey* key = mTargets[i]->keyOwner.key;
                    ffd::Task* task = mTargets[i]->task.data();
                    XC_PTR_ASSERT(key->data().positions());
                    XC_PTR_ASSERT(task->dstMesh());

                    // wait end of task
                    task->finish();

                    mStatus.commandRef->push(
                        new cmnd::AssignMemory(key->data().positions(), task->dstMesh(), task->dstSize(), kCopySize)
                    );
                }

                // push deform command
                stack.push(mStatus.commandRef);
            } else {
                const bool modifiable = stack.isModifiable(mStatus.commandRef);
                TimeLineEvent event;
                event.setType(TimeLineEvent::Type_ChangeKeyValue);

                for (int i = 0; i < mTargets.size(); ++i) {
                    FFDKey* key = mTargets[i]->keyOwner.key;
                    (void)key;
                    ffd::Task* task = mTargets[i]->task.data();
                    ObjectNode& node = *mTargets[i]->node;

                    // wait end of task
                    task->finish();

                    if (modifiable) {
                        // modify value
                        auto assign = mStatus.commandRef->assign(i);
                        XC_ASSERT(assign->size() == task->dstSize());
                        XC_ASSERT(assign->target() == key->data().positions());
                        assign->modifyValue(task->dstMesh());

                        // push event target
                        event.pushTarget(node, TimeKeyType_FFD, frame);
                    }
                }

                // notify
                if (event.targets().size()) {
                    mProject.onTimeLineModified(event, false);
                }

                return modifiable;
            }
        }
        return true;
    }

    //-------------------------------------------------------------------------------------------------
    void BrushMode::cancel()
    {
        mStroke.end();
        mStatus.clear();
    }

} // namespace ffd
} // namespace ctrl
