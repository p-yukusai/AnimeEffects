#ifndef CORE_IMAGEKEY_H
#define CORE_IMAGEKEY_H

#include <array>
#include "XC.h"
#include "util/Easing.h"
#include "core/KeyEasing.h"
#include "gl/Texture.h"
#include "img/ResourceHandle.h"
#include "img/BlendMode.h"
#include "core/GridMesh.h"
#include "core/TimeKey.h"
#include "gui/obj/obj_Item.h"

namespace core {

class ImageKey: public TimeKey {
public:
    // One easing per graph channel: 0 offset x, 1 offset y. The offset is
    // step-held, so playback does not consult either param; they are stored so
    // the graph's own channel model stays one type-wide rule.
    static constexpr int kComponents = easingComponentCount(TimeKeyType_Image);

    class Data {
        std::array<util::Easing::Param, kComponents> mEasings;
        img::ResourceHandle mResHandle;
        img::BlendMode mBlendMode;
        QVector2D mImageOffset;
        GridMesh mGridMesh;

    public:
        Data();
        Data(const Data& aRhs);
        Data& operator=(const Data& aRhs);
        util::Easing::Param& easing(int aComponent) {
            XC_ASSERT(0 <= aComponent && aComponent < kComponents);
            return mEasings[aComponent];
        }
        const util::Easing::Param& easing(int aComponent) const {
            XC_ASSERT(0 <= aComponent && aComponent < kComponents);
            return mEasings[aComponent];
        }
        img::ResourceHandle& resource() { return mResHandle; }
        const img::ResourceHandle& resource() const { return mResHandle; }
        void setBlendMode(img::BlendMode aMode) { mBlendMode = aMode; }
        img::BlendMode blendMode() const { return mBlendMode; }
        void setImageOffset(const QVector2D& aOffset);
        const QVector2D& imageOffset() const { return mImageOffset; }
        GridMesh& gridMesh() { return mGridMesh; }
        const GridMesh& gridMesh() const { return mGridMesh; }
    };

    class Cache {
        gl::Texture mTexture;

    public:
        Cache();
        gl::Texture& texture() { return mTexture; }
        const gl::Texture& texture() const { return mTexture; }
    };

    enum { kDefaultMeshCellSize = 16 };

    ImageKey();

    Data& data() { return mData; }
    const Data& data() const { return mData; }

    Cache& cache() { return mCache; }
    const Cache& cache() const { return mCache; }

    void setImage(const img::ResourceHandle& aResource, img::BlendMode aMode);
    void setImage(const img::ResourceHandle& aResource);
    void setImageOffset(const QVector2D& aOffset);
    void setImageOffsetByCenter();
    bool hasImage() const { return mData.resource() && mData.resource()->hasImage(); }
    void resetGridMesh(int CellSize = kDefaultMeshCellSize);
    QJsonObject serializeToJson() const;
    bool deserializeFromJson(QJsonObject json, util::LifeLink::Pointee<Project> project);
    virtual TimeKeyType type() const { return TimeKeyType_Image; }
    virtual bool canHoldChild() const { return true; }
    virtual TimeKey* createClone();
    virtual bool serialize(Serializer& aOut) const;
    virtual bool deserialize(Deserializer& aIn);

    virtual void sleep();
    virtual void awake();

private:
    void resetTextureCache();

    Data mData;
    Cache mCache;
    int mSleepCount;
};

} // namespace core

#endif // CORE_IMAGEKEY_H
