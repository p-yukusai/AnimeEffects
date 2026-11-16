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
//                [--frames <a>..<b>] [--step <n>] [--offset <px>] [--supersample <n>]
//                [--metrics] [--require-gpu] [--repeat <n>]
//
//   --size    framebuffer size in pixels (default 640x640)
//   --zoom    camera scale (1.0 = the project's pixels at 1:1); default fits the whole image
//   --frames  inclusive project frame range; default is the project's own frame range
//   --step    project frames advanced per output frame (default 1)
//   --repeat  render each frame N times and keep the last: the SAME frame repainted, i.e. a
//             still view (the measurement behind the demand replay, and the interactive case)
//   --pan     pan the camera right by N output pixels per repaint: the camera-navigation case
//   --offset  horizontal sub-pixel nudge of the camera centre, in pixels
//   --supersample render at Nx the size, then box-average down to --size (1..16, default 1)
//   --metrics one line per frame: FRAME <i> sharpness <float> mean <float>
//   --require-gpu fail (exit 4) when GL_RENDERER names a software renderer
//
// The GL renderer/vendor/version are printed at bootstrap, so a run under Xvfb (llvmpipe) is
// distinguishable from one that got the GPU.
//
// Output is <out>/frame-<index>.png, RGBA8, exactly --size. Run it under
// QT_QPA_PLATFORM=offscreen (no display needed).
//
// Exit codes: 0 success, 1 render/write failure, 2 usage, GL bootstrap or project load failure,
// 4 --require-gpu found a software renderer.
//
// NOTE: this project uses Qt5Compat, which defines the classic `slots` (and `signals`) macros.
// Range-for loop variables must not be named `slot`.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include <QApplication>
#include <QByteArrayView>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
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
#include <QTextStream>
#include <QVector2D>

