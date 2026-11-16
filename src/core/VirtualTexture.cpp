#include <QElapsedTimer>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QVector2D>
#include <QVector4D>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include "core/ImageKey.h"
#include "core/MaskPyramid.h"
#include "core/VirtualTexture.h"
#include "gl/EasyShaderProgram.h"
#include "gl/FrameStats.h"
#include "gl/Global.h"
#include "gl/VertexArrayObject.h"

namespace core {

namespace {

    // the AE_VT_DEMAND diagnostics, defined with the other helpers at the end of this block
    bool demandDrawEnabled();
    float demandCullMargin();

    //------------------------------------------------------------------------------
    // WHERE A LANDING'S TIME GOES (AE_VT_LANDING_SPLIT=1)
    //------------------------------------------------------------------------------
    //
    // vt_frame_ms says how long a landing took; this says what spent it. A landing is three kinds
    // of work - the pool's bookkeeping (acquire: the hash entry, the LRU, and a GL texture
    // creation when the budget grows an atlas), the CPU FILL (premultiply + gutter, see
    // img::premultiply), and the TEXEL UPLOAD (one glTexSubImage2D per page) - plus the table
    // publish and the demand parse around them. A change to any one is only a landing-rate change
    // if it is measured against the others, so the split is a switch rather than an inference:
    // `other` is the frame total minus the parts (the registry loops, the timers themselves).
    struct LandingSplit {
        qint64 poolNs = 0;     // PagePool::beginFrame: the frame stamp, atlas age-out
        qint64 readNs = 0;     // DemandMap::consume: the non-blocking fence check + the map
        qint64 parseNs = 0;    // each resource's consumeDemand, over the mapped bytes
        qint64 clearNs = 0;    // DemandMap::beginFrame: the state save, the clear, the restore
        qint64 acquireNs = 0;  // per page: the pool's bookkeeping
        qint64 fillNs = 0;     // per page: the CPU fill
        qint64 uploadNs = 0;   // per page: the texel upload
        qint64 tableNs = 0;    // the page-table publishes
        int pages = 0;
    };
    LandingSplit sLanding;

    bool landingSplitEnabled() {
        static const bool enabled = qEnvironmentVariableIsSet("AE_VT_LANDING_SPLIT");
        return enabled;
    }

    //------------------------------------------------------------------------------
    // THE DEMAND PASS
    //------------------------------------------------------------------------------
    //
    // One triangle -> the set of pages the fragment shader may sample for it, marked in the
    // resource's demand map. It runs once per drawn layer, before the layer's own draw, on the
    // skinned position buffer the draw uses; it needs no readback of its own and produces no image.
    //
    // The level it computes is the level sampleLayer() will pick. The fragment shader derives
    // d(texel)/d(screen) from the interpolated uv; for an affine triangle that 2x2 matrix is the
    // inverse of the triangle's own d(screen)/d(texel), so the footprint's minor singular value is
    // 1 / s_max(screen Jacobian). Both sides round that log2 the same way and add the same bias -
    // see LayerDrawingFrag.glsl's sampleLayer, which is the authority on the formula.
    const char* const kDemandVertexShader = R"(#version 330
in vec3 inPosition;
in vec2 inTexCoord;
uniform mat4 uViewMatrix;
uniform vec2 uScreenSize;
// MASKS sample a screen->content affine, not the mesh's own uv (LayerSampling.glsl's
// sampleMask): the demand must name the pages THAT mapping reaches, so the vertex shader
// computes the mask's texel-space coordinate from the SAME matrix the fragment shader uses.
uniform int uUvFromScreen;
uniform mat4 uScreenToContent;
uniform vec4 uMaskRect;
uniform vec2 uMaskTexelSize;
out vec2 vScreen;
out vec2 vTexCoord;
void main() {
    // DEVICE pixels - the space the layer shader's derivatives are taken in, and exactly the
    // layer vertex shader's vDestCoord (the matrix uniforms are built for that convention).
    vec4 p = uViewMatrix * vec4(inPosition, 1.0);
    vec3 ndc = p.xyz / p.w;
    vScreen = (ndc.xy + 1.0) * uScreenSize * 0.5;
    if (uUvFromScreen != 0) {
        vec2 q = (uScreenToContent * vec4(vScreen, 0.0, 1.0)).xy;
        vTexCoord = ((q - uMaskRect.xy) / uMaskRect.zw) * uMaskTexelSize;
    } else {
        vTexCoord = inTexCoord;
    }
    gl_Position = vec4(0.0);
}
)";

    const char* const kDemandGeometryShader = R"(#version 330
layout(triangles) in;
layout(triangle_strip, max_vertices = 4) out;
in vec2 vScreen[];
in vec2 vTexCoord[];
uniform vec2 uLayerSize;
uniform float uBias;
uniform int uMaxLevel;
uniform int uCoarseLevel;
uniform vec2 uPageMapSize;
uniform int uPageLevelRow[16];
uniform float uMinUvArea;
uniform float uPageTexels;
uniform vec2 uTexCoordOffset;
uniform int uDemandLayer;
// the DRAW's target, in device pixels: the triangles outside it contribute no fragment, so they
// must not demand a page (see main)
uniform vec2 uScreenSize;
// a triangle this far outside the target is still marked; a value beyond any screen disables the
// cull (AE_VT_DEMAND_NOCULL, the measurement's control)
uniform float uCullMargin;

void emitPageRect(ivec2 aMin, ivec2 aMax, int aRow) {
    // the shared demand array: this resource's page grid is one layer of it
    gl_Layer = uDemandLayer;
    vec2 size = uPageMapSize;
    float x0 = 2.0 * float(aMin.x) / size.x - 1.0;
    float x1 = 2.0 * float(aMax.x + 1) / size.x - 1.0;
    float y0 = 2.0 * float(aRow + aMin.y) / size.y - 1.0;
    float y1 = 2.0 * float(aRow + aMax.y + 1) / size.y - 1.0;
    gl_Position = vec4(x0, y0, 0.0, 1.0); EmitVertex();
    gl_Position = vec4(x1, y0, 0.0, 1.0); EmitVertex();
    gl_Position = vec4(x0, y1, 0.0, 1.0); EmitVertex();
    gl_Position = vec4(x1, y1, 0.0, 1.0); EmitVertex();
    EndPrimitive();
}

void markLevel(int pageLevel, float aTapReach, vec2 t0, vec2 t1, vec2 t2) {
    if (pageLevel < 0 || pageLevel >= uCoarseLevel)
        return;
    int w = max(1, int(uLayerSize.x) >> pageLevel);
    int h = max(1, int(uLayerSize.y) >> pageLevel);
    ivec2 grid = ivec2((w + int(uPageTexels) - 1) / int(uPageTexels),
                       (h + int(uPageTexels) - 1) / int(uPageTexels));
    float levelTexels = exp2(float(pageLevel));
    float scale = levelTexels * uPageTexels;
    vec2 p0 = t0 / scale, p1 = t1 / scale, p2 = t2 / scale;
    vec2 reach = vec2(min(aTapReach, uPageTexels) / uPageTexels);
    vec2 lo = min(min(p0, p1), p2) - reach;
    vec2 hi = max(max(p0, p1), p2) + reach;
    ivec2 minP = ivec2(floor(lo));
    ivec2 maxP = ivec2(floor(hi));
    minP = max(minP, ivec2(0));
    maxP = min(maxP, grid - 1);
    if (minP.x > maxP.x || minP.y > maxP.y)
        return;
    emitPageRect(minP, maxP, uPageLevelRow[pageLevel]);
}

void main() {
    // the layer shader samples (inTexCoord + uTexCoordOffset) / uImageSize; the demand must
    // name the pages THOSE coordinates fall in, not the untranslated mesh uvs (a mesh origin
    // offset or an edited image offset moves the sample by a whole part of the image)
    vec2 t0 = vTexCoord[0] + uTexCoordOffset, t1 = vTexCoord[1] + uTexCoordOffset,
         t2 = vTexCoord[2] + uTexCoordOffset;
    vec2 d1 = t1 - t0, d2 = t2 - t0;
    float detU = d1.x * d2.y - d2.x * d1.y;
    float uvArea = abs(detU) * 0.5;
    // the same sliver rule as the density meter: a triangle under a quarter of a uv pixel has
    // no visible surface to own a page, and its ratios are float noise
    if (uvArea < uMinUvArea)
        return;

    // THE SCREEN-BOUNDS CULL. A triangle whose screen bounding box misses the target produces no
    // fragment, so it can demand nothing. Without this the walk's union is the LAYER - every
    // triangle marks its own uv box whatever the camera sees - which is harmless at a fit view
    // (the whole layer is visible) and ruinous at a magnified one: on nene at --zoom 2 the walk
    // demanded 5,834 pages of which the pool could land 217, so most of the view fell back to the
    // coarse chain (92,701 samples, max 90 from the full-resolution control). The affine path
    // clipped its quad to the screen for the same reason; a deformed layer has no quad, so the
    // walk culls per triangle. A margin keeps a triangle that straddles the border - the only one
    // that can produce a fragment on it - in the set.
    {
        vec2 lo = min(min(vScreen[0], vScreen[1]), vScreen[2]);
        vec2 hi = max(max(vScreen[0], vScreen[1]), vScreen[2]);
        if (hi.x < -uCullMargin || hi.y < -uCullMargin || lo.x > uScreenSize.x + uCullMargin
            || lo.y > uScreenSize.y + uCullMargin)
            return;
    }

    vec2 e1 = vScreen[1] - vScreen[0];
    vec2 e2 = vScreen[2] - vScreen[0];
    vec2 j1 = (e1 * d2.y - e2 * d1.y) / detU;
    vec2 j2 = (e2 * d1.x - e1 * d2.x) / detU;
    float a = dot(j1, j1) + dot(j2, j2);
    float det = j1.x * j2.y - j2.x * j1.y;
    float disc = max(a * a - 4.0 * det * det, 0.0);
    float smax = sqrt(max(0.5 * (a + sqrt(disc)), 0.0));
    float smin = sqrt(max(0.5 * (a - sqrt(disc)), 0.0));
    if (!(smax > 0.0))
        return;

    float lod = log2(1.0 / smax) + uBias;
    // the magnified path in the fragment shader returns before picking a level: it samples
    // level 0, so that is the page this triangle wants
    int lodLevel = (lod <= 0.0) ? 0 : int(clamp(floor(lod + 0.5), 0.0, float(uMaxLevel)));

    // MARK THE LEVEL THIS TRIANGLE COMPUTES, and only that level. The fragment shader's level
    // comes from derivatives; on a 2x2 quad that straddles two triangles it is a mix of the two
    // planes, so it rounds to one of THESE TRIANGLES' levels, and both triangles mark their own.
    // A one-level margin each side was built and measured: on nene it demanded 474 pages instead
    // of 79, and it did NOT improve the frame's difference from the full-resolution control
    // (mean 0.83 against 0.61 for this tighter set); the finer neighbour also costs four pages
    // for every one of this level. The measured residual is not a missing level - see the
    // design doc's open item.
    float aniso = smax / max(smin, 1.0e-6);
    markLevel(lodLevel, 0.5 * aniso + 1.0, t0, t1, t2);
}
)";

    const char* const kDemandFragmentShader = R"(#version 330
