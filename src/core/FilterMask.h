#ifndef CORE_FILTERMASK_H
#define CORE_FILTERMASK_H

#include <QImage>
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
// RESOLUTION is capped (kMaxTexels on the long side) because a mask is a soft,
// low-frequency field: 1024 across a 1102-pixel layer is effectively 1:1, and
// across a 15000-pixel canvas it is ~15 canvas pixels per texel - plenty, at
// 1 MiB instead of 225.
class FilterMask: private util::NonCopyable {
public:
    // The long side's texel cap. See the header's note: masks are low frequency.
    static constexpr int kMaxTexels = 1024;

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

    quint64 revision() const { return mRevision; }
    // the value "the filter is unaffected" at (see filterMaskNeutral)
    int neutral() const { return mNeutral; }

    // The GL texture the filter pass samples, created lazily and updated by the
    // dirty box (a stroke uploads the box it touched, not the whole field). The
    // caller must have a current GL context - the renderer does.
    GLuint texture();
    // the revision the texture was last uploaded at (the gates read it)
    quint64 uploadedRevision() const { return mUploadedRevision; }

    // rect + texel size + a PNG blob (deflate is a good fit for a painted field)
    bool serialize(Serializer& aOut) const;
    bool deserialize(Deserializer& aIn);

private:
    void uploadAll();
    void uploadBox(const QRect& aTexels);

    QRectF mContentRect;
    QImage mImage; // Format_Grayscale8
    int mNeutral;  // the value clear() restores (filterMaskNeutral)
    gl::Texture mTexture;
    quint64 mRevision;
    quint64 mUploadedRevision;
    bool mCreated;
    bool mDirtyAll;
    QRect mDirtyBox;
};

} // namespace core

#endif // CORE_FILTERMASK_H