#include "XC.h"
#include "core/Animator.h"
#include "core/BoneKey.h"
#include "core/CameraInfo.h"
#include "core/ClippingFrame.h"
#include "core/DestinationTexturizer.h"
#include "core/FilterFrame.h"
#include "core/FolderNode.h"
#include "core/Frame.h"
#include "core/ImageKey.h"
#include "core/Project.h"
#include "core/LayerNode.h"
#include "core/PosePalette.h"
#include "core/RenderInfo.h"
#include "core/TimeInfo.h"
#include "core/TimeLine.h"
#include "core/VirtualTexture.h"
#include "ctrl/Driver.h"
#include "ctrl/DriverResources.h"
#include "ctrl/GraphicStyle.h"
#include "ctrl/ImageFileLoader.h"
#include "ctrl/ProjectLoader.h"
#include "ctrl/UILogger.h"
#include "gl/DeviceInfo.h"
#include "gl/Framebuffer.h"
#include "gl/FrameStats.h"
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
    "                    [--frames <a>..<b>] [--step <n>] [--offset <px>] [--supersample <n>]\n"
    "                    [--metrics] [--require-gpu] [--only-layer <text>] [--list-layers]\n"
    "                    [--repeat <n>]\n"
    "\n"
    "Renders project frames offscreen through ctrl::Driver::renderGL and writes\n"
    "<out>/frame-<index>.png (RGBA8, exactly --size).\n"
    "\n"
    "  --size        framebuffer size in pixels (default 640x640)\n"
    "  --zoom        camera scale; default fits the whole project image\n"
    "  --frames      inclusive project frame range; default: the project's range\n"
    "  --step        project frames per output frame (default 1)\n"
    "  --repeat      render each frame N times, keeping the last (default 1): the SAME frame\n"
    "                repainted, which is what a still view does (a pointer move, a brush stroke)\n"
    "  --pan         pan the camera right by N output pixels per repaint (with --repeat): the\n"
    "                camera-navigation instrument, and how the landing-order page defect was\n"
    "                found (texture-residency.md's first open item)\n"
    "  --offset      horizontal sub-pixel nudge of the camera centre, in pixels\n"
    "  --supersample render at Nx the size and box-average down to --size, N in 1..16 (default 1)\n"
    "  --metrics     print FRAME <i> sharpness <float> mean <float> per frame\n"
    "  --only-layer  hide every layer whose name does not contain <text>;\n"
    "                <text> of the form =NAME keeps only the exact name\n"
    "  --list-layers print each layer's name, size, shared-image id and filter/mask/deform\n"
    "                  markers, then exit\n"
    "  --require-gpu exit 4 unless GL_RENDERER looks like hardware (rejects llvmpipe,\n"
    "                softpipe, swrast, software)\n"
    "  --dump-influence <file>\n"
    "                write every built bone-influence map's attribute arrays as SHA-256,\n"
    "                with the build wait time, then exit (the build's reference oracle)\n"
    "  --bench-influence <n>\n"
    "                force n rebuilds of every bone key's influence caches and print each\n"
    "                wall time, then exit (the build-cost instrument; n = 1..100)\n"
    "  --bone-stats  print every bone key's bone count against PosePalette::kMaxCount,\n"
    "                then exit (the rig-size instrument for the skeleton cap)\n";

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
// The reference oracle for the influence build: every built cache's four
// attribute arrays hashed, plus the wait time. A change to the build (a broad
// phase, a chunk split) must leave this file byte-identical. The wait is taken
// around accessor(), which is where the async build is joined.
int dumpInfluenceMaps(core::Project& aProject, const QString& aPath) {
    QFile file(aPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return fail(2, QStringLiteral("cannot write '%1'").arg(aPath));
    }
    QTextStream out(&file);
    core::ObjectNode* top = aProject.objectTree().topNode();
    if (!top) {
        return fail(2, QStringLiteral("the project has no object tree"));
    }

    const auto hash = [](const void* aData, qsizetype aBytes) -> QString {
        if (!aData || aBytes <= 0)
            return QStringLiteral("-");
        const QByteArrayView view(static_cast<const char*>(aData), aBytes);
        return QString::fromLatin1(
            QCryptographicHash::hash(view, QCryptographicHash::Sha256).toHex()
        );
    };

    qint64 waitNs = 0;
    int mapCount = 0;
    qint64 vertexCount = 0;
    int nodeIndex = 0;
    qint64 storedBytes = 0;
    int slotHist[9] = {0};
    for (core::ObjectNode::Iterator itr(top); itr.hasNext();) {
        core::ObjectNode* node = itr.next();
        const int thisNode = nodeIndex++;
        if (!node || !node->timeLine())
            continue;
        const core::TimeLine::MapType& keys = node->timeLine()->map(core::TimeKeyType_Bone);
        for (auto itrKey = keys.begin(); itrKey != keys.end(); ++itrKey) {
            auto* boneKey = dynamic_cast<core::BoneKey*>(itrKey.value());
            if (!boneKey)
                continue;
            int cacheIndex = 0;
            for (const core::BoneKey::Cache* cache : boneKey->caches()) {
                if (!cache)
                    continue;
                const core::BoneInfluenceMap& map = cache->influence();
                QElapsedTimer timer;
                timer.start();
                const core::BoneInfluenceMap::Accessor accessor = map.accessor();
                waitNs += timer.nsecsElapsed();

                const int vertices = map.vertexCount();
                const int slotCount = accessor.slotCount();
                const bool dense = accessor.dense();
                const qsizetype groupBytes =
                    qsizetype(vertices) * core::BoneInfluenceMap::kBonePerVtxMaxEach;

                // The oracle is the LOGICAL eight-slot form: a sparse map is
                // expanded here so its line is the same line the dense writer
                // produced. That is what makes an old dump directly diffable.
                const auto logicalIndices = [&](int aGroup) -> QString {
                    if (dense) {
                        return hash(accessor.denseIndices(aGroup), groupBytes);
                    }
                    std::vector<core::BoneInfluenceMap::IndicesType> buf(
                        size_t(groupBytes), core::BoneInfluenceMap::IndicesType(0)
                    );
                    for (int v = 0; v < vertices; ++v) {
                        for (int k = 0; k < core::BoneInfluenceMap::kBonePerVtxMaxEach; ++k) {
                            const int slot = aGroup * core::BoneInfluenceMap::kBonePerVtxMaxEach + k;
                            if (slot < slotCount) {
                                buf[size_t(v) * core::BoneInfluenceMap::kBonePerVtxMaxEach + k] =
                                    accessor.indices(slot)[v];
                            }
                        }
                    }
                    return hash(buf.data(), qsizetype(buf.size()));
                };
                const auto logicalWeights = [&](int aGroup) -> QString {
                    if (dense) {
                        return hash(
                            accessor.denseWeights(aGroup),
                            groupBytes * qsizetype(sizeof(core::BoneInfluenceMap::WeightsType))
                        );
                    }
                    std::vector<core::BoneInfluenceMap::WeightsType> buf(
                        size_t(groupBytes), core::BoneInfluenceMap::WeightsType(0)
                    );
                    for (int v = 0; v < vertices; ++v) {
                        for (int k = 0; k < core::BoneInfluenceMap::kBonePerVtxMaxEach; ++k) {
                            const int slot = aGroup * core::BoneInfluenceMap::kBonePerVtxMaxEach + k;
                            if (slot >= slotCount)
                                continue;
                            const size_t at =
                                size_t(v) * core::BoneInfluenceMap::kBonePerVtxMaxEach + k;
                            // A one-slot map's weight is the exact 1 the dense
                            // writer stored and did not need to carry.
                            buf[at] = slotCount == 1 ? 1.0f : accessor.weights(slot)[v];
                        }
                    }
                    return hash(
                        buf.data(), qsizetype(buf.size() * sizeof(core::BoneInfluenceMap::WeightsType))
                    );
                };

                out << "map node=" << thisNode
                    << " name=\"" << (cache->node() ? cache->node()->name() : QString()) << "\""
                    << " frame=" << boneKey->frame() << " cache=" << cacheIndex
                    << " v=" << vertices
                    << " i0=" << logicalIndices(0)
                    << " i1=" << logicalIndices(1)
                    << " w0=" << logicalWeights(0)
                    << " w1=" << logicalWeights(1)
                    << "\n";
                ++mapCount;
                vertexCount += vertices;
                storedBytes += accessor.storedBytes();
                ++slotHist[slotCount];
                ++cacheIndex;
            }
        }
    }
    out.flush();
    if (file.error() != QFileDevice::NoError) {
        return fail(2, QStringLiteral("write failed for '%1'").arg(aPath));
    }
    std::printf(
        "INFLUENCE maps=%d vertices=%lld wait_ms=%.3f\n", mapCount, (long long)vertexCount,
        double(waitNs) / 1.0e6
    );
    std::printf(
        "INFLUENCE_STORAGE maps=%d vertices=%lld bytes=%lld logical_bytes=%lld"
        " slots1=%d slots2=%d slots3=%d slots4=%d slots5=%d slots6=%d slots7=%d slots8=%d\n",
        mapCount, (long long)vertexCount, (long long)storedBytes,
        (long long)(vertexCount * 40), slotHist[1], slotHist[2], slotHist[3], slotHist[4],
        slotHist[5], slotHist[6], slotHist[7], slotHist[8]
    );
    std::fflush(stdout);
    return 0;
}

