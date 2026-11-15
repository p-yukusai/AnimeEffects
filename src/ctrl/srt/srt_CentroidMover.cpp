#include "core/TimeKeyBlender.h"
#include "core/Project.h"
#include "core/TimeKeyBlender.h"
#include "core/MoveKey.h"
#include "core/ImageKey.h"
#include "core/MeshKey.h"
#include "ctrl/srt/srt_CentroidMover.h"

using namespace core;

namespace ctrl {
namespace srt {

#if 0
//-------------------------------------------------------------------------------------------------
void addAllKeysToEvent(ObjectNode& aTarget, TimeKeyType aType, TimeLineEvent& aEvent, bool aContainBaseValue)
{
    XC_PTR_ASSERT(aTarget.timeLine());
    auto& line = *aTarget.timeLine();
    {
        auto& map = line.map(aType);
        for (auto itr = map.begin(); itr != map.end(); ++itr)
        {
            aEvent.pushTarget(aTarget, aType, itr.key());
        }
    }

    if (aContainBaseValue && line.baseValue(aType))
    {
        aEvent.pushBaseValueTarget(aTarget, aType);
    }

}

//-------------------------------------------------------------------------------------------------
void CentroidMover::pushEventTargets(
        ObjectNode& aTarget, TimeLineEvent& aEvent, bool aAdjustPostures)
{
    if (aAdjustPostures)
    {
        addAllKeysToEvent(aTarget, TimeKeyType_Move, aEvent, true);
    }
    addAllKeysToEvent(aTarget, TimeKeyType_Mesh, aEvent, true);
    addAllKeysToEvent(aTarget, TimeKeyType_Image, aEvent, true);

    for (auto child : aTarget.children())
    {
        addAllKeysToEvent(*child, TimeKeyType_Move, aEvent, true);
    }
}

//-------------------------------------------------------------------------------------------------
CentroidMover::CentroidMover(Project& aProject,
                             ObjectNode& aTarget,
                             const QVector2D& aPrev,
                             const QVector2D& aNext,
                             bool aAdjustPostures)
    : mProject(aProject)
    , mTarget(aTarget)
    , mPrev(aPrev)
    , mNext(aNext)
    , mAdjustsPostures(aAdjustPostures)
    , mKeys()
    , mChildKeys()
    , mImageKeys()
    , mMeshKeys()
    , mDone()
    , mExecuteOnce()
{
}

QMatrix4x4 CentroidMover::getLocalSRMatrix(const TimeKey& aKey)
{
    if (aKey.frame() == TimeLine::kBaseValueIndex)
    {
        return QMatrix4x4();
    }
    auto time = mProject.currentTimeInfo();
    time.frame.set(aKey.frame());
    return TimeKeyBlender::getLocalSRMatrix(mTarget, time);
}

void CentroidMover::modifyValue(const QVector2D& aNext)
{
    mNext = aNext;

    if (mExecuteOnce)
    {
        const QVector2D keyMove = mNext - mPrev;
        const QVector3D keyMove3D(keyMove);

        // update moveKeys
        for (auto& key : mKeys)
        {
            key.next = key.prev + (getLocalSRMatrix(*key.ptr) * keyMove3D).toVector2D();
        }

        // update child moveKeys
        for (auto& key : mChildKeys)
        {
            key.next = key.prev - keyMove;
        }

        // update imageKeys
        for (auto& key : mImageKeys)
        {
            key.next = key.prev - keyMove;
        }

        // update meshKeys
        for (auto& key : mMeshKeys)
        {
            key.next = key.prev - keyMove;
        }

        if (mDone)
        {
            for (auto& key : mKeys)
            {
                key.ptr->setPos(key.next);
            }
            for (auto& key : mChildKeys)
            {
                key.ptr->setPos(key.next);
            }
            for (auto& key : mImageKeys)
            {
                key.ptr->data().setImageOffset(key.next);
            }
            for (auto& key : mMeshKeys)
            {
                key.ptr->data().setOriginOffset(key.next);
            }
        }
    }
}

void CentroidMover::addAllTargets(
        ObjectNode& aTarget, TimeKeyType aType, bool aContainBaseValue,
        const std::function<void(TimeKey*)>& aPusher)
{
    XC_PTR_ASSERT(aTarget.timeLine());

    auto& map = aTarget.timeLine()->map(aType);
    for (auto itr = map.begin(); itr != map.end(); ++itr)
    {
        auto key = itr.value();
        aPusher(key);
    }

    if (aContainBaseValue)
    {
        auto baseValue = aTarget.timeLine()->baseValue(aType);
        if (baseValue)
        {
            aPusher(baseValue);
        }
    }
}

void CentroidMover::exec()
{
    const QVector2D keyMove = mNext - mPrev;
    const QVector3D keyMove3D(keyMove);

    if (mAdjustsPostures)
    {
        // translate moveKeys
        addAllTargets(mTarget, TimeKeyType_Move, true, [=](TimeKey* aKey)
        {
            TIMEKEY_PTR_TYPE_ASSERT(aKey, Move);
            MoveKey* moveKey = (MoveKey*)aKey;
            const QVector2D prev = moveKey->pos();
            const QVector2D next = prev + (getLocalSRMatrix(*moveKey) * keyMove3D).toVector2D();
            KeyData keyData = { moveKey, prev, next };
            this->mKeys.push_back(keyData);
        });
    }

    // translate imageKeys negatively
    addAllTargets(mTarget, TimeKeyType_Image, true, [=](TimeKey* aKey)
    {
        TIMEKEY_PTR_TYPE_ASSERT(aKey, Image);
        ImageKey* imageKey = (ImageKey*)aKey;
        const QVector2D prev = imageKey->data().imageOffset();
        const QVector2D next = prev - keyMove;
        ImageKeyData keyData = { imageKey, prev, next };
        this->mImageKeys.push_back(keyData);
    });

    // translate meshKeys negatively
    addAllTargets(mTarget, TimeKeyType_Mesh, true, [=](TimeKey* aKey)
    {
        TIMEKEY_PTR_TYPE_ASSERT(aKey, Mesh);
        MeshKey* meshKey = (MeshKey*)aKey;
        const QVector2D prev = meshKey->data().originOffset();
        const QVector2D next = prev - keyMove;
        MeshKeyData keyData = { meshKey, prev, next };
        this->mMeshKeys.push_back(keyData);
    });

    if (mTarget.canHoldChild())
    {
        // translate child moveKeys negatively
        for (auto child : mTarget.children())
        {
            addAllTargets(*child, TimeKeyType_Move, true, [=](TimeKey* aKey)
            {
                TIMEKEY_PTR_TYPE_ASSERT(aKey, Move);
                MoveKey* moveKey = (MoveKey*)aKey;
                const QVector2D prev = moveKey->pos();
                const QVector2D next = prev - keyMove;
                ChildKeyData keyData = { moveKey, prev, next };
                this->mChildKeys.push_back(keyData);
            });
        }
    }

    redo();

    mExecuteOnce = true;
}

void CentroidMover::undo()
{
    for (auto& key : mKeys)
    {
        key.ptr->setPos(key.prev);
    }
    for (auto& key : mChildKeys)
    {
        key.ptr->setPos(key.prev);
    }
    for (auto& key : mImageKeys)
    {
        key.ptr->data().setImageOffset(key.prev);
    }
    for (auto& key : mMeshKeys)
    {
        key.ptr->data().setOriginOffset(key.prev);
    }
    mDone = false;
}

void CentroidMover::redo()
{
    for (auto& key : mKeys)
    {
        key.ptr->setPos(key.next);
    }
    for (auto& key : mChildKeys)
    {
        key.ptr->setPos(key.next);
    }
    for (auto& key : mImageKeys)
    {
        key.ptr->data().setImageOffset(key.next);
    }
    for (auto& key : mMeshKeys)
    {
        key.ptr->data().setOriginOffset(key.next);
    }
    mDone = true;
}
#else
    //-------------------------------------------------------------------------------------------------
    CentroidMover::CentroidMover(
        ObjectNode& aTarget,
        const QVector2D& aCentroidMove,
        const QVector2D& aPositionMove,
        int aFrame,
        bool aAdjustPos,
        bool aTrack
    ):
        mTarget(aTarget),
        mKey(),
        mLine(),
        mTracks(),
        mKeys(),
        mTrack(aTrack),
        mCentroidMove(aCentroidMove),
        mPositionMove(aPositionMove),
        mPrevPosition(),
        mPrevCentroid(),
        mFrame(aFrame),
        mAdjustPos(aAdjustPos),
        mDone() {
        if (mTrack) {
            mLine = mTarget.timeLine();
            for (int c = 0; c < 4; ++c) {
                mTracks[c] = mLine->channelTrack(TimeKeyType_Move, c);
                if (mFrame != TimeLine::kBaseValueIndex) {
                    mKeys[c] = mLine->channelKey(TimeKeyType_Move, c, mFrame);
                }
            }
        }
    }

