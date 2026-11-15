#include <QBuffer>
#include <QOpenGLContext>
#include <algorithm>
#include <cmath>
#include <QByteArray>
#include "gl/EasyShaderProgram.h"
#include "gl/Global.h"
#include "gl/VertexArrayObject.h"
#include "core/Deserializer.h"
#include "core/FilterMask.h"
#include "core/Serializer.h"

namespace core {

namespace {

// ---------------------------------------------------------------------------------------------
// Level geometry

/// The number of trailing zero bits of a side, i.e. the largest l for which 2^l still divides it -
/// the deepest level that is an EXACT halving of the side above it.
int trailingZeroBits(int aValue) {
    int n = 0;
    while (aValue > 1 && (aValue & 1) == 0) {
        aValue >>= 1;
        ++n;
    }
    return n;
}

/// GL's level geometry: level l has max(1, side >> l) texels, so a texture of W x H carries
/// 1 + floor(log2(max(W, H))) levels down to 1x1 (the rule gl/Texture.cpp records as
/// mMipmapLevels).
QSize levelSize(const QSize& aBase, int aLevel) {
    return QSize(qMax(1, aBase.width() >> aLevel), qMax(1, aBase.height() >> aLevel));
}

int levelCount(const QSize& aBase) {
    int side = qMax(aBase.width(), aBase.height());
    int count = 1;
    while (side > 1) {
        side >>= 1;
        ++count;
    }
    return count;
}

/// The deepest level that still halves its predecessor exactly on both axes. Above it a level is
/// the 2x2 average of the level above; below it, the area-weighted reduction of this one.
int exactLevels(const QSize& aBase) {
    return qMin(trailingZeroBits(aBase.width()), trailingZeroBits(aBase.height()));
}

/// The box on aDstSize's grid that the damage in aSrcBox reaches: every destination texel whose
/// footprint [j * src / dst, (j + 1) * src / dst) touches the source range, rounded OUTWARDS at
/// both ends and clamped into the grid. Exact integer arithmetic, so an exact dyadic ratio lands
/// on the boundary it should rather than on a float's neighbour; outward rounding can over-cover
/// (a texel rebuilt without needing to be comes out with the value it had) but never under-cover.
QRect coveredBox(const QRect& aSrcBox, const QSize& aSrcSize, const QSize& aDstSize) {
    if (aSrcBox.isEmpty() || aSrcSize.isEmpty() || aDstSize.isEmpty())
        return QRect();

    const auto span = [](int aLo, int aHi, int aSrc, int aDst, int& aFirst, int& aLast) {
        const qint64 src = aSrc;
        const qint64 dst = aDst;
        // floor(aLo * dst / src) and ceil(aHi * dst / src) - 1, in integers: a dyadic ratio then
        // lands on the boundary it should, not on a float's neighbour
        const int lo = int((qint64(aLo) * dst) / src);
        const int hi = int(((qint64(aHi) * dst) + src - 1) / src) - 1;
        aFirst = qBound(0, lo, aDst - 1);
        aLast = qBound(0, hi, aDst - 1);
    };
    int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    span(aSrcBox.left(), aSrcBox.right() + 1, aSrcSize.width(), aDstSize.width(), x0, x1);
    span(aSrcBox.top(), aSrcBox.bottom() + 1, aSrcSize.height(), aDstSize.height(), y0, y1);
    if (x1 < x0 || y1 < y0)
        return QRect();
    return QRect(QPoint(x0, y0), QPoint(x1, y1));
}

// ---------------------------------------------------------------------------------------------
// The resample pass

/// A fullscreen triangle from the vertex index alone: no attribute, no buffer, nothing to bind
/// but the VAO a core profile insists on.
const char* kResampleVert =
    "#version 330 \n"
    "void main() {"
    "  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));"
    "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);"
    "}";

/// ONE AXIS of the area-weighted reduction, the weights gl/Texture.cpp's GPU tail applies:
/// destination texel g covers [g * src / dst, (g + 1) * src / dst) of the sampled level, and each
/// source texel contributes the length of the overlap. Every destination texel normalizes by the
/// sum of its own weights, which is the per-axis divide the area rule implies.
///
/// It is run TWICE per level (x into the scratch, then y from it): a 2D single-pass version would
/// need spanX * spanY taps in one fragment, and on a level that does not halve the span can be the
/// whole side - 225 M texel fetches for one texel of a 15001-wide field. Separable keeps it at
/// span taps per fragment and the intermediate at (destination width x source height).
///
/// The destination is either the scratch (x pass) or a level of the mask texture (y pass), and the
/// source is always the OTHER texture, so no draw ever samples the image it renders into.
const char* kResampleFrag =
    "#version 330 \n"
    "uniform sampler2D uSource;"
    "uniform int uSourceLevel;"
    "uniform int uAxis;"          // 0: average along x, copy y; 1: average along y, copy x
    "uniform int uSourceSpan;"    // the sampled level's texels along the averaged axis
    "uniform int uDestSpan;"      // the destination level's texels along it
    "uniform int uSourceOffset;"  // subtracted from the averaged source index
    "uniform int uCopyOffset;"    // added to the local coordinate of the copied axis
    "uniform int uTaps;"
    "layout(location = 0, index = 0) out vec4 oFragColor;"
    "void main() {"
    "  ivec2 local = ivec2(gl_FragCoord.xy);"
    "  int g = (uAxis == 0) ? local.x : local.y;"
    "  float lo = float(g) * float(uSourceSpan) / float(uDestSpan);"
    "  float hi = float(g + 1) * float(uSourceSpan) / float(uDestSpan);"
    "  int first = int(floor(lo));"
    "  int copy = uCopyOffset + ((uAxis == 0) ? local.y : local.x);"
    "  float acc = 0.0;"
    "  float wsum = 0.0;"
    "  for (int i = 0; i < uTaps; ++i) {"
    "    int k = first + i;"
    "    float w = min(float(k + 1), hi) - max(float(k), lo);"
    "    if (w <= 0.0) { continue; }"
    "    int s = k - uSourceOffset;"
    "    ivec2 c = (uAxis == 0) ? ivec2(s, copy) : ivec2(copy, s);"
    "    acc += texelFetch(uSource, c, uSourceLevel).r * w;"
    "    wsum += w;"
    "  }"
    "  oFragColor = vec4(wsum > 0.0 ? acc / wsum : 0.0, 0.0, 0.0, 1.0);"
    "}";

/// Everything the passes need that belongs to the CONTEXT rather than to a field: the program, the
/// VAO, one framebuffer and one scratch target, shared by every chain.
///
/// Shared because a mask field is created and thrown away per key - undo/redo does it constantly -
/// and compiling a program per field would put a shader build on the timeline's hot path. The
/// objects belong to the context that first used them; a different context (each pixel gate makes
/// its own) builds its own and ABANDONS the previous set. Abandoning is deliberate: those ids
/// belong to a context that is gone, and deleting them through the current one would delete a
/// stranger, so the C++ holders are leaked (which is also why their GL-touching destructors never
/// run).
struct MipPass {
    QOpenGLContext* context = nullptr;
    gl::EasyShaderProgram* program = nullptr;
    gl::VertexArrayObject* vao = nullptr;
    GLuint framebuffer = 0;
    GLuint scratch = 0;
    QSize scratchSize;
};

MipPass& mipPass() {
    static MipPass s;
    QOpenGLContext* context = QOpenGLContext::currentContext();
    XC_ASSERT(context != nullptr);   // the texture path is only ever reached with a context current
    if (s.context == context && s.program != nullptr)
        return s;

    s = MipPass();
    s.context = context;
    s.program = new gl::EasyShaderProgram();
    const bool linked = s.program->setVertexSource(QString::fromLatin1(kResampleVert))
        && s.program->setFragmentSource(QString::fromLatin1(kResampleFrag)) && s.program->link();
    XC_ASSERT(linked);
    s.vao = new gl::VertexArrayObject();
    gl::Global::functions().glGenFramebuffers(1, &s.framebuffer);
    return s;
}

/// The scratch is the x pass's target: R16F, because rounding it to 8 bits between the two passes
/// would add a second rounding to a rule that is a single one on the CPU (a half float carries an
/// 8-bit sum's fraction to within 1/8 LSB, so the level keeps the one rounding the averaging
/// implies). Grown to the largest box seen; a stroke's box is tiny, a full build's is a level.
void ensureScratch(MipPass& aPass, const QSize& aSize) {
    gl::Global::Functions& ggl = gl::Global::functions();
    if (aPass.scratch != 0 && aPass.scratchSize.width() >= aSize.width()
        && aPass.scratchSize.height() >= aSize.height())
        return;

    if (aPass.scratch == 0)
        ggl.glGenTextures(1, &aPass.scratch);
    ggl.glBindTexture(GL_TEXTURE_2D, aPass.scratch);
    ggl.glTexImage2D(
        GL_TEXTURE_2D, 0, GL_R16F, aSize.width(), aSize.height(), 0, GL_RED, GL_FLOAT, nullptr
    );
    ggl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    ggl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    ggl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    ggl.glBindTexture(GL_TEXTURE_2D, 0);
    aPass.scratchSize = aSize;
}

/// Saves and puts back every piece of state the passes touch. They run INSIDE a render pass (a
/// layer binds its matte between glEnable(GL_BLEND) and its own draw, and keeps setting uniforms on
/// its program afterwards), so the target, the viewport, the scissor, the enabled capabilities, the
/// polygon mode and colour mask, the active unit and that unit's 2D binding, AND the current program
/// and vertex array all have to survive: leaving none bound would make the caller's next uniform a
/// write to program 0.
class GlStateGuard: private util::NonCopyable {
public:
    GlStateGuard():
        mDrawFramebuffer(0), mReadFramebuffer(0), mViewport(), mScissorBox(), mActiveTexture(GLint(GL_TEXTURE0)),
        mTexture(0), mProgram(0), mVertexArray(0), mScissorEnabled(GL_FALSE), mBlendEnabled(GL_FALSE),
        mDepthEnabled(GL_FALSE), mCullEnabled(GL_FALSE), mStencilEnabled(GL_FALSE), mDitherEnabled(GL_FALSE) {
        gl::Global::Functions& ggl = gl::Global::functions();
        ggl.glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &mDrawFramebuffer);
        ggl.glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &mReadFramebuffer);
        ggl.glGetIntegerv(GL_VIEWPORT, mViewport);
        ggl.glGetIntegerv(GL_SCISSOR_BOX, mScissorBox);
        ggl.glGetIntegerv(GL_ACTIVE_TEXTURE, &mActiveTexture);
        ggl.glGetIntegerv(GL_CURRENT_PROGRAM, &mProgram);
        ggl.glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &mVertexArray);
        ggl.glGetIntegerv(GL_POLYGON_MODE, mPolygonMode);
        ggl.glGetBooleanv(GL_COLOR_WRITEMASK, mColorMask);
        mScissorEnabled = ggl.glIsEnabled(GL_SCISSOR_TEST);
        mBlendEnabled = ggl.glIsEnabled(GL_BLEND);
        mDepthEnabled = ggl.glIsEnabled(GL_DEPTH_TEST);
        mCullEnabled = ggl.glIsEnabled(GL_CULL_FACE);
        mStencilEnabled = ggl.glIsEnabled(GL_STENCIL_TEST);
        mDitherEnabled = ggl.glIsEnabled(GL_DITHER);

