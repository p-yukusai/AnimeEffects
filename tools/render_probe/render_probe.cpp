// Headless, deterministic render tool for development.
//
// WHY THIS EXISTS
//
// Live playback and the AE_FRAMEBUFFER_DUMP hook are not a way to look at a frame: a software-GL
// run drops animation frames (so motion artifacts vanish) and a kiosk compositor has no input
// focus. This tool loads a real .anie and renders selected project frames offscreen, with no
// window and no event loop in the frame sequencing, so the same input always writes the same
// bytes.
//
// IT USES THE PRODUCTION PATH
//
// The render call is ctrl::Driver::renderGL - the exact call MainDisplayWidget::paintGL makes -
// against the same core::ClippingFrame / core::FilterFrame / core::DestinationTexturizer triple
// and the same camera wiring (screen size = the framebuffer's size, device pixel ratio 1, image
// size from the project attribute, image centred). Clipping, folder filters, masks and blend
// modes therefore go through the shaders the viewport runs, not a reimplementation.
//
// WHY A QOpenGLWidget (AND NOT QOffscreenSurface)
//
// A bare QOffscreenSurface would be the obvious choice, but the production code hard-requires the
// widget: core::LayerMesh::MeshBuffer::GLBinder and gl::DeviceInfo::load() call
// gl::Global::makeCurrent(), and that function only knows a registered QOpenGLWidget
// (gl::Global::setContext(QOpenGLWidget&)). Project loading builds those mesh buffers, so the tool
// creates a 32x32 QOpenGLWidget with Qt::WA_DontShowOnScreen - no visible window - and falls back
// to a normal (offscreen-platform) show only if the platform refuses to create a context for an
// unmapped widget. There is no event loop after bootstrap; app.processEvents() runs only until the
// context exists.
//
// BACKGROUND
//
// The viewport clears to the current theme's recessed colour. A project may override that colour
// (core::Project::Attribute::viewportBackground), and when it does this tool uses it - that is
// project data. Otherwise it uses the DEFAULT theme's floor (the dark theme the app selects when
// no preference is saved, src/gui/GUIResources.cpp) through the same token table
// (gui/theme/Colors.h) the viewport paints with. That keeps the output deterministic across
// machines instead of following whatever theme happens to be saved locally.
//
// USAGE
//
//   render_probe --project <file.anie> --out <dir> [--size WxH] [--zoom <scale>]
//                [--frames <a>..<b>] [--step <n>] [--offset <px>] [--metrics]
//
//   --size    framebuffer size in pixels (default 640x640)
//   --zoom    camera scale (1.0 = the project's pixels at 1:1); default fits the whole image
//   --frames  inclusive project frame range; default is the project's own frame range
//   --step    project frames advanced per output frame (default 1)
//   --offset  horizontal sub-pixel nudge of the camera centre, in pixels
//   --metrics one line per frame: FRAME <i> sharpness <float> mean <float>
//
// Output is <out>/frame-<index>.png, RGBA8, exactly --size. Run it under
// QT_QPA_PLATFORM=offscreen (no display needed).
//
// Exit codes: 0 success, 1 render/write failure, 2 usage, GL bootstrap or project load failure.
//
// NOTE: this project uses Qt5Compat, which defines the classic `slots` (and `signals`) macros.
// Range-for loop variables must not be named `slot`.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QImage>
#include <QJsonObject>
#include <QOpenGLVersionFunctionsFactory>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLVersionFunctionsFactory>
#include <QScopedPointer>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QSurfaceFormat>
#include <QVector2D>

#include "XC.h"
#include "core/Animator.h"
#include "core/CameraInfo.h"
#include "core/ClippingFrame.h"
#include "core/DestinationTexturizer.h"
#include "core/FilterFrame.h"
#include "core/Frame.h"
#include "core/Project.h"
#include "core/RenderInfo.h"
#include "core/TimeInfo.h"
#include "ctrl/Driver.h"
#include "ctrl/DriverResources.h"
#include "ctrl/GraphicStyle.h"
#include "ctrl/ProjectLoader.h"
#include "ctrl/UILogger.h"
#include "gl/DeviceInfo.h"
#include "gl/Framebuffer.h"
#include "gl/Global.h"
#include "gl/Texture.h"
#include "gl/Util.h"
#include "gl/VertexArrayObject.h"
#include "gui/AudioPlaybackWidget.h"
#include "gui/theme/Colors.h"
#include "util/IProgressReporter.h"
#include "util/Range.h"

