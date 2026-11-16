#ifndef CORE_MASKPYRAMID_H
#define CORE_MASKPYRAMID_H

#include <QImage>
#include <QRect>
#include <QSize>
#include <vector>
#include "XC.h"
#include "gl/Texture.h"
#include "util/NonCopyable.h"

namespace core {

/// THE MASK'S LEVELS, ON THE CPU.
///
/// A paged mask uploads its coarse chain and its pages from here: the GPU holds only what is
/// resident, and the level data lives where the authoring field does. LEVEL 0 IS NOT STORED: it
/// is the field itself (the QImage the mask's data owns), exactly as core::ImagePyramid leaves
/// level 0 to the resource's own pixels - a copy of the largest level would be the pyramid's
/// single biggest allocation for nothing. The pyramid is CAPTURED from the mask's GPU chain at
/// build (the chain knows the resample rule, and a capture is exact - see MaskMipChain), and
/// after that a stroke's dirty box is rebuilt on the CPU: level 1 from the field, one box per
/// level above it, halved and rounded outwards. The rebuild uses the WHOLE
/// build's rule (each level from the one directly above), so a stroke's result is what a rebuild
/// would have produced up to the CPU's area arithmetic - measured at <=2 LSB against the GPU
/// chain (MX1); the chain's own box path uses the deepest exact level as its source instead,
/// which is cheaper but can differ from a rebuild at the high levels.
///
/// Why the CPU and not the GPU: the chain's levels ARE the full texture, so a GPU source defeats
/// paging; a transient chain needs the full allocation plus a boxed readback (GL 3.3 has none)
/// per stroke; and a full 4096^2 chain costs 96 ms on the GPU against 3.6 s on the CPU (Debug),
/// so the build is captured, not computed here. A stroke's box is thousands of texels.
class MaskPyramid: private util::NonCopyable {
public:
    MaskPyramid();

    /// Reads levels 1 and deeper of aChain (R8) into the CPU; level 0 is the field itself.
    /// False on an empty chain.
    bool capture(const gl::Texture& aChain);
    void clear();

    // true for a captured pyramid AND for a blend view (which stores sizes but no level bytes)
    bool isValid() const { return !mSizes.empty(); }
    QSize size() const { return mSizes.empty() ? QSize() : mSizes.front(); }
    int levelCount() const { return (int)mSizes.size(); }
    QSize levelSize(int aLevel) const;
    /// The stored bytes of one level; level 0 is EMPTY by design (pass the field instead).
    const std::vector<uint8>& levelBytes(int aLevel) const;
    /// The bytes the stored levels hold (level 0 excluded), for the residency accounting.
    qint64 bytes() const;

    /// The page fill: the page's content plus the 2-texel gutter, kPitch rows of R8. The gutter is
    /// the level's neighbouring texels; outside the LEVEL it is the nearest edge texel, not zero,
    /// because the mask's full texture is sampled GL_CLAMP_TO_EDGE (see MaskMipChain::create) -
    /// a zero gutter would darken the matte's outermost texels against the control's read.
    /// Level 0 is filled from `aField`; levels 1 and deeper from the captured bytes.
    void fillPage(int aLevel, int aPageX, int aPageY, const QImage& aField, uint8* aDst) const;

    /// One whole level, tightly packed, for a level >= 1. Used to build the always-resident
    /// coarse chain from the same bytes the pages read.
    void copyLevel(int aLevel, uint8* aDst) const;

    /// The box one level is rebuilt over for a level-0 damage box: the same integer rule
    /// updateBox applies, exposed so the pager can upload exactly the coarse levels a stroke
    /// touched. Rounded outwards and clamped into the level.
    QRect boxAtLevel(int aLevel, const QRect& aBaseBox) const;

    /// Rebuilds the stored levels over the damage a stroke reports: level 1's box reads the field,
    /// each level above it reads the one above, halved and rounded outwards. The field must be
    /// Grayscale8 and the same size the pyramid was captured at.
    void updateBox(const QImage& aField, const QRect& aTexelBox);

    // ---- THE BLEND VIEW ----------------------------------------------------------------
    // An ANIMATED matte has two keys and, in the interval between them, a field that is the
    // per-texel lerp of the two. Every level is a LINEAR filter of the field, so the blended
    // pyramid is the lerp of the two keys' pyramids at the same weight: this object then stores
    // no level bytes of its own and answers level 0 from the two fields and levels 1+ by lerping
    // the sources. It stays a MaskPyramid because that is exactly what the pager reads (size,
    // level count/size, page fill, coarse copy) - the alternative was a second source type at
    // every call site.
    //
    // The two fields must be one grid (the authoring path keeps it; TimeKeyBlender refuses any
    // other pair), and the SOURCES must outlive this view: they are the keys' own pyramids,
    // owned by their Data, while the view lives in the frame expansion's Data and is rebuilt
    // whenever the pair or a field changes. Level 0 is the lerp of the two fields with the same
    // integer weights the materializing blend uses, so a page filled from the view and a page
    // filled from the blended field agree byte for byte; levels 1+ differ from a chain built
    // from the blended field only by the half-float rounding of the two source chains.
    bool setBlend(
        const MaskPyramid* aA, const QImage& aFieldA, const MaskPyramid* aB, const QImage& aFieldB,
        int aWeight
    );
    void setBlendWeight(int aWeight);
    bool isBlend() const { return mBlendA != nullptr && mBlendB != nullptr; }
    /// The blend's level-0 field, MATERIALIZED at the current weight - the only form a caller
    /// outside the pager can read (a UI thumbnail). Null for a plain pyramid, whose level 0 is
    /// the caller's own field. Expensive by design (a whole-field lerp): callers cache it.
    QImage displayField() const;
    const MaskPyramid* blendA() const { return mBlendA; }
    const MaskPyramid* blendB() const { return mBlendB; }
    int blendWeight() const { return mBlendWeight; }

private:
    static QSize levelSizeOf(const QSize& aBase, int aLevel);
    static QRect coveredBox(const QRect& aSrcBox, const QSize& aSrcSize, const QSize& aDstSize);
    void resampleBox(
        const uint8* aSrc, int aSrcStride, const QSize& aSrcSize, std::vector<uint8>& aDst,
        const QSize& aDstSize, const QRect& aDestBox
    ) const;
    // aDst = (aDst * (256 - weight) + aOther * weight) >> 8, the materializing blend's rule
    void lerpInto(uint8* aDst, const uint8* aOther, size_t aCount) const;

    std::vector<QSize> mSizes;
    std::vector<std::vector<uint8>> mLevels;

    // the blend view (see setBlend): sources borrowed, fields shared, nothing materialized
    const MaskPyramid* mBlendA = nullptr;
    const MaskPyramid* mBlendB = nullptr;
    QImage mBlendFieldA;
    QImage mBlendFieldB;
    int mBlendWeight = 0;
    mutable std::vector<uint8> mBlendScratch;
};

} // namespace core

#endif // CORE_MASKPYRAMID_H
