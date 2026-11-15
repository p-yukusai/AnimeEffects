#include <QVector4D>
#include <QtMath>
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

// WAVE DEFORM pass. Evaluates a two-wave field in the node's CONTENT space and
// samples the composite at the displaced coordinate.
//
// Everything below is in content pixels: vDestCoord is a composite pixel, and
// uScreenToLayer maps it to the layer's image pixels (or the folder's local
// frame), so the wave is measured against the ARTWORK, not the screen - it stays
// put while the camera zooms or the layer rotates. uLayerToScreen maps the
// displaced coordinate back. Both are the exact matrices the layer was drawn
// with, which is why the deformation cannot drift from the content.
//
// The two deform modes are the two things a wave can do to an image:
//   * uWaveMode == 1 (Sway): displace ACROSS the travel direction by the field's
//     height - cloth, hair, flags;
//   * otherwise (Ripple): displace ALONG the field's gradient - water, ripples,
//     heat haze, the refraction offset Wallpaper Engine's water effects use.
// `crest` shapes the sine and its derivative together, so a sharp crest has a
// sharp slope instead of a sharp displacement with a smooth slope.
static const char* kWaveFrag =
    "#version 330 \n"
    "uniform sampler2D uTexture;"
    "uniform vec2 uScreenSize;"
    "uniform mat3 uLayerToScreen;"
    "uniform mat3 uScreenToLayer;"
    "uniform vec2 uClampMin;"
    "uniform vec2 uClampMax;"
    "uniform vec4 uWaveA;" // strength px, wavelength px, angle rad, phase cycles
    "uniform vec4 uWaveB;" // mix 0..1, crest, dual angle rad, dual scale
    "uniform vec2 uWaveC;" // speed cycles/s, document seconds
    "uniform int uWaveMode;" // 0 ripple, 1 sway
    "uniform sampler2D uAmountMask;"
    "uniform vec4 uAmountMaskRect;" // content x, y, w, h
    "uniform sampler2D uTimeMask;"
    "uniform vec4 uTimeMaskRect;"
    "uniform float uTimeMaskCycles;"
    "in vec2 vDestCoord;"
    "layout(location = 0, index = 0) out vec4 oFragColor;"
    "const float kTau = 6.28318530717958647692;"
    "float waveSin(float p, float e) {"
    "  float s = sin(p);"
    "  return sign(s) * pow(abs(s), e);"
    "}"
    "float maskAt(sampler2D aMask, vec4 aRect, vec2 q) {"
    "  vec2 uv = (q - aRect.xy) / aRect.zw;"
    "  if (uv.x < 0.0 || 1.0 < uv.x || uv.y < 0.0 || 1.0 < uv.y) return 0.0;"
    "  return texture(aMask, uv).r;"
    "}"
    "float waveCos(float p, float e) {"
    "  float s = sin(p);"
    "  return e * pow(max(abs(s), 1.0e-3), e - 1.0) * cos(p);"
    "}"
    "void main() {"
    "  vec2 q = (uScreenToLayer * vec3(vDestCoord, 1.0)).xy;"
    "  float wavelength = max(uWaveA.y, 1.0e-3);"
    "  float wavelength2 = max(wavelength * uWaveB.w, 1.0e-3);"
    "  float phase = uWaveA.w + uWaveC.x * uWaveC.y;"
    // the TIME OFFSET mask delays this pixel's phase; the AMOUNT mask scales how
    // far it may move. Both are uniform branches, so an unmasked filter pays for
    // neither the sample nor the arithmetic.
    "  if (uTimeMaskCycles != 0.0) {"
    "    phase += maskAt(uTimeMask, uTimeMaskRect, q) * uTimeMaskCycles;"
    "  }"
    "  float amountScale = 1.0;"
    "  if (uAmountMaskRect.z > 0.0) {"
    "    amountScale = maskAt(uAmountMask, uAmountMaskRect, q);"
    "  }"
    "  vec2 dir1 = vec2(cos(uWaveA.z), sin(uWaveA.z));"
    "  vec2 dir2 = vec2(cos(uWaveA.z + uWaveB.z), sin(uWaveA.z + uWaveB.z));"
    // The second wave crosses the first and drifts at its own rate; the quarter
    // cycle offset keeps the pair from collapsing into one sine.
    "  float phase1 = kTau * (dot(q, dir1) / wavelength - phase);"
    "  float phase2 = kTau * (dot(q, dir2) / wavelength2 - phase * 1.37 + 0.25);"
    "  float amount2 = uWaveB.x;"
    "  float crest = uWaveB.y;"
    "  float weight = 1.0 / (1.0 + amount2);"
    "  vec2 offset;"
    "  if (uWaveMode == 1) {"
    "    vec2 across1 = vec2(-dir1.y, dir1.x);"
    "    float height = waveSin(phase1, crest) + amount2 * waveSin(phase2, crest);"
    "    offset = (uWaveA.x * weight * amountScale) * height * across1;"
    "  } else {"
    "    vec2 gradient = waveCos(phase1, crest) * dir1"
    "      + amount2 * waveCos(phase2, crest) * dir2 * (wavelength / wavelength2);"
    "    offset = (uWaveA.x * weight * amountScale) * gradient;"
    "  }"
    "  vec2 samplePos = q + offset;"
    // The clamp protects a pixel that IS content: a displaced sample there may not
    // leave the content rectangle, so the edge texels are stretched instead of a
    // hole opening (this is an image effect's texture clamp). It must NOT be
    // applied to every pixel of the composite: a layer covers a part of the
    // screen, and clamping the pixels outside its rectangle would smear its edge
    // texel over the whole viewport. Those pixels sample q + offset untouched.
    // The inside test carries half a content texel of slack: a content pixel's q
    // is a texel CENTRE (k + 0.5) recovered through a float matrix, so an exact
    // comparison against the box's own 0.5-inset edge can miss by one ULP - and a
    // missed clamp is a tear, which is exactly what the box exists to prevent.
    // The slack also covers the half-texel band that belongs to the last content
    // texel but lies outside the inset box.
    "  const float kContentEdgeSlack = 0.5;"
    "  if (uClampMax.x > uClampMin.x && uClampMax.y > uClampMin.y"
    "      && all(greaterThanEqual(q, uClampMin - vec2(kContentEdgeSlack)))"
    "      && all(lessThanEqual(q, uClampMax + vec2(kContentEdgeSlack)))) {"
    "    samplePos = clamp(samplePos, uClampMin, uClampMax);"
    "  }"
    "  vec2 pixel = (uLayerToScreen * vec3(samplePos, 1.0)).xy;"
    "  vec2 uv = clamp(pixel / uScreenSize, vec2(0.0), vec2(1.0));"
    "  oFragColor = texture(uTexture, uv);"
    "}";
