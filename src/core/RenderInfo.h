#ifndef CORE_RENDERINFO_H
#define CORE_RENDERINFO_H

#include <QtOpenGL/QtOpenGL>
#include "XC.h"
#include "core/CameraInfo.h"
#include "core/TimeInfo.h"
namespace core {
class ClippingFrame;
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
        opacityScale(1.0f) {}

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
};

} // namespace core

#endif // CORE_RENDERINFO_H