        // unit 0 is the one the passes sample on; its binding belongs to the caller
        ggl.glActiveTexture(GL_TEXTURE0);
        GLint texture = 0;
        ggl.glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        mTexture = GLuint(texture);
    }

    ~GlStateGuard() {
        gl::Global::Functions& ggl = gl::Global::functions();
        ggl.glActiveTexture(GL_TEXTURE0);
        ggl.glBindTexture(GL_TEXTURE_2D, mTexture);
        ggl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(mDrawFramebuffer));
        ggl.glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(mReadFramebuffer));
        ggl.glViewport(mViewport[0], mViewport[1], mViewport[2], mViewport[3]);
        ggl.glScissor(mScissorBox[0], mScissorBox[1], mScissorBox[2], mScissorBox[3]);
        ggl.glUseProgram(GLuint(mProgram));
        ggl.glBindVertexArray(GLuint(mVertexArray));
        // core profile accepts only GL_FRONT_AND_BACK here, and both faces share one mode
        ggl.glPolygonMode(GL_FRONT_AND_BACK, GLenum(mPolygonMode[0]));
        ggl.glColorMask(mColorMask[0], mColorMask[1], mColorMask[2], mColorMask[3]);
        setEnabled(ggl, GL_SCISSOR_TEST, mScissorEnabled);
        setEnabled(ggl, GL_BLEND, mBlendEnabled);
        setEnabled(ggl, GL_DEPTH_TEST, mDepthEnabled);
        setEnabled(ggl, GL_CULL_FACE, mCullEnabled);
        setEnabled(ggl, GL_STENCIL_TEST, mStencilEnabled);
        setEnabled(ggl, GL_DITHER, mDitherEnabled);
        ggl.glActiveTexture(GLenum(mActiveTexture));
    }

