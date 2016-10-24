#ifndef CTRL_SRT_KEYOWNER_H
#define CTRL_SRT_KEYOWNER_H

#include "cmnd/Stack.h"
#include "core/TimeLine.h"
#include "core/SRTKey.h"
namespace core { class TimeKeyExpans; }

namespace ctrl {
namespace srt {

struct KeyOwner
{
    KeyOwner();

    explicit operator bool() const { return key; }
    bool owns() const { return ownsKey; }
    void pushOwnsKey(cmnd::Stack& aStack, core::TimeLine& aLine, int aFrame);
    void deleteOwnsKey();

    bool updatePosture(const core::TimeKeyExpans& aExpans);

    core::SRTKey* key;
    bool ownsKey;

    QMatrix4x4 mtx;
    QMatrix4x4 invMtx;
    QMatrix4x4 invSRMtx;
    QMatrix4x4 locMtx;
    QMatrix4x4 locSRMtx;
    bool hasInv;
};

} // namespace srt
} // namespace ctrl

#endif // CTRL_SRT_KEYOWNER_H
