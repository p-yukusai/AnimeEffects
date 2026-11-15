// Mipmap verification probe.
//
// WHAT THIS VERIFIES
//
// AnimeEffects samples layer artwork through a mipmapped texture (core/ImageKey), so that minified
// layers are filtered from a prefiltered chain instead of aliasing on level 0. Two conditions make
// that chain correct, and this probe measures both:
//
//  1. the DATA is alpha-premultiplied. glGenerateMipmap box-filters every channel independently, so
//     averaging STRAIGHT colour mixes the RGB stored under alpha 0 into the edge texels of every
//     level - the classic mipmap dark halo (and its mirror, the light halo). The layer shader
//     divides the coverage back out (PREMULTIPLIED_INPUT), so the blend math still sees straight
//     colour.
//  2. the GEOMETRY is the content's own: the artwork starts at the texture's texel (0, 0) and the
//     texture is the content's own size. Level l is a grid of 2^l-texel boxes anchored to multiples
//     of 2^l IN THE TEXTURE, so an import ring that moves the artwork to (1, 1) puts every level
//     from 1 up half a texel off its own blocks - a minified pixel is then answered by a box twice
//     as wide as the level asked for. The texture size and the uniform the vertex shader divides by
//     (uImageSize, set from areaTexture()->size()) have to be the same size for the same reason.
//
// WHAT IS THE GROUND TRUTH
//
// There is no third-party author to diff against here (Krita/GIMP do not do GL mip selection), so
// ground truth is ANALYTIC: the correct content of mip level l of a known input is the area-weighted
// average of the input over that texel's footprint, computed independently on the CPU - once for
// premultiplied colour (correct) and once for straight colour (the naive pipeline we must not ship).
// Each pattern is chosen so those two references are far apart, and the composited consequence over
// an opaque backdrop is known in closed form.
//
// THE TEST MUST BE ABLE TO FAIL
//
// A probe that only asserts "the GPU agrees with the premultiplied reference" passes just as happily
// if mipmaps are never generated, if the driver ignores the request, or if the levels past the exact
// halving are filled the way this driver fills them (a band of the source, not its footprint). So
// every case also:
//   - asserts the two references genuinely DIFFER (the discriminant is real, not vacuous),
//   - asserts the chain has more than one level and that level 1 is not a copy of level 0,
//   - renders the straight-alpha chain as a POSITIVE CONTROL that must exhibit the halo,
//   - and M18 compares a RAW glGenerateMipmap chain (the code path this project replaced, with no
//     rebuild) against the same analytic box, so the exactness assertion is shown to have power: the
//     driver's own level of a non-halving source is NOT that box.
//
// SUITES
//   M1  the generated chain vs the analytic CPU reference (production gl::Texture)
//   M2  the production core::ImageKey texture: content-sized, exact at an odd size, premultiplied
//   M18 chain exactness at EVERY level for a sweep of sizes, odd sides included, plus the raw
//       driver chain as the mutation control
//   M3  the production RENDER path: minified layer, no halo; and the 1:1 texel phase (UV path)
//
// Run: QT_QPA_PLATFORM=offscreen ./build/tests/Debug/verify_mipmap
// Exit code: 0 = all pass, 1 = test failures, 2 = GL bootstrap failure.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include <QApplication>
#include <QMainWindow>
#include <QOpenGLVersionFunctionsFactory>
#include <QOpenGLWidget>
#include <QScopedPointer>
#include <QSize>
#include <QString>
#include <QSurfaceFormat>
#include <QVector2D>

#include "XC.h"
#include "core/CameraInfo.h"
#include "core/ClippingFrame.h"
#include "core/DestinationTexturizer.h"
#include "core/FilterFrame.h"
#include "core/FolderNode.h"
#include "core/Frame.h"
#include "core/ImageKey.h"
#include "core/LayerNode.h"
#include "core/ObjectNode.h"
#include "core/ObjectTree.h"
#include "core/RenderInfo.h"
#include "core/ScaleKey.h"
#include "core/TimeInfo.h"
#include "core/TimeKeyType.h"
#include "gl/DeviceInfo.h"
#include "gl/Framebuffer.h"
#include "gl/Global.h"
#include "gl/Texture.h"
#include "gl/Util.h"
#include "gl/VertexArrayObject.h"
#include "gui/AudioPlaybackWidget.h"
#include "img/BlendMode.h"
#include "img/ResourceData.h"
#include "img/ResourceNode.h"
#include "util/Range.h"

//-------------------------------------------------------------------------------------------------
// reporting
namespace {

int gChecks = 0;
int gFails = 0;

bool expect(bool aCond, const QString& aWhat) {
    ++gChecks;
    if (!aCond) {
        ++gFails;
        std::printf("    [FAIL] %s\n", aWhat.toUtf8().constData());
    }
    return aCond;
}

void suiteHeader(const char* aName) { std::printf("== %s ==\n", aName); }

void caseReport(const QString& aName, bool aOk, const QString& aDetails) {
    std::printf(
        "  [%s] %-52s %s\n", aOk ? "PASS" : "FAIL", aName.toUtf8().constData(), aDetails.toUtf8().constData()
    );
}

bool near(double aValue, double aExpected, double aTol) { return std::fabs(aValue - aExpected) <= aTol; }

//-------------------------------------------------------------------------------------------------
// images (RGBA8, straight alpha, as the loaders deliver them)

struct Img {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> d;

    uint8_t* at(int x, int y) { return d.data() + ((size_t)y * w + x) * 4; }
    const uint8_t* at(int x, int y) const { return d.data() + ((size_t)y * w + x) * 4; }
    QSize size() const { return QSize(w, h); }
};

img::ResourceNode* makeResource(const QString& aId, const Img& aSrc);

// An input whose transparent region carries a chosen RGB: that value is what a straight chain
// averages into the silhouette, and what a premultiplied chain multiplies away.
struct Pattern {
    const char* name;
    uint8_t inside[3];  // RGB where alpha is opaque
    uint8_t outside[3]; // RGB under alpha 0 (the halo pigment)
};

const Pattern kPatterns[] = {
    {"white on transparent black (dark halo risk)", {255, 255, 255}, {0, 0, 0}},
    {"black on transparent white (light halo risk)", {0, 0, 0}, {255, 255, 255}},
};

// A checkerboard: every 2x2 block is exactly half covered, so level 1 is 50% coverage everywhere
// and the analytic value is exact rather than a specific-pixel accident.
Img makeChecker(const Pattern& aPat, int aSize) {
    Img img;
    img.w = img.h = aSize;
    img.d.assign((size_t)aSize * aSize * 4, 0);
    for (int y = 0; y < aSize; ++y) {
        for (int x = 0; x < aSize; ++x) {
            uint8_t* p = img.at(x, y);
            const bool on = ((x + y) % 2) == 0;
            const uint8_t* c = on ? aPat.inside : aPat.outside;
            p[0] = c[0];
            p[1] = c[1];
            p[2] = c[2];
            p[3] = on ? 255 : 0;
        }
    }
    return img;
}

// Opaque square inside, transparent surround: the realistic silhouette case.
Img makeSquare(const Pattern& aPat, int aSize, int aInset) {
    Img img;
    img.w = img.h = aSize;
    img.d.assign((size_t)aSize * aSize * 4, 0);
    for (int y = 0; y < aSize; ++y) {
        for (int x = 0; x < aSize; ++x) {
            uint8_t* p = img.at(x, y);
            const bool on = x >= aInset && x < aSize - aInset && y >= aInset && y < aSize - aInset;
            const uint8_t* c = on ? aPat.inside : aPat.outside;
            p[0] = c[0];
            p[1] = c[1];
            p[2] = c[2];
            p[3] = on ? 255 : 0;
        }
    }
    return img;
}

// Constant RGB in every texel, alpha a checkerboard: every level is constant RGB at coverage, which
// is what correctly prefiltered artwork looks like. A straight chain instead multiplies the RGB by
// the coverage, and the shader then multiplies by coverage again - the halo.
Img makeAlphaChecker(int aSize, const uint8_t aRgb[3]) {
    Img img;
    img.w = img.h = aSize;
    img.d.assign((size_t)aSize * aSize * 4, 0);
    for (int y = 0; y < aSize; ++y) {
        for (int x = 0; x < aSize; ++x) {
            uint8_t* p = img.at(x, y);
            p[0] = aRgb[0];
            p[1] = aRgb[1];
            p[2] = aRgb[2];
            p[3] = ((x + y) % 2 == 0) ? 255 : 0;
        }
    }
    return img;
}

// The same content already prefiltered by hand: the equivalence reference.
Img makeUniform(int aSize, const uint8_t aRgb[3], uint8_t aAlpha) {
    Img img;
    img.w = img.h = aSize;
    img.d.assign((size_t)aSize * aSize * 4, 0);
    for (int y = 0; y < aSize; ++y) {
        for (int x = 0; x < aSize; ++x) {
            uint8_t* p = img.at(x, y);
            p[0] = aRgb[0];
            p[1] = aRgb[1];
            p[2] = aRgb[2];
            p[3] = aAlpha;
        }
    }
    return img;
}

// A 1-texel checkerboard, opaque: at 1:1 the sampler reads every texel at its centre, so this comes
// back at full contrast ONLY if the texture coordinate path lands content (x, y) on texel (x, y).
// Any half-texel phase error (an import ring, a padded texture size, a mismatched uImageSize) blurs
// it into a flat grey.
Img makeOpaqueChecker(int aSize) {
    Img img;
    img.w = img.h = aSize;
    img.d.assign((size_t)aSize * aSize * 4, 0);
    for (int y = 0; y < aSize; ++y) {
        for (int x = 0; x < aSize; ++x) {
            uint8_t* p = img.at(x, y);
            const uint8_t v = ((x + y) % 2 == 0) ? 255 : 0;
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }
    }
    return img;
}

// Opaque content with curvature: a quadratic in x and y plus a product of sinusoids. Smooth content
// is what artwork is, and curvature is what makes the test have power - a LINEAR ramp integrates to
// the same average under any symmetric kernel, so a wrong channel or a displaced/truncated support
// would not show. (The step sweep below is the sharp instrument for support: it reads a hard edge.)
Img makeCurved(int aW, int aH) {
    Img img;
    img.w = aW;
    img.h = aH;
    img.d.assign((size_t)aW * aH * 4, 0);
    for (int y = 0; y < aH; ++y) {
        for (int x = 0; x < aW; ++x) {
            const double u = (x + 0.5) / aW;
            const double v = (y + 0.5) / aH;
            const auto clamp8 = [](double t) { return (uint8_t)std::lround(std::max(0.0, std::min(255.0, t))); };
            uint8_t* p = img.at(x, y);
            p[0] = clamp8(255.0 * u * u);
            p[1] = clamp8(255.0 * v * v);
            p[2] = clamp8(255.0 * (0.5 + 0.5 * std::sin(6.283185307179586 * u) * std::cos(4.0 * v)));
            p[3] = 255;
        }
    }
    return img;
}

//-------------------------------------------------------------------------------------------------
// the two independent CPU references

// What glGenerateMipmap does to STRAIGHT data: each channel averaged on its own.
Img downsampleStraight(const Img& aSrc) {
    Img dst;
    dst.w = std::max(1, aSrc.w / 2);
    dst.h = std::max(1, aSrc.h / 2);
    dst.d.assign((size_t)dst.w * dst.h * 4, 0);
    for (int y = 0; y < dst.h; ++y) {
        for (int x = 0; x < dst.w; ++x) {
            for (int c = 0; c < 4; ++c) {
                int sum = 0;
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx)
                        sum += aSrc.at(std::min(2 * x + dx, aSrc.w - 1), std::min(2 * y + dy, aSrc.h - 1))[c];
                dst.at(x, y)[c] = (uint8_t)((sum + 2) / 4); // round to nearest
            }
        }
    }
    return dst;
}

