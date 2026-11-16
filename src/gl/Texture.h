#ifndef GL_TEXTURE_H
#define GL_TEXTURE_H

#include <QtOpenGL/QtOpenGL>
#include "XC.h"
#include "util/NonCopyable.h"

namespace gl {

class Texture: private util::NonCopyable {
public:
    // Whether create() also gives the texture a mipmap chain, and who fills it.
    //
    // NoMipmap: level 0 only, GL_TEXTURE_MAX_LEVEL pinned to 0, filters and wrap untouched. The
    // default, and what render targets, glyph caches and straight (non-premultiplied) data use.
    //
    // GenerateMipmap: mipmaps are only correct for data that is ALPHA-PREMULTIPLIED, because
    // glGenerateMipmap box-filters each channel independently and averaging STRAIGHT colour blends
    // the RGB of transparent texels into the edge texels of every level (the dark/light fringe once
    // the texture is minified). create() generates the levels that halve exactly and BUILDS every
    // level that does not, on the GPU, with the area weights the level grid implies (see
    // Texture.cpp) - so it needs no readback, but it does build the whole chain, with a reach-back
    // span that bounds the cost of a very large base.
    //
    // Manual: the whole chain is allocated and booked (levels 1..n-1 exist at their GL-mandated
    // sizes, GL_TEXTURE_MAX_LEVEL is n-1, mipmapLevels() and the residency counter report every
    // level), but the levels above 0 hold nothing until the caller fills them. create() builds and
    // draws nothing, and does not touch the filters or the wrap mode - the caller owns the sampling
    // policy and the per-level uploads (a mask patched a box at a time, for which a whole-chain
    // build would be the wrong unit of work).
    enum MipmapPolicy {
        NoMipmap,
        GenerateMipmap,
        Manual,
    };

    /// THE RULE, in one place, for every manual chain in the tree (this class's automatic tail and the
    /// mask chain's whole-field build). Two constants, both measured, both about the RENDER rather than
    /// about any level compared against an ideal:
    ///
    ///   kMipChainSpan = 1   every level is built from the level directly above.
    ///   kMipStepShrink = 0.8  the window of a step BELOW level 1 is narrowed 20 % around its own
    ///                       centre before the area weights are taken. Level 1 keeps the plain area
    ///                       average: its source is the original, so there is nothing to compensate.
    ///
    /// WHY SPAN 1. A chained level is not the exact area average of the base, and it cannot be: the
    /// level above has already averaged away what the exact level would need. The question is whether
    /// anyone can see it. Measured through the actual sampling model (trilinear, LOD 1..4, sharpness =
    /// mean gradient of the rendered layer, 000022): exact / span 1 / span 3 render at 11.25 / 11.25 /
    /// 11.25 at LOD 1, 14.26 / 14.25 / 14.26 at LOD 2, 15.00 / 14.58 / 15.00 at LOD 3 and 17.81 / 17.19
    /// / 17.77 at LOD 4 - i.e. the cheapest chain is within 3.4 % of the anchored one at the deepest
    /// scale, while the sampling model itself swings sharpness 30-40 % between LOD integers in every
    /// policy. Sub-pixel phase sensitivity is identical to three decimals.
    ///
    /// COST, worked out as taps per output texel times output texels rather than as a geometric series.
    /// The taps are the shader's own `ceil(src/dst)` per axis: 2x2 = 4 per output when the sizes divide
    /// exactly, 3x3 = 9 when the ratio is just above 2 (a 2.002 footprint always touches three texels -
    /// a partial, a full, a partial), and R^2 for an anchored level. Span 1 against span 3, in base
    /// reads: 1.33 / 3.33 for a power-of-two base, 1.37 / 3.49 for 1500x1440, 2.06 / 4.74 for
    /// 2894x4093 and 2.61 / 5.50 for 15001x15001 - odd bases pay more in both, because most of their
    /// steps then cut and cost 9 taps instead of 4. The saving is 2 to 3 base reads, always the same
    /// direction.
    ///
    /// THE SHRINK IS FREE: it narrows the window the weights are taken over, it does not add taps. The
    /// shader still loops ceil(src/dst) per axis; a narrowed window only ever gives a boundary tap less
    /// (or zero) overlap, and a zero-overlap tap is skipped.
    ///
    /// WHY THE SHRINK, and why it is exactly what that chain needs. The overlap-weighted average of
    /// whole source texels is WIDER than the footprint it stands for, and a chain applies it twice: that
    /// extra low-pass is the whole difference between a chained level and the exact one. Narrowing the
    /// window by 20 % compensates it: rendered sharpness goes from -13.33 % / -15.15 % against the ideal
    /// (LOD 3 / 4) to -11.00 % / -12.43 %, i.e. to within 0.1-0.3 points of the anchored chain, for the
    /// same 3x3 taps. Level 1 is exempt and must stay so: with a shrinking window it overshoots what
    /// the original contains (000026 +2.65 % -> +4.21 % against the ideal at LOD 1, 000169 -0.09 % ->
    /// +1.90 %), because a source that has not itself been averaged has no lost width to restore. The
    /// factor is not fragile (0.75 / 0.80 / 0.85 / 0.90 give -10.46 / -11.00 /
    /// -11.56 / -12.11 % at LOD 3) and it needs no ratio formula: on a step whose sizes divide exactly
    /// the narrowing is an EXACT no-op - the centred 1.6-texel window covers the same two texels with
    /// equal weights as the 2.0 one (measured identical on 750x720 -> 375x360 for every factor from
    /// 1.00 to 0.70). So one constant is right for every step of a span-1 chain, whose ratios are 2,
    /// 2.0054, 2.0108, 2.0217, 2.0909, 2.2 as the sides shrink - all of them "just above 2".
    ///
    /// WHAT THIS DOES NOT CLAIM. The levels are not the area average of the base, and a level compared
    /// against that average shows it (up to ~20 of 255 on a contour, -0.5 to -1.6 % high-frequency
    /// energy). That comparison measures a reference nobody sees; the numbers above are the ones that
    /// survive the path to the screen.
    static const int kMipChainSpan = 1;
    static const float kMipStepShrink;

