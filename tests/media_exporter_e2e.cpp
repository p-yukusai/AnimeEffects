// Headless export harness (real path): loads a real .anie, renders a few frames
// offscreen (GL via a QOpenGLWidget, like the GUI), and exports via
// ctrl::Exporter (which now uses the in-process libav* VideoEncoder/AudioMixer),
// then verifies the output with ffprobe.
//
// This is the end-to-end gate for the media/export rearchitecture on REAL
// project files. Links assets.qrc so the GL render can find :/shaders.
//
// Run: QT_QPA_PLATFORM=xcb ./build/tests/Debug/media_exporter_e2e <file.anie>
#include <QApplication>
#include <QOpenGLWidget>
#include <QOpenGLVersionFunctionsFactory>
#include <QOpenGLFunctions_4_0_Core>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <cstdio>
#include <memory>

#include "XCAssert.h"
#include "gl/Global.h"
#include "gl/DeviceInfo.h"
#include "core/Project.h"
#include "core/Animator.h"
#include "ctrl/Exporter.h"
#include "ctrl/VideoFormat.h"
#include "ctrl/ProjectLoader.h"
#include "gui/AudioPlaybackWidget.h"
#include "util/IProgressReporter.h"

// Project.cpp references the GUI AudioPlaybackWidget::deserialize (only to load
// a .aemus sidecar, which this harness never has). Provide a stub.
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