private:
    static void setEnabled(gl::Global::Functions& aGgl, GLenum aCapability, GLboolean aEnabled) {
        if (aEnabled)
            aGgl.glEnable(aCapability);
        else
            aGgl.glDisable(aCapability);
    }

    GLint mDrawFramebuffer;
    GLint mReadFramebuffer;
    GLint mViewport[4];
    GLint mScissorBox[4];
    GLint mActiveTexture;
    GLuint mTexture;
    GLint mProgram;
    GLint mVertexArray;
    GLint mPolygonMode[2];
    GLboolean mColorMask[4];
    GLboolean mScissorEnabled;
    GLboolean mBlendEnabled;
    GLboolean mDepthEnabled;
    GLboolean mCullEnabled;
    GLboolean mStencilEnabled;
    GLboolean mDitherEnabled;
};

/// One axis of one level: renders aScissor of (aTarget, aTargetLevel) from the given level of
/// aSource. See kResampleFrag for the coordinates.
void drawResample(
    MipPass& aPass, GLuint aSource, int aSourceLevel, const QSize& aSourceSize, const QSize& aDestSize,
    int aAxis, int aSourceOffset, int aCopyOffset, GLuint aTarget, int aTargetLevel,
    const QSize& aTargetSize, const QRect& aScissor
) {
    gl::Global::Functions& ggl = gl::Global::functions();

    ggl.glBindFramebuffer(GL_FRAMEBUFFER, aPass.framebuffer);
    ggl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, aTarget, aTargetLevel);
    XC_ASSERT(ggl.glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

    const int sourceSpan = (aAxis == 0) ? aSourceSize.width() : aSourceSize.height();
    const int destSpan = (aAxis == 0) ? aDestSize.width() : aDestSize.height();

    ggl.glViewport(0, 0, aTargetSize.width(), aTargetSize.height());
    // fixed-function state the pass must not inherit: blending would mix the average with what the
    // level already held, the scissor/stencil would mask texels, culling or a LINE polygon mode
    // would drop the full-screen triangle, and dithering would perturb the 8-bit result
    ggl.glDisable(GL_BLEND);
    ggl.glDisable(GL_DEPTH_TEST);
    ggl.glDisable(GL_CULL_FACE);
    ggl.glDisable(GL_STENCIL_TEST);
    ggl.glDisable(GL_DITHER);
    ggl.glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    ggl.glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    ggl.glEnable(GL_SCISSOR_TEST);
    ggl.glScissor(aScissor.x(), aScissor.y(), aScissor.width(), aScissor.height());

    aPass.program->bind();
    aPass.program->setUniformValue("uSource", 0);
    aPass.program->setUniformValue("uSourceLevel", aSourceLevel);
    aPass.program->setUniformValue("uAxis", aAxis);
    aPass.program->setUniformValue("uSourceSpan", sourceSpan);
    aPass.program->setUniformValue("uDestSpan", destSpan);
    aPass.program->setUniformValue("uSourceOffset", aSourceOffset);
    aPass.program->setUniformValue("uCopyOffset", aCopyOffset);
    // a destination span of d covers d * src/dst source texels, so at most ceil(src/dst) + 1
    // source texels can have a non-empty overlap (the extra one is skipped by its zero weight)
    aPass.program->setUniformValue("uTaps", int(std::ceil(double(sourceSpan) / double(destSpan))) + 1);

    ggl.glActiveTexture(GL_TEXTURE0);
    ggl.glBindTexture(GL_TEXTURE_2D, aSource);
    aPass.vao->bind();
    ggl.glDrawArrays(GL_TRIANGLES, 0, 3);
    // the program, the vertex array, the target and the bindings are NOT released here: the caller
    // may be in the middle of its own draw (see GlStateGuard, which puts every one of them back)
    ggl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ---------------------------------------------------------------------------------------------
// Chain mechanics (shared by create() and update())

/// Rebuilds ONE level's box: the x pass into the scratch (sampling the level named by aSourceLevel)
/// and then the y pass from the scratch into the level itself. The two passes are the axis split
/// of the area rule; for an exact halving each pass degenerates to two taps of weight 1, i.e. the
/// aligned 2x2 average.
void buildLevel(
    MipPass& aPass, GLuint aTexture, int aLevel, const QRect& aDestBox, int aSourceLevel,
    const QSize& aSourceSize, const QSize& aDestSize
) {
    // the source region the box's texels read - the same rule as the boxes above it, applied to
    // this box: the texels whose own footprint touches it
    const QRect sourceBox = coveredBox(aDestBox, aDestSize, aSourceSize);
    if (sourceBox.isEmpty())
        return;

    const QSize scratchSize(aDestSize.width(), sourceBox.height());
    ensureScratch(aPass, scratchSize);
    drawResample(
        aPass, aTexture, aSourceLevel, aSourceSize, aDestSize, 0, 0, sourceBox.top(), aPass.scratch, 0,
        scratchSize, QRect(aDestBox.left(), 0, aDestBox.width(), sourceBox.height())
    );
    // the scratch's rows start at the source box's top, so the resampled source index is offset by
    // it; the copied axis (x) is the destination's own column, so it has no offset
    drawResample(
        aPass, aPass.scratch, 0, aSourceSize, aDestSize, 1, sourceBox.top(), 0, aTexture, aLevel,
        aDestSize, aDestBox
    );
}

} // namespace