// Correct filtering of straight input: premultiply, average. Output is PREMULTIPLIED data,
// which is exactly what the shipped texture holds and what the shader's PREMULTIPLIED_INPUT
// branch expects, so it is directly comparable to a GPU readback of the premultiplied chain.
Img downsamplePremultiplied(const Img& aSrc) {
    Img dst;
    dst.w = std::max(1, aSrc.w / 2);
    dst.h = std::max(1, aSrc.h / 2);
    dst.d.assign((size_t)dst.w * dst.h * 4, 0);
    for (int y = 0; y < dst.h; ++y) {
        for (int x = 0; x < dst.w; ++x) {
            double pr = 0, pg = 0, pb = 0, pa = 0;
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    const uint8_t* s = aSrc.at(std::min(2 * x + dx, aSrc.w - 1), std::min(2 * y + dy, aSrc.h - 1));
                    const double a = s[3] / 255.0;
                    pr += s[0] * a;
                    pg += s[1] * a;
                    pb += s[2] * a;
                    pa += s[3];
                }
            }
            const auto clamp8 = [](double v) { return (uint8_t)std::lround(std::max(0.0, std::min(255.0, v))); };
            uint8_t* o = dst.at(x, y);
            o[0] = clamp8(pr / 4);
            o[1] = clamp8(pg / 4);
            o[2] = clamp8(pb / 4);
            o[3] = clamp8(pa / 4);
        }
    }
    return dst;
}

// The area-weighted resample of one level onto another grid covering the same span: destination
// texel j covers [j/dst, (j+1)/dst] of that span, so it is the average of the source texels it
// overlaps, with the fractional weights at the ends. This is the DEFINITION of a mip level (the box
// average of the level above), written out independently of gl/Texture.cpp's implementation.
std::vector<std::vector<std::pair<int, double>>> axisWeights(int aSrc, int aDst) {
    std::vector<std::vector<std::pair<int, double>>> w(aDst);
    for (int j = 0; j < aDst; ++j) {
        const double lo = (double)j * aSrc / aDst;
        const double hi = (double)(j + 1) * aSrc / aDst;
        const int i0 = (int)std::floor(lo);
        const int i1 = std::min(aSrc - 1, (int)std::ceil(hi) - 1);
        for (int i = i0; i <= i1; ++i) {
            const double overlap = std::min(hi, (double)i + 1.0) - std::max(lo, (double)i);
            if (overlap > 0.0)
                w[j].emplace_back(i, overlap / (hi - lo));
        }
    }
    return w;
}

std::vector<uint8_t> areaLevel(
    const std::vector<uint8_t>& aSrc, int aSrcW, int aSrcH, int aDstW, int aDstH
) {
    const auto wx = axisWeights(aSrcW, aDstW);
    const auto wy = axisWeights(aSrcH, aDstH);

    std::vector<double> rows((size_t)aDstW * aSrcH * 4, 0.0);
    for (int y = 0; y < aSrcH; ++y) {
        for (int j = 0; j < aDstW; ++j) {
            double acc[4] = {0.0, 0.0, 0.0, 0.0};
            for (const auto& t : wx[j]) {
                const uint8_t* p = aSrc.data() + ((size_t)y * aSrcW + t.first) * 4;
                for (int c = 0; c < 4; ++c)
                    acc[c] += p[c] * t.second;
            }
            for (int c = 0; c < 4; ++c)
                rows[((size_t)y * aDstW + j) * 4 + c] = acc[c];
        }
    }

    std::vector<uint8_t> dst((size_t)aDstW * aDstH * 4, 0);
    for (int i = 0; i < aDstH; ++i) {
        for (int x = 0; x < aDstW; ++x) {
            double acc[4] = {0.0, 0.0, 0.0, 0.0};
            for (const auto& t : wy[i]) {
                const double* p = rows.data() + ((size_t)t.first * aDstW + x) * 4;
                for (int c = 0; c < 4; ++c)
                    acc[c] += p[c] * t.second;
            }
            uint8_t* q = dst.data() + ((size_t)i * aDstW + x) * 4;
            for (int c = 0; c < 4; ++c)
                q[c] = (uint8_t)std::min(255.0, std::max(0.0, std::floor(acc[c] + 0.5)));
        }
    }
    return dst;
}

// The composited consequence over an opaque black backdrop, for a texture that holds
// PREMULTIPLIED data: the shader divides the coverage out (PREMULTIPLIED_INPUT) and the
// straight-alpha blend multiplies it back in, so the visible value is simply the stored
// premultiplied RGB. A correct chain therefore shows the source color scaled by coverage.
double overBlackPremultiplied(const uint8_t* aPx) { return aPx[0]; }

// ...and for a texture that holds STRAIGHT data (the naive pipeline): the blend multiplies
// the already-averaged color by the averaged coverage a second time.
double overBlackStraight(const uint8_t* aPx) { return aPx[0] * (aPx[3] / 255.0); }

//-------------------------------------------------------------------------------------------------
// GPU readback of a mip level

