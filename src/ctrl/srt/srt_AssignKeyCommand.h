#ifndef CTRL_SRT_ASSIGNKEYCOMMAND_H
#define CTRL_SRT_ASSIGNKEYCOMMAND_H

#include <QVector>
#include <QVector2D>
#include "cmnd/Stable.h"
#include "core/TimeKey.h"
#include "core/TimeKeyType.h"
#include "core/ChannelKey.h"
#include "core/ChannelTrack.h"
#include "core/TimeLine.h"
#include "ctrl/proportional/prop_Keys.h"

namespace ctrl {
namespace srt {

// ONE POSE DRAG'S VALUE WRITE, as the SRT modes' live command: the whole Data of
// one Move/Rotate/Scale key, rewritten on every drag step and put back on undo.
//
// It is cmnd::ModifiableAssign's shape (capture on exec, rewrite on
// modifyValue, restore on undo) with the PROPORTIONAL half added: while the drag
// moves a bound, the proportional keys of the same channel between that key and
// its stored neighbours follow it, by their own stored weights. So the drag is
// still ONE stack entry - modifyValue() redoes the same command - and the
// companions it carries ride the same entry, values AND weights.
//
// WHY THE KEY IS HELD, NOT ITS DATA FIELD: ctrl::proportional::ValueReflow needs
// the map the key lives in, which is (line, type) - a Data pointer knows neither.
// The key object is the one TimeLine::move re-keys and the SRT owner keeps alive
// for the whole gesture, exactly what the data pointer guaranteed before.
//
// tType is the key's own core::TimeKeyType (a compile-time parameter, so the
// reflow's group and the projection read the same map the SRT owner does). A key
// that is NOT proportional captures nothing, and every apply/restore is a no-op.
//
// For a key CREATED in the same macro the capture runs at exec - after the
// pusher that inserted it - which is what makes its first value edit part of the
// same one undo step.
template<typename tKey, core::TimeKeyType tType>
class AssignKeyCommand: public cmnd::Stable {
public:
    typedef typename tKey::Data Data;

    AssignKeyCommand(tKey& aKey, core::TimeLine& aLine, const Data& aValue):
        mKey(aKey), mLine(aLine), mReflow(), mPrev(), mNext(aValue), mDone(false) {}

    // A later step of the same gesture: the same command carries the new value,
    // and applies it at once when it is the stack's executed one. The reflow is
    // already captured, so it follows through the STORED weights again - never
    // by re-deriving them from the previous projection.
    void modifyValue(const Data& aValue) {
        mNext = aValue;
        if (mDone)
            redo();
    }

    virtual void exec() {
        // The value the key holds NOW is what undo restores - read at exec, after
        // the macro's own inserts, exactly as cmnd::ModifiableAssign did.
        mPrev = mKey.data();
        const QVector<core::TimeKey*> edited { static_cast<core::TimeKey*>(&mKey) };
        mReflow.capture(mLine, tType, edited);
        redo();
    }

    virtual void undo() {
        // Backwards of redo: the companions come back first, then the key the
        // drag wrote.
        mReflow.restore();
        mKey.data() = mPrev;
        mDone = false;
    }

    virtual void redo() {
        mKey.data() = mNext;
        mReflow.apply();
        mDone = true;
    }

private:
    tKey& mKey;
    core::TimeLine& mLine;
    proportional::ValueReflow mReflow;
    Data mPrev;
    Data mNext;
    bool mDone;
};


// ONE TRACK-BACKED TRANSFORM CHANNEL'S VALUE, rewritten by every drag step.
// The SRT modes still assemble a typed Move/Rotate/Scale Data; for a migrated
// property that Data is applied to the channel key (or the channel base) the
// proxy names. Proportional followers are not carried here yet; the migrated
// transform types do not have proportional keys until that module is ported.
class AssignChannelValueCommand: public cmnd::Stable {
public:
    AssignChannelValueCommand(
        core::ChannelTrack& aTrack, core::ChannelKey& aKey, float aValue
    ):
        mTrack(aTrack), mKey(aKey), mPrev(), mNext(aValue), mDone(false) {}

    void modifyValue(float aValue) {
        mNext = aValue;
        if (mDone)
            redo();
    }

    void exec() override {
        mPrev = mKey.value();
        redo();
    }
    void undo() override {
        mKey.setValue(mPrev);
        mDone = false;
    }
    void redo() override {
        mKey.setValue(mTrack.clamp(mTrack.quantize(mNext)));
        mDone = true;
    }

private:
    core::ChannelTrack& mTrack;
    core::ChannelKey& mKey;
    float mPrev;
    float mNext;
    bool mDone;
};

class AssignChannelBaseCommand: public cmnd::Stable {
public:
    AssignChannelBaseCommand(core::ChannelTrack& aTrack, float aValue):
        mTrack(aTrack), mPrev(), mNext(aValue), mDone(false) {}

