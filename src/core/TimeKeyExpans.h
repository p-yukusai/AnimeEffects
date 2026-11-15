#ifndef CORE_TIMEKEYEXPANS_H
#define CORE_TIMEKEYEXPANS_H

#include <algorithm>
#include "gl/Texture.h"
#include "core/SRTExpans.h"
#include "core/MoveKey.h"
#include "core/RotateKey.h"
#include "core/ScaleKey.h"
#include "core/OpaKey.h"
#include "core/HsvKey.h"
#include "core/WaveKey.h"
#include "core/MaskKey.h"
#include "core/BoneKey.h"
#include "core/PoseKey.h"
#include "core/MeshKey.h"
#include "core/FFDKey.h"
#include "core/ImageKey.h"
#include "core/TimeKeyType.h"
#include "core/PosePalette.h"
#include "core/BoneExpans.h"

namespace core {

class VirtualTexture;

class TimeKeyExpans {
public:
    TimeKeyExpans();

    // This value is valid when all of my children and I have caches.
    void setMasterCache(Frame aFrame);
    bool hasMasterCache(Frame aFrame) const;
    void clearMasterCache();

    void setKeyCache(TimeKeyType aType, Frame aFrame);
    bool hasKeyCache(TimeKeyType aType, Frame aFrame) const;

    void clearCaches();

    // How many times THIS cache has been rebuilt by the blender. The blender writes
    // in place - the storage pointers a consumer reads are stable while the values
    // behind them change - so a consumer that caches a derived result (LayerNode's
    // skinned positions) tells the new contents from the old ones by this stamp. It
    // belongs to ONE cache: rebuilding `working` must not invalidate a result derived
    // from `current`. Monotonic; never reset.
    quint64 revision() const { return mRevision; }
    void bumpRevision() { ++mRevision; }

    SRTExpans& srt() { return mSRT; }
    const SRTExpans& srt() const { return mSRT; }

    OpaKey::Data& opa() { return mOpa; }
    const OpaKey::Data& opa() const { return mOpa; }

    HSVKey::Data& hsv() { return mHSV; }
    const HSVKey::Data& hsv() const { return mHSV; }

    WaveKey::Data& wave() { return mWave; }
    const WaveKey::Data& wave() const { return mWave; }

    // The blended mask FIELD for this frame. An invalid field means "no mask",
    // which the renderer treats as fully visible - so a node without a mask pays
    // one null test and nothing else.
    MaskKey::Data& mask() { return mMask; }
    const MaskKey::Data& mask() const { return mMask; }

    void setBlurEllipse(float aBlurX, float aBlurY, float aAngleDeg) {
        mBlurX = aBlurX;
        mBlurY = aBlurY;
        mBlurAngle = aAngleDeg;
    }
    float blurX() const { return mBlurX; }
    float blurY() const { return mBlurY; }
    float angleDeg() const { return mBlurAngle; }
    float maxBlurRadius() const { return std::max(mBlurX, mBlurY); }

    void setWorldOpacity(float aValue) { mWorldOpacity = aValue; }
    float worldOpacity() const { return mWorldOpacity; }

    void setDepth(float aValue) { mDepth = aValue; }
    float depth() const { return mDepth; }
    void setWorldDepth(float aValue) { mWorldDepth = aValue; }
    float worldDepth() const { return mWorldDepth; }

    BoneExpans& bone() { return mBone; }
    const BoneExpans& bone() const { return mBone; }

    PoseKey::Data& pose() { return mPose; }
    const PoseKey::Data& pose() const { return mPose; }
    void setPoseParent(BoneKey* aKey) { mPoseParent = aKey; }
    BoneKey* poseParent() { return mPoseParent; }
    const BoneKey* poseParent() const { return mPoseParent; }
    PosePalette& posePalette() { return mPosePalette; }
    const PosePalette& posePalette() const { return mPosePalette; }

    void setAreaMeshKey(MeshKey* aKey) { mAreaMeshKey = aKey; }
    MeshKey* areaMeshKey() { return mAreaMeshKey; }
    const MeshKey* areaMeshKey() const { return mAreaMeshKey; }

    FFDKey::Data& ffd() { return mFFD; }
    const FFDKey::Data& ffd() const { return mFFD; }
    void setFFDMesh(LayerMesh* aMesh) { mFFDMesh = aMesh; }
    LayerMesh* ffdMesh() { return mFFDMesh; }
    void setFFDMeshParent(TimeKey* aKey) { mFFDMeshParent = aKey; }
    TimeKey* ffdMeshParent() { return mFFDMeshParent; }
    const TimeKey* ffdMeshParent() const { return mFFDMeshParent; }

    void setAreaImageKey(ImageKey* aKey) { mAreaImageKey = aKey; }
    ImageKey* areaImageKey() { return mAreaImageKey; }
    const ImageKey* areaImageKey() const { return mAreaImageKey; }
    /// The full-resolution texture, or nullptr when the key is paged (its sharp levels live
    /// in the pool and there is no single texture to name). Consumers that need the CONTENT
    /// SIZE, rather than a sampler, must use areaTextureSize().
    const gl::Texture* areaTexture() const;
    /// The paged resource for this key, or nullptr when it uses a plain texture.
    VirtualTexture* areaVirtualTexture() const;
    /// The content's pixel size, whichever sampling shape the key has. Empty when there is no
    /// image key or no image.
    QSize areaTextureSize() const;
    img::BlendMode blendMode() const;
    void setImageOffset(const QVector2D& aOffset) { mImageOffset = aOffset; }
    QVector2D imageOffset() const { return mImageOffset; }

private:
    // Frame the caches were blended for, plus explicit validity flags: the
    // frame value alone cannot mark validity because frame -1 is a legitimate
    // frame (negative project ranges) yet was the historical "no cache"
    // sentinel. Negative frames are cached like any other.
    Frame mMasterCache;
    bool mMasterCached;
    // see revision(); bumped only by TimeKeyBlender::updateCurrents
    quint64 mRevision;
    std::array<Frame, TimeKeyType_TERM> mKeyCaches;
    std::array<bool, TimeKeyType_TERM> mKeyCached;
    SRTExpans mSRT;
    OpaKey::Data mOpa;
    HSVKey::Data mHSV;
    WaveKey::Data mWave;
    MaskKey::Data mMask;
    float mBlurX;
    float mBlurY;
    float mBlurAngle;
    float mWorldOpacity;
    float mDepth;
    float mWorldDepth;
    BoneExpans mBone;
    PoseKey::Data mPose;
    BoneKey* mPoseParent;
    PosePalette mPosePalette;
    MeshKey* mAreaMeshKey;
    FFDKey::Data mFFD;
    LayerMesh* mFFDMesh;
    TimeKey* mFFDMeshParent;
    ImageKey* mAreaImageKey;
    QVector2D mImageOffset;
};

} // namespace core

#endif // CORE_TIMEKEYEXPANS_H
