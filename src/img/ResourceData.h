#ifndef IMG_RESOURCEDATA_H
#define IMG_RESOURCEDATA_H

#include <functional>
#include <QPoint>
#include "img/Buffer.h"
#include "img/BlendMode.h"
namespace img {
class ResourceNode;
}

namespace img {

class ResourceData {
public:
    typedef std::function<bool(ResourceData& aData)> ImageLoader;

    ResourceData(const QString& aIdentifier, const QString& aIdentityId, const ResourceNode* aSerialAddress);
    virtual ~ResourceData() {}

    void grabImage(const XCMemBlock& aBlock, const QSize& aSize, Format aFormat);
    XCMemBlock releaseImage();
    void freeImage();

    void setIdentifier(const QString& aId) { mIdentifier = aId; }
    void setPos(const QPoint& aPos);
    void setIsLayer(bool aIsLayer) { mIsLayer = aIsLayer; }
    void setBlendMode(BlendMode aMode);
    void setIsVisible(bool aIsVisible) { mIsVisible = aIsVisible; }
    void copyFrom(const ResourceData& aData);

    bool isLayer() const { return mIsLayer; }
    bool isVisible() const {return mIsVisible; }
    bool hasImage() const { return mBuffer.data(); }
    const QString& identifier() const { return mIdentifier; }
    const img::Buffer& image() const { return mBuffer; }
    const QPoint& pos() const { return mPos; }
    BlendMode blendMode() const { return mBlendMode; }
    const ResourceNode* serialAddress() const { return mSerialAddress; }

    // The identity this layer is matched by when its source file is reimported.
    // It is Photoshop's stable per-layer id ("lyid"), not the display name: a
    // rename in the source must not orphan the project's edits, and two layers
    // renamed to the same name must not be matched to each other. Writers that
    // carry no stable id (ORA, older PSDs, non-Photoshop writers) have nothing
    // but the name, so this holds the identifier for them -- that fallback is
    // applied here, in the constructor, so every consumer sees one usable value.
    // See ResourceNode's constructor and img::Util::createResourceNodes.
    const QString& identityId() const { return mIdentityId; }

    void setImageLoader(const ImageLoader& aLoader) { mImageLoader = aLoader; }
    bool loadImage();

    QRect rect() const;
    QVector2D center() const;

    bool hasSameLayerDataWith(const ResourceData& aData); // it's heavy

private:
    img::Buffer mBuffer;
    QPoint mPos;
    bool mIsLayer;
    QString mIdentifier;
    QString mIdentityId;
    BlendMode mBlendMode;
    bool mIsVisible;
    ImageLoader mImageLoader;
    const ResourceNode* mSerialAddress;
};

} // namespace img

#endif // IMG_RESOURCEDATA_H