std::vector<uint8_t> readLevel(GLuint aTex, int aLevel, const QSize& aSize) {
    std::vector<uint8_t> buf((size_t)aSize.width() * aSize.height() * 4, 0);
    auto& ggl = gl::Global::functions();
    ggl.glBindTexture(GL_TEXTURE_2D, aTex);
    ggl.glGetTexImage(GL_TEXTURE_2D, aLevel, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
    ggl.glBindTexture(GL_TEXTURE_2D, 0);
    return buf;
}

// The premultiplied upload of a source, done here rather than through the product's helper so that
// M1/M18 do not grade the product against its own premultiply (M2 grades that).
Img premultipliedUpload(const Img& aSrc) {
    Img upload = aSrc;
    for (size_t i = 0; i < upload.d.size(); i += 4) {
        const uint32_t a = upload.d[i + 3];
        if (a == 255)
            continue;
        upload.d[i + 0] = (uint8_t)((upload.d[i + 0] * a + 127) / 255);
        upload.d[i + 1] = (uint8_t)((upload.d[i + 1] * a + 127) / 255);
        upload.d[i + 2] = (uint8_t)((upload.d[i + 2] * a + 127) / 255);
    }
    return upload;
}

// Reads the whole chain the product builds, as one Img per level.
std::vector<Img> readChain(const Img& aSrc, bool aPremultiply) {
    const Img upload = aPremultiply ? premultipliedUpload(aSrc) : aSrc;

    gl::Texture tex;
    tex.create(
        upload.size(), upload.d.data(), GL_RGBA, GL_RGBA8, GL_UNSIGNED_BYTE, gl::Texture::GenerateMipmap
    );

    std::vector<Img> levels;
    for (int l = 0; l < tex.mipmapLevels(); ++l) {
        Img img;
        img.w = std::max(1, upload.w >> l);
        img.h = std::max(1, upload.h >> l);
        img.d = readLevel(tex.id(), l, QSize(img.w, img.h));
        levels.push_back(std::move(img));
    }
    return levels;
}

// max |a - b| over one channel, in 8-bit units
int maxChannelDelta(const std::vector<uint8_t>& aA, const std::vector<uint8_t>& aB, int aChannel) {
    int worst = 0;
    for (size_t i = (size_t)aChannel; i < aA.size() && i < aB.size(); i += 4)
        worst = std::max(worst, std::abs((int)aA[i] - (int)aB[i]));
    return worst;
}

// ...and over every channel. Needed for "is this level genuinely different from that one":
// for a premultiplied chain the discriminating channel can be ALPHA alone (a bright shape on
// a bright transparent region premultiplies to rgb 0 at both levels, so the color channels
// agree while coverage halves).
int maxAnyChannelDelta(const std::vector<uint8_t>& aA, const std::vector<uint8_t>& aB) {
    int worst = 0;
    for (int c = 0; c < 4; ++c)
        worst = std::max(worst, maxChannelDelta(aA, aB, c));
    return worst;
}

//-------------------------------------------------------------------------------------------------
// M1: is the generated chain the premultiplied one (and not the straight one)?

void verifyPattern(const Pattern& aPat, int aSize, bool aChecker) {
    // The inset is deliberately ODD: with even-aligned edges every 2x2 block of level 1
    // falls entirely inside or entirely outside the shape, so level 1 would carry no
    // partial coverage and the case could not distinguish the two pipelines at all.
    const Img src = aChecker ? makeChecker(aPat, aSize) : makeSquare(aPat, aSize, aSize / 4 + 1);

    // Independent CPU references for level 1, each in its own representation:
    //   refStraight - what averaging STRAIGHT data yields, held as straight data
    //   refPremult  - what averaging PREMULTIPLIED data yields, held premultiplied
    const Img refStraight = downsampleStraight(src);
    const Img refPremult = downsamplePremultiplied(src);

    // The invariant the halo violates: over an opaque black backdrop, a correct chain shows
    // the source's own opaque color scaled by coverage - never the color of the transparent
    // region. For a chain that keeps straight data, the same source shows
    // rgb*a, i.e. the silhouette color is dragged toward the transparent region's color.
    const double inside = aPat.inside[0];
    const auto expectedVisible = [&](const uint8_t* aPx) { return inside * (aPx[3] / 255.0); };

    // The two pipelines must genuinely disagree, or the case proves nothing.
    double maxDiscriminant = 0.0;
    for (size_t i = 0; i < refStraight.d.size(); i += 4) {
        const double correct = overBlackPremultiplied(refPremult.d.data() + i);
        const double naive = overBlackStraight(refStraight.d.data() + i);
        maxDiscriminant = std::max(maxDiscriminant, std::fabs(correct - naive));
    }

    // alpha is coverage and must be identical whichever way color is carried
    const int alphaDelta = maxChannelDelta(refStraight.d, refPremult.d, 3);

    const std::vector<Img> gpuPremult = readChain(src, true);
    const std::vector<Img> gpuStraight = readChain(src, false);
    const bool haveChain = gpuPremult.size() > 1 && gpuStraight.size() > 1;

    int premultRawDelta = 999;
    int straightRawDelta = 999;
    double worstPremultHalo = 999.0;
    double worstStraightHalo = 0.0;
    if (haveChain) {
        premultRawDelta = maxChannelDelta(gpuPremult[1].d, refPremult.d, 0);
        straightRawDelta = maxChannelDelta(gpuStraight[1].d, refStraight.d, 0);
        worstPremultHalo = 0.0;
        for (size_t i = 0; i < gpuPremult[1].d.size(); i += 4) {
            const uint8_t* pp = gpuPremult[1].d.data() + i;
            const uint8_t* sp = gpuStraight[1].d.data() + i;
            worstPremultHalo = std::max(worstPremultHalo, std::fabs(overBlackPremultiplied(pp) - expectedVisible(pp)));
            worstStraightHalo = std::max(worstStraightHalo, std::fabs(overBlackStraight(sp) - expectedVisible(sp)));
        }
    }

    const QString prefix = QString("%1 [%2]").arg(aPat.name).arg(aChecker ? "checker" : "square");
    const int failsBefore = gFails;

    expect(maxDiscriminant > 30.0,
        QString("%1: the two pipelines must differ (max %2) - otherwise the case is vacuous")
            .arg(prefix).arg(maxDiscriminant, 0, 'f', 1));
    expect(alphaDelta == 0,
        QString("%1: coverage must be identical in both references (delta %2)").arg(prefix).arg(alphaDelta));
    expect(haveChain, QString("%1: a mip chain must exist").arg(prefix));

    // the GPU must build the premultiplied chain, and not the straight one
    expect(premultRawDelta <= 3,
        QString("%1: GPU chain must match the premultiplied reference (delta %2)").arg(prefix).arg(premultRawDelta));
    expect(straightRawDelta <= 3,
        QString("%1: the straight control chain must match its reference (delta %2)")
            .arg(prefix).arg(straightRawDelta));

    // THE HALO ASSERTION: the shipped chain preserves the silhouette color...
    expect(worstPremultHalo <= 4.0,
        QString("%1: no halo - visible color must stay source*coverage (worst deviation %2)")
            .arg(prefix).arg(worstPremultHalo, 0, 'f', 1));
    // ...and the positive control must fail it, proving the assertion has power
    expect(worstStraightHalo > 30.0,
        QString("%1: the straight control must exhibit the halo (worst deviation %2)")
            .arg(prefix).arg(worstStraightHalo, 0, 'f', 1));

    caseReport(
        prefix,
        gFails == failsBefore,
        QString("discriminant=%1 | shipped halo=%2 straight halo=%3")
            .arg(maxDiscriminant, 0, 'f', 1)
            .arg(worstPremultHalo, 0, 'f', 1)
            .arg(worstStraightHalo, 0, 'f', 1)
    );
}

void suiteChain() {
    suiteHeader("M1 mip chain generation vs analytic CPU reference");
    const int size = 64;

    for (const Pattern& pat : kPatterns) {
        verifyPattern(pat, size, true);
        verifyPattern(pat, size, false);
    }

    // the chain must be complete: level 0 down to 1x1, all levels present
    const Img src = makeChecker(kPatterns[0], size);
    const std::vector<Img> chain = readChain(src, true);
    const int expectedLevels = 1 + (int)std::floor(std::log2((double)size));
    const QString prefix = "chain completeness";
    const int failsBefore = gFails;
    expect((int)chain.size() == expectedLevels,
        QString("%1: expected %2 levels, got %3").arg(prefix).arg(expectedLevels).arg(chain.size()));
    expect(!chain.empty() && chain.back().w == 1 && chain.back().h == 1,
        QString("%1: the last level must be 1x1").arg(prefix));
    // a full-resolution-sourced checkerboard averages to a constant at every level: the
    // convergence of the chain is itself a check that levels are not stale or uninitialised
    if (chain.size() >= 4) {
        double sum = 0;
        const Img& l3 = chain[3];
        for (int y = 0; y < l3.h; ++y)
            for (int x = 0; x < l3.w; ++x)
                sum += l3.at(x, y)[3];
        const double meanAlpha = sum / (l3.w * l3.h);
        expect(near(meanAlpha, 127.5, 2.0),
            QString("%1: level 3 mean coverage must stay ~50%% (got %2)").arg(prefix).arg(meanAlpha, 0, 'f', 1));
    }
    caseReport(prefix, gFails == failsBefore, QString("%1 levels").arg(chain.size()));
}

//-------------------------------------------------------------------------------------------------
// M2: the production core::ImageKey texture must be the content's own size, and its chain - the
// tail gl::Texture::create builds on the GPU included - must be the analytic area average.
//
// 129x77 is the interesting shape: an odd width, and no power of two in either side (129 = 3 * 43,
// 77 = 7 * 11), so the deepest level is 1x1 over a 129-texel base. The size assertion is what keeps
// the alignment padding from coming back - aligning to 128 would have uploaded 256x128.

void suiteImageKey() {
    suiteHeader("M2 production core::ImageKey texture: content-sized, exact at an odd size");

    const QSize size(129, 77);
    const Img src = makeCurved(size.width(), size.height());
    img::ResourceNode* node = makeResource("mip_probe_geometry", src);

    core::ImageKey key;
    key.setImage(node->handle());
    gl::Texture& tex = key.cache().texture();

    const QString prefix = "ImageKey texture";
    const int failsBefore = gFails;

    expect(tex.id() != 0, QString("%1: must be created").arg(prefix));
    expect(tex.size() == size,
        QString("%1: the texture must be the content's own size (%2x%3), got %4x%5 - padding it puts "
                "level 0's texels off the grid the level-l boxes divide into and costs up to +98% of "
                "a thin layer")
            .arg(prefix).arg(size.width()).arg(size.height())
            .arg(tex.size().width()).arg(tex.size().height()));

    const int expectedLevels = 1 + (int)std::floor(std::log2((double)std::max(size.width(), size.height())));
    expect(tex.mipmapLevels() == expectedLevels,
        QString("%1: %2 levels expected, got %3").arg(prefix).arg(expectedLevels).arg(tex.mipmapLevels()));

    // the sampling parameters the path depends on
    {
        auto& ggl = gl::Global::functions();
        ggl.glBindTexture(GL_TEXTURE_2D, tex.id());
        GLint minF = 0, magF = 0, maxLevel = -1, wrapS = 0;
        ggl.glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &minF);
        ggl.glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &magF);
        ggl.glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, &maxLevel);
        ggl.glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &wrapS);
        ggl.glBindTexture(GL_TEXTURE_2D, 0);
        expect(minF == GL_LINEAR_MIPMAP_LINEAR,
            QString("%1: minification must be trilinear (got 0x%2)").arg(prefix).arg(minF, 0, 16));
        expect(magF == GL_LINEAR, QString("%1: magnification must be GL_LINEAR (got 0x%2)").arg(prefix).arg(magF, 0, 16));
        expect(maxLevel == expectedLevels - 1,
            QString("%1: GL_TEXTURE_MAX_LEVEL must be %2 (got %3)").arg(prefix).arg(expectedLevels - 1).arg(maxLevel));
        expect(wrapS == GL_CLAMP_TO_BORDER, QString("%1: wrap must stay CLAMP_TO_BORDER").arg(prefix));
    }

    // Every level against the analytic area average of the premultiplied upload. gl::Texture::create
    // follows the reach-back rule (gl::Texture::mipSourceLevel): levels 1..3 are built from level 0
    // and are exactness assertions, while level 4 and above chain from level - 3 - an 8:1 step whose
    // source texels are themselves averages, exact only where the step tiles and its source level is
    // exact. Those levels are held to the same 3/255 here, which this 129x77 content meets.
    const Img upload = premultipliedUpload(src);
    int worstAll = 0;
    for (int l = 1; l < tex.mipmapLevels(); ++l) {
        const int dw = std::max(1, size.width() >> l);
        const int dh = std::max(1, size.height() >> l);
        const std::vector<uint8_t> got = readLevel(tex.id(), l, QSize(dw, dh));
        const std::vector<uint8_t> want = areaLevel(upload.d, size.width(), size.height(), dw, dh);
        const int delta = maxAnyChannelDelta(got, want);
        worstAll = std::max(worstAll, delta);
        expect(delta <= 3,
            QString("%1: level %2 (%3x%4) must be the area average of the premultiplied content (delta %5)")
                .arg(prefix).arg(l).arg(dw).arg(dh).arg(delta));
    }

    // The premultiply itself, on the pattern that can see it: transparent texels carrying WHITE.
    // (With transparent BLACK the two chains are numerically identical - premultiplying an opaque
    // texel is the identity and the transparent one contributes nothing either way - so a bright
    // shape on transparent black cannot see a missing premultiply at all.) Level 1 of a 129-texel
    // side is a ~3-texel footprint from level 0, i.e. exact.
    {
        const Pattern& pat = kPatterns[1]; // black on transparent white
        const int side = 129;
        const Img checker = makeChecker(pat, side);
        img::ResourceNode* checkerNode = makeResource("mip_probe_premultiply", checker);
        core::ImageKey checkerKey;
        checkerKey.setImage(checkerNode->handle());

        const int dw = std::max(1, side >> 1);
        const std::vector<uint8_t> got = readLevel(checkerKey.cache().texture().id(), 1, QSize(dw, dw));
        const std::vector<uint8_t> wantPremult =
            areaLevel(premultipliedUpload(checker).d, side, side, dw, dw);
        const std::vector<uint8_t> wantStraight = areaLevel(checker.d, side, side, dw, dw);
        const int vsPremult = maxAnyChannelDelta(got, wantPremult);
        const int vsStraight = maxAnyChannelDelta(got, wantStraight);

        expect(vsPremult <= 3,
            QString("ImageKey premultiply: level 1 must be the premultiplied chain (delta %2)")
                .arg(vsPremult));
        expect(vsStraight > 30,
            QString("ImageKey premultiply: this pattern's straight chain must differ, or the check "
                    "above is vacuous (delta %2)").arg(vsStraight));
        expect(vsPremult < vsStraight,
            QString("ImageKey premultiply: level 1 must be the premultiplied chain, not the straight "
                    "one (deltas %2 vs %3)").arg(vsPremult).arg(vsStraight));
    }

    caseReport(
        prefix,
        gFails == failsBefore,
        QString("%1x%2 at %3x%4, %5 levels, worst level delta %6")
            .arg(tex.size().width()).arg(tex.size().height())
            .arg(size.width()).arg(size.height())
            .arg(tex.mipmapLevels())
            .arg(worstAll)
    );
}

