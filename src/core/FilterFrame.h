#ifndef CORE_FILTERFRAME_H
#define CORE_FILTERFRAME_H

#include <QSize>
#include <array>
#include <vector>
#include <memory>
#include <QScopedPointer>
#include "gl/Framebuffer.h"
#include "gl/Texture.h"
#include "gl/BufferObject.h"
#include "gl/EasyShaderProgram.h"
#include "img/BlendMode.h"
#include "util/NonCopyable.h"

namespace core {

// Screen-sized RGBA render targets used by folder composite filters (HSV, blur, ...).
// Slots are acquired/released like a stack so that nested folder filters ping-pong
// through the pool without extra allocation.
class FilterFrame: private util::NonCopyable {
public:
    enum Kind {
        Kind_HSV, // presentation: applies per-pass HSV (folder key) and/or the given
                  // blend func against the captured scene
        Kind_Blur,
        Kind_Resample // premultiplied passthrough (bilinear) for blur ladder down/upsampling
    };

    // Largest radius rendered directly (two full-resolution passes). Larger radii go
    // through the downsample ladder so the tap count stays bounded.
    static constexpr float kDirectBlurRadius = 16.0f;
    static constexpr int kMaxLadderLevels = 8;

    // A blur whose blended radius is at or below this epsilon is treated as inactive
    // (no composite path). The value is an epsilon rather than a visible step so that
    // interpolating a blur amount from zero ramps in continuously: the shader's sigma
    // floor (see kBlurFrag) keeps kernels for radii below ~0.5 identity-like, so the
    // composite path engages visually unchanged and the smoothness is preserved.
    static constexpr float kMinActiveBlurRadius = 0.001f;

    // Number of halvings the blur ladder should use for the given radii and composite
    // size; 0 means the direct two-pass blur. Pure function of the inputs (the harness
    // replica computes the same value).
    static int blurLadderLevel(float aRadiusX, float aRadiusY, const QSize& aCompositeSize);

    struct BlurParams {
        QVector2D direction; // (1,0) = horizontal pass, (0,1) = vertical pass
        float radiusTexels;  // radius in texels along the pass direction
    };

    // RAII holder for a compositing slot: the slot is released when the holder
    // goes out of scope, so nested composite passes balance automatically.
    class ScopedSlot {
    public:
        explicit ScopedSlot(FilterFrame& aFrame): mFrame(&aFrame), mSlot(aFrame.acquire()) {}
        ~ScopedSlot() {
            if (mFrame)
                mFrame->release(mSlot);
        }
        ScopedSlot(const ScopedSlot&) = delete;
        ScopedSlot& operator=(const ScopedSlot&) = delete;

        int slot() const { return mSlot; }
        GLuint framebuffer() const { return mFrame->framebuffer(mSlot); }
        GLuint texture() const { return mFrame->texture(mSlot); }

    private:
        FilterFrame* mFrame;
        int mSlot;
    };

    FilterFrame();
    ~FilterFrame();

    void resize(const QSize& aSize);
    const QSize& size() const { return mSize; }

    void clearAll();

    int acquire();
    void release(int aSlot);

    GLuint framebuffer(int aSlot) const;
    GLuint texture(int aSlot) const;

    // blur ladder targets (level 1 = half size, ...); only valid after a ladder blur
    // has run (targets are created lazily; resize frees them). Used by the verification
    // harness.
    GLuint ladderTexture(int aLevel) const { return mTexturesL[aLevel - 1]->id(); }
    GLuint ladderTexture2(int aLevel) const { return mTexturesL2[aLevel - 1]->id(); }

    void bind(int aSlot);

    // Applies a separable Gaussian to the composite in aCompositeSlot and returns the
    // texture holding the result. The two passes run along aDirX/aDirY (unit vectors in
    // pixel space) with radii aRadiusX/aRadiusY: when the content is rotated by the world
    // transform the blur ellipse's principal axes are used, otherwise (1,0)/(0,1). Large
    // radii are handled through the downsample ladder (see blurLadderLevel); the composite
    // slot's content is overwritten by the final upsample, so the returned texture is
    // valid for the caller's presentation pass.
    GLuint blurApply(
        int aCompositeSlot, const QVector2D& aDirX, float aRadiusX, const QVector2D& aDirY, float aRadiusY);

