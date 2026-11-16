// GATE FOR THE DENSITY METER (gl::FrameStats::dens / dens_smax / aniso and their maxima,
// filled by the density pass inside core::MeshTransformer::callGL).
//
// The pass answers one question - how far did the DEFORMATION stretch the texels - and the
// only way to know it answers it correctly is to hand it a deformation whose answer is known
// in advance. So this harness builds the mesh the application builds (an image key's grid
// mesh, through GridMesh::createFromImage), runs the real callGL on it, and reads the frame
// line back:
//
//   [1] UNDEFORMED: every triangle's density is exactly 1.0 - log2 0, bin 8 - and dens_max
//       is 1. The uv area and the rest area are the same numbers, so anything but 1.0 means
//       the two areas are not in the same space (a transposed matrix, a uv source that is
//       not content pixels, a vertex order that does not match). s_max is 1 (the Jacobian is
//       the identity) and the anisotropy ratio is 1 (bin 0).
//   [2] SCALED 2x IN X: density 2.0 (log2 1, bin 10). Area scales with the linear factor in
//       ONE axis, which is what separates an area ratio from a length ratio. The Jacobian is
//       diag(2, 1), so s_max is 2 (also bin 10) and the ratio is 2 (aniso bin 2).
//   [3] SCALED 4x IN BOTH: density 16.0 (log2 4, bin 15 - the top bin, clamped there). This
//       is the only case that exercises the top of the range, where a real project's
//       outliers live. The Jacobian is 4 * I, so s_max is 4 (bin 12) but the ratio is 1:
//       uniform scale is not anisotropic, which is the control the ratio must not lose.
//   [4] ANISOTROPIC (2x in x, 1x in y) under a placement that churns every frame: density 2,
//       s_max 2, ratio 2 - the same footprint as [2] but asserted on all three readings, and
//       with a changing scale, rotation and translation to prove none of it leaks into any
//       of them. A square 2x2 and a 4x1 footprint have the SAME AREA (dens 4) but differ
//       here: s_max 2 vs 4, ratio 1 vs 4. That is the information the area ratio loses.
//   [5] ANIMATED: the deformation AND the layer's placement change on EVERY frame, and every
//       frame must still read exactly the density, s_max and ratio its own deformation
//       implies. This is the case a still image cannot see: a vertex buffer that is read one
//       frame late, a uniform that is only right on the first call, an inverse computed once,
//       or a readback that repeats the previous frame all pass cases [1]-[4] (which hold
//       their deformation for four frames) and fail here. The placement churn is the sharp
//       end: the pass divides the layer's world/inner matrices out, so a scale, rotation or
//       translation that changes every frame must not reach the numbers at all.
//
// It also asserts that each histogram's measured weight equals the mesh's own total uv area,
// which proves all three covered every triangle: the pass drops triangles below kMinUvArea (a
// quarter of a pixel) and a grid mesh has none, so a drop here would be a threshold that is
// too high.
//
// The line is read two frames after the one it describes (the copy is issued when the next
// frame starts and mapped one frame later, so that the map never waits on the GPU), hence
// FRAMES_PER_CASE frames per still deformation and the two-frame offset in [5].
//
// Run under xvfb-run (it creates a real core-profile context):
//   xvfb-run -a build/tests/Debug/density_meter
#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonObject>
#include <QOpenGLFunctions_4_0_Core>
#include <QOpenGLVersionFunctionsFactory>
#include <QOpenGLWidget>
#include <QSize>
#include <QStringList>
#include <QSurfaceFormat>
#include <QVector>
#include <QVector2D>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "XCAssert.h"
#include "core/GridMesh.h"
#include "core/ImageKey.h"
#include "core/MeshTransformer.h"
#include "core/TimeKeyExpans.h"
#include "gl/DeviceInfo.h"
#include "gl/FrameStats.h"
#include "gl/Global.h"
#include "gl/Vector2.h"
#include "gl/Vector3.h"
#include "gl/VertexArrayObject.h"
#include "util/ArrayBlock.h"

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

HarnessAssertHandler gAssertHandler;
HarnessErrorHandler gErrorHandler;