out float oDemand;
void main() { oDemand = 1.0; }
)";

    /// THE SHARED DEMAND MAP: one R8 layer of a 2D array per paged resource, one clear, one
    /// readback and one fence per FRAME however many resources there are.
    ///
    /// The per-resource map was the obvious shape, and the measurement killed it: on nene (105
    /// paged resources) 105 glGetTexImage calls and 105 fences cost ~40 ms of a 94 ms frame, while
    /// the geometry pass that marks them costs ~8 ms. One array makes the copy one command and the
    /// fence one; the parsing stays per resource, because the layers are read back in layer order
    /// and each resource knows its own grid.
    ///
    /// The array grows by reallocation, never by copy: it is cleared every frame, so a lost
    /// previous frame's marks are harmless (the demand is a set of page timestamps, not state the
    /// GPU must preserve). A resource keeps its layer index across a growth.
    class DemandMap {
    public:
        enum { kMaxLayers = 256 };

        struct Params {
            QSize layerSize;
            int coarseLevel = 0;
            int maxLevel = 0;
            float bias = 0.0f;
            int levelRow[VirtualTexture::kMaxPageLevels] = {};
            // a MASK's uv is the fragment shader's screen->content mapping, not the mesh's own
            // texture coordinates (see the vertex shader)
            bool uvFromScreen = false;
            QMatrix4x4 screenToContent;
            QVector4D maskRect;
            QVector2D maskTexelSize;
        };

        static DemandMap& instance() {
            static DemandMap* map = new DemandMap();
            return *map;
        }

        /// A layer for a resource's page grid; holes are reused. Reallocates the array when the
        /// grid is larger than the current cell or no layer is free.
        int allocateLayer(const QSize& aGrid) {
            if (!ensureBuilt())
                return -1;
            int layer = -1;
            if (!mFree.empty()) {
                layer = mFree.back();
                mFree.pop_back();
                mGrids[(size_t)layer] = aGrid;
            } else {
                if ((int)mGrids.size() >= kMaxLayers) {
                    qWarning("VirtualTexture: demand map is full at %d layers", kMaxLayers);
                    return -1;
                }
                layer = (int)mGrids.size();
                mGrids.push_back(aGrid);
            }
            ensureCapacity((int)mGrids.size(), aGrid);
            return layer;
        }

        void releaseLayer(int aLayer) {
            if (aLayer < 0 || aLayer >= (int)mGrids.size())
                return;
            mGrids[(size_t)aLayer] = QSize();
            mFree.push_back(aLayer);
        }

        QSize grid(int aLayer) const {
            if (aLayer < 0 || aLayer >= (int)mGrids.size())
                return QSize();
            return mGrids[(size_t)aLayer];
        }

        QSize cellSize() const { return mCell; }
        int capacity() const { return mCapacity; }
        int stride() const { return mCell.width(); }
        quint64 mappedFrame() const { return mMappedFrame; }
        bool isMapped() const { return mMapped != nullptr; }

        const uint8* layerData(int aLayer) const {
            if (!mMapped || aLayer < 0 || aLayer >= mCapacity)
                return nullptr;
            return mMapped + (size_t)aLayer * mCell.width() * mCell.height();
        }

        /// Opens the frame: the array is cleared ONCE for every resource. `mark` then only draws.
        void beginFrame(quint64 aFrame) {
            mFrame = aFrame;
            if (mClearedFrame == aFrame || !ensureBuilt())
                return;
            // no resource has allocated a layer yet: there is no array to clear
            if (mTexture == 0 || mCell.isEmpty()) {
                mClearedFrame = aFrame;
                return;
            }
            auto& ggl = gl::Global::functions();
            GLint prevDrawFbo = 0;
            GLint prevViewport[4] = {0, 0, 0, 0};
            GLboolean prevColorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
            ggl.glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
            ggl.glGetIntegerv(GL_VIEWPORT, prevViewport);
            ggl.glGetBooleanv(GL_COLOR_WRITEMASK, prevColorMask);

            bindFramebuffer();
            ggl.glViewport(0, 0, mCell.width(), mCell.height());
            ggl.glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            ggl.glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            ggl.glClear(GL_COLOR_BUFFER_BIT);

            ggl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prevDrawFbo);
            ggl.glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
            ggl.glColorMask(prevColorMask[0], prevColorMask[1], prevColorMask[2], prevColorMask[3]);
            mClearedFrame = aFrame;
        }

        /// Marks one resource's pages into its layer. `aViewMatrix` and `aDeviceSize` are the
        /// draw's own; the page map's NDC uses the ARRAY CELL, so the layer's sub-rect lands at
        /// its grid position and the rest stays cleared. The buffers are raw ids because a
        /// fullscreen pass has no vertex data of its own (see markFullscreen).
        bool mark(
            int aLayer,
            const QSize& aGrid,
            const Params& aParams,
            const QMatrix4x4& aViewMatrix,
            const QSize& aDeviceSize,
            const QVector2D& aTexCoordOffset,
            GLuint aPositions,
            const gl::Vector2* aTexCoords,
            int aVtxCount,
            GLuint aIndexBuffer,
            int aIndexCount
        ) {
            if (aLayer < 0 || aGrid.isEmpty() || aVtxCount <= 0 || aIndexCount < 3
                || (!aTexCoords && !aParams.uvFromScreen))
                return false;
            if (!ensureBuilt() || !ensureCapacity((int)mGrids.size(), aGrid))
                return false;

            auto& ggl = gl::Global::functions();
            GLint prevDrawFbo = 0;
            GLint prevViewport[4] = {0, 0, 0, 0};
            GLint prevVao = 0;
            GLint prevProgram = 0;
            GLboolean prevBlend = ggl.glIsEnabled(GL_BLEND);
            GLboolean prevDepth = ggl.glIsEnabled(GL_DEPTH_TEST);
            GLboolean prevScissor = ggl.glIsEnabled(GL_SCISSOR_TEST);
            GLboolean prevCull = ggl.glIsEnabled(GL_CULL_FACE);
            GLboolean prevColorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
            ggl.glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
            ggl.glGetIntegerv(GL_VIEWPORT, prevViewport);
            ggl.glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
            ggl.glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
            ggl.glGetBooleanv(GL_COLOR_WRITEMASK, prevColorMask);

            bindFramebuffer();
            ggl.glViewport(0, 0, mCell.width(), mCell.height());
            ggl.glDisable(GL_BLEND);
            ggl.glDisable(GL_DEPTH_TEST);
            ggl.glDisable(GL_SCISSOR_TEST);
            ggl.glDisable(GL_CULL_FACE);
            ggl.glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

            mProgram->bind();
            mProgram->setUniformValue("uViewMatrix", aViewMatrix);
            mProgram->setUniformValue(
                "uScreenSize", QVector2D((float)aDeviceSize.width(), (float)aDeviceSize.height())
            );
            mProgram->setUniformValue(
                "uLayerSize", QVector2D((float)aParams.layerSize.width(), (float)aParams.layerSize.height())
            );
            mProgram->setUniformValue("uBias", aParams.bias);
            mProgram->setUniformValue("uMaxLevel", aParams.maxLevel);
            mProgram->setUniformValue("uCoarseLevel", aParams.coarseLevel);
            mProgram->setUniformValue(
                "uPageMapSize", QVector2D((float)mCell.width(), (float)mCell.height())
            );
            mProgram->setUniformValueArray(
                "uPageLevelRow", aParams.levelRow, VirtualTexture::kMaxPageLevels
            );
            mProgram->setUniformValue("uMinUvArea", 0.25f);
            mProgram->setUniformValue("uPageTexels", (float)gl::PagePool::kPageSize);
            mProgram->setUniformValue("uTexCoordOffset", aTexCoordOffset);
            mProgram->setUniformValue("uDemandLayer", aLayer);
            mProgram->setUniformValue("uCullMargin", demandCullMargin());
            mProgram->setUniformValue("uUvFromScreen", aParams.uvFromScreen ? 1 : 0);
            mProgram->setUniformValue("uScreenToContent", aParams.screenToContent);
            mProgram->setUniformValue("uMaskRect", aParams.maskRect);
            mProgram->setUniformValue("uMaskTexelSize", aParams.maskTexelSize);

            mVAO->bind();
            ggl.glBindBuffer(GL_ARRAY_BUFFER, aPositions);
            ggl.glEnableVertexAttribArray(mPositionLocation);
            mProgram->setAttributeArray(mPositionLocation, GL_FLOAT, nullptr, 3, 0);
            if (aParams.uvFromScreen) {
                // the mask's uv is derived from the vertex's screen position; no per-vertex
                // texcoord exists to bind, and leaving a stale one enabled would feed the
                // attribute a buffer this draw did not write
                ggl.glDisableVertexAttribArray(mTexCoordLocation);
            } else {
                ggl.glBindBuffer(GL_ARRAY_BUFFER, mTexCoordVbo);
                ggl.glBufferData(
                    GL_ARRAY_BUFFER, (GLsizeiptr)aVtxCount * 2 * sizeof(GLfloat), aTexCoords,
                    GL_DYNAMIC_DRAW
                );
                ggl.glEnableVertexAttribArray(mTexCoordLocation);
                mProgram->setAttributeArray(mTexCoordLocation, GL_FLOAT, nullptr, 2, 0);
            }
            ggl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, aIndexBuffer);
            if (demandDrawEnabled())
                ggl.glDrawElements(GL_TRIANGLES, aIndexCount, GL_UNSIGNED_INT, nullptr);
            ggl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            mProgram->release();

            ggl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prevDrawFbo);
            ggl.glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
            ggl.glUseProgram((GLuint)prevProgram);
            ggl.glBindVertexArray((GLuint)prevVao);
            ggl.glColorMask(prevColorMask[0], prevColorMask[1], prevColorMask[2], prevColorMask[3]);
            const auto restore = [&ggl](GLenum aState, GLboolean aWasEnabled) {
                if (aWasEnabled)
                    ggl.glEnable(aState);
                else
                    ggl.glDisable(aState);
            };
            restore(GL_BLEND, prevBlend);
            restore(GL_DEPTH_TEST, prevDepth);
            restore(GL_SCISSOR_TEST, prevScissor);
            restore(GL_CULL_FACE, prevCull);
            mDrawFrame = mFrame;
            return true;
        }

        /// THE AFFINE DEMAND (a layer's mask and a folder's matte). There is no mesh to walk and
        /// no per-vertex uv: the pass covers its whole target, and its uv is the screen->content
        /// affine the fragment shader applies to the same coord. So the quad is the target rect
        /// in NDC and the vertex shader's uUvFromScreen branch derives every vertex's texel
        /// position from it - the SAME formula (and the same Jacobian) a layer's demand uses,
        /// rather than a second CPU implementation that could drift from the shader.
        bool markFullscreen(
            int aLayer, const QSize& aGrid, const Params& aParams, const QSize& aTargetSize,
            const QVector2D& aTexCoordOffset = QVector2D(0.0f, 0.0f)
        ) {
            if (!aParams.uvFromScreen || aTargetSize.isEmpty() || aGrid.isEmpty())
                return false;
            if (!ensureBuilt() || !ensureQuad())
                return false;
            // `aTexCoordOffset` is the DRAW's own texel offset (a layer's mesh origin and image
            // offset): the quad's uv comes from the screen mapping, which is in content pixels,
            // and the draw shifts that by the same amount - without it a layer whose image is
            // offset marks a page rect shifted by exactly that offset.
            return mark(
                aLayer, aGrid, aParams, QMatrix4x4(), aTargetSize, aTexCoordOffset,
                mQuadPositions, nullptr, 4, mQuadIndices, 6
            );
        }

        /// One copy of every layer into a PBO, one fence. Skipped when no layer drew this frame.
        void issue() {
            if (!ensureBuilt() || mDrawFrame != mFrame || mCapacity <= 0 || mIssuedFrame == mFrame)
                return;
            Slot& pending = mSlots[mWriteSlot];
            if (pending.pending)
                return;   // the previous copy has not been mapped yet
            auto& ggl = gl::Global::functions();
            ggl.glBindTexture(GL_TEXTURE_2D_ARRAY, mTexture);
            ggl.glBindBuffer(GL_PIXEL_PACK_BUFFER, pending.pbo);
            ggl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
            ggl.glGetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
            ggl.glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            ggl.glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
            pending.fence = ggl.glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            pending.pending = true;
            pending.frame = mFrame;
            mIssuedFrame = mFrame;
            mWriteSlot = (mWriteSlot + 1) % 2;
        }

        /// Maps the pending copy (optionally waiting for it). The pointer stays valid until
        /// endConsume(); callers parse their own layer in between.
        bool consume(bool aWait) {
            if (!ensureBuilt() || mMapped || mTexture == 0)
                return false;
            for (int i = 0; i < 2; ++i) {
                Slot& slot = mSlots[i];
                if (!slot.pending)
                    continue;
                auto& ggl = gl::Global::functions();
                GLenum status = GL_ALREADY_SIGNALED;
                if (slot.fence) {
                    if (aWait) {
                        for (int attempt = 0; attempt < 1000; ++attempt) {
                            status = ggl.glClientWaitSync(slot.fence, GL_SYNC_FLUSH_COMMANDS_BIT, 100000000);
                            if (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED
                                || status == GL_WAIT_FAILED)
                                break;
                        }
                    } else {
                        // A zero timeout is a poll by the spec, and this driver's implementation
                        // still surfaces the frame's submit here (measured 2.9-9.4 ms on the frames
                        // that carry a copy) - but REMOVING the call entirely changes the frame's
                        // ms by nothing (four alternating runs), so the time is the command buffer's
                        // flush, paid at the swap otherwise, not a wait to fix. The landing split's
                        // `read=` therefore reads larger than the landing actually costs; see the
                        // design doc's item 3.
                        status = ggl.glClientWaitSync(slot.fence, 0, 0);
                    }
                }
                if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED)
                    return false;
                ggl.glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.pbo);
                uint8* map = (uint8*)ggl.glMapBufferRange(
                    GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)mCell.width() * mCell.height() * mCapacity,
                    GL_MAP_READ_BIT
                );
                ggl.glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                if (!map)
                    return false;
                mMapped = map;
                mMappedSlot = i;
                mMappedFrame = slot.frame;
                return true;
            }
            return false;
        }

        void endConsume() {
            if (!mMapped || mMappedSlot < 0)
                return;
            auto& ggl = gl::Global::functions();
            Slot& slot = mSlots[mMappedSlot];
            ggl.glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.pbo);
            ggl.glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            ggl.glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            if (slot.fence) {
                ggl.glDeleteSync(slot.fence);
                slot.fence = nullptr;
            }
            slot.pending = false;
            mMapped = nullptr;
            mMappedSlot = -1;
        }

        private:
        DemandMap() {}

        bool ensureBuilt() {
            QOpenGLContext* context = QOpenGLContext::currentContext();
            if (!context)
                return false;
            if (context != mContext) {
                // a recreated context invalidates the program, the VAO and the array; the layers
                // and their grids survive on the CPU, the texture is rebuilt empty on demand
                mProgram.reset();
                mVAO.reset();
                mTexCoordVbo = 0;
                mQuadPositions = 0;
                mQuadIndices = 0;
                mTexture = 0;
                mFbo = 0;
                mClearedFrame = 0;
                mContext = context;
            }
            if (!mProgram) {
                std::unique_ptr<QOpenGLShaderProgram> program(new QOpenGLShaderProgram());
                bool ok = program->addShaderFromSourceCode(QOpenGLShader::Vertex, kDemandVertexShader);
                ok = ok && program->addShaderFromSourceCode(QOpenGLShader::Geometry, kDemandGeometryShader);
                ok = ok && program->addShaderFromSourceCode(QOpenGLShader::Fragment, kDemandFragmentShader);
                ok = ok && program->link();
                if (!ok) {
                    // demand failing must not take the renderer down: without demand the paged
                    // path degrades to the always-resident coarse chain, which is blur, not a hole
                    qWarning("VirtualTexture demand pass disabled: %s", qPrintable(program->log()));
                    return false;
                }
                mPositionLocation = program->attributeLocation("inPosition");
                mTexCoordLocation = program->attributeLocation("inTexCoord");
                if (mPositionLocation < 0 || mTexCoordLocation < 0) {
                    qWarning("VirtualTexture demand pass: attribute missing");
                    return false;
                }
                mProgram.reset(program.release());
                auto& ggl = gl::Global::functions();
                ggl.glGenFramebuffers(1, &mFbo);
                ggl.glGenBuffers(1, &mTexCoordVbo);
                mVAO.reset(new gl::VertexArrayObject());
            }
            return mProgram != nullptr;
        }

        /// The fullscreen quad markFullscreen draws: the target rect in NDC, two triangles. Its
        /// uv is DERIVED from the screen position in the shader, so there is no texcoord buffer.
        bool ensureQuad() {
            if (mQuadPositions != 0 && mQuadIndices != 0)
                return true;
            auto& ggl = gl::Global::functions();
            const GLfloat positions[4][3] = {
                {-1.0f, -1.0f, 0.0f}, {1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {-1.0f, 1.0f, 0.0f}
            };
            const GLuint indices[6] = {0, 1, 2, 0, 2, 3};
            if (mQuadPositions == 0) {
                ggl.glGenBuffers(1, &mQuadPositions);
                ggl.glBindBuffer(GL_ARRAY_BUFFER, mQuadPositions);
                ggl.glBufferData(GL_ARRAY_BUFFER, sizeof(positions), positions, GL_STATIC_DRAW);
                ggl.glBindBuffer(GL_ARRAY_BUFFER, 0);
            }
            if (mQuadIndices == 0) {
                ggl.glGenBuffers(1, &mQuadIndices);
                ggl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mQuadIndices);
                ggl.glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
                ggl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            }
            return mQuadPositions != 0 && mQuadIndices != 0;
        }

        bool ensureCapacity(int aLayers, const QSize& aGrid) {
            if (!ensureBuilt())
                return false;
            const QSize cell = mCell.expandedTo(aGrid);
            int capacity = mCapacity;
            while (capacity < aLayers)
                capacity = capacity ? capacity * 2 : 8;
            capacity = std::min(capacity, (int)kMaxLayers);
            if (mTexture != 0 && cell == mCell && capacity == mCapacity)
                return true;

            auto& ggl = gl::Global::functions();
            if (mMapped) {
                // a reallocation while a copy is mapped: close it first
                endConsume();
            }
            for (Slot& slot : mSlots) {
                if (slot.fence) {
                    ggl.glDeleteSync(slot.fence);
                    slot.fence = nullptr;
                }
                if (slot.pbo) {
                    ggl.glDeleteBuffers(1, &slot.pbo);
                    slot.pbo = 0;
                }
                slot.pending = false;
            }
            if (mTexture != 0)
                ggl.glDeleteTextures(1, &mTexture);
            mTexture = 0;
            mCell = cell;
            mCapacity = capacity;
            if (mCell.isEmpty() || mCapacity <= 0)
                return false;

            ggl.glGenTextures(1, &mTexture);
            ggl.glBindTexture(GL_TEXTURE_2D_ARRAY, mTexture);
            ggl.glTexImage3D(
                GL_TEXTURE_2D_ARRAY, 0, GL_R8, mCell.width(), mCell.height(), mCapacity, 0, GL_RED,
                GL_UNSIGNED_BYTE, nullptr
            );
            ggl.glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            ggl.glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            ggl.glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            ggl.glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            ggl.glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

            ggl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mFbo);
            ggl.glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, mTexture, 0);
            ggl.glDrawBuffer(GL_COLOR_ATTACHMENT0);
            const bool complete = ggl.glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
            ggl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            if (!complete) {
                qWarning("VirtualTexture: layered demand framebuffer is incomplete");
                ggl.glDeleteTextures(1, &mTexture);
                mTexture = 0;
                return false;
            }

            const GLsizeiptr bytes = (GLsizeiptr)mCell.width() * mCell.height() * mCapacity;
            for (Slot& slot : mSlots) {
                ggl.glGenBuffers(1, &slot.pbo);
                ggl.glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.pbo);
                ggl.glBufferData(GL_PIXEL_PACK_BUFFER, bytes, nullptr, GL_STREAM_READ);
                ggl.glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            }
            mClearedFrame = 0;
            return true;
        }

        void bindFramebuffer() {
            auto& ggl = gl::Global::functions();
            ggl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mFbo);
            ggl.glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, mTexture, 0);
            ggl.glDrawBuffer(GL_COLOR_ATTACHMENT0);
        }

        QOpenGLContext* mContext = nullptr;
        std::unique_ptr<QOpenGLShaderProgram> mProgram;
        std::unique_ptr<gl::VertexArrayObject> mVAO;
        GLuint mTexCoordVbo = 0;
        GLuint mQuadPositions = 0;
        GLuint mQuadIndices = 0;
        GLuint mTexture = 0;
        GLuint mFbo = 0;
        int mPositionLocation = -1;
        int mTexCoordLocation = -1;

        QSize mCell;
        int mCapacity = 0;
        std::vector<QSize> mGrids;
        std::vector<int> mFree;
        quint64 mFrame = 0;
        quint64 mClearedFrame = 0;
        quint64 mDrawFrame = 0;
        quint64 mIssuedFrame = 0;
        struct Slot {
            GLuint pbo = 0;
            GLsync fence = nullptr;
            bool pending = false;
            quint64 frame = 0;
        };
        Slot mSlots[2];
        int mWriteSlot = 0;
        const uint8* mMapped = nullptr;
        int mMappedSlot = -1;
        quint64 mMappedFrame = 0;
    };

    //------------------------------------------------------------------------------

    /// A DIAGNOSTIC, not a policy: with AE_VT_DEMAND=0 the demand pass and its readbacks are