    // draws srcTexture through the given filter into the currently bound framebuffer.
    // aNeedHSV requests per-pass HSV (folder presentation of its own keyed HSV, where the
    // composite content is still in plain RGB); when false the content's HSV is already
    // baked into the composite and the per-mode blend present (presentShader) is used, so
    // no identity-HSV round trip runs on the shader input.
    // aTargetSize/aSrcSize default to the frame size; the ladder passes them explicitly.
    // aFlipY mirrors the source vertically: the blur ladder uses it on the first
    // downsample and the last upsample so it works in image orientation (the composite
    // slot texture holds the scene y-flipped; a plain bilinear ladder is not
    // flip-invariant at the edges).
    void drawQuad(
        GLuint aSrcTexture, Kind aKind, const QList<int>& aHSV, const BlurParams& aBlur, float aOpacity,
        bool aNeedHSV = false, const QSizeF& aTargetSize = QSizeF(), const QSizeF& aSrcSize = QSizeF(),
        bool aFlipY = false, img::BlendMode aPresentBlend = img::BlendMode_Normal,
        GLuint aPresentDest = 0
    );

    // LAYER-LEVEL PRESENTATION for the blend-aware composite:
    // sets the shader to the given blend func, binds the real destination texture
    // (the scene capture) instead of the self dummy. Used by LayerNode for blurred
    // layers whose own blend mode must apply to the isolated blurred content.
    void drawBlendPresent(
        GLuint aSrcTexture, img::BlendMode aMode, GLuint aDestTexture, float aOpacity
    );

private:
    void createQuadBuffers();
    void createShaders();
    void appendSlot(const QSize& aSize);
    // creates ladder targets for levels 1..aLevel if not already present (level l is
    // mSize >> l); a size change frees all targets, so presence implies the right size
    void ensureLadder(int aLevel);

    // lazily compiled LayerDrawingFrag variant for the present pass above:
    // PREMULTIPLIED_INPUT=1 (the composite slot holds premultiplied pixels), the given
    // blend func, no per-pass HSV (a blurred layer's own HSV is already baked into its
    // composite), no clipping (content is pre-masked inside the composite).
    gl::EasyShaderProgram& presentShader(img::BlendMode aMode);

    GLuint ladderFramebuffer(int aLevel) const { return mFramebuffersL[aLevel - 1]->id(); }
    GLuint ladderFramebuffer2(int aLevel) const { return mFramebuffersL2[aLevel - 1]->id(); }

    // slots allocated on the FIRST acquire (lazy: a scene without filters allocates no
    // composite slots); the pool grows on demand because composite nesting depth is
    // unbounded (a deep folder/layer chain with active blur or HSV keys holds one slot
    // per level on the ladder path, two when direct-blurred)
    static const int kDefaultSlots = 8;

    QSize mSize;
    int mAcquired;
    // unique_ptr (not QScopedPointer) so the pool can grow without reallocating a
    // fixed-size array; the ladder texture pairs below stay fixed-size
    std::vector<std::unique_ptr<gl::Framebuffer>> mFramebuffers;
    std::vector<std::unique_ptr<gl::Texture>> mTextures;
    // blur ladder: two chains so the Gaussian can ping-pong at the lowest level,
    // level l is (mSize >> l); together they cost ~2/3 of one full-size buffer
    std::array<QScopedPointer<gl::Framebuffer>, kMaxLadderLevels> mFramebuffersL;
    std::array<QScopedPointer<gl::Texture>, kMaxLadderLevels> mTexturesL;
    std::array<QScopedPointer<gl::Framebuffer>, kMaxLadderLevels> mFramebuffersL2;
    std::array<QScopedPointer<gl::Texture>, kMaxLadderLevels> mTexturesL2;
    gl::EasyShaderProgram mPassShaders[3];
    // per-blend present variants (see presentShader)
    std::array<QScopedPointer<gl::EasyShaderProgram>, img::BlendMode_TERM> mPresentShaders;
    gl::BufferObject mIndices;
};

} // namespace core

#endif // CORE_FILTERFRAME_H
