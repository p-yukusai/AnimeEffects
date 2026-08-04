#ifndef CORE_OBJECTNODEUTIL_H
#define CORE_OBJECTNODEUTIL_H

#include <vector>
#include <QRectF>
#include <QMatrix4x4>
#include "cmnd/Listener.h"
#include "core/ObjectNode.h"
#include "core/TimeCacheAccessor.h"

namespace core {

namespace ObjectNodeUtil {
    bool writeObjectBlock(
        Serializer& out,
        const std::array<uint8, 8>& sig,
        const QString& name,
        bool visible,
        bool slim,
        const QRect& rect,
        bool clipped,
        const TimeLine& tl
    );

    bool readObjectBlock(
        Deserializer& in,
        const char* expectedSig,
        QString& name,
        bool& visible,
        bool& slim,
        QRect& rect,
        bool& clipped,
        TimeLine& tl
    );

    bool isClipper(const ObjectNode* self);

    void renderClippees(
        ObjectNode& self,
        std::vector<Renderer::SortUnit>& clippees,
        const RenderInfo& info,
        const TimeCacheAccessor& acc,
        std::function<void(const RenderInfo&, const TimeCacheAccessor&, uint8)> clipperFunc
    );

    float getInitialWorldDepth(ObjectNode& aNode);

    bool thereAreSomeKeysExceedingFrame(const ObjectNode* aRootNode, int aMaxFrame);

    void collectRenderClippees(
        ObjectNode& aNode, std::vector<Renderer::SortUnit>& aDest, const TimeCacheAccessor& aAccessor,
        const TimeInfo& aTime
    );

    // collects renderable units of a subtree; folders with an active composite filter
    // are pushed as single units (their subtree is rendered by the folder itself)
    void collectRenderUnits(
        ObjectNode& aNode, bool aPush, std::vector<Renderer::SortUnit>& aDest, const TimeCacheAccessor& aAccessor,
        const TimeInfo& aTime
    );

    class AttributeNotifier: public cmnd::Listener {
        Project& mProject;
        ObjectNode& mTarget;

    public:
        AttributeNotifier(Project& aProject, ObjectNode& aTarget);
        virtual void onExecuted();
        virtual void onUndone();
        virtual void onRedone();
    };

} // namespace ObjectNodeUtil

} // namespace core

#endif // CORE_OBJECTNODEUTIL_H