//-------------------------------------------------------------------------------------------------
// M18: is the chain the exact box at levels 1..3, and close to it below, for every size - odd sides
// included?
//
// Level l has max(1, side >> l) texels spanning the whole [0, 1] of normalized coordinates, so it is
// an exact grid of 2^l-texel boxes only while 2^l divides the side. Past that the level must still be
// the area average of its own footprint, and that is what gl::Texture::create builds: from level 0
// while l <= gl::Texture::kMipChainSpan (levels 1..3, exact for any size), and from level l - 3 after
// that - an 8:1 step whose source texels are themselves averages, so those levels are held to a wider
// bound: they are the exact box only where the step tiles and its source level is exact too.
//
// This driver's own glGenerateMipmap does NOT do that with a source that does not halve, and the
// mutation control below measures it: a step swept through a 384-texel side, level 8, read out of a
// RAW chain (plain GL, no tail builder) weights only rows [128, 256) - the middle third - and none of
// the rest. A truncated support makes the level the wrong COLOUR, not merely a soft one, so the
// sweep's worst deviation from the box is what a layer at that zoom is wrong by per unit of contrast:
// -83.7 of 255 on this driver for the 384 side, -117.5 at level 10 of 1920.
//
// Sizes: a power of two (nothing to build), sizes with 1 and 2 trailing zero bits, and odd 129/300/
// 501/513 sides (no exact 2:1 step to inherit: levels 1..3 are area steps from level 0, and level 4
// on chains from level - 3).

// The reach-back rule is gl::Texture's, not the probe's (gl/Texture.h: kMipChainSpan and
// mipSourceLevel) - one constant, one comment, so the assertion and the builder cannot drift.

// The level aLev of a step image (rows [0, aRow) white, the rest black), read at level texel (0, 0):
// 255 * the weight the level gives the rows above the step. aRaw builds the chain with plain GL
// (the driver's own levels); otherwise the product's gl::Texture::create builds it.
double levelStep(int aSide, int aLev, int aWidth, int aRow, bool aRaw) {
    Img src;
    src.w = aWidth;
    src.h = aSide;
    src.d.assign((size_t)aWidth * aSide * 4, 0);
    for (int y = 0; y < aSide; ++y) {
        const uint8_t v = (y < aRow) ? 255 : 0;
        for (int x = 0; x < aWidth; ++x) {
            uint8_t* p = src.at(x, y);
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }
    }

    if (!aRaw) {
        return (double)readChain(src, true)[(size_t)aLev].at(0, 0)[0];
    }

    auto& ggl = gl::Global::functions();
    GLuint tex = 0;
    ggl.glGenTextures(1, &tex);
    ggl.glBindTexture(GL_TEXTURE_2D, tex);
    ggl.glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGBA8, src.w, src.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, src.d.data()
    );
    ggl.glGenerateMipmap(GL_TEXTURE_2D);
    uint8_t px[4] = {0, 0, 0, 0};
    ggl.glGetTexImage(GL_TEXTURE_2D, aLev, GL_RGBA, GL_UNSIGNED_BYTE, px);
    ggl.glBindTexture(GL_TEXTURE_2D, 0);
    ggl.glDeleteTextures(1, &tex);
    return (double)px[0];
}