//-------------------------------------------------------------------------------------------------
// Force fresh builds and time the wall clock from the first schedule to the
// last join. resetCaches recycles each key's caches, so this runs the same
// per-vertex work a load does; the warm-up rep is the caller's to discard.
int benchInfluenceMaps(core::Project& aProject, int aReps) {
    core::ObjectNode* top = aProject.objectTree().topNode();
    if (!top) {
        return fail(2, QStringLiteral("the project has no object tree"));
    }

    for (int rep = 0; rep < aReps; ++rep) {
        int maps = 0;
        qint64 vertices = 0;
        QElapsedTimer timer;
        QElapsedTimer split;
        timer.start();

        for (core::ObjectNode::Iterator itr(top); itr.hasNext();) {
            core::ObjectNode* node = itr.next();
            if (!node || !node->timeLine())
                continue;
            const core::TimeLine::MapType& keys = node->timeLine()->map(core::TimeKeyType_Bone);
            for (auto itrKey = keys.begin(); itrKey != keys.end(); ++itrKey) {
                auto* boneKey = dynamic_cast<core::BoneKey*>(itrKey.value());
                if (boneKey)
                    boneKey->resetCaches(aProject, *node);
            }
        }
        split.start();

        for (core::ObjectNode::Iterator itr(top); itr.hasNext();) {
            core::ObjectNode* node = itr.next();
            if (!node || !node->timeLine())
                continue;
            const core::TimeLine::MapType& keys = node->timeLine()->map(core::TimeKeyType_Bone);
            for (auto itrKey = keys.begin(); itrKey != keys.end(); ++itrKey) {
                auto* boneKey = dynamic_cast<core::BoneKey*>(itrKey.value());
                if (!boneKey)
                    continue;
                for (const core::BoneKey::Cache* cache : boneKey->caches()) {
                    if (!cache)
                        continue;
                    ++maps;
                    vertices += cache->influence().vertexCount();
                    (void)cache->influence().accessor();
                }
            }
        }

        std::printf(
            "INFLUENCE_BENCH rep=%d ms=%.3f schedule_ms=%.3f wait_ms=%.3f maps=%d vertices=%lld\n",
            rep, double(timer.nsecsElapsed()) / 1.0e6,
            double(timer.nsecsElapsed() - split.nsecsElapsed()) / 1.0e6,
            double(split.nsecsElapsed()) / 1.0e6, maps, (long long)vertices
        );
        std::fflush(stdout);
    }
    return 0;
}

