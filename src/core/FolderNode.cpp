#include <algorithm>
#include <memory>
#include <cmath>
#include "gl/Global.h"
#include "gl/Util.h"
#include "core/FolderNode.h"
#include "core/ObjectNodeUtil.h"
#include "core/TimeKeyExpans.h"
#include "core/DepthKey.h"
#include "core/FilterActivity.h"
#include "core/FilterFrame.h"
#include "core/WavePass.h"
#include "core/WorldBlurMath.h"
#include "core/DestinationTexturizer.h"
#include "core/VirtualTexture.h"

namespace core {

namespace {

// THE FOLDER MATTE'S DEMAND. A folder applies its matte as one fullscreen multiply over its
// isolated composite (FilterFrame::maskTexture), so there is no mesh to walk: every page the
// screen->content affine reaches across the target is wanted. Marked through the demand pass's
// own fullscreen quad, so the level is the same Jacobian computation a layer's demand uses.
//
// Both the drawing path and the exporter's demand-only pass call this: the demand-only pass
// returns before renderComposite, and a folder matte left unmarked there would export its first
// frame from the coarse chain (the exporter is synchronous, so nothing later would fix it).
void markFolderMaskDemand(
    const RenderInfo& aInfo, const TimeKeyExpans& aExpans, const MaskKey::Data& aMaskData,
    const QSize& aTargetSize
) {
    if (aTargetSize.isEmpty())
        return;
    if (!aMaskData.isValid() || !aMaskData.isPainted())
        return;
    VirtualTexture* paged = aMaskData.virtualTexture();
    if (!paged)
        return;

    // the mapping is the one renderComposite's contentMapping() builds: a folder's content space
    // is its canvas box, so the image offset is not applied (maskContentImageOffset)
    const QMatrix4x4 toScreen = waveContentToScreen(
        aInfo.camera, QSizeF(aTargetSize), aExpans.srt().worldCSRTMatrix(),
        maskContentImageOffset(ObjectType_Folder, aExpans.imageOffset())
    );
    bool invertible = false;
    const QMatrix4x4 screenToLayer = toScreen.inverted(&invertible);
    if (!invertible)
        return;
    // The signature is the whole input: a fullscreen pass has no vertex data, so the mapping and
    // the target size decide its pages. A repaint of the same frame replays them (the exporter's
    // demand-only pass and its real pass are exactly such a repeat).
    VirtualTexture::DemandSignature signature;
    signature.targetSize = aTargetSize;
    signature.screenToContent = screenToLayer;
    const QRectF rect = aMaskData.contentRect();
    signature.maskRect =
        QVector4D(float(rect.x()), float(rect.y()), float(rect.width()), float(rect.height()));
    paged->markScreenMaskDemand(signature);
}

} // namespace

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

void FolderNode::setDefaultDepth(float aValue) {
    if (TimeLine::usesChannelTracks(TimeKeyType_Depth)) {
        mTimeLine.setChannelBaseValue(TimeKeyType_Depth, 0, aValue);
    } else {
        getOrCreateBaseValue<DepthKey, TimeKeyType_Depth>(mTimeLine)->setDepth(aValue);
    }
}

void FolderNode::setDefaultOpacity(float aValue) {
    if (TimeLine::usesChannelTracks(TimeKeyType_Opacity)) {
        mTimeLine.setChannelBaseValue(TimeKeyType_Opacity, 0, aValue);
    } else {
        getOrCreateBaseValue<OpacityKey, TimeKeyType_Opacity>(mTimeLine)->setOpacity(aValue);
    }
}

void FolderNode::grabHeightMap(HeightMap* aNode) { mHeightMap.reset(aNode); }

bool FolderNode::isClipper() const { return ObjectNodeUtil::isClipper(this); }

bool FolderNode::isCompositeFolder(const TimeInfo&, const TimeCacheAccessor& aAccessor) const {
    // A non-Normal blend mode always composites: its subtree is grouped before the
    // folder's own blend is applied. Otherwise only an ENABLED filter whose BLENDED
    // value is above its epsilon puts the folder on the composite path. The frame
    // is no longer part of the test: the value that plays is already the blended
    // one, so the old "a key exists at or before this frame" proxy only hid static
    // filters (values on the lane's base value) - see core/FilterActivity.h.
    if (mBlendMode != img::BlendMode_Normal)
        return true;
    // A folder with a MATTE composites too, and for the same reason the filter case
    // does: the matte multiplies the folder's PRIORITY as one image, which is what
    // makes it a mask of the subtree instead of a mask of each child separately (a
    // per-child mask would show the gaps between them).
    if (aAccessor.get(mTimeLine).mask().isPainted())
        return true;

    const TimeKeyExpans& expans = aAccessor.get(mTimeLine);
    for (const FilterEntry& entry : mTimeLine.filters()) {
        if (!entry.enabled)
            continue;
        // One test per entry, the same pair the pass itself uses. There is no
        // separate mask clause any more: an HSV whose blended value is the identity
        // skips its own pass (see renderComposite), so isolating just to host a
        // painted mask would composite an image nothing changes - and the Mask
        // tool's preview target still isolates through render()'s own check.
        if (filterValueIsActive(entry.type, expans))
            return true;
    }
    return false;
}

void FolderNode::prerender(const RenderInfo&, const TimeCacheAccessor&) {}

void FolderNode::render(const RenderInfo& aInfo, const TimeCacheAccessor& aAccessor) {
    if (!mIsVisible || aAccessor.get(mTimeLine).opacity().isZero() || aInfo.isGrid) return;

    // the export's demand-only pass: the sorted top-level list holds a COMPOSITE folder as one
    // unit, so its children are not visited by the caller - walk them here, mark, draw nothing
    if (aInfo.demandOnly) {
        // THE FOLDER'S OWN MATTE, before the children: renderComposite is where it is sampled,
        // and this branch returns before reaching it.
        if (aInfo.filterFrame) {
            const TimeKeyExpans& expans = aAccessor.get(mTimeLine);
            markFolderMaskDemand(aInfo, expans, expans.mask(), aInfo.filterFrame->size());
        }
        std::vector<Renderer::SortUnit> units;
        for (auto child : children()) {
            ObjectNodeUtil::collectRenderUnits(*child, true, units, aAccessor, aInfo.time);
        }
        std::stable_sort(
            units.begin(), units.end(), [](const Renderer::SortUnit& a, const Renderer::SortUnit& b) {
                return a.depth < b.depth;
            }
        );
        for (auto& unit : units)
            unit.renderer->render(aInfo, aAccessor);
        return;
    }

    // render clippees
    renderClippees(aInfo, aAccessor);

    // a folder with a filter renders its subtree as a composite and applies the
    // filter to the blended result - and so does the folder the Mask tool is painting,
    // because the field preview is one pass over that composite
    const bool previewTarget = aInfo.maskPreview && aInfo.maskPreviewTarget == this;
    if (isCompositeFolder(aInfo.time, aAccessor) || previewTarget) {
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

    const float ownOpacity = aAccessor.get(mTimeLine).opacity().opacity();

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

    // 2. apply the stack IN ORDER, top to bottom. Blur writes back into the same
    // slot it reads; HSV needs a scratch slot because it samples the source while
    // drawing. The final present applies the folder's opacity and blend mode once,
    // after every filter has run.
    GLuint srcTexture = composite.texture();
    int srcSlot = composite.slot();
    auto& expans = aAccessor.get(mTimeLine);
    std::vector<std::unique_ptr<FilterFrame::ScopedSlot>> scratchSlots;

    // A folder has no image, so the wave field lives in the folder's own local
    // frame (the space its children are positioned in) and there is no content
    // rectangle to clamp a sample into - the composite itself bounds it.
    WavePassParams waveParams;
    bool waveReady = false;
    if (mTimeLine.filterEnabled(TimeKeyType_Wave)
        && filterValueIsActive(TimeKeyType_Wave, expans)) {
        waveParams = wavePassFor(
            aInfo.camera, aInfo.time, QSizeF(frame.size()), expans.wave(),
            expans.srt().worldCSRTMatrix(), QVector2D(0.0f, 0.0f)
        );
        // a collapsed folder transform cannot be inverted; see LayerNode's twin
        waveReady = waveParams.strength != 0.0f;

        // the same two painted masks a layer offers (see LayerNode)
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

    // ONE content-space mapping for every filter input that lives there (the wave's
    // field, the HSV amount mask and the blur's mix mask): the inverse of the
    // matrices the subtree was drawn with.
    QMatrix4x4 screenToLayer;
    bool haveMapping = false;
    const auto contentMapping = [&]() -> const QMatrix4x4& {
        if (!haveMapping) {
            const QMatrix4x4 toScreen = waveContentToScreen(
                aInfo.camera, QSizeF(frame.size()), expans.srt().worldCSRTMatrix(),
                maskContentImageOffset(ObjectType_Folder, expans.imageOffset())
            );
            bool invertible = false;
            screenToLayer = toScreen.inverted(&invertible);
            haveMapping = true;
        }
        return screenToLayer;
    };

    // THE MATTE'S DEMAND, marked before the pass that samples it. A painted matte is applied
    // over the composite below; while the Mask tool paints this folder the matte is suspended
    // and the rubylith samples the PREVIEW key's field instead, so that is the one to demand.
    // (A filter input - a.FilterMask - is never paged: it has no field key of its own.)
    const bool previewingMask = aInfo.maskPreviewTarget == this
        && (aInfo.maskPreview != nullptr || aInfo.maskPreviewKey != nullptr);
    if (previewingMask) {
        if (aInfo.maskPreviewKey)
            markFolderMaskDemand(aInfo, expans, aInfo.maskPreviewKey->data(), frame.size());
    } else {
        markFolderMaskDemand(aInfo, expans, expans.mask(), frame.size());
    }

    // One predicate pair for both decisions - isCompositeFolder above and the
    // passes here: the type's stack entry is enabled AND its BLENDED value is
    // above the type's epsilon. Sharing it is what keeps an HSV-isolated folder
    // from skipping a live static blur (or the reverse).
    const auto live = [&](TimeKeyType aType) {
        return mTimeLine.filterEnabled(aType) && filterValueIsActive(aType, expans);
    };

    for (const FilterEntry& entry : mTimeLine.filters()) {
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
            auto slot = std::make_unique<FilterFrame::ScopedSlot>(frame);
            srcTexture = frame.waveTexture(slot->slot(), srcTexture, waveParams);
            srcSlot = slot->slot();
            scratchSlots.push_back(std::move(slot));
        }
    }

    // THE FOLDER'S OWN MATTE. A layer multiplies its matte inside its own fragment
    // pass; a folder presents an ISOLATED COMPOSITE, so the same multiply is one
    // fullscreen pass over it - and it runs HERE, after the filters, so a blurred
    // folder cannot bleed past its own matte either.
    // a PAINTING PREVIEW suspends the matte (see LayerNode): the rubylith says where
    // something is hidden, and composing it under the matte made it invisible
    if (const MaskKey::Data& maskData = aAccessor.get(mTimeLine).mask();
        !previewingMask && maskData.isValid() && maskData.isPainted()) {
        MaskInput matte;
        matte.rect = maskData.contentRect();
        matte.neutral = float(core::MaskKey::kNeutral) / 255.0f;
        // A PAGEABLE MATTE HAS NO FULL CHAIN (MaskKey::Data::virtualTexture): the pass reads
        // its page stack, and markFolderMaskDemand above demanded the pages this frame needs.
        if (VirtualTexture* paged = maskData.virtualTexture()) {
            matte.paged = paged;
        } else if (const gl::Texture* maskTexture = maskData.textureId()) {
            matte.texture = maskTexture->id();
        }
        if (matte.isValid()) {
            auto slot = std::make_unique<FilterFrame::ScopedSlot>(frame);
            srcTexture = frame.maskTexture(slot->slot(), srcTexture, matte, contentMapping());
            srcSlot = slot->slot();
            scratchSlots.push_back(std::move(slot));
        }
    }

    // THE MASK PREVIEW (see LayerNode): a folder can be the Mask tool's target too.
    // ...and a LAYER MASK being painted previews the same way: its field is the key's
    // own, its neutral is reveal-all, so the rubylith lands exactly on what is hidden.
    if (!aInfo.maskPreview && aInfo.maskPreviewKey && aInfo.maskPreviewTarget == this) {
        const MaskKey::Data& previewData = aInfo.maskPreviewKey->data();
        MaskInput preview;
        preview.rect = previewData.contentRect();
        preview.neutral = float(core::MaskKey::kNeutral) / 255.0f;
        // THE RUBYLITH PAGES WITH THE MATTE: a pageable field has no full chain, and the
        // overlay is drawn over the same composite whose pages markFolderMaskDemand marks.
        if (VirtualTexture* paged = previewData.virtualTexture()) {
            preview.paged = paged;
        } else if (const gl::Texture* keyTexture = previewData.textureId()) {
            preview.texture = keyTexture->id();
        }
        if (preview.isValid()) {
            auto slot = std::make_unique<FilterFrame::ScopedSlot>(frame);
            srcTexture = frame.maskOverlayTexture(
                slot->slot(), srcTexture, preview, contentMapping(), aInfo.maskPreviewColor
            );
            srcSlot = slot->slot();
            scratchSlots.push_back(std::move(slot));
        }
    }
    else if (aInfo.maskPreview && aInfo.maskPreviewTarget == this) {
        MaskInput preview;
        preview.texture = const_cast<FilterMask*>(aInfo.maskPreview)->texture();
        preview.rect = aInfo.maskPreview->contentRect();
        preview.neutral = float(aInfo.maskPreview->neutral()) / 255.0f;
        auto slot = std::make_unique<FilterFrame::ScopedSlot>(frame);
        srcTexture = frame.tintTexture(slot->slot(), srcTexture, preview, contentMapping(), aInfo.maskPreviewColor);
        srcSlot = slot->slot();
        scratchSlots.push_back(std::move(slot));
    }

    gl::Global::functions().glBindFramebuffer(GL_FRAMEBUFFER, aInfo.framebuffer);
    // A non-Normal folder blend mode samples the captured scene behind the folder
    // (DestinationTexturizer capture), so the blend sees the real background instead
    // of the composite itself; the opacity rides uColor.a exactly like the Normal
    // present. HSV is already an explicit stack pass, so the present itself is always
    // the empty-HSV variant.
    if (mBlendMode != img::BlendMode_Normal) {
        XC_PTR_ASSERT(aInfo.destTexturizer);
        aInfo.destTexturizer->updateAll(aInfo.framebuffer, aInfo.dest);
        frame.drawQuad(
            srcTexture, FilterFrame::Kind_HSV, QList<int>(), FilterFrame::BlurParams(), ownOpacity, false,
            QSizeF(), QSizeF(), false, mBlendMode, aInfo.destTexturizer->texture().id()
        );
    } else {
        frame.drawQuad(
            srcTexture, FilterFrame::Kind_HSV, QList<int>(), FilterFrame::BlurParams(), ownOpacity, false
        );
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
    if (TimeLine::usesChannelTracks(TimeKeyType_Depth)) {
        return mTimeLine.channelBaseValue(TimeKeyType_Depth, 0);
    }
    auto key = (DepthKey*)mTimeLine.baseValue(TimeKeyType_Depth);
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
