#include <QBuffer>
#include <QByteArray>
#include "core/Deserializer.h"
#include <QString>
#include "core/MaskKey.h"
#include "core/MaskPyramid.h"
#include "core/Serializer.h"
#include "core/VirtualTexture.h"

namespace core {

MaskKey::Data::Data(): mEasings(), mField(), mContentRect() {}

bool MaskKey::Data::isValid() const {
    return !mField.isNull() || (mTexture && mTexture->pyramid && mTexture->pyramid->isBlend());
}

QImage MaskKey::Data::fieldForDisplay() const {
    if (!mField.isNull())
        return mField;
    if (mTexture && mTexture->pyramid && mTexture->pyramid->isBlend())
        return mTexture->pyramid->displayField();
    return QImage();
}

MaskKey::Data::TextureHolder::~TextureHolder() = default;

const gl::Texture* MaskKey::Data::textureId() const {
    // A VIEW HAS NO FIELD TO UPLOAD. The full chain is built from the materialized field, and the
    // blender installs a view only when the paged path may draw it (see setBlend); a caller that
    // forces the full resolution gets the materialized blend instead.
    if (mField.isNull())
        return nullptr;
    if (!mTexture)
        mTexture = std::make_shared<TextureHolder>();
    if (mTexture->pyramid && mTexture->pyramid->isBlend())
        return nullptr;

    // A holder that does not describe THIS image is dropped rather than patched: it can be shared
    // with another Data (a blend result, an undo state) that still points at the image it was
    // built from, and rebuilding over it would replace THEIR texture with this one's pixels. See
    // markDirty for how a holder comes to be ahead of an image.
    if (mTexture->built() && mTexture->key != mField.cacheKey())
        mTexture = std::make_shared<TextureHolder>();

    if (!mTexture->uploaded) {
        // the whole field, once: ONE BYTE PER TEXEL as GL_R8 (see core/FilterMask.cpp) - the pass
        // multiplies .r, so an RGBA upload would be four times the VRAM - plus the mip chain its
        // minified sampling needs. A PAGED holder keeps its pool resource: both forms describe the
        // field, and markDirty keeps both in step, so a caller that wants the full chain pays for
        // it but does not destroy the paged one.
        mTexture->mip.create(mTexture->texture, mField);
        mTexture->uploaded = true;
        mTexture->key = mField.cacheKey();
        return &mTexture->texture;
    }

    // the accumulated damage: level 0's box from the image and one box per level above it
    mTexture->mip.update(mTexture->texture, mField);
    return &mTexture->texture;
}

const MaskPyramid* MaskKey::Data::ensurePyramid() const {
    if (mField.isNull())
        return nullptr;
    if (!mTexture)
        mTexture = std::make_shared<TextureHolder>();
    else if (mTexture->pyramid && mTexture->pyramid->isBlend())
        return nullptr;   // a view consumes sources; it is not one
    const quint64 fieldKey = mField.cacheKey();
    // THE HOLDER DESCRIBES ONE FIELD, and a stroke's damage box is the only thing that can move it
    // on without a rebuild (markDirty, and only for a PAGED holder). A pyramid built here for a
    // BLEND SOURCE has no paged resource, so a stroke that rewrites the field leaves the holder
    // behind its image and it must be rebuilt rather than read.
    if ((mTexture->built() || mTexture->pyramid) && mTexture->key != fieldKey)
        mTexture = std::make_shared<TextureHolder>();

    if (!mTexture->pyramid) {
        // THE ONE GPU BUILD. The chain owns the resample rule (MaskMipChain), so the pyramid is
        // captured from it rather than computed here - see MaskPyramid's class note. Reuse the
        // full chain when the holder already has one for this field (a mask that was drawn before
        // the pool got to it, or a tool's preview), otherwise build a transient one and drop it.
        gl::Texture transient;
        MaskMipChain transientMip;
        const gl::Texture* source = nullptr;
        if (mTexture->uploaded && mTexture->key == fieldKey && mTexture->texture.id() != 0) {
            mTexture->mip.update(mTexture->texture, mField);
            source = &mTexture->texture;
        } else {
            transientMip.create(transient, mField);
            source = &transient;
        }
        auto pyramid = std::make_unique<MaskPyramid>();
        const bool ok = pyramid->capture(*source);
        if (transient.id() != 0)
            transient.destroy();
        if (!ok)
            return nullptr;
        mTexture->pyramid = std::move(pyramid);
        mTexture->key = fieldKey;
    }
    return mTexture->pyramid.get();
}

VirtualTexture* MaskKey::Data::virtualTexture() const {
    if (mForceFullTexture || !mPainted)
        return nullptr;
    // THE VIEW FIRST. A blended matte has no field of its own and its content moves with the
    // weight every frame, so the coarse chain (always resident) and the resident pages are
    // brought to THIS frame's weight here - once, the first time the frame asks for the matte -
    // and not in the landing, which still carries the previous frame's weight.
    if (mTexture && mTexture->pyramid && mTexture->pyramid->isBlend()) {
        if (!mTexture->paged)
            return nullptr;
        if (mTexture->viewDirty) {
            mTexture->paged->refreshMaskView();
            mTexture->viewDirty = false;
        }
        return mTexture->paged.get();
    }
    if (mField.isNull())
        return nullptr;
    if (!VirtualTexture::pageable(mField.size()))
        return nullptr;
    if (!ensurePyramid())
        return nullptr;

    // THE DAMAGE IS APPLIED HERE, not in markDirty: the brush paints outside any guaranteed GL
    // context, and this accessor is called from the render path (LayerNode's demand pass), which
    // is exactly the point where an upload is safe.
    if (mTexture->paged) {
        if (mTexture->dirtyBoxValid) {
            mTexture->paged->updateMaskBox(mField, mTexture->dirtyBox);
            mTexture->dirtyBox = QRect();
            mTexture->dirtyBoxValid = false;
        }
        // the page fill reads level 0 from the field; the QImage copy is a refcount
        mTexture->paged->setMaskField(mField);
        return mTexture->paged.get();
    }

    auto paged = std::make_unique<VirtualTexture>();
    if (!paged->buildMask(*mTexture->pyramid)) {
        mTexture->pyramid.reset();
        return nullptr;
    }
    mTexture->paged = std::move(paged);
    mTexture->paged->setMaskField(mField);
    mTexture->key = mField.cacheKey();
    // the pyramid is the authority from here: the full chain is not what a page fill reads, and
    // keeping it would double the field's GPU cost for nothing
    if (mTexture->uploaded) {
        mTexture->texture.destroy();
        mTexture->uploaded = false;
    }
    return mTexture->paged.get();
}

bool MaskKey::Data::setBlend(const Data& aA, const Data& aB, int aWeight) {
    // THE TWO SOURCES. Each key builds its own pyramid ONCE (the GPU chain + capture, exactly as
    // a static matte does); the view borrows them and never copies their levels.
    const MaskPyramid* pa = aA.ensurePyramid();
    const MaskPyramid* pb = aB.ensurePyramid();
    if (!pa || !pb || pa == pb || pa->size() != pb->size())
        return false;
    if (!VirtualTexture::pageable(pa->size()))
        return false;

    const bool samePair = mTexture && mTexture->pyramid && mTexture->pyramid->isBlend()
        && mTexture->pyramid->blendA() == pa && mTexture->pyramid->blendB() == pb;
    const quint64 stampA = aA.field().cacheKey();
    const quint64 stampB = aB.field().cacheKey();
    // THE SAME INPUTS PUT THE SAME PIXELS ON SCREEN, so a frame the blender re-derives with an
    // unchanged weight, pair and pair of fields (a repaint, or two frames at one weight) has
    // nothing to refresh. The fields' cacheKeys see a stroke: a paint detaches the shared image.
    if (samePair && mTexture->blendStampA == stampA && mTexture->blendStampB == stampB
        && mTexture->pyramid->blendWeight() == aWeight) {
        return true;
    }
    if (!samePair) {
        // a new pair (or a new grid): the resource names the view's size and levels, so it is
        // rebuilt; the old one releases its pool pages through destroy()
        mTexture = std::make_shared<TextureHolder>();
        mTexture->pyramid = std::make_unique<MaskPyramid>();
    }
    if (!mTexture->pyramid->setBlend(pa, aA.field(), pb, aB.field(), aWeight))
        return false;

    mField = QImage();   // the view answers level 0 from the two fields
    mContentRect = aA.contentRect();
    mEasings = aA.mEasings;
    mPainted = aA.isPainted() || aB.isPainted();
    mForceFullTexture = false;

    mTexture->blendStampA = stampA;
    mTexture->blendStampB = stampB;
    if (!samePair) {
        mTexture->paged.reset();
        auto paged = std::make_unique<VirtualTexture>();
        if (!paged->buildMask(*mTexture->pyramid)) {
            mTexture.reset();
            return false;
        }
        mTexture->paged = std::move(paged);
        // buildMask uploaded the coarse chain from the view at THIS weight and no page is
        // resident yet, so the new resource is already current: the next frame is the one that
        // needs a refresh.
        mTexture->viewDirty = false;
        return true;
    }
    // THE SAME RESOURCE, MOVED: its coarse chain and every resident page still hold the previous
    // weight (or the previous fields), and the draw is this frame.
    mTexture->viewDirty = true;
    return true;
}

qint64 MaskKey::Data::ramBytes() const {
    if (mField.isNull())
        return 0;
    // Format_Grayscale8: one byte per texel, and the field is the level-0 authority (the pyramid
    // stores levels 1 and deeper, so the two never overlap)
    qint64 bytes = (qint64)mField.width() * mField.height();
    if (mTexture && mTexture->pyramid)
        bytes += mTexture->pyramid->bytes();
    return bytes;
}

void MaskKey::Data::markDirty(const QRect& aTexels) {
    // the brush wrote into the pixels, so the matte now hides something
    mPainted = true;
    if (!mTexture || !mTexture->built())
        return;   // nothing built yet: the next build reads the field it finds

    // THE BOX IS THE WHOLE DIFFERENCE. A paint writes through the image's scanlines, which detaches
    // the pixels (QImage's copy-on-write) and changes cacheKey, but the holder's chain/pyramid
    // still describe the pre-stamp image and the only pixels that differ are the box the brush
    // reports. So those are patched there - one box per level - and the holder is moved on to the
    // detached image. A copy taken before the stamp keeps the old key, so undo rebuilds.
    bool patched = false;
    // a VIEW holder's pyramid describes two keys, not this field's grid: it is never patched here
    if (mTexture->paged && mTexture->pyramid && !mTexture->pyramid->isBlend()
        && mTexture->pyramid->size() == mField.size()) {
        // GPU work is DEFERRED to virtualTexture() (the brush has no guaranteed context); the box
        // is a union, so a fast drag's stamps cost one update per frame, not one per stamp
        mTexture->dirtyBox = mTexture->dirtyBoxValid
            ? mTexture->dirtyBox.united(aTexels)
            : aTexels;
        mTexture->dirtyBoxValid = true;
        patched = true;
    }
    if (mTexture->uploaded && mTexture->mip.isValid() && mTexture->mip.baseSize() == mField.size()) {
        mTexture->mip.markDirty(aTexels);
        patched = true;
    }
    if (patched)
        mTexture->key = mField.cacheKey();
    else if (mTexture->built())
        mTexture.reset();   // a different grid: this chain is not this field's
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
    mForceFullTexture = aRhs.mForceFullTexture;

    // ONE HOLDER FOR ONE FIELD, AND THE SOURCE IS LINKED TO IT. The per-frame expansion assignment
    // used to take the source's holder only when it was already built; a holder built on the
    // EXPANS side was then invisible to the key, so the brush's markDirty (which runs on the KEY)
    // could not reach the paged resource the frame draws from. The rules below keep a holder
    // BUILT for this field when either side has one - preferring ours, which may be the one
    // carrying the page pool - and otherwise share a placeholder between the two, so whichever
    // side builds first builds for both.
    //
    // Sharing is only ever with a holder that describes these pixels: a holder built for another
    // image is dropped rather than patched, exactly as textureId()/markDirty require, because a
    // copy taken before a stamp must rebuild rather than show pixels its own image does not have.
    const quint64 fieldKey = mField.cacheKey();
    // A VIEW HOLDER IS NEVER SHARED WITH A KEY: it is the expansion's animated resource, not a
    // description of this image's pixels, so it can neither receive this field's texture nor be
    // the holder the key renders from.
    const auto builtForField = [fieldKey](const std::shared_ptr<TextureHolder>& aHolder) {
        return aHolder && aHolder->built() && aHolder->key == fieldKey
            && !(aHolder->pyramid && aHolder->pyramid->isBlend());
    };
    if (builtForField(mTexture)) {
        // ours is live: let the source render from it too
        aRhs.mTexture = mTexture;
    } else if (builtForField(aRhs.mTexture)) {
        mTexture = aRhs.mTexture;
    } else {
        // neither describes this field yet: one placeholder, shared, that the first build fills
        if (!aRhs.mTexture)
            aRhs.mTexture = std::make_shared<TextureHolder>();
        mTexture = aRhs.mTexture;
    }
}

void MaskKey::Data::swap(Data& aRhs) {
    mField.swap(aRhs.mField);
    mTexture.swap(aRhs.mTexture);
    std::swap(mPainted, aRhs.mPainted);
    std::swap(mContentRect, aRhs.mContentRect);
    std::swap(mEasings, aRhs.mEasings);
    std::swap(mForceFullTexture, aRhs.mForceFullTexture);
}

void MaskKey::Data::clear() {
    mTexture.reset();
    mPainted = false;
    mForceFullTexture = false;
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