//-------------------------------------------------------------------------------------------------
// Bone census: how many bones each BoneKey holds against
// PosePalette::kMaxCount, the one cap shared by the pose/binding palette and the
// influence build (BoneKey::Cache). A key above it loses its last bones
// silently, so the count is the first question any rig-size change has to
// answer.
int printBoneStats(core::Project& aProject) {
    core::ObjectNode* top = aProject.objectTree().topNode();
    if (!top) {
        return fail(2, QStringLiteral("the project has no object tree"));
    }

    int keyCount = 0;
    int overCapKeys = 0;
    int maxBones = 0;
    qint64 totalBones = 0;
    int nodeIndex = 0;
    for (core::ObjectNode::Iterator itr(top); itr.hasNext();) {
        core::ObjectNode* node = itr.next();
        const int thisNode = nodeIndex++;
        if (!node || !node->timeLine())
            continue;
        const core::TimeLine::MapType& keys = node->timeLine()->map(core::TimeKeyType_Bone);
        for (auto itrKey = keys.begin(); itrKey != keys.end(); ++itrKey) {
            auto* boneKey = dynamic_cast<core::BoneKey*>(itrKey.value());
            if (!boneKey)
                continue;

            int bones = 0;
            for (const core::Bone2* topBone : boneKey->data().topBones()) {
                for (core::Bone2::ConstIterator boneItr(topBone); boneItr.hasNext();) {
                    boneItr.next();
                    ++bones;
                }
            }

            ++keyCount;
            totalBones += bones;
            maxBones = std::max(maxBones, bones);
            const bool overCap = bones > core::PosePalette::kMaxCount;
            if (overCap)
                ++overCapKeys;

            QString path = node->name();
            for (core::ObjectNode* p = node->parent(); p; p = p->parent())
                path = p->name() + QStringLiteral("/") + path;
            std::printf(
                "BONEKEY node=%d path=\"%s\" frame=%d bones=%d caches=%d over_cap=%d\n",
                thisNode, path.toUtf8().constData(), boneKey->frame(), bones,
                (int)boneKey->caches().size(), overCap ? 1 : 0
            );
        }
    }

    std::printf(
        "BONE_STATS keys=%d total_bones=%lld max_bones=%d cap=%d over_cap_keys=%d\n", keyCount,
        (long long)totalBones, maxBones, (int)core::PosePalette::kMaxCount, overCapKeys
    );
    std::fflush(stdout);
    return 0;
}

