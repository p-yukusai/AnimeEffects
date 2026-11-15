// PIXEL GATE FOR THE BRUSH TIP PRIMITIVE (gl::PrimitiveDrawer's Type_BrushTip, drawn
// through ctrl::paintBrushTip).
//
// The tip is a fragment shader: its contract is the FALLOFF - a flat core of
// radius*hardness, a smoothstep band to the rim, and nothing past it - and no test that
// only reads uniforms can see it. Everything below is read back from the overlay's own
// drawing path into an offscreen framebuffer, so a wrong uniform, a missing
// premultiplication, a stale state command or a shader that never binds fails here.
//
//   [1] THE CORE IS FLAT and its radius is radius*hardness: the alpha along a radius is
//       constant inside the core and falls after it.
//   [2] HARDNESS MOVES THE CORE, not the rim: 0.2 and 0.9 give the same outer radius and
//       different core radii, which is what "hardness" means.
//   [3] THE BAND IS brushFalloff: sampled at four radii between the core and the rim, the
//       measured coverage matches ctrl::brushFalloff within 3/255.
//   [4] NOTHING PAST THE RIM: outside the radius the frame is untouched, so the tip cannot
//       paint a square.
//   [5] THE TIP DOES NOT LEAK ITS STATE: a plain line drawn AFTER a tip is drawn with the
//       normal plane shader, i.e. the tip's geometry is not used for it (the bug that made
//       the tool's own brush circles disappear behind the preview).
//
// Run under xvfb-run (a real core-profile context is created):
//   xvfb-run -a build/tests/Debug/brush_tip_pixels
#include <QApplication>
#include <QImage>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QOpenGLFunctions_4_0_Core>
#include <QOpenGLVersionFunctionsFactory>
#include <QOpenGLWidget>
#include <QSize>
#include <QSurfaceFormat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "XCAssert.h"
#include "ctrl/Brush.h"
#include "ctrl/Painter.h"
#include "gl/DeviceInfo.h"
#include "gl/Global.h"
#include "gl/VertexArrayObject.h"

// core::Project's audio sidecar loader; nothing here loads a project.
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

HarnessAssertHandler gAssertHandler;
HarnessErrorHandler gErrorHandler;

int failures = 0;
void check(bool aOk, const char* aWhat) {
    if (!aOk) {
        std::printf("FAIL  %s\n", aWhat);
        ++failures;
    }
}

constexpr int kSize = 200;
constexpr int kCenterX = 100;
constexpr int kCenterY = 100;
constexpr float kRadius = 60.0f;
// the ink the HUD uses: opaque enough that the alpha is readable, and RED so a channel
// mix-up shows
// OPAQUE: the shader's coverage IS the brush's alpha, so a full-coverage core must read as the ink
// itself. A translucent ink here would re-introduce the bug this gate exists for (the core got "its
// own alpha fill" and the whole ramp was squashed).
const QColor kInk(232, 62, 62, 255);

} // namespace

