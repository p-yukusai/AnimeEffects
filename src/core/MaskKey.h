#ifndef CORE_MASKKEY_H
#define CORE_MASKKEY_H

#include <array>
#include <memory>
#include <QImage>
#include <QRect>
#include <QRectF>
#include <QSize>
#include "util/Easing.h"
#include "core/KeyEasing.h"
#include "core/TimeKey.h"
#include "core/FilterMask.h"   // core::MaskMipChain, shared with the filter-lane masks
#include "gl/Texture.h"

namespace core {

class MaskPyramid;
class VirtualTexture;

// A MASK is an alpha field in the node's CONTENT space: the matte that says how
// much of the node is visible. 0 is fully hidden, 255 fully visible, and the
// renderer multiplies it into the node's present alpha AFTER its filters, so a
// blurred layer cannot bleed past its own matte.
//
// It is a TimeKey rather than a tree node or a filter-stack entry, and that is
// the whole design: its LANE, its keys, their easing, the proportional-key
// machinery, the key menu, the marquee and the undo model are the ones every
// other type already has, and animation comes from keying mask SHAPES and
// blending the fields between them - the way FFD animates its vertices.
//
// STRUCTURAL, like FFD and Pose: ONE easing shapes the whole key (address 0) and
// the type publishes no graph channel, because a field has no scalar to chart
// (core::easingComponentCount(_Mask) is 0, ::easingStorageCount is 1).
class MaskKey: public TimeKey {
public:
    static constexpr int kComponents = easingStorageCount(TimeKeyType_Mask);

    // A MASK IS STORED AT THE CONTENT'S OWN RESOLUTION: one 8-bit texel per content pixel, which
    // is a QUARTER of the layer's RGBA image - the same rule the artwork itself follows, and the
    // only one that keeps a painted edge sharp at 100% zoom. The old 1024-texel cap was an
    // unforced optimisation (it turned a 15000 px canvas into a 15 px grid) that nothing shipped
    // with, so it is gone. This constant is a SANITY BOUND for a corrupt file, not a design limit.
    static constexpr int kMaxStoredTexels = 32768;

    // THE FIELD IS NOT PADDED, and the texture is exactly the field's size: one content texel is
    // one texture texel, the same rule the layer path now follows. Padding a texture up to an
    // alignment so the DRIVER's levels divide buys nothing here, because the mask's levels are
    // computed from the data (see MaskMipChain) - and it must not be used: every mask shader maps
    // content pixels with `uv = (q - contentRect().topLeft()) / contentRect().size()`, so a texture
    // larger than the rect would stretch the field across the padding instead of placing one texel
    // per pixel.
    //
    // THE TEXTURE CARRIES A MIP CHAIN because a matte is sampled minified at every camera scale
    // below 1 (the fit zoom included) exactly like the layer's own image, and without a chain the
    // painted edge aliases there. It is kept INCREMENTALLY: the brush reports the box it painted
    // (markDirty) and each level rebuilds that box, halved and rounded outwards from the level
    // above, on the GPU - never the whole field. With no padding there is nothing but the field to
    // fill, so the neutral rule covers the whole chain: every level of a fresh (or cleared) mask is
    // kNeutral, and adding a mask still costs nothing on screen.

    // The value a fresh mask starts at: REVEAL ALL. Unlike a filter mask, whose
    // neutral is per input, a layer mask's neutral is "nothing hidden", so
    // adding one is visually a no-op until it is painted.
    static constexpr uchar kNeutral = 255;

    class Data {
        // The GL side of the field, kept beside the image. It is SHARED through the copy (a copy
        // of a QImage shares its pixels, so it shares the texture that holds them) and shed by
        // every whole-field mutator - a blended field is a new image and must never upload over
        // the key's own texture.
        //
        // `key` is the identity the sharing is checked against: the cacheKey of the image the
        // chain was built from. Painting patches the chain IN PLACE (the box the brush reports),
        // which puts the holder one stamp AHEAD of every other handle still pointing at the
        // pre-paint pixels, so markDirty moves the key forward with the patch: a copy taken
        // before the stamp (the undo state) then fails its own key check and rebuilds, which is
        // exactly what restoring a whole field must do.
        //
        // THE PAGED FORM lives in the same holder, because it describes the same two things the
        // full chain does - the field and its levels. `pyramid` is the CPU authority a stroke
        // patches; `paged` is the R8 pool resource built from it. A holder is in ONE of
        // the modes at a time while it is used: textureId() builds the full chain and leaves a
        // paged holder alone (both can be current, and markDirty then patches both); the paged
        // accessor captures the chain's levels once and drops it. Nothing may assume the full
        // texture exists: `built()` is the test, and it covers both.
        //
        // THE THIRD MODE: `pyramid` may be a BLEND VIEW (MaskPyramid::isBlend) over two keys'
        // pyramids. That is the frame expansion of an ANIMATED matte - no field of its own, one
        // pool resource kept across frames, and its levels read through the two keys. A view
        // holder is never shared with a key (assign's builtForField excludes it) and reports
        // itself valid without an image.
        struct TextureHolder {
            gl::Texture texture;
            MaskMipChain mip;
            std::unique_ptr<MaskPyramid> pyramid;
            std::unique_ptr<VirtualTexture> paged;
            quint64 key = 0;
            bool uploaded = false;
            // A VIEW'S OWN PENDING WORK: the coarse chain and resident pages must be brought to
            // the weight before the next draw, and they are only stale when an INPUT changed
            // (weight, either field's stamp, or the pair). A repaint of a frame the blender
            // cache already holds - a paused matte between its keys - changes nothing and
            // refreshes nothing.
            quint64 blendStampA = 0;
            quint64 blendStampB = 0;
            bool viewDirty = false;
            // a paged holder's damage awaiting the next GL-safe call (see markDirty)
            QRect dirtyBox;
            bool dirtyBoxValid = false;
            ~TextureHolder();
            bool built() const { return uploaded || paged != nullptr; }
        };
        mutable std::shared_ptr<TextureHolder> mTexture;