// The worst deviation of a level's weight profile from the box its grid promises, over a step swept
// through the side. The ideal profile is 255 * clamp(r / nominalRows, 0, 1), where nominalRows is
// the number of base rows the level's texels stand for; a level that weights a band of its source
// stalls where the ideal keeps rising, which is the defect this measures.
struct StepProfile {
    double worstDev = 0.0;
    int worstAt = -1;
    double midpoint = -1.0;
    bool monotone = true;
};

StepProfile scanStep(int aSide, int aLev, int aWidth, bool aRaw) {
    StepProfile p;
    // The level has max(1, side >> lev) texels spanning the whole [0, 1] of normalized coordinates,
    // so ONE level texel stands for side / texels BASE ROWS - not for `side >> lev` rows. (The
    // latter is the texel COUNT: using it makes the ideal saturate at r = 1, which reports a
    // correct chained level as -253 off and makes the raw-driver control unable to fail.)
    const int nominalRows = aSide / std::max(1, aSide >> aLev);
    const int step = std::max(1, aSide / 128);
    double prev = -1.0;
    for (int r = 0; r <= aSide; r += step) {
        const double v = levelStep(aSide, aLev, aWidth, r, aRaw);
        const double ideal = 255.0 * std::min(1.0, r / (double)nominalRows);
        if (std::fabs(v - ideal) > std::fabs(p.worstDev)) {
            p.worstDev = v - ideal;
            p.worstAt = r;
        }
        if (prev >= 0.0) {
            if (v < prev - 1.0)
                p.monotone = false;
            if (p.midpoint < 0.0 && ((prev < 127.5 && v >= 127.5) || (prev > 127.5 && v <= 127.5))) {
                const double t = (prev == v) ? 0.5 : (127.5 - prev) / (v - prev);
                p.midpoint = (r - step) + t * step;
            }
        }
        prev = v;
    }
    return p;
}

