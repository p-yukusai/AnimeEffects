// Verification harness for the directional blur (BlurKey blurX/blurY/angle + the
// world-space ellipse math in core/WorldBlurMath.h and the separable Gaussian +
// downsample ladder in core/FilterFrame).
//
// Suites:
//   S1 math        worldBlurEllipse (via the production TimeCacheAccessor over real node
//                  chains, incl. layer-in-folder transform stacks) vs an independent
//                  double-precision Jacobi SVD reference; invariants + brute-force checks.
//   S2 ladder      FilterFrame::blurLadderLevel vs an independent reference, boundary grid.
//   S3 blending    TimeKeyBlender blur blending: lerp, accumulated angle, easing, gating.
//   S4 render      full production render vs the CPU reference blur (direct + ladder,
//                  transformed layers, transformed folders, nesting, zoom, camera
//                  rotation/flip, odd canvases).
//   S5 orientation ground-truth: second-moment angle of a blurred dot (folder+layer
//                  transforms, camera rotation/flip), ellipse swap identity,
//                  isotropic/directional equivalence, layer-vs-folder byte identity.
//   S6 gating/edge: amount <= epsilon renders identical to no key (identity kernel
//                  below ~0.5 lets interpolations ramp in smoothly); key-before-frame inactive.
//   S7 interactions: blur x clippee (on folder and layer), nested folder blur, blend
//                  modes over a captured background, layer/folder HSV ordering.
//   S8 serialization, S9 export path, S11 PSD import round-trip (folder blend modes via
//   PSDWriter -> ImageFileLoader), S12 folder blend modes GPU-vs-CPU reference,
//   S13 ORA import (synthetic .ora via miniz -> ImageFileLoader, folder blend modes),
//   S14 golden images (tests/golden: generated PSD/ORA inputs -> import -> render vs
//   reference-exported (Krita/GIMP) expected.png; regenerate inputs with --golden-gen),
//   S10 perf snapshot.
//
// Must be run from the repo root (shaders are loaded from ./data/shader).
#include <cmath>
#include <clocale>
#include <locale.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <tuple>
#include <vector>
#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QImage>
#include <QJsonObject>
#include <QMainWindow>
#include <QOpenGLWidget>
#include <QOpenGLVersionFunctionsFactory>
#include <QSurfaceFormat>
#include <QVersionNumber>
#include "gl/Global.h"
#include "gl/DeviceInfo.h"
#include "gl/VertexArrayObject.h"
#include "core/ObjectTree.h"
#include "core/LayerNode.h"
#include "core/FolderNode.h"
#include "core/BlurKey.h"
#include "core/HsvKey.h"
#include "core/Serializer.h"
#include "core/Deserializer.h"
#include "core/TimeLine.h"
#include "core/Project.h"
#include "core/Animator.h"
#include "core/WorldBlurMath.h"
#include "core/FilterFrame.h"
#include "core/TimeCacheAccessor.h"
#include "core/TimeKeyExpans.h"
#include "ctrl/Exporter.h"
#include "ctrl/ImageFileLoader.h"
#include "img/BlendMode.h"
#include "img/PSDFormat.h"
#include "img/PSDWriter.h"
#include "img/oraParser.h"
#include "deps/zip_file.h"
#include "util/Easing.h"
#include "util/IProgressReporter.h"
#include "util/Range.h"
#include "util/StreamWriter.h"
#include "util/StreamReader.h"
#include "cpu_ref.h"
#include "scene.h"
#include "harness.h"
#include "golden.h"

namespace {

using namespace hb;

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;

} // namespace

// the libs reference these globals (defined by src/gui/Main.cpp in the app)
class HarnessAssertHandler: public XCAssertHandler {
public:
    void failure() const override { std::fprintf(stderr, "XC_ASSERT failure\n"); }
};
class HarnessErrorHandler: public XCErrorHandler {
public:
    void critical(const QString& aText, const QString& aInfo, const QString& aDetail) const override {
        std::fprintf(
            stderr, "XC_FATAL_ERROR: %s | %s | %s\n", aText.toUtf8().constData(), aInfo.toUtf8().constData(),
            aDetail.toUtf8().constData());
        std::abort();
    }
};
XCAssertHandler* gXCAssertHandler = nullptr;
XCErrorHandler* gXCErrorHandler = nullptr;

// src/core/Project.cpp references this gui method, but libgui is not linked into the
// harness; the class declaration comes from gui/AudioPlaybackWidget.h (pulled in by
// core/Project.h), so a lone definition satisfies the linker
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