        std::array<util::Easing::Param, kComponents> mEasings;
        QImage mField;
        QRectF mContentRect;
        bool mPainted = false;
        // see setForceFullTexture: an A/B control, not a user-facing fallback
        bool mForceFullTexture = false;

    public:
        Data();

        // Allocate an empty field. aValue is the fill (kNeutral for a fresh mask,
        // 0 for an erased one).
        void alloc(const QSize& aSize, const QRectF& aContentRect, uchar aValue = kNeutral);

        // Set the field from an existing image, converting to Grayscale8 and
        // clamping the texel count.
        void write(const QImage& aField, const QRectF& aContentRect);

        // The blended/animated value lands here.
        void assign(const Data& aRhs);
        void swap(Data& aRhs);
        void clear();

        // true when this Data has a field, OR is a blend view of two keys' fields (see setBlend)
        bool isValid() const;

        // THE ANIMATED FORM. Two keys' fields at a weight, as a paged view instead of a
        // materialized field: every level is a linear filter, so the blended pyramid is the lerp
        // of the two keys' pyramids at the same weight, and the per-frame work is the coarse
        // chain plus the pages the view actually samples - not the whole field, its chain, and a
        // readback. The two keys must be one grid (checked by the caller too). Returns false when
        // the keys cannot be sources (a null field, different grids, a non-pageable size) or the
        // pool declines, and the caller then falls back to the materializing blend.
        bool setBlend(const Data& aA, const Data& aB, int aWeight);

        // The key's own levels, built once (the GPU chain + capture) and kept in the holder.
        // Null when the field is absent or this Data is a view. The blend's sources come from
        // here; virtualTexture() uses it too.
        const MaskPyramid* ensurePyramid() const;

        // The field as a FULL GL texture (uploaded lazily, re-uploaded only when the
        // image behind it changes). Null when there is no field. The matte and its rubylith
        // ask virtualTexture() first: a pageable field is drawn from the page pool.
        const gl::Texture* textureId() const;

        // THE PAGED FORM. Builds (once per field) the CPU pyramid from the field's own chain
        // and the R8 pool resource from it, then drops the chain. Null when the field is not
        // pageable, is not painted, or the pool is disabled - the caller then uses textureId().
        VirtualTexture* virtualTexture() const;

        // FORCES THE FULL-RESOLUTION CHAIN, the A/B control for the page gates (the mask twin of
        // ImageKey::setForceFullTexture). virtualTexture() declines while it is set, so a pageable
        // field is built as a full chain instead - including one already built as a page stack,
        // which then stays alive beside it exactly as ImageKey's control keeps both. It rides the
        // VALUE (assign copies it): the renderer reads an expansion's copy, not the key.
        void setForceFullTexture(bool aForce) { mForceFullTexture = aForce; }
        bool forceFullTexture() const { return mForceFullTexture; }

        // HAS ANYTHING BEEN HIDDEN? A matte at its neutral is a no-op, and it must cost
        // nothing and change nothing: measured, a freshly added folder mask put the
        // folder on the isolated-composite path and shifted 2098 px of the picture
        // before a single stroke. The flag rides the value, like
        // FilterActivity's "is the blended value above its epsilon".
        bool isPainted() const { return mPainted; }

        // THE FLAG FOR A SYNTHESIZED FIELD. alloc/write/markDirty compute it from an image the
        // caller edited; a per-texel BLEND has no image behind it to scan and its producer knows
        // the value the scan would find (see TimeKeyBlender::blendMaskKey). Kept separate from
        // markDirty because that one also patches a built chain, which a fresh blend has not.
        void setPainted(bool aPainted) { mPainted = aPainted; }

        // A PAINTED write goes straight into the image's pixels, which QImage's
        // cacheKey does not see - so the brush says where it wrote and the chain is
        // patched there, level by level, instead of being rebuilt.
        void markDirty(const QRect& aTexels);
        const QImage& field() const { return mField; }
        QImage& field() { return mField; }
        // THE FIELD FOR A READER OUTSIDE THE PAGER (a thumbnail): the field itself, or - for a
        // blend view, which stores none - its level-0 lerp materialized at the current weight.
        // Never cheap on a view; the callers cache the result.
        QImage fieldForDisplay() const;
        QSize texelSize() const { return mField.size(); }
        QRectF contentRect() const { return mContentRect; }

        // THE RAM THIS FIELD HOLDS: its level-0 image (one byte per texel) plus the CPU pyramid a
        // paged form keeps for its levels. It never BUILDS anything - a matte that has not been
        // drawn yet reports its field only - and it holds no GPU bytes: those are the texture
        // accounting (gl::FrameStats::textureCreated) and the pool's own line.
        qint64 ramBytes() const;

        util::Easing::Param& easing(int aComponent) {
            XC_ASSERT(aComponent == 0);
            return mEasings[aComponent];
        }
        const util::Easing::Param& easing(int aComponent) const {
            XC_ASSERT(aComponent == 0);
            return mEasings[aComponent];
        }
    };

    MaskKey();

    Data& data() { return mData; }
    const Data& data() const { return mData; }

    bool belongsToDefaultParent() const { return !parent(); }

    virtual TimeKeyType type() const { return TimeKeyType_Mask; }
    virtual TimeKey* createClone();
    virtual bool serialize(Serializer& aOut) const;
    virtual bool deserialize(Deserializer& aIn);

private:
    Data mData;
};

} // namespace core

#endif // CORE_MASKKEY_H