// -------------------------------------------------------------------------------------------------
// MaskMipChain

MaskMipChain::MaskMipChain(): mBaseSize(), mDirtyBox(), mDirtyAll(false) {}

void MaskMipChain::markDirty(const QRect& aTexels) {
    if (aTexels.isEmpty())
        return;
    mDirtyBox = mDirtyBox.isNull() ? aTexels : mDirtyBox.united(aTexels);
}

void MaskMipChain::markDirtyAll() { mDirtyAll = true; }

void MaskMipChain::create(gl::Texture& aTexture, const QImage& aLevelZero) {
    XC_ASSERT(!aLevelZero.isNull());
    XC_ASSERT(aLevelZero.format() == QImage::Format_Grayscale8);
    if (aLevelZero.isNull())
        return;

    mBaseSize = aLevelZero.size();
    mDirtyBox = QRect();
    mDirtyAll = false;

    {
        // LEVEL 0 and the chain's storage. Manual is the device: the class allocates every level at
        // its GL-mandated size, states MAX_LEVEL, and books mipmapLevels() and the residency
        // counter for all of them - but builds and draws nothing, because the levels above 0 are
        // filled a box at a time by rebuild(). Level 0 is uploaded from the field's pixels here.
        GlStateGuard guard;
        gl::Global::Functions& ggl = gl::Global::functions();
        ggl.glActiveTexture(GL_TEXTURE0);   // the unit the guard saves and restores
        ggl.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        ggl.glPixelStorei(GL_UNPACK_ROW_LENGTH, aLevelZero.bytesPerLine());
        aTexture.create(
            mBaseSize, aLevelZero.constBits(), GL_RED, GL_R8, GL_UNSIGNED_BYTE, gl::Texture::Manual
        );
        ggl.glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        ggl.glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

        // THE SAMPLING POLICY is the caller's under Manual, so it is set here: trilinear
        // minification (every level that exists is sampled), bilinear magnification (a mipmap enum
        // is invalid there), and the field's edge clamps rather than a border colour - the shaders
        // already treat "outside the rect" as the neutral case.
        aTexture.setFilter(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR);
        aTexture.setWrap(GL_CLAMP_TO_EDGE);
    }

    rebuild(aTexture, QRect(QPoint(0, 0), mBaseSize), true);
}