/// skipped, so every paged layer samples the coarse chain through the VT shader. That is the
/// control for "is the cost the shader or the demand machinery" - both were measured with it.
bool demandEnabled() {
    static const QByteArray mode = qgetenv("AE_VT_DEMAND");
    return mode != "0";
}

/// The draw alone, for the second diagnostic form: AE_VT_DEMAND=nodraw keeps the pass's state
/// changes, uniforms and per-layer buffer uploads and issues no glDrawElements. Measured on nene's
/// 130 layers: 91.2-92.4 ms a frame against 91.1-91.8 with the whole machinery off and 96.8-102.3
/// with it, so the pass's cost IS the geometry - batching its setup would buy nothing.
bool demandDrawEnabled() {
    static const QByteArray mode = qgetenv("AE_VT_DEMAND");
    return mode != "nodraw";
}

/// The screen-bounds cull's margin, read once. AE_VT_DEMAND_NOCULL=1 puts it beyond any screen, so
/// every triangle marks its uv box whatever the camera sees - the measurement's control for "how
/// much of the walk's demand was off-screen" (see the cull in the geometry shader).
float demandCullMargin() {
    static const bool noCull = qEnvironmentVariableIsSet("AE_VT_DEMAND_NOCULL");
    return noCull ? 1.0e9f : 1.0f;
}

/// WHICH ENTRY POINT A SIGNATURE CAME FROM (see DemandSignature::kind). A resource is marked by
/// one of them - an image by the layer walk or by its affine quad, a matte (layer mask or folder)
/// by the fullscreen quad - and the kind is compared so one form can never replay another's page
/// set.
enum DemandKind {
    DemandKind_Layer = 0,
    DemandKind_MaskScreen = 2,
    // an IMAGE whose layer has no deformation: its uv is one affine function of the screen, so
    // its demand is a QUAD through the demand map rather than a walk over its grid (AE_VT_AFFINE_DEMAND)
    DemandKind_LayerScreen = 3,
};

/// AE_VT_VERIFY_PAGES' mode, read once: "table" checks the GPU page table against its CPU mirror,
/// anything else also compares every resident page's slot with the page's own bytes (slow - it
/// stalls per page, which changes the frame's timing; see the design doc's first open item).
/// "end" is that same comparison, run once after the last frame instead of inside each one.
const QByteArray& verifyMode() {
    static const QByteArray mode = qgetenv("AE_VT_VERIFY_PAGES");
    return mode;
}

/// Set only between debugVerifyAfterRun's first and last resource: the slot comparison runs for
/// mode "end" then and at no other time.
bool sVerifyAfterRun = false;

/// THE FRAME'S DEMAND BOOKKEEPING: one mark is one layer's triangles walked by the demand
/// geometry shader; one replay is a signature repeat that walks nothing (see markDemand). Both
/// reset in beginFrame, so the pair reads as "how did this frame get its demand" - what
/// AE_VT_STATS prints and what the replay's gate asserts.
int sDemandMarks = 0;
int sDemandReplays = 0;

std::vector<VirtualTexture*>& registry() {
        static std::vector<VirtualTexture*> resources;
        return resources;
    }

    void registerResource(VirtualTexture* aResource) {
        auto& all = registry();
        if (std::find(all.begin(), all.end(), aResource) == all.end())
            all.push_back(aResource);
    }

    void unregisterResource(VirtualTexture* aResource) {
        auto& all = registry();
        all.erase(std::remove(all.begin(), all.end(), aResource), all.end());
    }

} // namespace

bool VirtualTexture::pageable(const QSize& aSize) {
    if (!gl::PagePool::enabled() || aSize.isEmpty())
        return false;
    return std::max(aSize.width(), aSize.height()) > gl::PagePool::coarseSize();
}

int VirtualTexture::instanceCount() { return (int)registry().size(); }
int VirtualTexture::demandMarks() { return sDemandMarks; }
int VirtualTexture::demandReplays() { return sDemandReplays; }

