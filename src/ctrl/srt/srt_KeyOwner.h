#ifndef CTRL_SRT_KEYOWNER_H
#define CTRL_SRT_KEYOWNER_H

#include "cmnd/Stack.h"
#include "core/TimeLine.h"
#include "core/MoveKey.h"
#include "core/RotateKey.h"
#include "core/ScaleKey.h"
namespace core {
class TimeKeyExpans;
}

namespace ctrl {
namespace srt {

    struct KeyOwner {
        KeyOwner();
        ~KeyOwner();

        explicit operator bool() const { return moveKey && rotateKey && scaleKey; }
        bool ownsSomeKeys() const { return ownsMoveKey || ownsRotateKey || ownsScaleKey; }
        void deleteOwningKeys();

        void pushOwningMoveKey(cmnd::Stack& aStack, core::TimeLine& aLine, int aFrame);
        void pushOwningRotateKey(cmnd::Stack& aStack, core::TimeLine& aLine, int aFrame);
        void pushOwningScaleKey(cmnd::Stack& aStack, core::TimeLine& aLine, int aFrame);

        bool updatePosture(const core::TimeKeyExpans& aExpans);

        QMatrix4x4 getLocalSRTMatrixFromKeys() const;
        QMatrix4x4 getLocalSRMatrixFromKeys() const;

        core::MoveKey* moveKey;
        core::RotateKey* rotateKey;
        core::ScaleKey* scaleKey;
        bool ownsMoveKey;
        bool ownsRotateKey;
        bool ownsScaleKey;
        // A migrated property stores its value in a channel map. The typed key
        // above is then a PROXY owned by this object, seeded from the track and
        // written back by the SRT command; ownsRotateKey still means "a new key
        // must be created at the playhead".
        bool rotateIsTrack;
        bool rotateTrackProxy;
        bool scaleIsTrack;
        bool scaleTrackProxy;
        bool moveIsTrack;
        bool moveTrackProxy;
        // The key pointer is the timeline's BASE VALUE (a static transform), not a
        // key at the playhead: an edit writes it in place and must not push a key.
        bool moveIsBaseValue;
        bool rotateIsBaseValue;
        bool scaleIsBaseValue;

        QMatrix4x4 parentMtx;
        QMatrix4x4 invParentMtx;
        QMatrix4x4 invParentSRMtx;
        QMatrix4x4 locSRMtx;
        QMatrix4x4 locSRTMtx;
        QMatrix4x4 locCSRTMtx;
        bool isBound;
        bool hasInv;
    };

} // namespace srt
} // namespace ctrl

#endif // CTRL_SRT_KEYOWNER_H
