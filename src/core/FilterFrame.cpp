#include "gl/Global.h"
#include "gl/Util.h"
#include "gl/ExtendShader.h"
#include "img/BlendMode.h"
#include "core/FilterFrame.h"

namespace {

static const char* kFullscreenVert =
    "#version 330 \n"
    "in vec4 inPosition;"
    "in vec2 inTexCoord;"
    "uniform vec2 uScreenSize;"
    "uniform vec2 uImageSize;"
    "uniform vec2 uTexCoordOffset;"
    "out vec2 vTexCoord;"
    "out vec2 vDestCoord;"
    "void main() {"
    "  gl_Position = inPosition;"
    "  vDestCoord = vec2(uScreenSize.x * (inPosition.x + 1.0) * 0.5, uScreenSize.y * (inPosition.y + 1.0) * 0.5);"
    "  vTexCoord = (inTexCoord + uTexCoordOffset) / uImageSize;"
    "}";

// Plain premultiplied passthrough used by the blur ladder: downsampling and upsampling
// must average premultiplied samples (no unpremultiply, no uColor).
static const char* kResampleFrag =
    "#version 330 \n"
    "uniform sampler2D uTexture;"
    "in vec2 vTexCoord;"
    "layout(location = 0, index = 0) out vec4 oFragColor;"
    "void main() {"
    "  oFragColor = texture(uTexture, vTexCoord);"
    "}";

// The HSV pass and the per-blend present passes share the layer fragment shader source
// (single source of truth for the HSV math and the blend functions). The composite texture
// is premultiplied, so PREMULTIPLIED_INPUT is enabled.

// Separable 1D Gaussian pass. sigma = radius/2, taps = ceil(3*sigma).
// The composite texture is premultiplied; averaging premultiplied samples is correct.
// The sigma floor is a small epsilon (not a visible radius): below the epsilon a
// radius renders as a 3-tap kernel whose off-center weights are ~0, i.e. identity, so
// interpolations can ramp in from zero without a step (FilterFrame::kMinActiveBlurRadius
// matches this floor).
static const char* kBlurFrag =
    "#version 330 \n"
    "uniform vec4 uColor;"
    "uniform sampler2D uTexture;"
    "uniform vec2 uTexelSize;"
    "uniform vec2 uBlurDirection;"
    "uniform float uBlurRadius;"
    "in vec2 vTexCoord;"
    "layout(location = 0, index = 0) out vec4 oFragColor;"
    "void main() {"
    "  float sigma = max(uBlurRadius * 0.5, 0.001);"
    "  int taps = int(ceil(sigma * 3.0));"
    "  vec4 c = vec4(0.0);"
    "  float wsum = 0.0;"
    "  for (int i = -taps; i <= taps; ++i) {"
    "    float x = float(i);"
    "    float w = exp(-(x * x) / (2.0 * sigma * sigma));"
    "    c += w * texture(uTexture, vTexCoord + uBlurDirection * (x * uTexelSize));"
    "    wsum += w;"
    "  }"
    "  oFragColor = uColor * (c / wsum);"
    "}";
} // namespace