VirtualTexture::VirtualTexture(): mResource(), mSize(), mPyramid(), mCoarse(), mTable() {}
VirtualTexture::~VirtualTexture() { destroy(); }

void VirtualTexture::beginFrame() {
    if (!gl::PagePool::enabled())
        return;
    // THE LANDING COST, measured only while the counter is on (one enabled() branch otherwise):
    // the demand parse, the page fills and their uploads, and the table publishes. `ms` includes
    // it; vt_frame_ms is what a landing strategy has to divide the bytes by (see FrameStats.h).
    gl::FrameStats& frameStats = gl::FrameStats::instance();
    const bool split = landingSplitEnabled();
    QElapsedTimer timer; // the whole landing: vt_frame_ms, and the split's own total
    if (frameStats.enabled() || split)
        timer.start();
    // a SECOND timer, restarted per phase, so the phases never disturb the total they divide
    QElapsedTimer phase;
    if (split) {
        phase.start();
        sLanding = LandingSplit();
    }
    // First the previous frame's demand copy (it updates what each page last wanted), then the
    // clear that starts this frame's map, then the uploads: a page about to be sampled goes in
    // before the frame's draws.
    if (split)
        phase.restart(); // the landing's own time starts here, not at the instrument's start
    gl::PagePool::instance().beginFrame();
    if (split)
        sLanding.poolNs += phase.nsecsElapsed();
    sDemandMarks = 0;
    sDemandReplays = 0;
    DemandMap& demandMap = DemandMap::instance();
    if (split)
        phase.restart();
    const bool consumed = demandMap.consume(false);
    if (split) {
        sLanding.readNs += phase.nsecsElapsed();
        phase.restart();
    }
    if (consumed) {
        for (VirtualTexture* resource : registry())
            resource->consumeDemand();
        demandMap.endConsume();
    }
    if (split)
        sLanding.parseNs += phase.nsecsElapsed();
    if (split)
        phase.restart();
    demandMap.beginFrame(gl::PagePool::instance().frame());
    if (split)
        sLanding.clearNs += phase.nsecsElapsed();
    // THE STAGE IS OPEN FOR THE WHOLE LANDING, ACROSS RESOURCES: every page any resource uploads
    // this frame is filled into the same mapped buffer, and closed once, after the last one.
    {
        QElapsedTimer stagingTimer;
        if (split)
            stagingTimer.start();
        gl::PagePool::instance().beginStaging();
        if (split)
            sLanding.uploadNs += stagingTimer.nsecsElapsed();
    }
    for (VirtualTexture* resource : registry())
        resource->resolveDemand();
    {
        QElapsedTimer stagingTimer;
        if (split)
            stagingTimer.start();
        gl::PagePool::instance().endStaging();
        if (split)
            sLanding.uploadNs += stagingTimer.nsecsElapsed();
    }
    // TABLES GO OUT AFTER EVERY UPLOAD, not at the end of each resource: an upload for resource
    // B can evict a page of resource A, and A's table was already rebuilt by then. Publishing in
    // a second pass means no table in the frame names a slot some other resource has taken.
    for (VirtualTexture* resource : registry())
        resource->flushTable();
    const double totalMs = timer.nsecsElapsed() / 1000000.0;
    if (frameStats.enabled())
        frameStats.virtualTextureFrameTime(totalMs);
    if (split && sLanding.pages > 0) {
        const double partMs = (sLanding.poolNs + sLanding.readNs + sLanding.parseNs
                               + sLanding.clearNs + sLanding.acquireNs + sLanding.fillNs
                               + sLanding.uploadNs + sLanding.tableNs)
            / 1000000.0;
        qWarning(
            "VTLANDING frame=%llu pages=%d staged=%d pool=%.2f read=%.2f parse=%.2f clear=%.2f "
            "acquire=%.2f fill=%.2f upload=%.2f table=%.2f other=%.2f total=%.2f",
            (unsigned long long)gl::PagePool::instance().frame(), sLanding.pages,
            gl::PagePool::instance().stagedPages(),
            sLanding.poolNs / 1000000.0, sLanding.readNs / 1000000.0,
            sLanding.parseNs / 1000000.0, sLanding.clearNs / 1000000.0,
            sLanding.acquireNs / 1000000.0, sLanding.fillNs / 1000000.0,
            sLanding.uploadNs / 1000000.0, sLanding.tableNs / 1000000.0,
            totalMs - partMs, totalMs
        );
    }
}

void VirtualTexture::endFrame() {
    if (!gl::PagePool::enabled())
        return;
    DemandMap::instance().issue();
    gl::PagePool::instance().endFrame();
    // AFTER the frame: the pool's slots were sampled by the draws just recorded, so this is the
    // state a wrong-content read would have read (AE_VT_VERIFY_PAGES; a no-op without it).
    for (VirtualTexture* resource : registry())
        resource->debugVerifyPages();
    if (qEnvironmentVariableIsSet("AE_VT_STATS")) {
        int wanted = 0, resident = 0;
        for (VirtualTexture* resource : registry()) {
            wanted += resource->wantedPageCount();
            resident += resource->residentWantedCount();
        }
        qWarning(
            "VT frame %llu resources=%d wanted=%d resident=%d marks=%d replays=%d poolPages=%d "
            "poolBytes=%lld",
            (unsigned long long)gl::PagePool::instance().frame(),
            (int)registry().size(),
            wanted,
            resident,
            sDemandMarks,
            sDemandReplays,
            gl::PagePool::instance().residentPages(),
            (long long)gl::PagePool::instance().residentBytes()
        );
    }
}

void VirtualTexture::synchronize() {
    if (!gl::PagePool::enabled())
        return;
    DemandMap& demandMap = DemandMap::instance();
    demandMap.issue();
    if (demandMap.consume(true)) {
        for (VirtualTexture* resource : registry())
            resource->consumeDemand();
        demandMap.endConsume();
    }
    gl::PagePool::instance().beginStaging();
    for (VirtualTexture* resource : registry())
        resource->resolveDemand();
    gl::PagePool::instance().endStaging();
    for (VirtualTexture* resource : registry())
        resource->flushTable();
}

int VirtualTexture::pageGridX(int aLevel) const {
    if (aLevel < 0 || aLevel >= mCoarseLevel)
        return 0;
    return mLevelGridX[aLevel];
}

int VirtualTexture::pageGridY(int aLevel) const {
    if (aLevel < 0 || aLevel >= mCoarseLevel)
        return 0;
    return mLevelGridY[aLevel];
}

int VirtualTexture::pageCount() const { return mTableWidth * mTableRows; }

int VirtualTexture::pageIndex(int aLevel, int aPageX, int aPageY) const {
    XC_ASSERT(0 <= aLevel && aLevel < mCoarseLevel);
    return (mLevelRow[aLevel] + aPageY) * mTableWidth + aPageX;
}

gl::PagePool::PageId VirtualTexture::pageId(int aLevel, int aPageX, int aPageY) const {
    gl::PagePool::PageId id;
    id.owner = (quint64)(uintptr_t)this;
    id.level = aLevel;
    id.x = aPageX;
    id.y = aPageY;
    return id;
}

int VirtualTexture::levelOfIndex(int aIndex, int* aPageX, int* aPageY) const {
    const int row = aIndex / mTableWidth;
    for (int l = 0; l < mCoarseLevel; ++l) {
        if (row >= mLevelRow[l] && row < mLevelRow[l] + mLevelGridY[l]) {
            if (aPageX)
                *aPageX = aIndex % mTableWidth;
            if (aPageY)
                *aPageY = row - mLevelRow[l];
            return l;
        }
    }
    if (aPageX)
        *aPageX = 0;
    if (aPageY)
        *aPageY = 0;
    return 0;
}

gl::PagePool::PageId VirtualTexture::pageIdForIndex(int aIndex) const {
    int px = 0, py = 0;
    const int level = levelOfIndex(aIndex, &px, &py);
    return pageId(level, px, py);
}

bool VirtualTexture::build(const img::ResourceHandle& aResource, const QSize& aSize, gl::Texture& aFullChain) {
    destroy();
    if (!pageable(aSize) || aFullChain.id() == 0 || aFullChain.size() != aSize)
        return false;
    if (!gl::PagePool::enabled())
        return false;

    mResource = aResource;
    mSize = aSize;
    mMaxLevel = aFullChain.mipmapLevels() - 1;
    if (mMaxLevel < 1) {
        mSize = QSize();
        return false;
    }

    // the coarse level: the first level whose longest side fits the coarse threshold. Every
    // level from it down to 1x1 is resident as a plain mipmapped texture; the sharper levels
    // above it are paged.
    mCoarseLevel = 0;
    QSize coarseSize = aSize;
    while (std::max(coarseSize.width(), coarseSize.height()) > gl::PagePool::coarseSize() && mCoarseLevel < mMaxLevel) {
        coarseSize = QSize(std::max(1, coarseSize.width() >> 1), std::max(1, coarseSize.height() >> 1));
        ++mCoarseLevel;
    }
    if (mCoarseLevel < 1 || mCoarseLevel >= kMaxPageLevels) {
        mSize = QSize();
        return false;
    }

    // Capture EVERY level, the coarse tail included: the tail is small, and building the coarse
    // texture from the same bytes the pages read is what makes the fallback the same filter on
    // the same data. The tail is dropped from the pyramid as soon as the coarse texture has it.
    if (!mPyramid.capture(aFullChain, mMaxLevel)) {
        mSize = QSize();
        return false;
    }
    if (!buildCoarse(aFullChain)) {
        mPyramid.clear();
        mSize = QSize();
        return false;
    }
    mPyramid.truncate(mCoarseLevel - 1);
    if (!createTargets()) {
        destroy();
        return false;
    }

    registerResource(this);
    if (qEnvironmentVariableIsSet("AE_VT_HASH")) {
        const int level = qEnvironmentVariableIntValue("AE_VT_HASH");
        qWarning("VTHASH %dx%d level=%d pyramid=%016llx", mSize.width(), mSize.height(), level,
                 (unsigned long long)mPyramid.hashLevel(level));
    }
    return true;
}

bool VirtualTexture::buildMask(MaskPyramid& aPyramid) {
    destroy();
    if (!aPyramid.isValid() || !pageable(aPyramid.size()) || !gl::PagePool::enabled())
        return false;

    mFormat = gl::PagePool::Format_R8;
    mMaskPyramid = &aPyramid;
    mSize = aPyramid.size();
    mMaxLevel = aPyramid.levelCount() - 1;
    if (mMaxLevel < 1) {
        mMaskPyramid = nullptr;
        mSize = QSize();
        return false;
    }

    mCoarseLevel = 0;
    QSize coarseSize = mSize;
    while (std::max(coarseSize.width(), coarseSize.height()) > gl::PagePool::coarseSize()
           && mCoarseLevel < mMaxLevel) {
        coarseSize = QSize(std::max(1, coarseSize.width() >> 1), std::max(1, coarseSize.height() >> 1));
        ++mCoarseLevel;
    }
    if (mCoarseLevel < 1 || mCoarseLevel >= kMaxPageLevels) {
        qWarning("VT mask: coarse level %d out of range", mCoarseLevel);
        mMaskPyramid = nullptr;
        mSize = QSize();
        mFormat = gl::PagePool::Format_RGBA8;
        return false;
    }
    if (!buildMaskCoarse()) {
        qWarning("VT mask: coarse build failed (coarse %d of %d)", mCoarseLevel, mMaxLevel);
        mMaskPyramid = nullptr;
        mSize = QSize();
        mFormat = gl::PagePool::Format_RGBA8;
        return false;
    }
    if (!createTargets()) {
        qWarning("VT mask: targets failed (table %dx%d)", mTableWidth, mTableRows);
        destroy();
        return false;
    }
    registerResource(this);
    return true;
}