    void CentroidMover::modifyValue(const QVector2D& aNewCentroidMove, const QVector2D& aNewPositionMove) {
        mCentroidMove = aNewCentroidMove;
        mPositionMove = aNewPositionMove;

        if (mKey && mDone) {
            mKey->setCentroid(mPrevCentroid - mCentroidMove);
            if (mAdjustPos) {
                mKey->setPos(mPrevPosition + mPositionMove);
            }
        }
    }

    void CentroidMover::exec() {
        if (mTrack) {
            mPrevPosition = QVector2D(
                mKeys[0] ? mKeys[0]->value() : mTracks[0]->baseValue(),
                mKeys[1] ? mKeys[1]->value() : mTracks[1]->baseValue()
            );
            mPrevCentroid = QVector2D(
                mKeys[2] ? mKeys[2]->value() : mTracks[2]->baseValue(),
                mKeys[3] ? mKeys[3]->value() : mTracks[3]->baseValue()
            );
            redo();
            return;
        }

        mKey = (MoveKey*)(mFrame == TimeLine::kBaseValueIndex
                              ? mTarget.timeLine()->baseValue(TimeKeyType_Move)
                              : mTarget.timeLine()->timeKey(TimeKeyType_Move, mFrame));
        XC_PTR_ASSERT(mKey);
        mPrevPosition = mKey->pos();
        mPrevCentroid = mKey->centroid();
        redo();
    }