namespace {

const char* kUsage =
    "usage: render_probe --project <file.anie> --out <dir> [--size WxH] [--zoom <scale>]\n"
    "                    [--frames <a>..<b>] [--step <n>] [--offset <px>] [--metrics]\n"
    "\n"
    "Renders project frames offscreen through ctrl::Driver::renderGL and writes\n"
    "<out>/frame-<index>.png (RGBA8, exactly --size).\n"
    "\n"
    "  --size     framebuffer size in pixels (default 640x640)\n"
    "  --zoom     camera scale; default fits the whole project image\n"
    "  --frames   inclusive project frame range; default: the project's range\n"
    "  --step     project frames per output frame (default 1)\n"
    "  --offset   horizontal sub-pixel nudge of the camera centre, in pixels\n"
    "  --metrics  print FRAME <i> sharpness <float> mean <float> per frame\n";

int fail(int aCode, const QString& aMessage) {
    std::fprintf(stderr, "render_probe: %s\n", aMessage.toUtf8().constData());
    return aCode;
}

// box-average a supersampled RGBA buffer down to the output size
std::vector<uint8_t> boxDownsample(
    const std::vector<uint8_t>& aSrc, const QSize& aSrcSize, const QSize& aDstSize, int aFactor
) {
    std::vector<uint8_t> dst((size_t)aDstSize.width() * aDstSize.height() * 4, 0);
    for (int y = 0; y < aDstSize.height(); ++y)
        for (int x = 0; x < aDstSize.width(); ++x) {
            int acc[4] = {0, 0, 0, 0};
            for (int dy = 0; dy < aFactor; ++dy)
                for (int dx = 0; dx < aFactor; ++dx) {
                    const size_t p =
                        ((size_t)(y * aFactor + dy) * aSrcSize.width() + (x * aFactor + dx)) * 4;
                    for (int c = 0; c < 4; ++c)
                        acc[c] += aSrc[p + c];
                }
            const size_t q = ((size_t)y * aDstSize.width() + x) * 4;
            const int n = aFactor * aFactor;
            for (int c = 0; c < 4; ++c)
                dst[q + c] = (uint8_t)((acc[c] + n / 2) / n);
        }
    return dst;
}

//-------------------------------------------------------------------------------------------------
// stubs the GUI normally provides

class FrameAnimator: public core::Animator {
public:
    core::Frame currentFrame() const override { return mFrame; }
    void stop() override {}
    void suspend() override {}
    void resume() override {}
    bool isSuspended() const override { return false; }

    void setFrame(int aFrame) { mFrame.set(aFrame); }

private:
    core::Frame mFrame;
};

class NullReporter: public util::IProgressReporter {
public:
    void setSection(const QString&) override {}
    void setMaximum(int) override {}
    void setProgress(int) override {}
    bool wasCanceled() const override { return false; }
};

class NullGraphicStyle: public ctrl::GraphicStyle {
public:
    QFont font() const override { return QFont(); }
    QRect boundingRect(const QString&) const override { return QRect(); }
    QIcon icon(const QString&) const override { return QIcon(); }
};

class NullLogger: public ctrl::UILogger {
public:
    void pushLog(const QString&, ctrl::UILogType) override {}
};

class ProbeAssertHandler: public XCAssertHandler {
public:
    void failure() const override { std::fprintf(stderr, "XC_ASSERT failure\n"); }
};

class ProbeErrorHandler: public XCErrorHandler {
public:
    void critical(const QString& aText, const QString& aInfo, const QString& aDetail) const override {
        std::fprintf(
            stderr, "XC_FATAL_ERROR: %s | %s | %s\n", aText.toUtf8().constData(), aInfo.toUtf8().constData(),
            aDetail.toUtf8().constData());
        std::abort();
    }
};

//-------------------------------------------------------------------------------------------------
// command line

struct Options {
    QString projectPath;
    QString outDir;
    QSize size = QSize(640, 640);
    bool zoomSet = false;
    float zoom = 1.0f;
    bool framesSet = false;
    int frameBegin = 0;
    int frameEnd = 0;
    int step = 1;
    float offset = 0.0f;
    bool metrics = false;
    // render at supersample x supersample and box-average down: the ground truth for "is the 1x
    // render aliased", and a way to look at SSAA before paying for it in the viewport
    int supersample = 1;
};

bool parseSize(const QString& aText, QSize& aOut) {
    QStringList parts = aText.split(QLatin1Char('x'), Qt::SkipEmptyParts);
    if (parts.size() != 2) parts = aText.split(QLatin1Char('X'), Qt::SkipEmptyParts);
    if (parts.size() != 2) return false;
    bool okW = false;
    bool okH = false;
    const int w = parts[0].toInt(&okW);
    const int h = parts[1].toInt(&okH);
    if (!okW || !okH || w <= 0 || h <= 0) return false;
    aOut = QSize(w, h);
    return true;
}

bool parseFrames(const QString& aText, int& aBegin, int& aEnd) {
    const int dots = aText.indexOf(QStringLiteral(".."));
    if (dots < 0) return false;
    bool okBegin = false;
    bool okEnd = false;
    const int begin = aText.left(dots).toInt(&okBegin);
    const int end = aText.mid(dots + 2).toInt(&okEnd);
    if (!okBegin || !okEnd) return false;
    aBegin = begin;
    aEnd = end;
    return true;
}

} // namespace