bool VirtualTexture::buildCoarse(gl::Texture& aFullChain) {
    (void)aFullChain;
    const QSize coarseSize = mPyramid.levelSize(mCoarseLevel);
    XC_ASSERT(!coarseSize.isEmpty());

    // The first coarse level goes in through create()'s Manual allocation; the rest are
    // uploaded level by level. Manual is the policy whose point is "the caller owns every
    // level" - and every one of them is a level of the chain that was just captured, not a
    // rebuild, so the fallback and the control path cannot disagree about a texel.
    // THE PIXEL-STORE STATE IS SAVED AND RESTORED. This runs at load, between the chain builds of
    // the resources around it; leaving UNPACK_ALIGNMENT at 1 (which every row here needs) was
    // measured to change the NEXT chain's mip tail: the chain hashes of a project differ across two
    // paged runs and are identical across two full-resolution ones (design doc, Measured).
    auto& gglState = gl::Global::functions();
    GLint prevUnpackAlignment = 4;
    GLint prevUnpackRowLength = 0;
    gglState.glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevUnpackAlignment);
    gglState.glGetIntegerv(GL_UNPACK_ROW_LENGTH, &prevUnpackRowLength);

    std::vector<uint8> levelBytes((size_t)coarseSize.width() * coarseSize.height() * 4);
    mPyramid.copyLevel(mCoarseLevel, levelBytes.data(), coarseSize);

    mCoarse.create(coarseSize, levelBytes.data(), GL_RGBA, GL_RGBA8, GL_UNSIGNED_BYTE, gl::Texture::Manual);
    if (mCoarse.id() == 0)
        return false;

    auto& ggl = gl::Global::functions();
    for (int rel = 1; rel <= mMaxLevel - mCoarseLevel; ++rel) {
        const QSize levelSize = mPyramid.levelSize(mCoarseLevel + rel);
        levelBytes.resize((size_t)levelSize.width() * levelSize.height() * 4);
        mPyramid.copyLevel(mCoarseLevel + rel, levelBytes.data(), levelSize);
        ggl.glBindTexture(GL_TEXTURE_2D, mCoarse.id());
        ggl.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        ggl.glTexSubImage2D(
            GL_TEXTURE_2D,
            rel,
            0,
            0,
            levelSize.width(),
            levelSize.height(),
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            levelBytes.data()
        );
        ggl.glBindTexture(GL_TEXTURE_2D, 0);
    }

    // The full texture's own sampling policy: trilinear minification, LINEAR magnification,
    // transparent outside the content. The shader samples a specific level with textureLod, so
    // the level blend never runs - what matters is the bilinear read inside a level, which is
    // the same read a page gets, and the border colour at the content's edge.
    mCoarse.setFilter(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR);
    mCoarse.setWrap(GL_CLAMP_TO_BORDER, QColor(0, 0, 0, 0));
    mCoarse.setLodBias(0.0f);

    gglState.glPixelStorei(GL_UNPACK_ALIGNMENT, prevUnpackAlignment);
    gglState.glPixelStorei(GL_UNPACK_ROW_LENGTH, prevUnpackRowLength);
    return true;
}

bool VirtualTexture::buildMaskCoarse() {
    XC_PTR_ASSERT(mMaskPyramid);
    const QSize coarseSize = mMaskPyramid->levelSize(mCoarseLevel);
    if (coarseSize.isEmpty())
        return false;

    // THE SAME BYTES THE PAGES READ: the coarse tier is copied out of the pyramid, not rebuilt,
    // so a page miss and a page hit are the same field - the same rule the image's coarse chain
    // follows (buildCoarse).
    std::vector<uint8> levelBytes((size_t)coarseSize.width() * coarseSize.height());
    mMaskPyramid->copyLevel(mCoarseLevel, levelBytes.data());
    auto& ggl = gl::Global::functions();
    // BEFORE the level-0 upload, not after it: an R8 row of 450 texels is not a multiple of the
    // default 4-byte alignment, and glTexImage2D would skew every row by the padding (measured:
    // the coarse held the field shifted by two texels per row)
    ggl.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    mCoarse.create(
        coarseSize, levelBytes.data(), GL_RED, GL_R8, GL_UNSIGNED_BYTE, gl::Texture::Manual
    );
    if (mCoarse.id() == 0) {
        ggl.glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        return false;
    }

    for (int rel = 1; rel <= mMaxLevel - mCoarseLevel; ++rel) {
        const QSize levelSize = mMaskPyramid->levelSize(mCoarseLevel + rel);
        levelBytes.resize((size_t)levelSize.width() * levelSize.height());
        mMaskPyramid->copyLevel(mCoarseLevel + rel, levelBytes.data());
        ggl.glBindTexture(GL_TEXTURE_2D, mCoarse.id());
        ggl.glTexSubImage2D(
            GL_TEXTURE_2D, rel, 0, 0, levelSize.width(), levelSize.height(), GL_RED,
            GL_UNSIGNED_BYTE, levelBytes.data()
        );
        ggl.glBindTexture(GL_TEXTURE_2D, 0);
    }
    ggl.glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    // The mask's chain is sampled GL_CLAMP_TO_EDGE (MaskMipChain::create), and the page gutter
    // reproduces that at the level's border (MaskPyramid::fillPage): a zero border here would
    // make the fallback differ from the page path at every field edge.
    mCoarse.setFilter(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR);
    mCoarse.setWrap(GL_CLAMP_TO_EDGE);
    mCoarse.setLodBias(0.0f);
    return true;
}

bool VirtualTexture::createTargets() {
    mTableWidth = 0;
    mTableRows = 0;
    for (int l = 0; l < mCoarseLevel; ++l) {
        // the level's own grid, from whichever source holds the levels: the image's capture or
        // the mask's CPU pyramid
        const QSize size = isMask() ? mMaskPyramid->levelSize(l) : mPyramid.levelSize(l);
        mLevelGridX[l] = (size.width() + gl::PagePool::kPageSize - 1) / gl::PagePool::kPageSize;
        mLevelGridY[l] = (size.height() + gl::PagePool::kPageSize - 1) / gl::PagePool::kPageSize;
        mLevelRow[l] = mTableRows;
        mTableRows += mLevelGridY[l];
        mTableWidth = std::max(mTableWidth, mLevelGridX[l]);
    }
    if (mTableWidth <= 0 || mTableRows <= 0)
        return false;

    const int count = mTableWidth * mTableRows;
    mEntries.assign((size_t)count, 0);
    mDemandFrame.assign((size_t)count, 0);
    mActiveFlags.assign((size_t)count, 0);
    mActivePages.clear();
    mActivePages.reserve(512);
    mPageBuffer.resize((size_t)gl::PagePool::kPitch * gl::PagePool::kPitch * bytesPerTexel());

    const QSize mapSize(mTableWidth, mTableRows);
    std::vector<quint16> tableZeros((size_t)count, 0);
    mTable.create(
        mapSize,
        reinterpret_cast<const uint8*>(tableZeros.data()),
        GL_RED_INTEGER,
        GL_R16UI,
        GL_UNSIGNED_SHORT,
        gl::Texture::NoMipmap
    );
    // integer textures only accept NEAREST
    mTable.setFilter(GL_NEAREST);
    mTable.setWrap(GL_CLAMP_TO_EDGE);

    mTableDirty = false;
    mPoolGeneration = gl::PagePool::instance().generation();

    // the shared demand map's layer for this resource's own page grid
    mDemandLayer = DemandMap::instance().allocateLayer(mapSize);
    mDemandParsedFrame = 0;
    return mTable.id() != 0;
}

void VirtualTexture::destroy() {
    const bool wasValid = mSize.isValid();
    if (wasValid) {
        unregisterResource(this);
        if (gl::PagePool::enabled())
            gl::PagePool::instance().releaseOwner((quint64)(uintptr_t)this);
    }

    if (mDemandLayer >= 0)
        DemandMap::instance().releaseLayer(mDemandLayer);
    mDemandLayer = -1;
    if (QOpenGLContext::currentContext()) {
        mTable.destroy();
        mCoarse.destroy();
    }
    mPyramid.clear();
    mMaskPyramid = nullptr;
    mMaskField = QImage();
    mResource.reset();
    mSize = QSize();
    mMaxLevel = 0;
    mCoarseLevel = 0;
    mFormat = gl::PagePool::Format_RGBA8;
    mTableWidth = 0;
    mTableRows = 0;
    mEntries.clear();
    mDemandFrame.clear();
    mActiveFlags.clear();
    mActivePages.clear();
    mLastMarked.clear();
    mDemandSignature = DemandSignature();
    mDemandSignatureValid = false;
    mPageBuffer.clear();
    mTableDirty = false;
    mDemandParsedFrame = 0;
}

void VirtualTexture::pageEvicted(const gl::PagePool::PageId& aPage) {
    if (aPage.level < 0 || aPage.level >= mCoarseLevel)
        return;
    const int index = pageIndex(aPage.level, aPage.x, aPage.y);
    if (index >= 0 && index < (int)mEntries.size() && mEntries[(size_t)index] != 0) {
        mEntries[(size_t)index] = 0;
        mTableDirty = true;
    }
}

void VirtualTexture::consumeDemand() {
    DemandMap& demandMap = DemandMap::instance();
    if (mDemandLayer < 0 || !demandMap.isMapped()
        || demandMap.mappedFrame() == mDemandParsedFrame)
        return;
    const QSize grid = demandMap.grid(mDemandLayer);
    const uint8* base = demandMap.layerData(mDemandLayer);
    if (grid.isEmpty() || !base)
        return;
    mDemandParsedFrame = demandMap.mappedFrame();
    const quint64 frame = gl::PagePool::instance().frame();
    const int stride = demandMap.stride();
    // THIS COPY'S OWN SET is what a repeated signature replays. A copy that marked nothing - the
    // frames a replay produced - leaves the previous set in place: those pages are still exactly
    // what the same signature demands, and dropping them would age a still view's residency out.
    std::vector<quint32> marked;
    for (int l = 0; l < mCoarseLevel; ++l) {
        const int row0 = mLevelRow[l];
        for (int py = 0; py < mLevelGridY[l]; ++py) {
            const int row = row0 + py;
            const uint8* line = base + (size_t)row * stride;
            for (int px = 0; px < mLevelGridX[l]; ++px) {
                if (line[px] == 0)
                    continue;
                const int index = row * mTableWidth + px;
                if (mDemandFrame[(size_t)index] == (int)frame)
                    continue;
                mDemandFrame[(size_t)index] = (int)frame;
                marked.push_back((quint32)index);
                if (!mActiveFlags[(size_t)index]) {
                    mActiveFlags[(size_t)index] = 1;
                    mActivePages.push_back((quint32)index);
                }
            }
        }
    }
    if (!marked.empty())
        mLastMarked.swap(marked);
}