// MASKED BLUR MIX: `mix(sharp, blurred, mask)`, premultiplied in and out. The mask is
// sampled in the node's content space, exactly like the wave pass and the HSV amount
// mask, so one painted field means the same thing to every filter.
static const char* kMixFrag =
    "#version 330 \n"
    "uniform sampler2D uTexture;"      // sharp
    "uniform sampler2D uSecondTexture;" // blurred
    "uniform sampler2D uMixMask;"
    "uniform vec4 uMixMaskRect;"
    "uniform mat3 uScreenToLayer;"
    "in vec2 vDestCoord;"
    "layout(location = 0, index = 0) out vec4 oFragColor;"
    "void main() {"
    "  vec4 sharp = texture(uTexture, vDestCoord / vec2(textureSize(uTexture, 0)));"
    "  vec4 blurred = texture(uSecondTexture, vDestCoord / vec2(textureSize(uSecondTexture, 0)));"
    "  vec2 q = (uScreenToLayer * vec3(vDestCoord, 1.0)).xy;"
    "  vec2 uv = (q - uMixMaskRect.xy) / uMixMaskRect.zw;"
    "  float amount = (uv.x < 0.0 || 1.0 < uv.x || uv.y < 0.0 || 1.0 < uv.y) ? 1.0 : texture(uMixMask, uv).r;"
    // the field says how much of the BLUR applies: 1 = fully blurred, 0 = sharp
    "  oFragColor = mix(sharp, blurred, amount);"
    "}";

