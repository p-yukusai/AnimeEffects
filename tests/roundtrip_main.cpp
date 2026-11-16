// Headless round-trip harness, mirroring the GUI's multi-tab project lifecycle:
// load every project given on the command line (all alive at once, like tabs),
// round-trip each through serialize+reload, then "close" (destroy) each.
//
// GL: object-tree load constructs LayerNode -> MeshTransformer (shader build)
// and LayerMesh::MeshBuffer (GL binder), so a live GL context with the
// gl::Global functions table is required — same as the real GUI, which uses a
// QOpenGLWidget (MainDisplayWidget). We register a real QOpenGLWidget.
//
// NOTE: this project uses Qt5Compat, which defines the classic `slots` (and
// `signals`) macros. Range-for loop variables must not be named `slot`.
#include <QApplication>
#include <QOpenGLWidget>
#include <QOpenGLVersionFunctionsFactory>
#include <QOpenGLFunctions_4_0_Core>
#include <QFile>
#include <QTemporaryFile>
#include <QJsonObject>
#include <cstdio>
#include <vector>
#include <memory>
#include <utility>
#include "XCAssert.h"
#include "gl/Global.h"
#include "gl/DeviceInfo.h"
#include "gui/AudioPlaybackWidget.h"
#include "core/Project.h"
#include "core/Animator.h"
#include "ctrl/ProjectLoader.h"
#include "ctrl/ProjectSaver.h"
#include "util/IProgressReporter.h"

static QString joinLog(const QStringList& log) {
    QString s;
    for (const QString& l : log) {
        if (!s.isEmpty()) s += '\n';
        s += l;
    }
    return s;
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
    void critical(const QString& text, const QString& info, const QString& detail) const override {
        std::fprintf(stderr, "XC_FATAL %s %s %s\n", qPrintable(text), qPrintable(info), qPrintable(detail));
        std::abort();
    }
};
HarnessAssertHandler gAssertHandler;
HarnessErrorHandler gErrorHandler;
} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

// Project.cpp references the GUI AudioPlaybackWidget::deserialize (only to
// load a .aemus sidecar, which the round-trip never has). Provide a stub that
// always fails so the .aemus path is never taken.
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

static gl::DeviceInfo makeDeviceInfo() {
    gl::DeviceInfo dev;
    dev.vender = "harness";
    dev.renderer = "harness";
    dev.version = "5";
    dev.maxTextureSize = 16384;
    dev.maxRenderBufferSize = 16384;
    return dev;
}

struct ProjectSlot {
    std::unique_ptr<core::Project> project;
    QString filePath;
    QString tmpPath;
};