int failures = 0;
void check(bool aOk, const char* aWhat) {
    if (!aOk) {
        std::printf("FAIL  %s\n", aWhat);
        ++failures;
    }
}

// the image the grid is built from: a grid mesh needs an alpha mask, and a fully opaque one
// gives the plainest mesh the application ever draws
constexpr int kImageSize = 64;
constexpr int kCellPx = 8;
// enough frames for the readback pipeline to deliver the frame the case drew (see the top)
constexpr int kFramesPerCase = 4;

float triangleArea(const gl::Vector2& a, const gl::Vector2& b, const gl::Vector2& c) {
    return std::fabs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) * 0.5f;
}

// the bin the pass must put a triangle of this density in: the same arithmetic the geometry
// shader does, mirrored here so the gate reads the line the way a policy would
int binForDensity(double aDensity) {
    if (!(aDensity > 0.0))
        return 0;
    const double log2 = std::log2(aDensity);
    const double raw = (log2 - gl::FrameStats::kDensityBinBase) / gl::FrameStats::kDensityBinStep + 0.5;
    const int bin = (int)std::floor(raw);
    return bin < 0 ? 0 : (bin > gl::FrameStats::kDensityBins - 1 ? gl::FrameStats::kDensityBins - 1 : bin);
}

// one frame line: the area-weighted histograms and the largest value of that frame. `dens`
// and `dens_smax` share the bin arithmetic (log2 value, same base and step); `aniso` is per
// EDGE (gl::FrameStats::kAnisoBinEdges), so its bin index is an edge index, not a log.
struct FrameLine {
    bool valid = false;
    double bins[gl::FrameStats::kDensityBins] = {};
    double maxDensity = 0.0;
    double smaxBins[gl::FrameStats::kSmaxBins] = {};
    double maxSmax = 0.0;
    double anisoBins[gl::FrameStats::kAnisoBins] = {};
    double maxAniso = 0.0;
    double total() const {
        double sum = 0.0;
        for (int b = 0; b < gl::FrameStats::kDensityBins; ++b)
            sum += bins[b];
        return sum;
    }
    double smaxTotal() const {
        double sum = 0.0;
        for (int b = 0; b < gl::FrameStats::kSmaxBins; ++b)
            sum += smaxBins[b];
        return sum;
    }
    double anisoTotal() const {
        double sum = 0.0;
        for (int b = 0; b < gl::FrameStats::kAnisoBins; ++b)
            sum += anisoBins[b];
        return sum;
    }
    int dominant() const {
        int best = 0;
        for (int b = 1; b < gl::FrameStats::kDensityBins; ++b) {
            if (bins[b] > bins[best])
                best = b;
        }
        return best;
    }
    int smaxDominant() const {
        int best = 0;
        for (int b = 1; b < gl::FrameStats::kSmaxBins; ++b) {
            if (smaxBins[b] > smaxBins[best])
                best = b;
        }
        return best;
    }
    int anisoDominant() const {
        int best = 0;
        for (int b = 1; b < gl::FrameStats::kAnisoBins; ++b) {
            if (anisoBins[b] > anisoBins[best])
                best = b;
        }
        return best;
    }
};

// the value of a `key=value` field, ending at the next space (or the line's end)
double valueOf(const QString& aText, const QString& aKey) {
    const int at = aText.indexOf(aKey);
    if (at < 0)
        return 0.0;
    const int start = at + aKey.length();
    int end = aText.indexOf(QLatin1Char(' '), start);
    if (end < 0)
        end = aText.length();
    return aText.mid(start, end - start).toDouble();
}

// the numbers inside `key=[a,b,...]`, where aKey includes the opening bracket
bool binsOf(const QString& aText, const QString& aKey, double* aOut, int aCount) {
    const int at = aText.indexOf(aKey);
    if (at < 0)
        return false;
    const int start = at + aKey.length();
    const int close = aText.indexOf(QLatin1Char(']'), start);
    if (close < start)
        return false;
    const QStringList values = aText.mid(start, close - start).split(QLatin1Char(','));
    if (values.count() != aCount)
        return false;
    for (int i = 0; i < aCount; ++i)
        aOut[i] = values.at(i).toDouble();
    return true;
}

