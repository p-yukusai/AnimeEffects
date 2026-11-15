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
#include "core/VirtualTexture.h"
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
    mMeshRevision(0),
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
    mTimeLine.setBaseValue(TimeKeyType_Image, key);
    key->setImage(aHandle, aBlendMode);
    key->resetGridMesh();
    key->setImageOffsetByCenter();

    // the VT variation costs one more program per (clippee, hsv, mask) combination, so it is
    // reserved only for a resource that can actually be paged
    const bool pageable =
        aHandle->hasImage() && VirtualTexture::pageable(aHandle->image().pixelSize());
    mShaderHolder.reserveShaders(aBlendMode, pageable);
    mShaderHolder.reserveGridShader();
    mShaderHolder.reserveClipperShaders();
}

void LayerNode::setDefaultPosture(const QVector2D& aPos) {
    if (TimeLine::usesChannelTracks(TimeKeyType_Move)) {
        mTimeLine.setChannelBaseValue(TimeKeyType_Move, 0, aPos.x());
        mTimeLine.setChannelBaseValue(TimeKeyType_Move, 1, aPos.y());
        mTimeLine.setChannelBaseValue(TimeKeyType_Move, 2, 0.0f);
        mTimeLine.setChannelBaseValue(TimeKeyType_Move, 3, 0.0f);
    } else {
        getOrCreateBaseValue<MoveKey, TimeKeyType_Move>(mTimeLine)->data().setPos(aPos);
    }
    if (!TimeLine::usesChannelTracks(TimeKeyType_Rotate)) {
        getOrCreateBaseValue<RotateKey, TimeKeyType_Rotate>(mTimeLine);
    }
    if (!TimeLine::usesChannelTracks(TimeKeyType_Scale)) {
        getOrCreateBaseValue<ScaleKey, TimeKeyType_Scale>(mTimeLine);
    }
}

void LayerNode::setDefaultDepth(float aValue) {
    if (TimeLine::usesChannelTracks(TimeKeyType_Depth)) {
        mTimeLine.setChannelBaseValue(TimeKeyType_Depth, 0, aValue);
    } else {
        getOrCreateBaseValue<DepthKey, TimeKeyType_Depth>(mTimeLine)->setDepth(aValue);
    }
}

void LayerNode::setDefaultOpacity(float aValue) {
    if (TimeLine::usesChannelTracks(TimeKeyType_Opacity)) {
        mTimeLine.setChannelBaseValue(TimeKeyType_Opacity, 0, aValue);
    } else {
        getOrCreateBaseValue<OpacityKey, TimeKeyType_Opacity>(mTimeLine)->setOpacity(aValue);
    }
}

float LayerNode::initialDepth() const {
    if (TimeLine::usesChannelTracks(TimeKeyType_Depth)) {
        return mTimeLine.channelBaseValue(TimeKeyType_Depth, 0);
    }
    auto key = (DepthKey*)mTimeLine.baseValue(TimeKeyType_Depth);
    return key ? key->depth() : 0.0f;
}

void LayerNode::setClipped(bool aIsClipped) { mIsClipped = aIsClipped; }

bool LayerNode::isClipper()  const { return ObjectNodeUtil::isClipper(this); }

img::BlendMode LayerNode::blendMode() const {
    auto key = (ImageKey*)mTimeLine.baseValue(TimeKeyType_Image);
    return key ? key->data().blendMode() : img::BlendMode_Normal;
}