// The libs reference these globals; src/gui/Main.cpp defines them in the app. A fatal GL error
// aborts instead of continuing on a broken context.
XCAssertHandler* gXCAssertHandler = nullptr;
XCErrorHandler* gXCErrorHandler = nullptr;

// core/Project.cpp references this gui method, but libgui is not linked into the probe (the class
// declaration arrives through core/Project.h), so a lone definition satisfies the linker.
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

namespace {

//-------------------------------------------------------------------------------------------------
// GL bootstrap

// The GL context, offscreen: a QOffscreenSurface plus a QOpenGLContext. No window, no widget, and
// therefore no display server - the platform plugin still has to support GL surfaces, which the
// `offscreen` plugin does through EGL.
class ProbeGL {
public:
    gl::DeviceInfo mDeviceInfo;
    QScopedPointer<gl::VertexArrayObject> mDefaultVao;

    bool init(QApplication& aApp) {
        QSurfaceFormat format;
        format.setVersion(gl::Global::kVersion.first, gl::Global::kVersion.second);
        format.setProfile(QSurfaceFormat::CoreProfile);
        mSurface.setFormat(format);
        mSurface.create();
        if (!mSurface.isValid()) {
            std::fprintf(stderr, "FATAL: offscreen surface could not be created on this platform\n");
            return false;
        }
        mContext.setFormat(format);
        if (!mContext.create()) {
            std::fprintf(stderr, "FATAL: OpenGL context could not be created offscreen\n");
            return false;
        }
        if (!mContext.makeCurrent(&mSurface)) {
            std::fprintf(stderr, "FATAL: offscreen context could not be made current\n");
            return false;
        }
        auto* functions = QOpenGLVersionFunctionsFactory::get<gl::Global::Functions>(&mContext);
        if (!functions || !functions->initializeOpenGLFunctions()) {
            std::fprintf(stderr, "FATAL: failed to get OpenGL 4.0 core functions\n");
            return false;
        }
        gl::Global::setContext(mContext, mSurface);
        gl::Global::setFunctions(*functions);
        mDeviceInfo.load();
        gl::DeviceInfo::setInstance(&mDeviceInfo);
        const char* version = (const char*)mContext.functions()->glGetString(GL_VERSION);
        std::fprintf(stderr, "OpenGL: %s\n", version ? version : "(unknown)");
        // a core profile has no default VAO; the viewport keeps one bound for its lifetime
        mDefaultVao.reset(new gl::VertexArrayObject());
        mDefaultVao->bind();
        aApp.processEvents();
        return true;
    }

private:
    QOffscreenSurface mSurface;
    QOpenGLContext mContext;
};


//-------------------------------------------------------------------------------------------------
// render fixture: the framebuffer half of a RenderInfo, mirroring MainDisplayWidget

struct RenderFixture {
    QSize size;
    QScopedPointer<core::ClippingFrame> clipping;
    QScopedPointer<core::FilterFrame> filters;
    QScopedPointer<core::DestinationTexturizer> texturizer;
    QScopedPointer<gl::Texture> targetTex;
    QScopedPointer<gl::Framebuffer> targetFbo;