// every frame line in the stats file, oldest first
QVector<FrameLine> frameLines(const QString& aPath) {
    QVector<FrameLine> result;
    QFile file(aPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return result;
    const QList<QByteArray> lines = file.readAll().split('\n');
    for (const QByteArray& raw : lines) {
        const QString text = QString::fromUtf8(raw);
        if (!text.contains(QStringLiteral("dens=[")))
            continue;
        FrameLine line;
        if (!binsOf(text, QStringLiteral("dens=["), line.bins, gl::FrameStats::kDensityBins))
            continue;
        if (!binsOf(text, QStringLiteral("dens_smax=["), line.smaxBins, gl::FrameStats::kSmaxBins))
            continue;
        if (!binsOf(text, QStringLiteral("aniso=["), line.anisoBins, gl::FrameStats::kAnisoBins))
            continue;
        line.maxDensity = valueOf(text, QStringLiteral("dens_max="));
        line.maxSmax = valueOf(text, QStringLiteral("smax_max="));
        line.maxAniso = valueOf(text, QStringLiteral("aniso_max="));
        line.valid = true;
        result.push_back(line);
    }
    return result;
}

void reportLine(const char* aWhat, const FrameLine& aLine) {
    std::printf(
        "INFO %-20s bin=%2d weight=%9.2f/%9.2f dens_max=%.6f log2=%.6f "
        "smax_bin=%2d smax_max=%.6f aniso_bin=%d aniso_max=%.6f\n",
        aWhat,
        aLine.dominant(),
        aLine.bins[aLine.dominant()],
        aLine.total(),
        aLine.maxDensity,
        aLine.maxDensity > 0.0 ? std::log2(aLine.maxDensity) : -99.0,
        aLine.smaxDominant(),
        aLine.maxSmax,
        aLine.anisoDominant(),
        aLine.maxAniso
    );
}

// the anisotropy bin of a ratio: the highest edge it has reached, i.e. bin b covers
// [kAnisoBinEdges[b - 1], kAnisoBinEdges[b]) and the last bin is open-ended
int binForAniso(double aRatio) {
    int bin = 0;
    for (int i = 0; i < gl::FrameStats::kAnisoBins - 1; ++i) {
        if (aRatio >= (double)gl::FrameStats::kAnisoBinEdges[i])
            bin = i + 1;
    }
    return bin;
}

// one frame's reading: the deformation is known, so the whole mesh must be in one bin of
// each histogram and each maximum must be the value that deformation implies
void checkLine(
    const FrameLine& aLine,
    double aDensity,
    double aSMax,
    double aAniso,
    double aTolerance,
    const char* aWhat
) {
    check(aLine.valid, aWhat);
    if (!aLine.valid)
        return;
    const double total = aLine.total();
    const int expected = binForDensity(aDensity);
    check(total > 0.0, "the pass measured some area");
    check(std::fabs(aLine.maxDensity - aDensity) <= aDensity * aTolerance, "dens_max is the known density");
    check(aLine.dominant() == expected, "the weight lands in the bin the deformation implies");
    check(aLine.bins[expected] >= total * 0.999, "and essentially all of it does");

    // s_max is the larger singular value of the same Jacobian: 2 for 2x in x, 4 for 4x in both
    const double smaxTotal = aLine.smaxTotal();
    const int expectedSmax = binForDensity(aSMax);
    check(
        std::fabs(aLine.maxSmax - aSMax) <= aSMax * aTolerance,
        "smax_max is the known singular value"
    );
    check(aLine.smaxDominant() == expectedSmax, "s_max lands in the bin its value implies");
    check(aLine.smaxBins[expectedSmax] >= smaxTotal * 0.999, "and essentially all of it does");

    // the ratio a trilinear level would over-blur the minor axis by: 1 for a square footprint,
    // 2 for 2x in x, and 1 again for 4x in both (uniform scale is not anisotropic)
    const double anisoTotal = aLine.anisoTotal();
    const int expectedAniso = binForAniso(aAniso);
    check(
        std::fabs(aLine.maxAniso - aAniso) <= aAniso * aTolerance,
        "aniso_max is the known ratio"
    );
    check(aLine.anisoDominant() == expectedAniso, "the anisotropy lands in the bin its ratio implies");
    check(aLine.anisoBins[expectedAniso] >= anisoTotal * 0.999, "and essentially all of it does");
}

// every triangle of this mesh is far above the pass's sliver threshold, so all three
// histograms must account for the mesh's whole uv area
void checkWeights(const FrameLine& aLine, double aUvArea) {
    const double tolerance = aUvArea * 0.001;
    check(std::fabs(aLine.total() - aUvArea) < tolerance, "every triangle was measured (dens)");
    check(std::fabs(aLine.smaxTotal() - aUvArea) < tolerance, "every triangle was measured (s_max)");
    check(std::fabs(aLine.anisoTotal() - aUvArea) < tolerance, "every triangle was measured (aniso)");
}

} // namespace