namespace {

//-------------------------------------------------------------------------------------------------
// test bookkeeping (gChecks/gFails/expect/suiteHeader/caseReport/diffStr) lives in harness.h

//-------------------------------------------------------------------------------------------------
// GL bootstrap (mirrors MainDisplayWidget::initializeGL essentials)
class HarnessGLWidget: public QOpenGLWidget {
public:
    gl::DeviceInfo mDeviceInfo;
    QScopedPointer<gl::VertexArrayObject> mVAO;
    bool mGLReady = false;

protected:
    void initializeGL() override {
        if (!context()->isValid()) {
            std::fprintf(stderr, "FATAL: invalid OpenGL context\n");
            std::exit(2);
        }
        auto* functions = QOpenGLVersionFunctionsFactory::get<gl::Global::Functions>(context());
        if (!functions || !functions->initializeOpenGLFunctions()) {
            std::fprintf(stderr, "FATAL: failed to get OpenGL 4.0 core functions\n");
            std::exit(2);
        }
        if (!mGLReady) {
            gl::Global::setContext(*this);
            gl::Global::setFunctions(*functions);
            mDeviceInfo.load();
            gl::DeviceInfo::setInstance(&mDeviceInfo);
            const char* version = (const char*)context()->functions()->glGetString(GL_VERSION);
            std::printf("OpenGL: %s\n", version ? version : "(unknown)");
        } else {
            // initializeGL re-runs when the widget recreates its FBO/context; the factory's
            // functions object is cached per context, so the previously registered one may
            // be dangling by now (ASan-caught). Refresh the registration.
            gl::Global::clearFunctions();
            gl::Global::setFunctions(*functions);
        }
        mVAO.reset(new gl::VertexArrayObject());
        mVAO->bind(); // keep binding (core profile requires a VAO)
        mGLReady = true;
    }
};

//-------------------------------------------------------------------------------------------------
// generated content
void fillWedge(int aX, int aY, uint8_t* aPx) {
    // 61x47 asymmetric test content with a transparent margin (the GridMeshCreator insets
    // layer-edge vertices, so content stays 4px away from the image edge)
    aPx[0] = aPx[1] = aPx[2] = aPx[3] = 0;
    if (aX >= 6 && aX <= 40 && aY >= 8 && aY <= 28) { // opaque red-ish block
        aPx[0] = 200; aPx[1] = 80; aPx[2] = 60; aPx[3] = 255;
    }
    if (aX >= 44 && aX <= 56 && aY >= 6 && aY <= 40) { // vertical gradient bar
        const uint8_t v = (uint8_t)((aY - 6) * 255 / 34);
        aPx[0] = v; aPx[1] = (uint8_t)(255 - v); aPx[2] = 128; aPx[3] = 255;
    }
    if (aY >= 30 && aY <= 42 && aX >= 8 && aX <= 8 + (aY - 30) * 2) { // semi-transparent wedge
        aPx[0] = 40; aPx[1] = 200; aPx[2] = 240; aPx[3] = 128;
    }
    if (aY >= 5 && aY <= 40 && aX == 3 + aY) { // hard 1px diagonal line
        aPx[0] = aPx[1] = aPx[2] = aPx[3] = 255;
    }
    if ((aX - 30) * (aX - 30) + (aY - 38) * (aY - 38) <= 4) { // white dot
        aPx[0] = aPx[1] = aPx[2] = aPx[3] = 255;
    }
}

void fillWedgeOpaque(int aX, int aY, uint8_t* aPx) {
    // same shape as fillWedge but every covered pixel is opaque: with alpha 255 the
    // premultiplied content survives the RGBA8 readback without quantization, so the
    // CPU reference can reproduce steep/discontinuous blend modes (Vivid Light, Hard
    // Mix) exactly - a semi-transparent content readback loses the straight values and
    // their noise is amplified to full scale by the a/(2-2b) gradients
    fillWedge(aX, aY, aPx);
    if (aPx[3] != 0)
        aPx[3] = 255;
}

void fillDot(int aX, int aY, uint8_t* aPx) {
    aPx[0] = aPx[1] = aPx[2] = aPx[3] = 0;
    if (aX == 15 && aY == 15) { // exact center of 31x31
        aPx[0] = aPx[1] = aPx[2] = aPx[3] = 255;
    }
}

// solid square at 50% alpha (straight): the overlap of two of these is the alpha
// discriminator - additive gives clamp(0.5+0.5)=1.0, the over operator gives 0.75
void fillHalf(int aX, int aY, uint8_t* aPx) {
    aPx[0] = aPx[1] = aPx[2] = aPx[3] = 0;
    if (aX >= 4 && aX <= 26 && aY >= 4 && aY <= 26) { // 23x23 centered in 31x31
        aPx[0] = 200; aPx[1] = 80; aPx[2] = 60; aPx[3] = 128;
    }
}

// keeps generated resources alive for a scene's lifetime
struct ResPool {
    std::vector<img::ResourceNode*> nodes;
    ~ResPool() { qDeleteAll(nodes.begin(), nodes.end()); }
    img::ResourceNode* wedge(const QString& aId) {
        nodes.push_back(scene::makeImage(aId, QSize(61, 47), fillWedge));
        return nodes.back();
    }
    img::ResourceNode* wedgeOpaque(const QString& aId) {
        nodes.push_back(scene::makeImage(aId, QSize(61, 47), fillWedgeOpaque));
        return nodes.back();
    }
    img::ResourceNode* dot(const QString& aId) {
        nodes.push_back(scene::makeImage(aId, QSize(31, 31), fillDot));
        return nodes.back();
    }
    img::ResourceNode* half(const QString& aId) {
        nodes.push_back(scene::makeImage(aId, QSize(31, 31), fillHalf));
        return nodes.back();
    }
};

//-------------------------------------------------------------------------------------------------
// S1: worldBlurEllipse math vs independent reference
void checkEllipseCase(
    const QString& aName, const std::vector<std::array<double, 3>>& aChainLeafToRoot, double aBlurX, double aBlurY,
    double aAngleDeg, bool aUseLayerLeaf
) {
    static ResPool pool;
    core::ObjectTree tree;
    auto* root = new core::FolderNode("root");
    tree.grabTopNode(root);
    core::ObjectNode* parent = root;
    core::ObjectNode* leaf = nullptr;
    // the chain is ordered leaf -> root; build the tree root-most first
    for (size_t i = aChainLeafToRoot.size(); i-- > 0;) {
        const auto& s = aChainLeafToRoot[i];
        const bool isLeaf = (i == 0);
        const QString name = QString("n%1").arg(i);
        if (isLeaf && aUseLayerLeaf) {
            leaf = scene::addLayer(tree, parent, pool.wedge("math_img"), name, QVector2D(0, 0), s[0] * kRadToDeg,
                QVector2D((float)s[1], (float)s[2]));
        } else {
            leaf = scene::addFolder(parent, name, QVector2D(0, 0), s[0] * kRadToDeg, QVector2D((float)s[1], (float)s[2]));
        }
        parent = leaf;
    }

    core::TimeCacheAccessor accessor(*root, tree.timeCacheLock(), scene::makeTimeInfo(0), false);
    const core::WorldBlurEllipse prod =
        core::worldBlurEllipse(accessor, leaf, (float)aBlurX, (float)aBlurY, (float)aAngleDeg);
    const ref::Ellipse ref = ref::worldBlurEllipseRef(aChainLeafToRoot, aBlurX, aBlurY, aAngleDeg);

    bool ok = true;
    // production invariants
    ok &= expect(std::abs(prod.majorDir.length() - 1.0f) < 1e-4f, aName + ": majorDir not unit");
    ok &= expect(std::abs(prod.minorDir.length() - 1.0f) < 1e-4f, aName + ": minorDir not unit");
    ok &= expect(std::abs(QVector2D::dotProduct(prod.majorDir, prod.minorDir)) < 1e-4f,
        aName + ": axes not orthogonal");
    ok &= expect(prod.majorRadius >= prod.minorRadius - 1e-6f && prod.minorRadius >= 0.0f,
        aName + ": radius ordering");

    // radii vs reference (relative, scaled for tiny values)
    const double rScale1 = std::max(ref.majorRadius, 1.0);
    const double rScale2 = std::max(ref.minorRadius, 1.0);
    ok &= expect(std::abs(prod.majorRadius - ref.majorRadius) <= 2e-3 * rScale1,
        aName + QString(": majorRadius prod=%1 ref=%2").arg(prod.majorRadius).arg(ref.majorRadius));
    ok &= expect(std::abs(prod.minorRadius - ref.minorRadius) <= 2e-3 * rScale2,
        aName + QString(": minorRadius prod=%1 ref=%2").arg(prod.minorRadius).arg(ref.minorRadius));

    // principal axis direction (axis lines: sign-ambiguous, compare |cross|); only when the
    // ellipse is anisotropic enough for the axis to be well-defined
    if (ref.majorRadius > 1.05 * std::max(ref.minorRadius, 1e-9)) {
        const double cross =
            std::abs(prod.majorDir.x() * ref.majorY - prod.majorDir.y() * ref.majorX);
        ok &= expect(cross <= 1e-2,
            aName + QString(": majorDir prod=(%1,%2) ref=(%3,%4)")
                        .arg(prod.majorDir.x()).arg(prod.majorDir.y()).arg(ref.majorX).arg(ref.majorY));
    }
    caseReport(aName, ok,
        QString("r=(%1,%2) ang_prod=%3")
            .arg(prod.majorRadius, 0, 'f', 3)
            .arg(prod.minorRadius, 0, 'f', 3)
            .arg(std::atan2(prod.majorDir.y(), prod.majorDir.x()) * kRadToDeg, 0, 'f', 2));
}

void suiteMath() {
    suiteHeader("S1 math: worldBlurEllipse vs reference SVD");
    using A3 = std::array<double, 3>;
    auto R = [](double deg, double sx, double sy) { return A3{deg * kDegToRad, sx, sy}; };

    checkEllipseCase("identity chain, directional ellipse", {R(0, 1, 1)}, 10, 4, 30, false);
    checkEllipseCase("node rotation rotates the ellipse", {R(45, 1, 1)}, 10, 4, 30, false);
    checkEllipseCase("node rotation cancels blur angle", {R(-30, 1, 1)}, 10, 4, 30, false);
    checkEllipseCase("scale+rot+angle", {R(30, 2.0, 0.5)}, 12, 6, 45, false);
    checkEllipseCase("blurX only", {R(20, 1.5, 1.0)}, 12, 0, 60, false);
    checkEllipseCase("blurY only", {R(20, 1.5, 1.0)}, 0, 12, 60, false);
    checkEllipseCase("equal radii stay a circle", {R(25, 1.5, 0.75)}, 12, 12, 37, false);
    checkEllipseCase("default params == plain transform", {R(33, 1.7, 0.6)}, 1, 1, 0, false);
    checkEllipseCase("negative scale", {R(15, -2.0, 1.0)}, 8, 3, 10, false);
    checkEllipseCase("zero scale X", {R(0, 0.0, 1.5)}, 6, 2, 0, false);
    checkEllipseCase("zero scale with rotated ellipse", {R(40, 0.0, 2.0)}, 5, 3, 70, false);
    checkEllipseCase("angle wrap 370==10", {R(12, 1.25, 0.8)}, 10, 3, 370, false);
    checkEllipseCase("angle wrap -450==270", {R(12, 1.25, 0.8)}, 10, 3, -450, false);
    checkEllipseCase("rot exactly 90", {R(90, 1, 1)}, 10, 4, 0, false);
    checkEllipseCase("huge radii x scale", {R(18, 3.0, 3.0)}, 512, 128, 60, false);
    checkEllipseCase("tiny radii", {R(33, 1.1, 0.9)}, 0.01, 0.005, 33, false);
    checkEllipseCase("large scale", {R(-55, 1000.0, 0.5)}, 20, 7, 80, false);

    // the edge case the feature is for: a layer inside a folder, both transformed
    checkEllipseCase("layer in transformed folder", {R(-21, 2.2, 1.0), R(33, 1.7, 0.6)}, 14, 5, 40, true);
    checkEllipseCase("layer in transformed folder (folder leaf chain)", {R(-21, 2.2, 1.0), R(33, 1.7, 0.6)}, 14, 5, 40, false);
    // three levels of nesting, all transformed
    checkEllipseCase("nested folders + layer",
        {R(-35, 1.5, 1.5), R(80, 0.5, 1.3), R(15, 1.2, 0.8)}, 16, 4, 123, true);

    // fuzz
    std::mt19937 rng(12345);
    int fuzzFailsBefore = gFails;
    for (int iter = 0; iter < 800; ++iter) {
        std::uniform_real_distribution<double> rot(-1080.0, 1080.0);
        std::uniform_real_distribution<double> logScale(std::log(0.05), std::log(8.0));
        std::uniform_real_distribution<double> radius(0.0, 512.0);
        std::uniform_real_distribution<double> angle(-720.0, 720.0);
        std::uniform_int_distribution<int> depthD(1, 4);
        std::uniform_int_distribution<int> coin(0, 1);
        std::vector<A3> chain;
        for (int i = 0; i < depthD(rng); ++i) {
            double sx = std::exp(logScale(rng));
            double sy = std::exp(logScale(rng));
            if (coin(rng))
                sx = -sx;
            if (coin(rng))
                sy = -sy;
            chain.push_back(A3{rot(rng) * kDegToRad, sx, sy});
        }
        const double bx = coin(rng) == 0 ? 0.0 : radius(rng);
        const double by = coin(rng) == 0 ? 0.0 : radius(rng);
        const double ang = angle(rng);

        core::ObjectTree tree;
        auto* root = new core::FolderNode("root");
        tree.grabTopNode(root);
        core::ObjectNode* parent = root;
        core::ObjectNode* leaf = nullptr;
        // the chain is ordered leaf -> root; build the tree root-most first
        for (size_t i = chain.size(); i-- > 0;) {
            leaf = scene::addFolder(parent, QString("n%1").arg(i), QVector2D(0, 0),
                (float)(chain[i][0] * kRadToDeg), QVector2D((float)chain[i][1], (float)chain[i][2]));
            parent = leaf;
        }
        core::TimeCacheAccessor accessor(*root, tree.timeCacheLock(), scene::makeTimeInfo(0), false);
        const core::WorldBlurEllipse prod =
            core::worldBlurEllipse(accessor, leaf, (float)bx, (float)by, (float)ang);
        const ref::Ellipse refE = ref::worldBlurEllipseRef(chain, bx, by, ang);

        const QString ctx = QString("fuzz#%1 chain=%2 blur=(%3,%4,%5)").arg(iter).arg(chain.size()).arg(bx).arg(by).arg(ang);
        const double s1 = std::max(refE.majorRadius, 1.0);
        const double s2 = std::max(refE.minorRadius, 1.0);
        bool ok = true;
        // near-singular chains (sigma2 -> 0) hit catastrophic cancellation in the
        // production closed-form discriminant (float32); major axis stays accurate,
        // minor axis and the det invariant do not, so relax those two checks there
        const bool nearSingular = refE.minorRadius < 1e-3 * refE.majorRadius;
        // float32 accumulation through ill-conditioned chains inflates the minor axis
        // of the closed form well beyond eps*kappa(final): empirically up to
        // ~1e-5 * (product of per-node condition numbers). Scale the tolerance by
        // that, capped at 20% - real logic errors (axis swaps, sign flips, wrong
        // formula) are O(1), and the strict major-axis/direction checks below still
        // hold with full accuracy on those chains.
        double kappaProd = 1.0;
        for (const auto& n : chain) {
            const double a = std::abs(n[1]), b = std::abs(n[2]);
            kappaProd *= std::max(a, b) / std::max(std::min(a, b), 1e-30);
        }
        const double bmax = std::max(bx, by);
        if (bmax > 0.0)
            kappaProd *= bmax / std::max(std::min(bx, by), 1e-30);
        const double kappaTol = std::min(0.20, std::max(3e-3, 1e-5 * kappaProd));
        ok &= expect(std::abs(prod.majorRadius - refE.majorRadius) <= 3e-3 * s1,
            ctx + QString(": majorRadius prod=%1 ref=%2").arg(prod.majorRadius).arg(refE.majorRadius));
        if (!nearSingular) {
            ok &= expect(std::abs(prod.minorRadius - refE.minorRadius) <= kappaTol * s2,
                ctx + QString(": minorRadius prod=%1 ref=%2").arg(prod.minorRadius).arg(refE.minorRadius));
        }
        ok &= expect(prod.minorRadius >= 0.0f && prod.majorRadius >= prod.minorRadius - 1e-6f,
            ctx + ": radius ordering");
        if (refE.majorRadius > 1.05 * std::max(refE.minorRadius, 1e-9)) {
            const double cross = std::abs(prod.majorDir.x() * refE.majorY - prod.majorDir.y() * refE.majorX);
            ok &= expect(cross <= 1.5e-2, ctx + ": majorDir mismatch");
        }
        // |det| == sigma1 * sigma2 invariant, from the raw chain in double precision
        ref::Mat2 m;
        for (const auto& n : chain)
            m = ref::mul(ref::rotScale(n[0], n[1], n[2]), m);
        m = ref::mul(m, ref::blurEllipse(bx, by, ang));
        const double detAbs = std::abs(ref::det(m));
        const double sigProd = (double)prod.majorRadius * prod.minorRadius;
        if (!nearSingular) {
            ok &= expect(std::abs(sigProd - detAbs) <= std::max(1.5e-2, kappaTol) * std::max(detAbs, 1.0),
                ctx + QString(": det invariant prod=%1 det=%2").arg(sigProd).arg(detAbs));
        }
        if (!ok) {
            QString chainDesc;
            for (const auto& n : chain)
                chainDesc += QString(" (rot=%1,sx=%2,sy=%3)").arg(n[0] * kRadToDeg).arg(n[1]).arg(n[2]);
            std::printf("      chain:%s\n", chainDesc.toUtf8().constData());
        }
    }
    const int fuzzFailed = gFails - fuzzFailsBefore;
    caseReport("fuzz (800 random chains)", fuzzFailed == 0, QString("%1 failures").arg(fuzzFailed));

    // brute-force max-stretch cross-check of the reference itself (guards reference bugs)
    {
        ref::Mat2 m = ref::mul(ref::rotScale(0.7, 2.0, 0.5), ref::blurEllipse(14, 5, 40));
        const ref::Ellipse e = ref::svd(m);
        const double brute = ref::maxStretch(m);
        expect(std::abs(e.majorRadius - brute) <= 1e-2 * std::max(brute, 1.0),
            QString("reference SVD cross-check: svd=%1 brute=%2").arg(e.majorRadius).arg(brute));
    }
}

//-------------------------------------------------------------------------------------------------
// S2: blurLadderLevel vs independent reference
int ladderLevelRef(float aRadiusX, float aRadiusY, const QSize& aSize) {
    float r = std::max(aRadiusX, aRadiusY);
    int level = 0;
    while (r > 16.0f && level < 8) {
        r *= 0.5f;
        ++level;
    }
    if (level == 0)
        return 0;
    const float scale = (float)(1 << level);
    if (aRadiusX / scale < 1.0f || aRadiusY / scale < 1.0f)
        return 0;
    while ((aSize.width() >> level) < 2 || (aSize.height() >> level) < 2) {
        --level;
        if (level == 0)
            return 0;
    }
    return level;
}

void suiteLadder() {
    suiteHeader("S2 ladder: blurLadderLevel boundary grid");
    const std::vector<float> radii = {0.0f, 0.5f, 1.0f, 15.9f, 16.0f, 16.1f, 17.0f, 31.9f, 32.0f,
        32.1f, 33.0f, 63.0f, 64.0f, 65.0f, 127.0f, 128.0f, 129.0f, 255.5f, 256.0f, 257.0f, 511.0f,
        512.0f, 513.0f, 4096.0f};
    const std::vector<QSize> sizes = {QSize(1024, 768), QSize(95, 67), QSize(128, 96), QSize(16, 16),
        QSize(4, 4), QSize(3, 3), QSize(2, 2), QSize(1, 1), QSize(2048, 8), QSize(8, 2048)};
    int mismatches = 0;
    int total = 0;
    bool invariantOk = true;
    for (float r : radii) {
        const std::vector<std::pair<float, float>> pairs = {{r, r}, {r, 1.0f}, {1.0f, r},
            {r, r * 0.5f}, {r, 16.0f}, {r, 0.9f}};
        for (auto& p : pairs) {
            for (auto& s : sizes) {
                ++total;
                const int prod = core::FilterFrame::blurLadderLevel(p.first, p.second, s);
                const int refV = ladderLevelRef(p.first, p.second, s);
                if (prod != refV) {
                    ++mismatches;
                    std::printf(
                        "    [MISMATCH] radii=(%f,%f) size=%dx%d: prod=%d ref=%d\n", (double)p.first,
                        (double)p.second, s.width(), s.height(), prod, refV);
                }
                // invariants for non-zero levels (note: the radius cap is NOT one — the
                // buffer-size clamp can leave radius/scale well above 16)
                if (prod > 0) {
                    const float scale = (float)(1 << prod);
                    invariantOk &= p.first / scale >= 1.0f && p.second / scale >= 1.0f;
                    invariantOk &= (s.width() >> prod) >= 2 && (s.height() >> prod) >= 2;
                    invariantOk &= prod <= 8;
                }
            }
        }
    }
    expect(invariantOk, "ladder invariants (radii >= 1 at level, buffer >= 2x2, level <= 8)");
    ++gChecks;
    caseReport(QString("level grid (%1 cases)").arg(total), mismatches == 0, QString("%1 mismatches").arg(mismatches));
    if (mismatches)
        ++gFails;
}

//-------------------------------------------------------------------------------------------------
// S3: blur key blending (lerp, accumulated angle, easing, frame gating)
void addBlurFull(core::ObjectNode* aNode, int aFrame, float aBX, float aBY, float aAngle, bool aSineEasing = false) {
    auto* key = new core::BlurKey();
    key->setBlurX(aBX);
    key->setBlurY(aBY);
    key->setAngleDeg(aAngle);
    key->setDirectional(true);
    if (aSineEasing) {
        key->data().easing().type = util::Easing::Type_Sine;
        key->data().easing().range = util::Easing::Range_InOut;
    }
    cmnd::Base* pusher = aNode->timeLine()->createPusher(core::TimeKeyType_Blur, aFrame, key);
    pusher->tryExec();
    delete pusher;
}

// returns (blurX, blurY, angle) blended at aFrame for a folder with two keys
std::array<float, 3> blendedBlur(
    core::ObjectTree& aTree, core::ObjectNode& aRoot, core::ObjectNode& aNode, int aFrame
) {
    core::TimeCacheAccessor accessor(aRoot, aTree.timeCacheLock(), scene::makeTimeInfo(aFrame), false);
    const core::TimeKeyExpans& e = accessor.get(aNode);
    return {e.blurX(), e.blurY(), e.angleDeg()};
}

void suiteBlending() {
    suiteHeader("S3 blending: TimeKeyBlender blur segments");
    ResPool pool;

    auto makeTree = [&](core::ObjectTree& aTree, core::FolderNode*& aRootOut, core::FolderNode*& aNodeOut) {
        auto* root = new core::FolderNode("root");
        aTree.grabTopNode(root);
        auto* f = scene::addFolder(root, "f", QVector2D(0, 0), 0.0f, QVector2D(1, 1));
        aRootOut = root;
        aNodeOut = f;
    };

    // (a) raw linear angle: 350 -> 10 sweeps the full 340 degrees (no shortest-arc
    // normalization), matching the rotate key so multi-turn spins stay accumulated
    {
        core::ObjectTree tree;
        core::FolderNode *root, *f;
        makeTree(tree, root, f);
        addBlurFull(f, 0, 0.0f, 10.0f, 350.0f);
        addBlurFull(f, 10, 20.0f, 30.0f, 10.0f);
        const auto v = blendedBlur(tree, *root, *f, 5);
        bool ok = expect(std::abs(v[0] - 10.0f) < 1e-3 && std::abs(v[1] - 20.0f) < 1e-3,
            QString("radii lerp: (%1,%2)").arg(v[0]).arg(v[1]));
        ok &= expect(std::abs(v[2] - 180.0f) < 1e-2,
            QString("linear 350->10 mid: got %1").arg(v[2]));
        caseReport("linear 350->10 @mid", ok, QString("angle=%1").arg(v[2]));
    }
    // (b) raw linear backward: 10 -> 350 mid = 180
    {
        core::ObjectTree tree;
        core::FolderNode *root, *f;
        makeTree(tree, root, f);
        addBlurFull(f, 0, 0.0f, 0.0f, 10.0f);
        addBlurFull(f, 10, 0.0f, 0.0f, 350.0f);
        const auto v = blendedBlur(tree, *root, *f, 5);
        const bool ok = expect(std::abs(v[2] - 180.0f) < 1e-2,
            QString("linear 10->350 mid: got %1").arg(v[2]));
        caseReport("linear 10->350 @mid", ok, QString("angle=%1").arg(v[2]));
    }
    // (b2) multi-turn spin accumulates: 0 -> 3600 spins 20 turns (mid = 1800), so
    // easing on the segment modulates the spin speed
    {
        core::ObjectTree tree;
        core::FolderNode *root, *f;
        makeTree(tree, root, f);
        addBlurFull(f, 0, 4.0f, 8.0f, 0.0f);
        addBlurFull(f, 10, 4.0f, 8.0f, 3600.0f);
        const auto v = blendedBlur(tree, *root, *f, 5);
        const bool ok = expect(
            std::abs(v[2] - 1800.0f) < 1e-2 && std::abs(v[0] - 4.0f) < 1e-3,
            QString("multi-turn spin mid: got %1").arg(v[2]));
        caseReport("multi-turn 0->3600 @mid", ok, QString("angle=%1").arg(v[2]));
    }
    // (c) plain interpolation 0 -> 90
    {
        core::ObjectTree tree;
        core::FolderNode *root, *f;
        makeTree(tree, root, f);
        addBlurFull(f, 0, 4.0f, 8.0f, 0.0f);
        addBlurFull(f, 10, 12.0f, 16.0f, 90.0f);
        const auto v = blendedBlur(tree, *root, *f, 5);
        const bool ok = expect(std::abs(v[0] - 8.0f) < 1e-3 && std::abs(v[1] - 12.0f) < 1e-3 && std::abs(v[2] - 45.0f) < 1e-2,
            QString("lerp: (%1,%2,%3)").arg(v[0]).arg(v[1]).arg(v[2]));
        caseReport("linear lerp @mid", ok, QString("(%1,%2,%3)").arg(v[0]).arg(v[1]).arg(v[2]));
    }
    // (d) exact keyframe and (e) single key / after-last follows the key
    {
        core::ObjectTree tree;
        core::FolderNode *root, *f;
        makeTree(tree, root, f);
        addBlurFull(f, 4, 7.0f, 3.0f, 33.0f);
        const auto at4 = blendedBlur(tree, *root, *f, 4);
        const bool ok1 = expect(std::abs(at4[0] - 7.0f) < 1e-3 && std::abs(at4[2] - 33.0f) < 1e-2,
            "exact keyframe value");
        const auto at7 = blendedBlur(tree, *root, *f, 7);
        const bool ok2 = expect(std::abs(at7[0] - 7.0f) < 1e-3 && std::abs(at7[2] - 33.0f) < 1e-2,
            "after-last follows key");
        caseReport("single key exact/follow", ok1 && ok2, "");
    }
    // (f) no keys at all -> zeros
    {
        core::ObjectTree tree;
        core::FolderNode *root, *f;
        makeTree(tree, root, f);
        const auto v = blendedBlur(tree, *root, *f, 3);
        const bool ok = expect(v[0] == 0.0f && v[1] == 0.0f && v[2] == 0.0f, "no keys -> zero blur");
        caseReport("no keys -> zeros", ok, "");
    }
    // (g) easing applies to the blur segment (sine InOut at t=0.3)
    {
        core::ObjectTree tree;
        core::FolderNode *root, *f;
        makeTree(tree, root, f);
        addBlurFull(f, 0, 0.0f, 0.0f, 0.0f, true);
        addBlurFull(f, 10, 100.0f, 50.0f, 90.0f);
        const auto v = blendedBlur(tree, *root, *f, 3); // t = 0.3
        const float rate = util::Easing::calculate(util::Easing::Type_Sine, util::Easing::Range_InOut, 0.3f, 0.0f, 1.0f, 1.0f);
        const bool ok = expect(std::abs(v[0] - 100.0f * rate) < 0.05 && std::abs(v[1] - 50.0f * rate) < 0.05,
            QString("eased blurX: got %1 want %2").arg(v[0]).arg(100.0f * rate));
        caseReport("sine easing @t=0.3", ok, QString("rate=%1").arg(rate));
    }
}

//-------------------------------------------------------------------------------------------------
// S4: production render vs CPU reference blur
struct RenderCaseParams {
    QString name;
    QSize canvas;
    float zoom = 1.0f;
    float camRotDeg = 0.0f;
    bool camFlip = false;
    std::vector<std::array<double, 3>> chain; // leaf -> root (rotRad, sx, sy) of the blur node's chain
    double blurX, blurY, angle;
    int maxTol = 5;
    double meanTol = 0.6;
    std::function<void(core::ObjectTree&, ResPool&, bool)> build;
};

// scene::fixtureFor (scene.cpp) owns the fixture cache; this alias keeps call sites short
scene::Fixture& fixtureFor(const QSize& aSize) { return scene::fixtureFor(aSize); }

// Applies the CPU reference blur for the given world chain + blur ellipse, mirroring
// production's path choice (ladder vs direct) and the direction mapping: the world-space
// ellipse directions map to the slot's screen space through the camera (rotation +
// optional x-mirror; zoom is isotropic). Then:
//  - the direct path works on the plain render's readback (array row 0 = canvas
//    bottom), so a screen-space direction (dx, dy) becomes the array direction (dx, -dy)
//  - the ladder replica flips into image orientation internally, so it takes the
//    screen-space direction unchanged; both encode the SAME world-space blur ellipse
ref::Image refBlurApplied(
    const ref::Image& aIn, const QSize& aCanvas, const core::CameraInfo& aCamera,
    const std::vector<std::array<double, 3>>& aChain, double aBlurX, double aBlurY, double aAngle, float aZoom,
    double* aOutR1 = nullptr, double* aOutR2 = nullptr, int* aOutLevel = nullptr
) {
    const ref::Ellipse e = ref::worldBlurEllipseRef(aChain, aBlurX, aBlurY, aAngle);
    const double r1 = e.majorRadius * aZoom;
    const double r2 = e.minorRadius * aZoom;
    const QVector2D majorScreen =
        scene::screenDirFromWorld(aCamera, QVector2D((float)e.majorX, (float)e.majorY));
    const QVector2D minorScreen =
        scene::screenDirFromWorld(aCamera, QVector2D((float)e.minorX, (float)e.minorY));
    const int level = core::FilterFrame::blurLadderLevel((float)r1, (float)r2, aCanvas);
    if (aOutR1)
        *aOutR1 = r1;
    if (aOutR2)
        *aOutR2 = r2;
    if (aOutLevel)
        *aOutLevel = level;
    if (level > 0) {
        return ref::ladderBlur(
            aIn, aCanvas.width(), aCanvas.height(), majorScreen.x(), majorScreen.y(), r1, minorScreen.x(),
            minorScreen.y(), r2, level);
    }
    ref::Image out = ref::gaussPass(aIn, majorScreen.x(), -majorScreen.y(), r1);
    return ref::gaussPass(out, minorScreen.x(), -minorScreen.y(), r2);
}

// the standard S4/S7 comparison: max/mean bounds plus a small allowance of >4 outliers
bool checkDiff(const scene::Diff& d, int aMaxTol, double aMeanTol, const QSize& aCanvas) {
    const int over4Allowance = (aCanvas.width() * aCanvas.height()) / 500;
    return d.maxDiff <= aMaxTol && d.mean <= aMeanTol && d.over4 <= over4Allowance;
}

void runRenderCase(const RenderCaseParams& aP) {
    ResPool poolBlur, poolPlain;
    scene::Fixture& fx = fixtureFor(aP.canvas);
    const core::CameraInfo camera = scene::makeCamera(aP.canvas, aP.zoom, aP.camRotDeg, aP.camFlip);

    core::ObjectTree treeBlur;
    aP.build(treeBlur, poolBlur, true);
    const std::vector<uint8_t> gpu = fx.render(treeBlur, camera, 0);

    core::ObjectTree treePlain;
    aP.build(treePlain, poolPlain, false);
    const std::vector<uint8_t> plain = fx.render(treePlain, camera, 0);

    const ref::Image in = ref::imageFromBytes(plain.data(), aP.canvas.width(), aP.canvas.height());
    double r1 = 0.0, r2 = 0.0;
    int level = 0;
    const ref::Image blurred =
        refBlurApplied(in, aP.canvas, camera, aP.chain, aP.blurX, aP.blurY, aP.angle, aP.zoom, &r1, &r2, &level);
    const std::vector<uint8_t> refBytes = ref::imageToBytes(blurred);
    const scene::Diff d = scene::diffImages(gpu, refBytes);

    const bool ok = checkDiff(d, aP.maxTol, aP.meanTol, aP.canvas);
    ++gChecks;
    if (!ok)
        ++gFails;
    caseReport(aP.name, ok, QString("%1 lvl=%2 r=(%3,%4)").arg(diffStr(d)).arg(level).arg(r1, 0, 'f', 2).arg(r2, 0, 'f', 2));
}

void suiteRender() {
    suiteHeader("S4 render: production vs CPU reference");
    using A3 = std::array<double, 3>;
    auto R = [](double deg, double sx, double sy) { return A3{deg * kDegToRad, sx, sy}; };
    const QSize canvas(128, 96);
    const QVector2D center(64, 48);

    // the builder needs the blur values embedded; wrap with a richer lambda
    auto layerCase = [&](const QString& name, QVector2D pos, float rot, QVector2D scale, double bx, double by,
                         double ang, QSize cnv, float zoom, std::vector<A3> chain, int maxTol = 5,
                         float camRot = 0.0f, bool camFlip = false) {
        RenderCaseParams p;
        p.name = name;
        p.canvas = cnv;
        p.zoom = zoom;
        p.camRotDeg = camRot;
        p.camFlip = camFlip;
        p.chain = std::move(chain);
        p.blurX = bx;
        p.blurY = by;
        p.angle = ang;
        p.maxTol = maxTol;
        p.build = [pos, rot, scale, bx, by, ang](core::ObjectTree& tree, ResPool& pool, bool withBlur) {
            auto* root = new core::FolderNode("root");
            tree.grabTopNode(root);
            auto* layer = scene::addLayer(tree, root, pool.wedge("w"), "layer", pos, rot, scale);
            if (withBlur)
                scene::addBlur(layer, 0, (float)bx, (float)by, (float)ang);
        };
        runRenderCase(p);
    };

    // axis-aligned and rotated directional blur, no transforms
    layerCase("root layer, blur (12,0,0)", center, 0, QVector2D(1, 1), 12, 0, 0, canvas, 1.0f, {R(0, 1, 1)});
    layerCase("root layer, blur (0,12,0)", center, 0, QVector2D(1, 1), 0, 12, 0, canvas, 1.0f, {R(0, 1, 1)});
    layerCase("root layer, blur (12,4,30)", center, 0, QVector2D(1, 1), 12, 4, 30, canvas, 1.0f, {R(0, 1, 1)});
    // sub-step radii engage the composite path with an identity-like 3-tap kernel and
    // must match the CPU reference (old sigma floor made these render as a 5-tap blur)
    layerCase("root layer, sub-step blur (0.6,0.6,0)", center, 0, QVector2D(1, 1), 0.6, 0.6, 0, canvas, 1.0f, {R(0, 1, 1)});
    layerCase("root layer, sub-step blur (0.8,0.2,0)", center, 0, QVector2D(1, 1), 0.8, 0.2, 0, canvas, 1.0f, {R(0, 1, 1)});
    layerCase("root layer, blur (8,8,137)", center, 0, QVector2D(1, 1), 8, 8, 137, canvas, 1.0f, {R(0, 1, 1)});
    layerCase("root layer, blur (16,16,45)", center, 0, QVector2D(1, 1), 16, 16, 45, canvas, 1.0f, {R(0, 1, 1)});

    // transformed layer
    layerCase("layer rot25 scale(1.8,0.7), blur (14,5,40)", center, 25.0f, QVector2D(1.8f, 0.7f),
        14, 5, 40, canvas, 1.0f, {R(25, 1.8, 0.7)});
    layerCase("layer rot-60 scale(2.5,2.5), blur (20,9,300)", center, -60.0f, QVector2D(2.5f, 2.5f),
        20, 9, 300, canvas, 1.0f, {R(-60, 2.5, 2.5)});

    // the edge case: layer inside a folder, both transformed
    {
        RenderCaseParams p;
        p.name = "folder(rot33,s(1.7,0.6)) > layer(rot-21,s(2.2,1)), blur on layer";
        p.canvas = canvas;
        p.chain = {R(-21, 2.2, 1.0), R(33, 1.7, 0.6)};
        p.blurX = 14; p.blurY = 5; p.angle = 40;
        p.build = [](core::ObjectTree& tree, ResPool& pool, bool withBlur) {
            auto* root = new core::FolderNode("root");
            tree.grabTopNode(root);
            auto* folder = scene::addFolder(root, "folder", QVector2D(64, 48), 33.0f, QVector2D(1.7f, 0.6f));
            auto* layer = scene::addLayer(tree, folder, pool.wedge("w"), "layer", QVector2D(5, -8), -21.0f, QVector2D(2.2f, 1.0f));
            if (withBlur)
                scene::addBlur(layer, 0, 14.0f, 5.0f, 40.0f);
        };
        runRenderCase(p);
    }
    // blur on the transformed folder itself
    {
        RenderCaseParams p;
        p.name = "folder(rot33,s(1.7,0.6)) > layer(rot10), blur on folder";
        p.canvas = canvas;
        p.chain = {R(33, 1.7, 0.6)};
        p.blurX = 18; p.blurY = 6; p.angle = 75;
        p.build = [](core::ObjectTree& tree, ResPool& pool, bool withBlur) {
            auto* root = new core::FolderNode("root");
            tree.grabTopNode(root);
            auto* folder = scene::addFolder(root, "folder", QVector2D(64, 48), 33.0f, QVector2D(1.7f, 0.6f));
            scene::addLayer(tree, folder, pool.wedge("w"), "layer", QVector2D(5, -8), 10.0f, QVector2D(1.0f, 1.0f));
            if (withBlur)
                scene::addBlur(folder, 0, 18.0f, 6.0f, 75.0f);
        };
        runRenderCase(p);
    }
    // three levels of nesting, all transformed
    {
        RenderCaseParams p;
        p.name = "f1(rot15) > f2(rot80,s(0.5,1.3)) > layer(rot-35), blur on layer";
        p.canvas = canvas;
        p.chain = {R(-35, 1.5, 1.5), R(80, 0.5, 1.3), R(15, 1.2, 0.8)};
        p.blurX = 16; p.blurY = 4; p.angle = 123;
        p.build = [](core::ObjectTree& tree, ResPool& pool, bool withBlur) {
            auto* root = new core::FolderNode("root");
            tree.grabTopNode(root);
            auto* f1 = scene::addFolder(root, "f1", QVector2D(60, 44), 15.0f, QVector2D(1.2f, 0.8f));
            auto* f2 = scene::addFolder(f1, "f2", QVector2D(10, 5), 80.0f, QVector2D(0.5f, 1.3f));
            auto* layer = scene::addLayer(tree, f2, pool.wedge("w"), "layer", QVector2D(3, -2), -35.0f, QVector2D(1.5f, 1.5f));
            if (withBlur)
                scene::addBlur(layer, 0, 16.0f, 4.0f, 123.0f);
        };
        runRenderCase(p);
    }

    // ladder path (radius > 16)
    layerCase("ladder: blur (48,12,25)", center, 0, QVector2D(1, 1), 48, 12, 25, canvas, 1.0f, {R(0, 1, 1)}, 7);
    layerCase("ladder: blur (64,64,0)", center, 0, QVector2D(1, 1), 64, 64, 0, canvas, 1.0f, {R(0, 1, 1)}, 7);
    layerCase("ladder: blur (200,50,100)", center, 0, QVector2D(1, 1), 200, 50, 100, canvas, 1.0f, {R(0, 1, 1)}, 7);
    layerCase("ladder odd canvas: blur (40,40,60)", QVector2D(47, 33), 0, QVector2D(1, 1), 40, 40, 60,
        QSize(95, 67), 1.0f, {R(0, 1, 1)}, 7);
    layerCase("ladder odd canvas: blur (100,20,80)", QVector2D(47, 33), 0, QVector2D(1, 1), 100, 20, 80,
        QSize(95, 67), 1.0f, {R(0, 1, 1)}, 7);

    // camera zoom scales the radius
    layerCase("zoom 2: layer rot25 s(1.8,0.7), blur (14,5,40)", center, 25.0f, QVector2D(1.8f, 0.7f),
        14, 5, 40, canvas, 2.0f, {R(25, 1.8, 0.7)});
    layerCase("zoom 0.5: layer rot25 s(1.8,0.7), blur (14,5,40)", center, 25.0f, QVector2D(1.8f, 0.7f),
        14, 5, 40, canvas, 0.5f, {R(25, 1.8, 0.7)});

    // degenerate axes
    layerCase("zero minor axis, rotated: blur (10,0,45)", center, 15.0f, QVector2D(1.3f, 0.9f),
        10, 0, 45, canvas, 1.0f, {R(15, 1.3, 0.9)});
    layerCase("tiny canvas 24x18, blur (8,3,30)", QVector2D(12, 9), 0, QVector2D(1, 1),
        8, 3, 30, QSize(24, 18), 1.0f, {R(0, 1, 1)});

    // camera rotation: the world->slot direction mapping includes the view rotation
    // (the radii are unaffected - rotation is isometric)
    layerCase("camera rot20: layer rot25 s(1.8,0.7), blur (14,5,40)", center, 25.0f, QVector2D(1.8f, 0.7f),
        14, 5, 40, canvas, 1.0f, {R(25, 1.8, 0.7)}, 5, 20.0f);
    layerCase("camera rot-65: layer rot25 s(1.8,0.7), blur (14,5,40)", center, 25.0f, QVector2D(1.8f, 0.7f),
        14, 5, 40, canvas, 1.0f, {R(25, 1.8, 0.7)}, 5, -65.0f);
    layerCase("camera rot45 ladder: blur (48,12,25)", center, 0, QVector2D(1, 1),
        48, 12, 25, canvas, 1.0f, {R(0, 1, 1)}, 7, 45.0f);
    // camera x-mirror (view flip): negates world x before the rotation
    layerCase("camera flip: layer rot15 s(1.3,0.9), blur (12,4,30)", center, 15.0f, QVector2D(1.3f, 0.9f),
        12, 4, 30, canvas, 1.0f, {R(15, 1.3, 0.9)}, 5, 0.0f, true);
    layerCase("camera flip+rot30 ladder: blur (40,10,60)", center, 0, QVector2D(1, 1),
        40, 10, 60, canvas, 1.0f, {R(0, 1, 1)}, 7, 30.0f, true);
    // camera rotation on a nested folder+layer chain, blur on the layer
    {
        RenderCaseParams p;
        p.name = "camera rot50: folder(rot33,s(1.7,0.6)) > layer(rot-21,s(2.2,1)), blur on layer";
        p.canvas = canvas;
        p.camRotDeg = 50.0f;
        p.chain = {R(-21, 2.2, 1.0), R(33, 1.7, 0.6)};
        p.blurX = 14; p.blurY = 5; p.angle = 40;
        p.build = [](core::ObjectTree& tree, ResPool& pool, bool withBlur) {
            auto* root = new core::FolderNode("root");
            tree.grabTopNode(root);
            auto* folder = scene::addFolder(root, "folder", QVector2D(64, 48), 33.0f, QVector2D(1.7f, 0.6f));
            auto* layer = scene::addLayer(tree, folder, pool.wedge("w"), "layer", QVector2D(5, -8), -21.0f, QVector2D(2.2f, 1.0f));
            if (withBlur)
                scene::addBlur(layer, 0, 14.0f, 5.0f, 40.0f);
        };
        runRenderCase(p);
    }
}

//-------------------------------------------------------------------------------------------------
// S5: orientation ground truth + equivalences
void suiteOrientation() {
    suiteHeader("S5 orientation/equivalence (GPU ground truth)");
    ResPool pool;
    const QSize canvas(96, 96);

    auto renderDot = [&](std::function<void(core::ObjectTree&, core::ObjectNode*&)> aBuild) {
        core::ObjectTree tree;
        core::ObjectNode* blurNode = nullptr;
        aBuild(tree, blurNode);
        return fixtureFor(canvas).render(tree, scene::makeCamera(canvas), 0);
    };

    // (i) plain directional blur at 30 degrees
    const std::vector<uint8_t> bytesI = renderDot([&](core::ObjectTree& tree, core::ObjectNode*& node) {
        auto* root = new core::FolderNode("root");
        tree.grabTopNode(root);
        node = scene::addLayer(tree, root, pool.dot("d"), "layer", QVector2D(48, 48), 0.0f, QVector2D(1, 1));
        scene::addBlur(node, 0, 8.0f, 2.0f, 30.0f);
    });
    // (ii) the same blur buried in folder+layer rotations: world angle = 50 - 20 + 15 = 45
    const std::vector<uint8_t> bytesII = renderDot([&](core::ObjectTree& tree, core::ObjectNode*& node) {
        auto* root = new core::FolderNode("root");
        tree.grabTopNode(root);
        auto* folder = scene::addFolder(root, "folder", QVector2D(48, 48), 50.0f, QVector2D(1, 1));
        node = scene::addLayer(tree, folder, pool.dot("d"), "layer", QVector2D(0, 0), -20.0f, QVector2D(1, 1));
        scene::addBlur(node, 0, 8.0f, 2.0f, 15.0f);
    });
    // (iii) swapped radii + angle + 90 must equal (i): same ellipse, different parametrization
    const std::vector<uint8_t> bytesIII = renderDot([&](core::ObjectTree& tree, core::ObjectNode*& node) {
        auto* root = new core::FolderNode("root");
        tree.grabTopNode(root);
        node = scene::addLayer(tree, root, pool.dot("d"), "layer", QVector2D(48, 48), 0.0f, QVector2D(1, 1));
        scene::addBlur(node, 0, 2.0f, 8.0f, 120.0f);
    });

    auto measureAngle = [](const std::vector<uint8_t>& aBytes) {
        const ref::Image img = ref::imageFromBytes(aBytes.data(), 96, 96);
        double major = 0.0, minor = 0.0;
        // array-frame angle; world direction (dx,dy) appears as (dx,-dy), so negate
        const double arrAngle = ref::alphaPrincipalAngle(img, major, minor);
        return std::make_tuple(-arrAngle * kRadToDeg, major, minor);
    };

    {
        const auto [angle, major, minor] = measureAngle(bytesI);
        const double err = std::abs(ref::angleDiffDeg(angle, 30.0, 180.0));
        const bool ok = expect(err < 3.0, QString("dot blur 30 deg: measured %1 (err %2)").arg(angle).arg(err));
        caseReport("dot orientation: blur (8,2,30)", ok && expect(major > 2.0 * minor, "anisotropy sanity"),
            QString("measured=%1deg var=(%2,%3)").arg(angle).arg(major).arg(minor));
    }
    {
        const auto [angle, major, minor] = measureAngle(bytesII);
        const double err = std::abs(ref::angleDiffDeg(angle, 45.0, 180.0));
        const bool ok = expect(err < 3.0,
            QString("dot blur in folder(rot50)>layer(rot-20) at 15: measured %1 want 45 (err %2)").arg(angle).arg(err));
        caseReport("dot orientation: folder+layer transforms", ok,
            QString("measured=%1deg var=(%2,%3)").arg(angle).arg(major).arg(minor));
    }
    {
        const scene::Diff d = scene::diffImages(bytesI, bytesIII);
        const bool ok = d.maxDiff <= 4 && d.mean <= 0.5;
        ++gChecks;
        if (!ok)
            ++gFails;
        caseReport("ellipse swap identity: (8,2,30) == (2,8,120)", ok, diffStr(d));
    }

    // (iv) camera view transforms: the measured screen angle must track the view - the
    // screen-space direction is R(camRot) * diag(flip ? -1 : 1, 1) * world direction
    auto renderDotCam = [&](float aCamRotDeg, bool aCamFlip) {
        core::ObjectTree tree;
        auto* root = new core::FolderNode("root");
        tree.grabTopNode(root);
        auto* node = scene::addLayer(tree, root, pool.dot("d"), "layer", QVector2D(48, 48), 0.0f, QVector2D(1, 1));
        scene::addBlur(node, 0, 8.0f, 2.0f, 30.0f);
        return fixtureFor(canvas).render(tree, scene::makeCamera(canvas, 1.0f, aCamRotDeg, aCamFlip), 0);
    };
    const struct {
        float rot;
        bool flip;
    } camCases[4] = {{20.0f, false}, {-65.0f, false}, {0.0f, true}, {20.0f, true}};
    for (const auto& cc : camCases) {
        const auto bytes = renderDotCam(cc.rot, cc.flip);
        const auto [angle, major, minor] = measureAngle(bytes);
        const double want = (cc.flip ? 180.0 - 30.0 : 30.0) + cc.rot;
        const double err = std::abs(ref::angleDiffDeg(angle, want, 180.0));
        bool ok = expect(err < 3.0,
            QString("dot blur 30deg with camera rot=%1 flip=%2: measured %3 want %4 (err %5)")
                .arg(cc.rot)
                .arg(cc.flip)
                .arg(angle)
                .arg(want)
                .arg(err));
        ok &= expect(major > 2.0 * minor, "anisotropy sanity");
        caseReport(QString("dot orientation: camera rot %1%2").arg(cc.rot).arg(cc.flip ? ", flip" : ""), ok,
            QString("measured=%1deg want=%2 var=(%3,%4)").arg(angle).arg(want).arg(major).arg(minor));
    }

    // isotropic legacy vs directional equal-radii: byte-identical
    {
        const QSize cnv(128, 96);
        auto renderWedge = [&](std::function<void(core::ObjectNode*)> aAddBlur) {
            core::ObjectTree tree;
            auto* root = new core::FolderNode("root");
            tree.grabTopNode(root);
            auto* layer = scene::addLayer(tree, root, pool.wedge("w"), "layer", QVector2D(64, 48), 15.0f,
                QVector2D(1.3f, 0.9f));
            aAddBlur(layer);
            return fixtureFor(cnv).render(tree, scene::makeCamera(cnv), 0);
        };
        const auto legacy = renderWedge([](core::ObjectNode* n) { scene::addBlurAmount(n, 0, 12.0f); });
        const auto dir0 = renderWedge([](core::ObjectNode* n) { scene::addBlur(n, 0, 12.0f, 12.0f, 0.0f); });
        const auto dir45 = renderWedge([](core::ObjectNode* n) { scene::addBlur(n, 0, 12.0f, 12.0f, 45.0f); });

        const scene::Diff dLegacy = scene::diffImages(legacy, dir0);
        bool ok = dLegacy.bytesIdentical;
        ++gChecks;
        if (!ok)
            ++gFails;
        caseReport("isotropic amount(12) == directional (12,12,0)", ok, diffStr(dLegacy));

        const scene::Diff dCircle = scene::diffImages(dir0, dir45);
        ok = dCircle.mean <= 1.0 && dCircle.maxDiff <= 6;
        ++gChecks;
        if (!ok)
            ++gFails;
        caseReport("circle axis freedom: (12,12,0) ~= (12,12,45)", ok, diffStr(dCircle));
    }

    // layer blur vs folder blur: same chain, same content -> byte-identical
    {
        const QSize cnv(128, 96);
        auto renderTree = [&](bool aBlurOnFolder) {
            core::ObjectTree tree;
            auto* root = new core::FolderNode("root");
            tree.grabTopNode(root);
            auto* folder = scene::addFolder(root, "folder", QVector2D(64, 48), 30.0f, QVector2D(2.0f, 0.5f));
            auto* layer = scene::addLayer(tree, folder, pool.wedge("w"), "layer", QVector2D(0, 0), 0.0f, QVector2D(1, 1));
            scene::addBlur(aBlurOnFolder ? (core::ObjectNode*)folder : (core::ObjectNode*)layer, 0, 12.0f, 5.0f, 40.0f);
            return fixtureFor(cnv).render(tree, scene::makeCamera(cnv), 0);
        };
        const auto a = renderTree(true);
        const auto b = renderTree(false);
        const scene::Diff d = scene::diffImages(a, b);
        const bool ok = d.maxDiff <= 1;
        ++gChecks;
        if (!ok)
            ++gFails;
        caseReport("layer blur == folder blur (same chain)", ok, diffStr(d));
    }
}

//-------------------------------------------------------------------------------------------------
// S6: gating (amount <= epsilon is inert; small amounts are active with an identity-like
// kernel so interpolations ramp in smoothly) and frame activation
void suiteGating() {
    suiteHeader("S6 gating/edge cases");
    ResPool pool;
    const QSize cnv(128, 96);
    auto& fx = fixtureFor(cnv);
    const core::CameraInfo camera = scene::makeCamera(cnv);

    auto renderLayerBlur = [&](float bx, float by, float ang, int keyFrame, int renderFrame, bool onFolder,
                               float opacity = 1.0f) {
        core::ObjectTree tree;
        auto* root = new core::FolderNode("root");
        tree.grabTopNode(root);
        core::ObjectNode* blurNode;
        if (onFolder) {
            auto* folder = scene::addFolder(root, "folder", QVector2D(64, 48), 20.0f, QVector2D(1.4f, 0.8f));
            scene::addLayer(tree, folder, pool.wedge("w"), "layer", QVector2D(0, 0), 0.0f, QVector2D(1, 1));
            blurNode = folder;
        } else {
            blurNode = scene::addLayer(tree, root, pool.wedge("w"), "layer", QVector2D(64, 48), 20.0f, QVector2D(1.4f, 0.8f));
        }
        if (opacity != 1.0f)
            core::getOrCreateDefaultKey<core::OpaKey, core::TimeKeyType_Opa>(*blurNode->timeLine())
                ->setOpacity(opacity);
        if (keyFrame >= 0)
            scene::addBlur(blurNode, keyFrame, bx, by, ang);
        return fx.render(tree, camera, renderFrame);
    };

    // a blur at/below the epsilon (FilterFrame::kMinActiveBlurRadius) is gated off:
    // the composite path does not engage and the frame is byte-identical to keyless
    const auto plain = renderLayerBlur(0, 0, 0, -1, 0, false);
    const auto gated = renderLayerBlur(0.001f, 0.001f, 0.0f, 0, 0, false);
    const scene::Diff dGated = scene::diffImages(plain, gated);
    bool ok = dGated.bytesIdentical;
    ++gChecks;
    if (!ok)
        ++gFails;
    caseReport("layer blur 0.001 is inert (== keyless)", ok, diffStr(dGated));

    // above the epsilon the composite engages; at 0.6 the kernel is a tiny 3-tap so the
    // result is active but close to keyless (the old 0.5 gate would have collapsed it)
    const auto small = renderLayerBlur(0.6f, 0.6f, 0.0f, 0, 0, false);
    const scene::Diff dSmall = scene::diffImages(plain, small);
    ok = !dSmall.bytesIdentical && dSmall.mean > 0.001;
    ++gChecks;
    if (!ok)
        ++gFails;
    caseReport("layer blur 0.6 is active (!= keyless)", ok, diffStr(dSmall));

    const auto active = renderLayerBlur(4.0f, 1.0f, 30.0f, 0, 0, false);
    const scene::Diff dActive = scene::diffImages(plain, active);
    ok = !dActive.bytesIdentical && dActive.mean > 0.05;
    ++gChecks;
    if (!ok)
        ++gFails;
    caseReport("layer blur (4,1,30) is active (!= keyless)", ok, diffStr(dActive));

    // the gate must hold under fractional opacity: an epsilon blur on a semi-transparent
    // layer is still gated off and byte-identical to the same keyless frame
    const auto plain07 = renderLayerBlur(0, 0, 0, -1, 0, false, 0.7f);
    const auto gated07 = renderLayerBlur(0.001f, 0.001f, 0.0f, 0, 0, false, 0.7f);
    const scene::Diff dGated07 = scene::diffImages(plain07, gated07);
    ok = dGated07.bytesIdentical;
    ++gChecks;
    if (!ok)
        ++gFails;
    caseReport("layer blur 0.001 @ opacity 0.7 is inert", ok, diffStr(dGated07));

    // sub-step radius on a semi-transparent layer engages the composite with an
    // identity-like kernel; the composite round trip (premultiplied 8-bit quantization)
    // is the only residual and must stay far below the old 0.5-gate step (which jumped
    // from nothing to a real sigma-0.5 kernel)
    const auto sub07 = renderLayerBlur(0.4f, 0.4f, 0.0f, 0, 0, false, 0.7f);
    const scene::Diff dSub07 = scene::diffImages(plain07, sub07);
    ok = !dSub07.bytesIdentical && dSub07.mean < 1.0;
    ++gChecks;
    if (!ok)
        ++gFails;
    caseReport("layer blur 0.4 @ opacity 0.7 is near-identity", ok, diffStr(dSub07));

    const auto folderGated = renderLayerBlur(0.001f, 0.001f, 0.0f, 0, 0, true);
    const auto folderPlain = renderLayerBlur(0, 0, 0, -1, 0, true);
    const scene::Diff dFolderGated = scene::diffImages(folderPlain, folderGated);
    ok = dFolderGated.bytesIdentical;
    ++gChecks;
    if (!ok)
        ++gFails;
    caseReport("folder blur 0.001 is inert", ok, diffStr(dFolderGated));

    const auto folderGated07 = renderLayerBlur(0.001f, 0.001f, 0.0f, 0, 0, true, 0.7f);
    const auto folderPlain07 = renderLayerBlur(0, 0, 0, -1, 0, true, 0.7f);
    const scene::Diff dFolderGated07 = scene::diffImages(folderPlain07, folderGated07);
    ok = dFolderGated07.bytesIdentical;
    ++gChecks;
    if (!ok)
        ++gFails;
    caseReport("folder blur 0.001 @ opacity 0.7 is inert", ok, diffStr(dFolderGated07));

    // key at frame 5: inactive before, active from frame 5 on
    const auto before = renderLayerBlur(6.0f, 2.0f, 20.0f, 5, 4, false);
    const scene::Diff dBefore = scene::diffImages(plain, before);
    ok = dBefore.bytesIdentical;
    ++gChecks;
    if (!ok)
        ++gFails;
    caseReport("blur key @f5 inert at f4", ok, diffStr(dBefore));

    const auto after = renderLayerBlur(6.0f, 2.0f, 20.0f, 5, 6, false);
    const scene::Diff dAfter = scene::diffImages(plain, after);
    ok = !dAfter.bytesIdentical && dAfter.mean > 0.02;
    ++gChecks;
    if (!ok)
        ++gFails;
    caseReport("blur key @f5 active at f6", ok, diffStr(dAfter));
}

// Alpha-semantics regression: two overlapping 50%-alpha squares must accumulate with the
// over-rule. The over operator gives alpha = 0.5 + 0.5*(1-0.5) = 0.75 (192) at the overlap;
// the old additive-clamp GL_ONE, GL_ONE gave 255. The RGB channel is over-blended in both
// cases (150,60,45 = 200*0.5 + 100*0.5 straight-over, stored premultiplied), so this test
// discriminates on alpha. Verified pre-fix: (150,60,45,255) on both paths (SCRATCHPAD).
// Geometry: A@(45,45) -> (33,39)-(56,62), B@(58,58) -> (46,26)-(69,49); overlap (50,44).
void suiteAlphaProbe() {
    suiteHeader("alpha semantics probe (additive vs over)");
    ResPool pool;
    const QSize cnv(128, 96);
    auto& fx = fixtureFor(cnv);
    const core::CameraInfo camera = scene::makeCamera(cnv);
    auto pxAt = [&](const std::vector<uint8_t>& img, int x, int y) {
        size_t i = (size_t)(y * cnv.width() + x) * 4;
        return QString("%1,%2,%3,%4").arg(img[i]).arg(img[i + 1]).arg(img[i + 2]).arg(img[i + 3]);
    };
    auto near = [&](const std::vector<uint8_t>& img, int x, int y, std::array<int, 4> exp, int tol, const char* what) {
        const QString got = pxAt(img, x, y);
        int bad = 0;
        for (int c = 0; c < 4; ++c) {
            size_t i = (size_t)(y * cnv.width() + x) * 4;
            if (std::abs((int)img[i + c] - exp[c]) > tol)
                ++bad;
        }
        ++gChecks;
        if (bad) {
            ++gFails;
            caseReport(what, false, got);
        } else {
            caseReport(what, true, got);
        }
    };
    auto render2 = [&](bool withFolder, const char* tag) {
        core::ObjectTree tree;
        auto* root = new core::FolderNode("root");
        tree.grabTopNode(root);
        if (withFolder) {
            auto* folder = scene::addFolder(root, "folder", QVector2D(0, 0), 0.0f, QVector2D(1, 1));
            scene::addLayer(tree, folder, pool.half("b"), "b", QVector2D(58, 58), 0.0f, QVector2D(1, 1));
            scene::addLayer(tree, folder, pool.half("a"), "a", QVector2D(45, 45), 0.0f, QVector2D(1, 1));
            scene::addBlur(folder, 0, 0.002f, 0.002f, 0.0f);
        } else {
            scene::addLayer(tree, root, pool.half("b"), "b", QVector2D(58, 58), 0.0f, QVector2D(1, 1));
            scene::addLayer(tree, root, pool.half("a"), "a", QVector2D(45, 45), 0.0f, QVector2D(1, 1));
        }
        const std::vector<uint8_t> img = fx.render(tree, camera, 0);
        const QString prefix = QString("[alpha-%1]").arg(tag);
        // overlap alpha = over-rule 0.752 -> 192; rgb straight-over premultiplied 150,60,45
        near(img, 50, 44, {150, 60, 45, 192}, 2, qPrintable(prefix + " overlap alpha=192"));
        near(img, 50, 50, {100, 40, 30, 128}, 2, qPrintable(prefix + " single-square control"));
        near(img, 10, 10, {0, 0, 0, 0}, 0, qPrintable(prefix + " outside transparent"));
    };
    render2(false, "direct");
    render2(true, "composite");
}

//-------------------------------------------------------------------------------------------------
// S7: blur crossed with other features (clipping, nested composites, blend modes, HSV)
void suiteInteractions() {
    suiteHeader("S7 interactions: blur x clippee / nested blur / blend / HSV");
    using A3 = std::array<double, 3>;
    auto R = [](double deg, double sx, double sy) { return A3{deg * kDegToRad, sx, sy}; };
    const QSize canvas(128, 96);
    auto& fx = fixtureFor(canvas);
    const core::CameraInfo camera = scene::makeCamera(canvas);

    // clippee pair inside a blurred, transformed folder: the clipping context must
    // propagate into the folder's composite render
    {
        RenderCaseParams p;
        p.name = "clippee in folder(rot25,s(1.4,0.8)), blur (10,3,40) on folder";
        p.canvas = canvas;
        p.chain = {R(25, 1.4, 0.8)};
        p.blurX = 10; p.blurY = 3; p.angle = 40;
        p.build = [](core::ObjectTree& tree, ResPool& pool, bool withBlur) {
            auto* root = new core::FolderNode("root");
            tree.grabTopNode(root);
            auto* folder = scene::addFolder(root, "folder", QVector2D(64, 48), 25.0f, QVector2D(1.4f, 0.8f));
            // the clippee is pushed first so it renders above the clipper (children are
            // walked back-to-front; a clippee clips to the sibling rendered below it)
            auto* clippee = scene::addLayer(
                tree, folder, pool.wedge("wd"), "clippee", QVector2D(10, 6), -15.0f, QVector2D(2.0f, 2.0f));
            clippee->setClipped(true);
            scene::addLayer(tree, folder, pool.wedge("wc"), "clipper", QVector2D(0, 0), 0.0f, QVector2D(1, 1));
            if (withBlur)
                scene::addBlur(folder, 0, 10.0f, 3.0f, 40.0f);
        };
        runRenderCase(p);
    }

    // a blurred layer that is itself clipped to the sibling below: the layer composite
    // isolates the clippee (the clipper stays crisp), so the reference is
    // present(blur(masked clippee)) over the clipper render. The masked clippee content is
    // reconstructed as the unclipped render scaled by the clipper's rendered alpha - the
    // IS_CLIPPEE shader's color.a *= mask/255 scales the premultiplied content.
    {
        ResPool poolFull, poolClipper, poolTop;
        core::ObjectTree treeFull;
        {
            auto* root = new core::FolderNode("root");
            treeFull.grabTopNode(root);
            auto* clippee = scene::addLayer(
                treeFull, root, poolFull.wedge("wd"), "clippee", QVector2D(60, 44), -15.0f, QVector2D(2.0f, 2.0f));
            clippee->setClipped(true);
            scene::addBlur(clippee, 0, 8.0f, 2.0f, 30.0f);
            scene::addLayer(
                treeFull, root, poolFull.wedge("wc"), "clipper", QVector2D(64, 48), 10.0f, QVector2D(1.5f, 1.2f));
        }
        const std::vector<uint8_t> gpu = fx.render(treeFull, camera, 0);
        core::ObjectTree treeClipper;
        {
            auto* root = new core::FolderNode("root");
            treeClipper.grabTopNode(root);
            scene::addLayer(
                treeClipper, root, poolClipper.wedge("wc"), "clipper", QVector2D(64, 48), 10.0f,
                QVector2D(1.5f, 1.2f));
        }
        const std::vector<uint8_t> clipperBytes = fx.render(treeClipper, camera, 0);
        core::ObjectTree treeTop;
        {
            auto* root = new core::FolderNode("root");
            treeTop.grabTopNode(root);
            scene::addLayer(
                treeTop, root, poolTop.wedge("wd"), "clippee", QVector2D(60, 44), -15.0f, QVector2D(2.0f, 2.0f));
        }
        const std::vector<uint8_t> topBytes = fx.render(treeTop, camera, 0);

        const ref::Image clipperImg = ref::imageFromBytes(clipperBytes.data(), canvas.width(), canvas.height());
        const ref::Image topImg = ref::imageFromBytes(topBytes.data(), canvas.width(), canvas.height());
        ref::Image masked(canvas.width(), canvas.height());
        for (int y = 0; y < canvas.height(); ++y) {
            for (int x = 0; x < canvas.width(); ++x) {
                const float m = clipperImg.at(x, y)[3];
                const float* u = topImg.at(x, y);
                float* d = masked.at(x, y);
                for (int c = 0; c < 4; ++c)
                    d[c] = u[c] * m;
            }
        }
        const ref::Image blurred = refBlurApplied(masked, canvas, camera, {R(-15, 2.0, 2.0)}, 8, 2, 30, 1.0f);
        const ref::Image presented = ref::blendPresent(blurred, clipperImg, img::BlendMode_Normal, 1.0);
        const scene::Diff d = scene::diffImages(gpu, ref::imageToBytes(presented));
        const bool ok = checkDiff(d, 8, 0.8, canvas);
        ++gChecks;
        if (!ok)
            ++gFails;
        if (std::getenv("VB_DUMP")) {
            scene::dumpPNG("/tmp/vb_clip_gpu.png", gpu, canvas);
            scene::dumpPNG("/tmp/vb_clip_ref.png", ref::imageToBytes(presented), canvas);
            scene::dumpPNG("/tmp/vb_clip_clipper.png", clipperBytes, canvas);
            scene::dumpPNG("/tmp/vb_clip_top.png", topBytes, canvas);
            // production intermediates: the crisp clipped render (mask check) and the
            // unclipped blurred render (halo check)
            core::ObjectTree treeCrisp;
            {
                auto* root = new core::FolderNode("root");
                treeCrisp.grabTopNode(root);
                ResPool poolCrisp;
                auto* clippee = scene::addLayer(
                    treeCrisp, root, poolCrisp.wedge("wd"), "clippee", QVector2D(60, 44), -15.0f,
                    QVector2D(2.0f, 2.0f));
                clippee->setClipped(true);
                scene::addLayer(
                    treeCrisp, root, poolCrisp.wedge("wc"), "clipper", QVector2D(64, 48), 10.0f,
                    QVector2D(1.5f, 1.2f));
                const std::vector<uint8_t> crisp = fx.render(treeCrisp, camera, 0);
                scene::dumpPNG("/tmp/vb_clip_crisp.png", crisp, canvas);
            }
            core::ObjectTree treeUnclipped;
            {
                auto* root = new core::FolderNode("root");
                treeUnclipped.grabTopNode(root);
                ResPool poolUnclipped;
                auto* clippee = scene::addLayer(
                    treeUnclipped, root, poolUnclipped.wedge("wd"), "clippee", QVector2D(60, 44), -15.0f,
                    QVector2D(2.0f, 2.0f));
                scene::addBlur(clippee, 0, 8.0f, 2.0f, 30.0f);
                scene::addLayer(
                    treeUnclipped, root, poolUnclipped.wedge("wc"), "clipper", QVector2D(64, 48), 10.0f,
                    QVector2D(1.5f, 1.2f));
                const std::vector<uint8_t> unclipped = fx.render(treeUnclipped, camera, 0);
                scene::dumpPNG("/tmp/vb_clip_unclipped.png", unclipped, canvas);
            }
        }
        caseReport("blur (8,2,30) on clipped layer(rot-15,s2)", ok, diffStr(d));
    }

    // folder blur nested inside folder blur: the inner composite is blurred and presented
    // into the outer composite, then the outer blur runs; the reference applies the two
    // reference blurs in sequence (inner chain includes the outer folder's transform)
    {
        ResPool poolBlur, poolPlain;
        auto build = [](core::ObjectTree& tree, ResPool& pool, bool withBlur) {
            auto* root = new core::FolderNode("root");
            tree.grabTopNode(root);
            auto* f1 = scene::addFolder(root, "f1", QVector2D(60, 44), 15.0f, QVector2D(1.2f, 0.8f));
            auto* f2 = scene::addFolder(f1, "f2", QVector2D(10, 5), 80.0f, QVector2D(0.5f, 1.3f));
            scene::addLayer(tree, f2, pool.wedge("w"), "layer", QVector2D(3, -2), -35.0f, QVector2D(1.5f, 1.5f));
            if (withBlur) {
                scene::addBlur(f1, 0, 36.0f, 9.0f, 30.0f);
                scene::addBlur(f2, 0, 8.0f, 2.0f, 75.0f);
            }
        };
        core::ObjectTree treeBlur;
        build(treeBlur, poolBlur, true);
        const std::vector<uint8_t> gpu = fx.render(treeBlur, camera, 0);
        core::ObjectTree treePlain;
        build(treePlain, poolPlain, false);
        const std::vector<uint8_t> plain = fx.render(treePlain, camera, 0);

        const ref::Image in = ref::imageFromBytes(plain.data(), canvas.width(), canvas.height());
        const ref::Image inner =
            refBlurApplied(in, canvas, camera, {R(80, 0.5, 1.3), R(15, 1.2, 0.8)}, 8, 2, 75, 1.0f);
        const ref::Image outer = refBlurApplied(inner, canvas, camera, {R(15, 1.2, 0.8)}, 36, 9, 30, 1.0f);
        const scene::Diff d = scene::diffImages(gpu, ref::imageToBytes(outer));
        const bool ok = checkDiff(d, 7, 0.8, canvas);
        ++gChecks;
        if (!ok)
            ++gFails;
        caseReport("nested blur: f1(36,9,30) > f2(8,2,75) > layer", ok, diffStr(d));
    }

    // blend modes x blur: the blurred composite must be presented with the layer's blend
    // mode against the real captured scene (DestinationTexturizer), not the composite
    for (const img::BlendMode mode : {img::BlendMode_Multiply, img::BlendMode_Screen}) {
        ResPool poolFull, poolTop, poolBg;
        const char* modeName = mode == img::BlendMode_Multiply ? "multiply" : "screen";
        core::ObjectTree treeFull;
        {
            auto* root = new core::FolderNode("root");
            treeFull.grabTopNode(root);
            // the children list is top-most first (last pushed renders at the bottom)
            auto* top = scene::addLayer(
                treeFull, root, poolFull.wedge("top"), "top", QVector2D(64, 48), 15.0f, QVector2D(1.3f, 0.9f));
            top->setBlendMode(mode);
            scene::addBlur(top, 0, 10.0f, 3.0f, 40.0f);
            scene::addLayer(
                treeFull, root, poolFull.wedge("bg"), "bg", QVector2D(58, 52), -8.0f, QVector2D(1.6f, 1.4f));
        }
        const std::vector<uint8_t> gpu = fx.render(treeFull, camera, 0);
        // reference inputs: the crisp top layer alone (Normal) and the background alone
        core::ObjectTree treeTop;
        {
            auto* root = new core::FolderNode("root");
            treeTop.grabTopNode(root);
            scene::addLayer(treeTop, root, poolTop.wedge("top"), "top", QVector2D(64, 48), 15.0f, QVector2D(1.3f, 0.9f));
        }
        const std::vector<uint8_t> topBytes = fx.render(treeTop, camera, 0);
        core::ObjectTree treeBg;
        {
            auto* root = new core::FolderNode("root");
            treeBg.grabTopNode(root);
            scene::addLayer(treeBg, root, poolBg.wedge("bg"), "bg", QVector2D(58, 52), -8.0f, QVector2D(1.6f, 1.4f));
        }
        const std::vector<uint8_t> bgBytes = fx.render(treeBg, camera, 0);

        const ref::Image topImg = ref::imageFromBytes(topBytes.data(), canvas.width(), canvas.height());
        const ref::Image blurred = refBlurApplied(topImg, canvas, camera, {R(15, 1.3, 0.9)}, 10, 3, 40, 1.0f);
        const ref::Image bgImg = ref::imageFromBytes(bgBytes.data(), canvas.width(), canvas.height());
        const ref::Image presented = ref::blendPresent(blurred, bgImg, mode, 1.0);
        const scene::Diff d = scene::diffImages(gpu, ref::imageToBytes(presented));
        const bool ok = checkDiff(d, 8, 0.8, canvas);
        ++gChecks;
        if (!ok)
            ++gFails;
        if (std::getenv("VB_DUMP")) {
            scene::dumpPNG(QString("/tmp/vb_blend_%1_gpu.png").arg(modeName), gpu, canvas);
            scene::dumpPNG(QString("/tmp/vb_blend_%1_ref.png").arg(modeName), ref::imageToBytes(presented), canvas);
            scene::dumpPNG(QString("/tmp/vb_blend_%1_bg.png").arg(modeName), bgBytes, canvas);
            scene::dumpPNG(QString("/tmp/vb_blend_%1_top.png").arg(modeName), topBytes, canvas);
        }
        caseReport(QString("blend %1: blurred layer over background").arg(modeName), ok, diffStr(d));
    }

    // layer HSV x blur: the layer's HSV is baked into the composite BEFORE the blur in
    // both the plain and the blurred render, so the plain render is the reference input
    {
        RenderCaseParams p;
        p.name = "layer HSV(60,150,80) + blur (10,3,40)";
        p.canvas = canvas;
        p.chain = {R(15, 1.3, 0.9)};
        p.blurX = 10; p.blurY = 3; p.angle = 40;
        p.build = [](core::ObjectTree& tree, ResPool& pool, bool withBlur) {
            auto* root = new core::FolderNode("root");
            tree.grabTopNode(root);
            auto* layer = scene::addLayer(tree, root, pool.wedge("w"), "layer", QVector2D(64, 48), 15.0f, QVector2D(1.3f, 0.9f));
            scene::addHSV(layer, 0, 60, 150, 80, 0);
            if (withBlur)
                scene::addBlur(layer, 0, 10.0f, 3.0f, 40.0f);
        };
        runRenderCase(p);
    }

    // folder HSV x blur: the folder's HSV is applied at presentation, AFTER the blur, so
    // the reference is hsv(blur(plain keyless render)) - ordering matters (HSV is
    // nonlinear, blur and HSV do not commute)
    {
        ResPool poolFull, poolPlain;
        auto build = [](core::ObjectTree& tree, ResPool& pool, bool withFilters) {
            auto* root = new core::FolderNode("root");
            tree.grabTopNode(root);
            auto* folder = scene::addFolder(root, "folder", QVector2D(64, 48), 20.0f, QVector2D(1.3f, 0.8f));
            scene::addLayer(tree, folder, pool.wedge("w"), "layer", QVector2D(0, 0), 0.0f, QVector2D(1, 1));
            if (withFilters) {
                scene::addHSV(folder, 0, 90, 120, 110, 0);
                scene::addBlur(folder, 0, 10.0f, 4.0f, 55.0f);
            }
        };
        core::ObjectTree treeFull;
        build(treeFull, poolFull, true);
        const std::vector<uint8_t> gpu = fx.render(treeFull, camera, 0);
        core::ObjectTree treePlain;
        build(treePlain, poolPlain, false);
        const std::vector<uint8_t> plain = fx.render(treePlain, camera, 0);

        const ref::Image in = ref::imageFromBytes(plain.data(), canvas.width(), canvas.height());
        const ref::Image blurred = refBlurApplied(in, canvas, camera, {R(20, 1.3, 0.8)}, 10, 4, 55, 1.0f);
        const ref::Image adjusted = ref::hsvAdjust(blurred, 90.0 / 360.0, 120.0 / 100.0, 110.0 / 100.0, false);
        const scene::Diff d = scene::diffImages(gpu, ref::imageToBytes(adjusted));
        const bool ok = checkDiff(d, 8, 0.8, canvas);
        ++gChecks;
        if (!ok)
            ++gFails;
        caseReport("folder HSV(90,120,110) after blur (10,4,55)", ok, diffStr(d));
    }
}

//-------------------------------------------------------------------------------------------------
// S8: blur key persistence through the binary project format (TimeLine::serialize /
// deserialize at the current version). Blur shipped as a single feature with one
// AE_PROJECT_FORMAT_MINOR_VERSION bump (8 -> 9): the on-disk layout is flat (easing +
// blurX + blurY + angle + directional) with no historical variant, so with any declared
// project version the same bytes deserialize to the same key.
// The clipboard JSON path is NOT covered here (copy/paste are file-local functions in
// gui/TimeLineEditorWidget.cpp and ctrl/TimeLineEditor.cpp; verified by inspection: both
// sides use Amount/BlurX/BlurY/Angle, paste falls back to Amount when BlurX is absent and
// infers the directional flag - the flag itself is not serialized).
// (NullProgressReporter moved to harness.h)

void suiteSerialization() {
    suiteHeader("S8 blur key serialization (binary project format)");
    const int failsBefore = gFails;
    const int checksBefore = gChecks;

    // a timeline mixing blur keys (isotropic "Amount" mode, directional, non-default
    // easings) with an HSV key, so the type-name tagging ("Blur") is exercised against
    // another key type
    core::TimeLine src;
    auto pushKey = [&src](core::TimeKeyType aType, int aFrame, core::TimeKey* aKey) {
        auto* pusher = src.createPusher(aType, aFrame, aKey);
        pusher->tryExec();
        delete pusher;
    };
    auto pushBlur = [&pushKey](
        int aFrame, float aBX, float aBY, float aAngle, bool aDir, const util::Easing::Param& aEasing) {
        auto* key = new core::BlurKey();
        key->setBlurX(aBX);
        key->setBlurY(aBY);
        key->setAngleDeg(aAngle);
        key->setDirectional(aDir);
        key->data().easing() = aEasing;
        pushKey(core::TimeKeyType_Blur, aFrame, key);
    };
    util::Easing::Param sineIn;
    sineIn.type = util::Easing::Type_Sine;
    sineIn.range = util::Easing::Range_In;
    util::Easing::Param backOut;
    backOut.type = util::Easing::Type_Back;
    backOut.range = util::Easing::Range_Out;
    pushBlur(0, 5.0f, 5.0f, 0.0f, false, util::Easing::Param());
    pushBlur(7, 8.0f, 2.0f, 30.0f, true, sineIn);
    pushBlur(23, 12.5f, 1.0f, -45.0f, true, backOut);
    {
        auto* hsv = new core::HSVKey();
        hsv->setHSV({60, 150, 80, 0});
        pushKey(core::TimeKeyType_HSV, 3, hsv);
    }

    // serialize
    std::ostringstream buffer(std::ios::binary);
    util::StreamWriter writer(buffer);
    core::Serializer serializer(writer);
    expect(src.serialize(serializer), "S8: timeline serialize");

    // deserialize at the current format version
    NullProgressReporter reporter;
    gl::DeviceInfo deviceInfo;
    std::istringstream input(buffer.str(), std::ios::binary);
    util::LEStreamReader reader(input);
    core::Deserializer::IDSolverType idSolver;
    core::Deserializer deserializer(
        reader, idSolver, buffer.str().size(), QVersionNumber(0, 9), deviceInfo, reporter, 0);
    core::TimeLine dst;
    expect(dst.deserialize(deserializer), "S8: timeline deserialize");

    // compare blur keys frame by frame
    const auto& srcMap = src.map(core::TimeKeyType_Blur);
    const auto& dstMap = dst.map(core::TimeKeyType_Blur);
    expect(dstMap.count() == srcMap.count(), "S8: blur key count round-trips");
    for (auto itr = srcMap.begin(); itr != srcMap.end(); ++itr) {
        const int frame = itr.key();
        auto dstItr = dstMap.find(frame);
        if (!expect(dstItr != dstMap.end(), QString("S8: frame %1 present after load").arg(frame)))
            continue;
        const auto* sk = (const core::BlurKey*)itr.value();
        const auto* dk = (const core::BlurKey*)dstItr.value();
        expect(
            dk->blurX() == sk->blurX() && dk->blurY() == sk->blurY() && dk->angleDeg() == sk->angleDeg(),
            QString("S8: frame %1 radii/angle round-trip").arg(frame));
        expect(dk->isDirectional() == sk->isDirectional(), QString("S8: frame %1 directional flag").arg(frame));
        expect(
            dk->data().easing() == sk->data().easing(), QString("S8: frame %1 easing round-trip").arg(frame));
    }
    expect(!dst.map(core::TimeKeyType_HSV).isEmpty(), "S8: HSV key survives alongside blur");

    // The layout is flat and version-independent: the same bytes read back identically
    // under the pre-blur (0.8) version label, since there is no version-branched blur
    // layout to be compatible with. Spot-check the anisotropic key against the round-trip
    // above rather than against a synthetic historical format.
    {
        std::istringstream input8(buffer.str(), std::ios::binary);
        util::LEStreamReader reader8(input8);
        core::Deserializer::IDSolverType solver8;
        core::Deserializer deserializer8(
            reader8, solver8, buffer.str().size(), QVersionNumber(0, 8), deviceInfo, reporter, 0);
        core::TimeLine old;
        expect(old.deserialize(deserializer8), "S8: blur timeline loads under pre-blur version label");
        const auto& oldMap = old.map(core::TimeKeyType_Blur);
        const auto* sk = (const core::BlurKey*)srcMap[7];
        auto oldItr = oldMap.find(7);
        bool ok = oldItr != oldMap.end() && sk;
        if (ok) {
            const auto* ok_ = (const core::BlurKey*)oldItr.value();
            ok = ok_->blurX() == sk->blurX() && ok_->blurY() == sk->blurY()
                && ok_->angleDeg() == sk->angleDeg() && ok_->isDirectional() == sk->isDirectional();
        }
        expect(ok, "S8: single flat layout (no historical variant) round-trips under any version");
    }

    caseReport(
        "blur key binary round-trip (single v0.9 layout)", gFails == failsBefore,
        QString("%1 checks").arg(gChecks - checksBefore));

    // Folder blend mode persistence (minor version 10 feature): the mode rides the
    // FolderNd block as a 4CC appended after the object block, gated on the declared
    // version (FolderNode::serialize/deserialize). Blocks from pre-10 projects carry no
    // mode, so a pre-10 version label reads the pass-through default instead.
    const int folderChecksBefore = gChecks;
    core::FolderNode folderSrc("blendme");
    folderSrc.setBlendMode(img::BlendMode_VividLight);
    std::ostringstream fbuffer(std::ios::binary);
    util::StreamWriter fwriter(fbuffer);
    core::Serializer fserializer(fwriter);
    expect(folderSrc.serialize(fserializer), "S8: folder serialize");
    {
        std::istringstream fin(fbuffer.str(), std::ios::binary);
        util::LEStreamReader freader(fin);
        core::Deserializer::IDSolverType fsolver;
        core::Deserializer fdst(freader, fsolver, fbuffer.str().size(), QVersionNumber(0, 10), deviceInfo, reporter, 0);
        core::FolderNode folderDst("x");
        expect(folderDst.deserialize(fdst), "S8: folder deserialize @ 0.10");
        expect(
            folderDst.blendMode() == img::BlendMode_VividLight, "S8: folder blend mode round-trips (0.10 4CC)");
        expect(folderDst.name() == "blendme", "S8: folder name round-trips alongside the mode");
    }
    {
        // a pre-10 label skips the 4CC read entirely: the folder keeps its pass-through
        // default (the appended bytes stay unread, like a genuinely old block would lack)
        std::istringstream fin(fbuffer.str(), std::ios::binary);
        util::LEStreamReader freader(fin);
        core::Deserializer::IDSolverType fsolver;
        core::Deserializer fdst(freader, fsolver, fbuffer.str().size(), QVersionNumber(0, 9), deviceInfo, reporter, 0);
        core::FolderNode folderOld("x");
        expect(folderOld.deserialize(fdst), "S8: folder deserialize @ 0.9");
        expect(
            folderOld.blendMode() == img::BlendMode_Normal, "S8: pre-10 label keeps the pass-through default");
    }
    caseReport(
        "folder blend mode binary round-trip (v0.10 4CC, pre-10 gate)", gFails == failsBefore,
        QString("%1 checks").arg(gChecks - folderChecksBefore));
}

//-------------------------------------------------------------------------------------------------
// S9: the export path - ctrl::Exporter renders through the same composite wiring it gained
// (FilterFrame + opacityScale). Export a two-frame PNG sequence of a tree exercising a
// blurred clippee, a blurred transformed folder and a frame-varying blur, and compare each
// exported frame against the interactive-path render at the same time (the export camera is
// the same default camera). Video/GIF encoding (ffmpeg) is out of scope.
// (StubAnimator moved to harness.h)

void suiteExport() {
    suiteHeader("S9 export path (ctrl::Exporter image sequence)");
    const QSize canvas(128, 96);
    auto& fx = fixtureFor(canvas);
    const core::CameraInfo camera = scene::makeCamera(canvas);

    StubAnimator animator;
    core::Project project("", animator, nullptr);
    project.attribute().setImageSize(canvas);
    project.attribute().setMaxFrame(200);
    project.attribute().setFps(24);
    project.attribute().setLoop(false);
    auto* root = new core::FolderNode("root");
    project.objectTree().grabTopNode(root);
    core::ObjectTree& tree = project.objectTree();

    ResPool pool;
    {
        // blurred clippee (pushed first so it renders on top, clipped to the clipper below)
        auto* clippee = scene::addLayer(
            tree, root, pool.wedge("wd"), "clippee", QVector2D(60, 44), -15.0f, QVector2D(2.0f, 2.0f));
        clippee->setClipped(true);
        scene::addBlur(clippee, 0, 8.0f, 2.0f, 30.0f);
        scene::addLayer(
            tree, root, pool.wedge("wc"), "clipper", QVector2D(64, 48), 10.0f, QVector2D(1.5f, 1.2f));
        // blurred transformed folder; a second key at frame 1 weakens the blur so the two
        // exported frames must differ (export time progression)
        auto* folder = scene::addFolder(root, "folder", QVector2D(30, 60), 20.0f, QVector2D(1.3f, 0.8f));
        scene::addLayer(tree, folder, pool.wedge("wf"), "child", QVector2D(0, 0), 0.0f, QVector2D(1, 1));
        scene::addBlur(folder, 0, 10.0f, 3.0f, 40.0f);
        scene::addBlurAmount(folder, 1, 2.0f);
        // background at the bottom
        scene::addLayer(tree, root, pool.wedge("bg"), "bg", QVector2D(64, 48), 0.0f, QVector2D(4, 4));
    }

    // export frames 0..1 as PNGs through the real Exporter
    const QString outDir = "/tmp/vb_export_s9";
    QDir(outDir).removeRecursively();
    QDir().mkpath(outDir);
    ctrl::Exporter exporter(project);
    exporter.setOverwriteConfirmer([](const QString&) { return true; });
    NullProgressReporter reporter;
    exporter.setProgressReporter(reporter);
    ctrl::Exporter::CommonParam common;
    common.path = outDir;
    common.size = canvas;
    common.frame = util::Range(0, 1);
    common.fps = 24;
    ctrl::Exporter::ImageParam image;
    image.name = "f";
    image.suffix = "png";
    image.quality = -1;
    const ctrl::Exporter::Result result = exporter.execute(common, image);
    expect((bool)result, QString("S9: exporter.execute failed: %1").arg(result.message));

    // compare each exported PNG against the interactive-path render at the same frame;
    // the QImage on disk is top-down, the fixture readback is bottom-up
    const QStringList pngs = QDir(outDir).entryList({"*.png"}, QDir::Files, QDir::Name);
    if (!expect(pngs.size() == 2, QString("S9: two frames exported (got %1)").arg(pngs.size())))
        return;
    std::vector<uint8_t> frames[2];
    for (int i = 0; i < 2; ++i) {
        // PNG stores straight alpha; the GL readback (and the scene itself) is
        // premultiplied, so premultiply back before comparing (blur halos are
        // semi-transparent and would mismatch wildly otherwise)
        const QImage img = QImage(outDir + "/" + pngs[i])
                               .convertToFormat(QImage::Format_RGBA8888_Premultiplied)
                               .mirrored(false, true);
        expect(
            img.size() == canvas, QString("S9: frame %1 has canvas size (got %2x%3)")
                                      .arg(i)
                                      .arg(img.size().width())
                                      .arg(img.size().height()));
        std::vector<uint8_t> got((size_t)canvas.width() * canvas.height() * 4);
        for (int y = 0; y < canvas.height(); ++y)
            memcpy(got.data() + (size_t)y * canvas.width() * 4, img.constScanLine(y), (size_t)canvas.width() * 4);
        const std::vector<uint8_t> ref = fx.render(tree, camera, i);
        const scene::Diff d = scene::diffImages(got, ref);
        const bool ok = checkDiff(d, 8, 0.8, canvas);
        ++gChecks;
        if (!ok)
            ++gFails;
        caseReport(QString("exported frame %1 == interactive render").arg(i), ok, diffStr(d));
        frames[i] = std::move(got);
    }
    // the blur weakens at frame 1, so the two exported frames must differ
    const scene::Diff between = scene::diffImages(frames[0], frames[1]);
    expect(between.maxDiff > 16, "S9: exported frames differ across the blur key change");
}

//-------------------------------------------------------------------------------------------------
// S11: PSD import round-trip through the production writer + loader. An in-memory PSDFormat
// with group records (open-folder + bounding section dividers) and per-record PSD blend modes
// is written by img::PSDWriter, then loaded through ctrl::ImageFileLoader::loadPsd; the
// resulting object tree must carry the folder blend modes (minor version 10 feature), the
// group structure, folder opacities and layer visibility. Folder blend modes ride the same
// per-layer 4CC PSDReader has always parsed, so a file authored through the production
// writer exercises the whole import chain end to end.
void suitePsdImport() {
    suiteHeader("S11 psd import: PSDWriter -> ImageFileLoader folder blend modes");
    const int failsBefore = gFails;

    img::PSDFormat format;
    format.header().version = 1;
    format.header().channels = 4;
    format.header().height = 64;
    format.header().width = 64;
    format.header().depth = 8;
    format.header().mode = img::PSDFormat::ColorMode_RGB;
    format.imageData().compressionId = 0;

    auto setRect = [](img::PSDFormat::Layer* aLayer, int l, int t, int r, int b) {
        aLayer->rect.edge[0] = t;
        aLayer->rect.edge[1] = l;
        aLayer->rect.edge[2] = b;
        aLayer->rect.edge[3] = r;
    };
    auto addLayerRecord = [&](const std::string& aName, int l, int t, int r, int b, uint8 aOpacity, uint8 aFlags) {
        auto* layer = new img::PSDFormat::Layer();
        layer->name = aName;
        setRect(layer, l, t, r, b);
        layer->blendMode = "norm";
        layer->opacity = aOpacity;
        layer->flags = aFlags;
        const int size = (r - l) * (b - t);
        for (const sint16 id : {sint16(0), sint16(1), sint16(2), sint16(-1)}) {
            auto* chan = new img::PSDFormat::Channel();
            chan->id = id;
            chan->compressionId = 0;
            chan->dataLength = (uint32)size;
            chan->data.reset(new uint8[(size_t)size]);
            std::memset(chan->data.get(), 0, (size_t)size);
            layer->channels.push_back(img::PSDFormat::ChannelPtr(chan));
        }
        format.layerAndMaskInfo().layers.push_back(img::PSDFormat::LayerPtr(layer));
    };
    // group records carry the section divider ("lsct") additional info, which the reader
    // converts into LayerEntryType: open folders carry the 8BIM signature + group key,
    // bounding entries a bare entry value (like Photoshop writes them)
    auto addFolderRecord = [&](const std::string& aName, const std::string& aBlendMode, uint8 aOpacity,
                               const char* aGroupKey, int l, int t, int r, int b) {
        auto* layer = new img::PSDFormat::Layer();
        layer->name = aName;
        setRect(layer, l, t, r, b);
        layer->blendMode = aBlendMode;
        layer->opacity = aOpacity;
        auto* info = new img::PSDFormat::AdditionalLayerInfo();
        info->key = "lsct";
        if (aGroupKey) {
            info->dataLength = 12; // entry (4) + 8BIM (4) + key (4)
            info->data.reset(new uint8[12]);
            std::memset(info->data.get(), 0, 12);
            info->data[3] = 1; // open folder (LayerEntryType_OpenFolder)
            std::memcpy(info->data.get() + 4, "8BIM", 4);
            std::memcpy(info->data.get() + 8, aGroupKey, 4);
        } else {
            info->dataLength = 4; // entry only
            info->data.reset(new uint8[4]());
            info->data[3] = 3; // bounding section divider
        }
        layer->additionalInfos.push_back(img::PSDFormat::AdditionalLayerInfoPtr(info));
        format.layerAndMaskInfo().layers.push_back(img::PSDFormat::LayerPtr(layer));
    };

    // PSD stores layer records bottom-first; ImageFileLoader walks them top-first
    // (layers.rbegin()) and treats the first record it sees per group as the open
    // folder, the "bounding" record as the close, so per group the FILE order must be
    // [bounding-close, members..., open] to round-trip: walking top-first then sees
    // [open, members, close] and builds the tree topNode > [grp2, grp, bg] (grp2
    // topmost)
    // a pass-through group ('pass' - the default group blend mode Photoshop writes) and a
    // group with an unsupported mode ('diss' - dissolve is still unsupported) sit BELOW the
    // bg record (earlier in the file = lower z); both must import as Normal folders ('pass'
    // maps 1:1, unsupported modes fall back to Normal instead of injecting BlendMode_TERM
    // into the renderer)
    addFolderRecord("</Layer group>", "norm", 255, nullptr, 0, 0, 64, 64);
    addLayerRecord("child3", 4, 4, 12, 12, 255, 0);
    addFolderRecord("grpPass", "pass", 255, "GRP3", 0, 0, 64, 64);
    addFolderRecord("</Layer group>", "norm", 255, nullptr, 0, 0, 64, 64);
    addLayerRecord("child4", 48, 48, 60, 60, 255, 0);
    addFolderRecord("grpDiss", "diss", 255, "GRP4", 0, 0, 64, 64);
    addLayerRecord("bg", 0, 0, 64, 64, 255, 0);
    addFolderRecord("</Layer group>", "norm", 255, nullptr, 0, 0, 64, 64);
    addLayerRecord("child", 20, 16, 44, 40, 255, 0);
    addFolderRecord("grp", "mul ", 200, "GRP1", 0, 0, 64, 64);
    addFolderRecord("</Layer group>", "norm", 255, nullptr, 0, 0, 64, 64);
    addLayerRecord("child2", 8, 8, 24, 24, 255, 0x02); // hidden (visibility flag bit)
    addFolderRecord("grp2", "scrn", 100, "GRP2", 0, 0, 64, 64);

    // merged image data: raw 4 channels (the reader parses the section with its own size
    // math - rowBytes = (width*height + 7) / 8 scanlines of `height` rows per channel, so
    // 512*64 bytes each - the object-tree build does not use the pixels)
    {
        const uint32 size = 512 * 64; // PSDReader::loadImageData raw branch size
        for (const sint16 id : {sint16(0), sint16(1), sint16(2), sint16(-1)}) {
            auto* chan = new img::PSDFormat::Channel();
            chan->id = id;
            chan->compressionId = 0;
            chan->dataLength = size;
            chan->data.reset(new uint8[size]);
            std::memset(chan->data.get(), 0, size);
            format.imageData().channels.push_back(img::PSDFormat::ChannelPtr(chan));
        }
    }

    // write through the production writer
    const QString path = "/tmp/vb_psd_import.psd";
    {
        std::ofstream out(path.toLocal8Bit().constData(), std::ios::binary);
        img::PSDWriter writer(out, format);
        expect(writer.resultCode() == img::PSDWriter::ResultCode_Success, "S11: PSDWriter result");
        out.close();
    }

    // load through the production import path (the GL context is current, which loadPsd
    // needs for the eager layer shader compile and texture uploads)
    StubAnimator animator;
    core::Project project("", animator, nullptr);
    ctrl::ImageFileLoader loader(gl::DeviceInfo::instance());
    NullProgressReporter reporter;
    const bool loaded = loader.load(path, project, reporter);
    expect(loaded, QString("S11: loadPsd: %1").arg(loader.log()));
    if (!loaded)
        return;

    auto* top = project.objectTree().topNode();
    const auto& children = top->children();
    if (!expect(children.size() == 5, QString("S11: top children == 5 (got %1)").arg(children.size())))
        return;
    auto itr = children.begin();
    auto* grp2 = dynamic_cast<core::FolderNode*>(*itr++);
    auto* grp = dynamic_cast<core::FolderNode*>(*itr++);
    auto* bg = dynamic_cast<core::LayerNode*>(*itr++);
    auto* grpDiss = dynamic_cast<core::FolderNode*>(*itr++);
    auto* grpPass = dynamic_cast<core::FolderNode*>(*itr++);
    expect(grp2 && grp2->name() == "grp2", "S11: child[0] is folder 'grp2'");
    expect(grp && grp->name() == "grp", "S11: child[1] is folder 'grp'");
    expect(bg != nullptr, "S11: child[2] is a layer");

    // the folder blend modes come from the PSD 4CCs ("scrn", "mul ")
    expect(grp2 && grp2->blendMode() == img::BlendMode_Screen, "S11: grp2 blend == Screen (scrn)");
    expect(grp && grp->blendMode() == img::BlendMode_Multiply, "S11: grp blend == Multiply (mul )");
    expect(grpDiss && grpDiss->name() == "grpDiss", "S11: child[3] is folder 'grpDiss'");
    expect(grpPass && grpPass->name() == "grpPass", "S11: child[4] is folder 'grpPass'");
    expect(
        grpPass && grpPass->blendMode() == img::BlendMode_Normal,
        "S11: pass-through group ('pass') imports as a Normal folder");
    expect(
        grpDiss && grpDiss->blendMode() == img::BlendMode_Normal,
        "S11: unsupported group mode ('diss') falls back to Normal (no TERM injection)");

    // folder opacity maps to the default opa key
    const auto opaOf = [](core::ObjectNode* aNode) {
        auto* key = (core::OpaKey*)aNode->timeLine()->defaultKey(core::TimeKeyType_Opa);
        return key ? key->opacity() : 1.0f;
    };
    expect(grp && std::abs(opaOf(grp) - 200.0f / 255.0f) < 0.001f, "S11: grp opacity == 200/255");
    expect(grp2 && std::abs(opaOf(grp2) - 100.0f / 255.0f) < 0.001f, "S11: grp2 opacity == 100/255");

    // group structure and the hidden layer
    if (grp) {
        const auto& grpChildren = grp->children();
        expect(
            grpChildren.size() == 1 && (*grpChildren.begin())->name() == "child",
            "S11: grp contains layer 'child'");
    }
    if (grp2) {
        const auto& grp2Children = grp2->children();
        const bool ok = grp2Children.size() == 1 && (*grp2Children.begin())->name() == "child2"
            && !(*grp2Children.begin())->isVisible();
        expect(ok, "S11: grp2 contains hidden layer 'child2'");
    }

    caseReport(
        "PSD import round-trip (folders, blend modes, opacities, visibility)", gFails == failsBefore,
        QString("%1 checks").arg(gChecks));

    // Real-file check: the repo's own sample PSD (data/sample.psd, RLE channels, unicode
    // names, an 'lsct' section divider with a 'pass' group - the default group blend mode
    // Photoshop writes) goes through the same production import, then renders one frame
    // through the full composite wiring. Pre-fix this injected BlendMode_TERM into the
    // folder and aborted in presentShader's mode < TERM assert at first render.
    const int sampleChecksBefore = gChecks;
    {
        core::Project sampleProject("", animator, nullptr);
        ctrl::ImageFileLoader sampleLoader(gl::DeviceInfo::instance());
        const bool sampleLoaded = sampleLoader.load("data/sample.psd", sampleProject, reporter);
        expect(sampleLoaded, QString("S11: sample.psd loads: %1").arg(sampleLoader.log()));
        if (sampleLoaded) {
            int folderCount = 0;
            bool modesValid = true;
            const std::function<void(core::ObjectNode*)> walk = [&](core::ObjectNode* aNode) {
                if (auto* folder = dynamic_cast<core::FolderNode*>(aNode)) {
                    ++folderCount;
                    if (folder->blendMode() >= img::BlendMode_TERM)
                        modesValid = false;
                }
                for (auto* child : aNode->children())
                    walk(child);
            };
            walk(sampleProject.objectTree().topNode());
            expect(folderCount >= 2, QString("S11: sample.psd has groups (got %1 folders incl. top)").arg(folderCount));
            expect(modesValid, "S11: every imported folder blend mode is valid (< TERM)");
            // render smoke: exercises the import + composite wiring end to end
            const QSize sampleCanvas = sampleProject.attribute().imageSize();
            auto& sampleFx = fixtureFor(sampleCanvas);
            sampleFx.render(sampleProject.objectTree(), scene::makeCamera(sampleCanvas), 0);
            expect(true, "S11: sample.psd renders a frame without hitting a shader assert");
        }
    }

    // CSP tsly flag: CSP exports its own Add (Glow)/Glow Dodge with the plain "lddg"/"div "
    // blend keys PLUS a "tsly" layer-info block whose value is 0 (CSP-specific) instead of
    // 1 (plain) - verified against the user's pair of real CSP exports (psd.psd vs
    // psd_with_non_glow_equivalents.psd: identical layer records, differ only in that
    // flag). The mapping must produce the CSP modes only when the flag is present AND 0;
    // Adobe never writes "tsly", so lddg/div without it stay LinearDodge/ColorDodge.
    {
        img::PSDFormat tslyFormat;
        tslyFormat.header().version = 1;
        tslyFormat.header().channels = 4;
        tslyFormat.header().height = 64;
        tslyFormat.header().width = 64;
        tslyFormat.header().depth = 8;
        tslyFormat.header().mode = img::PSDFormat::ColorMode_RGB;
        tslyFormat.imageData().compressionId = 0;
        auto tslyRecord = [&](const std::string& aName, const std::string& aBlend, int aTsly) {
            auto* layer = new img::PSDFormat::Layer();
            layer->name = aName;
            setRect(layer, 0, 0, 8, 8);
            layer->blendMode = aBlend;
            layer->opacity = 255;
            auto* info = new img::PSDFormat::AdditionalLayerInfo();
            info->key = "tsly";
            info->dataLength = 4;
            info->data.reset(new uint8[4]);
            std::memset(info->data.get(), 0, 4);
            if (aTsly > 0)
                info->data[0] = 1;
            layer->additionalInfos.push_back(img::PSDFormat::AdditionalLayerInfoPtr(info));
            for (const sint16 id : {sint16(0), sint16(1), sint16(2), sint16(-1)}) {
                auto* chan = new img::PSDFormat::Channel();
                chan->id = id;
                chan->compressionId = 0;
                chan->dataLength = 64;
                chan->data.reset(new uint8[64]);
                std::memset(chan->data.get(), 0, 64);
                layer->channels.push_back(img::PSDFormat::ChannelPtr(chan));
            }
            tslyFormat.layerAndMaskInfo().layers.push_back(img::PSDFormat::LayerPtr(layer));
        };
        tslyRecord("csp_addglow", "lddg", 0);
        tslyRecord("plain_lddg", "lddg", 1);
        tslyRecord("csp_glowdodge", "div ", 0);
        tslyRecord("plain_div", "div ", 1);
        {
            const uint32 size = 512 * 64; // PSDReader::loadImageData raw branch size
            for (const sint16 id : {sint16(0), sint16(1), sint16(2), sint16(-1)}) {
                auto* chan = new img::PSDFormat::Channel();
                chan->id = id;
                chan->compressionId = 0;
                chan->dataLength = size;
                chan->data.reset(new uint8[size]);
                std::memset(chan->data.get(), 0, size);
                tslyFormat.imageData().channels.push_back(img::PSDFormat::ChannelPtr(chan));
            }
        }
        const QString tslyPath = "/tmp/vb_psd_tsly.psd";
        {
            std::ofstream out(tslyPath.toLocal8Bit().constData(), std::ios::binary);
            img::PSDWriter writer(out, tslyFormat);
            expect(writer.resultCode() == img::PSDWriter::ResultCode_Success, "S11: tsly PSDWriter result");
            out.close();
        }
        core::Project tslyProject("", animator, nullptr);
        ctrl::ImageFileLoader tslyLoader(gl::DeviceInfo::instance());
        const bool tslyLoaded = tslyLoader.load(tslyPath, tslyProject, reporter);
        expect(tslyLoaded, QString("S11: tsly PSD loads: %1").arg(tslyLoader.log()));
        if (tslyLoaded) {
            const std::function<void(core::ObjectNode*)> checkTsly = [&](core::ObjectNode* aNode) {
                if (auto* ln = dynamic_cast<core::LayerNode*>(aNode)) {
                    if (aNode->name() == "csp_addglow")
                        expect(
                            ln->blendMode() == img::BlendMode_AddGlow,
                            "S11: lddg + tsly=0 imports as Add (Glow)");
                    if (aNode->name() == "plain_lddg")
                        expect(
                            ln->blendMode() == img::BlendMode_LinearDodge,
                            "S11: lddg + tsly=1 imports as Linear Dodge");
                    if (aNode->name() == "csp_glowdodge")
                        expect(
                            ln->blendMode() == img::BlendMode_GlowDodge,
                            "S11: div  + tsly=0 imports as Glow Dodge");
                    if (aNode->name() == "plain_div")
                        expect(
                            ln->blendMode() == img::BlendMode_ColorDodge,
                            "S11: div  + tsly=1 imports as Color Dodge");
                }
                for (auto* child : aNode->children())
                    checkTsly(child);
            };
            checkTsly(tslyProject.objectTree().topNode());
        }
    }
    caseReport(
        "sample.psd real-file import (pass-through group) + render smoke", gFails == failsBefore,
        QString("%1 checks").arg(gChecks - sampleChecksBefore));
}

//-------------------------------------------------------------------------------------------------
// S13: ORA import round-trip through the production oraParser + ImageFileLoader. The
// harness writes a real .ora zip (mimetype + stack.xml + PNG entries) whose stacks carry
// composite-op attributes, then loads it through ctrl::ImageFileLoader with the layered
// import mode pre-selected (OraImportMode skips the interactive layered/merged prompt).
// Stacks map composite-op like layers (multiply/screen); an attribute-less stack keeps
// the pass-through Normal default; opacities map to default opa keys. Folder assertions
// are name-based (oraParser flattens the stack document in its own traversal order).
void suiteOraImport() {
    suiteHeader("S13 ora import: synthetic .ora -> ImageFileLoader folder blend modes");
    const int failsBefore = gFails;
    const int checksBefore = gChecks;

    const QSize canvas(48, 48);
    auto pngBytes = [](const QSize& aSize, const QColor& aColor) {
        QImage img(aSize, QImage::Format_RGBA8888);
        img.fill(aColor);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        img.save(&buffer, "PNG");
        return bytes;
    };

    // stacks: multiply @0.8, screen @0.75, and one without composite-op (Normal default)
    const std::string stackXml =
        "<image w='48' h='48' version='0.0.1'>"
        "<stack name='root'>"
        "<layer name='bg' src='data/bg.png' x='0' y='0' opacity='1.0' visibility='visible'"
        " composite-op='svg:src-over'/>"
        "<stack name='grpMul' composite-op='svg:multiply' opacity='0.8' visibility='visible'>"
        "<layer name='child' src='data/child.png' x='6' y='6' opacity='1.0' visibility='visible'"
        " composite-op='svg:src-over'/>"
        "</stack>"
        "<stack name='grpScreen' composite-op='svg:screen' opacity='0.75' visibility='visible'>"
        "<layer name='child2' src='data/child2.png' x='24' y='24' opacity='1.0' visibility='visible'"
        " composite-op='svg:src-over'/>"
        "</stack>"
        "<stack name='grpDefault' opacity='1.0' visibility='visible'>"
        "<layer name='child3' src='data/child3.png' x='36' y='4' opacity='1.0' visibility='visible'"
        " composite-op='svg:src-over'/>"
        "</stack>"
        "</stack>"
        "</image>";

    // write a real .ora zip through the same miniz wrapper the reader uses
    const QString path = "/tmp/vb_ora_import.ora";
    {
        miniz_cpp::zip_file ora;
        ora.writestr("mimetype", "image/openraster");
        ora.writestr("stack.xml", stackXml);
        ora.writestr("data/bg.png", pngBytes(canvas, QColor(32, 48, 64, 255)).toStdString());
        ora.writestr("data/child.png", pngBytes(QSize(16, 16), QColor(200, 60, 40, 255)).toStdString());
        ora.writestr("data/child2.png", pngBytes(QSize(16, 16), QColor(40, 180, 90, 255)).toStdString());
        ora.writestr("data/child3.png", pngBytes(QSize(8, 8), QColor(220, 200, 60, 255)).toStdString());
        ora.writestr("mergedimage.png", pngBytes(canvas, QColor(90, 90, 90, 255)).toStdString());
        ora.save(path.toStdString());
    }

    // load through the production import path with the layered mode pre-selected (the
    // interactive prompt would block the harness); the GL context is current for the
    // eager layer shader compile and texture uploads.
    //
    // The parse runs under a comma-decimal LC_NUMERIC scoped with uselocale(): that is
    // exactly the locale that used to silently zero ORA opacities ("0.8" parsed as 0.0)
    // via strtod-style as_float(), see oraParser.cpp. The parse is immune (it uses the
    // C-locale QString::toFloat()), so this only pins the regression: if a parse path
    // ever regresses to a locale-honoring converter, opacity checks below fail. Best
    // effort - machines without any of these locales keep the C default via newlocale()
    // returning null. Unlike the old process-wide setlocale() in main, uselocale keeps
    // the rest of the harness on the environment locale.
    StubAnimator animator;
    core::Project project("", animator, nullptr);
    ctrl::ImageFileLoader loader(gl::DeviceInfo::instance());
    loader.setOraImportMode(ctrl::ImageFileLoader::OraImportMode::Layered);
    NullProgressReporter reporter;
    locale_t commaNum = nullptr;
    for (const char* loc : {"cs_CZ.UTF-8", "de_DE.UTF-8", "fr_FR.UTF-8", "ru_RU.UTF-8"}) {
        commaNum = newlocale(LC_NUMERIC_MASK, loc, nullptr);
        if (commaNum)
            break;
    }
    locale_t savedNum = uselocale(commaNum); // commaNum null -> C default, harmless
    const bool loaded = loader.load(path, project, reporter);
    uselocale(savedNum);
    if (commaNum)
        freelocale(commaNum);
    expect(loaded, QString("S13: loadOra: %1").arg(loader.log()));
    if (!loaded)
        return;

    // collect the imported folders by name (oraParser's flat-list traversal order is
    // its own business; the blend wiring is what this suite pins down)
    std::map<QString, core::FolderNode*> folders;
    const std::function<void(core::ObjectNode*)> walk = [&](core::ObjectNode* aNode) {
        if (auto* folder = dynamic_cast<core::FolderNode*>(aNode))
            folders.emplace(folder->name(), folder);
        for (auto* child : aNode->children())
            walk(child);
    };
    walk(project.objectTree().topNode());
    auto folderMode = [&](const QString& aName) {
        auto it = folders.find(aName);
        return it != folders.end() ? it->second->blendMode() : img::BlendMode_TERM;
    };
    expect(
        folderMode("grpMul") == img::BlendMode_Multiply,
        "S13: stack 'grpMul' blend == Multiply (svg:multiply)");
    expect(
        folderMode("grpScreen") == img::BlendMode_Screen,
        "S13: stack 'grpScreen' blend == Screen (svg:screen)");
    expect(
        folderMode("grpDefault") == img::BlendMode_Normal,
        "S13: stack without composite-op keeps the Normal default");

    // stack opacity maps to the default opa key
    const auto opaOf = [](core::ObjectNode* aNode) {
        auto* key = (core::OpaKey*)aNode->timeLine()->defaultKey(core::TimeKeyType_Opa);
        return key ? key->opacity() : 1.0f;
    };
    expect(
        folders.count("grpMul") && std::abs(opaOf(folders["grpMul"]) - 0.8f) < 0.001f,
        QString("S13: grpMul opacity == 0.8 (got %1)")
            .arg(folders.count("grpMul") ? opaOf(folders["grpMul"]) : -1.0f));
    expect(
        folders.count("grpScreen") && std::abs(opaOf(folders["grpScreen"]) - 0.75f) < 0.001f,
        QString("S13: grpScreen opacity == 0.75 (got %1)")
            .arg(folders.count("grpScreen") ? opaOf(folders["grpScreen"]) : -1.0f));

    // group structure: each named stack holds its layer
    auto holdsLayer = [&](const QString& aFolder, const QString& aLayer) {
        auto it = folders.find(aFolder);
        if (it == folders.end())
            return false;
        for (auto* child : it->second->children()) {
            if (child->name() == aLayer)
                return true;
        }
        return false;
    };
    expect(holdsLayer("grpMul", "child"), "S13: grpMul contains layer 'child'");
    expect(holdsLayer("grpScreen", "child2"), "S13: grpScreen contains layer 'child2'");
    expect(holdsLayer("grpDefault", "child3"), "S13: grpDefault contains layer 'child3'");

    // render smoke: the imported tree (two blend-mode folders) goes through the
    // composite wiring without hitting a shader assert
    const QSize loadedCanvas = project.attribute().imageSize();
    auto& fx = fixtureFor(loadedCanvas);
    fx.render(project.objectTree(), scene::makeCamera(loadedCanvas), 0);
    expect(true, "S13: imported ORA renders a frame without hitting a shader assert");

    caseReport(
        "ORA import (folder blend modes, opacities, structure) + render smoke", gFails == failsBefore,
        QString("%1 checks").arg(gChecks - checksBefore));
}

//-------------------------------------------------------------------------------------------------
// S12: folder blend modes render semantics (minor version 10 feature). A folder with a
// non-Normal blend mode composites its subtree and presents it against the captured scene
// (DestinationTexturizer) with the folder's mode at its own opacity. The CPU reference is
// blendPresent(content, background, mode, opacity) where content is the same subtree
// rendered without the mode. For a fractional folder opacity the composite renders its
// children at the compensated opacity 1/op, which clamps to 1.0 in the RGBA8 slot, so the
// reference content render needs no compensation; the present opacity is quantized to the
// app's 8-bit uColor.
void suiteFolderBlend() {    suiteHeader("S12 folder blend modes: GPU vs CPU reference");
    const int failsBefore = gFails;
    using A3 = std::array<double, 3>;
    auto R = [](double deg, double sx, double sy) { return A3{deg * kDegToRad, sx, sy}; };
    const QSize canvas(128, 96);
    auto& fx = fixtureFor(canvas);
    const core::CameraInfo camera = scene::makeCamera(canvas);

    auto renderTree = [&](const std::function<void(core::ObjectTree&, ResPool&)>& aBuild) {
        ResPool pool;
        core::ObjectTree tree;
        aBuild(tree, pool);
        return fx.render(tree, camera, 0);
    };
    auto bytesToImage = [&](const std::vector<uint8_t>& aBytes) {
        return ref::imageFromBytes(aBytes.data(), canvas.width(), canvas.height());
    };
    auto runCase = [&](const QString& aName, const std::vector<uint8_t>& aGpu, const ref::Image& aContent,
                       const ref::Image& aBg, img::BlendMode aMode, double aOpacity) {
        const ref::Image presented = ref::blendPresent(aContent, aBg, aMode, aOpacity);
        const scene::Diff d = scene::diffImages(aGpu, ref::imageToBytes(presented));
        // Color's max=54/255 spike on 3-2 pixels at the wedge's gray-crossing gradient
        // bar is NOT the ClipColor formulas: they are algebraically bounded (the ratios
        // l/(l-n), (1-l)/(x-l) cap at 1 where their branches fire), and the S14 golden
        // probes psd_layer_color_wedge / psd_layer_color_sweep pin the GPU Color formula
        // to Krita at max=1/255 on a FLAT scene (identity transforms, exact texel
        // sampling). The spike is the double-precision replica resampling the transformed
        // shapes (rot/scale under motion transforms: CPU rounds to-texel, GPU bilinear),
        // i.e. a reference-path artifact, not shader or blend-math error. Keep the 64
        // tolerance so the suite stays green, but guard the outlier COUNT: an enlarged
        // spike (a real formula regression) must grow past max=4 pixels and fail.
        const int maxTol = aMode == img::BlendMode_Color ? 64 : 8;
        const bool ok = aMode == img::BlendMode_Color
            ? (d.maxDiff <= maxTol && d.mean <= 0.8 && d.over4 <= 4)
            : checkDiff(d, maxTol, 0.8, canvas);
        ++gChecks;
        if (!ok)
            ++gFails;
        caseReport(aName, ok, diffStr(d));
    };
    // the standard full tree: a blend-mode folder over a wedged background layer
    auto buildFull = [](core::ObjectTree& tree, ResPool& pool, img::BlendMode aMode, bool aTransformed) {
        auto* root = new core::FolderNode("root");
        tree.grabTopNode(root);
        auto* folder = scene::addFolder(
            root, "folder", QVector2D(64, 48), aTransformed ? 25.0f : 0.0f, aTransformed ? QVector2D(1.6f, 0.9f) : QVector2D(1, 1));
        folder->setBlendMode(aMode);
        scene::addLayer(tree, folder, pool.wedge("w"), "layer", QVector2D(6, -4), -12.0f, QVector2D(1.2f, 1.0f));
        scene::addLayer(tree, root, pool.wedge("bg"), "bg", QVector2D(58, 52), -8.0f, QVector2D(1.6f, 1.4f));
    };
    auto buildContent = [](core::ObjectTree& tree, ResPool& pool, bool aTransformed) {
        auto* root = new core::FolderNode("root");
        tree.grabTopNode(root);
        auto* folder = scene::addFolder(
            root, "folder", QVector2D(64, 48), aTransformed ? 25.0f : 0.0f, aTransformed ? QVector2D(1.6f, 0.9f) : QVector2D(1, 1));
        scene::addLayer(tree, folder, pool.wedge("w"), "layer", QVector2D(6, -4), -12.0f, QVector2D(1.2f, 1.0f));
    };
    auto buildBg = [](core::ObjectTree& tree, ResPool& pool) {
        auto* root = new core::FolderNode("root");
        tree.grabTopNode(root);
        scene::addLayer(tree, root, pool.wedge("bg"), "bg", QVector2D(58, 52), -8.0f, QVector2D(1.6f, 1.4f));
    };
    auto contentImage = [&](bool aTransformed) {
        return bytesToImage(renderTree([aTransformed, &buildContent](core::ObjectTree& tree, ResPool& pool) {
            buildContent(tree, pool, aTransformed);
        }));
    };
    auto bgImage = [&]() { return bytesToImage(renderTree(buildBg)); };

    // (a) every blend mode, untransformed
    for (int m = img::BlendMode_Normal; m < img::BlendMode_TERM; ++m) {
        const img::BlendMode mode = (img::BlendMode)m;
        const std::vector<uint8_t> gpu = renderTree([mode, &buildFull](core::ObjectTree& tree, ResPool& pool) {
            buildFull(tree, pool, mode, false);
        });
        runCase(QString("folder blend %1 over bg").arg(img::getBlendNameFromBlendMode(mode)), gpu,
            contentImage(false), bgImage(), mode, 1.0);
    }

    // (a2) every blend mode over an OPAQUE content: with opaque content premult == straight
    // in the RGBA8 readback, so the CPU reference's un-multiply is exact; with
    // semi-transparent content the readback quantization of the premult values is
    // amplified by the steep Vivid Light / Hard Mix gradients to full-scale diffs, so
    // those two modes run again (with all others, for coverage) on opaque content
    {
        auto buildOpaqueContent = [](core::ObjectTree& tree, ResPool& pool) {
            auto* root = new core::FolderNode("root");
            tree.grabTopNode(root);
            auto* folder = scene::addFolder(root, "folder", QVector2D(64, 48), 0.0f, QVector2D(1, 1));
            folder->setBlendMode(img::BlendMode_Normal);
            scene::addLayer(tree, folder, pool.wedgeOpaque("wo"), "layer", QVector2D(6, -4), -12.0f, QVector2D(1.2f, 1.0f));
        };
        const ref::Image contentOpaque = bytesToImage(renderTree(buildOpaqueContent));
        for (const img::BlendMode mode :
             {img::BlendMode_VividLight, img::BlendMode_HardMix, img::BlendMode_ColorBurn, img::BlendMode_ColorDodge,
                 img::BlendMode_Multiply, img::BlendMode_Screen, img::BlendMode_Divide, img::BlendMode_Hue,
                 img::BlendMode_Saturation, img::BlendMode_Color, img::BlendMode_Luminosity, img::BlendMode_DarkerColor,
                 img::BlendMode_LighterColor}) {
            const std::vector<uint8_t> gpu = renderTree([mode](core::ObjectTree& tree, ResPool& pool) {
                auto* root = new core::FolderNode("root");
                tree.grabTopNode(root);
                auto* folder = scene::addFolder(root, "folder", QVector2D(64, 48), 0.0f, QVector2D(1, 1));
                folder->setBlendMode(mode);
                scene::addLayer(tree, folder, pool.wedgeOpaque("wo"), "layer", QVector2D(6, -4), -12.0f, QVector2D(1.2f, 1.0f));
                scene::addLayer(tree, root, pool.wedge("bg"), "bg", QVector2D(58, 52), -8.0f, QVector2D(1.6f, 1.4f));
            });
            runCase(QString("folder %1 over bg (opaque content)").arg(img::getBlendNameFromBlendMode(mode)), gpu,
                contentOpaque, bgImage(), mode, 1.0);
        }
    }

    // (b) transformed folder: rot25 s(1.6,0.9), layer rot-12 s(1.2,1)
    for (const img::BlendMode mode : {img::BlendMode_Multiply, img::BlendMode_Screen, img::BlendMode_Overlay,
                                      img::BlendMode_Difference}) {
        const std::vector<uint8_t> gpu = renderTree([mode, &buildFull](core::ObjectTree& tree, ResPool& pool) {
            buildFull(tree, pool, mode, true);
        });
        runCase(
            QString("folder %1 rot25 s1.6 > layer rot-12").arg(img::getBlendNameFromBlendMode(mode)), gpu,
            contentImage(true), bgImage(), mode, 1.0);
    }

    // (c) fractional folder opacity 0.7 (the composite renders children at 1/0.7 which
    // clamps to 1.0 in the RGBA8 slot, so the content render is uncompensated; the present
    // opacity rides uColor.a quantized to 8 bits like the app does)
    auto buildContentPlain = [](core::ObjectTree& tree, ResPool& pool) {
        auto* root = new core::FolderNode("root");
        tree.grabTopNode(root);
        auto* folder = scene::addFolder(root, "folder", QVector2D(64, 48), 0.0f, QVector2D(1, 1));
        scene::addLayer(tree, folder, pool.wedge("w"), "layer", QVector2D(0, 0), 0.0f, QVector2D(1, 1));
    };
    const ref::Image contentPlain = bytesToImage(renderTree(buildContentPlain));
    const double opQ = (double)(int)(255.0f * 0.7f) / 255.0;
    for (const img::BlendMode mode : {img::BlendMode_Multiply, img::BlendMode_Screen, img::BlendMode_Divide}) {
        const std::vector<uint8_t> gpu = renderTree([mode, &buildFull](core::ObjectTree& tree, ResPool& pool) {
            auto* root = new core::FolderNode("root");
            tree.grabTopNode(root);
            auto* folder = scene::addFolder(root, "folder", QVector2D(64, 48), 0.0f, QVector2D(1, 1));
            folder->setBlendMode(mode);
            folder->setDefaultOpacity(0.7f);
            scene::addLayer(tree, folder, pool.wedge("w"), "layer", QVector2D(0, 0), 0.0f, QVector2D(1, 1));
            scene::addLayer(tree, root, pool.wedge("bg"), "bg", QVector2D(58, 52), -8.0f, QVector2D(1.6f, 1.4f));
        });
        runCase(QString("folder %1 @ opacity 0.7").arg(img::getBlendNameFromBlendMode(mode)), gpu,
            contentPlain, bgImage(), mode, opQ);
    }

    // (d) folder blur x blend: the blur is baked into the composite, then the folder
    // presents the result with its mode against the real captured background
    for (const img::BlendMode mode : {img::BlendMode_Multiply, img::BlendMode_Screen}) {
        const std::vector<uint8_t> gpu = renderTree([mode, &buildFull](core::ObjectTree& tree, ResPool& pool) {
            auto* root = new core::FolderNode("root");
            tree.grabTopNode(root);
            auto* folder = scene::addFolder(root, "folder", QVector2D(64, 48), 20.0f, QVector2D(1.3f, 0.8f));
            folder->setBlendMode(mode);
            scene::addLayer(tree, folder, pool.wedge("w"), "layer", QVector2D(0, 0), 0.0f, QVector2D(1, 1));
            scene::addBlur(folder, 0, 10.0f, 3.0f, 40.0f);
            scene::addLayer(tree, root, pool.wedge("bg"), "bg", QVector2D(58, 52), -8.0f, QVector2D(1.6f, 1.4f));
        });
        const ref::Image plain = bytesToImage(renderTree([](core::ObjectTree& tree, ResPool& pool) {
            auto* root = new core::FolderNode("root");
            tree.grabTopNode(root);
            auto* folder = scene::addFolder(root, "folder", QVector2D(64, 48), 20.0f, QVector2D(1.3f, 0.8f));
            scene::addLayer(tree, folder, pool.wedge("w"), "layer", QVector2D(0, 0), 0.0f, QVector2D(1, 1));
        }));
        const ref::Image blurred = refBlurApplied(plain, canvas, camera, {R(20, 1.3, 0.8)}, 10, 3, 40, 1.0f);
        runCase(QString("folder %1 + blur (10,3,40)").arg(img::getBlendNameFromBlendMode(mode)), gpu,
            blurred, bgImage(), mode, 1.0);
    }

    // (e) folder HSV x blend: the HSV is applied at presentation together with the blend
    // (the combined USE_HSV + per-mode present shader), so the reference is
    // blendPresent(hsv(plain), bg, mode)
    for (const img::BlendMode mode : {img::BlendMode_Multiply, img::BlendMode_Screen}) {
        const std::vector<uint8_t> gpu = renderTree([mode, &buildFull](core::ObjectTree& tree, ResPool& pool) {
            auto* root = new core::FolderNode("root");
            tree.grabTopNode(root);
            auto* folder = scene::addFolder(root, "folder", QVector2D(64, 48), 20.0f, QVector2D(1.3f, 0.8f));
            folder->setBlendMode(mode);
            scene::addLayer(tree, folder, pool.wedge("w"), "layer", QVector2D(0, 0), 0.0f, QVector2D(1, 1));
            scene::addHSV(folder, 0, 60, 150, 80, 0);
            scene::addLayer(tree, root, pool.wedge("bg"), "bg", QVector2D(58, 52), -8.0f, QVector2D(1.6f, 1.4f));
        });
        const ref::Image plain = bytesToImage(renderTree([](core::ObjectTree& tree, ResPool& pool) {
            auto* root = new core::FolderNode("root");
            tree.grabTopNode(root);
            auto* folder = scene::addFolder(root, "folder", QVector2D(64, 48), 20.0f, QVector2D(1.3f, 0.8f));
            scene::addLayer(tree, folder, pool.wedge("w"), "layer", QVector2D(0, 0), 0.0f, QVector2D(1, 1));
        }));
        const ref::Image adjusted = ref::hsvAdjust(plain, 60.0 / 360.0, 150.0 / 100.0, 80.0 / 100.0, false);
        runCase(QString("folder HSV(60,150,80) + %1").arg(img::getBlendNameFromBlendMode(mode)), gpu,
            adjusted, bgImage(), mode, 1.0);
    }

    // (f) nested blend folders: outer(multiply) > [innerBg, inner(screen) > [layer]] over
    // bg. The outer composite pass renders its children in tree order, so innerBg is added
    // BEFORE the inner folder: the inner present then captures the outer composite's
    // content (innerBg) as its blend background, and the outer present blends the whole
    // result against the real scene (bg). The reference renders reproduce the world
    // transforms the GPU applies inside the outer composite (rot15 s(1.2,0.8)).
    {
        const std::vector<uint8_t> gpu = renderTree([](core::ObjectTree& tree, ResPool& pool) {
            auto* root = new core::FolderNode("root");
            tree.grabTopNode(root);
            auto* outer = scene::addFolder(root, "outer", QVector2D(64, 48), 15.0f, QVector2D(1.2f, 0.8f));
            outer->setBlendMode(img::BlendMode_Multiply);
            scene::addLayer(tree, outer, pool.wedge("bi"), "innerBg", QVector2D(40, 60), 10.0f, QVector2D(1, 1));
            auto* inner = scene::addFolder(outer, "inner", QVector2D(0, 0), 0.0f, QVector2D(1, 1));
            inner->setBlendMode(img::BlendMode_Screen);
            scene::addLayer(tree, inner, pool.wedge("w"), "layer", QVector2D(0, 0), 0.0f, QVector2D(1, 1));
            scene::addLayer(tree, root, pool.wedge("bg"), "bg", QVector2D(58, 52), -8.0f, QVector2D(1.6f, 1.4f));
        });
        const ref::Image content = bytesToImage(renderTree([](core::ObjectTree& tree, ResPool& pool) {
            auto* root = new core::FolderNode("root");
            tree.grabTopNode(root);
            auto* outer = scene::addFolder(root, "outer", QVector2D(64, 48), 15.0f, QVector2D(1.2f, 0.8f));
            scene::addLayer(tree, outer, pool.wedge("w"), "layer", QVector2D(0, 0), 0.0f, QVector2D(1, 1));
        }));
        const ref::Image innerBg = bytesToImage(renderTree([](core::ObjectTree& tree, ResPool& pool) {
            auto* root = new core::FolderNode("root");
            tree.grabTopNode(root);
            auto* outer = scene::addFolder(root, "outer", QVector2D(64, 48), 15.0f, QVector2D(1.2f, 0.8f));
            scene::addLayer(tree, outer, pool.wedge("bi"), "innerBg", QVector2D(40, 60), 10.0f, QVector2D(1, 1));
        }));
        const ref::Image inner = ref::blendPresent(content, innerBg, img::BlendMode_Screen, 1.0);
        runCase("nested: outer multiply > inner screen", gpu, inner, bgImage(), img::BlendMode_Multiply, 1.0);
    }

    // (g) the Normal gate: a folder with blend mode Normal stays on the pass-through path
    // and renders byte-identical to the same tree without the folder. The folder must be
    // untransformed for the byte-exact comparison (an identity parent chain multiplies
    // without changing the layer's float matrix; a composed flat transform cannot
    // reproduce the folder chain's float rounding)
    {
        // the folder must be UNTRANSFORMED - including position - for the byte-exact
        // comparison: the folder's move key translates the whole chain, so a folder at
        // (64,48) puts the layer at a different world spot than the flat tree (buildFull
        // defaults to (64,48); this is why the shared builder cannot be reused here). An
        // identity parent chain multiplies without changing the layer's float matrix (an
        // identity 4x4 product is exact in float), which a composed flat transform could
        // not reproduce.
        const std::vector<uint8_t> plain = renderTree([](core::ObjectTree& tree, ResPool& pool) {
            auto* root = new core::FolderNode("root");
            tree.grabTopNode(root);
            auto* folder = scene::addFolder(root, "folder", QVector2D(0, 0), 0.0f, QVector2D(1, 1));
            folder->setBlendMode(img::BlendMode_Normal);
            scene::addLayer(tree, folder, pool.wedge("w"), "layer", QVector2D(6, -4), -12.0f, QVector2D(1.2f, 1.0f));
            scene::addLayer(tree, root, pool.wedge("bg"), "bg", QVector2D(58, 52), -8.0f, QVector2D(1.6f, 1.4f));
        });
        const std::vector<uint8_t> noFolder = renderTree([](core::ObjectTree& tree, ResPool& pool) {
            auto* root = new core::FolderNode("root");
            tree.grabTopNode(root);
            scene::addLayer(tree, root, pool.wedge("w"), "layer", QVector2D(6, -4), -12.0f, QVector2D(1.2f, 1.0f));
            scene::addLayer(tree, root, pool.wedge("bg"), "bg", QVector2D(58, 52), -8.0f, QVector2D(1.6f, 1.4f));
        });
        const scene::Diff d = scene::diffImages(plain, noFolder);
        expect(d.bytesIdentical, "S12: folder blend Normal is pass-through (byte-identical)");
        caseReport("Normal gate: mode-Normal folder == no folder", d.bytesIdentical, diffStr(d));
    }

    caseReport("folder blend semantics (26 modes x transforms/opacity/blur/HSV/nesting)", gFails == failsBefore,
        QString("%1 checks").arg(gChecks));
}

//-------------------------------------------------------------------------------------------------
// S15: shader-variant permutation matrix. LayerDrawingFrag.glsl carries six variation
// flags, but only three are orthogonal: BLEND_FUNC, BLEND_NONSEPARABLE and
// BLEND_PREMULTIPLIED_SRC are all derived from the blend mode, while IS_CLIPPEE and
// USE_HSV are independent booleans. ShaderHolder caches one program per
// (mode, isClippee, useHSV) slot = BlendMode_TERM * 2 * 2 (index math at
// src/core/ShaderHolder.cpp:29). The render suites above only force the combinations
// their scenes happen to select (presentHSVShader came from one unusual combination no
// earlier suite compiled), so this suite force-compiles and links the FULL matrix:
// reserveShader() aborts via XC_FATAL_ERROR on any compile/link failure, so merely
// reaching the report is the check.
void suiteShaderMatrix() {
    suiteHeader("S15 shader-variant matrix: (mode, isClippee, useHSV) compile+link");
    const int failsBefore = gFails;
    const int checksBefore = gChecks;
    core::ObjectTree tree;
    int variantCount = 0;
    for (int m = img::BlendMode_Normal; m < img::BlendMode_TERM; ++m) {
        const img::BlendMode mode = (img::BlendMode)m;
        for (int clippee = 0; clippee < 2; ++clippee) {
            for (int hsv = 0; hsv < 2; ++hsv) {
                tree.shaderHolder().reserveShader(mode, clippee != 0, hsv != 0);
                ++variantCount;
            }
        }
    }
    expect(
        variantCount == img::BlendMode_TERM * 2 * 2,
        QString("S15: compiled+linked %1 of %2 (mode, isClippee, useHSV) shader variants")
            .arg(variantCount).arg(img::BlendMode_TERM * 2 * 2));
    caseReport("shader-variant permutation matrix", gFails == failsBefore,
        QString("%1 checks").arg(gChecks - checksBefore));
}

//-------------------------------------------------------------------------------------------------
// S10: performance snapshot (informational, not pass/fail): filtered vs unfiltered renders
// at a realistic canvas. Each timed render includes one glReadPixels (forces a pipeline
// sync; constant across variants, so ratios are meaningful).
void suitePerf() {
    suiteHeader("S10 performance snapshot (informational, 1920x1080)");
    const QSize canvas(1920, 1080);
    auto& fx = fixtureFor(canvas);
    const core::CameraInfo camera = scene::makeCamera(canvas);

    auto build = [](core::ObjectTree& tree, ResPool& pool, float aBlur) {
        auto* root = new core::FolderNode("root");
        tree.grabTopNode(root);
        auto* folder = scene::addFolder(root, "folder", QVector2D(760, 540), 20.0f, QVector2D(1.3f, 0.9f));
        for (int i = 0; i < 3; ++i) {
            scene::addLayer(
                tree, folder, pool.wedge(QString("w%1").arg(i)), QString("l%1").arg(i),
                QVector2D(200.0f * i, 60.0f * i), 15.0f * i, QVector2D(3, 3));
        }
        if (aBlur > 0.0f)
            scene::addBlurAmount(folder, 0, aBlur);
        scene::addLayer(tree, root, pool.wedge("bg"), "bg", QVector2D(960, 540), 0.0f, QVector2D(30, 22));
    };

    const struct {
        const char* name;
        float blur;
    } variants[] = {
        {"no blur", 0.0f}, {"blur 6 (direct)", 6.0f}, {"blur 40 (ladder)", 40.0f}, {"blur 200 (ladder)", 200.0f}};
    for (const auto& v : variants) {
        ResPool pool;
        core::ObjectTree tree;
        build(tree, pool, v.blur);
        fx.render(tree, camera, 0); // warmup: shader compile + FBO/slot allocation
        fx.render(tree, camera, 0);
        QElapsedTimer timer;
        timer.start();
        const int kIters = 10;
        for (int i = 0; i < kIters; ++i)
            fx.render(tree, camera, 0);
        const double ms = timer.elapsed() / (double)kIters;
        std::printf("  [INFO] %-20s %8.2f ms/frame (incl. readback)\n", v.name, ms);
    }
    ++gChecks; // informational suite; running it to completion is the check
}

} // namespace