// MASK PREVIEW: adds the tint where the field DEVIATES from its neutral value, so a
// fresh (neutral) field tints nothing and a painted area reads at a glance. The
// content is premultiplied, and the tint adds colour without touching alpha, so it
// lands inside the artwork only.
static const char* kTintFrag =
    "#version 330 \n"
    "uniform sampler2D uTexture;"
    "uniform sampler2D uTintMask;"
    "uniform vec4 uTintMaskRect;"
    "uniform vec4 uTintColor;"   // rgb + strength
    "uniform float uTintNeutral;"
    "uniform mat3 uScreenToLayer;"
    "in vec2 vDestCoord;"
    "layout(location = 0, index = 0) out vec4 oFragColor;"
    "void main() {"
    "  vec4 color = texture(uTexture, vDestCoord / vec2(textureSize(uTexture, 0)));"
    "  vec2 q = (uScreenToLayer * vec3(vDestCoord, 1.0)).xy;"
    "  vec2 uv = (q - uTintMaskRect.xy) / uTintMaskRect.zw;"
    "  if (0.0 <= uv.x && uv.x <= 1.0 && 0.0 <= uv.y && uv.y <= 1.0) {"
    "    float value = texture(uTintMask, uv).r;"
    "    float deviation = abs(value - uTintNeutral);"
    "    color.rgb += uTintColor.rgb * (deviation * uTintColor.a);"
    "  }"
    "  oFragColor = color;"
    "}";

// THE NODE'S OWN MATTE, applied to a FOLDER's isolated composite. A layer multiplies
// its matte in the layer shader's own fragment pass; a folder presents an ISOLATED
// COMPOSITE, so the same multiply is one fullscreen pass over it - the same
// content-space sampling (uScreenToLayer, the matrix every content-space field uses)
// and the same rule: outside the field the node is fully visible.
static const char* kLayerMaskFrag =
    "#version 330 \n"
    "uniform sampler2D uTexture;"
    "uniform sampler2D uLayerMask;"
    "uniform vec4 uLayerMaskRect;"
    "uniform mat3 uScreenToLayer;"
    "in vec2 vDestCoord;"
    "layout(location = 0, index = 0) out vec4 oFragColor;"
    "void main() {"
    "  vec4 color = texture(uTexture, vDestCoord / vec2(textureSize(uTexture, 0)));"
    "  vec2 q = (uScreenToLayer * vec3(vDestCoord, 1.0)).xy;"
    "  vec2 uv = (q - uLayerMaskRect.xy) / uLayerMaskRect.zw;"
    "  float visible = (uv.x < 0.0 || 1.0 < uv.x || uv.y < 0.0 || 1.0 < uv.y)"
    "      ? 1.0 : texture(uLayerMask, uv).r;"
    // the composite is PREMULTIPLIED, so hiding part of it scales colour and alpha
    // together; scaling alpha alone would brighten what survives
    "  oFragColor = color * visible;"
    "}";

// THE RUBYLITH. A mask overlay is NOT a tint on the artwork: a tint only shows where
// the composite has colour, so a stroke across transparent pixels left the red out and
// a stroke across linework dyed the lines - "why are some of the lines red?". An
// overlay paints the hidden region as a FLAT translucent red, over whatever is there
// and over nothing, which is what Photoshop's quick mask means.
static const char* kMaskOverlayFrag =
    "#version 330 \n"
    "uniform sampler2D uTexture;"
    "uniform sampler2D uLayerMask;"
    "uniform vec4 uLayerMaskRect;"
    "uniform vec4 uOverlayColor;"   // rgb + alpha
    "uniform mat3 uScreenToLayer;"
    "in vec2 vDestCoord;"
    "layout(location = 0, index = 0) out vec4 oFragColor;"
    "void main() {"
    "  vec4 color = texture(uTexture, vDestCoord / vec2(textureSize(uTexture, 0)));"
    "  vec2 q = (uScreenToLayer * vec3(vDestCoord, 1.0)).xy;"
    "  vec2 uv = (q - uLayerMaskRect.xy) / uLayerMaskRect.zw;"
    "  float hidden = 0.0;"
    "  if (0.0 <= uv.x && uv.x <= 1.0 && 0.0 <= uv.y && uv.y <= 1.0)"
    "      hidden = 1.0 - texture(uLayerMask, uv).r;"
    "  float k = hidden * uOverlayColor.a;"
    // src-over with a premultiplied overlay: the composite is premultiplied too
    "  oFragColor = color * (1.0 - k) + vec4(uOverlayColor.rgb * k, k);"
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

