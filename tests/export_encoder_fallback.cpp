// Fallback gate for the encoder OPEN, driven through the real ctrl::Exporter.
//
// What this pins (export-review-2026-10-09, MED item 3 / 2026-09-27 §5.3):
// ctrl::Exporter used to return ResultCode_EncoderOpenFailed the moment
// avcodec_open2 refused the requested encoder. The codec had passed
// media::ffmpeg's capability probe — a 64x64 one-frame encode — and a hardware
// backend can pass that probe and still refuse the real resolution and option
// set (NVENC on a machine with no capable device, a QSV/VAAPI profile without
// the residency the resolution needs). Every such export aborted instead of
// stepping down to the family's software encoder.
//
// The three cases run one real export each over the committed 96x64 fixture,
// so what is asserted is what the app does, not what the media layer does:
//
//   1. HARDWARE codec whose open is made to fail -> the export retries ONCE with
//      the same family's software encoder (libx264 for h264_nvenc) and SUCCEEDS:
//      a real mp4 with a video stream at the requested size. Pre-fix this case
//      returned ResultCode_EncoderOpenFailed and wrote nothing.
//   2. SOFTWARE codec that cannot open -> no retry: the original failure and its
//      reason stand alone, with no second attempt recorded. Pre-fix this was the
//      only behaviour there was, so the assertion guards the retry logic itself
//      (a retry for an already-software codec would put a second attempt in the
//      log and try the same encoder again).
//   3. HARDWARE codec whose open fails AND whose software fallback also fails ->
//      ResultCode_EncoderOpenFailed is still returned, with a reason per
//      attempt. Pre-fix this was the same return code but with only the first
//      reason, and it never tried the second encoder at all.
//
// How the hardware open is made to fail: with an encoder option only the
// SOFTWARE encoder of the family understands ("x264opts", consumed by libx264
// and unknown to any hardware wrapper). That is device-independent, so the case
// does not turn green on a machine with a working NVENC and red on one without,
// and it models the defect exactly — an open that refuses the option set.
//
// Needs a live GL context: the exporter renders every frame (and its
// destructor binds GL), so a headless platform with no GL cannot run this gate
// (measured with the committed roundtrip harness: QT_QPA_PLATFORM=offscreen
// prints "GL context get failed"). Run it under a display, the way
// media_exporter_e2e is run: QT_QPA_PLATFORM=xcb xvfb-run -a <test>.
#include <QApplication>
#include <QOpenGLWidget>
#include <QOpenGLVersionFunctionsFactory>
#include <QOpenGLFunctions_4_0_Core>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSize>
#include <QStringList>
#include <QSurfaceFormat>
#include <QTextStream>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "XCAssert.h"
#include "temp_util.h"
#include "gl/Global.h"
#include "gl/DeviceInfo.h"
#include "core/Project.h"
#include "core/Animator.h"
#include "ctrl/Exporter.h"
#include "ctrl/ImageFileLoader.h"
#include "ctrl/ProjectLoader.h"
#include "gui/AudioPlaybackWidget.h"
#include "media/FFmpegHelpers.h"
#include "util/IProgressReporter.h"

// Project.cpp references the GUI AudioPlaybackWidget::deserialize (only to load
// a .aemus sidecar, which this harness never has). Provide a stub — same as
// media_exporter_e2e and bonekey_regression.
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

static int failures = 0;

