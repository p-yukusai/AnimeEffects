#ifndef CORE_FILTERMASK_H
#define CORE_FILTERMASK_H

#include <QImage>
#include <QRect>
#include <QRectF>
#include <QSize>
#include "gl/Texture.h"
#include "util/NonCopyable.h"

namespace core {

// What a painted field is FOR. A filter may consume two: an AMOUNT mask scales its
// effect per pixel, a TIME OFFSET mask delays it per pixel. (Wallpaper Engine's
// opacity mask and time offset mask respectively.)
enum FilterMaskChannel {
    FilterMaskChannel_Amount,
    FilterMaskChannel_TimeOffset,
    FilterMaskChannel_TERM,
};

// The value a channel means "the filter is unaffected" at, i.e. what a fresh field
// (and a cleared one) holds:
//   * AMOUNT: white, so allocating a mask changes nothing and the brush paints the
//     areas where the effect is REDUCED (Photoshop's layer mask, Wallpaper Engine's
//     opacity mask - both start at full);
//   * TIME OFFSET: black, because "no delay" is zero delay.
// A mask therefore never changes the picture until it is painted.
inline int filterMaskNeutral(FilterMaskChannel aChannel) {
    return aChannel == FilterMaskChannel_Amount ? 255 : 0;
}

class Serializer;
class Deserializer;

// THE MIP CHAIN OF A MASK FIELD, shared by the two field types (MaskKey::Data and
// FilterMask below) - they differ in who owns the level-0 pixels, not in how the chain
// above them is kept.
//
// WHY A MASK HAS A CHAIN AT ALL. A mask is sampled minified exactly like a layer's
// image - every camera scale below 1 and the fit zoom read it through the minification
// filter - so without a chain the painted edge aliases at those zooms. A layer's chain
// is built once at import; a mask's changes with every brush stamp, so this keeps ONE
// BOX OF DAMAGE PER LEVEL and rebuilds only that box.
//
// THE GRID HAS NO PADDING, like every other sampled texture now: the field is the content's
// own resolution, one texel per content pixel, and its texture is exactly that size - content
// texel (x, y) is texture texel (x, y). The alignment padding an upload used to carry (so that
// the driver's own levels divide) must NOT be used here: every mask shader maps content pixels
// onto the texture with `uv = (q - rect.xy) / rect.zw` (LayerDrawingFrag's LAYER_MASK,
// FilterFrame's mask passes), i.e. normalized coordinates over the rect's size - a texture
// larger than the rect would stretch the field across the padding. The chain needs no padding
// because the levels are computed from the data instead of asked of the driver.
//
// WHERE A LEVEL COMES FROM
//   * level l has max(1, side >> l) texels (gl/Texture.cpp's rule), so level l is an
//     EXACT halving of level l-1 while 2^l divides both sides, i.e. while
//     l <= exactLevels(). Such a level is the aligned 2x2 average of the level above, i.e. the
//     2^l x 2^l box of level 0 - the same value either way - and the box has the same shape as the
//     one above it. (The image path lets the driver produce those levels; this chain builds every
//     level itself, so a whole-field build still reads level 0 for them.)
//   * EVERY level is an AREA-WEIGHTED reduction of a finer level, with the weights
//     gl/Texture.cpp's GPU tail applies: destination texel j covers
//     [j * srcSide / dstSide, (j + 1) * srcSide / dstSide) of the source, every source texel
//     contributes the length of its overlap, and the window is NARROWED by kMipStepShrink from level
//     2 up (see gl/Texture.h - it compensates the extra width of an overlap-weighted average, and it
//     is an exact no-op on a step whose sizes divide). Where the step halves, those overlaps are whole
//     texels; where it does not they are fractional. MEASURED over a 45-sample corpus
//     (tools/mip_compare): a step-by-step chain deviates from the exact box by up to 65 of 255, a
//     tightened one by a few units, and neither difference survives the render (LOD 1..4 within
//     3.4 % of the anchored chain's sharpness, 0.1-0.3 points with the shrink).
//   * WHICH FINER LEVEL, chosen by how much is being rebuilt:
//       - a WHOLE-FIELD build (create, markDirtyAll: creation, clear, undo, deserialize) asks
//         gl::Texture::mipSourceLevel, the ONE rule the image path also uses: THE LEVEL DIRECTLY ABOVE
//         for every level (span 1). Cost in taps per output times output texels - 4 taps per output
//         where a step divides exactly, 9 where it cuts - 1.33 base reads for a power-of-two field,
//         2.61 for a 15001-texel one whose sides never halve: the cheapest chain there is. Exactness is not lost where it is visible: level 1 is the plain area
//         average of the base, and the tighten below keeps the render at the anchored chain's
//         sharpness. The rule lives in ONE place, so the mask and the image path cannot drift.
//       - a STROKE'S BOX takes the deepest exact level at any depth - UNCHANGED, and the reason a
//         box is affordable: a box rebuild costs about the box per level, so exactness at every
//         level is nearly free there (see COST for the depth at which it stops being free).
//
// HOW MANY TAPS THE TAIL NEEDS. One per source texel the destination texel's span
// overlaps, so ceil(srcSide / dstSide) + 1 per axis - NOT the 4 taps per output a
// chained reduction would cost. The first non-halving level spans between 2 and 3 source
// texels, so it needs 3 taps per axis; every octave deeper doubles the span, and the
// deepest level of a grid where nothing halves spans the whole side. Two taps per axis
// cannot express three weights: for a span of exactly 3 the box is 1/3 on each of three
// texels, and a two-tap scheme carries at most two of them - a third of the value, 85 of
// 255, dropped. With ceil taps nothing is dropped, and the extra (+1) tap is slack: the
// shader skips a tap whose overlap is zero and divides by the SUM of the weights it took,
// so a span that ends on a texel boundary cannot under-normalize.
//
// HOW THE BOXES PROPAGATE. The damage is a level-0 box (a stroke's stamp). Level l's box
// is the box of the level above it halved and rounded OUTWARDS (core::coveredBox in
// FilterMask.cpp), then clamped into the level. Outward rounding is not slack: a
// destination texel's footprint can cross the box edge, and such a texel must be
// recomputed because part of what it averages changed. Rounding out can never under-cover,
// so composing the halvings is a superset of the level's true damage; a texel rebuilt
// without needing to be comes out with the value it already had.
//
// WHY THE BUILD RUNS ON THE GPU. The same arithmetic could run on the CPU (the level-0
// pixels are in RAM), and it would be exact - but a paint happens dozens of times a second
// in the UI thread, and a CPU pass would cost a box per level per stamp plus an upload per
// level. Here each level's box is one small draw: the level (or the exact level) is sampled
// with texelFetch on the GPU, nothing is read back, and the UI thread only issues the draws.
// It is SEPARABLE (x into a scratch, then y from it), so a fragment costs span taps, not span
// squared - a 15001-wide deepest level would otherwise be 225 M fetches in ONE fragment - and
// the intermediate is at most (destination width x source height).
//
// COST, stated where it is largest.
//   * A WHOLE-FIELD build follows the shared span-1 rule: a 15001-texel side where nothing halves
//     costs 2.61 base reads - ~590 M fetches on a 225 M-texel base, three taps per axis per level
//     where the ratios do not divide - against the span-3 rule it replaced (5.50, ~1.2 G) and
//     anchoring every level (14 base passes, ~3 G). The tightened window costs nothing: same taps.
//   * A STROKE'S BOX costs about the box per level (thousands of fetches, sub-millisecond) until
//     the span outgrows the box. At extreme depth on a side where nothing halves the box is one or
//     two texels and the exact anchor's span is the whole side, so that level alone costs
//     ~span^2 fetches: ~0.5 M for a 1000-px side's deepest level (the whole tail a few times that,
//     still sub-millisecond), ~225 M for a 15001-px one. That is the price of exactness on a box, and
//     it is paid only on stroke boxes, where exactness is what makes a stroke's own level consistent
//     with the whole-field ones.
//
// WHAT gl::Texture IS TOLD. create() is called with MipmapPolicy::Manual: the class allocates and
// books the WHOLE chain - level 0 from the field's pixels, levels 1..n-1 at their GL-mandated
// sizes, MAX_LEVEL = n-1 - so its mipmapLevels() and its residency counter describe every level,
// and it builds and draws nothing. Under that policy the filters and the wrap are the caller's
// too, and are set here: trilinear minification, GL_LINEAR magnification, GL_CLAMP_TO_EDGE. What
// the class does not own is the part that matters for a mask: the CONTENTS above level 0, which
// are this chain's, written box by box.
class MaskMipChain: private util::NonCopyable {
public:
    MaskMipChain();