// In-process round trip for the optional viewport background: a project that
// set a colour must reload with the same rgb and has=true, a cleared one must
// reload with has=false (the theme default). Returns 0 on success.
static int checkViewportBackgroundRoundTrip(NullAnimator& animator, const gl::DeviceInfo& aDev, NullReporter& aReporter) {
    const auto saveAndReload = [&](core::Project& aProject, std::unique_ptr<core::Project>& aOut) -> bool {
        QTemporaryFile tmp;
        tmp.setAutoRemove(false);
        if (!tmp.open()) {
            std::fprintf(stderr, "BG tmp open failed\n");
            return false;
        }
        const QString tmpPath = tmp.fileName() + ".anie";
        tmp.close();
        QFile::remove(tmpPath);
        ctrl::ProjectSaver saver;
        if (!saver.save(tmpPath, aProject)) {
            std::fprintf(stderr, "BG SAVE FAILED: %s\n", qPrintable(saver.log()));
            QFile::remove(tmpPath);
            return false;
        }
        aOut = std::make_unique<core::Project>(tmpPath, animator, nullptr);
        ctrl::ProjectLoader loader;
        if (!loader.load(tmpPath, *aOut, aDev, aReporter)) {
            std::fprintf(stderr, "BG RELOAD FAILED: %s\n", qPrintable(joinLog(loader.log())));
            QFile::remove(tmpPath);
            return false;
        }
        QFile::remove(tmpPath);
        return true;
    };

    const auto fail = [](const char* aWhat) {
        std::fprintf(stderr, "BG ROUNDTRIP FAILED: %s\n", aWhat);
        return 1;
    };

    core::Project painted(QString(), animator, nullptr);
    painted.attribute().setViewportBackground(QColor(17, 34, 51));
    std::unique_ptr<core::Project> reloaded;
    if (!saveAndReload(painted, reloaded)) return 1;
    if (!reloaded->attribute().hasViewportBackground()) return fail("set colour came back unset");
    if (reloaded->attribute().viewportBackground().rgb() != QColor(17, 34, 51).rgb())
        return fail("set colour changed on reload");

    core::Project cleared(QString(), animator, nullptr);
    cleared.attribute().setViewportBackground(QColor(17, 34, 51));
    cleared.attribute().clearViewportBackground();
    std::unique_ptr<core::Project> reloadedClear;
    if (!saveAndReload(cleared, reloadedClear)) return 1;
    if (reloadedClear->attribute().hasViewportBackground()) return fail("cleared colour came back set");

    std::fprintf(stderr, "VIEWPORT BACKGROUND ROUNDTRIP OK\n");
    return 0;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <file.anie>...\n", argv[0]);
        return 2;
    }

    // Live GL context (see file header).
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
        std::fprintf(stderr, "GL context get failed\n");
        return 2;
    }
    auto funcs = QOpenGLVersionFunctionsFactory::get<gl::Global::Functions>(glWidget.context());
    if (!funcs || !funcs->initializeOpenGLFunctions()) {
        std::fprintf(stderr, "GL functions init failed\n");
        return 2;
    }
    gl::Global::setContext(glWidget);
    gl::Global::setFunctions(*funcs);

    gl::DeviceInfo dev = makeDeviceInfo();
    gl::DeviceInfo::setInstance(&dev);
    NullAnimator animator;
    NullReporter reporter;

    // The optional viewport background round trip is independent of the files
    // below, so exercise it once here.
    const int bgFailures = checkViewportBackgroundRoundTrip(animator, dev, reporter);

    // Load all projects up front (tabs alive simultaneously), round-trip each
    // through serialize+reload once, then "close" (destroy) all.
    // Load failures are reported and skipped (continue), so the rest of the set
    // is still validated; a nonzero exit reflects any failure.
    int loadFailures = 0;
    std::vector<ProjectSlot> projSlots;
    for (int i = 1; i < argc; ++i) {
        const QString path = QString::fromLocal8Bit(argv[i]);
        ProjectSlot pslot;
        pslot.filePath = path;
        pslot.project.reset(new core::Project(path, animator, nullptr));
        ctrl::ProjectLoader loader;
        if (!loader.load(path, *pslot.project, dev, reporter)) {
            std::fprintf(stderr, "LOAD FAILED: %s: %s\n", qPrintable(path), qPrintable(joinLog(loader.log())));
            loadFailures++;
            continue;
        }
        // Serialize to a temp .anie
        QTemporaryFile tmp;
        tmp.setAutoRemove(false);
        if (!tmp.open()) { std::fprintf(stderr, "tmp open failed: %s\n", qPrintable(path)); return 1; }
        const QString tmpPath = tmp.fileName() + ".anie";
        tmp.close();
        QFile::remove(tmpPath);
        ctrl::ProjectSaver saver;
        if (!saver.save(tmpPath, *pslot.project)) {
            std::fprintf(stderr, "SAVE FAILED: %s: %s\n", qPrintable(path), qPrintable(saver.log()));
            QFile::remove(tmpPath);
            return 1;
        }
        pslot.tmpPath = tmpPath;
        // Reload the saved copy into a fresh project (destroyed at end).
        auto reloaded = std::make_unique<core::Project>(tmpPath, animator, nullptr);
        ctrl::ProjectLoader loader2;
        if (!loader2.load(tmpPath, *reloaded, dev, reporter)) {
            std::fprintf(stderr, "RELOAD FAILED: %s: %s\n", qPrintable(tmpPath), qPrintable(joinLog(loader2.log())));
            QFile::remove(tmpPath);
            return 1;
        }
        std::fprintf(stderr, "LOADED: %s\n", qPrintable(path));
        projSlots.push_back(std::move(pslot));
        (void)reloaded; // destroyed on next loop iteration / scope exit
    }

    // Close all projects in order (like closing tabs).
    for (auto& pslot : projSlots) {
        if (!pslot.tmpPath.isEmpty()) QFile::remove(pslot.tmpPath);
        pslot.project.reset();
        std::fprintf(stderr, "CLOSED: %s\n", qPrintable(pslot.filePath));
    }
    std::fprintf(stderr, "DONE (load failures: %d)\n", loadFailures);
    return (loadFailures > 0 || bgFailures != 0) ? 1 : 0;
}