static void check(bool ok, const char* what) {
    QTextStream(stdout) << (ok ? "PASS  " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

// The private option that makes the hardware encoder's open fail and the
// software encoder's open succeed.
static const QString kSoftwareOnlyOption = QStringLiteral("x264opts");
static const QString kInertX264Value = QStringLiteral("no-mbtree");
// A key no encoder knows, so BOTH candidates refuse it. Same name
// media_exporter_e2e's 11a uses for "an unknown private option is rejected".
static const QString kUnknownOption = QStringLiteral("definitely-not-an-option");

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

// ffprobe a file's video stream: codec name and dimensions. False if there is
// no video stream (or no ffprobe), which is the only negative answer needed.
static bool ffprobeVideo(const QString& aPath, int* oW, int* oH, QString* oCodec) {
    *oW = *oH = 0;
    oCodec->clear();
    FILE* p = popen(QStringLiteral(
        "ffprobe -v error -select_streams v -show_entries stream=codec_name,width,height "
        "-of csv=p=0 \"%1\" 2>/dev/null | head -1").arg(aPath).toUtf8().constData(), "r");
    if (!p) return false;
    char buf[256] = {0};
    const bool got = fgets(buf, sizeof(buf), p) != nullptr;
    pclose(p);
    const QString line = got ? QString::fromUtf8(buf).trimmed() : QString();
    const QStringList parts = line.split(QLatin1Char(','));
    if (parts.size() < 3) return false;
    *oCodec = parts[0];
    *oW = parts[1].toInt();
    *oH = parts[2].toInt();
    return *oW > 0 && *oH > 0;
}

struct ExportRun {
    bool success = false;
    ctrl::Exporter::ResultCode code = ctrl::Exporter::ResultCode_TERM;
    QString log;
};

// One export through the REAL exporter: a new instance per call, because a
// second start() on one exporter crashes inside QOpenGLFramebufferObject (a
// framebuffer-lifecycle issue unrelated to the encoder), which is why the app
// creates one per export too.
static ExportRun runVideoExport(core::Project& aProject, util::IProgressReporter& aReporter,
                                const QString& aCodec, const QMap<QString, QString>& aEncOptions,
                                const QString& aContainer, const QString& aPath,
                                const QSize& aSize, int aFps, int aMinFrame, int aMaxFrame) {
    std::unique_ptr<ctrl::Exporter> ex(new ctrl::Exporter(aProject));
    ex->setOverwriteConfirmer([](const QString&) -> bool { return true; });
    ex->setProgressReporter(aReporter);

    ctrl::Exporter::CommonParam cp;
    cp.path = aPath;
    cp.size = aSize;
    cp.selection = ctrl::FrameSelection::fromRange(aMinFrame, aMaxFrame);
    cp.fps = aFps;
    ctrl::Exporter::VideoParam vp;
    vp.container = aContainer;
    vp.codec = aCodec;
    vp.colorIndex = 0;
    vp.bps = 1000;
    vp.pixfmt = QStringLiteral("yuv420p");
    vp.encOptions = aEncOptions;

    ExportRun run;
    const auto r = ex->execute(cp, vp);
    run.success = (bool)r;
    run.code = r.code;
    run.log = ex->log();
    return run;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    gXCAssertHandler = &gAssertH;
    gXCErrorHandler = &gErrorH;

    // A real project, or the committed fixture when none is named: the tiny
    // synthetic stage the e2e harness runs against, so this gate does not depend
    // on artwork that happens to be on the machine.
    const QString src = (argc > 1) ? QString::fromLocal8Bit(argv[1])
                                   : QStringLiteral("tests/fixtures/e2e_stage.png");

    // Live GL context (the same setup as media_exporter_e2e).
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setVersion(gl::Global::kVersion.first, gl::Global::kVersion.second);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);
    QOpenGLWidget glWidget;
    glWidget.resize(4, 4);
    glWidget.show();
    glWidget.makeCurrent();
    if (!glWidget.context()) {
        check(false, "GL context (this gate renders, so it needs a display - see the header)");
        return 1;
    }
    auto funcs = QOpenGLVersionFunctionsFactory::get<gl::Global::Functions>(glWidget.context());
    if (!funcs || !funcs->initializeOpenGLFunctions()) {
        check(false, "GL functions");
        return 1;
    }
    gl::Global::setContext(glWidget);
    gl::Global::setFunctions(*funcs);
    gl::DeviceInfo dev = makeDeviceInfo();
    gl::DeviceInfo::setInstance(&dev);

    NullAnimator animator;
    NullReporter reporter;

    core::Project project(src, animator, nullptr);
    if (QFileInfo(src).suffix().toLower() == QLatin1String("anie")) {
        ctrl::ProjectLoader loader;
        if (!loader.load(src, project, dev, reporter)) {
            QTextStream(stdout) << "FAIL  load: " << loader.log().join("\n") << "\n";
            return 1;
        }
    } else {
        ctrl::ImageFileLoader loader(dev);
        loader.setOraImportMode(ctrl::ImageFileLoader::OraImportMode::Layered);
        if (!loader.load(src, project, reporter)) {
            QTextStream(stdout) << "FAIL  load: " << loader.log() << "\n";
            return 1;
        }
    }

    const QSize size = project.attribute().imageSize();
    const int fps = project.attribute().fps() > 0 ? project.attribute().fps() : 24;
    const int minFrame = project.attribute().frameRange().min();
    const int maxFrame = std::min(project.attribute().frameRange().max(), minFrame + 1);
    QTextStream(stdout) << "=== encoder fallback: " << src << " (" << size.width() << "x"
                        << size.height() << " @ " << fps << "fps) ===\n";

    // The gate's premise: a hardware encoder the linked build COMPILES IN, so
    // the exporter reaches the open call (the only place the fallback can run),
    // and the software encoder of the same family it must step down to. Both are
    // registered-encoder questions, not device questions: the hardware one is
    // MEANT to fail to open below.
    if (!media::ffmpeg::encoderAvailable("h264_nvenc")) {
        check(false, "precondition: no h264_nvenc in this build, so no hardware open can fail");
        return 1;
    }
    if (!media::ffmpeg::encoderAvailable("libx264")) {
        check(false, "precondition: no libx264 in this build, so the family has no software encoder");
        return 1;
    }

    const QString container = QStringLiteral("mp4");

    // Case 1: the hardware encoder's open fails, so the export must retry with
    // the family's software encoder and succeed. Pre-fix this returned
    // ResultCode_EncoderOpenFailed and wrote no file at all.
    {
        const QString path = aeTestTempPath("ae_enc_fallback_hw.mp4");
        QFile::remove(path);
        QMap<QString, QString> opts;
        opts.insert(kSoftwareOnlyOption, kInertX264Value);
        const ExportRun r = runVideoExport(project, reporter, QStringLiteral("h264_nvenc"),
                                           opts, container, path, size, fps, minFrame, maxFrame);

        check(r.success, "case1: the hardware codec's open failing does not abort the export");
        check(r.log.contains(QLatin1String("h264_nvenc")),
              "case1: the log names the encoder that could not be opened");
        check(r.log.contains(QLatin1String("libx264")),
              "case1: the log names the software encoder it stepped down to");

        int w = 0, h = 0;
        QString codec;
        const bool probed = ffprobeVideo(path, &w, &h, &codec);
        check(probed, "case1: the retried export has a video stream (ffprobe)");
        check(probed && codec == QLatin1String("h264"),
              "case1: the retried export carries h264 (libx264)");
        check(probed && w == size.width() && h == size.height(),
              "case1: the retried export is at the requested size");
        check(QFile::exists(path) && QFile(path).size() > 0,
              "case1: the retried export wrote a file");
        QFile::remove(path);
    }

    // Case 2: the codec is already software, so a failure must NOT be retried:
    // the original reason stands and no second attempt is recorded. A retry
    // would put the fallback note in the log below.
    {
        const QString path = aeTestTempPath("ae_enc_fallback_sw.mp4");
        QFile::remove(path);
        QMap<QString, QString> opts;
        opts.insert(kUnknownOption, QStringLiteral("1"));
        const ExportRun r = runVideoExport(project, reporter, QStringLiteral("libx264"),
                                           opts, container, path, size, fps, minFrame, maxFrame);

        check(!r.success && r.code == ctrl::Exporter::ResultCode_EncoderOpenFailed,
              "case2: a software codec that cannot open still reports EncoderOpenFailed");
        check(r.log.contains(kUnknownOption),
              "case2: the original failure reason is preserved");
        check(!r.log.contains(QLatin1String("exported with the")) &&
                  !r.log.contains(QLatin1String("could not be opened either")),
              "case2: no second encoder was attempted (no fallback in the log)");
        check(!QFile::exists(path), "case2: no file was written");
    }

    // Case 3: both the hardware codec and its software fallback fail, so the
    // failure must still be reported — with a reason for each attempt, not just
    // the first.
    {
        const QString path = aeTestTempPath("ae_enc_fallback_both.mp4");
        QFile::remove(path);
        QMap<QString, QString> opts;
        opts.insert(kUnknownOption, QStringLiteral("1"));
        const ExportRun r = runVideoExport(project, reporter, QStringLiteral("h264_nvenc"),
                                           opts, container, path, size, fps, minFrame, maxFrame);

        check(!r.success && r.code == ctrl::Exporter::ResultCode_EncoderOpenFailed,
              "case3: both candidates failing still reports EncoderOpenFailed");
        check(r.log.contains(kUnknownOption),
              "case3: the failure keeps the reasons (the rejected option is named)");
        check(r.log.contains(QLatin1String("libx264")),
              "case3: the log names the software encoder that was also tried");
        check(!QFile::exists(path), "case3: no file was written");
    }

    return failures == 0 ? 0 : 1;
}
