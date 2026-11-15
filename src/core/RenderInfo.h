#ifndef CORE_RENDERINFO_H
#define CORE_RENDERINFO_H

#include <QtOpenGL/QtOpenGL>
#include "XC.h"
#include "core/CameraInfo.h"
#include "core/TimeInfo.h"

namespace core {
class MaskKey;
}
namespace core {
class ClippingFrame;
class FilterMask;
class ObjectNode;
}
namespace core {
class DestinationTexturizer;
}
namespace core {
class FilterFrame;
}

namespace core {

class RenderInfo {
public:
    RenderInfo():
        camera(),
        time(),
        framebuffer(),
        dest(0),
        isGrid(false),
        nonPosed(false),
        originMesh(false),
        clippingId(0),
        clippingFrame(),
        destTexturizer(),
        filterFrame(),
        opacityScale(1.0f),
        maskPreview(nullptr),
        maskPreviewKey(nullptr),
        maskPreviewTarget(nullptr),
        maskPreviewColor() {}

    CameraInfo camera;
    TimeInfo time;
    GLuint framebuffer;
    GLuint dest;
    bool isGrid;
    bool nonPosed;
    bool originMesh;
    uint8 clippingId;
    ClippingFrame* clippingFrame;
    DestinationTexturizer* destTexturizer;
    FilterFrame* filterFrame;
    float opacityScale; // 1/ (product of ancestor composite folder opacities)

    // THE MASK PREVIEW while the Mask tool paints: the field being painted, the node
    // it belongs to, and the tint. A node draws it as one extra pass over its content
    // (LayerNode/FolderNode), so the artist sees what is painted where - the field
    // lives in content space, which a screen-space overlay cannot show.
    const FilterMask* maskPreview;
    // The LAYER MASK being painted (core/MaskKey): a key's field, not a per-lane side
    // table, so the preview carries the key itself. Same tint pass, same rule.
    const MaskKey* maskPreviewKey;
    const ObjectNode* maskPreviewTarget;
    QColor maskPreviewColor;
};

} // namespace core

#endif // CORE_RENDERINFO_H