    // Has a grid been established (i.e. has create() built a chain)?
    bool isValid() const { return !mBaseSize.isEmpty(); }
    QSize baseSize() const { return mBaseSize; }

    // The damage, in LEVEL-0 texels. A stroke reports the box it painted; `all` is a
    // whole-field change (a restored image, a clear, a deserialization). They stay two
    // calls because they are two costs: a box rebuilds a box per level, `all` rebuilds
    // every level.
    void markDirty(const QRect& aTexels);
    void markDirtyAll();
    bool isDirty() const { return mDirtyAll || !mDirtyBox.isNull(); }

    // Creates aTexture at aLevelZero's size (level 0 from its pixels, every level above
    // it allocated) and fills the whole chain. aLevelZero must be Grayscale8.
    void create(gl::Texture& aTexture, const QImage& aLevelZero);

    // Brings an existing chain in step with the accumulated damage: uploads level 0's
    // box from aLevelZero and rebuilds the boxes above it. Requires a current GL context
    // and a chain created by create() for aLevelZero's size.
    void update(gl::Texture& aTexture, const QImage& aLevelZero);

private:
    // Fills aBaseBox's footprint at every level above 0 (see the class note for the rule and how
    // the source level is chosen). aWholeField selects the shared reach-back rule -
    // gl::Texture::mipSourceLevel - which is what a create/markDirtyAll rebuild uses; a stroke's
    // box keeps the deepest exact level.
    void rebuild(gl::Texture& aTexture, const QRect& aBaseBox, bool aWholeField);