void suiteChainExactness() {
    suiteHeader("M18 chain exactness at every level, odd sizes included");

    const int kSizes[] = {64, 100, 129, 300, 384, 501, 513};
    for (size_t si = 0; si < sizeof(kSizes) / sizeof(kSizes[0]); ++si) {
        const int size = kSizes[si];
        const Img src = makeCurved(size, size);
        const std::vector<Img> chain = readChain(src, true);
        const int expectedLevels = 1 + (int)std::floor(std::log2((double)size));

        const QString label = QString("M18 %1x%1").arg(size);
        const int failsBefore = gFails;

        expect((int)chain.size() == expectedLevels,
            QString("%1: %2 levels expected, got %3").arg(label).arg(expectedLevels).arg(chain.size()));

        int worstExact = 0;
        int firstChained = -1;
        for (int l = 1; l < (int)chain.size(); ++l) {
            const int dw = std::max(1, size >> l);
            const int dh = dw;
            const int taps = std::max((size + dw - 1) / dw, (size + dh - 1) / dh);
            const int srcLevel = gl::Texture::mipSourceLevel(l);
            const std::vector<uint8_t> want = areaLevel(src.d, size, size, dw, dh);
            const int delta = maxAnyChannelDelta(chain[(size_t)l].d, want);
            if (srcLevel == 0) {
                // base-sourced: this is an exactness assertion
                worstExact = std::max(worstExact, delta);
                expect(delta <= 3,
                    QString("%1: level %2 (%3x%4, %5 taps from level 0) must be the area average of "
                            "its own footprint (delta %6)")
                        .arg(label).arg(l).arg(dw).arg(dh).arg(taps).arg(delta));
            } else {
                // chained: the source is level - 3, whose texels are themselves averages, so the
                // cut ones are a sub-texel approximation and the bound is wider (a few units here)
                if (firstChained < 0)
                    firstChained = l;
                expect(delta <= 8,
                    QString("%1: level %2 (%3x%4, chained from level %6) must stay close to the "
                            "area average of the base (delta %5)")
                        .arg(label).arg(l).arg(dw).arg(dh).arg(delta).arg(srcLevel));
            }
        }

        caseReport(
            label, gFails == failsBefore,
            QString("%1 levels, %2 exact from level 0 (worst delta %3)%4")
                .arg(chain.size())
                .arg(firstChained < 0 ? chain.size() - 1 : firstChained - 1)
                .arg(worstExact)
                .arg(firstChained < 0 ? QString(" - every level")
                                     : QString(", chained from level %1").arg(firstChained))
        );
    }

    // ---- THE SHIPPED CHAIN, at the two levels that test the two halves of the rule.
    //
    // THE EXACT HALF: 384 = 3 * 2^7, so level 4 of a 384 side is 24 texels built from level 1 (192
    // texels) - an 8:1 step that TILES EXACTLY, and level 1 is itself the exact 2:1 box of level 0.
    // The composition is therefore the exact box of the base, and the strict bound (<= 1) is the
    // contract here. The precondition (source level exact, step tiling) is derived, not assumed.
    //
    // THE APPROXIMATE HALF: level 8 of the same side is one texel built from level 5 (12 texels,
    // itself chained from level 2), and 12 onto 1 tiles exactly - yet the result is NOT the box of
    // the base, because level 5 is not: the deviation measured here (2.9 of 255) is the chained
    // source's own error, which is the price kMipChainSpan documents. The bound is that measurement
    // with headroom, so a regression in the reach-back shows up.
    {
        const int side = 384;
        struct Half {
            int level;
            double bound;
            const char* what;
        };
        // With span 1 the levels below 8 are chained AND tightened, so level 8 is no longer the box of
        // the base - by design: kMipStepShrink trades that identity for rendered sharpness. The bound
        // here is the measurement with headroom, so a change in the shrink or the span shows up.
        const Half halves[] = {{gl::Texture::kMipChainSpan + 1, 1.0, "source exact, step tiles"},
                               {8, 15.0, "source itself chained and tightened"}};
        for (size_t hi = 0; hi < sizeof(halves) / sizeof(halves[0]); ++hi) {
            const int lev = halves[hi].level;
            const int srcLevel = gl::Texture::mipSourceLevel(lev);
            const int srcTexels = std::max(1, side >> srcLevel);
            const int dstTexels = std::max(1, side >> lev);
            const bool tilesExactly = (srcTexels % dstTexels) == 0;
            const bool sourceExact = srcLevel == 0 || srcLevel <= gl::Texture::kMipChainSpan;
            const int nominalRows = side / dstTexels;
            const QString label = QString("M18 shipped chain, %1 square level %2").arg(side).arg(lev);

            const int failsBefore = gFails;
            expect(tilesExactly,
                QString("%1: both halves need the step to tile exactly (%2 texels of level %3 onto "
                        "%4 of level %5)").arg(label)
                    .arg(srcTexels).arg(srcLevel).arg(dstTexels).arg(lev));
            if (halves[hi].bound <= 1.0) {
                expect(sourceExact,
                    QString("%1: the strict box assertion also needs an exact source level (level %2)")
                        .arg(label).arg(srcLevel));
            }

            const StepProfile shipped = scanStep(side, lev, 8, false);
            expect(shipped.monotone,
                QString("%1: the weight profile must rise monotonically").arg(label));
            expect(std::fabs(shipped.worstDev) <= halves[hi].bound,
                QString("%1: deviation from the box must stay within %2 (%3) - worst %4 at r=%5, "
                        "midpoint %6")
                    .arg(label).arg(halves[hi].bound, 0, 'f', 1).arg(halves[hi].what)
                    .arg(shipped.worstDev, 0, 'f', 1).arg(shipped.worstAt)
                    .arg(shipped.midpoint, 0, 'f', 1));

            // flat content must reduce to itself: no halo, dark or bright, from the level's shape
            Img flat;
            flat.w = flat.h = side;
            flat.d.assign((size_t)side * side * 4, 0);
            for (int y = 0; y < side; ++y)
                for (int x = 0; x < side; ++x) {
                    uint8_t* q = flat.at(x, y);
                    q[0] = q[1] = q[2] = 200;
                    q[3] = 255;
                }
            // the chain must outlive the pointer into its pixels: readChain returns a vector BY
            // VALUE, and binding the pointer straight out of the temporary (as this line used to)
            // read freed memory - a uniform field cannot produce a non-uniform value under any area
            // weighting, so the garbage showed up as "rgb 196, alpha 17".
            const std::vector<Img> flatChain = readChain(flat, true);
            const uint8_t* deep = flatChain[(size_t)lev].at(0, 0);
            expect(std::abs((int)deep[0] - 200) <= 1 && deep[3] >= 254,
                QString("%1: uniform content must reduce to itself (rgb %2, alpha %3)")
                    .arg(label).arg((int)deep[0]).arg((int)deep[3]));

            caseReport(
                label, gFails == failsBefore,
                QString("footprint %1 rows, midpoint at r=%2, worst deviation from the box %3 at "
                        "r=%4 (%5)")
                    .arg(nominalRows).arg(shipped.midpoint, 0, 'f', 1)
                    .arg(shipped.worstDev, 0, 'f', 1).arg(shipped.worstAt)
                    .arg(halves[hi].what));
        }
    }

    // ---- WHAT THE CHAIN IS FAITHFUL TO, and what it necessarily is not. Two analytic images, so the
    // assertions are statements about the resampler rather than about a photograph:
    //
    //   a piecewise-LINEAR ramp - the area average of a straight line is the value at the footprint's
    //     midpoint, whatever the ratio, so a chain of exact steps must reproduce it. This is what
    //     separates "the chain's weighting is wrong" from "the chain lost information": on this
    //     content a correct chain and the exact level agree to rounding at EVERY level.
    //   a hard EDGE - where the base has a discontinuity a coarse texel straddles it, and the chain
    //     can only hand over that texel's average (it has nothing finer). The difference is therefore
    //     confined to the texels that straddle the edge, and its size is the weight of the part of
    //     the texel that lies on the other side: up to half a coarse texel at a 2:1 step, an eighth at
    //     8:1 - which is why reach-back 3 measures a few units here where reach-back 1 measures tens.
    {
        const int w = 1938, h = 512; // non-dividing by 4 in x, so steps above level 1 do not tile
        const QString label = QString("M18 content faithfulness, %1x%2").arg(w).arg(h);
        const int failsBefore = gFails;

        Img ramp;
        ramp.w = w; ramp.h = h;
        ramp.d.assign((size_t)w * h * 4, 255);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const uint8_t v = (uint8_t)std::lround(x * 255.0 / (w - 1));
                uint8_t* q = ramp.at(x, y);
                q[0] = q[1] = q[2] = v;
            }
        const std::vector<Img> rampChain = readChain(ramp, true);

        Img edge;
        edge.w = w; edge.h = h;
        edge.d.assign((size_t)w * h * 4, 0);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                uint8_t* q = edge.at(x, y);
                q[0] = q[1] = q[2] = (x >= w / 2) ? 255 : 0;
                q[3] = 255;
            }
        const std::vector<Img> edgeChain = readChain(edge, true);

        int worstRamp = 0;
        int worstEdge = 0;
        int strayedEdgeTexels = 0;
        for (int l = 1; l < (int)rampChain.size(); ++l) {
            const int dw = std::max(1, w >> l);
            const int dh = std::max(1, h >> l);
            worstRamp = std::max(worstRamp, maxAnyChannelDelta(rampChain[(size_t)l].d,
                                                               areaLevel(ramp.d, w, h, dw, dh)));

            const std::vector<uint8_t> want = areaLevel(edge.d, w, h, dw, dh);
            worstEdge = std::max(worstEdge, maxAnyChannelDelta(edgeChain[(size_t)l].d, want));
            // every texel that differs must be within ONE texel of the edge: a chain has no finer
            // information than its source, so the error can never spread further than that
            const int edgeCol = w / 2 / std::max(1, w / dw);
            for (int y = 0; y < dh; ++y)
                for (int x = 0; x < dw; ++x) {
                    const uint8_t* got = edgeChain[(size_t)l].at(x, y);
                    const uint8_t* ref = &want[((size_t)y * dw + x) * 4];
                    for (int c = 0; c < 4; ++c)
                        if (std::abs((int)got[c] - (int)ref[c]) > 2 && std::abs(x - edgeCol) > 2)
                            ++strayedEdgeTexels;
                }
        }

        // IS THE REFERENCE ITSELF EXACT? For a straight ramp the area average over a footprint is
        // the analytic value at that footprint's MIDPOINT, independent of the weighting code. Checked
        // against that closed form, not against another implementation of the same idea.
        int worstAnalytic = 0;
        for (int l = 1; l <= 5; ++l) {
            const int dw = std::max(1, w >> l);
            const std::vector<uint8_t> got = areaLevel(ramp.d, w, h, dw, std::max(1, h >> l));
            for (int x = 0; x < dw; ++x) {
                const double lo = (double)x * w / dw;
                const double hi = (double)(x + 1) * w / dw;
                const double analytic = 255.0 / (w - 1) * (lo + hi) / 2.0;
                worstAnalytic = std::max(worstAnalytic, (int)std::lround(std::fabs(got[x * 4] - analytic)));
            }
        }
        expect(worstAnalytic <= 1,
            QString("%1: the exact level must equal the ANALYTIC area average of a ramp (worst %2)")
                .arg(label).arg(worstAnalytic));

        // a straight ramp survives the chain: the midpoint rule is exact whatever the ratio, so the
        // only error left is the slope times the fraction of a SOURCE texel that a straddling texel
        // hands over (255 / sourceWidth, so a few units at the deepest levels) plus rounding
        expect(worstRamp <= 8,
            QString("%1: a linear ramp must survive the chain to a few units at every level (worst %2)")
                .arg(label).arg(worstRamp));
        // a hard edge may not: the straddling texel comes over whole, up to half its width at 2:1
        // (127 of 255) and an eighth at 8:1 (32); the shipped reach-back is the 8:1 case
        expect(worstEdge <= 40,
            QString("%1: a hard edge must stay within the weight of one straddling coarse texel "
                    "(worst %2)").arg(label).arg(worstEdge));
        expect(strayedEdgeTexels == 0,
            QString("%1: the difference must stay at the edge, not spread (%2 stray texels)")
                .arg(label).arg(strayedEdgeTexels));

        caseReport(label, gFails == failsBefore,
            QString("reference vs analytic ramp %1/255; chained ramp %2/255; hard edge %3/255 at its "
                    "own column, %4 stray texels")
                .arg(worstAnalytic).arg(worstRamp).arg(worstEdge).arg(strayedEdgeTexels));
    }

    // ---- THE MUTATION CONTROL: the raw driver chain, with no tail builder, is NOT that box. This is
    // the measurement the builder replaced. If a future driver fills a non-halving level correctly,
    // this control fails and should be re-recorded - it is a statement about the driver, not about
    // this project, and without it the exactness assertions above would not be shown to have power.
    {
        const int side = 384;
        const int lev = 8;
        const int nominalRows = side / std::max(1, side >> lev);
        const QString label = QString("M18 raw driver chain, %1 square level %2").arg(side).arg(lev);
        const int failsBefore = gFails;
        const StepProfile raw = scanStep(side, lev, 8, true);
        expect(std::fabs(raw.worstDev) > 5.0,
            QString("%1: the unrebuilt chain must NOT be the box of its own footprint (worst "
                    "deviation %2 of 255 at r=%3, midpoint %4 of %5 rows) - this is the defect the "
                    "tail builder removes")
                .arg(label).arg(raw.worstDev, 0, 'f', 1).arg(raw.worstAt)
                .arg(raw.midpoint, 0, 'f', 1).arg(nominalRows));
        caseReport(
            label, gFails == failsBefore,
            QString("weight profile midpoint at r=%1 of %2 rows, worst deviation from the box %3 at "
                    "r=%4 (the box is what the tail builder produces)")
                .arg(raw.midpoint, 0, 'f', 1).arg(nominalRows)
                .arg(raw.worstDev, 0, 'f', 1).arg(raw.worstAt)
        );
    }
}

//-------------------------------------------------------------------------------------------------
// M3: the production RENDER path. M1/M2 prove the texture is built correctly; these prove the
// shader consumes it correctly, which is the other half of premultiplied mipmapping - the layer
// shader has to divide the coverage back out (PREMULTIPLIED_INPUT) - and that the vertex shader's
// division by uImageSize lands content texel (x, y) on texture texel (x, y).

// The harness's resources, owned HERE. A node handed to a layer is only borrowed - the layer takes
// an img::ResourceHandle (a shared_ptr) from it - so the node itself has an owner only if this
// registry is it. Released by clearRenderFixtures() while the GL context is still current.
std::vector<std::unique_ptr<img::ResourceNode>>& harnessResources() {
    static std::vector<std::unique_ptr<img::ResourceNode>> resources;
    return resources;
}

