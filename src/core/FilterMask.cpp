#include <QBuffer>
#include <algorithm>
#include <QByteArray>
#include "gl/Global.h"
#include "core/Deserializer.h"
#include "core/FilterMask.h"
#include "core/Serializer.h"

namespace core {

FilterMask::FilterMask():
    mContentRect(), mImage(), mNeutral(0), mTexture(), mRevision(0), mUploadedRevision(0), mCreated(false),
    mDirtyAll(false), mDirtyBox() {}

void FilterMask::reset(const QRectF& aContentRect, int aNeutralValue) {
    mContentRect = aContentRect;
    mNeutral = std::min(std::max(aNeutralValue, 0), 255);
    if (aContentRect.isEmpty()) {
        clear();
        return;
    }
    // The texel grid is the content rectangle scaled down so its long side fits
    // kMaxTexels. A mask is low frequency: at that cap a 15000px canvas still gets
    // ~15 pixels per texel.
    const float longSide = float(qMax(aContentRect.width(), aContentRect.height()));
    const float scale = longSide > float(kMaxTexels) ? float(kMaxTexels) / longSide : 1.0f;
    const QSize size(
        qMax(1, qRound(aContentRect.width() * scale)), qMax(1, qRound(aContentRect.height() * scale))
    );
    mImage = QImage(size, QImage::Format_Grayscale8);
    mImage.fill(uchar(mNeutral));
    mCreated = false;
    markDirtyAll();
}

void FilterMask::clear() {
    if (mImage.isNull())
        return;
    mImage.fill(uchar(mNeutral));
    markDirtyAll();
}

void FilterMask::markDirty(const QRect& aTexels) {
    if (mImage.isNull() || aTexels.isEmpty())
        return;
    const QRect box = aTexels.intersected(mImage.rect());
    if (box.isEmpty())
        return;
    mDirtyBox = mDirtyBox.isNull() ? box : mDirtyBox.united(box);
    ++mRevision;
}

void FilterMask::markDirtyAll() {
    if (mImage.isNull())
        return;
    mDirtyAll = true;
    ++mRevision;
}

GLuint FilterMask::texture() {
    if (mImage.isNull())
        return 0;
    if (!mCreated) {
        uploadAll();
        return mTexture.id();
    }
    if (mRevision != mUploadedRevision) {
        if (mDirtyAll) {
            uploadAll();
        } else if (!mDirtyBox.isNull()) {
            uploadBox(mDirtyBox);
        }
        mUploadedRevision = mRevision;
        mDirtyAll = false;
        mDirtyBox = QRect();
    }
    return mTexture.id();
}

void FilterMask::uploadAll() {
    if (mImage.isNull())
        return;
    // the pass multiplies .r, so the field is uploaded as opaque grey RGBA
    QImage rgba = mImage.convertToFormat(QImage::Format_RGBA8888);
    {
        // a grayscale image converted to RGBA repeats the value and sets alpha; the
        // shader reads .r, so nothing else to do
    }
    if (!mCreated) {
        mTexture.create(mImage.size(), rgba.constBits());
        mTexture.setFilter(GL_LINEAR);
        mTexture.setWrap(GL_CLAMP_TO_EDGE);
        mCreated = true;
    } else {
        gl::Global::Functions& ggl = gl::Global::functions();
        ggl.glBindTexture(GL_TEXTURE_2D, mTexture.id());
        ggl.glTexSubImage2D(
            GL_TEXTURE_2D, 0, 0, 0, mImage.width(), mImage.height(), GL_RGBA, GL_UNSIGNED_BYTE, rgba.constBits()
        );
        ggl.glBindTexture(GL_TEXTURE_2D, 0);
    }
    mUploadedRevision = mRevision;
    mDirtyAll = false;
    mDirtyBox = QRect();
}

void FilterMask::uploadBox(const QRect& aTexels) {
    if (mImage.isNull())
        return;
    const QRect box = aTexels.intersected(mImage.rect());
    if (box.isEmpty())
        return;

    // the sub-image is converted on its own so the upload is one contiguous box
    const QImage sub = mImage.copy(box).convertToFormat(QImage::Format_RGBA8888);
    gl::Global::Functions& ggl = gl::Global::functions();
    ggl.glBindTexture(GL_TEXTURE_2D, mTexture.id());
    ggl.glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    ggl.glTexSubImage2D(
        GL_TEXTURE_2D, 0, box.x(), box.y(), box.width(), box.height(), GL_RGBA, GL_UNSIGNED_BYTE, sub.constBits()
    );
    ggl.glBindTexture(GL_TEXTURE_2D, 0);
}

bool FilterMask::serialize(Serializer& aOut) const {
    aOut.write(mContentRect);
    aOut.write(mImage.size());
    aOut.write(mNeutral);

    QByteArray bytes;
    if (!mImage.isNull()) {
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        if (!mImage.save(&buffer, "PNG"))
            return false;
    }
    aOut.write(XCMemBlock((uint8*)bytes.data(), size_t(bytes.size())));
    return aOut.checkStream();
}

bool FilterMask::deserialize(Deserializer& aIn) {
    aIn.pushLogScope("FilterMask");
    aIn.read(mContentRect);
    QSize size;
    aIn.read(size);
    aIn.read(mNeutral);

    XCMemBlock blob;
    if (!aIn.readWithAlloc(blob)) {
        return aIn.errored("invalid filter mask bytes");
    }
    if (size.isEmpty() || size.width() > kMaxTexels || size.height() > kMaxTexels) {
        delete[] blob.data;
        return aIn.errored("invalid filter mask size");
    }

    QImage image = QImage::fromData(QByteArray::fromRawData((const char*)blob.data, int(blob.size)), "PNG");
    delete[] blob.data;
    if (image.isNull() || image.size() != size) {
        return aIn.errored("invalid filter mask image");
    }

    mImage = image.convertToFormat(QImage::Format_Grayscale8);
    mCreated = false;
    mDirtyAll = true;
    ++mRevision;

    aIn.popLogScope();
    return aIn.checkStream();
}

} // namespace core