static int failures = 0;
static void check(bool ok, const char* what) {
    QTextStream(stdout) << (ok ? "PASS  " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

class NullAnimator : public core::Animator {
public:
    core::Frame currentFrame() const override { return core::Frame(); }
    void stop() override {}
    void suspend() override {}
    void resume() override {}
    bool isSuspended() const override { return false; }
};

class NullReporter : public util::IProgressReporter {
public:
    void setSection(const QString&) override {}
    void setMaximum(int) override {}
    void setProgress(int) override {}
    bool wasCanceled() const override { return false; }
};

namespace {
class HarnessAssertHandler : public XCAssertHandler {
public:
    void failure() const override { std::abort(); }
};
class HarnessErrorHandler : public XCErrorHandler {
public:
    void critical(const QString& t, const QString&, const QString&) const override {
        std::fprintf(stderr, "XC_FATAL %s\n", qPrintable(t));
        std::abort();
    }
};
} // namespace
XCAssertHandler* gXCAssertHandler = nullptr;
XCErrorHandler* gXCErrorHandler = nullptr;
static HarnessAssertHandler gAssertH;
static HarnessErrorHandler gErrorH;

static gl::DeviceInfo makeDeviceInfo() {
    gl::DeviceInfo dev;
    dev.vender = "harness";
    dev.renderer = "harness";
    dev.version = "5";
    dev.maxTextureSize = 16384;
    dev.maxRenderBufferSize = 16384;
    return dev;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    gXCAssertHandler = &gAssertH;
    gXCErrorHandler = &gErrorH;

    if (argc < 2) {
        QTextStream(stdout) << "usage: " << argv[0] << " <file.anie>\n";
        return 2;
    }
    const QString src = QString::fromLocal8Bit(argv[1]);

    // Live GL context (same as roundtrip).
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setVersion(gl::Global::kVersion.first, gl::Global::kVersion.second);
    fmt.setSamples(4); // match the app's MainWindow GL setup
    QSurfaceFormat::setDefaultFormat(fmt);
    QOpenGLWidget glWidget;
    glWidget.resize(4, 4);
    glWidget.show();
    glWidget.makeCurrent();
    if (!glWidget.context()) { QTextStream(stdout) << "FAIL  GL context\n"; return 1; }
    auto funcs = QOpenGLVersionFunctionsFactory::get<gl::Global::Functions>(glWidget.context());
    if (!funcs || !funcs->initializeOpenGLFunctions()) { QTextStream(stdout) << "FAIL  GL funcs\n"; return 1; }
    gl::Global::setContext(glWidget);
    gl::Global::setFunctions(*funcs);
    gl::DeviceInfo dev = makeDeviceInfo();
    gl::DeviceInfo::setInstance(&dev);

    // Clear any stale GL error from context/shader setup so we only attribute
    // errors observed during the exporter's render.
    { GLuint e = 0; do { e = funcs->glGetError(); } while (e != GL_NO_ERROR && e <= 0x1295); }
    QTextStream(stdout) << "INFO  GL renderer: "
                        << QString::fromUtf8(reinterpret_cast<const char*>(funcs->glGetString(GL_RENDERER)))
                        << " | version: "
                        << QString::fromUtf8(reinterpret_cast<const char*>(funcs->glGetString(GL_VERSION)))
                        << "\n";

    NullAnimator animator;
    NullReporter reporter;

    // Load the real project.
    core::Project project(src, animator, nullptr);
    ctrl::ProjectLoader loader;
    if (!loader.load(src, project, dev, reporter)) {
        QTextStream(stdout) << "FAIL  load: " << loader.log().join("\n") << "\n";
        return 1;
    }
    QTextStream(stdout) << "=== media exporter e2e: " << src << " ===\n";
    const int nativeW = project.attribute().imageSize().width();
    const int nativeH = project.attribute().imageSize().height();
    const int fps = project.attribute().fps() > 0 ? project.attribute().fps() : 24;
    const int minFrame = project.attribute().frameRange().min();
    const int maxFrame = std::min(project.attribute().frameRange().max(), minFrame + 1);
    QTextStream(stdout) << "  project " << nativeW << "x" << nativeH << " @ " << fps
                        << "fps, frames " << minFrame << ".." << maxFrame << "\n";

    const QString out = QDir::temp().filePath("ae_e2e.mp4");
    QFile::remove(out);

    // A fixed small export size for the synthetic scenarios (independent of the
    // project's native size, so these run fast and deterministic).
    const QSize small(320, 180);

    // Build an mp4/libx264 format (the harness's canonical container/codec).
    auto makeMp4 = []() {
        ctrl::VideoFormat vf;
        vf.name = "mp4"; vf.label = "MP4"; vf.icodec = "ppm";
        ctrl::VideoCodec c;
        c.name = "libx264"; c.label = "H.264"; c.icodec = "ppm";
        c.pixfmts = QStringList{ QStringLiteral("yuv420p") };
        vf.codecs.push_back(c);
        return vf;
    };
    // webm/libvpx-vp9 @ yuva420p carries alpha as packet side data
    // (alpha_mode=1); the alpha is recovered by the libvpx decoder. This is the
    // primary alpha-capable animation container for the web.
    auto makeWebmVp9 = []() {
        ctrl::VideoFormat vf;
        vf.name = "webm"; vf.label = "WebM"; vf.icodec = "ppm";
        ctrl::VideoCodec c;
        c.name = "libvpx-vp9"; c.label = "VP9"; c.icodec = "ppm";
        c.transparent = true;
        c.pixfmts = QStringList{ QStringLiteral("yuva420p") };
        vf.codecs.push_back(c);
        return vf;
    };

    // The app creates a NEW ctrl::Exporter per export. Reusing a single exporter
    // is NOT supported: the second start()'s createFramebuffers crashes inside
    // QOpenGLFramebufferObject (a pre-existing GL framebuffer-lifecycle issue,
    // unrelated to the in-process media layer, which a single VideoEncoder
    // instance reuses cleanly across cycles — see the V1 suite). Mimic the app.
    auto makeExporter = [&]() {
        std::unique_ptr<ctrl::Exporter> ex(new ctrl::Exporter(project));
        ex->setOverwriteConfirmer([](const QString&) -> bool { return true; });
        ex->setProgressReporter(reporter);
        return ex;
    };

    auto runVideo = [&](const ctrl::VideoFormat& aFmt, const QSize& aSize,
                        const util::Range& aFrame, const QString& aPixFmt,
                        const QString& aPath, bool aExpectSuccess) -> bool {
        std::unique_ptr<ctrl::Exporter> ex = makeExporter();
        ctrl::Exporter::CommonParam cp;
        cp.path = aPath; cp.size = aSize; cp.frame = aFrame; cp.fps = fps;
        ctrl::Exporter::VideoParam vp;
        vp.format = aFmt;
        vp.codecIndex = 0; vp.colorIndex = 0; vp.bps = 1000; vp.pixfmt = aPixFmt;
        auto r = ex->execute(cp, vp);
        return aExpectSuccess ? (bool)r : !r;
    };

    // ffprobe a file and return whether it has a video stream + its dims.
    auto ffprobeVideo = [](const QString& aPath, int* oW, int* oH) -> bool {
        *oW = *oH = 0;
        FILE* p = popen(QStringLiteral(
            "ffprobe -v error -select_streams v -show_entries stream=width,height "
            "-of csv=p=0 \"%1\" 2>/dev/null | head -1").arg(aPath).toUtf8().constData(), "r");
        if (!p) return false;
        char buf[128];
        bool ok = false;
        if (fgets(buf, sizeof(buf), p)) {
            int w = 0, h = 0;
            if (std::sscanf(buf, "%d,%d", &w, &h) == 2) { *oW = w; *oH = h; ok = true; }
        }
        fclose(p);
        return ok && *oW > 0 && *oH > 0;
    };

    // Scenario 1: native-size happy path (regression: valid h264 at native dims).
    {
        std::unique_ptr<ctrl::Exporter> ex = makeExporter();
        ctrl::Exporter::CommonParam cp;
        cp.path = out; cp.size = QSize(nativeW, nativeH); cp.frame = util::Range(minFrame, maxFrame); cp.fps = fps;
        ctrl::Exporter::VideoParam vp;
        vp.format = makeMp4(); vp.codecIndex = 0; vp.colorIndex = 0; vp.bps = 1000; vp.pixfmt = "yuv420p";
        auto r = ex->execute(cp, vp);
        check((bool)r, "e2e mp4: native-size export succeeds");
        check(QFile::exists(out) && QFile(out).size() > 0, "e2e mp4: file exists + non-empty");
        int w = 0, h = 0;
        // YUV dimensions round odd natives up to even (documented encoder
        // behavior), so compare against the even-rounded size.
        const int expW = nativeW + (nativeW & 1);
        const int expH = nativeH + (nativeH & 1);
        check(ffprobeVideo(out, &w, &h) && w == expW && h == expH,
              "e2e mp4: ffprobe video stream at (even-rounded) native dims");
        QFile::remove(out);
    }

    // Scenario 2: odd export size must round to even (no corrupt file, no crash).
    {
        const QString oddPath = QDir::temp().filePath("ae_e2e_odd.mp4");
        bool ok = runVideo(makeMp4(), QSize(321, 241), util::Range(minFrame, maxFrame), "yuv420p", oddPath, true);
        check(ok, "e2e odd-size (321x241): export succeeds (rounded to even)");
        int w = 0, h = 0;
        check(ffprobeVideo(oddPath, &w, &h), "e2e odd-size: ffprobe reports a video stream");
        check(w % 2 == 0 && h % 2 == 0, "e2e odd-size: output dims are even");
        QFile::remove(oddPath);
    }

    // Scenario 3: single-frame export (range of exactly 1 frame).
    {
        const QString sPath = QDir::temp().filePath("ae_e2e_single.mp4");
        bool ok = runVideo(makeMp4(), small, util::Range(minFrame, minFrame), "yuv420p", sPath, true);
        check(ok, "e2e single-frame: export succeeds");
        int w = 0, h = 0;
        check(ffprobeVideo(sPath, &w, &h) && w == small.width() && h == small.height(),
              "e2e single-frame: ffprobe reports the video stream");
        QFile::remove(sPath);
    }

    // Scenario 4: transparent export -> webm/VP9 alpha. libx264/mp4 cannot carry
    // alpha; VP9/WebM carries it as packet side data (alpha_mode=1), recovered
    // only by the libvpx decoder (ffprobe's stream pix_fmt stays yuv420p). We
    // assert alpha_mode is set (the V1 unit test verifies the values survive).
    {
        const QString tPath = QDir::temp().filePath("ae_e2e_alpha.webm");
        bool ok = runVideo(makeWebmVp9(), small, util::Range(minFrame, maxFrame), "yuva420p", tPath, true);
        check(ok, "e2e transparent (webm/vp9 @ yuva420p): export succeeds");
        FILE* p = popen(QStringLiteral(
            "ffprobe -v error -select_streams v -show_entries stream=codec_name:stream_tags=alpha_mode "
            "-of default=noprint_wrappers=1 \"%1\" 2>/dev/null | head -4").arg(tPath).toUtf8().constData(), "r");
        char buf[256]; bool isVp9 = false, hasAlpha = false;
        if (p) {
            while (fgets(buf, sizeof(buf), p)) {
                const QString line = QString::fromUtf8(buf).trimmed();
                if (line.startsWith("codec_name=vp9")) isVp9 = true;
                if (line.contains("alpha_mode=1")) hasAlpha = true;
            }
            fclose(p);
        }
        check(isVp9, "e2e transparent (webm/vp9 @ yuva420p): ffprobe reports vp9");
        check(hasAlpha, "e2e transparent (webm/vp9 @ yuva420p): alpha_mode side-data present");
        QFile::remove(tPath);
    }

    // Scenario 5: unwritable output path must fail cleanly (no crash, no success).
    {
        const QString badPath = QDir::temp().filePath("ae_e2e_dir.mp4");
        QDir().mkpath(badPath); // create a DIRECTORY at the output path
        bool ok = runVideo(makeMp4(), small, util::Range(minFrame, maxFrame), "yuv420p", badPath, false);
        check(ok, "e2e unwritable path: export rejected cleanly (no false success)");
        QDir(badPath).removeRecursively();
    }

    // Scenario 6: GIF export via the in-process ctrl::Exporter -> valid .gif.
    {
        std::unique_ptr<ctrl::Exporter> ex = makeExporter();
        const QString gPath = QDir::temp().filePath("ae_e2e.gif");
        ctrl::Exporter::CommonParam cp;
        cp.path = gPath; cp.size = small; cp.frame = util::Range(minFrame, maxFrame); cp.fps = fps;
        ctrl::Exporter::GifParam gp;
        gp.optimizePalette = true; gp.intermediateBps = 0;
        auto r = ex->execute(cp, gp);
        check((bool)r, "e2e gif: export succeeds (in-process GifEncoder)");
        check(QFile::exists(gPath) && QFile(gPath).size() > 0, "e2e gif: file exists + non-empty");
        FILE* p = popen(QStringLiteral(
            "ffprobe -v error -select_streams v -show_entries stream=codec_name,width,height "
            "-of csv=p=0 \"%1\" 2>/dev/null | head -1").arg(gPath).toUtf8().constData(), "r");
        char buf[128]; bool isGif = false;
        if (p && fgets(buf, sizeof(buf), p)) {
            if (QString::fromUtf8(buf).trimmed().startsWith("gif")) isGif = true;
        }
        if (p) fclose(p);
        check(isGif, "e2e gif: ffprobe reports a gif video stream");
        QFile::remove(gPath);
    }

    // Scenario 7: image sequence export (ImageParam) -> frames written to a dir.
    {
        std::unique_ptr<ctrl::Exporter> ex = makeExporter();
        const QString imgDir = QDir::temp().filePath("ae_e2e_frames");
        QDir(imgDir).removeRecursively();
        QDir().mkpath(imgDir);
        ctrl::Exporter::CommonParam cp;
        cp.path = imgDir; cp.size = small; cp.frame = util::Range(minFrame, maxFrame); cp.fps = fps;
        ctrl::Exporter::ImageParam ip;
        ip.name = "ae_e2e_frame"; ip.suffix = "png"; ip.quality = 90;
        auto r = ex->execute(cp, ip);
        check((bool)r, "e2e image-seq: export succeeds");
        QDir d(imgDir);
        const QStringList pngs = d.entryList(QStringList{ "*.png" }, QDir::Files);
        check(pngs.size() >= 1, "e2e image-seq: at least one frame written");
        QDir(imgDir).removeRecursively();
    }

    // Audio is exercised at the unit level (V2) + the video+audio mux case in
    // media_video_encoder (V1); the real-path harness stays video/gif/image to
    // keep the GL + .anie load fast and deterministic.

    QTextStream(stdout) << "=== result ===\n";
    return failures == 0 ? 0 : 1;
}
