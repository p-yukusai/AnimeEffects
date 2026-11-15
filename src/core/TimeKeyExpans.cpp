#include "core/TimeKeyExpans.h"

namespace core {

TimeKeyExpans::TimeKeyExpans():
    mMasterCache(),
    mMasterCached(false),
    mKeyCaches(),
    mKeyCached(),
    mSRT(),
    mOpa(),
    mHSV(),
    mBlurX(0.0f),
    mBlurY(0.0f),
    mBlurAngle(0.0f),
    mWorldOpacity(1.0f),
    mDepth(),
    mWorldDepth(),
    mBone(),
    mPose(),
    mPoseParent(),
    mPosePalette(),
    mAreaMeshKey(),
    mFFD(),
    mFFDMesh(),
    mFFDMeshParent(),
    mAreaImageKey(),
    mImageOffset() {
    clearCaches();
}

void TimeKeyExpans::setMasterCache(Frame aFrame) {
    mMasterCache = aFrame;
    mMasterCached = true;
}

bool TimeKeyExpans::hasMasterCache(Frame aFrame) const { return mMasterCached && mMasterCache == aFrame; }

void TimeKeyExpans::clearMasterCache() {
    mMasterCached = false;
    mMasterCache.set(-1);
}

void TimeKeyExpans::setKeyCache(TimeKeyType aType, Frame aFrame) {
    mKeyCaches[aType] = aFrame;
    mKeyCached[aType] = true;
}

bool TimeKeyExpans::hasKeyCache(TimeKeyType aType, Frame aFrame) const {
    return mKeyCached[aType] && mKeyCaches[aType] == aFrame;
}

void TimeKeyExpans::clearCaches() {
    mMasterCached = false;
    mMasterCache.set(-1);

    for (int i = 0; i < TimeKeyType_TERM; ++i) {
        mKeyCached[i] = false;
        mKeyCaches[i].set(-1);
    }
    mSRT.clearSplineCache();
}

//-------------------------------------------------------------------------------------------------
const gl::Texture* TimeKeyExpans::areaTexture() const {
    return mAreaImageKey ? &(mAreaImageKey->cache().texture()) : nullptr;
}

img::BlendMode TimeKeyExpans::blendMode() const {
    return mAreaImageKey ? mAreaImageKey->data().blendMode() : img::BlendMode_Normal;
}

} // namespace core
