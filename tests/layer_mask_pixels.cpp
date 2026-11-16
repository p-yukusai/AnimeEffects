// Pixel gate for the LAYER MASK (assets/shaders/LayerDrawingFrag.glsl's LAYER_MASK
// variation) on a real GL context.
//
// Why a pixel gate: a mask is one texture read and one multiply, and every way it can
// be wrong is a PIXEL fact - the field sampled in the wrong space, a uniform bound to
// the wrong unit, a multiply that lands on the wrong channel, or an alpha that is
// written although the field says hidden. None of that is visible to a test that only
// checks that a key serialized.
//
// The harness compiles the SHIPPED variant through core::ShaderHolder (the same call
// the renderer makes, including the LAYER_MASK dimension) and draws one fullscreen
// quad into its own FBO:
//
//   [1] NO MASK: the same shader without the matte is opaque everywhere - the control
//       that makes [2] mean something.
//   [2] A HALF FIELD MASKS HALF: a field that is 0 on its left half and 255 on its
//       right leaves the right half opaque and the left half fully transparent.
//   [3] PARTIAL VALUES ARE PARTIAL: a soft edge (a gradient field) produces
//       intermediate alpha, monotonic across the edge - a mask is not a stencil.
//   [4] OUTSIDE THE FIELD THE LAYER IS VISIBLE: a field smaller than the quad leaves
//       everything outside it at full opacity. A field RESTRICTS; it never enables.
//   [5] THE FOLDER PATH: a folder presents an ISOLATED COMPOSITE, so its matte is
//       one fullscreen pass over that composite (core::FilterFrame::maskTexture)
//       rather than a fragment in the layer shader. Its composite is PREMULTIPLIED,
//       and the pass must scale colour WITH alpha - scaling alpha alone would
//       brighten what survives, which is invisible on an opaque red fixture and
//       obvious on a semi-transparent edge.
//   [6] THE FIELD LIVES IN CONTENT SPACE: scaling the screen->content matrix shrinks
//       the masked region with it, which is what makes the matte stick to the artwork
//       while the camera zooms or the layer moves.
//
// Run under xvfb-run (real core-profile context, own FBO):
//   xvfb-run -a build/tests/Debug/layer_mask_pixels
#include <QApplication>
#include <QImage>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QOpenGLFunctions_4_0_Core>
#include <QOpenGLVersionFunctionsFactory>
#include <QOpenGLWidget>
#include <QSize>
#include <QSurfaceFormat>
#include <QVector2D>
#include <QVector4D>
#include <array>
#include <cstdio>
#include <memory>
#include <cstdlib>
#include <vector>

#include "XCAssert.h"
#include "core/FilterFrame.h"
#include "core/ShaderHolder.h"
#include "core/WavePass.h"
#include "gl/DeviceInfo.h"
#include "gl/Global.h"
#include "gl/Texture.h"
#include "gl/Vector3.h"
#include "gl/VertexArrayObject.h"
#include "gl/Vector3.h"

// core::Project.cpp's audio sidecar loader; nothing here loads a project.
#include "gui/AudioPlaybackWidget.h"
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