GLuint FilterFrame::blurTexture(
    int aTargetSlot, GLuint aSrcTexture, const QVector2D& aDirX, float aRadiusX,
    const QVector2D& aDirY, float aRadiusY
) {
    if (blurLadderLevel(aRadiusX, aRadiusY, mSize) > 0)
        return blurApply(aTargetSlot, aDirX, aRadiusX, aDirY, aRadiusY);

    // Direct two-pass path, rewritten against an explicit target slot so callers
    // can chain filters through scratch slots.
    BlurParams h;
    h.direction = QVector2D(aDirX.x(), -aDirX.y());
    h.radiusTexels = aRadiusX;
    BlurParams v;
    v.direction = QVector2D(aDirY.x(), -aDirY.y());
    v.radiusTexels = aRadiusY;

    ScopedSlot hSlot(*this);
    bind(hSlot.slot());
    gl::Util::setViewportAsActualPixels(size());
    gl::Util::resetRenderState();
    gl::Global::functions().glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl::Global::functions().glClear(GL_COLOR_BUFFER_BIT);
    drawQuad(aSrcTexture, Kind_Blur, QList<int>(), h, 1.0f);

    bind(aTargetSlot);
    gl::Util::setViewportAsActualPixels(size());
    gl::Util::resetRenderState();
    gl::Global::functions().glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl::Global::functions().glClear(GL_COLOR_BUFFER_BIT);
    drawQuad(hSlot.texture(), Kind_Blur, QList<int>(), v, 1.0f);
    return texture(aTargetSlot);
}

GLuint FilterFrame::waveTexture(int aTargetSlot, GLuint aSrcTexture, const WavePassParams& aWave) {
    bind(aTargetSlot);
    gl::Util::setViewportAsActualPixels(size());
    gl::Util::resetRenderState();
    gl::Global::functions().glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl::Global::functions().glClear(GL_COLOR_BUFFER_BIT);
    drawQuad(
        aSrcTexture, Kind_Wave, QList<int>(), BlurParams(), 1.0f, false, QSizeF(), QSizeF(), false,
        img::BlendMode_Normal, 0, &aWave
    );
    return texture(aTargetSlot);
}

GLuint FilterFrame::hsvTexture(
    int aTargetSlot, GLuint aSrcTexture, const QList<int>& aHSV, const MaskInput& aMask,
    const QMatrix4x4& aScreenToLayer
) {
    bind(aTargetSlot);
    gl::Util::setViewportAsActualPixels(size());
    gl::Util::resetRenderState();
    gl::Global::functions().glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl::Global::functions().glClear(GL_COLOR_BUFFER_BIT);
    drawQuad(
        aSrcTexture, Kind_HSV, aHSV, BlurParams(), 1.0f, true, QSizeF(), QSizeF(), false, img::BlendMode_Normal, 0,
        nullptr, aMask.isValid() ? &aMask : nullptr, aScreenToLayer
    );
    return texture(aTargetSlot);
}

// defined next to createShaders(); this pass needs it earlier
static void fBuildLayerDrawingVariant(
    gl::EasyShaderProgram& aShader, const QString& aBlendFunc, int aIsClippee, int aUseHSV, int aPremultiplied,
    int aNonSeparable, int aPremultipliedSrc, int aUseHsvMask
);

GLuint FilterFrame::copyTexture(int aTargetSlot, GLuint aSrcTexture) {
    bind(aTargetSlot);
    gl::Util::setViewportAsActualPixels(size());
    gl::Util::resetRenderState();
    gl::Global::functions().glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl::Global::functions().glClear(GL_COLOR_BUFFER_BIT);
    drawQuad(
        aSrcTexture, Kind_Resample, QList<int>(), BlurParams(), 1.0f, false, QSizeF(), QSizeF(), false,
        img::BlendMode_Normal, 0
    );
    return texture(aTargetSlot);
}

GLuint FilterFrame::tintTexture(
    int aTargetSlot, GLuint aSrcTexture, const MaskInput& aMask, const QMatrix4x4& aScreenToLayer,
    const QColor& aColor
) {
    bind(aTargetSlot);
    gl::Util::setViewportAsActualPixels(size());
    gl::Util::resetRenderState();
    gl::Global::functions().glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl::Global::functions().glClear(GL_COLOR_BUFFER_BIT);
    drawQuad(
        aSrcTexture, Kind_Tint, QList<int>(), BlurParams(), 1.0f, false, QSizeF(), QSizeF(), false,
        img::BlendMode_Normal, 0, nullptr, nullptr, aScreenToLayer, 0, nullptr, &aMask, aColor
    );
    return texture(aTargetSlot);
}