void MaskMipChain::update(gl::Texture& aTexture, const QImage& aLevelZero) {
    if (!isDirty() || mBaseSize.isEmpty() || aLevelZero.size() != mBaseSize)
        return;

    const QRect field(QPoint(0, 0), mBaseSize);
    const bool wholeField = mDirtyAll;
    const QRect box = wholeField ? field : mDirtyBox.intersected(field);
    mDirtyAll = false;
    mDirtyBox = QRect();
    if (box.isEmpty())
        return;

    // LEVEL 0 is the field itself: one box upload, not the whole image.
    {
        GlStateGuard guard;
        gl::Global::Functions& ggl = gl::Global::functions();
        // unit 0 is the one the guard saves and restores, so the upload touches no other binding
        ggl.glActiveTexture(GL_TEXTURE0);
        ggl.glBindTexture(GL_TEXTURE_2D, aTexture.id());
        ggl.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        ggl.glPixelStorei(GL_UNPACK_ROW_LENGTH, aLevelZero.bytesPerLine());
        ggl.glTexSubImage2D(
            GL_TEXTURE_2D, 0, box.x(), box.y(), box.width(), box.height(), GL_RED, GL_UNSIGNED_BYTE,
            aLevelZero.constScanLine(box.top()) + box.left()
        );
        ggl.glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        ggl.glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        ggl.glBindTexture(GL_TEXTURE_2D, 0);
    }

    rebuild(aTexture, box, wholeField);
}

