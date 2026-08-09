#include <algorithm>
#include <cmath>
#include "gl/Global.h"
#include "gl/Util.h"
#include "core/FolderNode.h"
#include "core/ObjectNodeUtil.h"
#include "core/TimeKeyExpans.h"
#include "core/DepthKey.h"
#include "core/FilterFrame.h"
#include "core/WorldBlurMath.h"
#include "core/DestinationTexturizer.h"

namespace {

// An HSV key is a no-op when it does not change the pixel color: additive hue 0, sat/val
// at 100, and not absolute. Such a key must not force the folder onto the composite path
// (does not gate on the presentation-applied HSV).
bool hsvDataIsIdentity(const QList<int>& aHSV) {
    return aHSV.size() < 4 || (aHSV[0] == 0 && aHSV[1] == 100 && aHSV[2] == 100 && aHSV[3] == 0);
}

} // namespace

namespace core {

FolderNode::FolderNode(const QString& aName):
    mName(aName),
    mIsVisible(true),
    mIsSlimmedDown(),
    mInitialRect(),
    mHeightMap(),
    mTimeLine(),
    mIsClipped(),
    mBlendMode(img::BlendMode_Normal),
    mClippees() {}

FolderNode::~FolderNode() { qDeleteAll(children()); }

void FolderNode::setDefaultPosture(const QVector2D& aPos) {
    getOrCreateDefaultKey<MoveKey, TimeKeyType_Move>(mTimeLine)->data().setPos(aPos);
    getOrCreateDefaultKey<RotateKey, TimeKeyType_Rotate>(mTimeLine);
    getOrCreateDefaultKey<ScaleKey, TimeKeyType_Scale>(mTimeLine);
}

void FolderNode::setDefaultDepth(float aValue) {
    getOrCreateDefaultKey<DepthKey, TimeKeyType_Depth>(mTimeLine)->setDepth(aValue);
}

void FolderNode::setDefaultOpacity(float aValue) {
    getOrCreateDefaultKey<OpaKey, TimeKeyType_Opa>(mTimeLine)->setOpacity(aValue);
}

void FolderNode::grabHeightMap(HeightMap* aNode) { mHeightMap.reset(aNode); }

bool FolderNode::isClipper() const { return ObjectNodeUtil::isClipper(this); }

bool FolderNode::hasActiveHSVKey(const TimeInfo& aTime) const {
    return !mTimeLine.isEmpty(TimeKeyType_HSV)
        && aTime.frame.get() >= mTimeLine.map(TimeKeyType_HSV).values().first()->frame();
}

bool FolderNode::hasActiveBlurKey(const TimeInfo& aTime) const {
    return !mTimeLine.isEmpty(TimeKeyType_Blur)
        && aTime.frame.get() >= mTimeLine.map(TimeKeyType_Blur).values().first()->frame();
}

bool FolderNode::isCompositeFolder(const TimeInfo& aTime, const TimeCacheAccessor& aAccessor) const {
    // a BlurKey whose blended radius is at or below the epsilon has no visible effect
    // (the shader's sigma floor renders such radii as an identity kernel), so it must
    // not force the folder onto the composite path; the epsilon keeps interpolations
    // ramping in smoothly from zero. An HSV key that blends to identity (no-op adjust)
    // is gated the same way. A non-Normal blend mode always takes the composite path
    // (the subtree is grouped, then blended against the scene with the folder's mode).
    return mBlendMode != img::BlendMode_Normal
        || (hasActiveHSVKey(aTime) && !hsvDataIsIdentity(aAccessor.get(mTimeLine).hsv().hsv()))
        || (hasActiveBlurKey(aTime)
            && aAccessor.get(mTimeLine).maxBlurRadius() > FilterFrame::kMinActiveBlurRadius);
}

void FolderNode::prerender(const RenderInfo&, const TimeCacheAccessor&) {}

void FolderNode::render(const RenderInfo& aInfo, const TimeCacheAccessor& aAccessor) {
    if (!mIsVisible || aAccessor.get(mTimeLine).opa().isZero() || aInfo.isGrid) return;

    // render clippees
    renderClippees(aInfo, aAccessor);

    // a folder with a filter renders its subtree as a composite and applies the
    // filter to the blended result
    if (isCompositeFolder(aInfo.time, aAccessor)) {
        renderComposite(aInfo, aAccessor);
    }
}

void FolderNode::renderComposite(const RenderInfo& aInfo, const TimeCacheAccessor& aAccessor) {
    XC_PTR_ASSERT(aInfo.filterFrame);
    auto& frame = *aInfo.filterFrame;

    // 1. render the subtree into a composite frame slot at folder-relative opacity
    FilterFrame::ScopedSlot composite(frame);
    frame.bind(composite.slot());
    gl::Util::setViewportAsActualPixels(frame.size());
    gl::Util::resetRenderState();
    gl::Global::functions().glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl::Global::functions().glClear(GL_COLOR_BUFFER_BIT);

    const float ownOpacity = aAccessor.get(mTimeLine).opa().opacity();

    {
        std::vector<Renderer::SortUnit> units;
        for (auto child : children()) {
            ObjectNodeUtil::collectRenderUnits(*child, true, units, aAccessor, aInfo.time);
        }
        std::stable_sort(
            units.begin(), units.end(), [](const Renderer::SortUnit& a, const Renderer::SortUnit& b) {
                return a.depth < b.depth;
            }
        );

        for (auto& unit : units) {
            RenderInfo childInfo = aInfo;
            childInfo.framebuffer = composite.framebuffer();
            childInfo.dest = composite.texture();
            childInfo.opacityScale = aInfo.opacityScale / ownOpacity;
            unit.renderer->render(childInfo, aAccessor);
        }
    }

    // 2. apply filters to the composite and draw the result into the target framebuffer
    GLuint srcTexture = composite.texture();
    auto& expans = aAccessor.get(mTimeLine);
    if (expans.maxBlurRadius() > FilterFrame::kMinActiveBlurRadius && hasActiveBlurKey(aInfo.time)) {
        // The blur is defined in the folder's content pixels (a content-space ellipse when
        // the blur is directional). The composite is in project space (children render
        // with their world transforms), so the content ellipse maps to an ellipse in the
        // composite: the separable passes run along its principal axes (the singular
        // vectors of the accumulated world transform M, composed with the content ellipse
        // as M*E), with the resulting singular values as radii scaled by the camera zoom
        // (zoom-independent preview, blur scales with the folder).
        const WorldBlurEllipse ellipse = worldBlurEllipse(
            aAccessor, this, expans.blurX(), expans.blurY(), expans.angleDeg());
        const float zoom = aInfo.camera.scale();
        const float radiusX = ellipse.majorRadius * zoom;
        const float radiusY = ellipse.minorRadius * zoom;

        // map the world-space ellipse directions into the composite slot's screen space:
        // the slot holds the scene as rendered through the camera, whose view linear part
        // is R(rotate) * diag(flip ? -1 : 1, 1) (zoom is isotropic, directions stay unit)
        const float flipX = aInfo.camera.flip ? -1.0f : 1.0f;
        const float cr = std::cos(aInfo.camera.rotate());
        const float sr = std::sin(aInfo.camera.rotate());
        const auto screenDir = [=](const QVector2D& d) {
            const float x = flipX * d.x();
            const float y = d.y();
            return QVector2D(cr * x - sr * y, sr * x + cr * y);
        };
        const QVector2D majorScreen = screenDir(ellipse.majorDir);
        const QVector2D minorScreen = screenDir(ellipse.minorDir);

        if (FilterFrame::blurLadderLevel(radiusX, radiusY, frame.size()) > 0) {
            // large radius: downsample ladder (bounded taps, then progressive upsampling)
            srcTexture = frame.blurApply(
                composite.slot(), majorScreen, radiusX, minorScreen, radiusY);
        } else {
            // the composite slot holds the screen image y-flipped (texture row = H - screen
            // y), so a screen-space direction (dx, dy) appears in slot space as (dx, -dy);
            // the ladder path needs no adjustment (its first downsample flips back)
            FilterFrame::BlurParams h;
            h.direction = QVector2D(majorScreen.x(), -majorScreen.y());
            h.radiusTexels = radiusX;
            FilterFrame::BlurParams v;
            v.direction = QVector2D(minorScreen.x(), -minorScreen.y());
            v.radiusTexels = radiusY;

            FilterFrame::ScopedSlot hSlot(frame);
            frame.bind(hSlot.slot());
            gl::Util::setViewportAsActualPixels(frame.size());
            gl::Util::resetRenderState();
            gl::Global::functions().glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            gl::Global::functions().glClear(GL_COLOR_BUFFER_BIT);
            frame.drawQuad(srcTexture, FilterFrame::Kind_Blur, QList<int>(), h, 1.0f);

            // the vertical pass writes back into the composite slot (the same reuse the
            // ladder path applies on its final upsample), so the direct blur holds one
            // scratch slot instead of two
            frame.bind(composite.slot());
            gl::Util::setViewportAsActualPixels(frame.size());
            gl::Util::resetRenderState();
            gl::Global::functions().glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            gl::Global::functions().glClear(GL_COLOR_BUFFER_BIT);
            frame.drawQuad(hSlot.texture(), FilterFrame::Kind_Blur, QList<int>(), v, 1.0f);

            srcTexture = composite.texture();
        }
    }

    gl::Global::functions().glBindFramebuffer(GL_FRAMEBUFFER, aInfo.framebuffer);
    const bool needHSV = hasActiveHSVKey(aInfo.time)
        && !hsvDataIsIdentity(aAccessor.get(mTimeLine).hsv().hsv());
    // a non-Normal folder blend mode samples the captured scene behind the folder
    // (DestinationTexturizer capture), so the blend sees the real background instead
    // of the composite itself; the opacity rides uColor.a exactly like the Normal
    // present (the folder's own opacity is applied here, ancestors' at their presents)
    if (mBlendMode != img::BlendMode_Normal) {
        XC_PTR_ASSERT(aInfo.destTexturizer);
        aInfo.destTexturizer->updateAll(aInfo.framebuffer, aInfo.dest);
        frame.drawQuad(
            srcTexture, FilterFrame::Kind_HSV, aAccessor.get(mTimeLine).hsv().hsv(), FilterFrame::BlurParams(),
            ownOpacity, needHSV, QSizeF(), QSizeF(), false, mBlendMode, aInfo.destTexturizer->texture().id());
    } else {
        frame.drawQuad(srcTexture, FilterFrame::Kind_HSV, aAccessor.get(mTimeLine).hsv().hsv(), FilterFrame::BlurParams(),
            ownOpacity, needHSV);
    }
}

void FolderNode::renderClippees(const RenderInfo& i, const TimeCacheAccessor& a) {
    ObjectNodeUtil::renderClippees(*this, mClippees, i, a,
        [this](const auto& inf, const auto& acc, uint8 id){ renderClipper(inf, acc, id); });
}

void FolderNode::renderClipper(const RenderInfo& aInfo, const TimeCacheAccessor& aAccessor, uint8 aClipperId) {
    // A clipped child contributes only its CLIPPED alpha, which is a subset of its
    // clip base's alpha, and the base writes its own alpha into this texture: skipping
    // clipped children keeps the union exactly the folder's composite alpha. Writing
    // their RAW bitmap alpha instead would leak content outside the clip region into
    // the folder's clip mask (visible when layers above the folder clip against it).
    for (auto child : this->children()) {
        if (child->renderer() && !child->renderer()->isClipped()) {
            child->renderer()->renderClipper(aInfo, aAccessor, aClipperId);
        }
    }
}

float FolderNode::initialDepth() const {
    auto key = (DepthKey*)mTimeLine.defaultKey(TimeKeyType_Depth);
    return key ? key->depth() : 0.0f;
}

void FolderNode::setClipped(bool aIsClipped) { mIsClipped = aIsClipped; }

bool FolderNode::serialize(Serializer& aOut) const {
    static const std::array<uint8, 8> sig = {'F', 'o', 'l', 'd', 'e', 'r', 'N', 'd'};
    if (!ObjectNodeUtil::writeObjectBlock(aOut, sig, mName, mIsVisible, mIsSlimmedDown,
            mInitialRect, mIsClipped, mTimeLine)) {
        return false;
    }
    // the folder blend mode landed in AE_PROJECT_FORMAT_MINOR_VERSION 10; the 4CC layout
    // mirrors ImageKey/ResourceHolder so a folder mode survives the binary round-trip
    aOut.writeFixedString(img::getQuadIdFromBlendMode(mBlendMode), 4);
    return aOut.checkStream();
}

bool FolderNode::deserialize(Deserializer& aIn) {
    if (!ObjectNodeUtil::readObjectBlock(aIn, "FolderNd", mName, mIsVisible, mIsSlimmedDown,
            mInitialRect, mIsClipped, mTimeLine)) {
        return false;
    }
    // projects saved before the blend-mode bump (minor < 10) have no mode in the block;
    // the folder keeps its pass-through default
    if (aIn.version().minorVersion() >= 10) {
        QString bname;
        aIn.readFixedString(bname, 4);
        auto bmode = img::getBlendModeFromQuadId(bname);
        if (bmode == img::BlendMode_TERM) {
            return aIn.errored("invalid folder blending mode");
        }
        mBlendMode = bmode;
    }
    return aIn.checkStream();
}

} // namespace core