    /// The level to build level aLevel from: the level directly above (see kMipChainSpan).
    static int mipSourceLevel(int aLevel);

    Texture();
    ~Texture();

    void create(
        const QSize& aSize,
        const uint8* aData = nullptr,
        GLenum aFormat = GL_RGBA,
        GLint aInternalFormat = GL_RGBA8,
        GLenum aChannelType = GL_UNSIGNED_BYTE,
        MipmapPolicy aMipmap = NoMipmap
    );

    // Minification and magnification are set separately and deliberately: a mipmapped
    // minification filter must be paired with a NON-mipmap magnification filter (GL
    // rejects GL_*_MIPMAP_* for GL_TEXTURE_MAG_FILTER with GL_INVALID_ENUM), so one value
    // written to both cannot express a mipmapped texture at all.
    void setMinFilter(GLint aParam);
    void setMagFilter(GLint aParam);
    void setFilter(GLint aMinParam, GLint aMagParam);

    // Writes one value to both filters. Only for textures with no mip chain; a mipmapped
    // texture must use the two-argument form, because a mipmap enum is invalid for
    // GL_TEXTURE_MAG_FILTER.
    void setFilter(GLint aParam);

    void setWrap(GLint aParam, QColor aBorderColor = QColor(0, 0, 0, 0));

