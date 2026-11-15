#ifndef CORE_MASKKEY_H
#define CORE_MASKKEY_H

#include <array>
#include <memory>
#include <QImage>
#include <QRectF>
#include <QSize>
#include "util/Easing.h"
#include "core/KeyEasing.h"
#include "core/TimeKey.h"
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

    // The texel cap the painted filter masks already use (core/FilterMask.h):
    // a matte is low frequency, and 1024 texels on the long side is 1 MiB as an
    // 8-bit field instead of the 225 MiB the same field would cost at the
    // document's own resolution.
    static constexpr int kMaxTexels = 1024;

    // The value a fresh mask starts at: REVEAL ALL. Unlike a filter mask, whose
    // neutral is per input, a layer mask's neutral is "nothing hidden", so
    // adding one is visually a no-op until it is painted.
    static constexpr uchar kNeutral = 255;

    class Data {
        // The GL side of the field, kept beside the image and keyed on its
        // cacheKey(). It is SHARED through the copy (a copy of a QImage shares its
        // pixels, so it shares the texture that holds them) and DETACHED by every
        // mutator - a blended field is a new image and must never upload over the
        // key's own texture.
        struct TextureHolder {
            gl::Texture texture;
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
        // cacheKey does not see - so the brush says so and the next frame re-uploads.
        void markDirty();
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
