#ifndef CTRL_SRT_ASSIGNKEYCOMMAND_H
#define CTRL_SRT_ASSIGNKEYCOMMAND_H

#include <QVector>
#include "cmnd/Stable.h"
#include "core/TimeKey.h"
#include "core/TimeKeyType.h"
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

} // namespace srt
} // namespace ctrl

#endif // CTRL_SRT_ASSIGNKEYCOMMAND_H