void VirtualTexture::debugVerifyPages() {
    const QByteArray& mode = verifyMode();
    if (mode.isEmpty() || mActivePages.empty())
        return;
    // "end" is the same check, but only from the post-run entry point: a per-frame readback stalls
    // the pipeline and re-times the landing - which is the thing under test.
    if (mode == "end" && !sVerifyAfterRun)
        return;
    auto& ggl = gl::Global::functions();
    auto& pool = gl::PagePool::instance();

    // THE TABLE FIRST (cheap): what the shader actually reads, entry by entry, against the CPU
    // mirror the upload wrote. A page whose SLOT is right but whose TABLE entry names another
    // page's slot is the "displaced content" class, and the slot comparison below cannot see it.
    if (mTable.id() != 0) {
        std::vector<quint16> tableBytes((size_t)mTableWidth * mTableRows, 0);
        GLint prevRead = 0;
        GLuint fbo = 0;
        ggl.glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
        ggl.glGenFramebuffers(1, &fbo);
        ggl.glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        ggl.glFramebufferTexture2D(
            GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTable.id(), 0
        );
        ggl.glPixelStorei(GL_PACK_ALIGNMENT, 2);
        ggl.glReadPixels(
            0, 0, mTableWidth, mTableRows, GL_RED_INTEGER, GL_UNSIGNED_SHORT, tableBytes.data()
        );
        ggl.glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
        ggl.glDeleteFramebuffers(1, &fbo);
        int badEntries = 0;
        for (int index = 0; index < (int)mEntries.size(); ++index) {
            if (tableBytes[(size_t)index] == mEntries[(size_t)index])
                continue;
            ++badEntries;
            if (badEntries <= 4) {
                int px = 0, py = 0;
                const int level = levelOfIndex(index, &px, &py);
                qWarning(
                    "VTPAGETABLE owner=%p res=%dx%d mask=%d level=%d page=(%d,%d) gpu=%u cpu=%u",
                    (void*)this, mSize.width(), mSize.height(), isMask() ? 1 : 0, level, px, py,
                    (unsigned)tableBytes[(size_t)index], (unsigned)mEntries[(size_t)index]
                );
            }
        }
        if (badEntries > 0)
            qWarning("VTPAGETABLE owner=%p entries=%d", (void*)this, badEntries);
    }
    if (mode == "table")
        return;
    const size_t pageBytes =
        (size_t)gl::PagePool::kPitch * gl::PagePool::kPitch * (size_t)bytesPerTexel();
    std::vector<uint8> slotBytes(pageBytes);
    std::vector<uint8> own(pageBytes);
    const auto hashOf = [](const uint8* aData, size_t aCount) {
        quint64 h = 1469598103934665603ull;
        for (size_t i = 0; i < aCount; ++i) {
            h ^= aData[i];
            h *= 1099511628211ull;
        }
        return h;
    };
    // every wanted page's own bytes, hashed once: a mismatching slot is then matched to the page
    // whose upload it actually holds
    std::vector<quint64> ownHash(mActivePages.size(), 0);
    for (size_t i = 0; i < mActivePages.size(); ++i) {
        int px = 0, py = 0;
        const int level = levelOfIndex((int)mActivePages[i], &px, &py);
        fillPageBuffer(level, px, py);
        ownHash[i] = hashOf(mPageBuffer.data(), pageBytes);
    }
    int checked = 0, badPages = 0, worst = 0;
    for (size_t i = 0; i < mActivePages.size(); ++i) {
        int px = 0, py = 0;
        const int level = levelOfIndex((int)mActivePages[i], &px, &py);
        gl::PagePool::Slot slot;
        if (!pool.find(pageId(level, px, py), &slot))
            continue;
        if (!pool.readSlot(slot, slotBytes.data()))
            continue;
        ++checked;
        fillPageBuffer(level, px, py);
        std::memcpy(own.data(), mPageBuffer.data(), pageBytes);
        int bad = 0, maxDelta = 0;
        for (size_t b = 0; b < pageBytes; ++b) {
            const int d = std::abs((int)own[b] - (int)slotBytes[b]);
            if (d > 0)
                ++bad;
            if (d > maxDelta)
                maxDelta = d;
        }
        if (maxDelta > worst)
            worst = maxDelta;
        if (bad == 0)
            continue;
        ++badPages;
        const quint64 slotHash = hashOf(slotBytes.data(), pageBytes);
        int mate = -1;
        for (size_t j = 0; j < ownHash.size(); ++j) {
            if (j != i && ownHash[j] == slotHash) {
                mate = (int)j;
                break;
            }
        }
        qWarning(
            "VTPAGEBAD owner=%p res=%dx%d mask=%d level=%d page=(%d,%d) slot=a%d/%d "
            "mismatch=%d max=%d holdsPage=%d",
            (void*)this, mSize.width(), mSize.height(), isMask() ? 1 : 0, level, px, py,
            slot.atlas, slot.slot, bad, maxDelta, mate
        );
    }
    // THE LINK NEITHER CHECK ABOVE CLOSES: the table is the CPU mirror of the entries, and each
    // slot holds the page the POOL maps - but neither asks whether the MIRROR names the slot the
    // pool really holds for that page. A stale mirror (an entry naming a slot the pool has since
    // given to another page) passes both, and the shader reads that other page's texels: sharp,
    // wrong, and stable for as long as the mirror stays stale. No GL, so it costs nothing beside
    // the readbacks above.
    const std::vector<gl::PagePool::Resident> residents = pool.residentList();
    const auto occupantOf = [&residents](int aAtlas, int aSlot) -> const gl::PagePool::Resident* {
        for (const auto& resident : residents) {
            if (resident.slot.atlas == aAtlas && resident.slot.slot == aSlot)
                return &resident;
        }
        return nullptr;
    };
    int mirrorBad = 0;
    for (size_t i = 0; i < mActivePages.size(); ++i) {
        int px = 0, py = 0;
        const int level = levelOfIndex((int)mActivePages[i], &px, &py);
        gl::PagePool::Slot slot;
        if (!pool.find(pageId(level, px, py), &slot))
            continue; // not resident (the frame's upload cap deferred it): entry 0 is the coarse tier
        const quint16 entry = mEntries[(size_t)mActivePages[i]];
        if (entry == 0) {
            ++mirrorBad;
            qWarning(
                "VTPAGEMIRROR owner=%p res=%dx%d mask=%d level=%d page=(%d,%d) entry=0 but "
                "resident=a%d/%d",
                (void*)this, mSize.width(), mSize.height(), isMask() ? 1 : 0, level, px, py,
                slot.atlas, slot.slot
            );
            continue;
        }
        const int atlas = (int)(entry >> 8) + (int)mFormat * gl::PagePool::kMaxAtlasCount;
        const int slotIndex = (int)(entry & 255u) - 1;
        // AE_VT_DUMP_PAGES: the page -> slot layout of the final frame, one line per page. Two
        // runs of the same command are supposed to land the same pages in the same slots; when
        // they do not, a read that is one texel off shows a DIFFERENT NEIGHBOUR's content per
        // run, which is the shape of the panned-camera defect. This is how that is checked.
        if (sVerifyAfterRun && qEnvironmentVariableIsSet("AE_VT_DUMP_PAGES")) {
            qWarning(
                "VTPAGEMAP owner=%p res=%dx%d mask=%d level=%d page=(%d,%d) atlas=%d slot=%d",
                (void*)this, mSize.width(), mSize.height(), isMask() ? 1 : 0, level, px, py, atlas,
                slotIndex
            );
        }
        if (atlas == slot.atlas && slotIndex == slot.slot)
            continue;
        ++mirrorBad;
        const gl::PagePool::Resident* occupant = occupantOf(atlas, slotIndex);
        if (occupant) {
            qWarning(
                "VTPAGEMIRROR owner=%p res=%dx%d mask=%d level=%d page=(%d,%d) entry names="
                "a%d/%d pool=a%d/%d occupant owner=%p level=%d page=(%d,%d)",
                (void*)this, mSize.width(), mSize.height(), isMask() ? 1 : 0, level, px, py,
                atlas, slotIndex, slot.atlas, slot.slot,
                (void*)(uintptr_t)occupant->page.owner, occupant->page.level, occupant->page.x,
                occupant->page.y
            );
        } else {
            qWarning(
                "VTPAGEMIRROR owner=%p res=%dx%d mask=%d level=%d page=(%d,%d) entry names="
                "a%d/%d pool=a%d/%d occupant=none",
                (void*)this, mSize.width(), mSize.height(), isMask() ? 1 : 0, level, px, py,
                atlas, slotIndex, slot.atlas, slot.slot
            );
        }
    }
    // A CLEAN RUN MUST SAY IT CHECKED SOMETHING, or "no VTPAGEBAD" cannot be told from "the
    // verifier never ran". Printed only for the post-run form, whose whole point is that its
    // silence is evidence.
    if (sVerifyAfterRun) {
        qWarning(
            "VTPAGECHECK owner=%p res=%dx%d mask=%d wanted=%d checked=%d bad=%d mirrorBad=%d max=%d",
            (void*)this, mSize.width(), mSize.height(), isMask() ? 1 : 0,
            (int)mActivePages.size(), checked, badPages, mirrorBad, worst
        );
    }
}

void VirtualTexture::debugVerifyAfterRun() {
    // AE_VT_HASH_COARSE: the CPU side of every coarse chain (the pyramid's own bytes) and the GPU
    // side (the uploaded texture, read back). Two runs of one command are supposed to print the
    // same numbers. They are the ONE paged-path input no other verifier here covers, they are read
    // by BOTH tiers (a page fills from the pyramid and the coarse chain was uploaded from it), and
    // the full-resolution control reads neither - which is the shape of the panned-camera defect
    // (see the design doc): the control is bit-identical across runs while both VT modes are not.
    if (qEnvironmentVariableIsSet("AE_VT_HASH_COARSE")) {
        auto& ggl = gl::Global::functions();
        const auto hashOf = [](const uint8* aData, size_t aCount) {
            quint64 h = 1469598103934665603ull;
            for (size_t i = 0; i < aCount; ++i) {
                h ^= aData[i];
                h *= 1099511628211ull;
            }
            return h;
        };
        for (VirtualTexture* resource : registry()) {
            if (resource->mCoarse.id() == 0 || !resource->mSize.isValid())
                continue;
            // EVERY level, so the chain's level k and the coarse chain's copy of it (k -
            // coarseLevel) can be compared one by one: the copy is what a page miss samples, and
            // the live chain is what the control samples, so a mismatch at level k is exactly the
            // "paged frame differs from the control at the same level" the design doc is chasing.
            for (int rel = 0; rel <= resource->mMaxLevel - resource->mCoarseLevel; ++rel) {
                const int level = resource->mCoarseLevel + rel;
                const QSize size = resource->mPyramid.levelSize(level);
                std::vector<uint8> gpuBytes((size_t)size.width() * size.height() * 4, 0);
                ggl.glBindTexture(GL_TEXTURE_2D, resource->mCoarse.id());
                ggl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
                ggl.glGetTexImage(GL_TEXTURE_2D, rel, GL_RGBA, GL_UNSIGNED_BYTE, gpuBytes.data());
                ggl.glBindTexture(GL_TEXTURE_2D, 0);
                const quint64 gpu = hashOf(gpuBytes.data(), gpuBytes.size());
                qWarning(
                    "VTCOARSE owner=%p res=%dx%d mask=%d coarse=%d level=%d size=%dx%d "
                    "gpu=%016llx",
                    (void*)resource, resource->mSize.width(), resource->mSize.height(),
                    resource->isMask() ? 1 : 0, resource->mCoarseLevel, level, size.width(),
                    size.height(), (unsigned long long)gpu
                );
            }
        }
    }
    if (verifyMode() != "end")
        return;
    sVerifyAfterRun = true;
    for (VirtualTexture* resource : registry())
        resource->debugVerifyPages();
    sVerifyAfterRun = false;
}

void VirtualTexture::resolveDemand() {
    auto& pool = gl::PagePool::instance();
    const quint64 frame = pool.frame();

    // A purged pool (or a recreated one) has thrown every slot away: any entry still naming one
    // is a dangling slot number and must be cleared before this frame samples it.
    if (mPoolGeneration != pool.generation()) {
        mPoolGeneration = pool.generation();
        std::fill(mEntries.begin(), mEntries.end(), 0);
        mTableDirty = true;
    }

    // Touch every page still wanted BEFORE any upload can evict one: the pool's LRU must have
    // this frame's pages at the front, so the victim is a page nobody is sampling now.
    std::vector<quint32> kept;
    kept.reserve(mActivePages.size());
    for (quint32 value : mActivePages) {
        const int index = (int)value;
        const int when = mDemandFrame[(size_t)index];
        if (when <= 0 || frame > (quint64)when + kKeepFrames) {
            // stale: still resident, but now an eviction candidate
            mDemandFrame[(size_t)index] = 0;
            mActiveFlags[(size_t)index] = 0;
            continue;
        }
        kept.push_back(value);
        pool.touch(pageIdForIndex(index));
    }
    mActivePages.swap(kept);

    // Upload what is missing, sharpest level first: a zoom-in asks for level 0 and has less
    // time to converge than the fit view's level 3.
    std::stable_sort(mActivePages.begin(), mActivePages.end());
    for (quint32 value : mActivePages) {
        const int index = (int)value;
        int px = 0, py = 0;
        const int level = levelOfIndex(index, &px, &py);
        const gl::PagePool::PageId id = pageId(level, px, py);
        gl::PagePool::Slot slot;
        if (pool.find(id, &slot))
            continue;
        QElapsedTimer acquireTimer;
        if (landingSplitEnabled())
            acquireTimer.start();
        if (!pool.acquire(id, this, &slot, mFormat))
            break; // budget or upload cap: the rest waits, the coarse chain covers it
        if (landingSplitEnabled())
            sLanding.acquireNs += acquireTimer.nsecsElapsed();
        if (!uploadPage(level, px, py, slot)) {
            pool.cancel(slot);
            break;
        }
        if (landingSplitEnabled())
            ++sLanding.pages;
    }
}

void VirtualTexture::flushTable() {
    if (mTableDirty)
        rebuildTable();
}