GLuint FilterFrame::maskTexture(
    int aTargetSlot, GLuint aSrcTexture, GLuint aMaskTexture, const QRectF& aMaskRect,
    const QMatrix4x4& aScreenToLayer
) {
    MaskInput mask;
    mask.texture = aMaskTexture;
    mask.rect = aMaskRect;
    mask.neutral = 1.0f;

    bind(aTargetSlot);
    gl::Util::setViewportAsActualPixels(size());
    gl::Util::resetRenderState();
    gl::Global::functions().glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl::Global::functions().glClear(GL_COLOR_BUFFER_BIT);
    drawQuad(
        aSrcTexture, Kind_LayerMask, QList<int>(), BlurParams(), 1.0f, false, QSizeF(), QSizeF(), false,
        img::BlendMode_Normal, 0, nullptr, nullptr, aScreenToLayer, 0, nullptr, nullptr, QColor(), &mask
    );
    return texture(aTargetSlot);
}

GLuint FilterFrame::maskOverlayTexture(
    int aTargetSlot, GLuint aSrcTexture, GLuint aMaskTexture, const QRectF& aMaskRect,
    const QMatrix4x4& aScreenToLayer, const QColor& aOverlayColor
) {
    MaskInput mask;
    mask.texture = aMaskTexture;
    mask.rect = aMaskRect;

    bind(aTargetSlot);
    gl::Util::setViewportAsActualPixels(size());
    gl::Util::resetRenderState();
    gl::Global::functions().glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl::Global::functions().glClear(GL_COLOR_BUFFER_BIT);
    drawQuad(
        aSrcTexture, Kind_MaskOverlay, QList<int>(), BlurParams(), 1.0f, false, QSizeF(), QSizeF(), false,
        img::BlendMode_Normal, 0, nullptr, nullptr, aScreenToLayer, 0, nullptr, nullptr, aOverlayColor, &mask
    );
    return texture(aTargetSlot);
}

GLuint FilterFrame::mixTexture(
    int aTargetSlot, GLuint aSharpTexture, GLuint aBlurredTexture, const MaskInput& aMask,
    const QMatrix4x4& aScreenToLayer
) {
    bind(aTargetSlot);
    gl::Util::setViewportAsActualPixels(size());
    gl::Util::resetRenderState();
    gl::Global::functions().glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl::Global::functions().glClear(GL_COLOR_BUFFER_BIT);
    drawQuad(
        aSharpTexture, Kind_Mix, QList<int>(), BlurParams(), 1.0f, false, QSizeF(), QSizeF(), false,
        img::BlendMode_Normal, 0, nullptr, nullptr, aScreenToLayer, aBlurredTexture, &aMask
    );
    return texture(aTargetSlot);
}

gl::EasyShaderProgram& FilterFrame::hsvMaskShader() {
    if (!mHsvMaskShader) {
        mHsvMaskShader.reset(new gl::EasyShaderProgram());
        fBuildLayerDrawingVariant(*mHsvMaskShader.data(), QString("BlendNormal"), 0, 1, 1, 0, 0, 1);
    }
    return *mHsvMaskShader;
}

void FilterFrame::createQuadBuffers() {
    static const GLuint kIndices[4] = {0, 1, 3, 2};
    mIndices.resetData(4, GL_STATIC_DRAW, kIndices);
}

static void fBuildLayerDrawingVariant(
    gl::EasyShaderProgram& aShader, const QString& aBlendFunc, int aIsClippee, int aUseHSV, int aPremultiplied,
    int aNonSeparable, int aPremultipliedSrc, int aUseHsvMask = 0
) {
    gl::ExtendShader source;
    if (!source.openFromFileVert(":/shaders/LayerDrawingVert.glsl")) {
        XC_FATAL_ERROR("FileIO Error", "Failed to open vertex shader file.", source.log());
    }
    if (!source.openFromFileFrag(":/shaders/LayerDrawingFrag.glsl")) {
        XC_FATAL_ERROR("FileIO Error", "Failed to open fragment shader file.", source.log());
    }
    source.setVariationValue("BLEND_FUNC", aBlendFunc);
    source.setVariationValue("BLEND_NONSEPARABLE", aNonSeparable ? "1" : "0");
    source.setVariationValue("BLEND_PREMULTIPLIED_SRC", aPremultipliedSrc ? "1" : "0");
    source.setVariationValue("IS_CLIPPEE", aIsClippee ? "1" : "0");
    source.setVariationValue("USE_HSV", aUseHSV ? "1" : "0");
    source.setVariationValue("USE_HSV_MASK", aUseHsvMask ? "1" : "0");
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
    build(Kind_Wave, kFullscreenVert, kWaveFrag);
    build(Kind_Mix, kFullscreenVert, kMixFrag);
    build(Kind_Tint, kFullscreenVert, kTintFrag);
    build(Kind_LayerMask, kFullscreenVert, kLayerMaskFrag);
    build(Kind_MaskOverlay, kFullscreenVert, kMaskOverlayFrag);

    // HSV pass: shared layer fragment shader with variations
    fBuildLayerDrawingVariant(mPassShaders[Kind_HSV], QString("BlendNormal"), 0, 1, 1, 0, 0);
}