    void CentroidMover::undo() {
        if (mTrack) {
            const float pos[2] = { mPrevPosition.x(), mPrevPosition.y() };
            const float centroid[2] = { mPrevCentroid.x(), mPrevCentroid.y() };
            for (int c = 0; c < 4; ++c) {
                const float value = c < 2 ? pos[c] : centroid[c - 2];
                if (mKeys[c]) {
                    mKeys[c]->setValue(value);
                } else {
                    mTracks[c]->setBaseValue(value);
                }
            }
            mDone = false;
            return;
        }
        mKey->setCentroid(mPrevCentroid);
        mKey->setPos(mPrevPosition);
        mDone = false;
    }

    void CentroidMover::redo() {
        if (mTrack) {
            const QVector2D pos = mAdjustPos ? mPrevPosition + mPositionMove : mPrevPosition;
            const QVector2D centroid = mPrevCentroid - mCentroidMove;
            const float values[4] = { pos.x(), pos.y(), centroid.x(), centroid.y() };
            for (int c = 0; c < 4; ++c) {
                if (!mTracks[c]) {
                    continue;
                }
                if (mKeys[c]) {
                    mKeys[c]->setValue(mTracks[c]->clamp(mTracks[c]->quantize(values[c])));
                } else {
                    mTracks[c]->setBaseValue(values[c]);
                }
            }
            mDone = true;
            return;
        }
        mKey->setCentroid(mPrevCentroid - mCentroidMove);
        if (mAdjustPos) {
            mKey->setPos(mPrevPosition + mPositionMove);
        }
        mDone = true;
    }
#endif

} // namespace srt
} // namespace ctrl
