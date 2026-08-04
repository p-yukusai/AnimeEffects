// Scene/fixture helpers for the directional blur verification harness: an offscreen GL
// render fixture mirroring MainDisplayWidget's per-frame state, plus builders for trees
// with transforms and blur keys.
#ifndef VERIFY_BLUR_SCENE_H
#define VERIFY_BLUR_SCENE_H

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include <QScopedPointer>
#include <QSize>
#include <QString>
#include <QVector2D>
#include <QtOpenGL/QtOpenGL>

namespace core {
class ObjectTree;
class ObjectNode;
class LayerNode;
class FolderNode;
class CameraInfo;
class TimeInfo;
class ClippingFrame;
class FilterFrame;
class DestinationTexturizer;
} // namespace core
namespace gl {
class Texture;
class Framebuffer;
} // namespace gl
namespace img {
class ResourceNode;
}

namespace scene {

// Per-frame render state, mirroring MainDisplayWidget::paintGL's setup. The target is a
// plain RGBA8 FBO (like the app's mFramebuffer); readbacks are GL-ordered (row 0 = bottom).
struct Fixture {
    QSize size;
    QScopedPointer<core::ClippingFrame> clipping;
    QScopedPointer<core::FilterFrame> filters;
    QScopedPointer<core::DestinationTexturizer> texturizer;
    QScopedPointer<gl::Texture> targetTex;
    QScopedPointer<gl::Framebuffer> targetFbo;

    Fixture();
    ~Fixture(); // out-of-line: QScopedPointer members need complete types
    void init(const QSize& aSize);

    // renders the tree at the given frame into the target and reads it back (RGBA8)
    std::vector<uint8_t> render(core::ObjectTree& aTree, const core::CameraInfo& aCamera, int aFrame);

    // reads back any texture (used to inspect FilterFrame slot contents)
    std::vector<uint8_t> readTexture(GLuint aTex, const QSize& aSize);

    core::FilterFrame& filterFrame() { return *filters; }
};

core::TimeInfo makeTimeInfo(int aFrame);
core::CameraInfo makeCamera(const QSize& aSize, float aZoom = 1.0f, float aRotDeg = 0.0f, bool aFlip = false);

// maps a world-space direction into the screen space the composite slot renders in:
// the view's linear part is R(rotate) * diag(flip ? -1 : 1, 1) (zoom is isotropic, so a
// unit direction stays unit)
QVector2D screenDirFromWorld(const core::CameraInfo& aCamera, const QVector2D& aWorldDir);

// creates an RGBA8 image resource (caller keeps ownership and must outlive the layers)
img::ResourceNode* makeImage(const QString& aId, const QSize& aSize, const std::function<void(int, int, uint8_t*)>& aFill);

// adds a layer; pos/rotDeg/scale go into the default SRT keys (rotDeg is degrees)
core::LayerNode* addLayer(
    core::ObjectTree& aTree, core::ObjectNode* aParent, img::ResourceNode* aRes, const QString& aName,
    const QVector2D& aPos, float aRotDeg, const QVector2D& aScale
);

core::FolderNode* addFolder(
    core::ObjectNode* aParent, const QString& aName, const QVector2D& aPos, float aRotDeg, const QVector2D& aScale
);

// directional blur key at a frame
void addBlur(core::ObjectNode* aNode, int aFrame, float aBlurX, float aBlurY, float aAngleDeg);
// legacy isotropic path (BlurKey::setAmount; directional flag off)
void addBlurAmount(core::ObjectNode* aNode, int aFrame, float aAmount);
// HSV adjust key at a frame (hue in degrees, sat/val in percent, absolute = hue-set flag)
void addHSV(core::ObjectNode* aNode, int aFrame, int aHue, int aSat, int aVal, int aAbsolute);

// byte-diff stats between two same-sized readbacks
struct Diff {
    int maxDiff = 0;
    double mean = 0.0;
    int over2 = 0; // pixels with any channel off by > 2
    int over4 = 0;
    int count = 0; // pixels compared
    bool bytesIdentical = true;
};
Diff diffImages(const std::vector<uint8_t>& aA, const std::vector<uint8_t>& aB);

// debug helper: saves a GL-ordered readback as a PNG (flipped to viewing orientation)
void dumpPNG(const QString& aPath, const std::vector<uint8_t>& aBytes, const QSize& aSize);

} // namespace scene

#endif // VERIFY_BLUR_SCENE_H