namespace {

class HarnessAssertHandler: public XCAssertHandler {
public:
    void failure() const override { std::abort(); }
};
class HarnessErrorHandler: public XCErrorHandler {
public:
    void critical(const QString&, const QString&, const QString&) const override { std::abort(); }
};
class NullReporter: public util::IProgressReporter {
public:
    void setSection(const QString&) override {}
    void setMaximum(int) override {}
    void setProgress(int) override {}
    bool wasCanceled() const override { return false; }
};

HarnessAssertHandler gAssertHandler;
HarnessErrorHandler gErrorHandler;

int gFailures = 0;
int gMeasured = 0;

void check(bool aOk, const char* aWhat) {
    ++gMeasured;
    if (!aOk) {
        std::printf("FAIL  %s\n", aWhat);
        ++gFailures;
    }
}

constexpr int kW = 128;
constexpr int kH = 128;

// One draw of a fullscreen quad through the layer shader, with an optional matte.
class Probe {
public:
    bool init(QOpenGLFunctions_4_0_Core* aGgl) {
        mGgl = aGgl;
        mGgl->glGenFramebuffers(1, &mFbo);
        mGgl->glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
        mGgl->glGenTextures(1, &mColor);
        mGgl->glBindTexture(GL_TEXTURE_2D, mColor);
        mGgl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kW, kH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        mGgl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        mGgl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        mGgl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mColor, 0);
        if (mGgl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            return false;

        // the destination the shader blend reads: fully transparent
        mGgl->glGenTextures(1, &mDest);
        mGgl->glBindTexture(GL_TEXTURE_2D, mDest);
        std::vector<unsigned char> zeros(kW * kH * 4, 0);
        mGgl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kW, kH, 0, GL_RGBA, GL_UNSIGNED_BYTE, zeros.data());
        mGgl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        mGgl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        // the layer's own content: opaque red, so "visible" is red and "hidden" is
        // whatever the clear colour is
        mGgl->glGenTextures(1, &mLayer);
        mGgl->glBindTexture(GL_TEXTURE_2D, mLayer);
        std::vector<unsigned char> red(kW * kH * 4);
        for (int i = 0; i < kW * kH; ++i) {
            red[i * 4 + 0] = 255;
            red[i * 4 + 1] = 0;
            red[i * 4 + 2] = 0;
            red[i * 4 + 3] = 255;
        }
        mGgl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kW, kH, 0, GL_RGBA, GL_UNSIGNED_BYTE, red.data());
        mGgl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        mGgl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        // the VAO has to exist and be bound BEFORE any attribute is uploaded: in a
        // core profile glVertexAttribPointer without one is GL_INVALID_OPERATION,
        // which the app's BufferObject turns into an assert
        mVao = std::make_unique<gl::VertexArrayObject>();

        mGgl->glGenTextures(1, &mMask);
        mGgl->glBindTexture(GL_TEXTURE_2D, mMask);
        mGgl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        mGgl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        mGgl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        mGgl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        return true;
    }

    // aField: the matte (nullptr = no mask). aRect: where it lives, in content
    // pixels. aScreenToLayer maps a framebuffer pixel to a content pixel.
    std::vector<unsigned char> draw(
        const QImage* aField, const QRectF& aRect, const QMatrix4x4& aScreenToLayer, bool aUseMask
    ) {
        // the matte texture
        if (aUseMask && aField) {
            const QImage rgba = aField->convertToFormat(QImage::Format_RGBA8888);
            mGgl->glBindTexture(GL_TEXTURE_2D, mMask);
            mGgl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            mGgl->glTexImage2D(
                GL_TEXTURE_2D, 0, GL_RGBA8, rgba.width(), rgba.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                rgba.constBits()
            );
        }

        core::ShaderHolder holder;
        // the renderer reserves the variant before it draws (LayerNode::reserveShaders);
        // shader() is an accessor and asserts on an uncompiled slot
        holder.reserveShader(img::BlendMode_Normal, false, false, aUseMask, false);
        auto& shader = holder.shader(img::BlendMode_Normal, false, false, aUseMask, false);

        mGgl->glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
        mGgl->glViewport(0, 0, kW, kH);
        mGgl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        mGgl->glClear(GL_COLOR_BUFFER_BIT);

        // fullscreen quad in NDC (the vertex shader maps it back to screen px)
        static const gl::Vector4 positions[4] = {
            {-1.0f, -1.0f, 0.0f, 1.0f},
            {1.0f, -1.0f, 0.0f, 1.0f},
            {1.0f, 1.0f, 0.0f, 1.0f},
            {-1.0f, 1.0f, 0.0f, 1.0f},
        };
        static const gl::Vector2 texCoords[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};

        mGgl->glActiveTexture(GL_TEXTURE0);
        mGgl->glBindTexture(GL_TEXTURE_2D, mLayer);
        mGgl->glActiveTexture(GL_TEXTURE1);
        mGgl->glBindTexture(GL_TEXTURE_2D, mDest);
        if (aUseMask) {
            mGgl->glActiveTexture(GL_TEXTURE3);
            mGgl->glBindTexture(GL_TEXTURE_2D, mMask);
            mGgl->glActiveTexture(GL_TEXTURE0);
        }

        mVao->bind();
        shader.bind();
        shader.setAttributeArray("inPosition", positions, 4);
        shader.setAttributeArray("inTexCoord", texCoords, 4);
        shader.setUniformValue("uViewMatrix", QMatrix4x4());
        shader.setUniformValue("uScreenSize", QSizeF(kW, kH));
        shader.setUniformValue("uImageSize", QSizeF(kW, kH));
        shader.setUniformValue("uTexCoordOffset", QVector2D(0, 0));
        shader.setUniformValue("uColor", QColor(255, 255, 255, 255));
        shader.setUniformValue("uTexture", 0);
        shader.setUniformValue("uDestTexture", 1);
        if (aUseMask) {
            shader.setUniformValue("uLayerMask", 3);
            shader.setUniformValue(
                "uLayerMaskRect", QVector4D(float(aRect.x()), float(aRect.y()), float(aRect.width()), float(aRect.height()))
            );
            shader.setUniformValue("uScreenToLayer", core::waveAffine3(aScreenToLayer));
        }

        mGgl->glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        mVao->release();
        shader.release();

        std::vector<unsigned char> pixels(size_t(kW) * kH * 4);
        mGgl->glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
        mGgl->glReadPixels(0, 0, kW, kH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        mGgl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        mGgl->glActiveTexture(GL_TEXTURE0);
        mGgl->glBindTexture(GL_TEXTURE_2D, 0);
        return pixels;
    }

    ~Probe() {
        if (!mGgl)
            return;
        mGgl->glDeleteTextures(1, &mColor);
        mGgl->glDeleteTextures(1, &mDest);
        mGgl->glDeleteTextures(1, &mLayer);
        mGgl->glDeleteTextures(1, &mMask);
        mGgl->glDeleteFramebuffers(1, &mFbo);
    }

private:
    QOpenGLFunctions_4_0_Core* mGgl = nullptr;
    GLuint mFbo = 0;
    GLuint mColor = 0;
    GLuint mDest = 0;
    GLuint mLayer = 0;
    GLuint mMask = 0;
    std::unique_ptr<gl::VertexArrayObject> mVao;
};