    void modifyValue(float aValue) {
        mNext = aValue;
        if (mDone)
            redo();
    }

    void exec() override {
        mPrev = mTrack.baseValue();
        redo();
    }
    void undo() override {
        mTrack.setBaseValue(mPrev);
        mDone = false;
    }
    void redo() override {
        mTrack.setBaseValue(mNext);
        mDone = true;
    }

private:
    core::ChannelTrack& mTrack;
    float mPrev;
    float mNext;
    bool mDone;
};

// A whole SCALE key's two channel writes, rewritten by every drag step. The
// command names both channel keys (or neither, for the base pair) so one undo
// entry restores X and Y together.
class AssignScaleTrackCommand: public cmnd::Stable {
public:
    AssignScaleTrackCommand(
        core::ChannelTrack& aXTrack, core::ChannelKey* aXKey,
        core::ChannelTrack& aYTrack, core::ChannelKey* aYKey, const QVector2D& aValue
    ):
        mXTrack(aXTrack), mYTrack(aYTrack), mXKey(aXKey), mYKey(aYKey),
        mPrev(), mNext(aValue), mDone(false) {}

    void modifyValue(const QVector2D& aValue) {
        mNext = aValue;
        if (mDone)
            redo();
    }

    void exec() override {
        mPrev = current();
        redo();
    }
    void undo() override {
        write(mPrev);
        mDone = false;
    }
    void redo() override {
        write(mNext);
        mDone = true;
    }

private:
    QVector2D current() const {
        return QVector2D(
            mXKey ? mXKey->value() : mXTrack.baseValue(),
            mYKey ? mYKey->value() : mYTrack.baseValue()
        );
    }
    void write(const QVector2D& aValue) {
        if (mXKey) {
            mXKey->setValue(mXTrack.clamp(mXTrack.quantize(aValue.x())));
            mYKey->setValue(mYTrack.clamp(mYTrack.quantize(aValue.y())));
        } else {
            mXTrack.setBaseValue(aValue.x());
            mYTrack.setBaseValue(aValue.y());
        }
    }

    core::ChannelTrack& mXTrack;
    core::ChannelTrack& mYTrack;
    core::ChannelKey* mXKey;
    core::ChannelKey* mYKey;
    QVector2D mPrev;
    QVector2D mNext;
    bool mDone;
};

// A whole MOVE key: position X/Y and centroid X/Y, rewritten by every drag
// step. The state (spline mode) is untouched by a transform drag.
class AssignMoveTrackCommand: public cmnd::Stable {
public:
    struct Channels {
        core::ChannelTrack* track[4] = {};
        core::ChannelKey* key[4] = {};
    };

    AssignMoveTrackCommand(const Channels& aChannels, const QVector2D& aPos, const QVector2D& aCentroid):
        mChannels(aChannels), mPrevPos(), mPrevCentroid(), mNextPos(aPos), mNextCentroid(aCentroid), mDone(false) {}

    void modifyValue(const QVector2D& aPos, const QVector2D& aCentroid) {
        mNextPos = aPos;
        mNextCentroid = aCentroid;
        if (mDone)
            redo();
    }

    void exec() override {
        mPrevPos = currentPos();
        mPrevCentroid = currentCentroid();
        redo();
    }
    void undo() override {
        write(mPrevPos, mPrevCentroid);
        mDone = false;
    }
    void redo() override {
        write(mNextPos, mNextCentroid);
        mDone = true;
    }

private:
    QVector2D currentPos() const {
        return QVector2D(
            mChannels.key[0] ? mChannels.key[0]->value() : mChannels.track[0]->baseValue(),
            mChannels.key[1] ? mChannels.key[1]->value() : mChannels.track[1]->baseValue()
        );
    }
    QVector2D currentCentroid() const {
        return QVector2D(
            mChannels.key[2] ? mChannels.key[2]->value() : mChannels.track[2]->baseValue(),
            mChannels.key[3] ? mChannels.key[3]->value() : mChannels.track[3]->baseValue()
        );
    }
    void write(const QVector2D& aPos, const QVector2D& aCentroid) {
        const float values[4] = { aPos.x(), aPos.y(), aCentroid.x(), aCentroid.y() };
        for (int c = 0; c < 4; ++c) {
            if (!mChannels.track[c]) {
                continue;
            }
            if (mChannels.key[c]) {
                mChannels.key[c]->setValue(
                    mChannels.track[c]->clamp(mChannels.track[c]->quantize(values[c]))
                );
            } else {
                mChannels.track[c]->setBaseValue(values[c]);
            }
        }
    }

    Channels mChannels;
    QVector2D mPrevPos;
    QVector2D mPrevCentroid;
    QVector2D mNextPos;
    QVector2D mNextCentroid;
    bool mDone;
};
} // namespace srt
} // namespace ctrl

#endif // CTRL_SRT_ASSIGNKEYCOMMAND_H