namespace core {

FilterFrame::FilterFrame():
    mSize(), mAcquired(), mFramebuffers(), mTextures(), mFramebuffersL(), mTexturesL(), mFramebuffersL2(),
    mTexturesL2(), mPassShaders(), mIndices(GL_ELEMENT_ARRAY_BUFFER) {
    createQuadBuffers();
    createShaders();
}

FilterFrame::~FilterFrame() = default; // mPassShaders/mPresentShaders free themselves

static void fCreateLadderLevel(
    std::array<QScopedPointer<gl::Framebuffer>, FilterFrame::kMaxLadderLevels>& aFbs,
    std::array<QScopedPointer<gl::Texture>, FilterFrame::kMaxLadderLevels>& aTexs, const QSize& aSize, int aLevel
) {
    const QSize ls(qMax(1, aSize.width() >> aLevel), qMax(1, aSize.height() >> aLevel));
    aFbs[aLevel - 1].reset(new gl::Framebuffer());
    aTexs[aLevel - 1].reset(new gl::Texture());
    aTexs[aLevel - 1]->create(ls);
    aTexs[aLevel - 1]->setFilter(GL_LINEAR);
    aTexs[aLevel - 1]->setWrap(GL_CLAMP_TO_EDGE);
    aFbs[aLevel - 1]->setColorAttachment(0, aTexs[aLevel - 1]->id());
    XC_ASSERT(aFbs[aLevel - 1]->isComplete());
}

void FilterFrame::ensureLadder(int aLevel) {
    XC_ASSERT(aLevel >= 1 && aLevel <= kMaxLadderLevels);
    for (int l = 1; l <= aLevel; ++l) {
        if (!mTexturesL[l - 1])
            fCreateLadderLevel(mFramebuffersL, mTexturesL, mSize, l);
        // the second chain exists only for the Gaussian ping-pong, which happens at
        // the deepest level of each blurApply call, so L2 is created only there
        if (l == aLevel && !mTexturesL2[l - 1])
            fCreateLadderLevel(mFramebuffersL2, mTexturesL2, mSize, l);
    }
}

void FilterFrame::appendSlot(const QSize& aSize) {
    const int i = (int)mFramebuffers.size();
    mFramebuffers.emplace_back(new gl::Framebuffer());
    mTextures.emplace_back(new gl::Texture());
    mTextures[i]->create(aSize);
    mTextures[i]->setFilter(GL_LINEAR);
    mTextures[i]->setWrap(GL_CLAMP_TO_EDGE);
    mFramebuffers[i]->setColorAttachment(0, mTextures[i]->id());
    XC_ASSERT(mFramebuffers[i]->isComplete());
}

void FilterFrame::resize(const QSize& aSize) {
    if (aSize == mSize)
        return;
    mSize = aSize;
    // Lazy pool: nothing is allocated until a filter is actually used (the first
    // acquire creates the initial batch of slots, the ladder path creates its targets
    // on demand). On a size change the existing targets are freed so the next use
    // re-creates them at the new size.
    mFramebuffers.clear();
    mTextures.clear();
    for (int i = 0; i < kMaxLadderLevels; ++i) {
        mFramebuffersL[i].reset();
        mTexturesL[i].reset();
        mFramebuffersL2[i].reset();
        mTexturesL2[i].reset();
    }
    mAcquired = 0;
}

void FilterFrame::clearAll() {
    if (mSize.isEmpty())
        return;
    auto& ggl = gl::Global::functions();
    static const GLenum attachments[] = {GL_COLOR_ATTACHMENT0};
    for (int i = 0; i < (int)mFramebuffers.size(); ++i) {
        mFramebuffers[i]->bind();
        ggl.glDrawBuffers(1, attachments);
        gl::Util::setViewportAsActualPixels(mSize);
        gl::Util::resetRenderState();
        ggl.glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        ggl.glClear(GL_COLOR_BUFFER_BIT);
        mFramebuffers[i]->release();
    }
}

int FilterFrame::acquire() {
    // Lazy first use: the pool starts empty (no FBOs are allocated for a scene that
    // never uses a filter). The first acquire allocates the initial batch up front so
    // common nesting depths don't re-enter GL allocation once per acquire.
    if (mFramebuffers.empty()) {
        XC_ASSERT(!mSize.isEmpty());
        mFramebuffers.reserve(kDefaultSlots);
        mTextures.reserve(kDefaultSlots);
        for (int i = 0; i < kDefaultSlots; ++i)
            appendSlot(mSize);
    }
    // composite nesting depth is unbounded (a folder/layer chain with active keys holds one
    // slot per level on the ladder path, two when direct-blurred), so grow the pool on
    // demand instead of overflowing a fixed array
    if (mAcquired >= (int)mFramebuffers.size()) {
        XC_ASSERT(!mSize.isEmpty());
        appendSlot(mSize);
    }
    return mAcquired++;
}

void FilterFrame::release(int aSlot) {
    (void)aSlot;
    XC_ASSERT(mAcquired > 0);
    --mAcquired;
}

GLuint FilterFrame::framebuffer(int aSlot) const {
    XC_ASSERT(aSlot >= 0 && aSlot < (int)mFramebuffers.size());
    return mFramebuffers[aSlot]->id();
}

GLuint FilterFrame::texture(int aSlot) const {
    XC_ASSERT(aSlot >= 0 && aSlot < (int)mTextures.size());
    return mTextures[aSlot]->id();
}

void FilterFrame::bind(int aSlot) {
    mFramebuffers[aSlot]->bind();
}

int FilterFrame::blurLadderLevel(float aRadiusX, float aRadiusY, const QSize& aCompositeSize) {
    float r = std::max(aRadiusX, aRadiusY);
    int level = 0;
    while (r > kDirectBlurRadius && level < kMaxLadderLevels) {
        r *= 0.5f;
        ++level;
    }
    if (level == 0)
        return 0;
    // both axes must stay meaningful at the reduced level; otherwise the downsample
    // would over-blur the weak axis, so fall back to the direct two-pass blur
    const float scale = (float)(1 << level);
    if (aRadiusX / scale < 1.0f || aRadiusY / scale < 1.0f)
        return 0;
    // the reduced buffer must stay at least 2x2
    while ((aCompositeSize.width() >> level) < 2 || (aCompositeSize.height() >> level) < 2) {
        --level;
        if (level == 0)
            return 0;
    }
    return level;
}

GLuint FilterFrame::blurApply(
    int aCompositeSlot, const QVector2D& aDirX, float aRadiusX, const QVector2D& aDirY, float aRadiusY) {
    const int level = blurLadderLevel(aRadiusX, aRadiusY, mSize);
    XC_ASSERT(level > 0);
    ensureLadder(level);
    const float scale = (float)(1 << level);
    auto& ggl = gl::Global::functions();

    // each ladder pass draws into a stale target; blend is enabled in drawQuad, so the
    // target must be cleared first (the direct blur path clears its slots the same way)
    auto runPass = [&](GLuint aTargetFbo, GLuint aSrcTex, Kind aKind, const BlurParams& aBlur,
                       const QSizeF& aTarget, const QSizeF& aSrc, bool aFlipY) {
        ggl.glBindFramebuffer(GL_FRAMEBUFFER, aTargetFbo);
        gl::Util::setViewportAsActualPixels(aTarget.toSize());
        gl::Util::resetRenderState();
        ggl.glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        ggl.glClear(GL_COLOR_BUFFER_BIT);
        drawQuad(aSrcTex, aKind, QList<int>(), aBlur, 1.0f, false, aTarget, aSrc, aFlipY);
    };

    // 1. downsample the composite through the ladder down to level `level`
    QSize srcSize = mSize;
    GLuint srcTex = texture(aCompositeSlot);
    for (int l = 1; l <= level; ++l) {
        const QSize dstSize(mSize.width() >> l, mSize.height() >> l);
        runPass(ladderFramebuffer(l), srcTex, Kind_Resample, BlurParams(), QSizeF(dstSize), QSizeF(srcSize),
            l == 1);
        srcTex = ladderTexture(l);
        srcSize = dstSize;
    }

    // 2. separable Gaussian at the reduced level (ping-pong between the level chains)
    FilterFrame::BlurParams h, v;
    h.direction = aDirX;
    h.radiusTexels = aRadiusX / scale;
    v.direction = aDirY;
    v.radiusTexels = aRadiusY / scale;
    runPass(ladderFramebuffer2(level), ladderTexture(level), Kind_Blur, h, QSizeF(srcSize), QSizeF(srcSize),
        false);
    runPass(ladderFramebuffer(level), ladderTexture2(level), Kind_Blur, v, QSizeF(srcSize), QSizeF(srcSize),
        false);

    // 3. upsample back up the ladder and into the composite slot (full size)
    for (int l = level - 1; l >= 1; --l) {
        const QSize dstSize(mSize.width() >> l, mSize.height() >> l);
        const QSize sSz(mSize.width() >> (l + 1), mSize.height() >> (l + 1));
        runPass(ladderFramebuffer(l), ladderTexture(l + 1), Kind_Resample, BlurParams(), QSizeF(dstSize),
            QSizeF(sSz), false);
    }
    runPass(framebuffer(aCompositeSlot), ladderTexture(1), Kind_Resample, BlurParams(), QSizeF(mSize),
        QSizeF(mSize.width() >> 1, mSize.height() >> 1), true);

    return texture(aCompositeSlot);
}

void FilterFrame::createQuadBuffers() {
    static const GLuint kIndices[4] = {0, 1, 3, 2};
    mIndices.resetData(4, GL_STATIC_DRAW, kIndices);
}

static void fBuildLayerDrawingVariant(
    gl::EasyShaderProgram& aShader, const QString& aBlendFunc, int aIsClippee, int aUseHSV, int aPremultiplied
) {
    gl::ExtendShader source;
    if (!source.openFromFileVert("./data/shader/LayerDrawingVert.glsl")) {
        XC_FATAL_ERROR("FileIO Error", "Failed to open vertex shader file.", source.log());
    }
    if (!source.openFromFileFrag("./data/shader/LayerDrawingFrag.glsl")) {
        XC_FATAL_ERROR("FileIO Error", "Failed to open fragment shader file.", source.log());
    }
    source.setVariationValue("BLEND_FUNC", aBlendFunc);
    source.setVariationValue("IS_CLIPPEE", aIsClippee ? "1" : "0");
    source.setVariationValue("USE_HSV", aUseHSV ? "1" : "0");
    source.setVariationValue("PREMULTIPLIED_INPUT", aPremultiplied ? "1" : "0");
    if (!source.resolveVariation()) {
        XC_FATAL_ERROR("OpenGL Error", "Failed to resolve shader variation.", source.log());
    }
    if (!aShader.setAllSource(source)) {
        XC_FATAL_ERROR("OpenGL Error", "Failed to compile shader.", aShader.log());
    }
    if (!aShader.link()) {
        XC_FATAL_ERROR("OpenGL Error", "Failed to link shader.", aShader.log());
    }
}

void FilterFrame::createShaders() {
    auto build = [this](int aIndex, const char* aVert, const char* aFrag) {
        gl::EasyShaderProgram& shader = mPassShaders[aIndex];
        if (!shader.setVertexSource(QString(aVert))) {
            XC_FATAL_ERROR("OpenGL Error", "Failed to compile vertex shader.", shader.log());
        }
        if (!shader.setFragmentSource(QString(aFrag))) {
            XC_FATAL_ERROR("OpenGL Error", "Failed to compile fragment shader.", shader.log());
        }
        if (!shader.link()) {
            XC_FATAL_ERROR("OpenGL Error", "Failed to link shader.", shader.log());
        }
    };
    build(Kind_Blur, kFullscreenVert, kBlurFrag);
    build(Kind_Resample, kFullscreenVert, kResampleFrag);

    // HSV pass: shared layer fragment shader with variations
    fBuildLayerDrawingVariant(mPassShaders[Kind_HSV], QString("BlendNormal"), 0, 1, 1);
}

gl::EasyShaderProgram& FilterFrame::presentShader(img::BlendMode aMode) {
    XC_ASSERT(aMode < img::BlendMode_TERM);
    if (!mPresentShaders[aMode]) {
        mPresentShaders[aMode].reset(new gl::EasyShaderProgram());
        auto shader = mPresentShaders[aMode].data();
        auto blendFunc = QString("Blend") + img::getBlendFuncNameFromBlendMode(aMode);
        fBuildLayerDrawingVariant(*shader, blendFunc, 0, 0, 1);
    }
    return *mPresentShaders[aMode];
}

void FilterFrame::drawQuad(
    GLuint aSrcTexture, Kind aKind, const QList<int>& aHSV, const BlurParams& aBlur, float aOpacity,
    bool aNeedHSV, const QSizeF& aTargetSize, const QSizeF& aSrcSize, bool aFlipY, img::BlendMode aPresentBlend,
    GLuint aPresentDest
) {
    if (mSize.isEmpty())
        return;
    const QSizeF target = aTargetSize.isEmpty() ? QSizeF(mSize) : aTargetSize;
    const QSizeF src = aSrcSize.isEmpty() ? QSizeF(mSize) : aSrcSize;

    std::array<gl::Vector4, 4> positions;
    positions[0].set(-1.0f, -1.0f, 0.0f, 1.0f);
    positions[1].set(-1.0f, 1.0f, 0.0f, 1.0f);
    positions[2].set(1.0f, 1.0f, 0.0f, 1.0f);
    positions[3].set(1.0f, -1.0f, 0.0f, 1.0f);
    std::array<gl::Vector2, 4> texCoords;
    if (aFlipY) {
        texCoords[0].set(0.0f, (float)src.height());
        texCoords[1].set(0.0f, 0.0f);
        texCoords[2].set((float)src.width(), 0.0f);
        texCoords[3].set((float)src.width(), (float)src.height());
    } else {
        texCoords[0].set(0.0f, 0.0f);
        texCoords[1].set(0.0f, (float)src.height());
        texCoords[2].set((float)src.width(), (float)src.height());
        texCoords[3].set((float)src.width(), 0.0f);
    }

    auto& ggl = gl::Global::functions();
    gl::Util::setViewportAsActualPixels(target.toSize());
    gl::Util::resetRenderState();
    if (aKind == Kind_HSV) {
        // presentation passes draw the (unpremultiplied) composite onto the accumulated scene
        ggl.glEnable(GL_BLEND);
        ggl.glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE);
    } else {
        // resample and blur passes read PREMULTIPLIED textures and write into freshly
        // cleared targets; SRC_ALPHA blending would multiply the premultiplied RGB by the
        // averaged alpha again and darken every semi-transparent texel, so write the
        // samples through unchanged
        ggl.glDisable(GL_BLEND);
    }