    QSize mBaseSize;
    QRect mDirtyBox;
    bool mDirtyAll;
};

// A PAINTED FILTER MASK: one 8-bit field in the node's CONTENT space that a
// filter takes as an input. Wallpaper Engine calls these paintable effect
// textures; the concept is the same - the artist paints where an effect applies
// (an "amount" mask) or where it is delayed (a "time offset" mask) instead of
// masking with the layer's own alpha.
//
// CONTENT SPACE, not screen space: the rect the field covers is expressed in the
// same pixels the filter's pass already maps with (a layer's image pixels, a
// folder's canvas-shaped local box), so the mask travels with the artwork.
//
// ONE FIELD PER FILTER LANE, owned by the TimeLine (`TimeLine::ensureFilterMask`),
// NOT by the stack entry: the stack is value-replayed by its own commands, and a
// mask is megabytes of pixels - it must never travel through them. Removing the
// filter drops the field with it.
//
// RESOLUTION is the content's OWN: one 8-bit texel per content pixel. The 1024-texel cap this
// class used to carry was an unforced optimisation - it made a 15000 px canvas a 15 px grid - and
// nothing shipped with masks, so it is gone rather than versioned. A field now costs a quarter of
// the layer's RGBA image, the same rule the artwork follows.
//
// The field's TEXTURE carries a mip chain (see MaskMipChain above) because the filter passes
// sample it minified at any camera scale below 1; the chain is kept incrementally from the boxes
// markDirty reports, so a stroke uploads the texels it touched at every level and nothing else.
// There is no padding, so the channel's neutral is the value every level of a fresh field holds -
// filterMaskNeutral per channel - and a fresh mask still costs nothing and changes nothing.
class FilterMask: private util::NonCopyable {
public:
    // A SANITY BOUND for a corrupt file, not a design limit (see the class note).
    static constexpr int kMaxStoredTexels = 32768;

    FilterMask();

    // Allocates the field for a content rectangle at the channel's NEUTRAL value
    // (see filterMaskNeutral), so a fresh mask renders exactly like no mask at all.
    void reset(const QRectF& aContentRect, int aNeutralValue);
    // returns the field to its neutral value
    void clear();

    bool isEmpty() const { return mImage.isNull(); }
    const QRectF& contentRect() const { return mContentRect; }
    QSize texelSize() const { return mImage.size(); }

    const QImage& image() const { return mImage; }
    // painting writes here; the caller says which texels it touched
    QImage& image() { return mImage; }
    void markDirty(const QRect& aTexels);
    // bumps the revision without touching the pixels (a whole-field replacement)
    void markDirtyAll();

    // the field's CPU bytes, for the frame line (a filter mask is never paged, so this is its
    // whole footprint: its GPU chain is a texture and is counted by gl::FrameStats's texture
    // accounting instead)
    qint64 ramBytes() const { return mImage.isNull() ? 0 : (qint64)mImage.sizeInBytes(); }

    quint64 revision() const { return mRevision; }
    // the value "the filter is unaffected" at (see filterMaskNeutral)
    int neutral() const { return mNeutral; }

    // The GL texture the filter pass samples, created lazily and updated by the
    // dirty box (a stroke uploads the box it touched at every level, not the whole
    // field). The caller must have a current GL context - the renderer does.
    GLuint texture();
    // the revision the texture was last uploaded at (the gates read it)
    quint64 uploadedRevision() const { return mUploadedRevision; }

    // rect + texel size + a PNG blob (deflate is a good fit for a painted field)
    bool serialize(Serializer& aOut) const;
    bool deserialize(Deserializer& aIn);

private:
    QRectF mContentRect;
    QImage mImage; // Format_Grayscale8
    int mNeutral;  // the value clear() restores (filterMaskNeutral)
    gl::Texture mTexture;
    MaskMipChain mMip;
    quint64 mRevision;
    quint64 mUploadedRevision;
    bool mCreated;
};

} // namespace core

#endif // CORE_FILTERMASK_H