void releaseHarnessResources() { harnessResources().clear(); }

img::ResourceNode* makeResource(const QString& aId, const Img& aSrc) {
    std::unique_ptr<img::ResourceNode> node(new img::ResourceNode(aId));
    uint8_t* data = new uint8_t[aSrc.d.size()];
    std::memcpy(data, aSrc.d.data(), aSrc.d.size());
    node->data().grabImage(XCMemBlock(data, aSrc.d.size()), aSrc.size(), img::Format_RGBA8);
    harnessResources().emplace_back(std::move(node));
    return harnessResources().back().get();
}

// Mirrors MainDisplayWidget::paintGL's per-frame state so the render goes through the same wiring
// the app uses.
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

    std::vector<uint8_t> render(
        core::ObjectTree& aTree, const core::CameraInfo& aCamera, int aFrame, const uint8_t aBackdrop[3],
        bool aIsGrid = false, bool aWorkingCache = false
    ) {
        auto& ggl = gl::Global::functions();

        clipping->clearTexture();
        clipping->resetClippingId();
        texturizer->clearTexture();
        filters->clearAll();

        ggl.glBindFramebuffer(GL_FRAMEBUFFER, targetFbo->id());
        gl::Util::setViewportAsActualPixels(size);
        // an OPAQUE backdrop, as the clear colour - which is exactly the initial content of the
        // destination texture the layer shader reads. Opaque black gives M3 a clean analytic value.
        gl::Util::clearColorBuffer(
            aBackdrop[0] / 255.0f, aBackdrop[1] / 255.0f, aBackdrop[2] / 255.0f, 1.0f
        );
        gl::Util::resetRenderState();

        core::TimeInfo time;
        time.fps = 24;
        time.frameRange = util::Range(0, 200);
        time.loop = false;
        time.frame = core::Frame(aFrame);

        core::RenderInfo info;
        info.time = time;
        info.framebuffer = targetFbo->id();
        info.dest = targetTex->id();
        info.isGrid = aIsGrid;
        info.nonPosed = false;
        info.originMesh = false;
        info.clippingId = 0;
        info.clippingFrame = clipping.data();
        info.destTexturizer = texturizer.data();
        info.filterFrame = filters.data();
        info.opacityScale = 1.0f;
        info.camera = aCamera;

        aTree.render(info, aWorkingCache);

        std::vector<uint8_t> out((size_t)size.width() * size.height() * 4, 0);
        ggl.glReadPixels(0, 0, size.width(), size.height(), GL_RGBA, GL_UNSIGNED_BYTE, out.data());
        ggl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return out;
    }
};

core::LayerNode* addScaledLayer(
    core::ObjectTree& aTree, core::ObjectNode* aParent, img::ResourceNode* aRes, const QString& aName,
    const QVector2D& aPos, float aScale, img::BlendMode aMode
) {
    auto* layer = new core::LayerNode(aName, aTree.shaderHolder());
    layer->setDefaultImage(aRes->handle(), aMode);
    // setDefaultPosture is what creates the Move/Rotate/Scale default keys the transform
    // chain reads; without it the layer is never laid out and renders nothing. The position
    // matters too: the camera maps world (0,0) to the canvas ORIGIN, not its centre, so an
    // image centred on the world origin would hang off the top-left of the canvas.
    layer->setDefaultPosture(aPos);
    layer->setDefaultOpacity(1.0f);
    core::getOrCreateDefaultKey<core::ScaleKey, core::TimeKeyType_Scale>(*layer->timeLine())
        ->setScale(QVector2D(aScale, aScale));
    aParent->children().pushBack(layer);
    return layer;
}

// Renders one layer (source image, scale, blend mode) over an opaque backdrop.
std::map<QString, std::unique_ptr<RenderFixture>>& renderFixtures() {
    static std::map<QString, std::unique_ptr<RenderFixture>> fixtures;
    return fixtures;
}

// Drops the cached fixtures. MUST be called while the GL context is still current: the
// fixtures own GL objects, and a static destructor would run at process exit, after the
// context is gone (glDeleteFramebuffers through a dangling function pointer).
void clearRenderFixtures() {
    renderFixtures().clear();
    releaseHarnessResources();
}

std::vector<uint8_t> renderLayer(
    const QSize& aCanvas, const Img& aImage, float aScale, img::BlendMode aMode,
    const uint8_t aBackdrop[3] = nullptr
) {
    auto& fixtures = renderFixtures();
    const QString key = QString("%1x%2").arg(aCanvas.width()).arg(aCanvas.height());
    auto it = fixtures.find(key);
    if (it == fixtures.end()) {
        std::unique_ptr<RenderFixture> fx(new RenderFixture());
        fx->init(aCanvas);
        it = fixtures.emplace(key, std::move(fx)).first;
    }
    RenderFixture& fx = *it->second;

    core::ObjectTree tree;
    auto* root = new core::FolderNode("root");
    tree.grabTopNode(root);
    auto* res = makeResource(QString("mip_src_%1_%2").arg(aImage.w).arg(aImage.h), aImage);
    core::LayerNode* layer =
        addScaledLayer(tree, root, res, "layer",
                       QVector2D(aCanvas.width() * 0.5f, aCanvas.height() * 0.5f), aScale, aMode);

    core::CameraInfo camera;
    camera.reset(aCanvas, 1.0, aCanvas, QPoint(0, 0));
    const uint8_t black[3] = {0, 0, 0};

    // The layer's texture is built by the first render, so the sampler policy can only be set
    // after it. The fixtures pin what the CHAIN does - which level a footprint reads and what
    // kernel that level applies - so the shipped LOD bias (-0.5) and anisotropy (16) are turned off
    // here: they are policies on top of the chain, measured separately.
    fx.render(tree, camera, 0, aBackdrop ? aBackdrop : black);
    if (layer && layer->timeLine()) {
        if (auto* key = dynamic_cast<core::ImageKey*>(layer->timeLine()->defaultKey(core::TimeKeyType_Image))) {
            if (key->cache().texture().id() != 0) {
                gl::Texture& tex = key->cache().texture();
                tex.setLodBias(0.0f);
                tex.setMaxAnisotropy(1.0f);
            }
        }
    }
    return fx.render(tree, camera, 0, aBackdrop ? aBackdrop : black);
}

// max |a - b| over all channels, over the interior only: the layer's mesh insets its border
// vertices and the canvas edge samples the CLAMP_TO_BORDER (transparent) margin, so the outer
// band legitimately differs and is excluded.
int interiorMaxDelta(const std::vector<uint8_t>& aA, const std::vector<uint8_t>& aB, const QSize& aSize, int aMargin) {
    int worst = 0;
    for (int y = aMargin; y < aSize.height() - aMargin; ++y) {
        for (int x = aMargin; x < aSize.width() - aMargin; ++x) {
            const size_t i = ((size_t)y * aSize.width() + x) * 4;
            for (int c = 0; c < 4; ++c)
                worst = std::max(worst, std::abs((int)aA[i + c] - (int)aB[i + c]));
        }
    }
    return worst;
}

double interiorMeanRed(const std::vector<uint8_t>& aImg, const QSize& aSize, int aMargin) {
    double sum = 0;
    int n = 0;
    for (int y = aMargin; y < aSize.height() - aMargin; ++y) {
        for (int x = aMargin; x < aSize.width() - aMargin; ++x) {
            sum += aImg[((size_t)y * aSize.width() + x) * 4];
            ++n;
        }
    }
    return n ? sum / n : 0.0;
}

// min/max/mean of one channel over the interior: the 1:1 phase check reads contrast from it.
struct ChannelStats {
    double minV = 255.0;
    double maxV = 0.0;
    double mean = 0.0;
    double contrast() const { return maxV - minV; }
};

ChannelStats channelStats(const std::vector<uint8_t>& aImg, const QSize& aSize, int aMargin, int aChannel) {
    ChannelStats st;
    double sum = 0;
    int n = 0;
    for (int y = aMargin; y < aSize.height() - aMargin; ++y) {
        for (int x = aMargin; x < aSize.width() - aMargin; ++x) {
            const double v = aImg[((size_t)y * aSize.width() + x) * 4 + aChannel];
            st.minV = std::min(st.minV, v);
            st.maxV = std::max(st.maxV, v);
            sum += v;
            ++n;
        }
    }
    st.mean = n ? sum / n : 0.0;
    return st;
}