    ggl.glActiveTexture(GL_TEXTURE0);
    ggl.glBindTexture(GL_TEXTURE_2D, aSrcTexture);
    if (aKind == Kind_HSV) {
        // BlendNormal present reads the dest only as a dummy; the blend-aware present
        // samples the real captured scene instead (aPresentDest).
        const GLuint destTexture = (aPresentBlend != img::BlendMode_Normal) ? aPresentDest : aSrcTexture;
        ggl.glActiveTexture(GL_TEXTURE1);
        ggl.glBindTexture(GL_TEXTURE_2D, destTexture);
        ggl.glActiveTexture(GL_TEXTURE0);
    }

    // Presentation shader selection: when the composite content still needs its HSV
    // applied (a folder presenting its own keyed HSV), use the USE_HSV variant; otherwise
    // the content's HSV is already baked into the composite, so present with the per-mode
    // blend func and PREMULTIPLIED_INPUT (no identity-HSV round trip on the shader input).
    const bool presentBlend = (aKind == Kind_HSV && !aNeedHSV);
    auto& shader = presentBlend ? presentShader(aPresentBlend) : mPassShaders[aKind];
    shader.bind();
    shader.setAttributeArray("inPosition", positions.data(), 4);
    shader.setAttributeArray("inTexCoord", texCoords.data(), 4);