    /// Asks the sampler to filter anisotropically: up to aValue samples along the footprint's major
    /// axis, each at the level the MINOR axis asks for.
    ///
    /// Trilinear minification has to pick ONE level, and it picks it from the larger of the two
    /// screen-space derivatives, so an elongated footprint - a non-uniformly scaled layer, which this
    /// app makes in one keyframe - is filtered at the level the long axis needs and is therefore
    /// over-blurred along the short one by the ratio. Anisotropic filtering resolves that by taking
    /// several samples along the long axis, each at the level the short one asks for.
    ///
    /// Measured in the reference branch (M19 of tools/verify_mipmap), against the exact area integral
    /// of the footprint, in 255ths: a 4:1 footprint goes 4.443 -> 1.117, an 8:1 6.971 -> 0.741, a
    /// 16:1 12.683 -> 0.539, and a square one is 0.4817 at every value - the control that says this
    /// cannot make the ordinary case worse. The gain saturates at the footprint's own ratio because
    /// that is how many samples the driver takes; a large request is an upper bound, not a cost (a
    /// 4:1 footprint renders in the same 4.1 ms at 16 as at 1). Where the footprint is isotropic the
    /// anisotropic path still reads a marginally narrower kernel at FRACTIONAL levels - up to 0.155
    /// of a level, nothing at an integer one.
    ///
    /// The value is clamped to the device's limit (gl::DeviceInfo::maxAnisotropy) and is a no-op where
    /// the device reports 1, i.e. where EXT_texture_filter_anisotropic - or the ARB promotion of it -
    /// is absent. GL would clamp the value itself, but the app treats a GL error as a hard failure and
    /// there is no reason to send a value the device cannot honour.
    void setMaxAnisotropy(GLfloat aValue);

    /// Offsets the level the minification filter picks, in log2 units. Only the minimum filter uses
    /// it; magnification ignores it.
    ///
    /// A layer texture needs a negative one because of how a mip chain is READ, not because of how it
    /// is built - and the correct value follows from matching the read's kernel to the level the level
    /// system MEANT to produce.
    ///
    /// A bilinear read of level m is a TENT: bilinear interpolation between two samples d = 2^m base
    /// texels apart, each already the box average of its own d base texels, i.e. the convolution of two
    /// boxes of width d. A tent is the convolution of two boxes, so the READ has
    ///     variance = 2 * (d^2 / 12) = 2^(2m) / 6,
    /// while the ideal mip level for a footprint F = 2^lambda is a BOX of width F with variance
    /// F^2 / 12. Setting them equal, 2^(2m) / 6 = F^2 / 12, gives 2^m = F / sqrt(2), i.e.
    ///     m = lambda - 0.5
    /// for EVERY footprint: -0.5 is the second-moment match between a bilinear tent and the box the
    /// level stands for, not a taste. A NEGATIVE bias is the sharper direction - it picks a finer level
    /// - and the three candidate values order that way: -1 (match the tent's SUPPORT to the footprint,
    /// 2^(m+1) = F) is sharper than the ideal; -0.5 is the match; and 0 (the derivatives' own rule,
    /// m = lambda) is SOFTER than the ideal by exactly the tent's extra width, which is the reading a
    /// driver gives when nobody corrects it.
    ///
    /// Measured against supersampled ground truth over the reference corpus, -0.5 lowers the mean error
    /// from 1.631 to 0.968 of 255 at 4x and from 4.198 to 2.533 at 16x, per-file optimum between -0.5
    /// and -1.0, -1.5 past it. The gain is in the fractional-phase cases; a footprint that lands on a
    /// level-texel centre is exact at 0 and made marginally softer by this.
    ///
    /// THE OTHER SIDE OF IT: keeping more energy near the sampling Nyquist also means more sub-pixel
    /// shimmer (a 1-texel pair at offset d modulates as |1 - 2d|). Measured on three samples, 0 against
    /// -0.5 buys 4-9 % less shimmer for 1-14 % less sharpness, the split depending on content. The
    /// layer value is a choice between those, not a bug in either.
    void setLodBias(GLfloat aBias);

    void destroy();

    GLuint id() const { return mId; }
    QSize size() const { return mSize; }

    // 1 when no chain was built, otherwise the number of levels (including level 0).
    int mipmapLevels() const { return mMipmapLevels; }

private:
    GLuint mId;
    QSize mSize;
    int mMipmapLevels;
    qint64 mBytes;
};

} // namespace gl

#endif // GL_TEXTURE_H