    void init(const QSize& aSize) {
        size = aSize;
        clipping.reset(new core::ClippingFrame());
        clipping->resize(aSize);
        filters.reset(new core::FilterFrame());
        filters->resize(aSize);
        texturizer.reset(new core::DestinationTexturizer());
        texturizer->resize(aSize);
        targetTex.reset(new gl::Texture());
        targetTex->create(aSize);
        targetTex->setFilter(GL_LINEAR, GL_LINEAR);
        targetTex->setWrap(GL_CLAMP_TO_EDGE);
        targetFbo.reset(new gl::Framebuffer());
        targetFbo->setColorAttachment(0, targetTex->id());
        XC_ASSERT(targetFbo->isComplete());
    }

    // Renders one project frame and returns the RGBA8 pixels TOP-DOWN (readback is bottom-up).
    std::vector<uint8_t> render(
        core::Project& aProject, ctrl::Driver& aDriver, const Options& aOptions, int aFrame
    ) {
        auto& ggl = gl::Global::functions();

        clipping->clearTexture();
        clipping->resetClippingId();
        texturizer->clearTexture();
        filters->clearAll();

        targetFbo->bind();
        gl::Util::setViewportAsActualPixels(size);

        const core::Project::Attribute& attr = aProject.attribute();

        // The viewport clears to the theme's recessed colour. A saved project override wins (that
        // IS project data); otherwise use the DEFAULT theme's floor rather than the local saved
        // one, so the output is deterministic across machines - GUIResources falls back to the
        // dark theme when no setting exists (src/gui/GUIResources.cpp), and theme::Colors::dark()
        // is the same token table the viewport paints with.
        float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
        if (attr.hasViewportBackground()) {
            const QColor c = attr.viewportBackground();
            r = c.redF();
            g = c.greenF();
            b = c.blueF();
        } else {
            const QColor floor = theme::Colors::dark().recessed;
            r = floor.redF();
            g = floor.greenF();
            b = floor.blueF();
        }
        gl::Util::clearColorBuffer(r, g, b, a);
        gl::Util::resetRenderState();

        core::TimeInfo time = aProject.currentTimeInfo();
        time.frame = core::Frame(aFrame);

        // The supersampled frame is `ss` times larger, so an ABSOLUTE zoom (and a sub-pixel offset
        // given in output pixels) must scale with it, or the framing changes and any comparison
        // against the supersampled reference is meaningless.
        const float ssScale = float(std::max(1, aOptions.supersample));
        core::CameraInfo camera;
        camera.setDevicePixelRatio(1.0);
        camera.setScreenSize(size);
        camera.setImageSize(attr.imageSize());
        camera.setCenter(
            QVector2D(size.width() * 0.5f + aOptions.offset * ssScale, size.height() * 0.5f)
        );
        float scale = 1.0f;
        if (aOptions.zoomSet) {
            scale = aOptions.zoom * ssScale;
        } else {
            const float scaleX = (float)size.width() / (float)attr.imageSize().width();
            const float scaleY = (float)size.height() / (float)attr.imageSize().height();
            scale = scaleX < scaleY ? scaleX : scaleY;
        }
        camera.setScale(scale);
        camera.setFlip(false);

        core::RenderInfo info;
        info.camera = camera;
        info.time = time;
        info.framebuffer = targetFbo->id();
        info.dest = targetTex->id();
        info.isGrid = false;
        info.nonPosed = false;
        info.originMesh = false;
        info.clippingId = 0;
        info.clippingFrame = clipping.data();
        info.destTexturizer = texturizer.data();
        info.filterFrame = filters.data();
        info.opacityScale = 1.0f;

        XC_ASSERT(info.framebuffer != 0);
        XC_ASSERT(info.dest != 0);

        aDriver.renderGL(info, nullptr);
        ggl.glFinish();

        const int w = size.width();
        const int h = size.height();
        const size_t stride = (size_t)w * 4;
        std::vector<uint8_t> raw(stride * (size_t)h, 0);
        ggl.glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());
        targetFbo->release();

        std::vector<uint8_t> topDown(raw.size(), 0);
        for (int y = 0; y < h; ++y) {
            std::memcpy(
                topDown.data() + (size_t)y * stride,
                raw.data() + (size_t)(h - 1 - y) * stride,
                stride
            );
        }
        return topDown;
    }
};

//-------------------------------------------------------------------------------------------------
// metrics

