#ifndef CORE_IMAGEKEY_H
#define CORE_IMAGEKEY_H

#include <array>
#include <memory>
#include "XC.h"
#include "util/Easing.h"
#include "core/KeyEasing.h"
#include "gl/Texture.h"
#include "img/ResourceHandle.h"
#include "img/BlendMode.h"
#include "core/GridMesh.h"
#include "core/TimeKey.h"
#include "util/LifeLink.h"

namespace core {

class Project;
class VirtualTexture;

class ImageKey: public TimeKey {
public:
    // One easing per graph channel: 0 offset x, 1 offset y. The offset is
    // step-held, so playback does not consult either param; they are stored so
    // the graph's own channel model stays one type-wide rule.
    static constexpr int kComponents = easingComponentCount(TimeKeyType_Image);

    class Data {
        std::array<util::Easing::Param, kComponents> mEasings;
        img::ResourceHandle mResHandle;
        img::BlendMode mBlendMode;
        QVector2D mImageOffset;
        GridMesh mGridMesh;

    public:
        Data();
        Data(const Data& aRhs);
        Data& operator=(const Data& aRhs);
        util::Easing::Param& easing(int aComponent) {
            XC_ASSERT(0 <= aComponent && aComponent < kComponents);
            return mEasings[aComponent];
        }
        const util::Easing::Param& easing(int aComponent) const {
            XC_ASSERT(0 <= aComponent && aComponent < kComponents);
            return mEasings[aComponent];
        }
        img::ResourceHandle& resource() { return mResHandle; }
        const img::ResourceHandle& resource() const { return mResHandle; }
        void setBlendMode(img::BlendMode aMode) { mBlendMode = aMode; }
        img::BlendMode blendMode() const { return mBlendMode; }
        void setImageOffset(const QVector2D& aOffset);
        const QVector2D& imageOffset() const { return mImageOffset; }
        GridMesh& gridMesh() { return mGridMesh; }
        const GridMesh& gridMesh() const { return mGridMesh; }
    };

    /// THE LAYER'S TEXTURE. Two shapes, one accessor:
    ///   - a plain mipmapped texture, level 0 to 1x1 (the full-resolution path, and every
    ///     resource small enough that paging would only add machinery); or
    ///   - a PAGED resource (core::VirtualTexture): the sharp levels live in the pool and the
    ///     always-resident coarse chain is the fallback, so `texture()` has no id at all. Code
    ///     that only needs the CONTENT SIZE must ask sourceSize(), not texture().size().
    class Cache {
        gl::Texture mTexture;
        std::unique_ptr<VirtualTexture> mVirtualTexture;
        QSize mSourceSize;

    public:
        Cache();
        ~Cache();
        gl::Texture& texture() { return mTexture; }
        const gl::Texture& texture() const { return mTexture; }

        bool isPaged() const { return mVirtualTexture != nullptr; }
        VirtualTexture* virtualTexture() { return mVirtualTexture.get(); }
        const VirtualTexture* virtualTexture() const { return mVirtualTexture.get(); }

        /// The content's pixel size, whichever shape this cache has.
        QSize sourceSize() const { return mSourceSize; }

        /// Paged-or-not is a function of the image and the switch; the texture must already
        /// have been created with the production chain (VirtualTexture captures its levels).
        bool buildVirtualTexture(const img::ResourceHandle& aResource, const QSize& aSize);
        void resetVirtualTexture();
        void setSourceSize(const QSize& aSize) { mSourceSize = aSize; }
    };

    enum { kDefaultMeshCellSize = 16 };

    ImageKey();

    Data& data() { return mData; }
    const Data& data() const { return mData; }

    Cache& cache() { return mCache; }
    const Cache& cache() const { return mCache; }

    void setImage(const img::ResourceHandle& aResource, img::BlendMode aMode);
    void setImage(const img::ResourceHandle& aResource);