bool VirtualTexture::uploadPage(int aLevel, int aPageX, int aPageY, const gl::PagePool::Slot& aSlot) {
    auto& pool = gl::PagePool::instance();
    QElapsedTimer phaseTimer;
    if (landingSplitEnabled())
        phaseTimer.start();
    // THE STAGE, WHEN IT IS ON: the fill writes straight into mapped buffer memory and
    // PagePool::endStaging copies it to the atlas, so the driver's staging copy never happens -
    // measured as the whole CPU cost of the direct path (see PagePool's staging comment). A full
    // or unavailable stage is not an error: this page goes direct.
    const qint64 staged = pool.reservePage(mFormat);
    uint8* dst = staged >= 0 ? pool.stagedPage(staged) : mPageBuffer.data();
    if (!dst)
        return false;
    fillPageBuffer(aLevel, aPageX, aPageY, dst);
    if (landingSplitEnabled())
        sLanding.fillNs += phaseTimer.nsecsElapsed();
    if (!isMask() && aLevel > 0 && qEnvironmentVariableIsSet("AE_VT_PAGE_HASH")) {
        const QSize levelSize = mPyramid.levelSize(aLevel);
        const int w = std::min((int)gl::PagePool::kPageSize, levelSize.width() - aPageX * (int)gl::PagePool::kPageSize);
        const int h = std::min((int)gl::PagePool::kPageSize, levelSize.height() - aPageY * (int)gl::PagePool::kPageSize);
        quint64 bufferHash = 1469598103934665603ull;
        for (int y = 0; y < h; ++y) {
            const uint8* row = dst
                + ((size_t)(y + gl::PagePool::kGutter) * gl::PagePool::kPitch + gl::PagePool::kGutter) * 4;
            for (int i = 0; i < w * 4; ++i) {
                bufferHash ^= row[i];
                bufferHash *= 1099511628211ull;
            }
        }
        const quint64 levelHash =
            mPyramid.hashRegion(aLevel, aPageX * (int)gl::PagePool::kPageSize, aPageY * (int)gl::PagePool::kPageSize, w, h);
        if (bufferHash != levelHash) {
            qWarning("VTPAGE level=%d page=(%d,%d) %dx%d buffer=%016llx level=%016llx", aLevel, aPageX,
                     aPageY, w, h, (unsigned long long)bufferHash, (unsigned long long)levelHash);
        }
    }
    if (staged >= 0) {
        // the copy is recorded, not issued: the stage is still mapped, and a mapped buffer may not
        // be read by GL (PagePool::endStaging unmaps and issues them, before the frame's draws)
        pool.stageCopy(aSlot, mFormat, staged);
    } else {
        auto& ggl = gl::Global::functions();
        const GLuint atlas = pool.atlasTextureId(aSlot.atlas);
        if (atlas == 0)
            return false;
        if (landingSplitEnabled())
            phaseTimer.restart();
        ggl.glBindTexture(GL_TEXTURE_2D, atlas);
        ggl.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        ggl.glTexSubImage2D(
            GL_TEXTURE_2D,
            0,
            aSlot.x - gl::PagePool::kGutter,
            aSlot.y - gl::PagePool::kGutter,
            gl::PagePool::kPitch,
            gl::PagePool::kPitch,
            isMask() ? GL_RED : GL_RGBA,
            GL_UNSIGNED_BYTE,
            dst
        );
        ggl.glBindTexture(GL_TEXTURE_2D, 0);
        if (landingSplitEnabled())
            sLanding.uploadNs += phaseTimer.nsecsElapsed();
    }

    const int index = pageIndex(aLevel, aPageX, aPageY);
    // the shader's atlas branch is PER STACK: the entry names 0..3 inside the resource's own
    // format, not the pool's global atlas index (R8 lives at 4..7 - see PagePool::Format)
    const int shaderAtlas = aSlot.atlas - (int)mFormat * gl::PagePool::kMaxAtlasCount;
    mEntries[(size_t)index] = (quint16)((shaderAtlas << 8) | (aSlot.slot + 1));
    mTableDirty = true;
    return true;
}

qint64 VirtualTexture::pyramidBytes() const {
    return isMask() && mMaskPyramid ? mMaskPyramid->bytes() : mPyramid.bytes();
}

void VirtualTexture::fillPageBuffer(int aLevel, int aPageX, int aPageY) {
    fillPageBuffer(aLevel, aPageX, aPageY, mPageBuffer.data());
}

void VirtualTexture::fillPageBuffer(int aLevel, int aPageX, int aPageY, uint8* aDst) {
    if (isMask()) {
        XC_PTR_ASSERT(mMaskPyramid);
        mMaskPyramid->fillPage(aLevel, aPageX, aPageY, mMaskField, aDst);
        return;
    }
    const uint8* base = nullptr;
    QSize baseSize;
    if (aLevel == 0 && mResource && mResource->hasImage()) {
        base = mResource->image().data();
        baseSize = mResource->image().pixelSize();
    }
    mPyramid.fillPage(aLevel, aPageX, aPageY, base, baseSize, aDst);
}

