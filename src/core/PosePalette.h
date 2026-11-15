#ifndef CORE_POSEPALETTE_H
#define CORE_POSEPALETTE_H

#include <array>
#include <vector>
#include <QMatrix4x4>
#include <QVector>
#include "XC.h"
#include "util/ArrayBlock.h"
#include "gl/Vector4.h"
#include "core/BoneKey.h"
#include "core/PoseKey.h"

namespace core {

class PosePalette {
public:
    // The skeleton's hard cap: the influence map stores a bone index as one
    // uint8, so a skeleton can use indices 0..255 (256 bones). This is the only
    // remaining cap. The palette travels in a uniform block, whose guaranteed
    // 16384 bytes hold all 256 dual quaternions (8192 bytes); a default-block
    // uniform array could not: 128 bones alone is its whole 1024-component
    // budget.
    enum { kMaxCount = 256 };

    struct KeyPair {
        const BoneKey::Data* origin;
        const PoseKey::Data* pose;
    };
    typedef QVector<KeyPair> KeyPairs;

    struct DualQuaternion {
        gl::Vector4 real;
        gl::Vector4 dual;
        const GLfloat* data() const { return (const GLfloat*)(this); }
    };

    static int getBoneIndex(const BoneKey::Data& aData, const Bone2& aBone);

    /// The no-transform entry: real (1,0,0,0), dual (0,0,0,0). A bone with no
    /// pose contribution (a cleared palette, or a skeleton larger than its pose
    /// key) reads this, which is what the old identity tail of the fixed array
    /// supplied.
    static DualQuaternion identityDualQuaternion();

    PosePalette();

    void build(const KeyPairs& aKeyPairs);
    void clear();

    /// The number of bones actually built. The arrays are sized to it (never to
    /// kMaxCount), so a small rig neither allocates nor uploads 256 entries; a
    /// palette that has not been built holds one identity entry, which is what a
    /// vertex with no influence samples.
    int usedCount() const { return mUsedCount; }

    /// Count is max(1, usedCount()): the identity fallback entry is always there.
    util::ArrayBlock<const QMatrix4x4> matrices() const;
    util::ArrayBlock<const DualQuaternion> dualQuaternions() const;

private:
    struct BonePair {
        const Bone2* origin;
        const Bone2* pose;
    };
    typedef std::array<BonePair, kMaxCount> BonePairs;
    int makeBoneOrigins(const KeyPairs& aSrc, BonePairs& aDst);
    int makeBonePoses(const KeyPairs& aSrc, BonePairs& aDst);
    void resetIdentity();
    static DualQuaternion makeDualQuaternion(const QQuaternion& aUnitQuat, const QVector3D& aTrans);

    // sized to max(1, mUsedCount) by build()/clear(); the identity entry at 0 is
    // the no-influence fallback and is never counted in mUsedCount
    std::vector<QMatrix4x4> mData;
    std::vector<DualQuaternion> mDualQuats;
    int mUsedCount;
    bool mIsUnit;
};

} // namespace core

#endif // CORE_POSEPALETTE_H