// mean |horizontal gradient| over the RGB channels: the same sharpness the mipmap probe's render
// suite uses, because a blurred frame and a sharper one differ here even when both have the same
// average colour.
double meanHorizontalGradient(const std::vector<uint8_t>& aPixels, int aWidth, int aHeight) {
    if (aWidth < 2 || aHeight < 1) return 0.0;
    double sum = 0.0;
    for (int y = 0; y < aHeight; ++y) {
        const uint8_t* row = aPixels.data() + (size_t)y * aWidth * 4;
        for (int x = 1; x < aWidth; ++x) {
            const uint8_t* cur = row + (size_t)x * 4;
            const uint8_t* prev = cur - 4;
            for (int c = 0; c < 3; ++c) {
                sum += std::abs((int)cur[c] - (int)prev[c]);
            }
        }
    }
    return sum / (double)((aWidth - 1) * aHeight * 3);
}

// mean |channel delta| against the previous frame; all four channels, so a change that only moves
// coverage is visible too.
double meanAbsChannelDelta(const std::vector<uint8_t>& aPrev, const std::vector<uint8_t>& aCur) {
    if (aPrev.size() != aCur.size() || aPrev.empty()) return 0.0;
    double sum = 0.0;
    for (size_t i = 0; i < aPrev.size(); ++i) {
        sum += std::abs((int)aPrev[i] - (int)aCur[i]);
    }
    return sum / (double)aPrev.size();
}

//-------------------------------------------------------------------------------------------------
// run