unsigned char alphaAt(const std::vector<unsigned char>& aPixels, int aX, int aY) {
    return aPixels[size_t((aY * kW + aX) * 4 + 3)];
}

// Count of pixels whose alpha is above aThreshold.
int opaqueCount(const std::vector<unsigned char>& aPixels, int aThreshold = 8) {
    int count = 0;
    for (int i = 0; i < kW * kH; ++i) {
        if (aPixels[size_t(i * 4 + 3)] > aThreshold)
            ++count;
    }
    return count;
}

// content <=> screen for these probes: one content pixel per screen pixel, with the
// content's origin at the quad's top-left.
QMatrix4x4 identityMapping() {
    QMatrix4x4 m;
    m.scale(1.0f, -1.0f);          // glReadPixels' y is bottom-up, content y is top-down
    m.translate(0.0f, float(-kH)); // ...and the quad's top row is row 0 of the field
    return m;
}

// A field on the SCREEN'S OWN grid, so uv == q / kW and every expectation below is
// the shader's own formula: 0 on the left half, 255 on the right, an 8px ramp.
QImage halfField() {
    QImage field(kW, kH, QImage::Format_Grayscale8);
    for (int y = 0; y < kH; ++y) {
        uchar* row = field.scanLine(y);
        for (int x = 0; x < kW; ++x) {
            const int edge = kW / 2;
            const int ramp = 8;
            int v = 0;
            if (x > edge + ramp)
                v = 255;
            else if (x > edge - ramp)
                v = (x - (edge - ramp)) * 255 / (2 * ramp);
            row[x] = uchar(v);
        }
    }
    return field;
}