//-------------------------------------------------------------------------------------------------
// command line

struct Options {
    QString projectPath;
    QString outDir;
    QSize size = QSize(640, 640);
    bool zoomSet = false;
    float zoom = 1.0f;
    // per-repaint pan, in output pixels: the camera moves sideways
    float pan = 0.0f;
    bool framesSet = false;
    int frameBegin = 0;
    int frameEnd = 0;
    int step = 1;
    // repaint each output frame N times: the still-view case (same time frame, same camera)
    int repeat = 1;
    float offset = 0.0f;
    bool metrics = false;
    // refuse to run when the driver fell back to a software rasterizer (Xvfb's GLX is llvmpipe)
    bool requireGpu = false;
    // render at supersample x supersample and box-average down: the ground truth for "is the 1x
    // render aliased", and a way to look at SSAA before paying for it in the viewport
    int supersample = 1;
    // diagnostics for a per-layer question: list the layer names, or hide every layer whose name
    // does not contain this text (the camera and the canvas stay the project's)
    QString onlyLayer;
    bool listLayers = false;
    // write every built bone-influence map's attribute arrays to a file, as
    // SHA-256 per set plus the wait time: the reference oracle for a change to
    // the influence build (the arrays must stay byte-identical).
    QString dumpInfluence;
    // force N rebuilds of every bone key's influence caches and print the wall
    // time of each: the build-cost instrument. 0 disables.
    int benchInfluence = 0;
    // count the bones in every BoneKey against PosePalette::kMaxCount: the
    // rig-size instrument for the skeleton-bone truncation.
    bool boneStats = false;
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
    // GL_RENDERER as reported by the driver; empty when the driver returns null
    QString mRenderer;
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
        // Which driver we actually got: llvmpipe under Xvfb's GLX, radeonsi on the GPU. Printed
        // unconditionally so a caller can tell the two apart without guessing.
        const char* renderer = (const char*)mContext.functions()->glGetString(GL_RENDERER);
        const char* vendor = (const char*)mContext.functions()->glGetString(GL_VENDOR);
        mRenderer = QString::fromLatin1(renderer ? renderer : "");
        std::fprintf(
            stderr, "GL renderer: %s (vendor: %s, version: %s)\n",
            renderer ? renderer : "(unknown)", vendor ? vendor : "(unknown)",
            version ? version : "(unknown)");
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
    // `aPanPixels` shifts the camera centre to the RIGHT by that many output pixels. A pan is the
    // camera-navigation case: the demand's page RECTS do not move with it, but the pass re-marks
    // every frame because the fragment's level rounding does (see --pan and VirtualTexture.h).
    std::vector<uint8_t> render(
        core::Project& aProject, ctrl::Driver& aDriver, const Options& aOptions, int aFrame,
        float aPanPixels = 0.0f
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
            QVector2D(
                size.width() * 0.5f + (aOptions.offset + aPanPixels) * ssScale,
                size.height() * 0.5f
            )
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

        // AE_FRAME_STATS: the same frame line the viewport writes, so a paged vs full-resolution
        // A/B can be read from `ms`, `tex_live` and `vt_pages` on one scene instead of two.
        gl::FrameStats& frameStats = gl::FrameStats::instance();
        frameStats.beginFrame();
        aDriver.renderGL(info, nullptr);
        ggl.glFinish();
        frameStats.endFrame(size.width(), size.height());

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
    // A PSD/ORA is imported through the app's own loader (masks included); an .anie through the
    // project serializer. The probe's camera and render path are the same either way.
    const QString suffix = QFileInfo(aOptions.projectPath).suffix().toLower();
    const bool imageFile = (suffix == QLatin1String("psd") || suffix == QLatin1String("ora"));
    if (imageFile) {
        ctrl::ImageFileLoader loader(device);
        loader.setOraImportMode(ctrl::ImageFileLoader::OraImportMode::Layered);
        if (!loader.load(aOptions.projectPath, project, reporter)) {
            std::fprintf(
                stderr, "render_probe: failed to import '%s'\n",
                aOptions.projectPath.toUtf8().constData()
            );
            std::fprintf(stderr, "  %s\n", loader.log().toUtf8().constData());
            return 2;
        }
        for (const QString& warning : loader.warnings())
            std::fprintf(stderr, "  warning: %s\n", warning.toUtf8().constData());
    } else {
        ctrl::ProjectLoader loader;
        if (!loader.load(aOptions.projectPath, project, device, reporter)) {
            std::fprintf(
                stderr, "render_probe: failed to load '%s'\n",
                aOptions.projectPath.toUtf8().constData()
            );
            for (const QString& line : loader.log()) {
                std::fprintf(stderr, "  %s\n", line.toUtf8().constData());
            }
            return 2;
        }
        // Conditions that did not fail the load but change what is drawn (the
        // skeleton cap): print them, so a probe run states the limitation it
        // measured instead of hiding it.
        for (const QString& warning : loader.warnings())
            std::fprintf(stderr, "  warning: %s\n", warning.toUtf8().constData());
    }

    // The rig-size census: does any skeleton cross the one cap?
    if (aOptions.boneStats) {
        return printBoneStats(project);
    }

    // The influence build's reference oracle: join every cache and hash its arrays.
    if (aOptions.benchInfluence > 0) {
        const int code = benchInfluenceMaps(project, aOptions.benchInfluence);
        if (code != 0)
            return code;
        if (aOptions.dumpInfluence.isEmpty())
            return 0;
    }
    if (!aOptions.dumpInfluence.isEmpty()) {
        const int code = dumpInfluenceMaps(project, aOptions.dumpInfluence);
        if (code != 0)
            return code;
        return 0;
    }

    // A per-layer diagnostic: list what the project holds, or hide all but the named layers.
    if (aOptions.listLayers || !aOptions.onlyLayer.isEmpty()) {
        int shown = 0;
        // WHICH LAYERS SHARE ONE IMAGE RESOURCE: duplicated layers keep separate keys and caches
        // but point at the same resource, and that is the identity a memory question asks about.
        // The id is assigned in walk order, so two lines with the same `res=` are one image.
        std::map<const img::ResourceData*, int> resourceIds;
        core::ObjectNode::Iterator itr(project.objectTree().topNode());
        while (itr.hasNext()) {
            core::ObjectNode* node = itr.next();
            if (!node)
                continue;
            // A FOLDER is not drawn as a layer, but it is where a folder MATTE lives (and where
            // the composite path starts), so the diagnostic must show it - otherwise a project
            // with a folder mask looks like it has none at all.
            if (node->type() == core::ObjectType_Folder) {
                if (aOptions.listLayers) {
                    auto* folder = static_cast<core::FolderNode*>(node);
                    QString flags;
                    if (folder->blendMode() != img::BlendMode_Normal)
                        flags += QStringLiteral("blend:%1 ").arg((int)folder->blendMode());
                    if (core::TimeLine* line = folder->timeLine()) {
                        for (core::TimeKeyType type :
                             {core::TimeKeyType_Blur, core::TimeKeyType_Wave, core::TimeKeyType_HSV}) {
                            if (line->filterEnabled(type))
                                flags += QStringLiteral("filter:%1 ").arg((int)type);
                        }
                        // a matte can live on the base value OR on animated keys; the old test
                        // saw only the first, so a keyed mask read as "no mask"
                        if (line->baseValue(core::TimeKeyType_Mask)
                            || !line->map(core::TimeKeyType_Mask).isEmpty())
                            flags += QStringLiteral("mask ");
                    }
                    std::printf(
                        "folder \"%s\" %s\n", folder->name().toUtf8().constData(),
                        flags.toUtf8().constData()
                    );
                }
                continue;
            }
            if (node->type() != core::ObjectType_Layer)
                continue;
            auto* layer = static_cast<core::LayerNode*>(node);
            if (aOptions.listLayers) {
                const QRect rect = layer->initialRect();
                core::TimeLine* line = layer->timeLine();
                QString flags;
                if (line) {
                    for (core::TimeKeyType type :
                         {core::TimeKeyType_Blur, core::TimeKeyType_Wave, core::TimeKeyType_HSV}) {
                        if (line->filterEnabled(type))
                            flags += QStringLiteral("filter:%1 ").arg((int)type);
                    }
                    if (line->baseValue(core::TimeKeyType_Mask)
                        || !line->map(core::TimeKeyType_Mask).isEmpty())
                        flags += QStringLiteral("mask ");
                    if (!line->map(core::TimeKeyType_Bone).isEmpty()
                        || !line->map(core::TimeKeyType_Pose).isEmpty())
                        flags += QStringLiteral("bone ");
                    if (!line->map(core::TimeKeyType_FFD).isEmpty()
                        || !line->map(core::TimeKeyType_Mesh).isEmpty())
                        flags += QStringLiteral("ffd ");
                }
                int resourceId = 0;
                if (line) {
                    if (auto* imageKey =
                            dynamic_cast<core::ImageKey*>(line->baseValue(core::TimeKeyType_Image))) {
                        const img::ResourceData* resource = imageKey->data().resource().get();
                        if (resource) {
                            auto found = resourceIds.find(resource);
                            if (found == resourceIds.end())
                                found = resourceIds.emplace(resource, (int)resourceIds.size() + 1).first;
                            resourceId = found->second;
                        }
                    }
                }
                std::printf(
                    "layer \"%s\" %dx%d res=%d %s\n", layer->name().toUtf8().constData(),
                    rect.width(), rect.height(), resourceId, flags.toUtf8().constData()
                );
                continue;
            }
            // A CONTAINS filter is what a person types when they do not know the list ("Layer 6"
            // also reaches Layer 60..69); "=NAME" is the exact form, for an isolation run where
            // the extra layers would be the thing under test.
            const bool exact = aOptions.onlyLayer.startsWith(QLatin1Char('='));
            const QString needle = exact ? aOptions.onlyLayer.mid(1) : aOptions.onlyLayer;
            const bool match = exact
                ? layer->name().compare(needle, Qt::CaseInsensitive) == 0
                : layer->name().contains(needle, Qt::CaseInsensitive);
            if (!match) {
                layer->setVisibility(false);
            } else {
                ++shown;
            }
        }
        if (aOptions.listLayers) {
            std::fflush(stdout);
            return 0;
        }
        if (shown == 0) {
            return fail(2, QStringLiteral("no layer name matches '%1'").arg(aOptions.onlyLayer));
        }
        std::printf("render_probe: %d layer(s) match '%s'\n", shown, aOptions.onlyLayer.toUtf8().constData());
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
        // THE STILL VIEW: the same frame, the same camera, repainted. Nothing about the scene
        // changed, so the demand pass has nothing new to learn - which is exactly the case the
        // paged renderer's signature replay exists for (see core::VirtualTexture::markDemand).
        // Each repaint is also written as frame-<f>-r<i>.png, so a `--pan` sequence can be
        // compared pixel for pixel against a run that re-marked.
        const QString path = outDir.filePath(QStringLiteral("frame-%1.png").arg(frame));
        const auto savePng = [&](const QString& aPath) {
            const QImage image(
                pixels.data(), aOptions.size.width(), aOptions.size.height(),
                QImage::Format_RGBA8888
            );
            return image.save(aPath, "PNG");
        };
        for (int i = 1; i < aOptions.repeat; ++i) {
            pixels = fixture.render(project, driver, aOptions, frame, aOptions.pan * float(i));
            if (ss > 1)
                pixels = boxDownsample(pixels, renderSize, aOptions.size, ss);
            const QString repaintPath =
                outDir.filePath(QStringLiteral("frame-%1-r%2.png").arg(frame).arg(i));
            if (!savePng(repaintPath)) {
                return fail(1, QStringLiteral("cannot write '%1'").arg(repaintPath));
            }
        }
        if (ss > 1)
            pixels = boxDownsample(pixels, renderSize, aOptions.size, ss);
        if (!savePng(path)) {
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

    // AE_VT_VERIFY_PAGES=end: the page-slot check, run AFTER the last frame with nothing in
    // flight, so it cannot re-time the landing it is asked about. A no-op for every other mode.
    core::VirtualTexture::debugVerifyAfterRun();

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
            if (!ok || options.supersample < 1 || options.supersample > 16) {
                std::fprintf(stderr, "render_probe: --supersample must be 1..16\n");
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
        } else if (arg == QLatin1String("--pan")) {
            QString text;
            if (!value(text)) return 2;
            bool ok = false;
            const float pan = text.toFloat(&ok);
            if (!ok || !std::isfinite(pan)) {
                return fail(2, QStringLiteral("--pan must be a number, got '%1'").arg(text));
            }
            options.pan = pan;
        } else if (arg == QLatin1String("--repeat")) {
            QString text;
            if (!value(text)) return 2;
            bool ok = false;
            const int repeat = text.toInt(&ok);
            if (!ok || repeat < 1 || repeat > 64) {
                return fail(2, QStringLiteral("--repeat must be 1..64, got '%1'").arg(text));
            }
            options.repeat = repeat;
        } else if (arg == QLatin1String("--offset")) {
            QString text;
            if (!value(text)) return 2;
            bool ok = false;
            const float offset = text.toFloat(&ok);
            if (!ok || !std::isfinite(offset)) {
                return fail(2, QStringLiteral("--offset must be a number, got '%1'").arg(text));
            }
            options.offset = offset;
        } else if (arg == QLatin1String("--only-layer")) {
            QString text;
            if (!value(text)) return 2;
            options.onlyLayer = text;
        } else if (arg == QLatin1String("--list-layers")) {
            options.listLayers = true;
        } else if (arg == QLatin1String("--metrics")) {
            options.metrics = true;
        } else if (arg == QLatin1String("--require-gpu")) {
            options.requireGpu = true;
        } else if (arg == QLatin1String("--dump-influence")) {
            if (!value(options.dumpInfluence)) return 2;
        } else if (arg == QLatin1String("--bench-influence")) {
            QString text;
            if (!value(text)) return 2;
            bool ok = false;
            options.benchInfluence = text.toInt(&ok);
            if (!ok || options.benchInfluence < 1 || options.benchInfluence > 100) {
                std::fprintf(stderr, "render_probe: --bench-influence must be 1..100\n");
                return 2;
            }
        } else if (arg == QLatin1String("--bone-stats")) {
            options.boneStats = true;
        } else if (arg == QLatin1String("--help") || arg == QLatin1String("-h")) {
            std::printf("%s", kUsage);
            return 0;
        } else {
            std::fprintf(stderr, "render_probe: unknown option '%s'\n", arg.toUtf8().constData());
            std::fprintf(stderr, "%s", kUsage);
            return 2;
        }
    }

    if (options.projectPath.isEmpty()
        || (options.outDir.isEmpty() && options.dumpInfluence.isEmpty() && options.benchInfluence <= 0
            && !options.boneStats)) {
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

    if (options.requireGpu) {
        // Xvfb's GLX is llvmpipe by construction; these markers cover the Mesa software
        // rasterizers (llvmpipe/softpipe/swrast) and drivers that spell "software" out. A
        // hardware renderer matches none of them.
        static const char* const kSoftwareMarkers[] = {"llvmpipe", "softpipe", "swrast", "software"};
        for (const char* marker : kSoftwareMarkers) {
            if (probe.mRenderer.contains(QLatin1String(marker), Qt::CaseInsensitive)) {
                return fail(4, QStringLiteral("--require-gpu: software renderer '%1'").arg(probe.mRenderer));
            }
        }
    }

    const int result = runRender(probe, options);

    // Tear GL objects down while the context is still current. runRender's project, driver and
    // fixture are already gone; the widget's own vertex array object goes here.
    gl::DeviceInfo::setInstance(nullptr);
    probe.mDefaultVao.reset();
    gl::Global::doneCurrent();
    return result;
}