int runRender(ProbeGL& aProbe, const Options& aOptions) {
    FrameAnimator animator;
    NullReporter reporter;
    gl::DeviceInfo& device = aProbe.mDeviceInfo;
    gl::DeviceInfo::setInstance(&device);

    core::Project project(aOptions.projectPath, animator, nullptr);
    ctrl::ProjectLoader loader;
    if (!loader.load(aOptions.projectPath, project, device, reporter)) {
        std::fprintf(
            stderr, "render_probe: failed to load '%s'\n", aOptions.projectPath.toUtf8().constData());
        for (const QString& line : loader.log()) {
            std::fprintf(stderr, "  %s\n", line.toUtf8().constData());
        }
        return 2;
    }

    const QSize imageSize = project.attribute().imageSize();
    if (imageSize.width() <= 0 || imageSize.height() <= 0) {
        return fail(2, QStringLiteral("the project has an invalid image size (%1x%2)")
                           .arg(imageSize.width())
                           .arg(imageSize.height()));
    }

    int begin = aOptions.framesSet ? aOptions.frameBegin : project.attribute().frameRange().min();
    int end = aOptions.framesSet ? aOptions.frameEnd : project.attribute().frameRange().max();
    if (begin > end) {
        return fail(2, QStringLiteral("the frame range is inverted (%1..%2)").arg(begin).arg(end));
    }

    QDir outDir(aOptions.outDir);
    if (!outDir.exists() && !QDir().mkpath(aOptions.outDir)) {
        return fail(2, QStringLiteral("cannot create the output directory '%1'").arg(aOptions.outDir));
    }

    const int ss = std::max(1, aOptions.supersample);
    const QSize renderSize(aOptions.size.width() * ss, aOptions.size.height() * ss);
    RenderFixture fixture;
    fixture.init(renderSize);

    ctrl::DriverResources resources;
    NullGraphicStyle style;
    NullLogger logger;
    ctrl::Driver driver(project, resources, style, logger);

    std::vector<uint8_t> previous;
    int written = 0;
    for (int frame = begin; frame <= end; frame += aOptions.step) {
        animator.setFrame(frame);
        // The viewport's order: MainWindow updates the frame, then paintGL runs the frameTick /
        // deferred blend before the GL pass.
        driver.updateFrame();
        driver.frameTick();
        driver.flushPendingBlend();

        std::vector<uint8_t> pixels = fixture.render(project, driver, aOptions, frame);
        if (ss > 1)
            pixels = boxDownsample(pixels, renderSize, aOptions.size, ss);

        const QString path = outDir.filePath(QStringLiteral("frame-%1.png").arg(frame));
        const QImage image(
            pixels.data(), aOptions.size.width(), aOptions.size.height(), QImage::Format_RGBA8888
        );
        if (!image.save(path, "PNG")) {
            return fail(1, QStringLiteral("cannot write '%1'").arg(path));
        }

        if (aOptions.metrics) {
            const double sharpness =
                meanHorizontalGradient(pixels, aOptions.size.width(), aOptions.size.height());
            const double mean = previous.empty() ? 0.0 : meanAbsChannelDelta(previous, pixels);
            std::printf("FRAME %d sharpness %f mean %f\n", frame, sharpness, mean);
            std::fflush(stdout);
        }

        previous = std::move(pixels);
        ++written;
    }

    std::fprintf(
        stderr, "render_probe: wrote %d frame(s) at %dx%d (%s) to %s\n", written,
        aOptions.size.width(), aOptions.size.height(),
        aOptions.framesSet ? "requested range" : "project range", qPrintable(outDir.absolutePath()));
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    static ProbeAssertHandler assertHandler;
    static ProbeErrorHandler errorHandler;
    gXCAssertHandler = &assertHandler;
    gXCErrorHandler = &errorHandler;

    Options options;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        const auto value = [&](QString& aOut) -> bool {
            if (i + 1 >= argc) {
                std::fprintf(
                    stderr, "render_probe: %s needs a value\n", arg.toUtf8().constData());
                return false;
            }
            aOut = QString::fromLocal8Bit(argv[++i]);
            return true;
        };

        if (arg == QLatin1String("--project")) {
            if (!value(options.projectPath)) return 2;
        } else if (arg == QLatin1String("--out")) {
            if (!value(options.outDir)) return 2;
        } else if (arg == QLatin1String("--size")) {
            QString text;
            if (!value(text)) return 2;
            if (!parseSize(text, options.size)) {
                return fail(2, QStringLiteral("--size must be WxH with positive sides, got '%1'").arg(text));
            }
        } else if (arg == QLatin1String("--supersample")) {
            QString text;
            if (!value(text)) return 2;
            bool ok = false;
            options.supersample = text.toInt(&ok);
            if (!ok || options.supersample < 1 || options.supersample > 4) {
                std::fprintf(stderr, "render_probe: --supersample must be 1..4\n");
                return 2;
            }
        } else if (arg == QLatin1String("--zoom")) {
            QString text;
            if (!value(text)) return 2;
            bool ok = false;
            const float zoom = text.toFloat(&ok);
            if (!ok || !std::isfinite(zoom) || zoom <= 0.0f) {
                return fail(2, QStringLiteral("--zoom must be a positive number, got '%1'").arg(text));
            }
            options.zoom = zoom;
            options.zoomSet = true;
        } else if (arg == QLatin1String("--frames")) {
            QString text;
            if (!value(text)) return 2;
            if (!parseFrames(text, options.frameBegin, options.frameEnd)) {
                return fail(2, QStringLiteral("--frames must be a..b, got '%1'").arg(text));
            }
            options.framesSet = true;
        } else if (arg == QLatin1String("--step")) {
            QString text;
            if (!value(text)) return 2;
            bool ok = false;
            const int step = text.toInt(&ok);
            if (!ok || step < 1) {
                return fail(2, QStringLiteral("--step must be a positive integer, got '%1'").arg(text));
            }
            options.step = step;
        } else if (arg == QLatin1String("--offset")) {
            QString text;
            if (!value(text)) return 2;
            bool ok = false;
            const float offset = text.toFloat(&ok);
            if (!ok || !std::isfinite(offset)) {
                return fail(2, QStringLiteral("--offset must be a number, got '%1'").arg(text));
            }
            options.offset = offset;
        } else if (arg == QLatin1String("--metrics")) {
            options.metrics = true;
        } else if (arg == QLatin1String("--help") || arg == QLatin1String("-h")) {
            std::printf("%s", kUsage);
            return 0;
        } else {
            std::fprintf(stderr, "render_probe: unknown option '%s'\n", arg.toUtf8().constData());
            std::fprintf(stderr, "%s", kUsage);
            return 2;
        }
    }

    if (options.projectPath.isEmpty() || options.outDir.isEmpty()) {
        std::fprintf(stderr, "render_probe: --project and --out are required\n");
        std::fprintf(stderr, "%s", kUsage);
        return 2;
    }

    {
        QSurfaceFormat format;
        format.setVersion(gl::Global::kVersion.first, gl::Global::kVersion.second);
        format.setProfile(QSurfaceFormat::CoreProfile);
        QSurfaceFormat::setDefaultFormat(format);
    }
    QApplication app(argc, argv);

    ProbeGL probe;
    if (!probe.init(app))
        return 2;

    const int result = runRender(probe, options);

    // Tear GL objects down while the context is still current. runRender's project, driver and
    // fixture are already gone; the widget's own vertex array object goes here.
    gl::DeviceInfo::setInstance(nullptr);
    probe.mDefaultVao.reset();
    gl::Global::doneCurrent();
    return result;
}
