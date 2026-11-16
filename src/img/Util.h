#ifndef IMG_UTIL_H
#define IMG_UTIL_H

#include <QSize>
#include <QRect>
#include <QImage>
#include <vector>
#include "XC.h"
#include "util/TextUtil.h"
#include "img/PSDFormat.h"
#include "img/ResourceNode.h"

namespace img {

class Util {
public:
    static bool isShiftJisCode(const img::PSDFormat& aFormat);

    class TextFilter {
    public:
        TextFilter(const img::PSDFormat& aFormat);
        QString get(const std::string& aText) const;

    private:
        util::TextUtil::ShiftJisDecoder mShiftJisDecoder;
        bool mIsShiftJis;
    };

    static void expandAlpha1Pixel(uint8* aImage, const QSize& aSize);

    /// Returns a copy of the image whose fully transparent texels carry the RGB of the nearest
    /// opaque neighbour, so a bilinear read across the silhouette mixes the layer's own colour
    /// instead of black. Alpha is untouched.
    ///
    /// The copy is the SAME SIZE as the input, and that is the point: the artwork keeps the texture's
    /// origin. A mip level is a box of 2^l base texels whose grid is anchored to multiples of 2^l IN
    /// THE TEXTURE, so the artwork's own 2^l blocks fall inside single level-l texels only while its
    /// origin is a multiple of 2^l - level 0 satisfies that for every l. Any fixed ring puts the
    /// artwork's origin at (margin, margin), so from level log2(margin)+1 upward every level-l texel
    /// is half artwork and half ring, and a minified pixel - whose footprint IS one level-l texel -
    /// is answered by a mix of two of them, i.e. a box twice as wide as the level it asked for.
    ///
    /// The 1-texel transparent ring this function used to add is therefore gone. It predates
    /// GL_CLAMP_TO_BORDER, which the layer texture is created with (ImageKey::resetTextureCache) and
    /// which gives the transparent neighbour a bilinear read needs at the artwork's edge - at every
    /// mip level, applied by the hardware. Its only remaining job was the RGB bleed above, and that
    /// needs no texels outside the image. Measured in the reference branch: the 1-texel ring cost
    /// 112 of 255 at 2x, a ring of 8 was exact only through level 3 and 111 of 255 off at 16x, and
    /// no ring at all is exact at every level (worst 1.3, mean 0.66 of 255).
    static XCMemBlock prepareForSampling(XCMemBlock& aGrabbedImage, const QSize& aSize);

    /// The bytes to hand to glTexImage2D for a layer: aData (aSize texels) ALPHA-PREMULTIPLIED, at
    /// the image's own size - no padding and no alignment unit.
    ///
    /// Premultiplied because the texture is mipmapped - see ImageKey::resetTextureCache, and
    /// PREMULTIPLIED_INPUT in the layer shader, which divides the coverage back out. Returned by
    /// value because the caller owns it: this buffer is as large as the texture.
    ///
    /// The size is the image's own because a padded texture would cost up to +98% of the layer's
    /// memory for nothing: aligning a 144-texel side rounds it up to 256, and padding exists only so
    /// that glGenerateMipmap can generate the first levels - which gl::Texture::create no longer
    /// needs, because it builds every level that does not halve exactly itself, on the GPU, with the
    /// area weights the grid implies (levels 1..3 from level 0 - exact for any size - and level 4
    /// and up from level - 3, see gl::Texture::kMipChainSpan).
    /// Uploading the content's own size is also what
    /// makes the two ends of the texture-coordinate path agree: the mesh's coordinates are content
    /// pixels (core::GridMesh), the layer's vertex shader divides them by uImageSize, and LayerNode
    /// sets uImageSize from areaTexture()->size(), so content texel (x, y) is sampled at texture
    /// texel (x, y) only while those two sizes are the same.
    static std::vector<uint8> makeTextureUpload(const uint8* aData, const QSize& aSize);

    static void
    copyImage(uint8* aDst, const QSize& aDstSize, const QPoint& aTopLeft, const uint8* aSrc, const QSize& aSrcSize);

    static std::pair<XCMemBlock, QRect>
    createTextureImage(const PSDFormat::Header& aHeader, const PSDFormat::Layer& aLayer);
    static std::pair<XCMemBlock, QRect> createTextureImage(const QImage& aImage);

    static ResourceNode* createResourceNodes(PSDFormat& aFormat, bool aLoadImage);
    static ResourceNode* createResourceNodes(bool merged, const std::string& aFilePath, bool aLoadImage);
    static ResourceNode* createResourceNode(const QImage& aImage, const QString& aName, bool aLoadImage);
};

} // namespace img

#endif // IMG_UTIL_H