gl::EasyShaderProgram& FilterFrame::presentShader(img::BlendMode aMode) {
    XC_ASSERT(aMode < img::BlendMode_TERM);
    if (!mPresentShaders[aMode]) {
        mPresentShaders[aMode].reset(new gl::EasyShaderProgram());
        auto shader = mPresentShaders[aMode].data();
        auto blendFunc = QString("Blend") + img::getBlendFuncNameFromBlendMode(aMode);
        fBuildLayerDrawingVariant(
            *shader, blendFunc, 0, 0, 1, img::isNonSeparableBlendMode(aMode) ? 1 : 0,
            img::isPremultipliedSrcBlendMode(aMode) ? 1 : 0
        );
    }
    return *mPresentShaders[aMode];
}

gl::EasyShaderProgram& FilterFrame::presentHSVShader(img::BlendMode aMode) {
    XC_ASSERT(aMode < img::BlendMode_TERM);
    if (!mPresentHSVShaders[aMode]) {
        mPresentHSVShaders[aMode].reset(new gl::EasyShaderProgram());
        auto shader = mPresentHSVShaders[aMode].data();
        auto blendFunc = QString("Blend") + img::getBlendFuncNameFromBlendMode(aMode);
        fBuildLayerDrawingVariant(
            *shader, blendFunc, 0, 1, 1, img::isNonSeparableBlendMode(aMode) ? 1 : 0,
            img::isPremultipliedSrcBlendMode(aMode) ? 1 : 0
        );
    }
    return *mPresentHSVShaders[aMode];
}