QImage uniformField(uchar aValue) {
    QImage field(kW, kH, QImage::Format_Grayscale8);
    field.fill(aValue);
    return field;
}

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setVersion(gl::Global::kVersion.first, gl::Global::kVersion.second);
    QSurfaceFormat::setDefaultFormat(fmt);

    QOpenGLWidget glWidget;
    glWidget.resize(kW, kH);
    glWidget.show();
    glWidget.makeCurrent();
    if (!glWidget.context()) {
        std::fprintf(stderr, "layer_mask_pixels: no GL context\n");
        return 2;
    }
    auto* funcs = QOpenGLVersionFunctionsFactory::get<gl::Global::Functions>(glWidget.context());
    if (!funcs || !funcs->initializeOpenGLFunctions()) {
        std::fprintf(stderr, "layer_mask_pixels: no GL function table\n");
        return 2;
    }
    gl::Global::setContext(glWidget);
    gl::Global::setFunctions(*funcs);

    gl::DeviceInfo dev;
    dev.vender = "harness";
    dev.renderer = "harness";
    dev.version = "5";
    dev.maxTextureSize = 16384;
    dev.maxRenderBufferSize = 16384;
    gl::DeviceInfo::setInstance(&dev);

    std::printf(
        "INFO GL renderer=%s\n", reinterpret_cast<const char*>(funcs->glGetString(GL_RENDERER))
    );

    {
        GLuint e = 0;
        while ((e = funcs->glGetError()) != GL_NO_ERROR) {
        }
    }

    Probe probe;
    if (!probe.init(funcs)) {
        std::printf("layer_mask_pixels: no framebuffer\n");
        return 2;
    }

    const QMatrix4x4 mapping = identityMapping();
    const QRectF fullRect(0.0, 0.0, float(kW), float(kH));
    const QImage half = halfField();

    // [1] NO MASK: the control.
    {
        const auto pixels = probe.draw(nullptr, fullRect, mapping, false);
        const int opaque = opaqueCount(pixels);
        std::printf("MEASURED no mask: %d of %d px opaque\n", opaque, kW * kH);
        check(opaque == kW * kH, "without the matte the layer is opaque everywhere");
    }

    // [2] A HALF FIELD MASKS HALF.
    {
        const auto pixels = probe.draw(&half, fullRect, mapping, true);
        const int leftOpaque = alphaAt(pixels, 8, 64) > 8 ? 1 : 0;
        const int rightOpaque = alphaAt(pixels, kW - 8, 64) > 8 ? 1 : 0;
        const int opaque = opaqueCount(pixels);
        std::printf(
            "MEASURED half field: left=%d right=%d opaque=%d of %d\n", leftOpaque, rightOpaque, opaque,
            kW * kH
        );
        check(rightOpaque == 1, "the visible half of the field keeps the layer");
        check(leftOpaque == 0, "the hidden half of the field removes the layer");
        check(opaque > kW * kH / 4 && opaque < kW * kH * 3 / 4, "roughly half the quad survives");
    }

    // [3] PARTIAL VALUES ARE PARTIAL: the soft edge is monotonic.
    {
        const auto pixels = probe.draw(&half, fullRect, mapping, true);
        int prev = -1;
        bool monotonic = true;
        int intermediates = 0;
        for (int x = kW / 2 - 12; x < kW / 2 + 12; ++x) {
            const int a = alphaAt(pixels, x, 64);
            if (prev >= 0 && a < prev - 2)
                monotonic = false;
            if (a > 8 && a < 247)
                ++intermediates;
            prev = a;
        }
        std::printf(
            "MEASURED soft edge: %d intermediate columns, monotonic=%d\n", intermediates,
            monotonic ? 1 : 0
        );
        check(monotonic, "the mask's edge alpha rises monotonically");
        check(intermediates >= 8, "a gradient field produces partial alpha, not a stencil");
    }

    // [4] OUTSIDE THE FIELD: visible.
    {
        const QImage hiddenField = uniformField(0);  // hides everything INSIDE
        const QRectF small(0.0, 0.0, 32.0, 32.0);    // ...but only a corner
        const auto pixels = probe.draw(&hiddenField, small, mapping, true);
        const int inside = alphaAt(pixels, 4, kH - 6) > 8 ? 1 : 0;  // y up = content top
        const int outside = alphaAt(pixels, kW / 2, kH / 2) > 8 ? 1 : 0;
        const int opaque = opaqueCount(pixels);
        std::printf(
            "MEASURED field rect: inside=%d outside=%d opaque=%d of %d\n", inside, outside, opaque,
            kW * kH
        );
        check(inside == 0, "a hidden field removes the layer where it covers it");
        check(outside == 1, "outside the field the layer is visible");
        check(opaque > kW * kH / 2, "the field restricts, it does not enable");
    }

    // [5] THE FOLDER PATH: premultiplied, so colour and alpha scale together.
    {
        // the pass uploads its attributes, and a core profile needs a bound VAO for that
        gl::VertexArrayObject vao;
        vao.bind();

        core::FilterFrame frame;
        frame.resize(QSize(kW, kH));
        const int srcSlot = frame.acquire();
        const int outSlot = frame.acquire();

        // a HALF-TRANSPARENT composite: premultiplied (128,0,0) with alpha 128 is
        // straight red at 50%; if the pass scaled only alpha the surviving pixels
        // would come out (128,0,0,64) - twice as bright as they should be
        std::vector<unsigned char> straight(size_t(kW) * kH * 4);
        for (int i = 0; i < kW * kH; ++i) {
            straight[size_t(i * 4 + 0)] = 128;
            straight[size_t(i * 4 + 1)] = 0;
            straight[size_t(i * 4 + 2)] = 0;
            straight[size_t(i * 4 + 3)] = 128;
        }
        gl::Texture composite;
        composite.create(QSize(kW, kH), straight.data());
        composite.setFilter(GL_NEAREST);
        composite.setWrap(GL_CLAMP_TO_EDGE);

        const QImage half = halfField();
        gl::Texture maskTexture;
        const QImage maskRgba = half.convertToFormat(QImage::Format_RGBA8888);
        maskTexture.create(half.size(), maskRgba.constBits());
        maskTexture.setFilter(GL_NEAREST);
        maskTexture.setWrap(GL_CLAMP_TO_EDGE);

        frame.maskTexture(outSlot, composite.id(), maskTexture.id(), fullRect, mapping);

        QImage out(kW, kH, QImage::Format_RGBA8888);
        frame.bind(outSlot);
        funcs->glReadPixels(0, 0, kW, kH, GL_RGBA, GL_UNSIGNED_BYTE, out.bits());

        const auto rgba = [&](int x, int y) {
            const uchar* row = out.constScanLine(kH - 1 - y);
            return std::array<int, 4>{row[x * 4], row[x * 4 + 1], row[x * 4 + 2], row[x * 4 + 3]};
        };
        const auto kept = rgba(8, 64);         // hidden half
        const auto survived = rgba(kW - 8, 64);  // visible half
        std::printf(
            "MEASURED folder pass: hidden=(%d,%d,%d,%d) visible=(%d,%d,%d,%d)\n", kept[0], kept[1],
            kept[2], kept[3], survived[0], survived[1], survived[2], survived[3]
        );
        check(kept[3] <= 2, "the folder pass removes the hidden half");
        check(
            survived[3] >= 120 && survived[3] <= 136,
            "the visible half keeps its own alpha"
        );
        check(
            std::abs(survived[0] - 128) <= 6,
            "and its premultiplied COLOUR (scaling alpha alone would leave it at 128 over 64)"
        );

        frame.release(outSlot);
        frame.release(srcSlot);
        vao.release();
    }

    // [6] CONTENT SPACE: halving the matrix halves the masked region.
    {
        const QImage hiddenField = uniformField(0);
        // a field that covers a CORNER of the content, so the mapping's scale is what
        // decides how much of the screen it covers
        const QRectF full(0.0, 0.0, 32.0, 32.0);
        const auto wide = probe.draw(&hiddenField, full, mapping, true);
        QMatrix4x4 halfScale;
        halfScale.scale(0.5f, 0.5f);
        const auto narrow = probe.draw(&hiddenField, full, halfScale * mapping, true);
        const int wideHidden = kW * kH - opaqueCount(wide);
        const int narrowHidden = kW * kH - opaqueCount(narrow);
        std::printf(
            "MEASURED content mapping: hidden %d px at 1:1, %d px at half scale\n", wideHidden,
            narrowHidden
        );
        // q = scale * p, so halving the mapping DOUBLES the field's extent on screen (32x32
        // content px cover 32x32 screen px at 1:1 and 64x64 at half scale): the mask
        // follows the CONTENT, not the window.
        check(narrowHidden > wideHidden * 3, "the field follows the content mapping, not the screen");
    }

    glWidget.doneCurrent();

    if (gFailures == 0)
        std::printf("layer_mask_pixels: %d measurements, the matte multiplies the present alpha\n", gMeasured);
    else
        std::printf("layer_mask_pixels: %d of %d FAILED\n", gFailures, gMeasured);
    return gFailures == 0 ? 0 : 1;
}