void LayerNode::setBlendMode(img::BlendMode aMode) {
    auto key = (ImageKey*)mTimeLine.baseValue(TimeKeyType_Image);
    if (key) {
        key->data().setBlendMode(aMode);
        const bool pageable = key->data().resource() && key->data().resource()->hasImage()
            && VirtualTexture::pageable(key->data().resource()->image().pixelSize());
        mShaderHolder.reserveShaders(aMode, pageable);
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

    if (!mIsVisible || aAccessor.get(mTimeLine).opacity().isZero()) {
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
    // decision: an HSV edited on its base value must bake into the layer shader
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
        const QSize contentSize = expans.areaTextureSize();
        if (contentSize.isEmpty())
            return;
        frameStats.layerDrawn(screenAreaPx(aInfo, expans, contentSize));
    };

    // The paged path's demand for THIS frame: the pages this layer's own triangles sample,
    // marked from the same skinned positions the draw will read. A layer with no paged texture
    // (or a grid pass, which does not sample the image) costs one null check.
    const auto markPagedDemand = [&]() {
        if (aInfo.isGrid || !mCurrentMesh)
            return;
        const TimeKeyExpans& expans = aAccessor.get(mTimeLine);
        // THE DEMAND'S INPUTS, so a repaint of the same frame can replay its pages instead of
        // walking every triangle again (VirtualTexture::markDemand). `mMeshRevision` is the
        // skinning pass's own stamp: the buffer it writes into is stable while the values behind
        // it change, so the revision - not the buffer's identity - is what says "same vertices".
        // A camera-only pan keeps the revision and changes the view matrix, which re-marks.
        if (VirtualTexture* virtualTexture = expans.areaVirtualTexture()) {
            // THE AFFINE FAST PATH. A layer with no deformation has one Jacobian over its whole
            // surface, so the LAYER SHADER's uv is a single affine function of the screen
            // position and the pages its fragments can reach are the quad's page extent - the
            // same union the mesh walk computes (every triangle of a grid shares that Jacobian),
            // at four vertices instead of the grid's triangles. Measured on nene at frame 6: ALL
            // 107 paged layers qualify and they own ALL 1,746,491 triangles the pass walks
            // (`AE_VT_DEMAND_SPLIT=1` prints the split); `AE_VT_AFFINE_DEMAND=0` walks the mesh
            // anyway, which is the control.
            //
            // The predicate is deliberately conservative - a mesh key, an FFD KEY (not the
            // position storage: `ffd()` is what the blender always fills, grid positions
            // included), a bone binding or a non-identity wave each count as deformation - and a
            // degenerate screen->content mapping falls back to the walk rather than trusting it.
            //
            // THE FFD TEST IS THE KEY'S PRESENCE, not the type of the key it deforms:
            // `ffdMeshParent()` names the ImageKey (or MeshKey) whose grid mesh the FFD keys
            // displace - a key can never BE an FFD key, so a `type() == TimeKeyType_FFD` test
            // there is false by construction and every FFD-warped layer took the quad. A quad
            // cannot see a warp: a fragment's uv is the WARPED mesh's, while `screenToContent` is
            // the node's SRT, which maps the REST grid - so the pages a warp pulls in are not the
            // quad's pages, and a miss is a coarse fallback wherever the warp exceeds a page.
            // (An FFD key whose value happens to be the rest grid costs the walk and nothing
            // else; the alternative - comparing `ffd()` against the area mesh every frame - is a
            // per-vertex CPU pass to save a geometry pass.)
            const bool ffdWarp = !mTimeLine.map(TimeKeyType_FFD).isEmpty();
            // THE TWO BONE POPULATIONS, which `isAffectedByBinding()` alone conflates. A layer
            // RIGIDLY BOUND to a bone is drawn with one matrix (`MeshTransformer::callGL` takes the
            // outer matrix, inner identity) and `contentMappingFor` reads the same matrix, so its
            // quad is exact - it is still excluded, conservatively. A layer under an active
            // BoneKey is an INFLUENCE TARGET: the blender gives every such descendant a cache, so
            // `bone().targetMesh()` is its own mesh, and `callGL` takes the `useInfluence` branch
            // and skins it per vertex with the pose palette. That is a genuine deformation the
            // transform cannot see, so it must walk. `AE_VT_DEMAND_SPLIT`'s `infl=` prints the
            // population - dinner lab frame 0 has nine such layers, seven of them on the quad
            // before this test.
            const bool skinned = expans.bone().targetMesh() != nullptr;
            const bool undeformed = expans.areaMeshKey() == nullptr && !ffdWarp && !skinned
                && !expans.bone().isAffectedByBinding() && expans.wave().isIdentity();
            const QSize contentSize = expans.areaTextureSize();
            QMatrix4x4 screenToContent;
            bool affine = undeformed && contentSize.width() > 0 && contentSize.height() > 0
                && qgetenv("AE_VT_AFFINE_DEMAND") != "0";
            if (affine) {
                screenToContent =
                    contentMappingFor(aInfo, aAccessor, aInfo.camera.deviceScreenSize());
                // a collapsed mapping would mark nothing (or everything): the mesh walk is the
                // safe answer, and it is what the draw uses anyway
                const QVector3D o = screenToContent.map(QVector3D(0.0f, 0.0f, 0.0f));
                const QVector3D u = screenToContent.map(QVector3D(1.0f, 0.0f, 0.0f)) - o;
                const QVector3D v = screenToContent.map(QVector3D(0.0f, 1.0f, 0.0f)) - o;
                affine = QVector3D::crossProduct(u, v).length() > 1e-6f;
            }
            if (qEnvironmentVariableIsSet("AE_VT_DEMAND_SPLIT")) {
                // `infl=` is the bone INFLUENCE target (every descendant of an active BoneKey),
                // which `MeshTransformer::callGL` skins per vertex - the second population the
                // predicate excludes (see the class comment above). Before that test, a layer
                // reporting infl=1 affine=1 was on the quad although its draw was deformed.
                qWarning(
                    "VTDSPLIT layer=%s tris=%d affine=%d fast=%d infl=%d", qPrintable(name()),
                    mCurrentMesh->indexCount() / 3, undeformed ? 1 : 0, affine ? 1 : 0,
                    expans.bone().targetMesh() ? 1 : 0
                );
            }
            if (affine) {
                VirtualTexture::DemandSignature signature;
                signature.targetSize = aInfo.camera.deviceScreenSize();
                signature.viewMatrix = aInfo.camera.viewMatrix();
                signature.screenToContent = screenToContent;
                // the DRAW's texel offset, so the quad names the same texels the mesh walk would
                signature.texCoordOffset = mCurrentMesh->originOffset() - expans.imageOffset();
                signature.maskRect = QVector4D(
                    0.0f, 0.0f, (float)contentSize.width(), (float)contentSize.height()
                );
                virtualTexture->markScreenDemand(signature);
            } else {
                VirtualTexture::DemandSignature signature;
                signature.positions = mMeshRevision;
                signature.targetSize = aInfo.camera.deviceScreenSize();
                signature.viewMatrix = aInfo.camera.viewMatrix();
                signature.texCoordOffset = mCurrentMesh->originOffset() - expans.imageOffset();
                virtualTexture->markDemand(
                    signature, mMeshTransformer.positions(), mCurrentMesh->texCoords(),
                    mCurrentMesh->vertexCount(), mCurrentMesh->getIndexBuffer(),
                    mCurrentMesh->indexCount()
                );
            }
        }
        // THE MATTE'S OWN PAGES. The mask is paged by its own stack (its grid is the field's,
        // not the image's: replacing an image leaves an authored mask at its old resolution), and
        // it is sampled through the node's screen-content mapping rather than the mesh's uv, so
        // its demand pass gets that mapping and the same skinned triangles. It is marked even
        // while the mask is previewed: the rubylith samples the SAME paged mask.
        const MaskKey::Data& mask = expans.mask();
        // The MATTE follows the layer's paging policy: a pageable mask on an unpaged image stays
        // a full chain, so the two textures of one draw are always paged together (and the A/B
        // control, which forces the image full, is a control for the mask too).
        if (expans.areaVirtualTexture() && mask.isValid() && mask.isPainted()) {
            if (VirtualTexture* pagedMask = mask.virtualTexture()) {
                // THE MATTE'S UV IS ONE AFFINE OVER THE WHOLE SCREEN, so its demand is a QUAD, not
                // a walk of the layer's triangles. The fragment shader reads
                // `(uScreenToLayer * vDestCoord - rect.xy) / rect.zw` (LayerDrawingFrag.glsl) - the
                // node's SRT, not the mesh's texcoords and not the deformation - so every fragment
                // has the SAME Jacobian and the same level, and the pages a fragment can sample
                // are the affine image of the target rect. That image contains every fragment the
                // layer can draw wherever the deformation puts it, which is why the signature
                // carries no mesh revision: a moving FFD or pose cannot change the matte's pages,
                // only which of them are sampled, and the quad's set is a superset. This is the
                // same entry point a folder's matte uses (markScreenMaskDemand) - one demand
                // implementation for both mattes, and no triangle walk anywhere for a mask.
                VirtualTexture::DemandSignature signature;
                signature.targetSize = aInfo.camera.deviceScreenSize();
                signature.viewMatrix = aInfo.camera.viewMatrix();
                signature.screenToContent =
                    contentMappingFor(aInfo, aAccessor, aInfo.camera.deviceScreenSize());
                const QRectF rect = mask.contentRect();
                signature.maskRect = QVector4D(
                    float(rect.x()), float(rect.y()), float(rect.width()), float(rect.height())
                );
                pagedMask->markScreenMaskDemand(signature);
            }
        }
    };

    // THE EXPORT'S DEMAND-ONLY PASS: mark this layer's pages, walk the clippees too (they are
    // paged layers of their own), draw nothing.
    if (aInfo.demandOnly) {
        markPagedDemand();
        renderClippees(aInfo, aAccessor);
        return;
    }

    if (!aInfo.isGrid && (isCompositeLayer(aAccessor) || isPreviewTarget(aInfo))) {
        countDrawnLayer();
        markPagedDemand();
        renderComposite(aInfo, aAccessor);
    } else {
        countDrawnLayer();
        markPagedDemand();
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
    // a filter edited on its base value from being dropped here. The composite
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
    if (!expans.areaImageKey() || (!expans.areaTexture() && !expans.areaVirtualTexture()))
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
        const QSize textureSize = expans.areaTextureSize();
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
            // THE RUBYLITH PAGES WITH THE MATTE. A pageable field has no full chain at all
            // (MaskKey::Data::virtualTexture), and the overlay is drawn over the very content
            // whose pages the demand pass marks, so the tool paints a paged matte end to end.
            const MaskKey::Data& previewData = aInfo.maskPreviewKey->data();
            MaskInput preview;
            preview.rect = previewData.contentRect();
            if (VirtualTexture* paged = expans.areaVirtualTexture() ? previewData.virtualTexture()
                                                                  : nullptr) {
                preview.paged = paged;
            } else if (const gl::Texture* keyTexture = previewData.textureId()) {
                preview.texture = keyTexture->id();
            }
            if (preview.isValid()) {
                srcTexture = frame.maskOverlayTexture(
                    slot->slot(), srcTexture, preview, contentMapping(), aInfo.maskPreviewColor
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
    // the demand-only pass marks the clipper's pages in its own render(): the mask samples the
    // same mesh, so the same pages are the ones wanted
    if (aInfo.demandOnly)
        return;
    if (!mIsVisible || !aInfo.clippingFrame)
        return;
    if (!mCurrentMesh)
        return;

    gl::Global::Functions& ggl = gl::Global::functions();
    auto& expans = aAccessor.get(mTimeLine);
    const VirtualTexture* virtualTexture = expans.areaVirtualTexture();
    if (!expans.areaImageKey() || (!expans.areaTexture() && !virtualTexture))
        return;
    auto& shader = mShaderHolder.clipperShader(aInfo.clippingId != 0, virtualTexture != nullptr);
    // the paged path has no full texture: unit 0 is the coarse chain, which keeps the sampler
    // complete and is what a missed page reads anyway
    auto textureId = virtualTexture ? virtualTexture->coarseTexture().id() : expans.areaTexture()->id();
    auto textureSize = expans.areaTextureSize();
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
        // the shared sampler's level controls, exactly as renderLayer sets them for the same
        // texture: without these the program's defaults would clamp every read to level 0
        shader.setUniformValue("uLayerLodBias", ImageKey::layerLodBias());
        shader.setUniformValue(
            "uLayerMaxLod", float(virtualTexture ? virtualTexture->maxLevel()
                                                 : std::max(0, expans.areaTexture()->mipmapLevels() - 1))
        );
        if (virtualTexture)
            virtualTexture->bind(shader);

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
    ++mMeshRevision;

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
    const VirtualTexture* virtualTexture = expans.areaVirtualTexture();
    if (!expans.areaImageKey() || (!expans.areaTexture() && !virtualTexture))
        return;

    // the paged path has no full texture; unit 0 is bound to the coarse chain so the sampler
    // is never an incomplete texture (the variation does not read it, but GL still requires a
    // complete texture for any sampler the program can reach)
    auto textureId = virtualTexture ? virtualTexture->coarseTexture().id() : expans.areaTexture()->id();
    auto textureSize = expans.areaTextureSize();
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
    // A PAGED MATTE has its own chain: the layer's VIRTUAL_TEXTURE bit says the image is paged,
    // the mask's says the field is (same field, its own page grid). The mask accessor builds it
    // on first use; the demand pass has already asked for it this frame in the normal case.
    VirtualTexture* maskVirtualTexture =
        (virtualTexture && useLayerMask) ? maskData.virtualTexture() : nullptr;
    // THE LAYER SAMPLE's footprint is the draw's own derivatives, unconditionally (see
    // LayerDrawingFrag.glsl's sampleLayer): a mesh/FFD/bone deformation moves the pixels without
    // moving the SRT, so there is no second, transform-fed variant left to choose between.
    auto& shader = aInfo.isGrid
        ? mShaderHolder.gridShader()
        : mShaderHolder.shader(
            blendMode, isClippee, useHSV, useLayerMask, virtualTexture != nullptr,
            maskVirtualTexture != nullptr
        );

    // update destination color
    XC_PTR_ASSERT(aInfo.destTexturizer);
    auto destTextureId = aInfo.destTexturizer->texture().id();
    const bool refreshDestination = !aInfo.isGrid && blendMode != img::BlendMode_Normal;
    if (refreshDestination) {
        aInfo.destTexturizer->update(
            aInfo.framebuffer, aInfo.dest, viewMatrix, *mCurrentMesh, mMeshTransformer.positions()
        );
    }
    // AE_TRACE_DEST: which draws read the destination texture and which of them refreshed it.
    // The fragment shader's output goes through blendColor(color, destColor) for EVERY layer, so
    // a draw with refreshed=0 is blending against whatever the texturizer last held.
    if (qEnvironmentVariableIsSet("AE_TRACE_DEST") && !aInfo.isGrid) {
        qWarning(
            "VTDEST layer=%s blend=%d clippee=%d refreshed=%d mask=%d maskPaged=%d", qPrintable(name()),
            (int)blendMode, isClippee ? 1 : 0, refreshDestination ? 1 : 0, useLayerMask ? 1 : 0,
            maskVirtualTexture ? 1 : 0
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

        // AE_TRACE_GEO: the DRAW'S GEOMETRY, hashed. Everything else about the paged read has been
        // proven equal to the control path (see the design doc): the uv, the level and the texels.
        // The positions come from the skinning pass and decide which fragments exist at all, so a
        // sub-pixel difference there is exactly an edge-only difference on a thin stroke. Two runs
        // of one command either print the same hashes or they do not.
        if (qEnvironmentVariableIsSet("AE_TRACE_GEO")) {
            const int count = mCurrentMesh->vertexCount();
            std::vector<float> positions((size_t)count * 3, 0.0f);
            ggl.glBindBuffer(GL_ARRAY_BUFFER, mMeshTransformer.positions().id());
            ggl.glGetBufferSubData(
                GL_ARRAY_BUFFER, 0, (GLsizeiptr)(positions.size() * sizeof(float)), positions.data()
            );
            ggl.glBindBuffer(GL_ARRAY_BUFFER, 0);
            const auto hashBytes = [](const void* aData, size_t aCount) {
                const uint8* bytes = (const uint8*)aData;
                quint64 h = 1469598103934665603ull;
                for (size_t i = 0; i < aCount; ++i) {
                    h ^= bytes[i];
                    h *= 1099511628211ull;
                }
                return h;
            };
            const quint64 posHash = hashBytes(positions.data(), positions.size() * sizeof(float));
            const quint64 uvHash = hashBytes(
                mCurrentMesh->texCoords(), (size_t)count * sizeof(gl::Vector2)
            );
            qWarning(
                "VTGEO layer=%s verts=%d pos=%016llx uv=%016llx", qPrintable(name()), count,
                (unsigned long long)posHash, (unsigned long long)uvHash
            );
        }

        shader.setUniformValue("uViewMatrix", viewMatrix);
        shader.setUniformValue("uScreenSize", QSizeF(aInfo.camera.deviceScreenSize()));
        shader.setUniformValue("uImageSize", QSizeF(textureSize));
        shader.setUniformValue("uTexCoordOffset", texCoordOffset);
        shader.setUniformValue("uColor", color);
        shader.setUniformValue("uTexture", 0);
        shader.setUniformValue("uDestTexture", 1);


        // THE LAYER SAMPLE's level controls, for LayerDrawingFrag.glsl's two-tap read. The footprint
        // itself comes from the shader's own derivatives, so nothing about the draw's geometry is
        // passed here. The bias is applied in the shader, so the texture's own GL_TEXTURE_LOD_BIAS
        // must stay 0 (ImageKey sets it to 0 for this reason).
        shader.setUniformValue("uLayerLodBias", ImageKey::layerLodBias());
        shader.setUniformValue(
            "uLayerMaxLod", float(virtualTexture ? virtualTexture->maxLevel()
                                                 : std::max(0, expans.areaTexture()->mipmapLevels() - 1))
        );
        // AE_VT_DEBUG_LEVEL: the level this fragment chose, as colour, on BOTH paths (see
        // LayerSampling.glsl). Two runs - control and paged - then differ only in this value, so
        // the level fields can be compared pixel for pixel. AE_VT_TAG_LAYER makes each draw output
        // its own opaque tag instead, which names the layer on top at a pixel; the tag is printed
        // once per frame so the colour in the frame can be looked up.
        const bool tagLayer = qEnvironmentVariableIsSet("AE_VT_TAG_LAYER");
        shader.setUniformValue(
            "uVtDebugLevel",
            tagLayer ? 2
                      : (qEnvironmentVariableIsSet("AE_VT_DEBUG_UV")
                             ? 3
                             : (qEnvironmentVariableIsSet("AE_VT_DEBUG_LEVEL") ? 1 : 0))
        );
        if (tagLayer) {
            const quint32 tag = qHash(name());
            const int tr = (int)((tag >> 16) & 255u);
            const int tg = (int)((tag >> 8) & 255u);
            const int tb = (int)(tag & 255u);
            shader.setUniformValue(
                "uVtTag", QVector3D(float(tr) / 255.0f, float(tg) / 255.0f, float(tb) / 255.0f)
            );
            qWarning("VTTAG layer=%s tag=%d,%d,%d", qPrintable(name()), tr, tg, tb);
        }
        // THE PAGED READ's own state: the coarse chain, the page table, the atlases and the
        // per-resource grid. It also re-sets uLayerMaxLod (the chain's last level), so the two
        // values cannot disagree.
        if (virtualTexture)
            virtualTexture->bind(shader);

        if (isClippee) {
            shader.setUniformValue("uClippingId", (int)aInfo.clippingId);
            shader.setUniformValue("uClippingTexture", 2);
        }

        if (useLayerMask) {
            // unit 3: 0 is the layer, 1 the destination, 2 the clipping frame. A PAGED mask
            // binds its coarse chain there - the variation never samples unit 3, but GL wants
            // every sampler it can reach complete, and the coarse chain is what a miss reads.
            const gl::Texture* fullMask = nullptr;
            ggl.glActiveTexture(GL_TEXTURE3);
            if (maskVirtualTexture) {
                ggl.glBindTexture(GL_TEXTURE_2D, maskVirtualTexture->coarseTexture().id());
                maskVirtualTexture->bindMask(shader);
            } else if ((fullMask = maskData.textureId()) != nullptr) {
                ggl.glBindTexture(GL_TEXTURE_2D, fullMask->id());
            }
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
        const bool pageable = k->data().resource() && k->data().resource()->hasImage()
            && VirtualTexture::pageable(k->data().resource()->image().pixelSize());
        mShaderHolder.reserveShaders(k->data().blendMode(), pageable);
    };

    if (auto def = static_cast<ImageKey*>(mTimeLine.baseValue(TimeKeyType_Image)))
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