void VirtualTexture::updateMaskBox(const QImage& aField, const QRect& aTexelBox) {
    if (!isMask() || !mMaskPyramid || aField.isNull() || aField.size() != mSize)
        return;
    const QRect box = aTexelBox.intersected(QRect(QPoint(0, 0), mSize));
    if (box.isEmpty())
        return;

    // THE PYRAMID IS THE AUTHORITY FOR LEVELS 1+; the field is level 0 and is kept here for the
    // page fills. What is RESIDENT is brought in step: a level the box misses does not change,
    // and a page that is not resident needs nothing - its next fill reads the current field and
    // pyramid.
    mMaskField = aField;
    mMaskPyramid->updateBox(aField, box);

    auto& ggl = gl::Global::functions();
    ggl.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // the always-resident coarse chain: one box per level, halved and rounded outwards exactly
    // like the pyramid's own rule
    if (mCoarse.id() != 0) {
        ggl.glBindTexture(GL_TEXTURE_2D, mCoarse.id());
        for (int rel = 0; rel <= mMaxLevel - mCoarseLevel; ++rel) {
            const int level = mCoarseLevel + rel;
            const QSize levelSize = mMaskPyramid->levelSize(level);
            const QRect levelBox = mMaskPyramid->boxAtLevel(level, box);
            if (levelBox.isEmpty())
                break;
            const std::vector<uint8>& bytes = mMaskPyramid->levelBytes(level);
            const uint8* base =
                bytes.data() + (size_t)levelBox.top() * levelSize.width() + levelBox.left();
            ggl.glPixelStorei(GL_UNPACK_ROW_LENGTH, levelSize.width());
            ggl.glTexSubImage2D(
                GL_TEXTURE_2D, rel, levelBox.left(), levelBox.top(), levelBox.width(),
                levelBox.height(), GL_RED, GL_UNSIGNED_BYTE, base
            );
        }
        ggl.glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        ggl.glBindTexture(GL_TEXTURE_2D, 0);
    }

    // the pages: only those already resident, and only the ones the box's level-box touches
    for (int level = 0; level < mCoarseLevel; ++level) {
        const QRect levelBox = mMaskPyramid->boxAtLevel(level, box);
        if (levelBox.isEmpty())
            break;
        const int px0 = levelBox.left() / gl::PagePool::kPageSize;
        const int px1 = levelBox.right() / gl::PagePool::kPageSize;
        const int py0 = levelBox.top() / gl::PagePool::kPageSize;
        const int py1 = levelBox.bottom() / gl::PagePool::kPageSize;
        for (int py = py0; py <= py1; ++py) {
            for (int px = px0; px <= px1; ++px) {
                gl::PagePool::Slot slot;
                if (!gl::PagePool::instance().find(pageId(level, px, py), &slot))
                    continue;
                const GLuint atlas = gl::PagePool::instance().atlasTextureId(slot.atlas);
                if (atlas == 0)
                    continue;
                fillPageBuffer(level, px, py);
                ggl.glBindTexture(GL_TEXTURE_2D, atlas);
                ggl.glTexSubImage2D(
                    GL_TEXTURE_2D, 0, slot.x - gl::PagePool::kGutter,
                    slot.y - gl::PagePool::kGutter, gl::PagePool::kPitch, gl::PagePool::kPitch,
                    GL_RED, GL_UNSIGNED_BYTE, mPageBuffer.data()
                );
                ggl.glBindTexture(GL_TEXTURE_2D, 0);
            }
        }
    }
    ggl.glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

void VirtualTexture::refreshMaskView() {
    if (!isMask() || !mMaskPyramid || !mMaskPyramid->isBlend())
        return;
    auto& ggl = gl::Global::functions();
    auto& pool = gl::PagePool::instance();
    ggl.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // THE COARSE CHAIN, whole. It is always resident and every level of it moves with the weight,
    // so there is no box to narrow this to; the view copies both sources per level and lerps.
    if (mCoarse.id() != 0) {
        std::vector<uint8> levelBytes;
        ggl.glBindTexture(GL_TEXTURE_2D, mCoarse.id());
        for (int rel = 0; rel <= mMaxLevel - mCoarseLevel; ++rel) {
            const int level = mCoarseLevel + rel;
            const QSize levelSize = mMaskPyramid->levelSize(level);
            if (levelSize.isEmpty())
                break;
            levelBytes.resize((size_t)levelSize.width() * levelSize.height());
            mMaskPyramid->copyLevel(level, levelBytes.data());
            ggl.glTexSubImage2D(
                GL_TEXTURE_2D, rel, 0, 0, levelSize.width(), levelSize.height(), GL_RED,
                GL_UNSIGNED_BYTE, levelBytes.data()
            );
        }
        ggl.glBindTexture(GL_TEXTURE_2D, 0);
    }
    // EVERY RESIDENT PAGE, through the table entry that names its slot: a page outside the
    // demanded set can still be sampled this frame (its mark and its residency are a frame
    // apart), and on a view that page would otherwise hold the last frame's weight.
    for (int index = 0; index < pageCount(); ++index) {
        if (mEntries[(size_t)index] == 0)
            continue;
        int px = 0, py = 0;
        const int level = levelOfIndex(index, &px, &py);
        gl::PagePool::Slot slot;
        if (!pool.find(pageId(level, px, py), &slot))
            continue;
        const GLuint atlas = pool.atlasTextureId(slot.atlas);
        if (atlas == 0)
            continue;
        fillPageBuffer(level, px, py);
        ggl.glBindTexture(GL_TEXTURE_2D, atlas);
        ggl.glTexSubImage2D(
            GL_TEXTURE_2D, 0, slot.x - gl::PagePool::kGutter, slot.y - gl::PagePool::kGutter,
            gl::PagePool::kPitch, gl::PagePool::kPitch, GL_RED, GL_UNSIGNED_BYTE,
            mPageBuffer.data()
        );
        ggl.glBindTexture(GL_TEXTURE_2D, 0);
    }
    ggl.glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

void VirtualTexture::rebuildTable() {
    if (mTable.id() == 0)
        return;
    QElapsedTimer tableTimer;
    if (landingSplitEnabled())
        tableTimer.start();
    auto& ggl = gl::Global::functions();
    ggl.glBindTexture(GL_TEXTURE_2D, mTable.id());
    ggl.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    ggl.glTexSubImage2D(
        GL_TEXTURE_2D, 0, 0, 0, mTableWidth, mTableRows, GL_RED_INTEGER, GL_UNSIGNED_SHORT, mEntries.data()
    );
    ggl.glBindTexture(GL_TEXTURE_2D, 0);
    mTableDirty = false;
    if (landingSplitEnabled())
        sLanding.tableNs += tableTimer.nsecsElapsed();
}

void VirtualTexture::markDemand(
    const DemandSignature& aInputSignature,
    const gl::BufferObject& aPositions,
    const gl::Vector2* aTexCoords,
    int aVtxCount,
    const gl::BufferObject& aIndexBuffer,
    int aIndexCount
) {
    if (!isValid() || aVtxCount <= 0 || aIndexCount < 3 || !demandEnabled())
        return;
    if (mDemandLayer < 0)
        return;

    DemandSignature signature = aInputSignature;
    signature.kind = DemandKind_Layer;
    // the level bias is a session policy, not a caller's argument: read it here so a signature
    // cannot disagree with the uniform the pass actually used
    signature.bias = ImageKey::layerLodBias();
    if (demandSignatureRepeats(signature)) {
        replayDemand();
        return;
    }

    DemandMap::Params params;
    params.layerSize = mSize;
    params.coarseLevel = mCoarseLevel;
    params.maxLevel = mMaxLevel;
    params.bias = signature.bias;
    for (int i = 0; i < kMaxPageLevels; ++i)
        params.levelRow[i] = mLevelRow[i];

    const bool marked = DemandMap::instance().mark(
        mDemandLayer, QSize(mTableWidth, mTableRows), params, signature.viewMatrix,
        signature.targetSize, signature.texCoordOffset, aPositions.id(), aTexCoords, aVtxCount,
        aIndexBuffer.id(), aIndexCount
    );
    if (marked) {
        mDemandSignature = signature;
        mDemandSignatureValid = true;
        ++sDemandMarks;
    }
}

void VirtualTexture::markScreenMaskDemand(const DemandSignature& aInputSignature) {
    if (!isMask() || mDemandLayer < 0 || !demandEnabled()
        || aInputSignature.maskRect.z() <= 0.0f || aInputSignature.maskRect.w() <= 0.0f
        || aInputSignature.targetSize.isEmpty())
        return;

    DemandSignature signature = aInputSignature;
    signature.kind = DemandKind_MaskScreen;
    signature.bias = ImageKey::layerLodBias();
    if (demandSignatureRepeats(signature)) {
        replayDemand();
        return;
    }

    DemandMap::Params params;
    params.layerSize = mSize;
    params.coarseLevel = mCoarseLevel;
    params.maxLevel = mMaxLevel;
    params.bias = signature.bias;
    for (int i = 0; i < kMaxPageLevels; ++i)
        params.levelRow[i] = mLevelRow[i];
    params.uvFromScreen = true;
    params.screenToContent = signature.screenToContent;
    params.maskRect = signature.maskRect;
    params.maskTexelSize = QVector2D((float)mSize.width(), (float)mSize.height());

    const bool marked = DemandMap::instance().markFullscreen(
        mDemandLayer, QSize(mTableWidth, mTableRows), params, signature.targetSize
    );
    if (marked) {
        mDemandSignature = signature;
        mDemandSignatureValid = true;
        ++sDemandMarks;
    }
}

void VirtualTexture::markScreenDemand(const DemandSignature& aInputSignature) {
    if (isMask() || mDemandLayer < 0 || !demandEnabled() || mSize.isEmpty()
        || aInputSignature.targetSize.isEmpty())
        return;

    DemandSignature signature = aInputSignature;
    signature.kind = DemandKind_LayerScreen;
    signature.bias = ImageKey::layerLodBias();
    if (demandSignatureRepeats(signature)) {
        replayDemand();
        return;
    }

    DemandMap::Params params;
    params.layerSize = mSize;
    params.coarseLevel = mCoarseLevel;
    params.maxLevel = mMaxLevel;
    params.bias = signature.bias;
    for (int i = 0; i < kMaxPageLevels; ++i)
        params.levelRow[i] = mLevelRow[i];
    // THE SAME BRANCH THE FOLDER MATTE USES. The vertex shader derives each quad corner's texel
    // position from the screen->content affine the fragment shader applies, so the page rect is
    // the quad's own extent - for a plain layer that IS the union of its triangles' rects, because
    // every triangle of a grid shares one Jacobian. The quad also clips against the screen, which
    // a mesh walk cannot do without walking.
    params.uvFromScreen = true;
    params.screenToContent = signature.screenToContent;
    params.maskRect = signature.maskRect;    // (0, 0, W, H): the content rect
    params.maskTexelSize = QVector2D((float)mSize.width(), (float)mSize.height());

    const bool marked = DemandMap::instance().markFullscreen(
        mDemandLayer, QSize(mTableWidth, mTableRows), params, signature.targetSize,
        signature.texCoordOffset
    );
    if (marked) {
        mDemandSignature = signature;
        mDemandSignatureValid = true;
        ++sDemandMarks;
    }
}

bool VirtualTexture::DemandSignature::operator==(const DemandSignature& aRhs) const {
    // EXACT comparisons, deliberately: a value that drifted by an epsilon re-marks, which costs
    // one geometry pass, while a value that changed and compared equal would leave the frame
    // sampling the coarse chain. The values come from the same expressions every frame, so an
    // unchanged input does compare equal.
    if (kind != aRhs.kind || positions != aRhs.positions || targetSize != aRhs.targetSize
        || texCoordOffset != aRhs.texCoordOffset || maskRect != aRhs.maskRect
        || bias != aRhs.bias)
        return false;
    // THE WHOLE MATRIX, even though a mesh walk's page RECTS do not move with the camera. The
    // LEVEL does not either in exact arithmetic - it comes from the triangle's screen edges, where
    // a translation cancels - but the FRAGMENT's level comes from the hardware's derivatives of
    // the very same screen positions, and those round differently for a different camera. A mark
    // made at one camera therefore does not predict what the fragment will sample at another:
    // measured on nene, reusing the fit view's mark across a 40 px pan left 263 pixels reading the
    // coarse chain, and at a 280 px pan the reused mark was SHARPER than a fresh one (max 1 against
    // the control, where the fresh mark measured max 223) - it happened to be a superset. Correct
    // by luck is not correct, so a camera move re-marks.
    return viewMatrix == aRhs.viewMatrix && screenToContent == aRhs.screenToContent;
}

bool VirtualTexture::demandSignatureRepeats(const DemandSignature& aSignature) const {
    // NO MARK YET is not a repeat: the first frame of a resource must walk its triangles, and a
    // resource whose mark failed keeps mDemandSignatureValid false so the next frame tries again.
    return mDemandSignatureValid && mDemandSignature == aSignature;
}

void VirtualTexture::replayDemand() {
    ++sDemandReplays;
    const int frame = (int)gl::PagePool::instance().frame();
    for (quint32 value : mLastMarked) {
        const int index = (int)value;
        if (mDemandFrame[(size_t)index] == frame)
            continue;
        // The page is wanted THIS frame, exactly as if the pass had marked it: the kKeepFrames
        // window moves with the replay, so a still view keeps its pages instead of letting them
        // age out while they are still sampled. resolveDemand (next beginFrame) uploads any page
        // that is no longer resident.
        mDemandFrame[(size_t)index] = frame;
        if (!mActiveFlags[(size_t)index]) {
            mActiveFlags[(size_t)index] = 1;
            mActivePages.push_back(value);
        }
    }
}

void VirtualTexture::bind(gl::EasyShaderProgram& aShader) const {
    auto& ggl = gl::Global::functions();
    // A HOLE in the atlas list is bound to the coarse texture, never left incomplete: a slot
    // that names a released atlas is a bug, but an incomplete sampler is a driver-dependent
    // colour. The count the shader checks is the NUMBER OF ADDRESSABLE ATLAS SLOTS, not the
    // number currently allocated - a released atlas 0 with atlas 1 still full would otherwise
    // make every atlas-1 entry look out of range.
    for (int i = 0; i < gl::PagePool::kMaxAtlasCount; ++i) {
        const GLuint id = gl::PagePool::instance().atlasTextureId(i);
        ggl.glActiveTexture(GL_TEXTURE6 + i);
        ggl.glBindTexture(GL_TEXTURE_2D, id != 0 ? id : mCoarse.id());
    }
    ggl.glActiveTexture(GL_TEXTURE4);
    ggl.glBindTexture(GL_TEXTURE_2D, mCoarse.id());
    ggl.glActiveTexture(GL_TEXTURE5);
    ggl.glBindTexture(GL_TEXTURE_2D, mTable.id());
    ggl.glActiveTexture(GL_TEXTURE0);

    int rows[kMaxPageLevels];
    for (int i = 0; i < kMaxPageLevels; ++i)
        rows[i] = mLevelRow[i];

    aShader.setUniformValue("uLayerSize", QVector2D((float)mSize.width(), (float)mSize.height()));
    aShader.setUniformValue("uLayerMaxLod", (float)mMaxLevel);
    aShader.setUniformValue("uCoarseLevel", mCoarseLevel);
    aShader.setUniformValue("uCoarseTexture", 4);
    aShader.setUniformValue("uPageTable", 5);
    aShader.setUniformValueArray("uPageLevelRow", rows, kMaxPageLevels);
    aShader.setUniformValue("uPageAtlasSize", (float)gl::PagePool::kAtlasSize);
    aShader.setUniformValue("uPageSlotsPerAxis", gl::PagePool::kSlotsPerAxis);
    aShader.setUniformValue("uPagePitch", (float)gl::PagePool::kPitch);
    aShader.setUniformValue("uPageGutter", (float)gl::PagePool::kGutter);
    aShader.setUniformValue("uPageTexels", (float)gl::PagePool::kPageSize);
    aShader.setUniformValue("uAtlasCount", (int)gl::PagePool::kMaxAtlasCount);
    // AE_VT_MISS_MAGENTA is a UNIFORM, so the program (and therefore the frame's timing) is the
    // same with it on: see the declaration in LayerSampling.glsl.
    aShader.setUniformValue(
        "uVtMissDebug", qEnvironmentVariableIsSet("AE_VT_MISS_MAGENTA") ? 1 : 0
    );
    aShader.setUniformValue("uAtlas0", 6);
    aShader.setUniformValue("uAtlas1", 7);
    aShader.setUniformValue("uAtlas2", 8);
    aShader.setUniformValue("uAtlas3", 9);
}

void VirtualTexture::bindMask(gl::EasyShaderProgram& aShader) const {
    auto& ggl = gl::Global::functions();
    // a hole in the atlas list is bound to the coarse chain, never left incomplete (the same
    // rule as bind)
    for (int i = 0; i < gl::PagePool::kMaxAtlasCount; ++i) {
        const GLuint id =
            gl::PagePool::instance().atlasTextureId((int)mFormat * gl::PagePool::kMaxAtlasCount + i);
        ggl.glActiveTexture(GL_TEXTURE12 + i);
        ggl.glBindTexture(GL_TEXTURE_2D, id != 0 ? id : mCoarse.id());
    }
    ggl.glActiveTexture(GL_TEXTURE10);
    ggl.glBindTexture(GL_TEXTURE_2D, mCoarse.id());
    ggl.glActiveTexture(GL_TEXTURE11);
    ggl.glBindTexture(GL_TEXTURE_2D, mTable.id());
    ggl.glActiveTexture(GL_TEXTURE0);

    int rows[kMaxPageLevels];
    for (int i = 0; i < kMaxPageLevels; ++i)
        rows[i] = mLevelRow[i];

    aShader.setUniformValue("uMaskCoarseTexture", 10);
    aShader.setUniformValue("uMaskPageTable", 11);
    aShader.setUniformValueArray("uMaskPageLevelRow", rows, kMaxPageLevels);
    aShader.setUniformValue("uMaskSize", QVector2D((float)mSize.width(), (float)mSize.height()));
    aShader.setUniformValue("uMaskMaxLod", (float)mMaxLevel);
    aShader.setUniformValue("uMaskCoarseLevel", mCoarseLevel);
    aShader.setUniformValue("uMaskAtlas0", 12);
    aShader.setUniformValue("uMaskAtlas1", 13);
    aShader.setUniformValue("uMaskAtlas2", 14);
    aShader.setUniformValue("uMaskAtlas3", 15);

    // the page geometry both stacks share; a draw that pages only the mask (the rubylith
    // overlay) has no image VT to set them
    aShader.setUniformValue("uPageAtlasSize", (float)gl::PagePool::kAtlasSize);
    aShader.setUniformValue("uPageSlotsPerAxis", gl::PagePool::kSlotsPerAxis);
    aShader.setUniformValue("uPagePitch", (float)gl::PagePool::kPitch);
    aShader.setUniformValue("uPageGutter", (float)gl::PagePool::kGutter);
    aShader.setUniformValue("uPageTexels", (float)gl::PagePool::kPageSize);
    aShader.setUniformValue("uAtlasCount", (int)gl::PagePool::kMaxAtlasCount);
}

int VirtualTexture::residentWantedCount() const {
    int resident = 0;
    for (quint32 value : mActivePages) {
        const int index = (int)value;
        int px = 0, py = 0;
        const int level = levelOfIndex(index, &px, &py);
        gl::PagePool::Slot slot;
        if (gl::PagePool::instance().find(pageId(level, px, py), &slot))
            ++resident;
    }
    return resident;
}

} // namespace core