    /// Forces this key onto the plain full-resolution texture. It is the A/B control the page
    /// gates compare against (and the escape hatch for a resource the paged path cannot serve);
    /// a caller can hold it for the duration of a test.
    void setForceFullTexture(bool aForce);
    bool forceFullTexture() const { return mForceFullTexture; }
    void setImageOffset(const QVector2D& aOffset);
    void setImageOffsetByCenter();
    bool hasImage() const { return mData.resource() && mData.resource()->hasImage(); }
    void resetGridMesh(int CellSize = kDefaultMeshCellSize);
    QJsonObject serializeToJson() const;
    bool deserializeFromJson(QJsonObject json, util::LifeLink::Pointee<Project> project);
    virtual TimeKeyType type() const { return TimeKeyType_Image; }
    virtual bool canHoldChild() const { return true; }
    virtual TimeKey* createClone();
    virtual bool serialize(Serializer& aOut) const;
    virtual bool deserialize(Deserializer& aIn);

    virtual void sleep();
    virtual void awake();

    /// The LOD bias the LAYER SHADER applies, in levels (see LayerDrawingFrag.glsl's sampleLayer and
    /// LayerNode's uniform block). -1.35 = -0.5 for the bilinear read's second moment (derivation on
    /// gl::Texture::setLodBias) plus -0.85 measured on the reconstructing read: at the fit zoom that
    /// gives rendered sharpness 20.145 against a 4x supersampled reference's 20.132, error 0.84 and
    /// the error's frame-to-frame variation 1.27.
    ///
    /// It is the SUM because Mesa's textureLod INHERITS GL_TEXTURE_LOD_BIAS: the candidate numbers
    /// (-0.85 in the shader with the texture's bias still -0.5) reproduced exactly when the texture's
    /// bias was put back, and the committed build without it was 7 % softer. The texture's own bias is
    /// therefore 0 and this value lives here alone, where no driver can apply it twice.
    ///
    /// The texture's own GL_TEXTURE_LOD_BIAS is therefore 0: the shader computes the level itself.
    static float layerLodBias() { return -1.35f; }

    /// The anisotropy every layer texture asks the sampler for, clamped to what the device reports
    /// (gl::Texture::setMaxAnisotropy, which is also a no-op where the extension is absent).
    ///
    /// Trilinear minification picks ONE level, and picks it from the LARGER of the two screen-space
    /// derivatives, so a layer squashed on one axis is filtered at the level its long axis needs and
    /// is over-blurred along the short one by the ratio. Anisotropic filtering keeps the level and
    /// takes several samples along the long axis instead. Measured in the reference branch's M19 on a
    /// synthetic broadband image against the exact area integral of the footprint (error in 255ths,
    /// 64x16 / 128x16 / 256x16 texel footprints):
    ///
    ///     anisotropy         1      2      4      8     16
    ///     4:1 footprint   4.443  2.244  1.117  1.117  1.117
    ///     8:1 footprint   6.971  4.012  1.838  0.741  0.741
    ///     16:1 footprint 12.683  6.846  3.803  1.736  0.539
    ///
    /// So the gain saturates at the FOOTPRINT'S OWN RATIO - the driver takes ceil(ratio) samples,
    /// capped by this value - and on a square footprint it is a no-op (0.4817 at every value above).
    /// 16 is therefore an upper bound, not a cost: a 4:1 footprint at anisotropy 16 renders in the
    /// same 4.1 ms as at 1 (M19, 1024x1024 canvas), and only a 16:1 squash ever asks for the samples.
    /// On real layers the error against the supersampled truth at a 4:1 squash fell from 1.239 to
    /// 0.191 (qitqmd.anie), and the LOD bias scan was bit-identical with the anisotropy at 1 or 16,
    /// so -0.5 stays.
    static float layerMaxAnisotropy() { return 16.0f; }

private:
    void resetTextureCache();

    Data mData;
    Cache mCache;
    int mSleepCount;
    bool mForceFullTexture;
};

} // namespace core

#endif // CORE_IMAGEKEY_H
