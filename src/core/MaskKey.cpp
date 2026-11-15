#include <QBuffer>
#include <QByteArray>
#include "core/Deserializer.h"
#include <QString>
#include "core/MaskKey.h"
#include "core/Serializer.h"

namespace core {

MaskKey::Data::Data(): mEasings(), mField(), mContentRect() {}

const gl::Texture* MaskKey::Data::textureId() const {
    if (mField.isNull())
        return nullptr;
    if (!mTexture)
        mTexture = std::make_shared<TextureHolder>();

    // A holder that does not describe THIS image is dropped rather than patched: it can be shared
    // with another Data (a blend result, an undo state) that still points at the image it was
    // built from, and rebuilding over it would replace THEIR texture with this one's pixels. See
    // markDirty for how a holder comes to be ahead of an image.
    if (mTexture->uploaded && mTexture->key != mField.cacheKey())
        mTexture = std::make_shared<TextureHolder>();

    if (!mTexture->uploaded) {
        // the whole field, once: ONE BYTE PER TEXEL as GL_R8 (see core/FilterMask.cpp) - the pass
        // multiplies .r, so an RGBA upload would be four times the VRAM - plus the mip chain its
        // minified sampling needs
        mTexture->mip.create(mTexture->texture, mField);
        mTexture->uploaded = true;
        mTexture->key = mField.cacheKey();
        return &mTexture->texture;
    }

    // the accumulated damage: level 0's box from the image and one box per level above it
    mTexture->mip.update(mTexture->texture, mField);
    return &mTexture->texture;
}

void MaskKey::Data::markDirty(const QRect& aTexels) {
    // the brush wrote into the pixels, so the matte now hides something
    mPainted = true;
    if (!mTexture || !mTexture->uploaded)
        return;   // nothing built yet: the next textureId() builds the field it finds

    // THE BOX IS THE WHOLE DIFFERENCE. A paint writes through the image's scanlines, which detaches
    // the pixels (QImage's copy-on-write) and changes cacheKey, but the holder's chain still
    // describes the pre-stamp image and the only pixels that differ are the box the brush reports.
    // So the chain is patched there - one box per level, on the GPU - and the holder is moved on to
    // the detached image. A copy taken before the stamp keeps the old key, so undo rebuilds.
    if (mTexture->mip.isValid() && mTexture->mip.baseSize() == mField.size()) {
        mTexture->mip.markDirty(aTexels);
        mTexture->key = mField.cacheKey();
    } else {
        mTexture.reset();   // a different grid: this chain is not this field's
    }
}

void MaskKey::Data::alloc(const QSize& aSize, const QRectF& aContentRect, uchar aValue) {
    mTexture.reset();   // a new image always gets its own texture
    const QSize size(aSize.expandedTo(QSize(1, 1)));
    mField = QImage(size, QImage::Format_Grayscale8);
    mField.fill(aValue);
    mContentRect = aContentRect;
    mPainted = (aValue != kNeutral);
}

void MaskKey::Data::write(const QImage& aField, const QRectF& aContentRect) {
    mTexture.reset();
    if (aField.isNull()) {
        clear();
        return;
    }
    QImage image = aField;
    // NO DOWNSCALE HERE: the field is the content's own resolution (see kMaxStoredTexels), so
    // whatever a caller hands over is kept as it is - a resampled matte is a blurrier matte, and
    // the caller (TimeLineUtil::maskTexelSize) already builds the right grid.
    if (image.format() != QImage::Format_Grayscale8)
        image = image.convertToFormat(QImage::Format_Grayscale8);
    mField = image;
    mContentRect = aContentRect;
    // an AUTHORING path (an import, an invert): scan once here rather than per frame
    mPainted = false;
    for (int y = 0; y < image.height() && !mPainted; ++y) {
        const uchar* row = image.constScanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            if (row[x] != kNeutral) {
                mPainted = true;
                break;
            }
        }
    }
}

void MaskKey::Data::assign(const Data& aRhs) {
    mField = aRhs.mField;
    mContentRect = aRhs.mContentRect;
    mEasings = aRhs.mEasings;
    mPainted = aRhs.mPainted;
    // Sharing the holder is what keeps a STATIC mask free: the same image means the same
    // texture, so a node whose mask has one key uploads it once and then only binds it. Nothing
    // can write through a shared holder - every whole-field mutator sheds it - and a holder that
    // was patched ahead of its image (markDirty) is not shared either, because the copy would then
    // show pixels its own image does not have.
    if (aRhs.mTexture && aRhs.mTexture->uploaded && aRhs.mTexture->key == aRhs.mField.cacheKey())
        mTexture = aRhs.mTexture;
    else
        mTexture.reset();
}

void MaskKey::Data::swap(Data& aRhs) {
    mField.swap(aRhs.mField);
    mTexture.swap(aRhs.mTexture);
    std::swap(mPainted, aRhs.mPainted);
    std::swap(mContentRect, aRhs.mContentRect);
    std::swap(mEasings, aRhs.mEasings);
}

void MaskKey::Data::clear() {
    mTexture.reset();
    mPainted = false;
    mField = QImage();
    mContentRect = QRectF();
}

MaskKey::MaskKey(): TimeKey(), mData() {}

TimeKey* MaskKey::createClone() {
    auto newKey = new MaskKey();
    newKey->mData.assign(mData);
    return newKey;
}

bool MaskKey::serialize(Serializer& aOut) const {
    // easing
    aOut.writeEasings(&mData.easing(0), kComponents);

    // the field: presence, where it lives, its texel grid, then a PNG blob. The grid is the
    // CONTENT's own - the mip chain is an allocation detail of the texture and never reaches the
    // file.
    const bool valid = mData.isValid();
    aOut.write(valid);
    if (valid) {
        aOut.write(mData.contentRect());
        aOut.write(mData.texelSize());

        QByteArray bytes;
        {
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            if (!mData.field().save(&buffer, "PNG")) {
                return false;
            }
        }
        aOut.write(XCMemBlock((uint8*)bytes.data(), size_t(bytes.size())));
    }
    return aOut.checkStream();
}

bool MaskKey::deserialize(Deserializer& aIn) {
    aIn.pushLogScope("MaskKey");

    if (!aIn.readEasings(&mData.easing(0), kComponents)) {
        return aIn.errored("invalid easing param");
    }

    bool valid = false;
    aIn.read(valid);
    if (!valid) {
        mData.clear();
        aIn.popLogScope();
        return aIn.checkStream();
    }

    QRectF rect;
    QSize size;
    aIn.read(rect);
    aIn.read(size);

    XCMemBlock blob;
    if (!aIn.readWithAlloc(blob)) {
        return aIn.errored("invalid mask bytes");
    }
    if (size.isEmpty() || size.width() > kMaxStoredTexels || size.height() > kMaxStoredTexels) {
        delete[] blob.data;
        return aIn.errored("invalid mask size");
    }

    QImage image = QImage::fromData(QByteArray::fromRawData((const char*)blob.data, int(blob.size)), "PNG");
    delete[] blob.data;
    if (image.isNull() || image.size() != size) {
        return aIn.errored("invalid mask image");
    }

    mData.write(image, rect);
    aIn.popLogScope();
    return aIn.checkStream();
}

} // namespace core