void FilterFrame::drawQuad(
    GLuint aSrcTexture, Kind aKind, const QList<int>& aHSV, const BlurParams& aBlur, float aOpacity,
    bool aNeedHSV, const QSizeF& aTargetSize, const QSizeF& aSrcSize, bool aFlipY, img::BlendMode aPresentBlend,
    GLuint aPresentDest, const WavePassParams* aWave, const MaskInput* aHsvMask, const QMatrix4x4& aScreenToLayer,
    GLuint aSecondTexture, const MaskInput* aMixMask, const MaskInput* aTintMask, const QColor& aTintColor,
    const MaskInput* aLayerMask
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
        ggl.glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
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
        // a blend-aware present texelFetches unit1 in LayerDrawingFrag.glsl; an unset
        // dest (0) would silently sample the default texture, so require the capture
        XC_ASSERT(aPresentBlend == img::BlendMode_Normal || aPresentDest != 0);
        ggl.glActiveTexture(GL_TEXTURE1);
        ggl.glBindTexture(GL_TEXTURE_2D, destTexture);
        ggl.glActiveTexture(GL_TEXTURE0);
    }

    // Presentation shader selection: when the composite content still needs its HSV
    // applied (a folder presenting its own keyed HSV), use the USE_HSV variant; otherwise
    // the content's HSV is already baked into the composite, so present with the per-mode
    // blend func and PREMULTIPLIED_INPUT (no identity-HSV round trip on the shader input).
    // A folder with BOTH its own HSV and a non-Normal blend mode needs the combined
    // USE_HSV + per-mode blend variant (the plain HSV pass is fixed to BlendNormal).
    gl::EasyShaderProgram* shader;
    if (aKind == Kind_HSV && aHsvMask) {
        // the per-pixel HSV adjustment; the mask's texture goes to unit 3 so the
        // present pass's unit 1 (the scene) is untouched
        shader = &hsvMaskShader();
    } else if (aKind == Kind_HSV && !aNeedHSV) {
        shader = &presentShader(aPresentBlend);
    } else if (aKind == Kind_HSV && aNeedHSV && aPresentBlend != img::BlendMode_Normal) {
        shader = &presentHSVShader(aPresentBlend);
    } else {
        shader = &mPassShaders[aKind];
    }
    shader->bind();
    shader->setAttributeArray("inPosition", positions.data(), 4);
    shader->setAttributeArray("inTexCoord", texCoords.data(), 4);

    QColor color(255, 255, 255, xc_clamp((int)(255 * aOpacity), 0, 255));
    shader->setUniformValue("uColor", color);
    shader->setUniformValue("uTexture", 0);
    shader->setUniformValue("uScreenSize", QSizeF(target));
    shader->setUniformValue("uImageSize", QSizeF(src));
    shader->setUniformValue("uTexCoordOffset", QVector2D(0, 0));

    if (aKind == Kind_HSV) {
        QMatrix4x4 ident;
        shader->setUniformValue("uViewMatrix", ident);
        shader->setUniformValue("uDestTexture", 1);
        if (aHsvMask) {
            ggl.glActiveTexture(GL_TEXTURE3);
            ggl.glBindTexture(GL_TEXTURE_2D, aHsvMask->texture);
            ggl.glActiveTexture(GL_TEXTURE0);
            shader->setUniformValue("uHsvMask", 3);
            shader->setUniformValue(
                "uHsvMaskRect",
                QVector4D(
                    float(aHsvMask->rect.x()), float(aHsvMask->rect.y()), float(aHsvMask->rect.width()),
                    float(aHsvMask->rect.height())
                )
            );
            shader->setUniformValue("uScreenToLayer", waveAffine3(aScreenToLayer));
        }
        if (aNeedHSV) {
            // per-pass HSV (folder presentation of its own key)
            shader->setUniformValue("setColor", bool(aHSV.size() > 3 ? aHSV[3] : 0));
            shader->setUniformValue("hue", (float)(aHSV.size() > 0 ? aHSV[0] : 0) / 360.0f);
            shader->setUniformValue("saturation", (float)(aHSV.size() > 1 ? aHSV[1] : 100) / 100.0f);
            shader->setUniformValue("value", (float)(aHSV.size() > 2 ? aHSV[2] : 100) / 100.0f);
        }
    } else if (aKind == Kind_Blur) {
        shader->setUniformValue("uTexelSize", QVector2D(1.0f / target.width(), 1.0f / target.height()));
        shader->setUniformValue("uBlurDirection", aBlur.direction);
        shader->setUniformValue("uBlurRadius", aBlur.radiusTexels);
    } else if (aKind == Kind_Mix) {
        // ONE branch: the bindings AND the uniforms (an earlier version split them,
        // and the second half sat in a dead `else if`, so the mix sampled the sharp
        // texture twice and every masked blur looked like "no blur anywhere")
        XC_PTR_ASSERT(aMixMask);
        ggl.glActiveTexture(GL_TEXTURE1);
        ggl.glBindTexture(GL_TEXTURE_2D, aSecondTexture);
        ggl.glActiveTexture(GL_TEXTURE2);
        ggl.glBindTexture(GL_TEXTURE_2D, aMixMask->texture);
        ggl.glActiveTexture(GL_TEXTURE0);
        shader->setUniformValue("uSecondTexture", 1);
        shader->setUniformValue("uMixMask", 2);
        shader->setUniformValue(
            "uMixMaskRect",
            QVector4D(
                float(aMixMask->rect.x()), float(aMixMask->rect.y()), float(aMixMask->rect.width()),
                float(aMixMask->rect.height())
            )
        );
        shader->setUniformValue("uScreenToLayer", waveAffine3(aScreenToLayer));
    } else if (aKind == Kind_Tint) {
        // the tint's field is the SAME channel every filter mask uses; `maskPreviewColor`
        // carries the tint
        XC_PTR_ASSERT(aTintMask);
        ggl.glActiveTexture(GL_TEXTURE1);
        ggl.glBindTexture(GL_TEXTURE_2D, aTintMask->texture);
        ggl.glActiveTexture(GL_TEXTURE0);
        shader->setUniformValue("uTintMask", 1);
        shader->setUniformValue(
            "uTintMaskRect",
            QVector4D(
                float(aTintMask->rect.x()), float(aTintMask->rect.y()), float(aTintMask->rect.width()),
                float(aTintMask->rect.height())
            )
        );
        shader->setUniformValue("uScreenToLayer", waveAffine3(aScreenToLayer));
        shader->setUniformValue(
            "uTintColor", QVector4D(aTintColor.redF(), aTintColor.greenF(), aTintColor.blueF(), aTintColor.alphaF())
        );
        shader->setUniformValue("uTintNeutral", aTintMask->neutral);
    } else if (aKind == Kind_MaskOverlay) {
        XC_PTR_ASSERT(aLayerMask);
        ggl.glActiveTexture(GL_TEXTURE1);
        ggl.glBindTexture(GL_TEXTURE_2D, aLayerMask->texture);
        ggl.glActiveTexture(GL_TEXTURE0);
        shader->setUniformValue("uLayerMask", 1);
        shader->setUniformValue(
            "uLayerMaskRect",
            QVector4D(
                float(aLayerMask->rect.x()), float(aLayerMask->rect.y()), float(aLayerMask->rect.width()),
                float(aLayerMask->rect.height())
            )
        );
        shader->setUniformValue("uScreenToLayer", waveAffine3(aScreenToLayer));
        shader->setUniformValue(
            "uOverlayColor", QVector4D(aTintColor.redF(), aTintColor.greenF(), aTintColor.blueF(), aTintColor.alphaF())
        );
    } else if (aKind == Kind_LayerMask) {
        XC_PTR_ASSERT(aLayerMask);
        ggl.glActiveTexture(GL_TEXTURE1);
        ggl.glBindTexture(GL_TEXTURE_2D, aLayerMask->texture);
        ggl.glActiveTexture(GL_TEXTURE0);
        shader->setUniformValue("uLayerMask", 1);
        shader->setUniformValue(
            "uLayerMaskRect",
            QVector4D(
                float(aLayerMask->rect.x()), float(aLayerMask->rect.y()), float(aLayerMask->rect.width()),
                float(aLayerMask->rect.height())
            )
        );
        shader->setUniformValue("uScreenToLayer", waveAffine3(aScreenToLayer));
    } else if (aKind == Kind_Wave) {
        XC_PTR_ASSERT(aWave);
        const WavePassParams& wave = *aWave;
        // The masks are separate textures: unit 0 is the composite. An absent mask
        // is not bound and its rect is empty, which is the shader's own "do not
        // sample" switch.
        if (aWave->amountMask) {
            ggl.glActiveTexture(GL_TEXTURE1);
            ggl.glBindTexture(GL_TEXTURE_2D, aWave->amountMask);
        }
        if (aWave->timeMask) {
            ggl.glActiveTexture(GL_TEXTURE2);
            ggl.glBindTexture(GL_TEXTURE_2D, aWave->timeMask);
        }
        ggl.glActiveTexture(GL_TEXTURE0);
        shader->setUniformValue("uLayerToScreen", waveAffine3(wave.layerToScreen));
        shader->setUniformValue("uScreenToLayer", waveAffine3(wave.screenToLayer));
        shader->setUniformValue("uClampMin", wave.clampMin);
        shader->setUniformValue("uClampMax", wave.clampMax);
        shader->setUniformValue(
            "uWaveA",
            QVector4D(
                wave.strength, wave.wavelength, qDegreesToRadians(wave.angleDeg), wave.phase
            )
        );
        shader->setUniformValue(
            "uWaveB",
            QVector4D(wave.mix, wave.crest, qDegreesToRadians(wave.dualAngleDeg), wave.dualScale)
        );
        shader->setUniformValue("uWaveC", QVector2D(wave.speed, wave.seconds));
        shader->setUniformValue("uWaveMode", wave.deform == WaveKey::Deform_Sway ? 1 : 0);
        shader->setUniformValue("uAmountMask", 1);
        shader->setUniformValue(
            "uAmountMaskRect",
            wave.amountMask ? QVector4D(
                float(wave.amountMaskRect.x()), float(wave.amountMaskRect.y()),
                float(wave.amountMaskRect.width()), float(wave.amountMaskRect.height())
            ) : QVector4D(0.0f, 0.0f, 0.0f, 0.0f)
        );
        shader->setUniformValue("uTimeMask", 2);
        shader->setUniformValue(
            "uTimeMaskRect",
            wave.timeMask ? QVector4D(
                float(wave.timeMaskRect.x()), float(wave.timeMaskRect.y()),
                float(wave.timeMaskRect.width()), float(wave.timeMaskRect.height())
            ) : QVector4D(0.0f, 0.0f, 0.0f, 0.0f)
        );
        shader->setUniformValue("uTimeMaskCycles", wave.timeMask ? wave.timeMaskCycles : 0.0f);
    }

    gl::Util::drawElements(GL_TRIANGLE_STRIP, GL_UNSIGNED_INT, mIndices);
    shader->release();

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
