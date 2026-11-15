#include <QMatrix4x4>
#include <QOpenGLFunctions>
#include <QOpenGLContext>
#include <cmath>
#include "gl/FrameStats.h"
#include "gl/Global.h"
#include "gl/Util.h"
#include "img/BlendMode.h"
#include "core/LayerNode.h"
#include "core/ObjectNodeUtil.h"
#include "core/TimeKeyExpans.h"
#include "core/DepthKey.h"
#include "core/ResourceEvent.h"
#include "core/ResourceUpdatingWorkspace.h"
#include "core/FFDKeyUpdater.h"
#include "core/ImageKeyUpdater.h"
#include "core/ClippingFrame.h"
#include "core/WorldBlurMath.h"
#include "core/DestinationTexturizer.h"
#include "core/FilterActivity.h"
#include "core/FilterFrame.h"
#include "core/WavePass.h"

namespace core {

LayerNode::LayerNode(const QString& aName, ShaderHolder& aShaderHolder):
    mName(aName),
    mIsVisible(true),
    mIsSlimmedDown(),
    mInitialRect(),
    mTimeLine(),
    mShaderHolder(aShaderHolder),
    mIsClipped(),
    mMeshTransformer(aShaderHolder.meshTransformerResource()),
    mCurrentMesh(),
    mTransformKey(),
    mTransformKeyValid(),
    mClippees() {}

void LayerNode::setDefaultImage(const img::ResourceHandle& aHandle) {
    setDefaultImage(aHandle, aHandle->blendMode());
}

void LayerNode::setDefaultImage(const img::ResourceHandle& aHandle, img::BlendMode aBlendMode) {
    /*XC_ASSERT(aHandle);
    XC_PTR_ASSERT(aHandle->image().data());
    XC_ASSERT(aHandle->image().pixelSize().isValid());*/
    if (!aHandle || !aHandle->image().data() || !aHandle->image().pixelSize().isValid())
        return; // fail safe

    auto key = new ImageKey();
    mTimeLine.grabDefaultKey(TimeKeyType_Image, key);
    key->setImage(aHandle, aBlendMode);
    key->resetGridMesh();
    key->setImageOffsetByCenter();

    mShaderHolder.reserveShaders(aBlendMode);
    mShaderHolder.reserveGridShader();
    mShaderHolder.reserveClipperShaders();
}

void LayerNode::setDefaultPosture(const QVector2D& aPos) {
    getOrCreateDefaultKey<MoveKey, TimeKeyType_Move>(mTimeLine)->data().setPos(aPos);
    getOrCreateDefaultKey<RotateKey, TimeKeyType_Rotate>(mTimeLine);
    getOrCreateDefaultKey<ScaleKey, TimeKeyType_Scale>(mTimeLine);
}

void LayerNode::setDefaultDepth(float aValue) {
    getOrCreateDefaultKey<DepthKey, TimeKeyType_Depth>(mTimeLine)->setDepth(aValue);
}

void LayerNode::setDefaultOpacity(float aValue) {
    getOrCreateDefaultKey<OpaKey, TimeKeyType_Opa>(mTimeLine)->setOpacity(aValue);
}

float LayerNode::initialDepth() const {
    auto key = (DepthKey*)mTimeLine.defaultKey(TimeKeyType_Depth);
    return key ? key->depth() : 0.0f;
}

void LayerNode::setClipped(bool aIsClipped) { mIsClipped = aIsClipped; }

bool LayerNode::isClipper()  const { return ObjectNodeUtil::isClipper(this); }

img::BlendMode LayerNode::blendMode() const {
    auto key = (ImageKey*)mTimeLine.defaultKey(TimeKeyType_Image);
    return key ? key->data().blendMode() : img::BlendMode_Normal;
}

void LayerNode::setBlendMode(img::BlendMode aMode) {
    auto key = (ImageKey*)mTimeLine.defaultKey(TimeKeyType_Image);
    if (key) {
        key->data().setBlendMode(aMode);
        mShaderHolder.reserveShaders(aMode);
    }
}

void LayerNode::prerender(const RenderInfo& aInfo, const TimeCacheAccessor& aAccessor) {
    // HIDDEN nodes draw nothing (renderLayer/renderComposite guard on !mCurrentMesh),
    // and the cache may not survive a later show: it would be skipped for inputs that
    // did not pass through a transformShape run.
    if (!mIsVisible) {
        mCurrentMesh = nullptr;
        mTransformKeyValid = false;
        return;
    }

    // transform shape by current keys (transformShape skips the GL pass when the
    // key says its output buffers already hold this exact result)
    transformShape(aInfo, aAccessor);
}


namespace {
// does this node's blended state deform its geometry at all? (mesh/FFD keys, or a bone binding)
bool layersDeformationActive(TimeKeyExpans& aExpans) {
    // A BONE BINDING deforms the draw, and so does an FFD/mesh KEY - the mesh the layer draws with
    // is `ffdMesh()` and its DEFORMATION is `ffd()`'s positions, so a mesh that merely exists (the
    // base grid) is not a deformation.
    if (aExpans.bone().isAffectedByBinding())
        return true;
    return aExpans.ffd().count() > 0;
}

// ONE half-plane clip of a convex polygon (Sutherland-Hodgman): keep the points on the
// `aKeepGreater` side of the axis-aligned line (`aAxis` 0 = x, 1 = y) `aBound`.
std::vector<QVector2D> clipToHalfPlane(
    const std::vector<QVector2D>& aPolygon, int aAxis, float aBound, bool aKeepGreater
) {
    std::vector<QVector2D> out;
    const int count = int(aPolygon.size());
    for (int i = 0; i < count; ++i) {
        const QVector2D& current = aPolygon[i];
        const QVector2D& previous = aPolygon[(i + count - 1) % count];
        const auto coordinate = [aAxis](const QVector2D& aPoint) {
            return aAxis == 0 ? aPoint.x() : aPoint.y();
        };
        const float currentValue = coordinate(current);
        const float previousValue = coordinate(previous);
        const bool currentInside = aKeepGreater ? currentValue >= aBound : currentValue <= aBound;
        const bool previousInside = aKeepGreater ? previousValue >= aBound : previousValue <= aBound;
        // the edge crosses the line: keep its intersection on the inside
        if (currentInside != previousInside) {
            const float delta = currentValue - previousValue;
            const float t = delta != 0.0f ? (aBound - previousValue) / delta : 0.0f;
            out.push_back(previous + (current - previous) * t);
        }
        if (currentInside)
            out.push_back(current);
    }
    return out;
}

double polygonAreaPx(const std::vector<QVector2D>& aPolygon) {
    const int count = int(aPolygon.size());
    double twiceArea = 0.0;
    for (int i = 0; i < count; ++i) {
        const QVector2D& a = aPolygon[i];
        const QVector2D& b = aPolygon[(i + 1) % count];
        twiceArea += double(a.x()) * double(b.y()) - double(b.x()) * double(a.y());
    }
    return std::abs(twiceArea) * 0.5;
}

// THE FILL ONE LAYER ASKS FOR, in screen pixels: the node's content rectangle (the
// image's pixel box, the space the mesh texcoords span) mapped by EXACTLY the matrices
// the layer draw maps it with (`contentMappingFor` inverts the same forward mapping),
// then clipped to the viewport. Only reached with the counter enabled.
double screenAreaPx(const RenderInfo& aInfo, const TimeKeyExpans& aExpans, const QSize& aContentSize) {
    if (aContentSize.isEmpty())
        return 0.0;
    const QSize viewSize = aInfo.camera.screenSize();
    if (viewSize.width() <= 0 || viewSize.height() <= 0)
        return 0.0;

    QMatrix4x4 world = aExpans.srt().worldCSRTMatrix();
    if (!aInfo.nonPosed && aExpans.bone().isAffectedByBinding())
        world = aExpans.bone().outerMatrix();
    const QMatrix4x4 contentToScreen =
        waveContentToScreen(aInfo.camera, QSizeF(viewSize), world, aExpans.imageOffset());

    const float width = float(aContentSize.width());
    const float height = float(aContentSize.height());
    std::vector<QVector2D> quad{
        contentToScreen.map(QVector3D(0.0f, 0.0f, 0.0f)).toVector2D(),
        contentToScreen.map(QVector3D(width, 0.0f, 0.0f)).toVector2D(),
        contentToScreen.map(QVector3D(width, height, 0.0f)).toVector2D(),
        contentToScreen.map(QVector3D(0.0f, height, 0.0f)).toVector2D()
    };

    // the viewport rect (0, 0, camera.screenSize()): four half planes, in order
    const float maxX = float(viewSize.width());
    const float maxY = float(viewSize.height());
    quad = clipToHalfPlane(quad, 0, 0.0f, true);
    quad = clipToHalfPlane(quad, 0, maxX, false);
    quad = clipToHalfPlane(quad, 1, 0.0f, true);
    quad = clipToHalfPlane(quad, 1, maxY, false);
    return polygonAreaPx(quad);
}
} // namespace

void LayerNode::render(const RenderInfo& aInfo, const TimeCacheAccessor& aAccessor) {
    // AE_FRAME_STATS: ONE `considered` per layer, no matter how often this function is
    // entered for it. The mask-preview recursion below re-enters with `originMesh` for
    // the SAME node, and the grid pass draws the node a second time - neither is
    // another layer, so neither counts again. With the counter off this is one branch.
    gl::FrameStats& frameStats = gl::FrameStats::instance();
    const bool frameStatsOn = frameStats.enabled();
    bool countsLayer = false;
    if (frameStatsOn) {
        const bool previewRecursion = aInfo.originMesh && aInfo.maskPreviewTarget == this
            && aInfo.maskPreviewKey != nullptr;
        countsLayer = !aInfo.isGrid && !previewRecursion;
        if (countsLayer)
            frameStats.layerConsidered();
    }

    if (!mIsVisible || aAccessor.get(mTimeLine).opa().isZero()) {
        if (countsLayer)
            frameStats.layerCulled();
        return;
    }

    // THE MASK TOOL PAINTS AN UNDISTORTED LAYER, and that is a RULE, not a shortcut. A matte
    // lives in the layer's CONTENT space, and the mask pass samples it through the node's SRT -
    // a matrix. A mesh/FFD/bone deformation is NOT a matrix, so with it in the draw the pixels on
    // screen come from content points the matte pass cannot name, and the stroke, its rubylith
    // and the masked result end up in three different places (reported: "mask painting paints on
    // the untransformed mesh ... what you see is not what you get"). Drawing the node's ORIGINAL
    // mesh while its matte is painted (originMesh: the grid without its deformation) makes the
    // three one mapping. The alternatives are a per-pixel mesh INVERSE in both the paint path and
    // the mask pass, or limiting a mask to the deformed silhouette - the first is a pipeline
    // change, the second takes away exactly what a matte is for.
    // ...and `!aInfo.originMesh` is what makes it happen ONCE: the recursive call below still
    // carries the preview target, so testing only that recursed forever (a crash on the first
    // stroke of a matte on a deformed layer).
    if (!aInfo.originMesh && aInfo.maskPreviewTarget == this && aInfo.maskPreviewKey != nullptr
        && layersDeformationActive(const_cast<TimeKeyExpans&>(aAccessor.get(mTimeLine)))) {
        RenderInfo undistorted(aInfo);
        undistorted.originMesh = true;
        render(undistorted, aAccessor);
        return;
    }

    // The direct path's HSV comes from the BLENDED value, like every other filter
    // decision: an HSV edited on its default key must bake into the layer shader
    // even though the lane has no explicit key at all.
    const bool useHSV = mTimeLine.filterEnabled(TimeKeyType_HSV)
        && filterValueIsActive(TimeKeyType_HSV, aAccessor.get(mTimeLine));
    const QList<int> hsvData = useHSV ? aAccessor.get(mTimeLine).hsv().hsv() : QList<int>{};

    // AE_FRAME_STATS: the node ISSUES its draw here, on one of the two paths below. A
    // path that will bail (no mesh, no image texture) draws nothing and is not counted;
    // the grid pass is left out like it already is for `countsLayer`.
    const auto countDrawnLayer = [&]() {
        if (!frameStatsOn || aInfo.isGrid || !mCurrentMesh)
            return;
        const TimeKeyExpans& expans = aAccessor.get(mTimeLine);
        const gl::Texture* texture = expans.areaTexture();
        if (!texture)
            return;
        frameStats.layerDrawn(screenAreaPx(aInfo, expans, texture->size()));
    };

    if (!aInfo.isGrid && (isCompositeLayer(aAccessor) || isPreviewTarget(aInfo))) {
        countDrawnLayer();
        renderComposite(aInfo, aAccessor);
    } else {
        countDrawnLayer();
        renderLayer(aInfo, aAccessor, useHSV, hsvData);
    }

    if (aInfo.isGrid) return;

    renderClippees(aInfo, aAccessor);
}

bool LayerNode::hasActiveHsvMask(const TimeCacheAccessor& aAccessor) const {
    // An HSV whose BLENDED value changes color AND a painted amount mask: the
    // adjustment then has to become a stack pass (per pixel), which is what
    // isolates the layer. Both halves are read from the same value the direct
    // path bakes, so a static HSV with a mask isolates too.
    if (!filterValueIsActive(TimeKeyType_HSV, aAccessor.get(mTimeLine)))
        return false;
    const FilterMask* mask = mTimeLine.filterMask(TimeKeyType_HSV, FilterMaskChannel_Amount);
    return mask && !mask->isEmpty();
}

bool LayerNode::isPreviewTarget(const RenderInfo& aInfo) const {
    // the Mask tool paints THIS node: isolate it so the preview can be drawn over its
    // content, whatever the filter's own enabled state says. BOTH kinds count - a
    // filter input AND the node's own matte - because the isolation is what gives the
    // overlay a surface to draw on (a matte preview on a plain layer stayed on the
    // DIRECT path while this tested only maskPreview, so the rubylith never appeared).
    return aInfo.maskPreviewTarget == this
        && (aInfo.maskPreview != nullptr || aInfo.maskPreviewKey != nullptr);
}

bool LayerNode::isCompositeLayer(const TimeCacheAccessor& aAccessor) const {
    // Only an ENABLED filter whose BLENDED value is above its epsilon isolates the
    // layer (filterValueIsActive carries each type's test: HSV's inert Data,
    // FilterFrame::kMinActiveBlurRadius, WaveKey::Data::isActive). Testing the
    // blended value - not "some key exists at or before this frame" - is what keeps
    // a filter edited on its default key from being dropped here. The composite
    // path then applies the whole stack in order; HSV alone stays on the direct
    // path, where it is baked into the layer shader exactly as before.
    const TimeKeyExpans& expans = aAccessor.get(mTimeLine);
    if (mTimeLine.filterEnabled(TimeKeyType_Blur)
        && filterValueIsActive(TimeKeyType_Blur, expans))
        return true;
    if (mTimeLine.filterEnabled(TimeKeyType_Wave)
        && filterValueIsActive(TimeKeyType_Wave, expans))
        return true;
    return mTimeLine.filterEnabled(TimeKeyType_HSV) && hasActiveHsvMask(aAccessor);
}

// A FILTERED layer is isolated: the layer draws into a composite slot (its own HSV,
// blend mode and clipping preserved), the stack is applied to that isolated content in
// order, and the result is presented onto the scene with the layer's blend mode at the
// layer's accumulated opacity.
void LayerNode::renderComposite(const RenderInfo& aInfo, const TimeCacheAccessor& aAccessor) {
    XC_PTR_ASSERT(aInfo.filterFrame);
    auto& frame = *aInfo.filterFrame;
    if (!mCurrentMesh)
        return;

    auto& expans = aAccessor.get(mTimeLine);
    if (!expans.areaImageKey() || !expans.areaTexture())
        return;

    const float worldOpacity = expans.worldOpacity();
    if (worldOpacity <= 0.0f)
        return;

    // 1. render the layer WITHOUT its HSV into a composite slot at full opacity: the
    // stack applies HSV as an explicit pass, so the order (HSV before blur or after)
    // is the user's. The direct (no blur) path still bakes HSV for efficiency.
    const QList<int> hsvData;

    // 1. render the layer into a composite slot at full opacity (its opacity is applied
    // at presentation so the blur samples the crisp shape, not a faded one)
    FilterFrame::ScopedSlot composite(frame);
    frame.bind(composite.slot());
    gl::Util::setViewportAsActualPixels(frame.size());
    gl::Util::resetRenderState();
    gl::Global::functions().glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl::Global::functions().glClear(GL_COLOR_BUFFER_BIT);

    {
        RenderInfo childInfo = aInfo;
        childInfo.framebuffer = composite.framebuffer();
        childInfo.dest = composite.texture();
        childInfo.opacityScale = 1.0f / worldOpacity;
        renderLayer(childInfo, aAccessor, false, hsvData);
    }

    // 2. apply the stack IN ORDER to the isolated content. Blur reuses the current
    // slot (its direct path is a two-pass in-place filter); HSV takes a scratch slot
    // because it samples the source while drawing.
    GLuint srcTexture = composite.texture();
    int srcSlot = composite.slot();
    const QVector<FilterEntry> stack = mTimeLine.filters();
    std::vector<std::unique_ptr<FilterFrame::ScopedSlot>> scratchSlots;

    // The wave's field lives in the layer's own image pixels, so its mapping is
    // built once from the matrices the layer was DRAWN with: the world transform
    // (bone-aware, like MeshTransformer's choice) times the camera's view, plus
    // the image offset that turns an ImageKey texcoord into content space. The
    // sample may not leave the image rectangle - that clamp is the
    // texture-space clamp an image effect has, and it is what stops a displaced
    // sample from tearing a transparent hole at the silhouette.
    WavePassParams waveParams;
    bool waveReady = false;
    if (mTimeLine.filterEnabled(TimeKeyType_Wave)
        && filterValueIsActive(TimeKeyType_Wave, expans)) {
        QMatrix4x4 world = expans.srt().worldCSRTMatrix();
        if (!aInfo.nonPosed && expans.bone().isAffectedByBinding())
            world = expans.bone().outerMatrix();
        const QSize textureSize = expans.areaTexture()->size();
        // The clamp is the image's texel-centre range: one half texel inside its
        // outline, so a saturated sample lands on the edge texel instead of on the
        // boundary between it and the transparent composite around it.
        waveParams = wavePassFor(
            aInfo.camera, aInfo.time, QSizeF(frame.size()), expans.wave(), world, expans.imageOffset(),
            QVector2D(0.5f, 0.5f),
            QVector2D(float(textureSize.width()) - 0.5f, float(textureSize.height()) - 0.5f)
        );
        // A transform that collapses a dimension cannot be inverted; wavePassFor
        // zeroes the strength for it rather than dividing by it, and a pass that
        // would copy its own input is not run at all.
        waveReady = waveParams.strength != 0.0f;

        // PAINTED MASKS: the amount mask scales the displacement, the time-offset
        // mask delays the phase (its scale is the wave key's own field). Both are
        // sampled in the same content space as the field, so the mask sticks to the
        // artwork exactly like the wave does.
        if (FilterMask* mask = mTimeLine.filterMask(TimeKeyType_Wave, FilterMaskChannel_Amount)) {
            if (!mask->isEmpty()) {
                waveParams.amountMask = mask->texture();
                waveParams.amountMaskRect = mask->contentRect();
            }
        }
        if (FilterMask* mask = mTimeLine.filterMask(TimeKeyType_Wave, FilterMaskChannel_TimeOffset)) {
            if (!mask->isEmpty()) {
                waveParams.timeMask = mask->texture();
                waveParams.timeMaskRect = mask->contentRect();
                waveParams.timeMaskCycles = expans.wave().timeOffset();
            }
        }
    }
    // One predicate pair for both decisions - the isolation above and the pass
    // here: the type's stack entry is enabled AND its BLENDED value is above the
    // type's epsilon. Sharing it is what keeps a wave-isolated layer from
    // skipping a live static blur (or the reverse).
    const auto live = [&](TimeKeyType aType) {
        return mTimeLine.filterEnabled(aType) && filterValueIsActive(aType, expans);
    };

    // ONE content-space mapping, shared by every filter input that lives there (the
    // wave's field and the HSV amount mask): the inverse of the matrices the layer
    // was drawn with.
    QMatrix4x4 screenToLayer;
    bool haveMapping = false;
    const auto contentMapping = [&]() -> const QMatrix4x4& {
        if (!haveMapping) {
            QMatrix4x4 world = expans.srt().worldCSRTMatrix();
            if (!aInfo.nonPosed && expans.bone().isAffectedByBinding())
                world = expans.bone().outerMatrix();
            const QMatrix4x4 toScreen =
                waveContentToScreen(
                    aInfo.camera, QSizeF(frame.size()), world,
                    maskContentImageOffset(ObjectType_Layer, expans.imageOffset())
                );
            bool invertible = false;
            screenToLayer = toScreen.inverted(&invertible);
            haveMapping = true;
        }
        return screenToLayer;
    };

    for (const FilterEntry& entry : stack) {
        if (!entry.enabled)
            continue;
        if (entry.type == TimeKeyType_HSV) {
            if (!live(TimeKeyType_HSV))
                continue;
            auto slot = std::make_unique<FilterFrame::ScopedSlot>(frame);
            MaskInput mask;
            const FilterMask* maskField = mTimeLine.filterMask(TimeKeyType_HSV, FilterMaskChannel_Amount);
            if (maskField && !maskField->isEmpty()) {
                mask.texture = const_cast<FilterMask*>(maskField)->texture();
                mask.rect = maskField->contentRect();
                mask.neutral = float(maskField->neutral()) / 255.0f;
            }
            srcTexture = frame.hsvTexture(
                slot->slot(), srcTexture, expans.hsv().hsv(), mask, mask.isValid() ? contentMapping() : QMatrix4x4()
            );
            srcSlot = slot->slot();
            scratchSlots.push_back(std::move(slot));
        } else if (entry.type == TimeKeyType_Blur) {
            if (!live(TimeKeyType_Blur))
                continue;
            const WorldBlurEllipse ellipse = worldBlurEllipse(
                aAccessor, this, expans.blurX(), expans.blurY(), expans.angleDeg());
            const float zoom = aInfo.camera.scale();
            const float radiusX = ellipse.majorRadius * zoom;
            const float radiusY = ellipse.minorRadius * zoom;
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
            // A MASKED blur cannot vary its kernel per pixel: render the blurred
            // result into a scratch slot, keep the sharp composite, and mix the two
            // by the field (one extra pass, no extra blur taps). The ladder blur
            // writes its input slot in place, which is why the sharp source is copied
            // first only on that path.
            const FilterMask* blurMask = mTimeLine.filterMask(TimeKeyType_Blur, FilterMaskChannel_Amount);
            if (blurMask && !blurMask->isEmpty()) {
                MaskInput mask;
                mask.texture = const_cast<FilterMask*>(blurMask)->texture();
                mask.rect = blurMask->contentRect();
                mask.neutral = float(blurMask->neutral()) / 255.0f;

                auto blurred = std::make_unique<FilterFrame::ScopedSlot>(frame);
                const bool ladder =
                    FilterFrame::blurLadderLevel(radiusX, radiusY, frame.size()) > 0;
                GLuint blurredSrc = srcTexture;
                if (ladder) {
                    // keep the sharp content: blur a COPY in the scratch slot
                    auto copy = std::make_unique<FilterFrame::ScopedSlot>(frame);
                    srcTexture = frame.copyTexture(copy->slot(), srcTexture);
                    blurredSrc = srcTexture;
                    scratchSlots.push_back(std::move(copy));
                }
                frame.blurTexture(
                    blurred->slot(), blurredSrc, majorScreen, radiusX, minorScreen, radiusY
                );

                auto mixed = std::make_unique<FilterFrame::ScopedSlot>(frame);
                srcTexture =
                    frame.mixTexture(mixed->slot(), srcTexture, frame.texture(blurred->slot()), mask, contentMapping());
                srcSlot = mixed->slot();
                scratchSlots.push_back(std::move(blurred));
                scratchSlots.push_back(std::move(mixed));
            } else {
                srcTexture = frame.blurTexture(
                    srcSlot, srcTexture, majorScreen, radiusX, minorScreen, radiusY
                );
            }
        } else if (entry.type == TimeKeyType_Wave) {
            if (!waveReady)
                continue;
            // A wave SAMPLES its source, so it needs a scratch slot exactly like
            // HSV (writing back into the source slot would read what it just
            // wrote for the pixels it has already covered).
            auto slot = std::make_unique<FilterFrame::ScopedSlot>(frame);
            srcTexture = frame.waveTexture(slot->slot(), srcTexture, waveParams);
            srcSlot = slot->slot();
            scratchSlots.push_back(std::move(slot));
        }
    }

    // 2b. THE MASK PREVIEW, and the two kinds are shown differently on purpose:
    //   * a FILTER input is a VALUE field, and the artist wants to see it over the
    //     artwork - a tint where it deviates from its neutral (unchanged behaviour);
    //   * a LAYER MASK is the node's VISIBILITY, so painting it shows the RUBYLITH: a
    //     flat translucent overlay on what the matte hides, over the content and over
    //     nothing. A tint cannot do that - it only lands where the composite has
    //     colour, which dyed the artwork's own lines.
    if (isPreviewTarget(aInfo)) {
        auto slot = std::make_unique<FilterFrame::ScopedSlot>(frame);
        if (aInfo.maskPreviewKey) {
            if (const gl::Texture* keyTexture = aInfo.maskPreviewKey->data().textureId()) {
                srcTexture = frame.maskOverlayTexture(
                    slot->slot(), srcTexture, keyTexture->id(), aInfo.maskPreviewKey->data().contentRect(),
                    contentMapping(), aInfo.maskPreviewColor
                );
                srcSlot = slot->slot();
                scratchSlots.push_back(std::move(slot));
            }
        } else if (aInfo.maskPreview) {
            MaskInput preview;
            preview.texture = const_cast<FilterMask*>(aInfo.maskPreview)->texture();
            preview.rect = aInfo.maskPreview->contentRect();
            preview.neutral = float(aInfo.maskPreview->neutral()) / 255.0f;
            srcTexture = frame.tintTexture(
                slot->slot(), srcTexture, preview, contentMapping(), aInfo.maskPreviewColor
            );
            srcSlot = slot->slot();
            scratchSlots.push_back(std::move(slot));
        }
    }

    // 3. present onto the scene with the layer's blend mode and accumulated opacity.
    // non-Normal blend modes sample the captured scene behind the layer (DestinationText
    // capture), so the blend sees real background pixels instead of the composite itself.
    const img::BlendMode blendMode = expans.blendMode();
    const float presentOpacity = worldOpacity * aInfo.opacityScale;
    auto& ggl = gl::Global::functions();
    ggl.glBindFramebuffer(GL_FRAMEBUFFER, aInfo.framebuffer);
    if (blendMode != img::BlendMode_Normal) {
        XC_PTR_ASSERT(aInfo.destTexturizer);
        aInfo.destTexturizer->updateAll(aInfo.framebuffer, aInfo.dest);
        frame.drawBlendPresent(srcTexture, blendMode, aInfo.destTexturizer->texture().id(), presentOpacity);
    } else {
        frame.drawBlendPresent(srcTexture, blendMode, 0, presentOpacity);
    }
}

void LayerNode::renderClippees(const RenderInfo& i, const TimeCacheAccessor& a)
{
    ObjectNodeUtil::renderClippees(*this, mClippees, i, a,
        [this](const auto& inf, const auto& acc, uint8 id){ renderClipper(inf, acc, id); });
}

void LayerNode::renderClipper(const RenderInfo& aInfo, const TimeCacheAccessor& aAccessor, uint8 aClipperId) {
    if (!mIsVisible || !aInfo.clippingFrame)
        return;
    if (!mCurrentMesh)
        return;

    gl::Global::Functions& ggl = gl::Global::functions();
    auto& shader = mShaderHolder.clipperShader(aInfo.clippingId != 0);
    auto& expans = aAccessor.get(mTimeLine);

    if (!expans.areaTexture())
        return;
    auto textureId = expans.areaTexture()->id();
    auto textureSize = expans.areaTexture()->size();
    auto texCoordOffset = mCurrentMesh->originOffset() - expans.imageOffset();

    core::ClippingFrame& frame = *aInfo.clippingFrame;
    frame.updateRenderStamp();

    // bind framebuffer
    frame.bind();
    frame.setupDrawBuffers();

    // bind textures
    ggl.glActiveTexture(GL_TEXTURE0);
    ggl.glBindTexture(GL_TEXTURE_2D, textureId);
    ggl.glActiveTexture(GL_TEXTURE1);
    ggl.glBindTexture(GL_TEXTURE_2D, frame.texture().id());

    // view matrix
    const QMatrix4x4 viewMatrix = aInfo.camera.viewMatrix();
    // color
    const float opacity = expans.worldOpacity() * aInfo.opacityScale;
    QColor color(255, 255, 255, xc_clamp((int)(255 * opacity), 0, 255));
    {
        shader.bind();

        shader.setAttributeBuffer("inPosition", mMeshTransformer.positions(), GL_FLOAT, 3);
        shader.setAttributeArray("inTexCoord", mCurrentMesh->texCoords(), mCurrentMesh->vertexCount());

        shader.setUniformValue("uViewMatrix", viewMatrix);
        shader.setUniformValue("uScreenSize", QSizeF(aInfo.camera.deviceScreenSize()));
        shader.setUniformValue("uImageSize", QSizeF(textureSize));
        shader.setUniformValue("uTexCoordOffset", texCoordOffset);
        shader.setUniformValue("uColor", color);
        shader.setUniformValue("uClipperId", (int)aClipperId);
        shader.setUniformValue("uTexture", 0);
        shader.setUniformValue("uDestTexture", 1);

        if (aInfo.clippingId != 0) {
            shader.setUniformValue("uClippingId", (int)aInfo.clippingId);
        }

        gl::Util::drawElements(mCurrentMesh->primitiveMode(), GL_UNSIGNED_INT, mCurrentMesh->getIndexBuffer());

        shader.release();
    }
    // unbind texture
    ggl.glActiveTexture(GL_TEXTURE0);
    ggl.glBindTexture(GL_TEXTURE_2D, 0);

    // release framebuffer
    frame.release();

    // bind default framebuffer
    ggl.glBindFramebuffer(GL_FRAMEBUFFER, aInfo.framebuffer);

    ggl.glFlush();
}

void LayerNode::transformShape(const RenderInfo& aInfo, const TimeCacheAccessor& aAccessor) {
    auto& expans = aAccessor.get(mTimeLine);

    // current mesh
    LayerMesh* mesh = expans.ffdMesh();
    if (!mesh || mesh->vertexCount() <= 0) {
        // no source geometry: nothing may be drawn from a previous frame, and no old
        // key may make a later valid pass look cached
        mCurrentMesh = nullptr;
        mTransformKeyValid = false;
        return;
    }

    // positions
    util::ArrayBlock<const gl::Vector3> positions;
    bool useInfluence = true;

    if (aInfo.originMesh) // ignore mesh deforming
    {
        useInfluence = false;
        if (expans.areaImageKey()) {
            mesh = &(expans.areaImageKey()->data().gridMesh());
            if (!mesh || mesh->vertexCount() <= 0) {
                mCurrentMesh = nullptr;
                mTransformKeyValid = false;
                return;
            }
        }
        positions = util::ArrayBlock<const gl::Vector3>(mesh->positions(), mesh->vertexCount());
    }
    else {
        useInfluence = (mesh == expans.bone().targetMesh());
        positions = util::ArrayBlock<const gl::Vector3>(expans.ffd().positions(), expans.ffd().count());
        XC_MSG_ASSERT(
            mesh->vertexCount() == positions.count(), "vtx count = %d, %d", mesh->vertexCount(), positions.count()
        );
    }
    XC_ASSERT(positions);

    TransformKey key = {};
    key.frame = aInfo.time.frame;
    key.blendRevision = expans.revision();
    key.mesh = mesh;
    key.positions = positions.array();
    key.positionsCount = positions.count();
    key.nonPosed = aInfo.nonPosed;
    key.originMesh = aInfo.originMesh;
    key.useInfluence = useInfluence;
    key.boneTargetMesh = expans.bone().targetMesh();

    // IDENTICAL inputs: the buffers callGL wrote last time are still mounted and still
    // hold this result, and the draw reads them (it applies the camera itself). No GL
    // state is touched here, so the transformer stays bound exactly as it was left.
    if (mTransformKeyValid && mCurrentMesh && mTransformKey == key)
        return;

    // transform
    mMeshTransformer.callGL(
        expans, mesh->getMeshBuffer(), mesh->originOffset(), positions, aInfo.nonPosed, useInfluence
    );

    mCurrentMesh = mesh;
    mTransformKey = key;
    mTransformKeyValid = true;
}

// The inverse of the matrices this layer is drawn with: screen pixels -> the
// layer's content pixels. The filter passes build it for their content-space fields
// (the wave's, the HSV mask's); a layer mask needs it on the DIRECT path too.
QMatrix4x4 LayerNode::contentMappingFor(
    const RenderInfo& aInfo, const TimeCacheAccessor& aAccessor, const QSize& aTargetSize
) const {
    const auto& expans = aAccessor.get(mTimeLine);
    QMatrix4x4 world = expans.srt().worldCSRTMatrix();
    if (!aInfo.nonPosed && expans.bone().isAffectedByBinding())
        world = expans.bone().outerMatrix();
    const QMatrix4x4 toScreen =
        waveContentToScreen(aInfo.camera, QSizeF(aTargetSize), world, expans.imageOffset());
    bool invertible = false;
    const QMatrix4x4 inverted = toScreen.inverted(&invertible);
    return invertible ? inverted : QMatrix4x4();
}

void LayerNode::renderLayer(const RenderInfo& aInfo, const TimeCacheAccessor& aAccessor, bool useHSV, QList<int> HSVData = {}) {
    if (!mCurrentMesh)
        return;

    gl::Global::Functions& ggl = gl::Global::functions();
    const bool isClippee = (aInfo.clippingFrame && aInfo.clippingId != 0);

    auto& expans = aAccessor.get(mTimeLine);
    if (!expans.areaImageKey() || !expans.areaTexture())
        return;

    auto textureId = expans.areaTexture()->id();
    auto textureSize = expans.areaTexture()->size();
    auto texCoordOffset = mCurrentMesh->originOffset() - expans.imageOffset();
    auto blendMode = expans.blendMode();
    const QMatrix4x4 viewMatrix = aInfo.camera.viewMatrix();

    // THE NODE'S MATTE. A mask with a field compiles into the draw (LAYER_MASK), so
    // the multiply is one texture read in the layer's own fragment shader and needs
    // no isolated composite: a masked layer still draws on the direct path.
    const MaskKey::Data& maskData = expans.mask();
    // a matte at its neutral changes nothing, so it does not even take the masked
    // shader variant: adding a mask stays a no-op until the first stroke
    const bool previewingMask = aInfo.maskPreviewTarget == this
        && (aInfo.maskPreview != nullptr || aInfo.maskPreviewKey != nullptr);
    const bool useLayerMask =
        !aInfo.isGrid && !previewingMask && maskData.isValid() && maskData.isPainted();
    auto& shader = aInfo.isGrid ? mShaderHolder.gridShader()
                                : mShaderHolder.shader(blendMode, isClippee, useHSV, useLayerMask);

    // update destination color
    XC_PTR_ASSERT(aInfo.destTexturizer);
    auto destTextureId = aInfo.destTexturizer->texture().id();
    if (!aInfo.isGrid && blendMode != img::BlendMode_Normal) {
        aInfo.destTexturizer->update(
            aInfo.framebuffer, aInfo.dest, viewMatrix, *mCurrentMesh, mMeshTransformer.positions()
        );
    }

    if (aInfo.isGrid) {
        ggl.glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    } else {
        // blend func
        ggl.glEnable(GL_BLEND);
        ggl.glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        ggl.glActiveTexture(GL_TEXTURE0);
        ggl.glBindTexture(GL_TEXTURE_2D, textureId);
        ggl.glActiveTexture(GL_TEXTURE1);
        ggl.glBindTexture(GL_TEXTURE_2D, destTextureId);

        if (isClippee) {
            ggl.glActiveTexture(GL_TEXTURE2);
            ggl.glBindTexture(GL_TEXTURE_2D, aInfo.clippingFrame->texture().id());
        }
    }

    {
        const float opacity = expans.worldOpacity() * aInfo.opacityScale;
        QColor color(255, 255, 255, xc_clamp((int)(255 * opacity), 0, 255));
        if (aInfo.isGrid)
            color = QColor(Qt::black);

        shader.bind();
        shader.setAttributeBuffer("inPosition", mMeshTransformer.positions(), GL_FLOAT, 3);
        shader.setAttributeArray("inTexCoord", mCurrentMesh->texCoords(), mCurrentMesh->vertexCount());

        shader.setUniformValue("uViewMatrix", viewMatrix);
        shader.setUniformValue("uScreenSize", QSizeF(aInfo.camera.deviceScreenSize()));
        shader.setUniformValue("uImageSize", QSizeF(textureSize));
        shader.setUniformValue("uTexCoordOffset", texCoordOffset);
        shader.setUniformValue("uColor", color);
        shader.setUniformValue("uTexture", 0);
        shader.setUniformValue("uDestTexture", 1);

        // THE LAYER SAMPLE's footprint and level, for LayerDrawingFrag.glsl's reconstructing read.
        // From the TRANSFORM, not from derivatives: a derivative is per 2x2 quad and shifts with the
        // sub-pixel phase, which would modulate the sharpness the reconstruction exists to keep
        // constant. The bias is applied HERE, so the texture's own GL_TEXTURE_LOD_BIAS must stay 0
        // (ImageKey sets it to 0 for this reason).
        {
            const QVector2D meshScale = expans.srt().scale();
            const float screenScale = aInfo.camera.scale()
                * std::max(std::fabs(meshScale.x()), std::fabs(meshScale.y()));
            const float footprint = 1.0f / std::max(screenScale, 1e-4f);
            shader.setUniformValue("uLayerFootprint", QVector2D(footprint, footprint));
            shader.setUniformValue("uLayerLodBias", ImageKey::layerLodBias());
            shader.setUniformValue(
                "uLayerMaxLod", float(std::max(0, expans.areaTexture()->mipmapLevels() - 1))
            );
        }

        if (isClippee) {
            shader.setUniformValue("uClippingId", (int)aInfo.clippingId);
            shader.setUniformValue("uClippingTexture", 2);
        }

        if (useLayerMask) {
            if (const gl::Texture* maskTexture = maskData.textureId()) {
                // unit 3: 0 is the layer, 1 the destination, 2 the clipping frame
                ggl.glActiveTexture(GL_TEXTURE3);
                ggl.glBindTexture(GL_TEXTURE_2D, maskTexture->id());
                ggl.glActiveTexture(GL_TEXTURE0);
                shader.setUniformValue("uLayerMask", 3);
                const QRectF rect = maskData.contentRect();
                shader.setUniformValue(
                    "uLayerMaskRect",
                    QVector4D(
                        float(rect.x()), float(rect.y()), float(rect.width()), float(rect.height())
                    )
                );
                shader.setUniformValue(
                    "uScreenToLayer", waveAffine3(contentMappingFor(aInfo, aAccessor, aInfo.camera.deviceScreenSize()))
                );
            }
        }

        if (useHSV && !HSVData.isEmpty()) {
            shader.setUniformValue("setColor", bool(HSVData[3]));

            float hue = (float)HSVData[0] / 360.0f;
            float saturation = (float)(HSVData[1]) / 100.0f;
            float value = (float)(HSVData[2]) / 100.0f;

            shader.setUniformValue("hue", hue);
            shader.setUniformValue("saturation", saturation);
            shader.setUniformValue("value", value);
        }

        gl::Util::drawElements(mCurrentMesh->primitiveMode(), GL_UNSIGNED_INT, mCurrentMesh->getIndexBuffer());

        shader.release();
    }

    if (aInfo.isGrid) {
        ggl.glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    } else {
        ggl.glActiveTexture(GL_TEXTURE0);
        ggl.glBindTexture(GL_TEXTURE_2D, 0);

        // blend func
        ggl.glDisable(GL_BLEND);
    }

    // No glFlush here: the frame flushes once, in MainDisplayWidget::paintGL. A flush
    // per layer measured ~5 us each - one driver submission per layer, for no ordering
    // benefit (GL orders a context's commands itself).
}

cmnd::Vector LayerNode::createResourceUpdater(const ResourceEvent& aEvent) {
    cmnd::Vector result;

    if (aEvent.type() != ResourceEvent::Type_Reload) {
        return result;
    }

    ResourceUpdatingWorkspacePtr workspace = std::make_shared<ResourceUpdatingWorkspace>();
    const bool createTransitions = !mTimeLine.isEmpty(TimeKeyType_FFD);

    // image key
    result.push(ImageKeyUpdater::createResourceUpdater(*this, aEvent, workspace, createTransitions));

    // ffd key should be called finally
    if (createTransitions) {
        result.push(FFDKeyUpdater::createResourceUpdater(*this, workspace));
    }

    return result;
}

void LayerNode::reserveShadersFromTimeline() {
    mShaderHolder.reserveGridShader();
    mShaderHolder.reserveClipperShaders();

    auto reserveOne = [this](ImageKey* k) {
        mShaderHolder.reserveShaders(k->data().blendMode());
    };

    if (auto def = static_cast<ImageKey*>(mTimeLine.defaultKey(TimeKeyType_Image)))
        reserveOne(def);

    for (auto key : mTimeLine.map(TimeKeyType_Image))
        reserveOne(static_cast<ImageKey*>(key));
}

bool LayerNode::serialize(Serializer& aOut) const {
    static const std::array<uint8, 8> sig = {'L', 'a', 'y', 'e', 'r', 'N', 'd', '_'};
    return ObjectNodeUtil::writeObjectBlock(aOut, sig, mName, mIsVisible, mIsSlimmedDown,
                                  mInitialRect, mIsClipped, mTimeLine);
}

bool LayerNode::deserialize(Deserializer& aIn) {
    auto res = ObjectNodeUtil::readObjectBlock(aIn, "LayerNd_", mName, mIsVisible, mIsSlimmedDown,
                            mInitialRect, mIsClipped, mTimeLine);
    if (!res) return res;
    reserveShadersFromTimeline();
    return res;
}

} // namespace core
