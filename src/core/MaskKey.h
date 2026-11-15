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
        struct TextureHolder {
            gl::Texture texture;
            MaskMipChain mip;
            quint64 key = 0;
            bool uploaded = false;
        };
        mutable std::shared_ptr<TextureHolder> mTexture;

        std::array<util::Easing::Param, kComponents> mEasings;
        QImage mField;
        QRectF mContentRect;
        bool mPainted = false;

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

        bool isValid() const { return !mField.isNull(); }

        // The field as a GL texture (uploaded lazily, re-uploaded only when the
        // image behind it changes). Null when there is no field.
        const gl::Texture* textureId() const;

        // HAS ANYTHING BEEN HIDDEN? A matte at its neutral is a no-op, and it must cost
        // nothing and change nothing: measured, a freshly added folder mask put the
        // folder on the isolated-composite path and shifted 2098 px of the picture
        // before a single stroke. The flag rides the value, like
        // FilterActivity's "is the blended value above its epsilon".
        bool isPainted() const { return mPainted; }

        // A PAINTED write goes straight into the image's pixels, which QImage's
        // cacheKey does not see - so the brush says where it wrote and the chain is
        // patched there, level by level, instead of being rebuilt.
        void markDirty(const QRect& aTexels);
        const QImage& field() const { return mField; }
        QImage& field() { return mField; }
        QSize texelSize() const { return mField.size(); }
        QRectF contentRect() const { return mContentRect; }

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