// The XC hooks are process-wide symbols the libraries reference: they must have EXTERNAL
// linkage, so they live outside the anonymous namespace.
XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setVersion(gl::Global::kVersion.first, gl::Global::kVersion.second);
    QSurfaceFormat::setDefaultFormat(fmt);

    QOpenGLWidget glWidget;
    glWidget.resize(kImageSize, kImageSize);
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

    // a core-profile draw needs a bound VAO; the application relies on the viewport's
    // default one staying bound, so the harness does the same
    gl::VertexArrayObject vao;
    vao.bind();

    // the meter reports through FrameStats, which is a singleton built on first use: the
    // variables have to be set before anything touches it
    const QString statsPath = QDir::temp().filePath(QStringLiteral("ae_density_meter_gate.txt"));
    const QString tracePath = statsPath + QStringLiteral(".trace");
    QFile::remove(statsPath);
    QFile::remove(tracePath);
    qputenv("AE_FRAME_STATS", statsPath.toUtf8());
    // the trace is part of the instrument, so the gate runs it (a budget of two layers: it
    // does a synchronous read, and this is a gate, not a benchmark)
    qputenv("AE_FRAME_STATS_DENSITY_TRACE", "2");

    // ---- the mesh, built exactly as an image layer builds it -----------------------------
    QImage mask(kImageSize, kImageSize, QImage::Format_RGBA8888);
    mask.fill(QColor(255, 255, 255, 255));
    core::ImageKey imageKey;
    core::GridMesh& grid = imageKey.data().gridMesh();
    grid.createFromImage(mask.bits(), mask.size(), kCellPx);

    core::TimeKeyExpans expans;
    expans.setAreaImageKey(&imageKey);
    expans.setFFDMesh(&grid);

    const int vertexCount = grid.vertexCount();
    const int triangleCount = (int)grid.indexCount() / 3;
    check(vertexCount > 4, "the image grid has vertices");
    check(triangleCount > 4, "the image grid has triangles");
    if (vertexCount <= 0 || triangleCount <= 0)
        return 1;

    // the mesh's own uv area total: what the histogram must account for in full
    double totalUvArea = 0.0;
    {
        const gl::Vector2* uv = grid.texCoords();
        for (int t = 0; t < triangleCount; ++t) {
            const int i0 = (int)grid.indices()[t * 3 + 0];
            const int i1 = (int)grid.indices()[t * 3 + 1];
            const int i2 = (int)grid.indices()[t * 3 + 2];
            totalUvArea += triangleArea(uv[i0], uv[i1], uv[i2]);
        }
    }
    check(totalUvArea > 0.0, "the mesh has uv area");
    std::printf("INFO mesh verts=%d tris=%d total_uv=%.2f\n", vertexCount, triangleCount, totalUvArea);

    // the mesh's rest positions: every deformation below is a known function of these
    QVector<gl::Vector3> rest(vertexCount);
    for (int i = 0; i < vertexCount; ++i)
        rest[i] = grid.positions()[i];

    // ---- [1]-[3] three known deformations, held still -----------------------------------
    QVector<gl::Vector3> x2(vertexCount);
    QVector<gl::Vector3> xy4(vertexCount);
    for (int i = 0; i < vertexCount; ++i) {
        const gl::Vector3 p = rest[i];
        x2[i] = gl::Vector3::make(p.x * 2.0f, p.y, p.z);
        xy4[i] = gl::Vector3::make(p.x * 4.0f, p.y * 4.0f, p.z);
    }

    core::MeshTransformer transformer(QStringLiteral(":/shaders/MeshTransformVert.glsl"));
    core::LayerMesh::MeshBuffer& buffer = grid.getMeshBuffer();

    gl::FrameStats& stats = gl::FrameStats::instance();
    check(stats.enabled(), "the meter is enabled by AE_FRAME_STATS");

    auto runFrames = [&](const QVector<gl::Vector3>& aPositions, int aFrames) {
        const util::ArrayBlock<const gl::Vector3> positions(aPositions.constData(), vertexCount);
        for (int frame = 0; frame < aFrames; ++frame) {
            stats.beginFrame();
            // the real pass: transform feedback, then the density draw and its copy
            transformer.callGL(expans, buffer, grid.originOffset(), positions, false, false);
            stats.endFrame(kImageSize, kImageSize);
        }
    };

    struct Case {
        const char* what;
        const QVector<gl::Vector3>* positions;
        double density;
        // the larger singular value of the uv->mesh Jacobian, and its ratio to the smaller:
        // a uniform 4x scale is s_max 4 but ratio 1, while 2x in x alone is s_max 2 and ratio 2
        double sMax;
        double aniso;
    };
    const Case cases[] = {
        {"undeformed", &rest, 1.0, 1.0, 1.0},
        {"scaled 2x in x", &x2, 2.0, 2.0, 2.0},
        {"scaled 4x in both", &xy4, 16.0, 4.0, 1.0},
    };

    for (const Case& c : cases) {
        const int before = frameLines(statsPath).count();
        runFrames(*c.positions, kFramesPerCase);
        const QVector<FrameLine> lines = frameLines(statsPath);
        if (lines.count() <= before) {
            check(false, "the frame line carries dens/dens_smax/aniso");
            continue;
        }
        const FrameLine line = lines.last();
        reportLine(c.what, line);
        checkLine(line, c.density, c.sMax, c.aniso, 0.002, "the still frame reads the known deformation");
        checkWeights(line, totalUvArea);
    }

    // ---- [4] ANISOTROPIC: 2x in x, 1x in y, under a placement that churns every frame ----
    // The case the area ratio cannot see: density is 2 (s_max * s_min), but the footprint is
    // a 2:1 ellipse, so the level a trilinear read picks is one too coarse along the major
    // axis. s_max says 2 (log2 1, the same bin as density 2) and the ratio says 2, which is
    // aniso bin 2. The placement changes on every frame to prove none of it reaches a number.
    {
        const int before = frameLines(statsPath).count();
        const util::ArrayBlock<const gl::Vector3> positions(x2.constData(), vertexCount);
        for (int frame = 0; frame < kFramesPerCase; ++frame) {
            expans.srt().setScale(QVector2D(1.0f + 0.4f * (float)frame, 0.7f + 0.3f * (float)frame));
            expans.srt().setRotate(0.15f * (float)frame);
            expans.srt().setPos(QVector2D(7.0f * (float)frame, -5.0f * (float)frame));
            stats.beginFrame();
            transformer.callGL(expans, buffer, grid.originOffset(), positions, false, false);
            stats.endFrame(kImageSize, kImageSize);
        }
        const QVector<FrameLine> lines = frameLines(statsPath);
        if (lines.count() <= before) {
            check(false, "the anisotropic frame line was written");
        } else {
            const FrameLine line = lines.last();
            reportLine("anisotropic 2x in x", line);
            checkLine(line, 2.0, 2.0, 2.0, 0.002, "the anisotropic frame reads its own footprint");
            // the exact bins the policy reads, independent of the helper
            check(line.dominant() == 10, "the anisotropic density is bin 10");
            check(line.smaxDominant() == 10, "the anisotropic s_max is bin 10");
            check(line.anisoDominant() == 2, "the anisotropy ratio 2 is aniso bin 2");
            checkWeights(line, totalUvArea);
        }
        // leave the expansion at rest; the animated case sets its own placement
        expans.srt().setScale(QVector2D(1.0f, 1.0f));
        expans.srt().setRotate(0.0f);
        expans.srt().setPos(QVector2D(0.0f, 0.0f));
    }

    // ---- [5] ANIMATED: a new deformation and a new placement on every frame --------------
    {
        constexpr int kAnimatedFrames = 6;
        QVector<QVector<gl::Vector3>> deformed(kAnimatedFrames);
        QVector<double> density(kAnimatedFrames);
        for (int i = 0; i < kAnimatedFrames; ++i) {
            const float factor = 1.0f + 0.5f * (float)i;
            density[i] = factor;
            QVector<gl::Vector3>& positions = deformed[i];
            positions.resize(vertexCount);
            for (int v = 0; v < vertexCount; ++v)
                positions[v] = gl::Vector3::make(rest[v].x * factor, rest[v].y, rest[v].z);
        }

        const int before = frameLines(statsPath).count();
        for (int i = 0; i < kAnimatedFrames; ++i) {
            // a different PLACEMENT every frame as well: scale, rotation and position. The
            // pass divides world * inner out, so none of this may reach the number.
            expans.srt().setScale(QVector2D(1.0f + 0.4f * (float)i, 0.7f + 0.3f * (float)i));
            expans.srt().setRotate(0.15f * (float)i);
            expans.srt().setPos(QVector2D(7.0f * (float)i, -5.0f * (float)i));

            const util::ArrayBlock<const gl::Vector3> positions(deformed[i].constData(), vertexCount);
            stats.beginFrame();
            transformer.callGL(expans, buffer, grid.originOffset(), positions, false, false);
            stats.endFrame(kImageSize, kImageSize);
        }

        const QVector<FrameLine> lines = frameLines(statsPath);
        // the line a frame writes describes the frame two frames back (the copy is issued
        // when the next frame starts and mapped one frame later), so the frame that wrote
        // lines[before + i] measured deformation i - 2
        for (int i = 2; i < kAnimatedFrames; ++i) {
            if (lines.count() <= before + i) {
                check(false, "the animated frame wrote a line");
                continue;
            }
            const FrameLine& line = lines.at(before + i);
            const double expected = density[i - 2];
            char what[64] = {};
            std::snprintf(what, sizeof(what), "animated frame %d", i);
            reportLine(what, line);
            // the placement churn makes the composed inverse inexact by a few ulps, so the
            // tolerance is relative and small rather than exact. The x-only scale means the
            // singular values are (factor, 1): s_max = factor and ratio = factor as well.
            checkLine(
                line, expected, expected, expected, 0.002, "the animated frame reads its own deformation"
            );
            checkWeights(line, totalUvArea);
        }
    }

    // the trace is the instrument a real project is read with: it must have run and produced
    // both spaces for the worst triangle
    {
        QFile trace(tracePath);
        const bool opened = trace.open(QIODevice::ReadOnly | QIODevice::Text);
        const QString text = opened ? QString::fromUtf8(trace.readAll()) : QString();
        check(!text.isEmpty(), "the density trace wrote a file");
        check(text.contains(QStringLiteral("det_wi=")), "the trace reports the content/mesh space factor");
        check(text.contains(QStringLiteral("d_mesh=")), "the trace reports the mesh-space density");
    }

    // a line that describes a frame must never carry a NaN or an infinity
    {
        const QVector<FrameLine> lines = frameLines(statsPath);
        bool finite = !lines.isEmpty();
        for (const FrameLine& line : lines) {
            for (int b = 0; b < gl::FrameStats::kDensityBins; ++b)
                finite = finite && std::isfinite(line.bins[b]);
            for (int b = 0; b < gl::FrameStats::kSmaxBins; ++b)
                finite = finite && std::isfinite(line.smaxBins[b]);
            for (int b = 0; b < gl::FrameStats::kAnisoBins; ++b)
                finite = finite && std::isfinite(line.anisoBins[b]);
            finite = finite && std::isfinite(line.maxDensity);
            finite = finite && std::isfinite(line.maxSmax);
            finite = finite && std::isfinite(line.maxAniso);
        }
        check(finite, "every line is finite");
    }

    if (failures == 0) {
        std::printf("PASS density_meter\n");
        QFile::remove(statsPath);
        QFile::remove(tracePath);
        return 0;
    }
    std::printf("FAIL density_meter (%d checks)\n", failures);
    std::printf("INFO frame lines left at %s\n", qPrintable(statsPath));
    return 1;
}