void MaskMipChain::rebuild(gl::Texture& aTexture, const QRect& aBaseBox, bool aWholeField) {
    const int levels = levelCount(mBaseSize);
    if (levels <= 1 || aBaseBox.isEmpty())
        return;

    GlStateGuard guard;
    // every binding the build makes is on unit 0, the unit the guard saves
    gl::Global::functions().glActiveTexture(GL_TEXTURE0);
    MipPass& pass = mipPass();
    const int exact = exactLevels(mBaseSize);

    QRect box = aBaseBox;
    QSize aboveSize = levelSize(mBaseSize, 0);
    for (int level = 1; level < levels; ++level) {
        const QSize size = levelSize(mBaseSize, level);
        const QRect destBox = coveredBox(box, aboveSize, size);
        if (destBox.isEmpty())
            break;
        // WHICH FINER LEVEL, from the one rule for a whole field (gl::Texture::mipSourceLevel: the
        // level directly above, span 1) and from the deepest exact level for a box
        // level 0 while the footprint is short enough, else the level above) and from the exact
        // anchor for a stroke's box - see the class note
        const int sourceLevel = aWholeField ? gl::Texture::mipSourceLevel(level)
                                            : ((level <= exact) ? level - 1 : exact);
        buildLevel(pass, aTexture.id(), level, destBox, sourceLevel, levelSize(mBaseSize, sourceLevel), size);
        aboveSize = size;
        box = destBox;
    }
}