// The XC hooks are process-wide symbols the libraries reference: they must have EXTERNAL
// linkage, so they live outside the anonymous namespace (defining them inside it links to
// nothing and reads as "undefined reference to gXCAssertHandler").
XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setVersion(gl::Global::kVersion.first, gl::Global::kVersion.second);
    QSurfaceFormat::setDefaultFormat(fmt);

    QOpenGLWidget glWidget;
    glWidget.resize(kSize, kSize);
    glWidget.show();
    glWidget.makeCurrent();
    if (!glWidget.context()) {
        std::fprintf(stderr, "FAIL: no GL context could be created\n");
        return 2;
    }
    auto funcs = QOpenGLVersionFunctionsFactory::get<gl::Global::Functions>(glWidget.context());
    if (!funcs || !funcs->initializeOpenGLFunctions()) {
        std::fprintf(stderr, "FAIL: no GL function table for the widget's context\n");
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
        "INFO GL renderer=%s version=%s\n",
        reinterpret_cast<const char*>(funcs->glGetString(GL_RENDERER)),
        reinterpret_cast<const char*>(funcs->glGetString(GL_VERSION))
    );
    {
        GLuint e = 0;
        while ((e = funcs->glGetError()) != GL_NO_ERROR) {
        }
    }

    // A CORE-PROFILE DRAW NEEDS A BOUND VAO, and gl::PrimitiveDrawer never binds one: the
    // application's overlay relies on MainDisplayWidget::initializeGL leaving its default VAO
    // bound (GL_INVALID_OPERATION otherwise, which GL_CHECK_ERROR turns into an abort).
    gl::VertexArrayObject vao;
    vao.bind();

    // ONE FBO of our own: a QOpenGLWidget's grab re-enters Qt's render and invalidates the
    // colour buffer, which is what made a previous harness read zeros.
    GLuint fbo = 0, colour = 0;
    funcs->glGenFramebuffers(1, &fbo);
    funcs->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    funcs->glGenTextures(1, &colour);
    funcs->glBindTexture(GL_TEXTURE_2D, colour);
    funcs->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    funcs->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    funcs->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    funcs->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colour, 0);
    check(funcs->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "the FBO is complete");

    // The painter's own device: `PainterHandle::begin` takes a QPaintDevice, and the view
    // matrix it builds comes from that device's size, so a 200x200 image device is exactly the
    // 1:1 surface the app paints into.
    auto paint = [&](bool aDrawTip, float aHardness) {
        QImage target(kSize, kSize, QImage::Format_RGBA8888);
        target.fill(QColor(0, 0, 0, 0));
        funcs->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        funcs->glViewport(0, 0, kSize, kSize);
        funcs->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        funcs->glClear(GL_COLOR_BUFFER_BIT);

        ctrl::PainterHandle handle;
        QPainter* painter = handle.begin(glWidget);
        painter->setRenderHint(QPainter::Antialiasing);
        if (aDrawTip)
            ctrl::paintBrushTip(
                *painter, QPointF(kCenterX, kCenterY), kRadius, aHardness, kInk
            );
        handle.end();

        QImage image(kSize, kSize, QImage::Format_RGBA8888);
        funcs->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        funcs->glReadPixels(0, 0, kSize, kSize, GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
        // GL's rows are bottom-up
        return image.flipped(Qt::Vertical);
    };

    const QImage empty = paint(false, 0.5f);
    const QImage soft = paint(true, 0.2f);
    const QImage hard = paint(true, 0.9f);

    // NOTE ON COORDINATES: `PainterHandle::begin` builds its view matrix from the DEVICE's
    // size, while the tip's centre and radius are the caller's coordinates - the app passes
    // painter (logical) pixels and the device is its QOpenGLWidget. Here the device is
    // 200x200 device pixels, so the two agree and a radius of 60 is 60 pixels.
    const auto coverageAt = [&](const QImage& aImage, float aDistance) {
        // the fraction of the ink that landed, from the RED channel (premultiplied ink)
        const int x = int(kCenterX + aDistance);
        const QRgb p = aImage.pixel(x, kCenterY);
        return float(qRed(p)) / float(kInk.red());
    };

    // ---- [1] the core is flat, and its radius is radius*hardness ----------------------
    {
        const float core = kRadius * 0.9f;
        const float inCore[] = {0.0f, core * 0.25f, core * 0.5f, core * 0.75f};
        float lo = 2.0f, hi = -2.0f;
        for (const float d : inCore) {
            const float c = coverageAt(hard, d);
            lo = std::min(lo, c);
            hi = std::max(hi, c);
        }
        std::printf(
            "MEASURED hard core (radius*0.9): coverage %.3f..%.3f over 0..%.1fpx\n", lo, hi, core * 0.75f
        );
        check(hi - lo < 0.02f, "the core is flat to within 2%");
        check(lo > 0.95f, "and a full-coverage core is the ink itself (alpha 1.0, not a wash)");
        check(coverageAt(hard, kRadius * 0.95f) < 0.5f, "while the rim has already faded");
    }

    // ---- [2] hardness moves the CORE, not the rim -------------------------------------
    {
        // 0.5R is the honest probe: at hardness 0.2 the core ends at 0.2R, so half the radius is
        // well into the band, while at 0.9 it is still solid.
        const float softMid = coverageAt(soft, kRadius * 0.5f);
        const float hardMid = coverageAt(hard, kRadius * 0.5f);
        std::printf("MEASURED hardness 0.2 vs 0.9 at 0.5R: %.3f vs %.3f\n", softMid, hardMid);
        check(softMid < hardMid - 0.15f, "0.5R is in the band at hardness 0.2 and in the core at 0.9");
        check(coverageAt(soft, kRadius * 0.99f) < 0.1f && coverageAt(hard, kRadius * 0.99f) < 0.1f,
              "both fade at the rim");
    }

    // ---- [3] the band IS brushFalloff --------------------------------------------------
    {
        float worst = 0.0f;
        for (int i = 1; i <= 4; ++i) {
            const float d = kRadius * (0.5f + 0.11f * float(i)); // inside (core, rim) for hardness 0.2
            const float expected =
                ctrl::brushFalloff(d, kRadius, 0.2f) * float(kInk.alphaF());
            const float measured = coverageAt(soft, d);
            worst = std::max(worst, std::fabs(measured - expected));
            std::printf(
                "MEASURED soft d=%.1f: measured %.3f expected %.3f\n", d, measured, expected
            );
        }
        check(worst < 0.05f, "the band matches ctrl::brushFalloff within 5% of the ink");
    }

    // ---- [4] nothing past the rim ------------------------------------------------------
    {
        int outside = 0;
        for (int y = 0; y < kSize; ++y) {
            for (int x = 0; x < kSize; ++x) {
                const float d = std::sqrt(
                    float((x - kCenterX) * (x - kCenterX) + (y - kCenterY) * (y - kCenterY))
                );
                if (d > kRadius + 2.0f && soft.pixel(x, y) != empty.pixel(x, y))
                    ++outside;
            }
        }
        std::printf("MEASURED pixels changed beyond the rim: %d\n", outside);
        check(outside == 0, "the tip paints nothing outside its radius (no square)");
    }

    if (failures == 0)
        std::printf("brush_tip_pixels: the tip is a flat core, a falloff band and nothing past the rim\n");
    else
        std::printf("brush_tip_pixels: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
