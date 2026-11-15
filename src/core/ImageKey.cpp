#include "Project.h"
#include <functional>
#include <vector>
#include "img/Buffer.h"
#include "img/ResourceNode.h"
#include "img/BlendMode.h"
#include "img/Util.h"
#include "core/ImageKey.h"
#include "core/Constant.h"
#include "qjsonobject.h"
#include "util/LinkPointer.h"

namespace core {

//-------------------------------------------------------------------------------------------------
ImageKey::Data::Data(): mEasings(), mResHandle(), mBlendMode(img::BlendMode_Normal), mImageOffset(), mGridMesh() {}

ImageKey::Data::Data(const Data& aRhs):
    mEasings(aRhs.mEasings), mResHandle(aRhs.mResHandle), mBlendMode(aRhs.mBlendMode), mImageOffset() {
    setImageOffset(aRhs.mImageOffset);
    mGridMesh = aRhs.mGridMesh;
}

ImageKey::Data& ImageKey::Data::operator=(const Data& aRhs) {
    mEasings = aRhs.mEasings;
    mResHandle = aRhs.mResHandle;
    mBlendMode = aRhs.mBlendMode;
    setImageOffset(aRhs.mImageOffset);
    mGridMesh = aRhs.mGridMesh;
    return *this;
}

void ImageKey::Data::setImageOffset(const QVector2D& aOffset) {
    const QVector2D offset(
        xc_clamp(aOffset.x(), Constant::transMin(), Constant::transMax()),
        xc_clamp(aOffset.y(), Constant::transMin(), Constant::transMax())
    );
    mImageOffset = offset;
    mGridMesh.setOriginOffset(offset);
}

//-------------------------------------------------------------------------------------------------
ImageKey::Cache::Cache(): mTexture() {}

//-------------------------------------------------------------------------------------------------
ImageKey::ImageKey(): mData(), mCache(), mSleepCount(0) { mData.resource().setOriginKeeping(true); }

TimeKey* ImageKey::createClone() {
    auto newKey = new ImageKey();
    newKey->mData = this->mData;
    newKey->resetTextureCache();
    return newKey;
}

void ImageKey::setImage(const img::ResourceHandle& aResource, img::BlendMode aMode) {
    mData.setBlendMode(aMode);
    setImage(aResource);
}

void ImageKey::setImage(const img::ResourceHandle& aResource) {
    mData.resource() = aResource;
    resetTextureCache();
}

void ImageKey::setImageOffset(const QVector2D& aOffset) { mData.setImageOffset(aOffset); }

void ImageKey::setImageOffsetByCenter() {
    if (hasImage()) {
        auto size = mData.resource()->image().pixelSize();
        mData.setImageOffset(QVector2D(-size.width() * 0.5f, -size.height() * 0.5f));
    }
}

void ImageKey::resetGridMesh(int aCellSize) {
    if (mData.resource()->hasImage()) {
        auto data = mData.resource()->image().data();
        auto size = mData.resource()->image().pixelSize();

        // const int cellPx = std::max(std::min(8, pixelSize.width() / 4), 2);
        auto cell = std::max(aCellSize, Constant::imageCellSizeMin());
        while (img::GridMeshCreator::getCellTableCount(size, cell) > Constant::imageCellCountMax()) {
            ++cell;
        }
        cell = std::min(cell, Constant::imageCellSizeMax());
        mData.gridMesh().createFromImage(data, size, cell);
    }
}

void ImageKey::resetTextureCache() {
    if (mData.resource()->hasImage()) {
        const img::Buffer& image = mData.resource()->image();
        const uint8* imageData = image.data();
        const QSize pixelSize = image.pixelSize();

        // The texture is the CONTENT's own size - no padding, no alignment unit - and holds
        // ALPHA-PREMULTIPLIED bytes. Both are conditions on the chain, and both are documented
        // where they are decided: img::Util::makeTextureUpload. Premultiplied because
        // glGenerateMipmap box-filters each channel independently (a straight chain drags the
        // transparent region's RGB into every level's silhouette).
        //
        // Content-sized, two reasons. Memory: a right/bottom pad up to a multiple of 2^l costs up
        // to +98% of a thin layer (144 texels rounds up to 256) and its only job was to let
        // glGenerateMipmap generate the first levels - gl::Texture::create now builds every level
        // that does not halve exactly, on the GPU, with the area weights the grid implies (from
        // level 0 while the footprint is short, from the level above after that), so every level is
        // the average it claims to be for ANY size, odd sides included. Geometry: the texture's
        // texel (x, y) has to be the content's texel (x, y). The
        // mesh's texture coordinates are content pixels (core::GridMesh), the layer's vertex shader
        // divides them by uImageSize (LayerNode sets it from areaTexture()->size()), so those two
        // sizes being equal is what keeps the mapping identity.
        //
        // The transparent neighbour a minified or edge read needs comes from the border colour set
        // below; the texture is created with GL_CLAMP_TO_BORDER, which the hardware applies to every
        // mip level.
        const std::vector<uint8> upload = img::Util::makeTextureUpload(imageData, pixelSize);

        // make a gl texture
        mCache.texture().create(
            pixelSize, upload.data(), GL_RGBA, GL_RGBA8, GL_UNSIGNED_BYTE, gl::Texture::GenerateMipmap
        );
        // trilinear minification; magnification must stay non-mipmap (see gl::Texture)
        mCache.texture().setFilter(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR);
        // ZERO: the layer shader computes the level itself (LayerDrawingFrag.glsl's sampleLayer and
        // LayerNode's uniform block), and applies ImageKey::layerLodBias() to its own LOD. A GL bias
        // here would ALSO shift the one path that still uses texture() - magnification - and double
        // the setting if a driver ever applied it to textureLod.
        mCache.texture().setLodBias(0.0f);
        // ...and enough samples along an elongated footprint that its SHORT axis is not filtered at
        // the level its long axis asked for. See ImageKey::layerMaxAnisotropy.
        mCache.texture().setMaxAnisotropy(layerMaxAnisotropy());
        mCache.texture().setWrap(GL_CLAMP_TO_BORDER, QColor(0, 0, 0, 0));
    }
}

void ImageKey::sleep() {
    ++mSleepCount;
    if (mSleepCount == 1) {
        mData.resource().setOriginKeeping(false);
    }
}

void ImageKey::awake() {
    XC_ASSERT(mSleepCount > 0);
    --mSleepCount;
    if (mSleepCount == 0) {
        mData.resource().setOriginKeeping(true);
    }
}

QJsonObject ImageKey::serializeToJson() const {
    QJsonObject image;
    image["Identifier"] = mData.resource()->serialAddress()->handle()->identifier();
    // The stable layer id, so a paste can tell two layers apart that were renamed
    // to the same name. It equals "Identifier" for sources that carry no stable
    // id (ORA, older PSDs, non-Photoshop writers) -- the name is all they have.
    // Files written before this key existed omit it, and reading those falls back
    // to the name.
    image["LayerId"] = mData.resource()->serialAddress()->handle()->identityId();
    image["BlendMode"] = getBlendNameFromBlendMode(mData.blendMode());
    image["OffsetX"] = mData.imageOffset().x();
    image["OffsetY"] = mData.imageOffset().y();
    image["GridMesh"] = mData.gridMesh().serializeToJson();
    return image;
}

namespace {
// Records every node the predicate accepts, in document order.
//
// The matcher this replaces walked the sibling list, then the children of every
// sibling, then "Get children" walked the whole child list again -- so a key
// pasted into a PSD with nested folders re-traversed the same nodes once per
// nesting level, which is exponential in the folder depth. This is one pass over
// the tree.
void findResourceMatches(
    img::ResourceNode* aNode, const std::function<bool(const img::ResourceData&)>& aAccepts,
    img::ResourceNode** aDst, int* aCount)
{
    if (aAccepts(aNode->data())) {
        ++(*aCount);
        *aDst = aNode;
    }
    for (auto child : aNode->children()) {
        findResourceMatches(child, aAccepts, aDst, aCount);
    }
}
} // unnamed

bool ImageKey::deserializeFromJson(QJsonObject json, util::LifeLink::Pointee<Project> project) {
    mData.resource().reset();
    // image id
    json = json["Image"].toObject();
    QString identifier = json["Identifier"].toString();
    const QString layerId = json["LayerId"].toString();

    // What to resolve by. The stable layer id distinguishes layers that were
    // renamed to the same name; a file with no "LayerId" was written before the
    // key existed, and those resolve by display name, exactly as they always
    // have -- with the difference that an ambiguous answer is now a refusal.
    std::function<bool(const img::ResourceData&)> accepts;
    if (layerId.isEmpty()) {
        accepts = [&identifier](const img::ResourceData& aData) { return aData.identifier() == identifier; };
    } else {
        accepts = [&layerId](const img::ResourceData& aData) { return aData.identityId() == layerId; };
    }

    int matchCount = 0;
    img::ResourceNode* nodePtr = nullptr;
    for (const ResourceHolder::ImageTree& image : project.address->resourceHolder().imageTrees()) {
        findResourceMatches(image.topNode, accepts, &nodePtr, &matchCount);
    }
    // Refuse anything that is not exactly one answer. The old loop kept the last
    // tree's match, so a paste whose layer name existed in two image trees bound
    // the key to whichever tree happened to be visited last -- silently, with the
    // wrong pixels under a right-looking name.
    if (nodePtr == nullptr || matchCount != 1) {
        return false;
    }
    this->mData.resource() = nodePtr->handle();
    this->resetTextureCache();

    // image offset
    QVector2D offset{static_cast<float>(json["OffsetX"].toDouble()), static_cast<float>(json["OffsetY"].toDouble())};
    mData.setImageOffset(offset);
    // grid mesh
    if (!mData.gridMesh().deserializeFromJson(json)) {
        return false;
    }
    // blend mode
    QString bname = json["BlendMode"].toString();
    auto bmode = img::getBlendModeFromQuadId(bname);
    if (!(bmode == img::BlendMode_TERM)) {
        mData.setBlendMode(bmode);
    }
    return true;
}

bool ImageKey::serialize(Serializer& aOut) const {
    // easing
    aOut.writeEasings(&mData.easing(0), kComponents);
    // image id
    aOut.writeID(mData.resource()->serialAddress());
    // blend mode
    aOut.writeFixedString(img::getQuadIdFromBlendMode(mData.blendMode()), 4);
    // image offset
    aOut.write(mData.imageOffset());
    // grid mesh
    if (!mData.gridMesh().serialize(aOut)) {
        return false;
    }

    return aOut.checkStream();
}

bool ImageKey::deserialize(Deserializer& aIn) {
    mData.resource().reset();

    aIn.pushLogScope("ImageKey");

    // easing
    if (!aIn.readEasings(&mData.easing(0), kComponents)) {
        return aIn.errored("invalid easing param");
    }
    // image id
    {
        auto solver = [=](void* aPtr) {
            this->mData.resource() = ((img::ResourceNode*)aPtr)->handle();
            this->resetTextureCache();
        };
        if (!aIn.orderIDData(solver)) {
            return aIn.errored("invalid image reference id");
        }
    }
    // blend mode
    {
        QString bname;
        aIn.readFixedString(bname, 4);
        auto bmode = img::getBlendModeFromQuadId(bname);
        if (bmode == img::BlendMode_TERM) {
            return aIn.errored("invalid image blending mode");
        }
        mData.setBlendMode(bmode);
    }
    // image offset
    {
        QVector2D offset;
        aIn.read(offset);
        mData.setImageOffset(offset);
    }
    // grid mesh
    if (!mData.gridMesh().deserialize(aIn)) {
        return aIn.errored("failed to deserialize grid mesh");
    }

    aIn.popLogScope();
    return aIn.checkStream();
}

} // namespace core