// -------------------------------------------------------------------------------------------------
// FilterMask

FilterMask::FilterMask():
    mContentRect(), mImage(), mNeutral(0), mTexture(), mMip(), mRevision(0), mUploadedRevision(0),
    mCreated(false) {}

void FilterMask::reset(const QRectF& aContentRect, int aNeutralValue) {
    mContentRect = aContentRect;
    mNeutral = std::min(std::max(aNeutralValue, 0), 255);
    if (aContentRect.isEmpty()) {
        clear();
        return;
    }
    // The texel grid is the content rectangle itself: one texel per content pixel (see the
    // header - no cap, the artwork's own resolution, and no padding: the shaders map content
    // pixels over the rect, so a larger texture would stretch the field).
    const QSize size(
        qMax(1, qRound(aContentRect.width())), qMax(1, qRound(aContentRect.height()))
    );
    mImage = QImage(size, QImage::Format_Grayscale8);
    mImage.fill(uchar(mNeutral));
    mCreated = false;
    markDirtyAll();
}

void FilterMask::clear() {
    if (mImage.isNull())
        return;
    mImage.fill(uchar(mNeutral));
    markDirtyAll();
}

void FilterMask::markDirty(const QRect& aTexels) {
    if (mImage.isNull() || aTexels.isEmpty())
        return;
    const QRect box = aTexels.intersected(mImage.rect());
    if (box.isEmpty())
        return;
    mMip.markDirty(box);
    ++mRevision;
}

void FilterMask::markDirtyAll() {
    if (mImage.isNull())
        return;
    mMip.markDirtyAll();
    ++mRevision;
}

GLuint FilterMask::texture() {
    if (mImage.isNull())
        return 0;
    if (!mCreated) {
        mMip.create(mTexture, mImage);
        mCreated = true;
        mUploadedRevision = mRevision;
        return mTexture.id();
    }
    if (mRevision != mUploadedRevision) {
        mMip.update(mTexture, mImage);
        mUploadedRevision = mRevision;
    }
    return mTexture.id();
}

bool FilterMask::serialize(Serializer& aOut) const {
    aOut.write(mContentRect);
    // the CONTENT's grid, which is also the field's own: the chain is an allocation detail of the
    // texture and never reaches the file
    aOut.write(mImage.size());
    aOut.write(mNeutral);

    QByteArray bytes;
    if (!mImage.isNull()) {
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        if (!mImage.save(&buffer, "PNG"))
            return false;
    }
    aOut.write(XCMemBlock((uint8*)bytes.data(), size_t(bytes.size())));
    return aOut.checkStream();
}

bool FilterMask::deserialize(Deserializer& aIn) {
    aIn.pushLogScope("FilterMask");
    aIn.read(mContentRect);
    QSize size;
    aIn.read(size);
    aIn.read(mNeutral);

    XCMemBlock blob;
    if (!aIn.readWithAlloc(blob)) {
        return aIn.errored("invalid filter mask bytes");
    }
    if (size.isEmpty() || size.width() > kMaxStoredTexels || size.height() > kMaxStoredTexels) {
        delete[] blob.data;
        return aIn.errored("invalid filter mask size");
    }

    QImage image = QImage::fromData(QByteArray::fromRawData((const char*)blob.data, int(blob.size)), "PNG");
    delete[] blob.data;
    if (image.isNull() || image.size() != size) {
        return aIn.errored("invalid filter mask image");
    }

    mImage = image.convertToFormat(QImage::Format_Grayscale8);
    mCreated = false;
    mMip.markDirtyAll();
    ++mRevision;

    aIn.popLogScope();
    return aIn.checkStream();
}

} // namespace core
