#include <QBuffer>
#include <QByteArray>
#include "core/Deserializer.h"
#include <QString>
#include "core/MaskKey.h"
#include "core/Serializer.h"
#include "gl/Global.h"

namespace core {

MaskKey::Data::Data(): mEasings(), mField(), mContentRect() {}

const gl::Texture* MaskKey::Data::textureId() const {
    if (mField.isNull())
        return nullptr;
    if (!mTexture)
        mTexture = std::make_shared<TextureHolder>();
    const quint64 key = mField.cacheKey();
    if (mTexture->uploaded && mTexture->key == key)
        return &mTexture->texture;

    // the pass multiplies .r, so the field rides as opaque grey RGBA - the same
    // choice the painted filter masks make, and the reason the shaders read .r
    const QImage rgba = mField.convertToFormat(QImage::Format_RGBA8888);
    if (!mTexture->uploaded) {
        mTexture->texture.create(mField.size(), rgba.constBits());
        mTexture->texture.setFilter(GL_LINEAR);
        mTexture->texture.setWrap(GL_CLAMP_TO_EDGE);
        mTexture->uploaded = true;
    } else {
        gl::Global::Functions& ggl = gl::Global::functions();
        ggl.glActiveTexture(GL_TEXTURE0);
        ggl.glBindTexture(GL_TEXTURE_2D, mTexture->texture.id());
        ggl.glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        ggl.glTexSubImage2D(
            GL_TEXTURE_2D, 0, 0, 0, mField.width(), mField.height(), GL_RGBA, GL_UNSIGNED_BYTE, rgba.constBits()
        );
        ggl.glBindTexture(GL_TEXTURE_2D, 0);
    }
    mTexture->key = key;
    return &mTexture->texture;
}

void MaskKey::Data::markDirty() {
    mTexture.reset();
    // the brush wrote into the pixels, so the matte now hides something
    mPainted = true;
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
    if (image.size().width() > kMaxTexels || image.size().height() > kMaxTexels) {
        // The cap belongs to the FIELD, not to whoever set it: a matte is low
        // frequency, and downscaling here keeps the serialized blob bounded
        // whatever a caller hands over.
        image = image.scaled(kMaxTexels, kMaxTexels, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
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
    // Sharing the holder is what keeps a STATIC mask free: the same image means the
    // same texture, so a node whose mask has one key uploads it once and then only
    // binds it. Nothing can write through a shared holder - every mutator detaches.
    mTexture = aRhs.mTexture;
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

    // the field: presence, where it lives, its texel grid, then a PNG blob
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
    if (size.isEmpty() || size.width() > kMaxTexels || size.height() > kMaxTexels) {
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
