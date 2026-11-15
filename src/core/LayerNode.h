#ifndef CORE_LAYERNODE_H
#define CORE_LAYERNODE_H

#include <QVector>
#include "img/ResourceHandle.h"
#include "core/ObjectNode.h"
#include "core/Renderer.h"
#include "core/TimeLine.h"
#include "core/BoneInfluenceMap.h"
#include "core/MeshTransformer.h"
#include "core/ShaderHolder.h"

namespace core {

class LayerNode: public ObjectNode, public Renderer {
public:
    LayerNode(const QString& aName, ShaderHolder& aShaderHolder);

    util::LifeLink::Pointee<LayerNode> pointee() { return lifeLink().pointee<LayerNode>(this); }

    // default image
    void setDefaultImage(const img::ResourceHandle& aHandle);
    void setDefaultImage(const img::ResourceHandle& aHandle, img::BlendMode aBlendMode);
    // default posture
    void setDefaultPosture(const QVector2D& aPos);
    // default depth
    void setDefaultDepth(float aValue);
    // default opacity
    void setDefaultOpacity(float aValue);

    // from ObjectNode
    virtual ObjectType type() const { return ObjectType_Layer; }
    virtual void setName(const QString& aName) { mName = aName; }
    virtual const QString& name() const { return mName; }
    virtual float initialDepth() const;
    virtual void setVisibility(bool aIsVisible) { mIsVisible = aIsVisible; }
    virtual bool isVisible() const { return mIsVisible; }
    virtual void setSlimDown(bool aIsSlimmed) { mIsSlimmedDown = aIsSlimmed; }
    virtual bool isSlimmedDown() const { return mIsSlimmedDown; }
    virtual bool canHoldChild() const { return false; }
    virtual void setInitialRect(const QRect& aRect) { mInitialRect = aRect; }
    virtual QRect initialRect() const { return mInitialRect; }
    virtual Renderer* renderer() { return this; }
    virtual const Renderer* renderer() const { return this; }
    virtual TimeLine* timeLine() { return &mTimeLine; }
    virtual const TimeLine* timeLine() const { return &mTimeLine; }
    virtual bool hasAnyMesh() const { return true; }
    virtual bool hasAnyImage() const { return true; }

    virtual cmnd::Vector createResourceUpdater(const ResourceEvent& aEvent);

    void reserveShadersFromTimeline();

    virtual bool serialize(Serializer& aOut) const;
    virtual bool deserialize(Deserializer& aIn);

    // from Renderer
    virtual void prerender(const RenderInfo&, const TimeCacheAccessor&);
    virtual void render(const RenderInfo&, const TimeCacheAccessor&);
    virtual void renderClipper(const RenderInfo&, const TimeCacheAccessor&, uint8 aClipperId);
    virtual void setClipped(bool aIsClipped);
    virtual bool isClipped() const { return mIsClipped; }
    virtual bool hasBlendMode() const { return true; }
    virtual img::BlendMode blendMode() const;
    virtual void setBlendMode(img::BlendMode);
    
    private:
    void transformShape(const RenderInfo& aInfo, const TimeCacheAccessor&);
    void renderLayer(const RenderInfo& aInfo, const TimeCacheAccessor&, bool useHSV, QList<int> HSVData);
    // screen pixels -> this layer's content pixels, the inverse of the transforms it
    // is drawn with. Shared by the direct draw (a layer mask samples in content
    // space) and the composite path's filter fields.
    QMatrix4x4 contentMappingFor(
        const RenderInfo& aInfo, const TimeCacheAccessor& aAccessor, const QSize& aTargetSize
    ) const;
    void renderComposite(const RenderInfo& aInfo, const TimeCacheAccessor&);
    void renderClippees(const RenderInfo& aInfo, const TimeCacheAccessor&);
    bool isClipper() const;
    // an HSV adjustment with a painted amount mask, while its BLENDED value is live
    // (it becomes a stack pass, which is what isolates the layer)
    bool hasActiveHsvMask(const TimeCacheAccessor& aAccessor) const;
    // "an enabled filter whose BLENDED value is above its epsilon" - see
    // core/FilterActivity.h for why the blended value, not the key map, decides
    bool isCompositeLayer(const TimeCacheAccessor& aAccessor) const;
    // this node is the Mask tool's target: it isolates and draws the field preview
    bool isPreviewTarget(const RenderInfo& aInfo) const;
    
    QString mName;
    bool mIsVisible;
    bool mIsSlimmedDown;
    QRect mInitialRect;
    TimeLine mTimeLine;
    ShaderHolder& mShaderHolder;
    bool mIsClipped;

    MeshTransformer mMeshTransformer;
    LayerMesh* mCurrentMesh;
    std::vector<Renderer::SortUnit> mClippees; // a cache for performance
};

} // namespace core

#endif // CORE_LAYERNODE_H