    QColor color(255, 255, 255, xc_clamp((int)(255 * aOpacity), 0, 255));
    shader.setUniformValue("uColor", color);
    shader.setUniformValue("uTexture", 0);
    shader.setUniformValue("uScreenSize", QSizeF(target));
    shader.setUniformValue("uImageSize", QSizeF(src));
    shader.setUniformValue("uTexCoordOffset", QVector2D(0, 0));

    if (aKind == Kind_HSV) {
        QMatrix4x4 ident;
        shader.setUniformValue("uViewMatrix", ident);
        shader.setUniformValue("uDestTexture", 1);
        if (aNeedHSV) {
            // per-pass HSV (folder presentation of its own key)
            shader.setUniformValue("setColor", bool(aHSV.size() > 3 ? aHSV[3] : 0));
            shader.setUniformValue("hue", (float)(aHSV.size() > 0 ? aHSV[0] : 0) / 360.0f);
            shader.setUniformValue("saturation", (float)(aHSV.size() > 1 ? aHSV[1] : 100) / 100.0f);
            shader.setUniformValue("value", (float)(aHSV.size() > 2 ? aHSV[2] : 100) / 100.0f);
        }
    } else if (aKind == Kind_Blur) {
        shader.setUniformValue("uTexelSize", QVector2D(1.0f / target.width(), 1.0f / target.height()));
        shader.setUniformValue("uBlurDirection", aBlur.direction);
        shader.setUniformValue("uBlurRadius", aBlur.radiusTexels);
    }

    gl::Util::drawElements(GL_TRIANGLE_STRIP, GL_UNSIGNED_INT, mIndices);
    shader.release();

    ggl.glActiveTexture(GL_TEXTURE0);
    ggl.glBindTexture(GL_TEXTURE_2D, 0);
    ggl.glDisable(GL_BLEND);
}

void FilterFrame::drawBlendPresent(
    GLuint aSrcTexture, img::BlendMode aMode, GLuint aDestTexture, float aOpacity
) {
    // a blurred layer's own HSV is already baked into its composite, so the present never
    // applies per-pass HSV; it only selects the blend func and the real dest capture
    drawQuad(aSrcTexture, Kind_HSV, QList<int>(), BlurParams(), aOpacity, false, QSizeF(), QSizeF(), false, aMode,
        aDestTexture);
}
} // namespace core
