#include <algorithm>
#include "util/MathUtil.h"
#include "img/ResourceNode.h"
#include "img/ResourceHandle.h"

namespace img {

ResourceData::ResourceData(const QString& aIdentifier, const QString& aIdentityId, const ResourceNode* aSerialAddress):
    mBuffer(),
    mPos(),
    mIsLayer(),
    mIdentifier(aIdentifier),
    // Explicit fallback: a source that carries no stable layer id offers only its
    // name, so the name is the identity. Callers never have to special case it.
    mIdentityId(aIdentityId.isEmpty() ? aIdentifier : aIdentityId),
    mBlendMode(BlendMode_Normal),
    mImageLoader(),
    mSerialAddress(aSerialAddress) {}

void ResourceData::grabImage(const XCMemBlock& aBlock, const QSize& aSize, Format aFormat) {
    mBuffer.grab(aFormat, aBlock, aSize);
}

XCMemBlock ResourceData::releaseImage() { return mBuffer.release(); }

void ResourceData::freeImage() { mBuffer.free(); }

void ResourceData::setPos(const QPoint& aPos) { mPos = aPos; }

void ResourceData::setBlendMode(BlendMode aMode) {
    if (aMode != BlendMode_TERM) {
        mBlendMode = aMode;
    } else {
        mBlendMode = BlendMode_Normal;
    }
}

void ResourceData::copyFrom(const ResourceData& aData) {
    mBuffer = aData.mBuffer;
    mIdentifier = aData.mIdentifier;
    mIdentityId = aData.mIdentityId;
    mPos = aData.mPos;
    mIsLayer = aData.mIsLayer;
    mBlendMode = aData.mBlendMode;
    mImageLoader = aData.mImageLoader;
}

bool ResourceData::loadImage() {
    if (!mImageLoader) {
        return false;
    }
    if (!mImageLoader(*this)) {
        return false;
    }
    // One-shot. The loader owns the pixels of exactly one parse of its source, so
    // running it a second time would either re-read the same bytes or -- for a
    // closure left over from a source that has since been destroyed -- walk
    // through whatever occupies that memory now.
    mImageLoader = nullptr;
    return true;
}

QRect ResourceData::rect() const { return QRect(mPos, mBuffer.pixelSize()); }

QVector2D ResourceData::center() const { return util::MathUtil::getCenter(rect()); }

bool ResourceData::hasSameLayerDataWith(const ResourceData& aData) {
    if (!isLayer() || !aData.isLayer())
        return false;
    if (pos() != aData.pos())
        return false;
    if (blendMode() != aData.blendMode())
        return false;
    if (!hasImage())
        return !aData.hasImage();
    if (!aData.hasImage())
        return false;
    if (image().pixelSize() != aData.image().pixelSize())
        return false;
    if (image().format() != aData.image().format())
        return false;

    auto size = image().size();
    XC_ASSERT(size == aData.image().size());
    auto ptr1 = image().data();
    auto ptr2 = aData.image().data();
    return std::equal(ptr1, ptr1 + size, ptr2);
}

} // namespace img
