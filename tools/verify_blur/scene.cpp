#include "scene.h"

#include <cmath>
#include <cstring>
#include <QImage>

#include "gl/Global.h"
#include "gl/Util.h"
#include "gl/Texture.h"
#include "gl/Framebuffer.h"
#include "core/ObjectTree.h"
#include "core/LayerNode.h"
#include "core/FolderNode.h"
#include "core/BlurKey.h"
#include "core/HsvKey.h"
#include "core/RotateKey.h"
#include "core/ScaleKey.h"
#include "core/ClippingFrame.h"
#include "core/FilterFrame.h"
#include "core/DestinationTexturizer.h"
#include "core/CameraInfo.h"
#include "core/TimeInfo.h"
#include "img/ResourceNode.h"

namespace scene {

namespace {
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
}

Fixture::Fixture() = default;
Fixture::~Fixture() = default;

void Fixture::init(const QSize& aSize) {
    size = aSize;
    clipping.reset(new core::ClippingFrame());
    clipping->resize(aSize);
    filters.reset(new core::FilterFrame());
    filters->resize(aSize);
    texturizer.reset(new core::DestinationTexturizer());
    texturizer->resize(aSize);
    targetTex.reset(new gl::Texture());
    targetTex->create(aSize);
    targetTex->setFilter(GL_LINEAR);
    targetTex->setWrap(GL_CLAMP_TO_EDGE);
    targetFbo.reset(new gl::Framebuffer());
    targetFbo->setColorAttachment(0, targetTex->id());
    XC_ASSERT(targetFbo->isComplete());
}

std::vector<uint8_t> Fixture::render(core::ObjectTree& aTree, const core::CameraInfo& aCamera, int aFrame) {
    auto& ggl = gl::Global::functions();

    // mirror MainDisplayWidget::paintGL
    clipping->clearTexture();
    clipping->resetClippingId();
    texturizer->clearTexture();
    filters->clearAll();

    ggl.glBindFramebuffer(GL_FRAMEBUFFER, targetFbo->id());
    gl::Util::setViewportAsActualPixels(size);
    gl::Util::clearColorBuffer(0.0f, 0.0f, 0.0f, 0.0f);
    gl::Util::resetRenderState();

    core::RenderInfo info;
    info.time = makeTimeInfo(aFrame);
    info.framebuffer = targetFbo->id();
    info.dest = targetTex->id();
    info.isGrid = false;
    info.nonPosed = false;
    info.originMesh = false;
    info.clippingId = 0;
    info.clippingFrame = clipping.data();
    info.destTexturizer = texturizer.data();
    info.filterFrame = filters.data();
    info.opacityScale = 1.0f;
    info.camera = aCamera;

    aTree.render(info, false);

    return readTexture(targetTex->id(), size);
}

std::vector<uint8_t> Fixture::readTexture(GLuint aTex, const QSize& aSize) {
    auto& ggl = gl::Global::functions();
    gl::Framebuffer readFbo;
    readFbo.setColorAttachment(0, aTex);
    XC_ASSERT(readFbo.isComplete());
    readFbo.bind();
    gl::Util::setViewportAsActualPixels(aSize);
    std::vector<uint8_t> out((size_t)aSize.width() * aSize.height() * 4);
    ggl.glPixelStorei(GL_PACK_ALIGNMENT, 4);
    ggl.glReadPixels(0, 0, aSize.width(), aSize.height(), GL_RGBA, GL_UNSIGNED_BYTE, out.data());
    readFbo.release();
    return out;
}

core::TimeInfo makeTimeInfo(int aFrame) {
    core::TimeInfo info;
    info.fps = 24;
    info.frameMax = 200;
    info.loop = false;
    info.frame = core::Frame(aFrame);
    return info;
}

core::CameraInfo makeCamera(const QSize& aSize, float aZoom, float aRotDeg, bool aFlip) {
    core::CameraInfo camera;
    camera.reset(aSize, 1.0, aSize, QPoint(0, 0));
    if (aZoom != 1.0f)
        camera.setScale(aZoom);
    if (aRotDeg != 0.0f)
        camera.setRotate((float)(aRotDeg * kDegToRad));
    camera.setFlip(aFlip);
    return camera;
}

QVector2D screenDirFromWorld(const core::CameraInfo& aCamera, const QVector2D& aWorldDir) {
    const float x = aCamera.flip ? -aWorldDir.x() : aWorldDir.x();
    const float y = aWorldDir.y();
    const float c = std::cos(aCamera.rotate());
    const float s = std::sin(aCamera.rotate());
    return QVector2D(c * x - s * y, s * x + c * y);
}

img::ResourceNode* makeImage(const QString& aId, const QSize& aSize, const std::function<void(int, int, uint8_t*)>& aFill) {
    auto* node = new img::ResourceNode(aId);
    const int w = aSize.width();
    const int h = aSize.height();
    uint8_t* data = new uint8_t[(size_t)w * h * 4]();
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            aFill(x, y, data + ((size_t)y * w + x) * 4);
    // grabImage takes ownership of the block (see img::Buffer::grab)
    node->data().grabImage(XCMemBlock(data, (size_t)w * h * 4), aSize, img::Format_RGBA8);
    return node;
}

core::LayerNode* addLayer(
    core::ObjectTree& aTree, core::ObjectNode* aParent, img::ResourceNode* aRes, const QString& aName,
    const QVector2D& aPos, float aRotDeg, const QVector2D& aScale
) {
    auto* layer = new core::LayerNode(aName, aTree.shaderHolder());
    layer->setDefaultImage(aRes->handle());
    layer->setDefaultPosture(aPos);
    core::getOrCreateDefaultKey<core::RotateKey, core::TimeKeyType_Rotate>(*layer->timeLine())
        ->setRotate((float)(aRotDeg * kDegToRad));
    core::getOrCreateDefaultKey<core::ScaleKey, core::TimeKeyType_Scale>(*layer->timeLine())->setScale(aScale);
    aParent->children().pushBack(layer);
    return layer;
}

core::FolderNode* addFolder(
    core::ObjectNode* aParent, const QString& aName, const QVector2D& aPos, float aRotDeg, const QVector2D& aScale
) {
    auto* folder = new core::FolderNode(aName);
    folder->setDefaultPosture(aPos);
    core::getOrCreateDefaultKey<core::RotateKey, core::TimeKeyType_Rotate>(*folder->timeLine())
        ->setRotate((float)(aRotDeg * kDegToRad));
    core::getOrCreateDefaultKey<core::ScaleKey, core::TimeKeyType_Scale>(*folder->timeLine())->setScale(aScale);
    aParent->children().pushBack(folder);
    return folder;
}

void addBlur(core::ObjectNode* aNode, int aFrame, float aBlurX, float aBlurY, float aAngleDeg) {
    auto* key = new core::BlurKey();
    key->setBlurX(aBlurX);
    key->setBlurY(aBlurY);
    key->setAngleDeg(aAngleDeg);
    key->setDirectional(true);
    cmnd::Base* pusher = aNode->timeLine()->createPusher(core::TimeKeyType_Blur, aFrame, key);
    pusher->tryExec();
    delete pusher;
}

void addBlurAmount(core::ObjectNode* aNode, int aFrame, float aAmount) {
    auto* key = new core::BlurKey();
    key->setAmount(aAmount);
    cmnd::Base* pusher = aNode->timeLine()->createPusher(core::TimeKeyType_Blur, aFrame, key);
    pusher->tryExec();
    delete pusher;
}

void addHSV(core::ObjectNode* aNode, int aFrame, int aHue, int aSat, int aVal, int aAbsolute) {
    auto* key = new core::HSVKey();
    key->setHSV({aHue, aSat, aVal, aAbsolute});
    cmnd::Base* pusher = aNode->timeLine()->createPusher(core::TimeKeyType_HSV, aFrame, key);
    pusher->tryExec();
    delete pusher;
}

Diff diffImages(const std::vector<uint8_t>& aA, const std::vector<uint8_t>& aB) {
    Diff d;
    XC_ASSERT(aA.size() == aB.size());
    long long sum = 0;
    for (size_t i = 0; i < aA.size(); i += 4) {
        int worst = 0;
        for (int c = 0; c < 4; ++c) {
            const int v = std::abs((int)aA[i + c] - (int)aB[i + c]);
            worst = std::max(worst, v);
            sum += v;
            if (v != 0)
                d.bytesIdentical = false;
        }
        d.maxDiff = std::max(d.maxDiff, worst);
        if (worst > 2)
            ++d.over2;
        if (worst > 4)
            ++d.over4;
        ++d.count;
    }
    d.mean = d.count ? (double)sum / (double)(d.count * 4) : 0.0;
    return d;
}

void dumpPNG(const QString& aPath, const std::vector<uint8_t>& aBytes, const QSize& aSize) {
    QImage img(aSize.width(), aSize.height(), QImage::Format_RGBA8888);
    for (int y = 0; y < aSize.height(); ++y) {
        // GL readback row 0 is the bottom row; QImage row 0 is the top
        std::memcpy(img.scanLine(aSize.height() - 1 - y), aBytes.data() + (size_t)y * aSize.width() * 4,
            (size_t)aSize.width() * 4);
    }
    img.save(aPath);
}

} // namespace scene