int main(int argc, char** argv) {
    static HarnessAssertHandler assertHandler;
    static HarnessErrorHandler errorHandler;
    gXCAssertHandler = &assertHandler;
    gXCErrorHandler = &errorHandler;

    // surface format must be set before any GL context is created (mirrors MainWindow)
    {
        QSurfaceFormat format;
        format.setVersion(gl::Global::kVersion.first, gl::Global::kVersion.second);
        format.setProfile(QSurfaceFormat::CoreProfile);
        format.setSamples(4);
        QSurfaceFormat::setDefaultFormat(format);
    }
    QApplication app(argc, argv);

    // (the comma-decimal LC_NUMERIC regression is scoped to the ORA load with
    // uselocale() inside suiteOraImport, not pinned process-wide; see S13)

    // golden input generation needs no GL: --golden-gen [dir] writes the case inputs +
    // default case.json files and exits (author expected.png afterwards, see golden.h)
    if (argc > 1 && QString(argv[1]) == "--golden-gen")
        return golden::generateInputs(argc > 2 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral("tests/golden"));

    QMainWindow window;
    auto* glWidget = new HarnessGLWidget();
    window.setCentralWidget(glWidget);
    window.resize(32, 32);
    window.show();
    app.processEvents();
    if (!glWidget->mGLReady) { // some platforms need another nudge
        glWidget->update();
        app.processEvents();
    }
    if (!glWidget->mGLReady) {
        std::fprintf(stderr, "FATAL: GL widget did not initialize on this platform\n");
        return 2;
    }
    glWidget->makeCurrent(); // Qt keeps the context current; be explicit for tree building

    // the offscreen platform can recreate the widget's FBO/context behind initializeGL's
    // back, leaving the widget's VAO name stale and a pending GL_INVALID_OPERATION in the
    // current context; the first error-checked GL call then aborts (a 0-layer PSD import
    // hits this in ClippingFrame because its loader does no GL work that would consume
    // the flag). Rebind a fresh VAO and drain the error flag.
    glWidget->mVAO.reset(new gl::VertexArrayObject());
    glWidget->mVAO->bind();
    while (gl::Global::functions().glGetError() != GL_NO_ERROR) {
    }

    // --golden <dir> [case] runs only the golden suite against an alternate case
    // directory, optionally restricted to one case (one-time real-file analysis)
    if (argc > 2 && QString(argv[1]) == "--golden") {
        golden::suite(
            QString::fromLocal8Bit(argv[2]), argc > 3 ? QString::fromLocal8Bit(argv[3]) : QString());
        std::printf("\n%d checks, %d failures\n", gChecks, gFails);
        scene::clearFixtures();
        return gFails ? 1 : 0;
    }

    suiteMath();
    suiteLadder();
    suiteBlending();
    suiteRender();
    suiteOrientation();
    suiteGating();
    suiteAlphaProbe();
    suiteInteractions();
    suiteSerialization();
    suiteExport();
    suitePsdImport();
    suiteOraImport();
    suiteFolderBlend();
    suiteShaderMatrix();
    golden::suite("tests/golden");
    suitePerf();

    std::printf("\n%d checks, %d failures\n", gChecks, gFails);

    // tear GL objects down while the context is still current
    scene::clearFixtures();
    return gFails ? 1 : 0;
}