void suiteRender() {
    suiteHeader("M3 production render: minified layer, no halo");
    const QSize canvas(64, 64);
    const int imageSize = 64;
    const uint8_t color[3] = {200, 100, 50};
    // at scale 0.5 the quad is the central 32x32 of the 64x64 canvas; keep the crop inside it
    const int margin = 19;

    // The layer is drawn at half scale, so the artwork is minified 2x and the sampler reads
    // mip level 1 - which, for this source, is constant RGB at 50% coverage.
    const std::vector<uint8_t> rendered =
        renderLayer(canvas, makeAlphaChecker(imageSize, color), 0.5f, img::BlendMode_Normal);

    // GROUND TRUTH, analytic. The layer composites onto opaque black with straight-alpha
    // blending, so a correct chain leaves the silhouette's own color scaled by coverage,
    // while a chain that keeps straight data is darkened by coverage a second time.
    const double correctRed = color[0] * 0.5;
    const double haloRed = color[0] * 0.25;
    const double discrimination = std::fabs(correctRed - haloRed);

    const double measuredRed = interiorMeanRed(rendered, canvas, margin);
    const double measuredGreen = [&] {
        double sum = 0;
        int n = 0;
        for (int y = margin; y < canvas.height() - margin; ++y)
            for (int x = margin; x < canvas.width() - margin; ++x) {
                sum += rendered[((size_t)y * canvas.width() + x) * 4 + 1];
                ++n;
            }
        return n ? sum / n : 0.0;
    }();

    const int failsBefore = gFails;
    const QString prefix = "minified layer over black";

    expect(discrimination > 30.0,
        QString("%1: the halo must be distinguishable from the correct value (margin %2)")
            .arg(prefix).arg(discrimination, 0, 'f', 1));
    // THE ASSERTION: the rendered silhouette keeps its color, scaled only by coverage
    expect(near(measuredRed, correctRed, 4.0),
        QString("%1: red must be %2 (source*coverage), got %3")
            .arg(prefix).arg(correctRed, 0, 'f', 1).arg(measuredRed, 0, 'f', 1));
    expect(near(measuredGreen, color[1] * 0.5, 4.0),
        QString("%1: green must be %2, got %3")
            .arg(prefix).arg(color[1] * 0.5, 0, 'f', 1).arg(measuredGreen, 0, 'f', 1));
    expect(!near(measuredRed, haloRed, 4.0),
        QString("%1: red must NOT be the haloed %2 (got %3)")
            .arg(prefix).arg(haloRed, 0, 'f', 1).arg(measuredRed, 0, 'f', 1));

    // EQUIVALENCE: a correctly mipmapped checkerboard must render exactly like the same
    // content prefiltered by hand (constant color at the equivalent coverage). This reference
    // needs no mipmapping at all - the texture is uniform, so every level is identical - which
    // makes it a ground truth independent of the mip machinery under test.
    const std::vector<uint8_t> reference =
        renderLayer(canvas, makeUniform(imageSize, color, 128), 0.5f, img::BlendMode_Normal);
    const int equivDelta = interiorMaxDelta(rendered, reference, canvas, margin);
    expect(equivDelta <= 4,
        QString("%1: a correct chain must match the hand-prefiltered reference (delta %2)")
            .arg(prefix).arg(equivDelta));

    caseReport(
        prefix,
        gFails == failsBefore,
        QString("red=%1 (correct %2, halo %3) equivDelta=%4")
            .arg(measuredRed, 0, 'f', 1)
            .arg(correctRed, 0, 'f', 1)
            .arg(haloRed, 0, 'f', 1)
            .arg(equivDelta)
    );

    // ---- THE UV PATH AT 1:1. A 1-texel checkerboard drawn at scale 1.0 must come back at full
    // contrast: a bilinear filter returns a texel unchanged only when the sample sits on its centre,
    // and the vertex shader places it there only if the texture is the content's own size (so
    // texCoord / uImageSize maps content pixel x to texel x). A padded texture, an import ring or a
    // mismatched uImageSize shows up here as a collapse of contrast, not as a tolerance issue.
    {
        const std::vector<uint8_t> phase =
            renderLayer(canvas, makeOpaqueChecker(imageSize), 1.0f, img::BlendMode_Normal);
        const ChannelStats st = channelStats(phase, canvas, 6, 0);
        const int caseFails = gFails;
        expect(st.contrast() >= 250.0,
            QString("M10 1-texel checkerboard at 1:1: the source pattern must survive at full contrast "
                    "(got %1 of 255; a half-texel phase error blurs it toward 0)")
                .arg(st.contrast(), 0, 'f', 1));
        expect(near(st.mean, 127.5, 12.0),
            QString("M10 1-texel checkerboard at 1:1: the mean must stay the pattern's own 127.5 (got %1)")
                .arg(st.mean, 0, 'f', 1));
        caseReport(
            "M10 texel phase at 1:1", gFails == caseFails,
            QString("contrast %1 of 255, mean %2 - content texel (x, y) is texture texel (x, y)")
                .arg(st.contrast(), 0, 'f', 1).arg(st.mean, 0, 'f', 2)
        );
    }
}

//-------------------------------------------------------------------------------------------------
// GL bootstrap: mirrors MainDisplayWidget so the probe runs on the same context the app uses.

class ProbeGLWidget: public QOpenGLWidget {
public:
    gl::DeviceInfo mDeviceInfo;
    bool mGLReady = false;
    // A GL 4.0 core profile has NO default vertex array object, and every draw that sets up
    // attributes needs one bound. The app creates its own in MainDisplayWidget::initializeGL and
    // keeps it bound (`mDefaultVAO->bind(); // keep binding`); the probe does the same, so the draw
    // paths it drives behave exactly as they do in the app.
    QScopedPointer<gl::VertexArrayObject> mDefaultVao;

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
        // setContext() asserts it is called exactly once, and initializeGL re-runs when the
        // widget recreates its FBO/context behind our back, so only the first pass installs
        // the globals.
        if (!mGLReady) {
            gl::Global::setContext(*this);
            gl::Global::setFunctions(*functions);
            mDeviceInfo.load();
            gl::DeviceInfo::setInstance(&mDeviceInfo);
            const char* version = (const char*)context()->functions()->glGetString(GL_VERSION);
            std::printf("OpenGL: %s\n", version ? version : "(unknown)");
            mGLReady = true;
        } else {
            gl::Global::clearFunctions();
            gl::Global::setFunctions(*functions);
        }

        // ...and keep a vertex array object bound for the probe's lifetime. Created on every pass
        // because it belongs to the context: a rebuilt widget gets a fresh one, exactly like the
        // app's initializeGL. The check below is what turns a missing VAO into a bootstrap failure
        // instead of a mysterious GL_INVALID_OPERATION (1282) inside the first draw.
        mDefaultVao.reset(new gl::VertexArrayObject());
        mDefaultVao->bind();
        GLint boundVao = 0;
        gl::Global::functions().glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &boundVao);
        if (boundVao == 0) {
            std::fprintf(stderr, "FATAL: no vertex array object is bound (core profile needs one)\n");
            std::exit(2);
        }
    }
};

} // namespace

// The libs reference these globals; src/gui/Main.cpp defines them in the app. Same definitions the
// other GL harnesses use, so an XC_ASSERT failure is loud instead of a silent no-op, and a fatal GL
// error aborts rather than continuing on a broken context.
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
XCAssertHandler* gXCAssertHandler = nullptr;
XCErrorHandler* gXCErrorHandler = nullptr;

// core/Project.cpp references this gui method, but libgui is not linked into the probe (the class
// declaration arrives through core/Project.h), so a lone definition satisfies the linker.
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

int main(int argc, char** argv) {
    static ProbeAssertHandler assertHandler;
    static ProbeErrorHandler errorHandler;
    gXCAssertHandler = &assertHandler;
    gXCErrorHandler = &errorHandler;

    {
        QSurfaceFormat format;
        format.setVersion(gl::Global::kVersion.first, gl::Global::kVersion.second);
        format.setProfile(QSurfaceFormat::CoreProfile);
        QSurfaceFormat::setDefaultFormat(format);
    }
    QApplication app(argc, argv);

    QMainWindow window;
    auto* glWidget = new ProbeGLWidget();
    window.setCentralWidget(glWidget);
    window.resize(32, 32);
    window.show();
    app.processEvents();
    if (!glWidget->mGLReady) {
        glWidget->update();
        app.processEvents();
    }
    if (!glWidget->mGLReady) {
        std::fprintf(stderr, "FATAL: GL widget did not initialize on this platform\n");
        return 2;
    }
    glWidget->makeCurrent();

    suiteChain();
    suiteImageKey();
    suiteChainExactness();
    suiteRender();

    std::printf("\n%d checks, %d failures\n", gChecks, gFails);
    std::fflush(stdout);

    // tear GL objects down while the context is still current: the fixtures, and the widget's own
    // vertex array object (its destructor runs with the window, after the context is gone)
    clearRenderFixtures();
    glWidget->mDefaultVao.reset();
    glWidget->doneCurrent();
    return gFails ? 1 : 0;
}
