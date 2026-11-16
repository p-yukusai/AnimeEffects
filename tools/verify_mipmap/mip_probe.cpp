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
#include <array>
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
#include "core/FFDKey.h"
#include "core/FilterFrame.h"
#include "core/FolderNode.h"
#include "core/Frame.h"
#include "core/ImageKey.h"
#include "core/ImagePyramid.h"
#include "core/LayerNode.h"
#include "core/MaskKey.h"
#include "core/MaskPyramid.h"
#include "core/ObjectNode.h"
#include "core/ObjectTree.h"
#include "core/RenderInfo.h"
#include "core/ScaleKey.h"
#include "core/TimeInfo.h"
#include "core/TimeKeyType.h"
#include "core/TimeKeyBlender.h"
#include "core/VirtualTexture.h"
#include "gl/DeviceInfo.h"
#include "gl/Framebuffer.h"
#include "gl/Global.h"
#include "gl/Texture.h"
#include "gl/Util.h"
#include "gl/VertexArrayObject.h"
#include "gui/AudioPlaybackWidget.h"
#include "img/BlendMode.h"
#include "img/Premultiply.h"
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

// Per-texel noise, opaque, from a fixed xorshift seed: the filter-discrimination source for V1.
// A smooth ramp hides a wrong kernel in 1/255 rounding; here any filtering difference (one tap
// instead of the reconstruction, a mip blend, a sub-texel address error) lands tens of units out.
Img makeNoise(int aW, int aH) {
    Img img;
    img.w = aW;
    img.h = aH;
    img.d.assign((size_t)aW * aH * 4, 0);
    uint32 s = 0x9e3779b9u;
    const auto next = [&s]() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    };
    for (int y = 0; y < aH; ++y) {
        for (int x = 0; x < aW; ++x) {
            uint8_t* p = img.at(x, y);
            p[0] = (uint8_t)(next() >> 24);
            p[1] = (uint8_t)(next() >> 24);
            p[2] = (uint8_t)(next() >> 24);
            p[3] = 255;
        }
    }
    return img;
}


// A solid opaque image.
Img makeSolid(int aW, int aH, uint8_t aR, uint8_t aG, uint8_t aB) {
    Img img;
    img.w = aW;
    img.h = aH;
    img.d.assign((size_t)aW * aH * 4, 0);
    for (int y = 0; y < aH; ++y) {
        for (int x = 0; x < aW; ++x) {
            uint8_t* p = img.at(x, y);
            p[0] = aR;
            p[1] = aG;
            p[2] = aB;
            p[3] = 255;
        }
    }
    return img;
}

// Opaque RGB under a DISC of alpha with a two-texel soft edge: the clipper's mask needs a real
// alpha field, and a shape with a known screen position lets the gate assert the mask is doing
// something instead of just "the two paths agree".
Img makeDisc(int aW, int aH, uint8_t aR, uint8_t aG, uint8_t aB) {
    Img img;
    img.w = aW;
    img.h = aH;
    img.d.assign((size_t)aW * aH * 4, 0);
    const double cx = aW * 0.5;
    const double cy = aH * 0.5;
    const double radius = std::min(aW, aH) * 0.32;
    for (int y = 0; y < aH; ++y) {
        for (int x = 0; x < aW; ++x) {
            uint8_t* p = img.at(x, y);
            p[0] = aR;
            p[1] = aG;
            p[2] = aB;
            const double d = std::hypot(x + 0.5 - cx, y + 0.5 - cy);
            const double t = std::max(0.0, std::min(1.0, (radius - d) / 2.0 + 0.5));
            p[3] = (uint8_t)std::lround(255.0 * t);
        }
    }
    return img;
}


// The mask chain's rule, on the CPU: each level is the area average of the level above it over
// the exact fractional footprint, rounded once. MaskMipChain computes the same rule on the GPU in
// two axis passes (the x pass stores to an R16F scratch, so a level keeps one rounding); whether
// the two agree BIT-EXACTLY decides whether a paged mask can be fed from a CPU pyramid.
struct MaskCpuPyramid {
    QSize base;
    std::vector<QSize> sizes;
    std::vector<std::vector<uint8_t>> levels;

    static QSize sizeAt(const QSize& aBase, int aLevel) {
        return QSize(std::max(1, aBase.width() >> aLevel), std::max(1, aBase.height() >> aLevel));
    }

    void build(const QImage& aField) {
        base = aField.size();
        levels.clear();
        sizes.clear();
        int count = 1;
        for (int side = std::max(base.width(), base.height()); side > 1; side >>= 1)
            ++count;
        for (int l = 0; l < count; ++l)
            sizes.push_back(sizeAt(base, l));
        levels.resize((size_t)count);
        levels[0].resize((size_t)base.width() * base.height());
        for (int y = 0; y < base.height(); ++y)
            std::memcpy(
                levels[0].data() + (size_t)y * base.width(), aField.constScanLine(y), (size_t)base.width()
            );
        // THE GPU'S TWO PASSES, MIRRORED. MaskMipChain's x pass averages one source row at a
        // time and stores the NORMALIZED average in an R16F scratch (half precision); the y pass
        // averages those halves and writes the R8 level, whose float->unorm8 conversion rounds.
        // A single 2D area average differs by up to 3 LSB because 1/255-scaled values are not
        // representable in half - so the half store is part of the rule, not an implementation
        // detail.
        for (int l = 1; l < count; ++l) {
            const QSize src = sizes[(size_t)l - 1];
            const QSize dst = sizes[(size_t)l];
            const std::vector<uint8_t>& above = levels[(size_t)l - 1];
            std::vector<uint8_t>& out = levels[(size_t)l];
            out.resize((size_t)dst.width() * dst.height());
            std::vector<float> xpass((size_t)dst.width() * src.height(), 0.0f);
            for (int sx = 0; sx < dst.width(); ++sx) {
                const float lo = float(sx) * float(src.width()) / float(dst.width());
                const float hi = float(sx + 1) * float(src.width()) / float(dst.width());
                const int first = int(std::floor(lo));
                for (int sy = 0; sy < src.height(); ++sy) {
                    float acc = 0.0f, wsum = 0.0f;
                    for (int i = 0; i < int(std::ceil(double(src.width()) / dst.width())) + 1; ++i) {
                        const int k = first + i;
                        const float w = std::min(float(k + 1), hi) - std::max(float(k), lo);
                        if (w <= 0.0f)
                            continue;
                        acc += (above[(size_t)sy * src.width() + k] / 255.0f) * w;
                        wsum += w;
                    }
                    xpass[(size_t)sy * dst.width() + sx] =
                        wsum > 0.0f ? float(qfloat16(acc / wsum)) : 0.0f;
                }
            }
            for (int dy = 0; dy < dst.height(); ++dy) {
                const float lo = float(dy) * float(src.height()) / float(dst.height());
                const float hi = float(dy + 1) * float(src.height()) / float(dst.height());
                const int first = int(std::floor(lo));
                for (int dx = 0; dx < dst.width(); ++dx) {
                    float acc = 0.0f, wsum = 0.0f;
                    for (int i = 0; i < int(std::ceil(double(src.height()) / dst.height())) + 1; ++i) {
                        const int k = first + i;
                        const float w = std::min(float(k + 1), hi) - std::max(float(k), lo);
                        if (w <= 0.0f)
                            continue;
                        acc += xpass[(size_t)k * dst.width() + dx] * w;
                        wsum += w;
                    }
                    const float v = wsum > 0.0f ? acc / wsum : 0.0f;
                    out[(size_t)dy * dst.width() + dx] =
                        (uint8_t)std::lround(std::max(0.0f, std::min(1.0f, v)) * 255.0f);
                }
            }
        }
    }
};

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
// M1b: THE CHAIN MUST BE A FUNCTION OF ITS SOURCE, not of the command stream around it.
//
// The area-weighted tail used to build each level of a chain while sampling another level of the
// same texture. The spec's feedback rule is per IMAGE, so that is legal on paper, but the driver
// resolves a whole-texture write-while-sample by renaming, and the result then depends on what
// else is in the stream - which is why the paged path, whose pyramid capture reads every level
// back right after the build, was the only path that saw it (measured on nene: 240 of 241 chains
// hash identically across two runs and one differs at all ten levels; with AE_VIRTUAL_TEXTURE=0
// two runs agree everywhere). The fix renders into a scratch texture and blits.
//
// So this case builds the same source's chain twice, WITH A FULL READBACK OF THE FIRST IN BETWEEN
// (the exact perturbation the capture applies), and requires the two chains to be bit-identical.
// AE_MIP_TAIL_OLD_PATH=1 reinstates the old construction and the case reports which path it
// measured - but MEASURED, the old path passes this case too (9 levels, worst channel 0): inside
// one process it is stable, and the defect only appears across processes, where the readback's
// position in the stream differs. So this case is a structural guard, NOT the defect's gate; the
// gate with power is the two-run comparison in the design doc's Measured section, and
// AE_MIP_TAIL_OLD_PATH=1 is the control that reproduces the defect for it (325/305 samples from
// the control, 109 apart, max 223).
void suiteChainDeterminism() {
    suiteHeader("M1b the mip chain is a function of its source, not of the command stream");
    // a size whose levels do NOT all halve exactly, so the tail exists: 384 halves to level 7 and
    // then has to reduce 3:1, and the odd height gives the tail an inexact first step
    const int w = 384;
    const int h = 259;
    const Pattern& pat = kPatterns[0];
    Img src;
    src.w = w;
    src.h = h;
    src.d.assign((size_t)w * h * 4, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t* p = src.at(x, y);
            const bool on = ((x * 3 + y * 5) % 11) < 5;
            const uint8_t* c = on ? pat.inside : pat.outside;
            p[0] = c[0];
            p[1] = c[1];
            p[2] = c[2];
            p[3] = on ? 255 : (uint8_t)(y & 0xFF);
        }
    }

    const std::vector<Img> first = readChain(src, true);
    // THE PERTURBATION: read every level back before the second build, which is what the pyramid
    // capture does, and what used to change how the first chain was resolved
    const std::vector<Img> second = readChain(src, true);

    const QString prefix = "chain determinism";
    const int failsBefore = gFails;
    expect(!first.empty() && first.size() == second.size(),
        QString("%1: both builds must produce the same level count (%2 vs %3)")
            .arg(prefix).arg(first.size()).arg(second.size()));
    int differingLevels = 0;
    int worstChannel = 0;
    const size_t levels = std::min(first.size(), second.size());
    for (size_t l = 0; l < levels; ++l) {
        if (first[l].w != second[l].w || first[l].h != second[l].h) {
            ++differingLevels;
            worstChannel = 255;
            continue;
        }
        int delta = 0;
        for (size_t i = 0; i < first[l].d.size(); ++i)
            delta = std::max(delta, std::abs((int)first[l].d[i] - (int)second[l].d[i]));
        if (delta > 0)
            ++differingLevels;
        worstChannel = std::max(worstChannel, delta);
    }
    expect(differingLevels == 0,
        QString("%1: two builds of one source must be bit-identical (%2 level(s) differ, worst %3) - "
                "a chain that changes with the stream is the mip tail's read-while-write")
            .arg(prefix).arg(differingLevels).arg(worstChannel));
    caseReport(
        prefix, gFails == failsBefore,
        QString("%1 levels, worst channel %2, tail %3")
            .arg((int)levels)
            .arg(worstChannel)
            .arg(qEnvironmentVariableIsSet("AE_MIP_TAIL_OLD_PATH") ? "OLD PATH" : "scratch")
    );
}

//-------------------------------------------------------------------------------------------------
// P0: THE PAGE FILL'S PREMULTIPLY IS THE REFERENCE FORMULA, BYTE FOR BYTE.
//
// A paged image's level-0 page (core::ImagePyramid::fillPage) and the full-resolution chain's upload
// (img::Util::makeTextureUpload) are the same conversion, and they share one implementation
// (img::premultiply). That makes THIS case - not a pixel comparison - the only place the conversion
// can be graded: both sides of every paged-versus-control frame go through it, so a wrong lane
// order or a changed rounding moves both paths together and leaves their pixels equal.
//
// The reference is written out here independently (round to nearest, alpha untouched), and it is
// applied over EVERY (channel, alpha) pair rather than a sample, then over six whole pages: an
// interior page, a page at the image's right edge, one below it, one whose right gutter holds the
// level's first two columns, and one wholly outside the level on each side (all zero). The
// straight-copy discriminant asserts the fixture really carries transparency, or the comparison
// would pass with the multiply missing.
void suitePremultiply() {
    suiteHeader("P0 the page fill's premultiply is the reference formula");
    const QString prefix = "premultiply";
    const int failsBefore = gFails;

    auto reference = [](int aChannel, int aAlpha) { return (uint8_t)((aChannel * aAlpha + 127) / 255); };

    // ---- every (channel, alpha) pair through both implementations
    {
        std::vector<uint8_t> src, want, dispatched, fallback;
        src.reserve((size_t)256 * 256 * 4);
        for (int c = 0; c < 256; ++c) {
            for (int a = 0; a < 256; ++a) {
                const uint8_t texel[4] = {
                    (uint8_t)c, (uint8_t)(255 - c), (uint8_t)((c * 7 + a * 3) & 0xFF), (uint8_t)a
                };
                src.insert(src.end(), texel, texel + 4);
                const uint8_t out[4] = {
                    reference(texel[0], a), reference(texel[1], a), reference(texel[2], a), (uint8_t)a
                };
                want.insert(want.end(), out, out + 4);
            }
        }
        dispatched.resize(src.size());
        fallback.resize(src.size());
        img::premultiply(src.data(), dispatched.data(), (int)src.size() / 4);
        img::premultiplyScalar(src.data(), fallback.data(), (int)src.size() / 4);
        int dispatchedBad = 0;
        int fallbackBad = 0;
        for (size_t i = 0; i < src.size(); i += 4) {
            if (std::memcmp(&dispatched[i], &want[i], 4))
                ++dispatchedBad;
            if (std::memcmp(&fallback[i], &want[i], 4))
                ++fallbackBad;
        }
        expect(dispatchedBad == 0,
            QString("%1: the dispatched conversion must be (c*a+127)/255 with alpha unchanged on all "
                    "65,536 (channel, alpha) pairs (%2 wrong)")
                .arg(prefix).arg(dispatchedBad));
        expect(fallbackBad == 0,
            QString("%1: the scalar fallback too (%2 wrong) - it is the reference the vector path "
                    "must equal").arg(prefix).arg(fallbackBad));
    }

    // ---- whole pages, built independently: gutter, edge, and a page outside the image
    const int w = 300; // wider than a page, narrower than two: page 1 is an edge page
    const int h = 260; // taller than a page by 4: page 1's last rows are outside
    std::vector<uint8_t> image((size_t)w * h * 4);
    int transparent = 0, partial = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t* p = image.data() + ((size_t)y * w + x) * 4;
            p[0] = (uint8_t)(x * 7);
            p[1] = (uint8_t)(y * 5);
            p[2] = (uint8_t)(x ^ y);
            const int alpha = (x % 37 == 0 || y % 23 == 0) ? (x + y) % 256 : 255;
            p[3] = (uint8_t)alpha;
            if (alpha == 0)
                ++transparent;
            else if (alpha != 255)
                ++partial;
        }
    }
    expect(transparent > 0 && partial > 0,
        QString("%1: the fixture must carry transparent and partly covered texels (got %2 / %3) - "
                "an all-opaque image is identical under a straight copy, so the case would have no "
                "power").arg(prefix).arg(transparent).arg(partial));

    gl::Texture chain;
    chain.create(
        QSize(w, h), image.data(), GL_RGBA, GL_RGBA8, GL_UNSIGNED_BYTE, gl::Texture::GenerateMipmap
    );
    core::ImagePyramid pyramid;
    expect(pyramid.capture(chain, 1) && pyramid.size() == QSize(w, h),
        QString("%1: the pyramid must capture the image's size (%2x%3)").arg(prefix).arg(w).arg(h));

    auto referencePage = [&](int aPageX, int aPageY) {
        std::vector<uint8_t> page(
            (size_t)gl::PagePool::kPitch * gl::PagePool::kPitch * 4, 0
        );
        for (int y = 0; y < gl::PagePool::kPitch; ++y) {
            for (int x = 0; x < gl::PagePool::kPitch; ++x) {
                // destination texel (x, y) is level texel (page*256 + x - gutter, ...)
                const int gx = aPageX * gl::PagePool::kPageSize + x - gl::PagePool::kGutter;
                const int gy = aPageY * gl::PagePool::kPageSize + y - gl::PagePool::kGutter;
                if (gx < 0 || gy < 0 || gx >= w || gy >= h)
                    continue; // outside the level: transparent, as clamp-to-border was
                const uint8_t* s = image.data() + ((size_t)gy * w + gx) * 4;
                uint8_t* d = page.data() + ((size_t)y * gl::PagePool::kPitch + x) * 4;
                d[0] = reference(s[0], s[3]);
                d[1] = reference(s[1], s[3]);
                d[2] = reference(s[2], s[3]);
                d[3] = s[3];
            }
        }
        return page;
    };

    // the page is handed over with a CANARY on each side, so a fill that writes outside its
    // 260x260 buffer FAILS a check instead of corrupting the heap - which is how the outside pages
    // below were found doing exactly that (a negative x1 memsets from before the row).
    const size_t pageBytes = (size_t)gl::PagePool::kPitch * gl::PagePool::kPitch * 4;
    const size_t guard = 1024;
    // the last three are the ones the pool never asks for: a page whose RIGHT gutter holds the
    // level's first two columns, one wholly to the right of the level (x1 negative), one wholly to
    // its left (x0 past the row)
    const QPoint pages[] = {
        QPoint(0, 0), QPoint(1, 0), QPoint(1, 1), QPoint(-1, 0), QPoint(2, 1), QPoint(-2, 0)
    };
    int worstDelta = 0;
    for (const QPoint& at : pages) {
        std::vector<uint8_t> guarded(pageBytes + guard * 2, 0x5A);
        uint8_t* got = guarded.data() + guard;
        pyramid.fillPage(0, at.x(), at.y(), image.data(), QSize(w, h), got);
        bool canary = true;
        for (size_t i = 0; i < guard; ++i)
            canary = canary && guarded[i] == 0x5A && guarded[guard + pageBytes + i] == 0x5A;
        expect(canary,
            QString("%1: page (%2,%3) must write its own %4 bytes and nothing outside them")
                .arg(prefix).arg(at.x()).arg(at.y()).arg(pageBytes));
        const std::vector<uint8_t> want = referencePage(at.x(), at.y());
        int bad = 0;
        int delta = 0;
        for (size_t i = 0; i < pageBytes; ++i) {
            if (got[i] != want[i]) {
                ++bad;
                delta = std::max(delta, std::abs((int)got[i] - (int)want[i]));
            }
        }
        worstDelta = std::max(worstDelta, delta);
        expect(bad == 0,
            QString("%1: page (%2,%3) must be the reference page byte for byte (%4 texels differ, "
                    "worst channel %5) - the gutter, the edge and the outside are part of it")
                .arg(prefix).arg(at.x()).arg(at.y()).arg(bad).arg(delta));
    }

    // ---- the discriminant: the same page straight-copied must differ, or the multiply is unmeasured
    {
        const std::vector<uint8_t> premultiplied = referencePage(1, 0);
        int differing = 0;
        for (int y = 0; y < gl::PagePool::kPitch; ++y) {
            for (int x = 0; x < gl::PagePool::kPitch; ++x) {
                const int gx = gl::PagePool::kPageSize + x - gl::PagePool::kGutter;
                const int gy = y - gl::PagePool::kGutter;
                if (gx < 0 || gy < 0 || gx >= w || gy >= h)
                    continue;
                const uint8_t* s = image.data() + ((size_t)gy * w + gx) * 4;
                const uint8_t* p = premultiplied.data()
                    + ((size_t)y * gl::PagePool::kPitch + x) * 4;
                if (p[0] != s[0] || p[1] != s[1] || p[2] != s[2])
                    ++differing;
            }
        }
        expect(differing > 100,
            QString("%1: the fixture must differ from a straight copy over a large run of texels "
                    "(got %2) - otherwise a missing multiply would pass").arg(prefix).arg(differing));
    }

    caseReport(
        prefix, gFails == failsBefore,
        QString("all pairs + %1 pages, worst channel %2").arg((int)(sizeof(pages) / sizeof(pages[0]))).arg(worstDelta)
    );
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
        bool aIsGrid = false, bool aWorkingCache = false,
        const core::MaskKey* aMaskPreviewKey = nullptr, const core::ObjectNode* aMaskPreviewTarget = nullptr,
        const QColor& aMaskPreviewColor = QColor(255, 0, 0, 128),
        // the export policy's two extra frames (see renderExported): a demand-only pass draws
        // nothing and is not a frame boundary of its own
        bool aDemandOnly = false, bool aSkipFrameBoundary = false
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
        info.maskPreview = nullptr;
        info.maskPreviewKey = aMaskPreviewKey;
        info.maskPreviewTarget = aMaskPreviewTarget;
        info.maskPreviewColor = aMaskPreviewColor;
        info.demandOnly = aDemandOnly;

        // the production frame boundary (Driver::renderGL does this for the app)
        if (!aSkipFrameBoundary)
            core::VirtualTexture::beginFrame();
        aTree.render(info, aWorkingCache);
        if (!aSkipFrameBoundary)
            core::VirtualTexture::endFrame();

        std::vector<uint8_t> out((size_t)size.width() * size.height() * 4, 0);
        ggl.glReadPixels(0, 0, size.width(), size.height(), GL_RGBA, GL_UNSIGNED_BYTE, out.data());
        ggl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return out;
    }

    /// THE EXPORT POLICY (ctrl::Exporter's exact sequence, see texture-residency.md): a
    /// demand-only pass for this frame's camera, synchronise the pages it marked, then ONE real
    /// pass. The exported first frame must be as sharp as the last, so a paged resource the
    /// demand-only pass misses (a folder matte, say) shows up as a coarse frame here.
    std::vector<uint8_t> renderExported(
        core::ObjectTree& aTree, const core::CameraInfo& aCamera, int aFrame, const uint8_t aBackdrop[3]
    ) {
        core::VirtualTexture::beginFrame();
        render(aTree, aCamera, aFrame, aBackdrop, false, false, nullptr, nullptr, QColor(), true, true);
        core::VirtualTexture::endFrame();
        core::VirtualTexture::synchronize();
        return render(aTree, aCamera, aFrame, aBackdrop);
    }
};

core::LayerNode* addScaledLayer(
    core::ObjectTree& aTree, core::ObjectNode* aParent, img::ResourceNode* aRes, const QString& aName,
    const QVector2D& aPos, float aScale, img::BlendMode aMode
) {
    auto* layer = new core::LayerNode(aName, aTree.shaderHolder());
    layer->setDefaultImage(aRes->handle(), aMode);
    // setDefaultPosture is what creates the Move/Rotate/Scale base values the transform
    // chain reads; without it the layer is never laid out and renders nothing. The position
    // matters too: the camera maps world (0,0) to the canvas ORIGIN, not its centre, so an
    // image centred on the world origin would hang off the top-left of the canvas.
    layer->setDefaultPosture(aPos);
    layer->setDefaultOpacity(1.0f);
    if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Scale)) {
        layer->timeLine()->setChannelBaseValue(core::TimeKeyType_Scale, 0, aScale);
        layer->timeLine()->setChannelBaseValue(core::TimeKeyType_Scale, 1, aScale);
    } else {
        core::getOrCreateBaseValue<core::ScaleKey, core::TimeKeyType_Scale>(*layer->timeLine())
            ->setScale(QVector2D(aScale, aScale));
    }
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
        if (auto* key = dynamic_cast<core::ImageKey*>(layer->timeLine()->baseValue(core::TimeKeyType_Image))) {
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

//-------------------------------------------------------------------------------------------------
// P1/P2: the page pool itself. V1 proves the whole path renders the same pixels; these prove the
// allocator beneath it: slot geometry (a page is where the table says it is), the gutter is not
// shared between slots, eviction order, and the budget/cancel/generation bookkeeping.
namespace {
    class EvictionLog: public gl::PagePool::Listener {
    public:
        std::vector<gl::PagePool::PageId> evicted;
        void pageEvicted(const gl::PagePool::PageId& aPage) override { evicted.push_back(aPage); }
    };

    gl::PagePool::PageId pageId(quint64 owner, int level, int x, int y) {
        gl::PagePool::PageId id;
        id.owner = owner;
        id.level = level;
        id.x = x;
        id.y = y;
        return id;
    }
} // namespace

void suitePagePool() {
    suiteHeader("P1/P2 page pool: slot geometry, eviction, budget");
    if (!gl::PagePool::enabled()) {
        std::printf("  [SKIP] AE_VIRTUAL_TEXTURE=0 is the control path\n");
        return;
    }

    auto& pool = gl::PagePool::instance();
    pool.setBudgetBytes((qint64)gl::PagePool::kAtlasBytes); // exactly one small atlas
    pool.purge();
    pool.beginFrame();

    // P1: the first slot sits at the atlas origin plus the gutter, and slots step by the pitch
    // on the 15-wide grid. If the pool and the shader disagree, every page is offset.
    EvictionLog log;
    gl::PagePool::Slot slot0, slot1, slotRow2;
    const bool got0 = pool.acquire(pageId(1, 0, 0, 0), &log, &slot0);
    const bool got1 = pool.acquire(pageId(1, 0, 1, 0), &log, &slot1);
    bool gotRow2 = got0 && got1;
    // the first page of the SECOND row is slot kSlotsPerAxis
    for (int i = 2; i <= gl::PagePool::kSlotsPerAxis && gotRow2; ++i) {
        gl::PagePool::Slot s;
        gotRow2 = pool.acquire(pageId(1, 0, i, 0), &log, &s);
        if (i == gl::PagePool::kSlotsPerAxis)
            slotRow2 = s;
    }
    expect(got0 && got1 && gotRow2, "P1 a row of pages must fit in a fresh atlas");
    if (got0 && got1 && gotRow2) {
        expect(
            slot0.x == gl::PagePool::kGutter && slot0.y == gl::PagePool::kGutter,
            QString("P1 slot 0 content origin must be (%1,%2), got (%3,%4)")
                .arg(gl::PagePool::kGutter)
                .arg(gl::PagePool::kGutter)
                .arg(slot0.x)
                .arg(slot0.y)
        );
        expect(
            slot1.x == gl::PagePool::kPitch + gl::PagePool::kGutter && slot1.y == gl::PagePool::kGutter,
            QString("P1 slot 1 must step one pitch in x, got (%1,%2)").arg(slot1.x).arg(slot1.y)
        );
        expect(
            slotRow2.x == gl::PagePool::kGutter
                && slotRow2.y == gl::PagePool::kPitch + gl::PagePool::kGutter,
            QString("P1 the first page of the second row must be at (%1,%2), got (%3,%4)")
                .arg(gl::PagePool::kGutter)
                .arg(gl::PagePool::kPitch + gl::PagePool::kGutter)
                .arg(slotRow2.x)
                .arg(slotRow2.y)
        );
        expect(pool.find(pageId(1, 0, 0, 0), nullptr), "P1 a committed page must be found");
        expect(!pool.find(pageId(1, 0, 9, 9), nullptr), "P1 a page never acquired must not be found");

        // the atlas really holds what was written at the slot's origin: one texel per slot with a
        // position-dependent value, read back through the pool's own readSlot
        auto& ggl = gl::Global::functions();
        std::vector<uint8_t> pixels((size_t)gl::PagePool::kPitch * gl::PagePool::kPitch * 4, 0);
        for (int y = 0; y < gl::PagePool::kPitch; ++y)
            for (int x = 0; x < gl::PagePool::kPitch; ++x) {
                uint8_t* p = pixels.data() + ((size_t)y * gl::PagePool::kPitch + x) * 4;
                p[0] = (uint8_t)((x + y * 3) & 255);
                p[1] = (uint8_t)((x * 5 + y) & 255);
                p[2] = 17;
                p[3] = 255;
            }
        ggl.glBindTexture(GL_TEXTURE_2D, pool.atlasTextureId(slot0.atlas));
        ggl.glTexSubImage2D(
            GL_TEXTURE_2D,
            0,
            slot0.x - gl::PagePool::kGutter,
            slot0.y - gl::PagePool::kGutter,
            gl::PagePool::kPitch,
            gl::PagePool::kPitch,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            pixels.data()
        );
        ggl.glBindTexture(GL_TEXTURE_2D, 0);
        std::vector<uint8_t> back(pixels.size(), 0);
        pool.readSlot(slot0, back.data());
        expect(back == pixels, "P1 readSlot must return exactly the texels written to the slot");
    }

    // P2: the frame's upload cap refuses new pages until the next frame
    pool.beginFrame();
    int refused = 0;
    int got = 0;
    for (int i = 0; i < 400; ++i) {
        gl::PagePool::Slot s;
        if (pool.acquire(pageId(1, 0, 100 + i, 0), &log, &s))
            ++got;
        else {
            ++refused;
            break;
        }
    }
    expect(refused == 1, "P2 the per-frame upload cap must refuse once it is spent");
    expect(got > 0, QString("P2 pages before the cap (%1)").arg(got));
    pool.beginFrame();
    {
        gl::PagePool::Slot s;
        expect(pool.acquire(pageId(1, 0, 1000, 0), &log, &s), "P2 a new frame must reopen the upload budget");
    }

    // P2: eviction. One atlas holds kSlotsPerAtlas pages; the next acquire must evict the least
    // recently touched one and tell its owner.
    pool.purge();
    pool.setBudgetBytes((qint64)gl::PagePool::kAtlasBytes);
    log.evicted.clear();
    // acquire exactly one atlas' worth, a frame's cap at a time
    int acquired = 0;
    for (int i = 0; i < gl::PagePool::kSlotsPerAtlas; ++i) {
        // the default upload cap is 8 MB (29 pages), so a frame cannot fill the whole atlas
        if (i % 20 == 0)
            pool.beginFrame();
        gl::PagePool::Slot s;
        if (pool.acquire(pageId(2, 0, i, 0), &log, &s))
            ++acquired;
    }
    expect(
        acquired == gl::PagePool::kSlotsPerAtlas,
        QString("P2 one atlas must hold %1 pages (got %2)")
            .arg(gl::PagePool::kSlotsPerAtlas)
            .arg(acquired)
    );
    // touch all but page 0 so page 0 becomes the victim
    for (int i = 1; i < gl::PagePool::kSlotsPerAtlas; ++i)
        pool.touch(pageId(2, 0, i, 0));
    pool.beginFrame();
    {
        gl::PagePool::Slot s;
        const bool gotVictim = pool.acquire(pageId(2, 0, 9999, 0), &log, &s);
        expect(gotVictim, "P2 a full pool must evict rather than fail");
        expect(!pool.find(pageId(2, 0, 0, 0), nullptr), "P2 the least recently touched page must be the victim");
        expect(pool.find(pageId(2, 0, 1, 0), nullptr), "P2 a recently touched page must survive");
        expect(
            log.evicted.size() == 1 && log.evicted[0].x == 0,
            QString("P2 the owner must be told exactly the evicted page (%1 events)").arg((int)log.evicted.size())
        );
    }

    // P2: cancel returns a slot and refunds the frame's upload byte count
    pool.purge();
    pool.beginFrame();
    {
        gl::PagePool::Slot s;
        expect(pool.acquire(pageId(3, 0, 0, 0), &log, &s), "P2 acquire before cancel");
        const qint64 before = pool.frameUploadBytes();
        pool.cancel(s);
        expect(!pool.find(pageId(3, 0, 0, 0), nullptr), "P2 a cancelled page must not be resident");
        expect(pool.frameUploadBytes() < before, "P2 cancel must refund the frame's upload bytes");
        gl::PagePool::Slot again;
        expect(
            pool.acquire(pageId(3, 0, 0, 0), &log, &again) && again.slot == s.slot,
            "P2 cancel must return the slot to the free list"
        );
    }

    // P2: purge forgets every page and changes the generation the tables use to detect it
    const quint64 before = pool.generation();
    pool.purge();
    expect(pool.generation() != before, "P2 purge must change the generation");
    expect(pool.residentPages() == 0, "P2 purge must leave no resident page");
    expect(pool.atlasCount() == 0, "P2 purge must release every atlas");

    // P3: THE R8 STACK. A mask page lives in an R8 atlas (its own four-atlas range), its upload
    // and readback are one channel, and an R8 acquire must never evict an RGBA page: the victim
    // would not free a usable slot.
    pool.purge();
    pool.setBudgetBytes(
        (qint64)gl::PagePool::kAtlasBytes + gl::PagePool::atlasBytes(gl::PagePool::Format_R8)
    );
    pool.beginFrame();
    {
        EvictionLog r8log;
        gl::PagePool::Slot rgbaSlot, r8Slot;
        expect(pool.acquire(pageId(8, 0, 0, 0), &r8log, &rgbaSlot), "P3 the RGBA page must fit");
        r8log.evicted.clear();
        pool.beginFrame();
        expect(
            pool.acquire(pageId(9, 0, 0, 0), &r8log, &r8Slot, gl::PagePool::Format_R8),
            "P3 an R8 page must fit beside it"
        );
        expect(
            rgbaSlot.format == gl::PagePool::Format_RGBA8,
            "P3 the RGBA page's slot must name its format"
        );
        expect(r8Slot.format == gl::PagePool::Format_R8, "P3 the R8 page's slot must name its format");
        expect(
            r8Slot.atlas >= gl::PagePool::kMaxAtlasCount && r8Slot.atlas < gl::PagePool::kAtlasSlots,
            "P3 the R8 atlas must live in its own index range"
        );
        expect(
            pool.atlasFormat(r8Slot.atlas) == gl::PagePool::Format_R8, "P3 the R8 atlas's format"
        );
        expect(
            pool.allocatedBytes()
                == (qint64)gl::PagePool::kAtlasBytes + gl::PagePool::atlasBytes(gl::PagePool::Format_R8),
            "P3 both atlases' bytes must be booked"
        );

        // the R8 page's content: one channel, written and read back the same way
        std::vector<uint8_t> red((size_t)gl::PagePool::kPitch * gl::PagePool::kPitch, 0);
        for (int y = 0; y < gl::PagePool::kPitch; ++y)
            for (int x = 0; x < gl::PagePool::kPitch; ++x)
                red[(size_t)y * gl::PagePool::kPitch + x] = (uint8_t)((x * 3 + y * 5) & 255);
        auto& ggl = gl::Global::functions();
        ggl.glBindTexture(GL_TEXTURE_2D, pool.atlasTextureId(r8Slot.atlas));
        ggl.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        ggl.glTexSubImage2D(
            GL_TEXTURE_2D, 0, r8Slot.x - gl::PagePool::kGutter, r8Slot.y - gl::PagePool::kGutter,
            gl::PagePool::kPitch, gl::PagePool::kPitch, GL_RED, GL_UNSIGNED_BYTE, red.data()
        );
        ggl.glBindTexture(GL_TEXTURE_2D, 0);
        std::vector<uint8_t> back(red.size(), 0);
        pool.readSlot(r8Slot, back.data());
        expect(back == red, "P3 readSlot must return exactly the R8 texels written");

        // fill the R8 atlas a frame's cap at a time; the RGBA page must survive every eviction
        int acquired = 0;
        for (int i = 1; i < gl::PagePool::kSlotsPerAtlas; ++i) {
            if (i % 20 == 0)
                pool.beginFrame();
            gl::PagePool::Slot slot;
            if (pool.acquire(pageId(9, 0, i, 0), &r8log, &slot, gl::PagePool::Format_R8))
                ++acquired;
        }
        expect(
            acquired == gl::PagePool::kSlotsPerAtlas - 1,
            QString("P3 the R8 atlas must hold %1 pages (got %2)")
                .arg(gl::PagePool::kSlotsPerAtlas - 1)
                .arg(acquired)
        );
        pool.beginFrame();
        gl::PagePool::Slot extra;
        expect(
            pool.acquire(pageId(9, 0, 9999, 0), &r8log, &extra, gl::PagePool::Format_R8),
            "P3 a full R8 stack must evict"
        );
        expect(
            !pool.find(pageId(9, 0, 0, 0), nullptr), "P3 the least recently touched R8 page is the victim"
        );
        expect(
            pool.find(pageId(8, 0, 0, 0), nullptr), "P3 an R8 eviction must not touch an RGBA page"
        );
        expect(
            pool.residentBytes()
                == (qint64)gl::PagePool::kPageBytes
                    + (qint64)gl::PagePool::kSlotsPerAtlas * gl::PagePool::pageBytes(gl::PagePool::Format_R8),
            "P3 residentBytes must sum each page's own format"
        );
    }
    pool.purge();

    // P4: THE UPLOAD STAGE. The bytes a landing frame fills into the mapped stage must be the
    // bytes its slot holds after endStaging(), for BOTH formats and at each format's own page
    // size; a full stage must refuse the page (the caller then uploads direct) instead of writing
    // past its buffer; and the counters the gates read must count exactly what was copied.
    if (!gl::PagePool::stagingEnabled()) {
        std::printf("  [SKIP] AE_VT_STAGE=0 is the direct-upload control\n");
    } else {
        // both stacks: the R8 page needs its own atlas, and a budget of one RGBA atlas refuses it
        pool.setBudgetBytes(
            (qint64)gl::PagePool::kAtlasBytes + gl::PagePool::atlasBytes(gl::PagePool::Format_R8)
        );
        pool.beginFrame();
        expect(pool.beginStaging(), "P4 the stage must open");
        const qint64 stagedBefore = pool.stagedPageTotal();

        // RGBA8: a position-dependent pattern, so a wrong offset or pitch cannot pass
        std::vector<uint8_t> rgba((size_t)gl::PagePool::kPitch * gl::PagePool::kPitch * 4, 0);
        for (int y = 0; y < gl::PagePool::kPitch; ++y)
            for (int x = 0; x < gl::PagePool::kPitch; ++x) {
                uint8_t* p = rgba.data() + ((size_t)y * gl::PagePool::kPitch + x) * 4;
                p[0] = (uint8_t)((x * 3 + y) & 255);
                p[1] = (uint8_t)((x + y * 7) & 255);
                p[2] = 91;
                p[3] = 255;
            }
        gl::PagePool::Slot rgbaSlot;
        expect(pool.acquire(pageId(10, 0, 0, 0), &log, &rgbaSlot), "P4 acquire the staged RGBA page");
        const qint64 rgbaOffset = pool.reservePage(gl::PagePool::Format_RGBA8);
        expect(rgbaOffset >= 0, "P4 the stage must reserve an RGBA page");
        uint8_t* staged = pool.stagedPage(rgbaOffset);
        expect(staged != nullptr, "P4 the reserved page must be mapped");
        if (staged) {
            std::memcpy(staged, rgba.data(), rgba.size());
            pool.stageCopy(rgbaSlot, gl::PagePool::Format_RGBA8, rgbaOffset);
        }

        // R8: the other format, at its own page size, in the same stage
        std::vector<uint8_t> red((size_t)gl::PagePool::kPitch * gl::PagePool::kPitch, 0);
        for (size_t i = 0; i < red.size(); ++i)
            red[i] = (uint8_t)((i * 13) & 255);
        gl::PagePool::Slot r8Slot;
        expect(
            pool.acquire(pageId(11, 0, 0, 0), &log, &r8Slot, gl::PagePool::Format_R8),
            "P4 acquire the staged R8 page"
        );
        const qint64 r8Offset = pool.reservePage(gl::PagePool::Format_R8);
        expect(
            r8Offset == rgbaOffset + gl::PagePool::pageBytes(gl::PagePool::Format_RGBA8),
            "P4 the second page must follow the first at its own page size"
        );
        uint8_t* stagedRed = pool.stagedPage(r8Offset);
        expect(stagedRed != nullptr, "P4 the reserved R8 page must be mapped");
        if (stagedRed) {
            std::memcpy(stagedRed, red.data(), red.size());
            pool.stageCopy(r8Slot, gl::PagePool::Format_R8, r8Offset);
        }

        pool.endStaging();
        expect(pool.stagedPageTotal() == stagedBefore + 2, "P4 the stage must count both pages");
        expect(pool.stagedPages() == 2, "P4 this frame's staged count must be 2");
        expect(pool.stagedPage(rgbaOffset) == nullptr, "P4 the stage must be unmapped after endStaging");

        std::vector<uint8_t> back(rgba.size(), 0);
        expect(
            pool.readSlot(rgbaSlot, back.data()) && back == rgba,
            "P4 the staged RGBA bytes must be the slot's bytes"
        );
        std::vector<uint8_t> backRed(red.size(), 0);
        expect(
            pool.readSlot(r8Slot, backRed.data()) && backRed == red,
            "P4 the staged R8 bytes must be the slot's bytes"
        );

        // the stage's end: it fills, then refuses, and the refusal is stable (no wraparound)
        pool.beginFrame();
        expect(pool.beginStaging(), "P4 the stage must reopen for a second frame");
        int reserved = 0;
        while (reserved < 1000 && pool.reservePage(gl::PagePool::Format_RGBA8) >= 0)
            ++reserved;
        expect(
            reserved > 0 && reserved < 1000,
            QString("P4 the stage must fill and then refuse (%1 pages of the %2 MB cap)")
                .arg(reserved)
                .arg(gl::PagePool::defaultUploadCapBytes() / (1024 * 1024))
        );
        expect(pool.reservePage(gl::PagePool::Format_RGBA8) < 0, "P4 a full stage must keep refusing");
        pool.endStaging();
        pool.purge();
    }

    pool.setBudgetBytes(gl::PagePool::defaultBudgetBytes());
}

void suiteVirtualTexture() {
    suiteHeader("V1 paged layer vs the full-resolution control");
    if (!gl::PagePool::enabled()) {
        std::printf("  [SKIP] AE_VIRTUAL_TEXTURE=0 is the control path\n");
        return;
    }

    const QSize canvas(320, 320);
    // Noise, not the smooth curve: the residual bugs this suite pins are filtering differences
    // that a ramp reduces to 1/255, while noise turns the same difference into tens of units.
    const Img src = makeNoise(900, 700);
    const uint8_t black[3] = {0, 0, 0};

    auto& fixtures = renderFixtures();
    const QString fixtureKey = QString("%1x%2").arg(canvas.width()).arg(canvas.height());
    auto it = fixtures.find(fixtureKey);
    if (it == fixtures.end()) {
        std::unique_ptr<RenderFixture> fx(new RenderFixture());
        fx->init(canvas);
        it = fixtures.emplace(fixtureKey, std::move(fx)).first;
    }
    RenderFixture& fx = *it->second;

    core::CameraInfo camera;
    camera.reset(canvas, 1.0, canvas, QPoint(0, 0));

    // A tree per case: the scale key is laid out at the first blend, and changing it after a
    // render needs the timeline's cache invalidated - building the fixture the way the app builds
    // it (setDefaultImage/setDefaultPosture, then a ScaleKey) is the state the renderer sees.
    struct Case {
        float scale;
        float scaleY;
        int maxTolerance;
        int minPages;
    };
    // What each case pins, all on the noise source:
    //   0.25 - the coarse tier's read: the SAME reconstruction on the full chain's own bytes.
    //     A single bilinear tap there measured 816 px over 8/255 (max 115) on nene's Layer 6.
    //   0.0625x0.25 - the same tier with a 4:1 footprint, eight level texels along the major
    //     axis, where one tap is a visibly different integral from the two taps.
    //   0.3 - level 0 paged with the layer's own edges on screen: the border read and the
    //     per-tap page resolve.
    //   0.5, 1.0 - the paged levels and the magnified read (this layer is larger than the
    //     canvas, so only its interior is compared).
    //   0.6 - a 1.67-texel footprint: lod = log2(minor) - 1.35 <= 0 enters the magnified branch
    //     while the unbiased footprint is still minifying, so the control's read must be level 0
    //     explicitly (an implicit texture() measured a mean of 0.149 of 255 on the old fixture).
    //   2.0 - MAGNIFIED, where the demand's affine fast path (AE_VT_AFFINE_DEMAND, the default)
    //     marks the quad's page extent instead of walking the grid. On nene this case is the one
    //     that exposed the mesh walk's over-demand: at 2x the walk demanded 5,834 pages of which
    //     the pool could land 217, so most of the view fell back to the coarse chain (92,701
    //     samples from the control) while the quad demanded 18 and matched it within 1. The
    //     fixture here is too small to exhaust the pool, so it pins the quad's correctness at
    //     magnification rather than that failure.
    // A 1-unit tolerance is headroom for driver sub-texel rounding, not for a wrong read; each
    // bug class above measures far more than it on this source. 0.3 carries 2: its outermost
    // content row is a half-texel border blend, and the same rounding lands 2 units there on
    // noise.
    const Case cases[] = {
        {0.25f, 0.25f, 1, 0}, {0.0625f, 0.25f, 1, 0}, {0.5f, 0.5f, 1, 1}, {1.0f, 1.0f, 1, 1},
        {0.3f, 0.3f, 2, 12}, {0.6f, 0.6f, 1, 1}, {2.0f, 2.0f, 1, 1}
    };

    for (const Case& c : cases) {
        const QString scaleText = c.scaleY == c.scale
            ? QString::number(c.scale)
            : QString("%1x%2").arg(c.scale).arg(c.scaleY);
        core::ObjectTree tree;
        auto* root = new core::FolderNode("root");
        tree.grabTopNode(root);
        auto* res = makeResource(QString("vt_src_%1").arg(scaleText), src);
        core::LayerNode* layer = addScaledLayer(
            tree,
            root,
            res,
            "layer",
            QVector2D(canvas.width() * 0.5f, canvas.height() * 0.5f),
            c.scale,
            img::BlendMode_Normal
        );
        if (c.scaleY != c.scale) {
            if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Scale)) {
                layer->timeLine()->setChannelBaseValue(core::TimeKeyType_Scale, 0, c.scale);
                layer->timeLine()->setChannelBaseValue(core::TimeKeyType_Scale, 1, c.scaleY);
            } else {
                core::getOrCreateBaseValue<core::ScaleKey, core::TimeKeyType_Scale>(*layer->timeLine())
                    ->setScale(QVector2D(c.scale, c.scaleY));
            }
        }
        auto* imageKey = dynamic_cast<core::ImageKey*>(layer->timeLine()->baseValue(core::TimeKeyType_Image));
        expect(imageKey != nullptr, QString("V1 scale %1: the layer must have an image key").arg(scaleText));
        if (!imageKey)
            continue;

        imageKey->setForceFullTexture(false);
        std::vector<uint8_t> paged;
        for (int i = 0; i < 8; ++i)
            paged = fx.render(tree, camera, 0, black);

        const core::VirtualTexture* vt = imageKey->cache().virtualTexture();
        expect(vt != nullptr, QString("V1 scale %1: the 900x700 image must take the paged path").arg(scaleText));
        if (!vt)
            continue;
        const int wanted = vt->wantedPageCount();
        const int resident = vt->residentWantedCount();
        const int coarseLevel = vt->coarseLevel();
        if (wanted >= c.minPages && c.minPages > 0) {
            expect(
                resident == wanted,
                QString("V1 scale %1: every wanted page must be resident (%2 of %3)")
                    .arg(scaleText)
                    .arg(resident)
                    .arg(wanted)
            );
        }

        imageKey->setForceFullTexture(true);
        const std::vector<uint8_t> control = fx.render(tree, camera, 0, black);
        expect(
            imageKey->cache().texture().id() != 0,
            QString("V1 scale %1: the control path must have a full texture").arg(scaleText)
        );

        int maxDelta = 0;
        double meanDelta = 0.0;
        int n = 0;
        int maxX = 0, maxY = 0, maxC = 0;
        for (int y = 16; y < canvas.height() - 16; ++y) {
            for (int x = 16; x < canvas.width() - 16; ++x) {
                const size_t i = ((size_t)y * canvas.width() + x) * 4;
                for (int ch = 0; ch < 4; ++ch) {
                    const int d = std::abs((int)paged[i + ch] - (int)control[i + ch]);
                    if (d > maxDelta) {
                        maxDelta = d;
                        maxX = x;
                        maxY = y;
                        maxC = ch;
                    }
                    meanDelta += d;
                    ++n;
                }
            }
        }
        meanDelta = n ? meanDelta / n : 0.0;
        expect(
            wanted >= c.minPages,
            QString("V1 scale %1: the paged levels must be demanded (%2 pages, at least %3)")
                .arg(scaleText)
                .arg(wanted)
                .arg(c.minPages)
        );
        const int failsBefore = gFails;
        expect(
            maxDelta <= c.maxTolerance,
            QString("V1 scale %1: paged vs full resolution max delta %2 at (%3,%4) ch %5, tolerance %6")
                .arg(scaleText)
                .arg(maxDelta)
                .arg(maxX)
                .arg(maxY)
                .arg(maxC)
                .arg(c.maxTolerance)
        );
        expect(
            meanDelta < 0.5,
            QString("V1 scale %1: mean delta %2 below 0.5 of 255").arg(scaleText).arg(meanDelta, 0, 'f', 3)
        );
        caseReport(
            QString("scale %1: levels 0-%2 paged, %3 wanted").arg(scaleText).arg(coarseLevel - 1).arg(wanted),
            gFails == failsBefore,
            QString("max %1 mean %2 of 255").arg(maxDelta).arg(meanDelta, 0, 'f', 3)
        );
    }

    // NO HOLES with the pool emptied: the pages are gone (purge) and cannot be re-acquired (the
    // budget is below one atlas), and the layer must still draw through the coarse chain.
    {
        core::ObjectTree tree;
        auto* root = new core::FolderNode("root");
        tree.grabTopNode(root);
        auto* res = makeResource("vt_src_holes", src);
        core::LayerNode* layer = addScaledLayer(
            tree,
            root,
            res,
            "layer",
            QVector2D(canvas.width() * 0.5f, canvas.height() * 0.5f),
            1.0f,
            img::BlendMode_Normal
        );
        auto* imageKey = dynamic_cast<core::ImageKey*>(layer->timeLine()->baseValue(core::TimeKeyType_Image));
        if (!imageKey)
            return;

        imageKey->setForceFullTexture(true);
        const std::vector<uint8_t> full = fx.render(tree, camera, 0, black);
        imageKey->setForceFullTexture(false);

        const qint64 savedBudget = gl::PagePool::instance().budgetBytes();
        gl::PagePool::instance().setBudgetBytes(1);
        gl::PagePool::instance().purge();
        std::vector<uint8_t> noPages;
        for (int i = 0; i < 4; ++i)
            noPages = fx.render(tree, camera, 0, black);
        gl::PagePool::instance().setBudgetBytes(savedBudget);

        int coveredNoPages = 0;
        int coveredFull = 0;
        for (int y = 16; y < canvas.height() - 16; ++y) {
            for (int x = 16; x < canvas.width() - 16; ++x) {
                const size_t i = ((size_t)y * canvas.width() + x) * 4;
                coveredNoPages += noPages[i + 3] > 0;
                coveredFull += full[i + 3] > 0;
            }
        }
        expect(coveredFull > 70000, QString("V1 the control must cover the interior (%1 px)").arg(coveredFull));
        expect(
            coveredNoPages > coveredFull * 9 / 10,
            QString("V1 with no page resident the coarse chain must still cover the layer (%1 of %2 px)")
                .arg(coveredNoPages)
                .arg(coveredFull)
        );
    }
}


// CL1: THE CLIPPER PASS. A clipper layer is singulated into a mask and its clipped sibling draws
// through it. The clipper's own sampler used to be the hardware's implicit-LOD read while the
// layer used the two-tap reconstruction; both now take LayerSampling.glsl, so this gate compares
// the paged path (pages + coarse) against the full-resolution control on a real clipped pair.
void suiteClipper() {
    suiteHeader("CL1 clipper singulation: paged vs the full-resolution control");
    if (!gl::PagePool::enabled()) {
        std::printf("  [SKIP] AE_VIRTUAL_TEXTURE=0 is the control path\n");
        return;
    }

    const QSize canvas(320, 320);
    const uint8_t black[3] = {0, 0, 0};
    const Img clippedSrc = makeSolid(900, 700, 40, 80, 220);
    const Img clipperSrc = makeDisc(900, 700, 30, 200, 90);

    auto& fixtures = renderFixtures();
    const QString fixtureKey = QString("%1x%2").arg(canvas.width()).arg(canvas.height());
    auto it = fixtures.find(fixtureKey);
    if (it == fixtures.end()) {
        std::unique_ptr<RenderFixture> fx(new RenderFixture());
        fx->init(canvas);
        it = fixtures.emplace(fixtureKey, std::move(fx)).first;
    }
    RenderFixture& fx = *it->second;

    core::CameraInfo camera;
    camera.reset(canvas, 1.0, canvas, QPoint(0, 0));

    struct Pair {
        core::LayerNode* clipped = nullptr;
        core::LayerNode* clipper = nullptr;
    };
    const auto build = [&](core::ObjectTree& aTree, const QString& aPrefix, bool aClipped) {
        Pair pair;
        auto* root = new core::FolderNode("root");
        aTree.grabTopNode(root);
        auto* clippedRes = makeResource(aPrefix + "_clipped", clippedSrc);
        auto* clipperRes = makeResource(aPrefix + "_clipper", clipperSrc);
        pair.clipped = addScaledLayer(
            aTree, root, clippedRes, "clipped", QVector2D(canvas.width() * 0.5f, canvas.height() * 0.5f),
            0.6f, img::BlendMode_Normal
        );
        pair.clipper = addScaledLayer(
            aTree, root, clipperRes, "clipper", QVector2D(canvas.width() * 0.5f, canvas.height() * 0.5f),
            0.6f, img::BlendMode_Normal
        );
        pair.clipped->setClipped(aClipped);
        return pair;
    };
    const auto keyOf = [](core::LayerNode* aLayer) {
        return aLayer ? dynamic_cast<core::ImageKey*>(
                            aLayer->timeLine()->baseValue(core::TimeKeyType_Image))
                      : nullptr;
    };

    // the clipped pair, paged (the demand has to land: eight frames)
    core::ObjectTree pagedTree;
    const Pair pagedPair = build(pagedTree, "cl_paged", true);
    std::vector<uint8_t> paged;
    for (int i = 0; i < 8; ++i)
        paged = fx.render(pagedTree, camera, 0, black);

    const auto* clippedKey = keyOf(pagedPair.clipped);
    const auto* clipperKey = keyOf(pagedPair.clipper);
    expect(clippedKey != nullptr && clipperKey != nullptr, "CL1 both layers must have image keys");
    if (!clippedKey || !clipperKey)
        return;
    expect(
        clippedKey->cache().virtualTexture() != nullptr,
        "CL1 the clipped layer must take the paged path"
    );
    expect(
        clipperKey->cache().virtualTexture() != nullptr,
        "CL1 the clipper layer must take the paged path too (the clipper exclusion is gone)"
    );
    // the mask's pages must be DEMANDED, not just left to the coarse chain: a clipper whose
    // demand was never marked would fall back everywhere and this pair would differ by far
    // more than the edge rounding below
    if (const core::VirtualTexture* clipperVt = clipperKey->cache().virtualTexture()) {
        expect(clipperVt->wantedPageCount() > 0, "CL1 the clipper pass must demand pages");
        expect(
            clipperVt->residentWantedCount() == clipperVt->wantedPageCount(),
            QString("CL1 every wanted clipper page must be resident (%1 of %2)")
                .arg(clipperVt->residentWantedCount())
                .arg(clipperVt->wantedPageCount())
        );
    }

    // the full-resolution control for the same tree
    auto* clippedMutable = const_cast<core::ImageKey*>(clippedKey);
    auto* clipperMutable = const_cast<core::ImageKey*>(clipperKey);
    clippedMutable->setForceFullTexture(true);
    clipperMutable->setForceFullTexture(true);
    const std::vector<uint8_t> control = fx.render(pagedTree, camera, 0, black);
    expect(
        clippedKey->cache().texture().id() != 0 && clipperKey->cache().texture().id() != 0,
        "CL1 the control must build both full textures"
    );

    int maxDelta = 0, maxX = 0, maxY = 0, maxC = 0;
    double meanDelta = 0.0;
    int n = 0;
    for (int y = 16; y < canvas.height() - 16; ++y) {
        for (int x = 16; x < canvas.width() - 16; ++x) {
            const size_t i = ((size_t)y * canvas.width() + x) * 4;
            for (int ch = 0; ch < 4; ++ch) {
                const int d = std::abs((int)paged[i + ch] - (int)control[i + ch]);
                if (d > maxDelta) {
                    maxDelta = d;
                    maxX = x;
                    maxY = y;
                    maxC = ch;
                }
                meanDelta += d;
                ++n;
            }
        }
    }
    meanDelta = n ? meanDelta / n : 0.0;
    // The mask is a COVERAGE field with a hard-ish edge: the atlas read's sub-texel rounding
    // moves one edge pixel by up to 2/255, which no visual differs by. The page-path bug this
    // gate exists for (a coarse-only mask) is tens of units across the whole disc.
    expect(
        maxDelta <= 2,
        QString("CL1 paged clipper vs control max delta %1 at (%2,%3) ch %4").arg(maxDelta).arg(maxX).arg(maxY).arg(maxC)
    );
    expect(meanDelta < 0.5, QString("CL1 mean delta %1 below 0.5 of 255").arg(meanDelta, 0, 'f', 3));

    // the mask is ACTIVE: the clipped frame is covered in the middle and empty in a corner that
    // the same pair covers when the clipped layer is not clipped at all
    const auto at = [&](const std::vector<uint8_t>& aFrame, int aX, int aY, int aCh) {
        return (int)aFrame[((size_t)aY * canvas.width() + aX) * 4 + aCh];
    };
    // the backdrop is OPAQUE black, so "covered" is RGB above zero, not alpha
    int covered = 0;
    for (int y = 16; y < canvas.height() - 16; ++y)
        for (int x = 16; x < canvas.width() - 16; ++x)
            covered += (at(paged, x, y, 0) | at(paged, x, y, 1) | at(paged, x, y, 2)) > 0;
    const auto rgbCovered = [&](const std::vector<uint8_t>& aFrame, int aX, int aY) {
        return (at(aFrame, aX, aY, 0) | at(aFrame, aX, aY, 1) | at(aFrame, aX, aY, 2)) > 0;
    };

    core::ObjectTree plainTree;
    build(plainTree, "cl_plain", false);
    std::vector<uint8_t> plain;
    for (int i = 0; i < 8; ++i)
        plain = fx.render(plainTree, camera, 0, black);

    expect(covered > 20000, QString("CL1 the clipped pair must cover the middle (%1 px)").arg(covered));
    expect(
        !rgbCovered(paged, 8, 8),
        "CL1 the disc must not reach the corner (the clipper's mask is doing something)"
    );
    expect(
        rgbCovered(plain, 8, 8),
        "CL1 without clipping the same pair must cover the corner (the excuse has an occupied side)"
    );
    expect(
        rgbCovered(paged, 160, 160),
        "CL1 the disc must cover the centre"
    );

    caseReport(
        "paged clipped pair vs full resolution",
        gFails == 0,
        QString("max %1 mean %2 of 255, %3 covered").arg(maxDelta).arg(meanDelta, 0, 'f', 3).arg(covered)
    );
}

// MA1: A MASKED LAYER ON THE PAGED PATH. LAYER_MASK and VIRTUAL_TEXTURE are independent
// variations, but the mask is sampled by the same fragment as the paged image, through its own
// matrix, rect and unit - a combination no gate drew. This also measures what a mask costs: the
// field is R8 at the content's own resolution and the chain adds a third.
void suiteMaskPaged() {
    suiteHeader("MA1 layer mask on a paged layer");
    if (!gl::PagePool::enabled()) {
        std::printf("  [SKIP] AE_VIRTUAL_TEXTURE=0 is the control path\n");
        return;
    }

    const QSize canvas(320, 320);
    const uint8_t black[3] = {0, 0, 0};
    const Img layerSrc = makeNoise(900, 700);

    auto& fixtures = renderFixtures();
    const QString fixtureKey = QString("%1x%2").arg(canvas.width()).arg(canvas.height());
    auto it = fixtures.find(fixtureKey);
    if (it == fixtures.end()) {
        std::unique_ptr<RenderFixture> fx(new RenderFixture());
        fx->init(canvas);
        it = fixtures.emplace(fixtureKey, std::move(fx)).first;
    }
    RenderFixture& fx = *it->second;

    core::CameraInfo camera;
    camera.reset(canvas, 1.0, canvas, QPoint(0, 0));

    core::ObjectTree tree;
    auto* root = new core::FolderNode("root");
    tree.grabTopNode(root);
    auto* res = makeResource("ma_src", layerSrc);
    core::LayerNode* layer = addScaledLayer(
        tree, root, res, "layer", QVector2D(canvas.width() * 0.5f, canvas.height() * 0.5f),
        0.6f, img::BlendMode_Normal
    );
    auto* imageKey = dynamic_cast<core::ImageKey*>(layer->timeLine()->baseValue(core::TimeKeyType_Image));
    expect(imageKey != nullptr, "MA1 the layer must have an image key");
    if (!imageKey)
        return;

    // THE FIELD: hidden on its left half, revealed on its right - a hard vertical edge, so the
    // screen's left half of the layer must be empty and its right half covered
    auto* maskKey = new core::MaskKey();
    layer->timeLine()->setBaseValue(core::TimeKeyType_Mask, maskKey);
    maskKey->data().alloc(QSize(900, 700), QRectF(0, 0, 900, 700), core::MaskKey::kNeutral);
    QImage& field = maskKey->data().field();
    for (int y = 0; y < field.height(); ++y)
        std::memset(field.scanLine(y), 0, 450);
    maskKey->data().markDirty(QRect(0, 0, 450, 700));
    expect(maskKey->data().isPainted(), "MA1 the painted field must report itself painted");

    imageKey->setForceFullTexture(false);
    std::vector<uint8_t> paged;
    for (int i = 0; i < 8; ++i)
        paged = fx.render(tree, camera, 0, black);
    expect(
        imageKey->cache().virtualTexture() != nullptr,
        "MA1 the masked layer's image must take the paged path"
    );

    // THE MASK ITSELF MUST BE PAGED, not just drawn beside a paged image: the page pool has an R8
    // stack and the mask has its own table/coarse chain (MaskKey::Data::virtualTexture). The
    // demand pass runs every frame, so by now every wanted page should be resident.
    {
        core::VirtualTexture* maskVT = maskKey->data().virtualTexture();
        expect(maskVT != nullptr, "MA1 the matte must take the paged path");
        if (maskVT) {
            expect(maskVT->isMask(), "MA1 the matte's resource must be an R8 mask");
            expect(
                maskVT->size() == QSize(900, 700),
                QString("MA1 the matte's grid is the field's (%1x%2)")
                    .arg(maskVT->size().width()).arg(maskVT->size().height())
            );
            expect(maskVT->wantedPageCount() > 0, "MA1 the matte must have wanted pages");
            expect(
                maskVT->residentWantedCount() == maskVT->wantedPageCount(),
                QString("MA1 every wanted matte page must be resident (%1 of %2)")
                    .arg(maskVT->residentWantedCount()).arg(maskVT->wantedPageCount())
            );
        }
    }

    imageKey->setForceFullTexture(true);
    const std::vector<uint8_t> control = fx.render(tree, camera, 0, black);

    int maxDelta = 0, maxX = 0, maxY = 0, maxC = 0;
    double meanDelta = 0.0;
    int n = 0;
    for (int y = 16; y < canvas.height() - 16; ++y) {
        for (int x = 16; x < canvas.width() - 16; ++x) {
            const size_t i = ((size_t)y * canvas.width() + x) * 4;
            for (int ch = 0; ch < 4; ++ch) {
                const int d = std::abs((int)paged[i + ch] - (int)control[i + ch]);
                if (d > maxDelta) {
                    maxDelta = d;
                    maxX = x;
                    maxY = y;
                    maxC = ch;
                }
                meanDelta += d;
                ++n;
            }
        }
    }
    meanDelta = n ? meanDelta / n : 0.0;
    expect(
        maxDelta <= 1,
        QString("MA1 masked paged layer vs control max delta %1 at (%2,%3) ch %4")
            .arg(maxDelta).arg(maxX).arg(maxY).arg(maxC)
    );
    expect(meanDelta < 0.5, QString("MA1 mean delta %1 below 0.5 of 255").arg(meanDelta, 0, 'f', 3));

    // the mask's edge on screen is content x = 450 -> device x = 160; count coverage on each side
    const auto coveredIn = [&](int aX0, int aX1) {
        int count = 0;
        for (int y = 20; y < 300; ++y)
            for (int x = aX0; x < aX1; ++x)
                count += (paged[((size_t)y * canvas.width() + x) * 4] |
                          paged[((size_t)y * canvas.width() + x) * 4 + 1] |
                          paged[((size_t)y * canvas.width() + x) * 4 + 2]) > 0;
        return count;
    };
    const int hiddenPx = coveredIn(10, 150);
    const int visiblePx = coveredIn(170, 310);
    expect(hiddenPx == 0, QString("MA1 the masked half must be empty (%1 px)").arg(hiddenPx));
    expect(visiblePx > 10000, QString("MA1 the revealed half must be covered (%1 px)").arg(visiblePx));

    // THE COST: the field's RAM and the R8 chain's VRAM, at this size and at a 4096^2 field
    const gl::Texture* maskTex = maskKey->data().textureId();
    qint64 chainBytes = 0;
    if (maskTex) {
        QSize level = maskTex->size();
        for (int l = 0; l < maskTex->mipmapLevels(); ++l) {
            chainBytes += (qint64)level.width() * level.height();
            level = QSize(std::max(1, level.width() / 2), std::max(1, level.height() / 2));
        }
    }
    core::MaskKey::Data big;
    big.alloc(QSize(4096, 4096), QRectF(0, 0, 4096, 4096), 0);
    qint64 bigBytes = 0;
    if (const gl::Texture* bigTex = big.textureId()) {
        QSize level = bigTex->size();
        for (int l = 0; l < bigTex->mipmapLevels(); ++l) {
            bigBytes += (qint64)level.width() * level.height();
            level = QSize(std::max(1, level.width() / 2), std::max(1, level.height() / 2));
        }
    }
    caseReport(
        "mask + paged image",
        gFails == 0,
        QString("max %1 mean %2 of 255; field %3 B, 900x700 chain %4 B, 4096^2 chain %5 B")
            .arg(maxDelta).arg(meanDelta, 0, 'f', 3)
            .arg((qint64)900 * 700).arg(chainBytes).arg(bigBytes)
    );
}

// MX3: THE PAGED MATTE'S STROKE PATH. MA1 pages a mask whose field was finished before the first
// upload; this gate paints into it while the paged resource is live, so the box path is what runs:
// MaskPyramid::updateBox for the levels, the coarse chain's box uploads and the RESIDENT pages'
// re-fills (MaskKey::Data::markDirty -> VirtualTexture::updateMaskBox). The reference is the full
// chain built from the SAME field, which is what the forced-full control draws.
void suiteMaskPagedUpdate() {
    suiteHeader("MX3 paged mask: a stroke updates the paged matte");
    if (!gl::PagePool::enabled()) {
        std::printf("  [SKIP] AE_VIRTUAL_TEXTURE=0 is the control path\n");
        return;
    }

    const QSize canvas(320, 320);
    const uint8_t black[3] = {0, 0, 0};
    // a WHITE opaque image: the frame's pixel IS the matte's value, so an edge difference is
    // read in mask units instead of being scaled by whatever the noise happened to hold
    Img layerSrc;
    layerSrc.w = 900;
    layerSrc.h = 700;
    layerSrc.d.assign((size_t)900 * 700 * 4, 255);

    auto& fixtures = renderFixtures();
    const QString fixtureKey = QString("%1x%2-mx3").arg(canvas.width()).arg(canvas.height());
    auto it = fixtures.find(fixtureKey);
    if (it == fixtures.end()) {
        std::unique_ptr<RenderFixture> fx(new RenderFixture());
        fx->init(canvas);
        it = fixtures.emplace(fixtureKey, std::move(fx)).first;
    }
    RenderFixture& fx = *it->second;

    core::CameraInfo camera;
    camera.reset(canvas, 1.0, canvas, QPoint(0, 0));

    core::ObjectTree tree;
    auto* root = new core::FolderNode("root");
    tree.grabTopNode(root);
    auto* res = makeResource("mx3_src", layerSrc);
    core::LayerNode* layer = addScaledLayer(
        tree, root, res, "layer", QVector2D(canvas.width() * 0.5f, canvas.height() * 0.5f),
        0.6f, img::BlendMode_Normal
    );
    auto* imageKey = dynamic_cast<core::ImageKey*>(layer->timeLine()->baseValue(core::TimeKeyType_Image));
    expect(imageKey != nullptr, "MX3 the layer must have an image key");
    if (!imageKey)
        return;

    // the field starts hidden on its left half and revealed on its right; the stroke below hides
    // the middle band, leaving only content x >= 675 visible (device x >= ~295 at scale 0.6)
    auto* maskKey = new core::MaskKey();
    layer->timeLine()->setBaseValue(core::TimeKeyType_Mask, maskKey);
    maskKey->data().alloc(QSize(900, 700), QRectF(0, 0, 900, 700), core::MaskKey::kNeutral);
    QImage& field = maskKey->data().field();
    for (int y = 0; y < field.height(); ++y)
        std::memset(field.scanLine(y), 0, 450);
    maskKey->data().markDirty(QRect(0, 0, 450, 700));

    imageKey->setForceFullTexture(false);
    std::vector<uint8_t> paged;
    for (int i = 0; i < 8; ++i)
        paged = fx.render(tree, camera, 0, black);

    core::VirtualTexture* maskVT = maskKey->data().virtualTexture();
    expect(maskVT != nullptr, "MX3 the matte must take the paged path");
    if (!maskVT)
        return;
    expect(
        maskVT->wantedPageCount() > 0 && maskVT->residentWantedCount() == maskVT->wantedPageCount(),
        QString("MX3 the pages must be resident before the stroke (%1 of %2)")
            .arg(maskVT->residentWantedCount()).arg(maskVT->wantedPageCount())
    );

    // THE STROKE. Paint the middle band hidden and report the box exactly as the brush does; the
    // paged resource has no full chain, so the only way this reaches the screen is the box path.
    for (int y = 0; y < field.height(); ++y)
        std::memset(field.scanLine(y) + 450, 0, 225);
    maskKey->data().markDirty(QRect(450, 0, 225, 700));
    // THE EDITOR'S OWN NOTIFICATION, in the gate's terms: a paint detaches the field and changes
    // its cacheKey, and the expansion only sees it when the timeline re-blends (the app does this
    // from onTimeLineModified). Without it the expans keeps the pre-paint field and the stroke is
    // invisible to every renderer, paged or not.
    {
        core::TimeInfo time;
        time.fps = 24;
        time.frameRange = util::Range(0, 200);
        time.loop = false;
        time.frame = core::Frame(0);
        core::TimeKeyBlender blender(*root, false);
        blender.updateCurrents(root, time);
    }
    for (int i = 0; i < 8; ++i)
        paged = fx.render(tree, camera, 0, black);

    const auto coveredIn = [&](const std::vector<uint8_t>& aFrame, int aX0, int aX1) {
        int count = 0;
        for (int y = 20; y < 300; ++y)
            for (int x = aX0; x < aX1; ++x)
                count += (aFrame[((size_t)y * canvas.width() + x) * 4] |
                          aFrame[((size_t)y * canvas.width() + x) * 4 + 1] |
                          aFrame[((size_t)y * canvas.width() + x) * 4 + 2]) > 0;
        return count;
    };
    expect(coveredIn(paged, 10, 150) == 0, "MX3 the painted-over band must be hidden");
    expect(coveredIn(paged, 170, 290) == 0, "MX3 the revealed half the stroke covers must be hidden");
    expect(coveredIn(paged, 300, 315) > 1000, "MX3 the part the stroke left alone must stay visible");

    // THE REFERENCE: the same field through a full chain (the control's mask path)
    imageKey->setForceFullTexture(true);
    const std::vector<uint8_t> control = fx.render(tree, camera, 0, black);

    int maxDelta = 0, maxX = 0, maxY = 0, maxC = 0;
    double meanDelta = 0.0;
    int n = 0;
    for (int y = 16; y < canvas.height() - 16; ++y) {
        for (int x = 16; x < canvas.width() - 16; ++x) {
            const size_t i = ((size_t)y * canvas.width() + x) * 4;
            for (int ch = 0; ch < 4; ++ch) {
                const int d = std::abs((int)paged[i + ch] - (int)control[i + ch]);
                if (d > maxDelta) {
                    maxDelta = d;
                    maxX = x;
                    maxY = y;
                    maxC = ch;
                }
                meanDelta += d;
                ++n;
            }
        }
    }
    meanDelta = n ? meanDelta / n : 0.0;
    expect(
        maxDelta <= 3,
        QString("MX3 a stroke's box vs a rebuilt full chain: max %1 at (%2,%3) ch %4")
            .arg(maxDelta).arg(maxX).arg(maxY).arg(maxC)
    );
    expect(meanDelta < 0.5, QString("MX3 mean delta %1 below 0.5 of 255").arg(meanDelta, 0, 'f', 3));

    // THE RUBYLITH PAGES WITH THE MATTE. The mask tool's preview is a separate pass
    // (FilterFrame::maskOverlayTexture with MASK_VIRTUAL_TEXTURE), and it is what the artist
    // paints against: a paged matte has no full chain, so the overlay must draw from the page
    // stack. The white image makes the tint unambiguous; the hidden band is the middle third.
    const auto pixelAt = [&](const std::vector<uint8_t>& aFrame, int aX, int aY) {
        const size_t i = ((size_t)aY * canvas.width() + aX) * 4;
        return std::array<int, 3>{aFrame[i], aFrame[i + 1], aFrame[i + 2]};
    };
    imageKey->setForceFullTexture(false);
    const std::vector<uint8_t> pagedPreview =
        fx.render(tree, camera, 0, black, false, false, maskKey, layer, QColor(255, 0, 0, 128));
    const auto hiddenPx = pixelAt(pagedPreview, 200, 100);
    const auto shownPx = pixelAt(pagedPreview, 310, 100);
    expect(
        hiddenPx[0] > 230 && hiddenPx[1] > 80 && hiddenPx[1] < 170,
        QString("MX3 the rubylith must tint the hidden band (got %1,%2,%3)")
            .arg(hiddenPx[0]).arg(hiddenPx[1]).arg(hiddenPx[2])
    );
    expect(
        shownPx[1] > 230,
        QString("MX3 the rubylith must not tint the revealed part (got %1,%2,%3)")
            .arg(shownPx[0]).arg(shownPx[1]).arg(shownPx[2])
    );
    // the paged rubylith against the full-mask one: same field, same sampler, so only the
    // image's paging and the CPU pyramid's 1-2 LSB can differ
    imageKey->setForceFullTexture(true);
    const std::vector<uint8_t> fullPreview =
        fx.render(tree, camera, 0, black, false, false, maskKey, layer, QColor(255, 0, 0, 128));
    int previewDelta = 0;
    for (size_t i = 0; i < pagedPreview.size(); i += 4)
        previewDelta = std::max(
            previewDelta,
            std::max(
                std::abs((int)pagedPreview[i] - (int)fullPreview[i]),
                std::max(
                    std::abs((int)pagedPreview[i + 1] - (int)fullPreview[i + 1]),
                    std::abs((int)pagedPreview[i + 2] - (int)fullPreview[i + 2])
                )
            )
        );
    expect(
        previewDelta <= 3,
        QString("MX3 the paged rubylith must match the full-mask one (max %1)").arg(previewDelta)
    );

    caseReport(
        "paged matte stroke", gFails == 0,
        QString("max %1 mean %2 of 255; %3 wanted pages")
            .arg(maxDelta).arg(meanDelta, 0, 'f', 3).arg(maskVT->wantedPageCount())
    );
}

// AM1: AN ANIMATED MASK. Two mask keys on one grid (left-hidden at frame 0, right-hidden at 24), so
// every frame between them is the per-texel blend of the two fields. The paged form must not build a
// new field, chain and pool resource per frame: the blend is a VIEW over the two keys' own pyramids
// (MaskPyramid::setBlend), one resource across frames, and its content follows one integer weight.
// The gate asks: that the matte is applied at all (a blended expansion that does not report itself
// painted is invisible - the regression this started from); that the blend keeps ONE resource and
// materializes no field; that the drawn frame equals the materialized full-chain control (the
// materializing blend is the control path and the fallback, so it is a true A/B of the view); and
// that a stroke on one key reaches the view. The timings are REPORTED, not asserted, and compare
// the view with the materialized path and with a frame drawn again.
void suiteAnimatedMask() {
    suiteHeader("AM1 animated mask: a blended matte every frame");
    if (!gl::PagePool::enabled()) {
        std::printf("  [SKIP] AE_VIRTUAL_TEXTURE=0 is the control path\n");
        return;
    }

    const QSize canvas(320, 320);
    const uint8_t black[3] = {0, 0, 0};
    // THE FIELD'S SIZE IS THE MASK'S, and it can be overridden for the scaling measurement: the
    // blended matte's per-frame cost is a function of the FIELD on the materialized path and of
    // the VISIBLE PAGES plus the coarse chain on the view's, so the crossover is a measurement,
    // not a claim. AE_AM1_FIELD=4096 is the large-matte run (Release; see the design doc).
    const int fieldSide = qEnvironmentVariableIntValue("AE_AM1_FIELD");
    const QSize fieldSize = fieldSide > 0 ? QSize(fieldSide, fieldSide) : QSize(900, 700);
    const QRectF fieldRect(0, 0, fieldSize.width(), fieldSize.height());
    const Img layerSrc = makeNoise(fieldSize.width(), fieldSize.height());

    auto& fixtures = renderFixtures();
    const QString fixtureKey = QString("%1x%2").arg(canvas.width()).arg(canvas.height());
    auto it = fixtures.find(fixtureKey);
    if (it == fixtures.end()) {
        std::unique_ptr<RenderFixture> fx(new RenderFixture());
        fx->init(canvas);
        it = fixtures.emplace(fixtureKey, std::move(fx)).first;
    }
    RenderFixture& fx = *it->second;

    core::CameraInfo camera;
    camera.reset(canvas, 1.0, canvas, QPoint(0, 0));

    core::ObjectTree tree;
    auto* root = new core::FolderNode("root");
    tree.grabTopNode(root);
    auto* res = makeResource("am_src", layerSrc);
    core::LayerNode* layer = addScaledLayer(
        tree, root, res, "layer", QVector2D(canvas.width() * 0.5f, canvas.height() * 0.5f), 0.6f,
        img::BlendMode_Normal
    );

    // THE TWO KEYS, on one grid as the authoring path keeps it: the matte is left-hidden at frame 0
    // and right-hidden at frame 24, so the frames between them are new fields with a hard vertical
    // edge at content x = 450 - and frame 12 is a uniform 127. A hard edge in a SMOOTH field is what
    // tells a coarse read from a level-0 page; a moving blob would look the same at both.
    const auto makeKey = [&](int aFrame, bool aLeftHidden) {
        auto* key = new core::MaskKey();
        key->data().alloc(fieldSize, fieldRect, core::MaskKey::kNeutral);
        QImage& field = key->data().field();
        for (int y = 0; y < field.height(); ++y) {
            uchar* row = field.scanLine(y);
            if (aLeftHidden)
                std::memset(row, 0, field.width() / 2);
            else
                std::memset(row + field.width() / 2, 0, field.width() - field.width() / 2);
        }
        key->data().markDirty(QRect(0, 0, fieldSize.width(), fieldSize.height()));
        const bool adopted = layer->timeLine()->adoptKey(core::TimeKeyType_Mask, aFrame, key);
        expect(adopted, QString("AM1 the mask key at frame %1 must adopt").arg(aFrame));
        if (!adopted)
            delete key;
        return key;
    };
    core::MaskKey* key0 = makeKey(0, true);
    core::MaskKey* key1 = makeKey(24, false);

    // frames 0..24: each is a distinct blend, so the weight moves every frame
    std::vector<double> animated;
    std::vector<uint8_t> atSix;
    bool blendedFieldAtSix = false;
    bool paintedAtSix = false;
    core::VirtualTexture* viewAtSix = nullptr;
    core::VirtualTexture* viewAtSeven = nullptr;
    QElapsedTimer timer;
    for (int frame = 0; frame <= 24; ++frame) {
        timer.start();
        std::vector<uint8_t> pixels = fx.render(tree, camera, frame, black);
        const double ms = timer.nsecsElapsed() / 1000000.0;
        if (frame > 0)
            animated.push_back(ms);   // frame 0 also pays the layer's own first upload
        if (frame == 6 || frame == 7) {
            const core::MaskKey::Data& matte = layer->timeLine()->current().mask();
            core::VirtualTexture* pagedMatte = matte.virtualTexture();
            if (frame == 6) {
                atSix = std::move(pixels);
                blendedFieldAtSix = !matte.field().isNull();
                paintedAtSix = matte.isPainted();
                viewAtSix = pagedMatte;
            } else {
                viewAtSeven = pagedMatte;
            }
        }
    }

    // ONE RESOURCE ACROSS FRAMES is the whole point of the view: a matte rebuilt per frame hands
    // out a different resource (and has no resident page), and the per-frame build is what the
    // animation pays for. The second assertion is the property that makes it possible - no field
    // is materialized while the paged path draws the blend.
    expect(viewAtSix != nullptr && viewAtSix == viewAtSeven,
        "AM1 the animated matte must keep ONE pool resource across frames");
    expect(!blendedFieldAtSix,
        "AM1 the paged blend must not materialize a field (the view is its level 0)");
    expect(paintedAtSix, "AM1 the blended matte must report itself painted");

    // THE MATTE MUST BE THERE AT ALL. The same tree without a mask key is the control: if the
    // blended expansion does not report itself painted, the draw and the demand pass BOTH skip it
    // and the layer is drawn unmasked between its keys - which is exactly what a flag that only
    // alloc/write/markDirty set on the blend path used to do.
    {
        core::ObjectTree plainTree;
        auto* plainRoot = new core::FolderNode("plain");
        plainTree.grabTopNode(plainRoot);
        auto* plainRes = makeResource("am_plain_src", layerSrc);
        addScaledLayer(
            plainTree, plainRoot, plainRes, "layer",
            QVector2D(canvas.width() * 0.5f, canvas.height() * 0.5f), 0.6f, img::BlendMode_Normal
        );
        const std::vector<uint8_t> unmasked = fx.render(plainTree, camera, 6, black);
        int matteMax = 0;
        double matteMean = 0.0;
        int m = 0;
        for (int y = 8; y < canvas.height() - 8; ++y) {
            for (int x = 8; x < canvas.width() - 8; ++x) {
                const size_t i = ((size_t)y * canvas.width() + x) * 4;
                const int d = std::abs((int)atSix[i] - (int)unmasked[i]);
                matteMax = std::max(matteMax, d);
                matteMean += d;
                ++m;
            }
        }
        matteMean = m ? matteMean / m : 0.0;
        expect(
            matteMax > 32 && matteMean > 8.0,
            QString("AM1 the animated matte must be APPLIED (frame 6 against the same tree "
                    "without a matte: max %1, mean %2)")
                .arg(matteMax).arg(matteMean, 0, 'f', 2)
        );
    }

    // THE PIXELS: frame 6 against the materialized control. The matte's hard edge is the only
    // place a wrong level or a stale page can show, and at this scale the fragment reads level 0 -
    // the same integer lerp the view fills a page with - so the two must be identical.
    for (core::MaskKey* key : {key0, key1})
        key->data().setForceFullTexture(true);
    fx.render(tree, camera, 5, black);   // uncached, so the expansion takes the forced value
    const std::vector<uint8_t> control = fx.render(tree, camera, 6, black);
    int maxDelta = 0, maxX = 0, maxY = 0;
    double meanDelta = 0.0;
    int n = 0;
    for (int y = 8; y < canvas.height() - 8; ++y) {
        for (int x = 8; x < canvas.width() - 8; ++x) {
            const size_t i = ((size_t)y * canvas.width() + x) * 4;
            for (int ch = 0; ch < 4; ++ch) {
                const int d = std::abs((int)atSix[i + ch] - (int)control[i + ch]);
                if (d > maxDelta) {
                    maxDelta = d;
                    maxX = x;
                    maxY = y;
                }
                meanDelta += d;
                ++n;
            }
        }
    }
    meanDelta = n ? meanDelta / n : 0.0;
    expect(
        maxDelta <= 1,
        QString("AM1 the animated matte at full resolution vs the control: max delta %1 at (%2,%3)")
            .arg(maxDelta).arg(maxX).arg(maxY)
    );

    // A STROKE ON AN ANIMATED KEY must reach the view. The paint detaches the key's field, which
    // moves its stamp; the next blend rebuilds the key's pyramid (the damaged holder is not the
    // paged one, so there is no box to patch) and re-points the view. The paged frame must then
    // equal the materialized one again - a view left on the pre-stroke field would differ by the
    // whole patch.
    {
        for (core::MaskKey* key : {key0, key1})
            key->data().setForceFullTexture(false);
        QImage& field = key0->data().field();
        const int halfW = field.width() / 2;
        const int blockW = std::max(1, field.width() / 8);
        const int blockH = std::max(1, field.height() / 8);
        const QRect block(halfW + blockW, field.height() / 3, blockW, blockH);
        for (int y = block.top(); y <= block.bottom(); ++y)
            std::memset(field.scanLine(y) + block.left(), 0, (size_t)block.width());
        key0->data().markDirty(block);
        fx.render(tree, camera, 5, black);   // uncached: the blend re-reads both keys
        const std::vector<uint8_t> paintedPaged = fx.render(tree, camera, 6, black);
        for (core::MaskKey* key : {key0, key1})
            key->data().setForceFullTexture(true);
        fx.render(tree, camera, 5, black);
        const std::vector<uint8_t> paintedControl = fx.render(tree, camera, 6, black);
        int strokeDelta = 0;
        for (int y = 8; y < canvas.height() - 8; ++y)
            for (int x = 8; x < canvas.width() - 8; ++x) {
                const size_t i = ((size_t)y * canvas.width() + x) * 4;
                strokeDelta = std::max(strokeDelta, std::abs((int)paintedPaged[i] - (int)paintedControl[i]));
            }
        expect(
            strokeDelta <= 1,
            QString("AM1 a stroke on an animated key must reach the view (max delta %1)")
                .arg(strokeDelta)
        );
        for (core::MaskKey* key : {key0, key1})
            key->data().setForceFullTexture(false);
        fx.render(tree, camera, 4, black);   // back to the view, uncached
    }

    // ---- the timing report, three forms of the same animation:
    //   view        - the paged blend of the two keys (the shipped path);
    //   cached      - the SAME frame drawn again: the blender's cache holds the expansion, so
    //                 nothing is re-derived (the static cost of a matte);
    //   materialized- the full-resolution control: a blended field and its full chain per frame,
    //                 which is what an animated matte costs with the pool off (AE_VIRTUAL_TEXTURE=0)
    //                 and therefore the "before" this path is measured against. Not asserted - a
    //                 wall-clock gate is machine-dependent - but it is the number that decides.
    for (core::MaskKey* key : {key0, key1})
        key->data().setForceFullTexture(false);
    fx.render(tree, camera, 24, black);   // uncached: the expansion takes the unforced value
    fx.render(tree, camera, 12, black);   // builds the frame-12 matte once, lands its pages
    std::vector<double> cached;
    for (int i = 0; i < 8; ++i) {
        timer.start();
        fx.render(tree, camera, 12, black);
        cached.push_back(timer.nsecsElapsed() / 1000000.0);
    }
    for (core::MaskKey* key : {key0, key1})
        key->data().setForceFullTexture(true);
    std::vector<double> materialized;
    for (int frame = 0; frame <= 24; ++frame) {
        timer.start();
        fx.render(tree, camera, frame, black);
        if (frame > 0)
            materialized.push_back(timer.nsecsElapsed() / 1000000.0);
    }
    for (core::MaskKey* key : {key0, key1})
        key->data().setForceFullTexture(false);
    const auto stats = [](std::vector<double> aValues) {
        std::sort(aValues.begin(), aValues.end());
        return QString("%1 / %2 / %3")
            .arg(aValues.front(), 0, 'f', 2)
            .arg(aValues[aValues.size() / 2], 0, 'f', 2)
            .arg(aValues.back(), 0, 'f', 2);
    };
    std::printf(
        "  AM1 frame ms (min/median/max): view 0..24 = %s, cached frame 12 = %s, "
        "materialized 0..24 = %s\n",
        stats(animated).toUtf8().constData(), stats(cached).toUtf8().constData(),
        stats(materialized).toUtf8().constData()
    );

    caseReport(
        "animated mask", maxDelta <= 1,
        QString("frame 6 max %1 mean %2 of 255; frame ms view %3, cached %4, materialized %5")
            .arg(maxDelta).arg(meanDelta, 0, 'f', 3).arg(stats(animated)).arg(stats(cached))
            .arg(stats(materialized))
    );
}

// MX1: THE MASK CHAIN'S RULE ON THE CPU. MaskMipChain builds a mask's levels on the GPU (two area
// passes through an R16F scratch); a paged mask needs those levels from a source that is not the
// full texture, and the obvious source is the CPU field. This gate compares a CPU area-average
// pyramid with the GPU chain, level by level, on random odd-sized fields. Bit equality makes the
// CPU pyramid the shipped source; a small tail of 1-LSB differences would mean the pager must keep
// one tolerance for the tier seam (or a CPU two-pass half-float mirror), which is why the number is
// reported rather than assumed.
void suiteMaskPyramid() {
    suiteHeader("MX1 mask chain rule: CPU pyramid vs the GPU chain");
    const QSize sizes[] = {QSize(257, 193), QSize(900, 700), QSize(64, 64)};
    for (const QSize& size : sizes) {
        QImage field(size, QImage::Format_Grayscale8);
        uint32 seed = 0x12345678u;
        for (int y = 0; y < size.height(); ++y) {
            uchar* row = field.scanLine(y);
            for (int x = 0; x < size.width(); ++x) {
                seed = seed * 1664525u + 1013904223u;
                row[x] = (uchar)(seed >> 24);
            }
        }
        core::MaskKey::Data data;
        data.alloc(size, QRectF(0, 0, size.width(), size.height()), 0);
        data.field() = field;
        const gl::Texture* texture = data.textureId();
        expect(texture != nullptr, "MX1 the GPU chain must build");
        if (!texture)
            continue;

        MaskCpuPyramid cpu;
        cpu.build(data.field());

        int mismatches = 0, maxDelta = 0, total = 0;
        const int levels = texture->mipmapLevels();
        for (int l = 0; l < levels; ++l) {
            const QSize levelSize = MaskCpuPyramid::sizeAt(size, l);
            std::vector<uint8_t> gpu((size_t)levelSize.width() * levelSize.height(), 0);
            gl::Global::Functions& ggl = gl::Global::functions();
            ggl.glBindTexture(GL_TEXTURE_2D, texture->id());
            ggl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
            ggl.glGetTexImage(GL_TEXTURE_2D, l, GL_RED, GL_UNSIGNED_BYTE, gpu.data());
            ggl.glBindTexture(GL_TEXTURE_2D, 0);
            const std::vector<uint8_t>& cpuLevel = cpu.levels[(size_t)l];
            for (size_t i = 0; i < gpu.size(); ++i) {
                const int d = std::abs((int)gpu[i] - (int)cpuLevel[i]);
                if (d != 0) {
                    ++mismatches;
                    maxDelta = std::max(maxDelta, d);
                }
                ++total;
            }
        }
        // NOT bit-exact: the GPU's x pass stores the 1/255-normalized average in an R16F
        // scratch, and those values are not representable in half, so a CPU mirror that keeps
        // the half store still leaves a tail of 1-2 LSB differences (max 2 measured, <=4 % of
        // texels). A CPU-sourced paged mask therefore needs a 2-LSB tolerance at the tier seam
        // or an exact mirror of the shader's arithmetic; this gate records which it is.
        caseReport(
            QString("MX1 %1x%2").arg(size.width()).arg(size.height()),
            maxDelta <= 2 && mismatches * 10 < total,
            QString("%1 levels, %2 of %3 texels differ, max %4")
                .arg(levels).arg(mismatches).arg(total).arg(maxDelta)
        );
        expect(
            maxDelta <= 2,
            QString("MX1 %1x%2: the CPU rule must agree within 2 LSB (max %3)")
                .arg(size.width()).arg(size.height()).arg(maxDelta)
        );
    }

    // THE BUILD COST, on the one size that matters for the decision: a 4096^2 mask. The GPU
    // chain is MaskMipChain::create (level 0 upload + the two resample passes per level); the CPU
    // side is the two-pass mirror above. Debug build: use the RATIO, not the absolute numbers.
    {
        const QSize bigSize(4096, 4096);
        QImage field(bigSize, QImage::Format_Grayscale8);
        uint32 seed = 0x9e3779b9u;
        for (int y = 0; y < bigSize.height(); ++y) {
            uchar* row = field.scanLine(y);
            for (int x = 0; x < bigSize.width(); ++x) {
                seed = seed * 1664525u + 1013904223u;
                row[x] = (uchar)(seed >> 24);
            }
        }
        core::MaskKey::Data data;
        data.alloc(bigSize, QRectF(0, 0, bigSize.width(), bigSize.height()), 0);
        data.field() = field;
        QElapsedTimer timer;
        timer.start();
        const gl::Texture* gpuTex = data.textureId();
        const qint64 gpuMs = timer.elapsed();
        timer.restart();
        MaskCpuPyramid cpu;
        cpu.build(data.field());
        const qint64 cpuMs = timer.elapsed();
        caseReport(
            "MX1 4096^2 build cost",
            gpuTex != nullptr,
            QString("gpu chain %1 ms, cpu pyramid %2 ms (Debug build; ratio is the signal)")
                .arg(gpuMs).arg(cpuMs)
        );
    }
}

// MX2: THE CPU PYRAMID'S UPDATE. The paged mask's content comes from a CPU pyramid captured from
// the GPU chain; a stroke then rebuilds boxes on the CPU. This gate checks the two properties the
// pager relies on: the capture is exactly the chain, and a stroke's box update equals a FRESH
// whole build of the same field (the chain's own incremental path uses a cheaper source level,
// so the CPU rebuild is the more consistent reference) - within the 2 LSB MX1 measured for the
// CPU's half-float mirror.
void suiteMaskPyramidUpdate() {
    suiteHeader("MX2 mask pyramid: capture, a stroke box, a rebuild");
    const QSize sizes[] = {QSize(257, 193), QSize(900, 700)};
    for (const QSize& size : sizes) {
        QImage field(size, QImage::Format_Grayscale8);
        uint32 seed = 0xdeadbeefu;
        for (int y = 0; y < size.height(); ++y) {
            uchar* row = field.scanLine(y);
            for (int x = 0; x < size.width(); ++x) {
                seed = seed * 1664525u + 1013904223u;
                row[x] = (uchar)(seed >> 24);
            }
        }
        core::MaskKey::Data first;
        first.alloc(size, QRectF(0, 0, size.width(), size.height()), 0);
        first.field() = field;
        const gl::Texture* chain = first.textureId();
        expect(chain != nullptr, "MX2 the GPU chain must build");
        core::MaskPyramid pyr;
        expect(pyr.capture(*chain), "MX2 the pyramid must capture the chain");
        if (!chain || !pyr.isValid()) {
            continue;
        }
        expect(
            pyr.levelCount() == chain->mipmapLevels(),
            QString("MX2 the pyramid must have every chain level (%1 of %2)")
                .arg(pyr.levelCount())
                .arg(chain->mipmapLevels())
        );

        // the capture is the chain, byte for byte ABOVE LEVEL 0: level 0 is the field, not a
        // copy (the whole point of the MX4 memory gate)
        int captureMismatch = 0;
        expect(pyr.levelBytes(0).empty(), "MX2 level 0 must not be copied into the pyramid");
        for (int l = 1; l < pyr.levelCount(); ++l) {
            const QSize levelSize = pyr.levelSize(l);
            std::vector<uint8_t> gpu((size_t)levelSize.width() * levelSize.height(), 0);
            gl::Global::Functions& ggl = gl::Global::functions();
            ggl.glBindTexture(GL_TEXTURE_2D, chain->id());
            ggl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
            ggl.glGetTexImage(GL_TEXTURE_2D, l, GL_RED, GL_UNSIGNED_BYTE, gpu.data());
            ggl.glBindTexture(GL_TEXTURE_2D, 0);
            if (gpu != pyr.levelBytes(l))
                ++captureMismatch;
        }
        expect(captureMismatch == 0, QString("MX2 the capture must be exact (%1 levels differ)").arg(captureMismatch));

        // a stroke box, painted into the field
        const QRect box(
            size.width() / 5, size.height() / 4, size.width() / 4, size.height() / 5
        );
        for (int y = box.top(); y <= box.bottom(); ++y) {
            uchar* row = field.scanLine(y);
            for (int x = box.left(); x <= box.right(); ++x) {
                seed = seed * 1664525u + 1013904223u;
                row[x] = (uchar)(seed >> 24);
            }
        }
        const std::vector<uint8_t> level2Before = pyr.levelBytes(std::min(2, pyr.levelCount() - 1));
        pyr.updateBox(field, box);

        // level 0's box is the FIELD itself, read by the page fill, not a stored copy
        std::vector<uint8_t> basePage((size_t)gl::PagePool::kPitch * gl::PagePool::kPitch, 0);
        pyr.fillPage(0, 0, 0, field, basePage.data());
        // only page (0,0)'s CONTENT region: a box that crosses 256 lives in the next page,
        // which this one-page fill does not cover (the per-page fill reads the same field)
        const int boxRight = std::min(box.right(), gl::PagePool::kPageSize - 1);
        const int boxBottom = std::min(box.bottom(), gl::PagePool::kPageSize - 1);
        bool baseMatches = boxRight >= box.left() && boxBottom >= box.top();
        for (int y = box.top(); y <= boxBottom && baseMatches; ++y)
            for (int x = box.left(); x <= boxRight; ++x)
                if (basePage[(size_t)(y + gl::PagePool::kGutter) * gl::PagePool::kPitch + x
                             + gl::PagePool::kGutter]
                    != field.constScanLine(y)[x]) {
                    baseMatches = false;
                    break;
                }
        expect(baseMatches, "MX2 level 0's page fill must be the field");
        const int far = std::min(2, pyr.levelCount() - 1);
        const std::vector<uint8_t>& after = pyr.levelBytes(far);
        expect(
            after == level2Before || box.right() >= pyr.levelSize(far).width(),
            "MX2 a box update must not touch the level outside its covered box"
        );

        // the reference: a fresh whole build of the same field
        core::MaskKey::Data fresh;
        fresh.alloc(size, QRectF(0, 0, size.width(), size.height()), 0);
        fresh.field() = field;
        const gl::Texture* rebuilt = fresh.textureId();
        expect(rebuilt != nullptr, "MX2 the fresh chain must build");
        if (!rebuilt)
            continue;
        int mismatches = 0, maxDelta = 0, total = 0;
        for (int l = 1; l < pyr.levelCount(); ++l) {
            const QSize levelSize = pyr.levelSize(l);
            std::vector<uint8_t> gpu((size_t)levelSize.width() * levelSize.height(), 0);
            gl::Global::Functions& ggl = gl::Global::functions();
            ggl.glBindTexture(GL_TEXTURE_2D, rebuilt->id());
            ggl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
            ggl.glGetTexImage(GL_TEXTURE_2D, l, GL_RED, GL_UNSIGNED_BYTE, gpu.data());
            ggl.glBindTexture(GL_TEXTURE_2D, 0);
            const std::vector<uint8_t>& cpu = pyr.levelBytes(l);
            for (size_t i = 0; i < gpu.size() && i < cpu.size(); ++i) {
                const int d = std::abs((int)gpu[i] - (int)cpu[i]);
                if (d != 0) {
                    ++mismatches;
                    maxDelta = std::max(maxDelta, d);
                }
                ++total;
            }
        }
        expect(
            maxDelta <= 2,
            QString("MX2 a stroke's boxes must equal a rebuild within 2 LSB (max %1)").arg(maxDelta)
        );

        // the page fill: content is the field's (level 0) texels, the gutter is the neighbour
        std::vector<uint8_t> page((size_t)gl::PagePool::kPitch * gl::PagePool::kPitch, 0);
        pyr.fillPage(0, 0, 0, field, page.data());
        const auto fieldAt = [&](int aX, int aY) { return (int)field.constScanLine(aY)[aX]; };
        bool pageOk = true;
        for (int y = 0; y < 64 && pageOk; ++y)
            for (int x = 0; x < 64; ++x)
                if (page[((size_t)(y + 2)) * gl::PagePool::kPitch + x + 2] != fieldAt(x, y)) {
                    pageOk = false;
                    break;
                }
        expect(pageOk, "MX2 the page fill must place the level at the gutter's origin");
        // GL_CLAMP_TO_EDGE: outside the level the gutter repeats the nearest edge texel, so a
        // bilinear read at the matte's border matches the full chain's (see MaskPyramid.h)
        expect(
            page[0] == fieldAt(0, 0) && page[1] == fieldAt(0, 0),
            "MX2 the page's outside gutter must repeat the level's edge texel"
        );
        expect(
            page[(size_t)2 * gl::PagePool::kPitch + 258] == fieldAt(256, 0),
            "MX2 the first page's right gutter must be the level's next texel"
        );
        expect(
            page[(size_t)2 * gl::PagePool::kPitch + 259]
                == fieldAt(std::min(257, size.width() - 1), 0),
            "MX2 the gutter's second texel must be the level's next one or its edge"
        );

        caseReport(
            QString("MX2 %1x%2").arg(size.width()).arg(size.height()),
            maxDelta <= 2,
            QString("capture exact; rebuild max %1, %2 of %3 texels differ")
                .arg(maxDelta).arg(mismatches).arg(total)
        );
    }
}

// MX4: THE PYRAMID KEEPS NO LEVEL-0 COPY. Level 0 is the largest level - 16 MiB of a 4096^2
// mask - and it is exactly the field the mask's Data already owns; the image pyramid has always
// left level 0 to the resource, and the mask now does the same. This gate builds the production
// path for a 4096^2 painted mask (`MaskKey::Data::virtualTexture`, which builds a transient chain,
// captures it and drops it) and checks the pyramid holds ONLY the levels above the field, and
// that the resident floor is the coarse chain.
void suiteMaskPyramidMemory() {
    suiteHeader("MX4 a 4096^2 mask pyramid: no level-0 copy");
    const QSize size(4096, 4096);
    const qint64 baseBytes = (qint64)size.width() * size.height();

    QImage field(size, QImage::Format_Grayscale8);
    for (int y = 0; y < size.height(); ++y) {
        uchar* row = field.scanLine(y);
        for (int x = 0; x < size.width(); ++x)
            row[x] = uchar((x ^ y) >> 4);
    }
    core::MaskKey::Data data;
    data.alloc(size, QRectF(0, 0, size.width(), size.height()), 0);
    data.field() = field;

    core::VirtualTexture* vt = data.virtualTexture();
    expect(vt != nullptr, "MX4 a 4096^2 painted mask must page");
    if (!vt)
        return;

    // the chain the paged form replaced, level by level
    qint64 chainBytes = 0;
    QSize level = size;
    while (true) {
        chainBytes += (qint64)level.width() * level.height();
        if (level.width() == 1 && level.height() == 1)
            break;
        level = QSize(std::max(1, level.width() / 2), std::max(1, level.height() / 2));
    }
    qint64 coarseBytes = 0;
    level = QSize(512, 512);
    while (true) {
        coarseBytes += (qint64)level.width() * level.height();
        if (level.width() == 1 && level.height() == 1)
            break;
        level = QSize(std::max(1, level.width() / 2), std::max(1, level.height() / 2));
    }

    expect(
        vt->pyramidBytes() == chainBytes - baseBytes,
        QString("MX4 the pyramid must hold levels 1.. only (%1 B, chain %2 B, field %3 B)")
            .arg((qint64)vt->pyramidBytes()).arg(chainBytes).arg(baseBytes)
    );
    expect(
        vt->pyramidBytes() < baseBytes,
        QString("MX4 the pyramid must be smaller than the level-0 copy it avoids (%1 of %2 B)")
            .arg((qint64)vt->pyramidBytes()).arg(baseBytes)
    );
    expect(
        vt->coarseTexture().size() == QSize(512, 512),
        QString("MX4 the resident floor must be the coarse chain (%1x%2)")
            .arg(vt->coarseTexture().size().width()).arg(vt->coarseTexture().size().height())
    );

    caseReport(
        "4096^2 mask", gFails == 0,
        QString("field %1 MiB, pyramid %2 MiB (chain was %3 MiB), coarse %4 MiB")
            .arg(baseBytes / 1048576.0, 0, 'f', 2)
            .arg(vt->pyramidBytes() / 1048576.0, 0, 'f', 2)
            .arg(chainBytes / 1048576.0, 0, 'f', 2)
            .arg(coarseBytes / 1048576.0, 0, 'f', 2)
    );
}

// FM1: A FOLDER'S MATTE ON THE PAGED PATH. A layer mask is sampled inside the layer's own draw;
// a folder applies its matte as one FULLSCREEN multiply over its isolated composite, which used
// to read the full chain with the hardware's implicit-LOD texture() and had no demand source at
// all - a page stack could not reproduce that read, and nothing marked the pages it would need.
// This gate pages a composite folder's matte end to end and checks the three things that decide
// whether the pass is really paged:
//   - the pages are DEMANDED and RESIDENT (wanted > 0 proves the fullscreen demand marked them;
//     a resource built by an accessor alone would have wanted == 0);
//   - the paged frame equals the forced-full control (the same sampleMask on both paths, so a
//     difference is residency, not a filter);
//   - the matte is active in both, and the rubylith preview pages through the same stack.
// The field is 640^2 on a 640x640 canvas: level 0 is a 3x3 page grid, the coarse chain starts at
// 320^2, and the camera is 1:1 - so the demand's level is 0 and exactly its 9 pages are wanted.
void suiteFolderMask() {
    suiteHeader("FM1 folder matte: paged vs the full-resolution control");
    if (!gl::PagePool::enabled()) {
        std::printf("  [SKIP] AE_VIRTUAL_TEXTURE=0 is the control path\n");
        return;
    }

    const QSize canvas(640, 640);
    const uint8_t black[3] = {0, 0, 0};
    // WHITE opaque content: the frame's pixel IS the matte's value, so the matte's edge is read
    // in mask units instead of being scaled by whatever the artwork happened to hold
    Img src;
    src.w = 900;
    src.h = 700;
    src.d.assign((size_t)src.w * src.h * 4, 255);

    auto& fixtures = renderFixtures();
    const QString fixtureKey = "640x640-fm1";
    auto it = fixtures.find(fixtureKey);
    if (it == fixtures.end()) {
        std::unique_ptr<RenderFixture> fx(new RenderFixture());
        fx->init(canvas);
        it = fixtures.emplace(fixtureKey, std::move(fx)).first;
    }
    RenderFixture& fx = *it->second;

    core::CameraInfo camera;
    camera.reset(canvas, 1.0, canvas, QPoint(0, 0));

    // One tree per mode, each built from scratch: the mask's force-full flag rides the VALUE and
    // the expansion caches its copy, so a control toggled after the first blend would not reach
    // the renderer. The layer is 900x700 at 0.6, centred: it spans screen x 50..590, so the
    // field's left half and its right half both lie over content.
    const auto buildTree = [&](bool aForceFull, std::unique_ptr<core::ObjectTree>& aTree,
                               core::FolderNode** aRoot, core::MaskKey** aMask) {
        aTree.reset(new core::ObjectTree());
        auto* root = new core::FolderNode("root");
        aTree->grabTopNode(root);
        auto* res = makeResource(aForceFull ? "fm1_src_full" : "fm1_src_paged", src);
        addScaledLayer(
            *aTree, root, res, "layer", QVector2D(canvas.width() * 0.5f, canvas.height() * 0.5f),
            0.6f, img::BlendMode_Normal
        );
        auto* maskKey = new core::MaskKey();
        maskKey->data().setForceFullTexture(aForceFull);
        root->timeLine()->setBaseValue(core::TimeKeyType_Mask, maskKey);
        maskKey->data().alloc(QSize(640, 640), QRectF(0, 0, 640, 640), core::MaskKey::kNeutral);
        QImage& field = maskKey->data().field();
        for (int y = 0; y < field.height(); ++y)
            std::memset(field.scanLine(y), 0, 320);
        maskKey->data().markDirty(QRect(0, 0, 320, 640));
        expect(maskKey->data().isPainted(), "FM1 the painted field must report itself painted");
        *aRoot = root;
        *aMask = maskKey;
    };

    std::unique_ptr<core::ObjectTree> pagedTree, fullTree;
    core::FolderNode* pagedRoot = nullptr;
    core::FolderNode* fullRoot = nullptr;
    core::MaskKey* pagedMask = nullptr;
    core::MaskKey* fullMask = nullptr;
    buildTree(false, pagedTree, &pagedRoot, &pagedMask);
    buildTree(true, fullTree, &fullRoot, &fullMask);

    std::vector<uint8_t> paged;
    for (int i = 0; i < 8; ++i)
        paged = fx.render(*pagedTree, camera, 0, black);

    // THE PAGES MUST BE THE DEMAND PASS'S, not an accessor's: markFolderMaskDemand builds the
    // resource through virtualTexture(), so a wanted count here is evidence the render path ran
    // the fullscreen demand. 1:1 on a 640^2 field is level 0, a 3x3 grid.
    core::VirtualTexture* maskVT = pagedMask->data().virtualTexture();
    expect(maskVT != nullptr, "FM1 the folder matte must take the paged path");
    if (!maskVT)
        return;
    expect(maskVT->isMask(), "FM1 the folder matte's resource must be an R8 mask");
    expect(
        maskVT->size() == QSize(640, 640),
        QString("FM1 the matte's grid is the field's (%1x%2)")
            .arg(maskVT->size().width()).arg(maskVT->size().height())
    );
    expect(
        maskVT->coarseLevel() == 1 && maskVT->maxLevel() == 9,
        QString("FM1 the coarse chain starts at 320^2 (coarse %1 of %2)")
            .arg(maskVT->coarseLevel()).arg(maskVT->maxLevel())
    );
    const int wanted = maskVT->wantedPageCount();
    const int resident = maskVT->residentWantedCount();
    expect(
        wanted == 9,
        QString("FM1 a 1:1 640^2 matte demands exactly its level-0 3x3 grid (got %1)").arg(wanted)
    );
    expect(
        resident == wanted,
        QString("FM1 every wanted matte page must be resident (%1 of %2)").arg(resident).arg(wanted)
    );

    // THE CONTROL: the same field forced onto the full chain. It must really be full, and the
    // matte must not be paged beside it (virtualTexture declines while the flag is set).
    expect(fullMask->data().forceFullTexture(), "FM1 the control must hold the force-full flag");
    expect(
        fullMask->data().virtualTexture() == nullptr,
        "FM1 the forced-full matte must not take the paged path"
    );
    const gl::Texture* fullTexture = fullMask->data().textureId();
    expect(fullTexture != nullptr && fullTexture->id() != 0, "FM1 the control must build a full matte");
    // EIGHT FRAMES LIKE THE PAGED SIDE, even though the MATTE is full: the control tree's LAYER
    // IMAGE is still a pageable resource, and its first frame samples the coarse chain until the
    // demand readback lands. Comparing a settled paged frame against an unsettled control measured
    // 40 units at the layer's silhouette - the image's transition, not the matte's.
    std::vector<uint8_t> control;
    for (int i = 0; i < 8; ++i)
        control = fx.render(*fullTree, camera, 0, black);
    if (fullTexture) {
        expect(
            fullTexture->mipmapLevels() == 10,
            QString("FM1 the control's chain must be complete (10 levels, got %1)")
                .arg(fullTexture->mipmapLevels())
        );
    }

    int maxDelta = 0, maxX = 0, maxY = 0, maxC = 0;
    double meanDelta = 0.0;
    int n = 0;
    for (int y = 16; y < canvas.height() - 16; ++y) {
        for (int x = 16; x < canvas.width() - 16; ++x) {
            const size_t i = ((size_t)y * canvas.width() + x) * 4;
            for (int ch = 0; ch < 4; ++ch) {
                const int d = std::abs((int)paged[i + ch] - (int)control[i + ch]);
                if (d > maxDelta) {
                    maxDelta = d;
                    maxX = x;
                    maxY = y;
                    maxC = ch;
                }
                meanDelta += d;
                ++n;
            }
        }
    }
    meanDelta = n ? meanDelta / n : 0.0;
    expect(
        maxDelta <= 1,
        QString("FM1 folder matte paged vs full max delta %1 at (%2,%3) ch %4")
            .arg(maxDelta).arg(maxX).arg(maxY).arg(maxC)
    );
    expect(meanDelta < 0.5, QString("FM1 mean delta %1 below 0.5 of 255").arg(meanDelta, 0, 'f', 3));

    // THE MATTE IS DOING SOMETHING, in both frames: the field hides content x < 320 (screen
    // 0..320 at 1:1), so the layer's left part must be empty and its right part covered.
    const auto coveredIn = [&](const std::vector<uint8_t>& aFrame, int aX0, int aX1) {
        int count = 0;
        for (int y = 100; y < 540; ++y)
            for (int x = aX0; x < aX1; ++x)
                count += (aFrame[((size_t)y * canvas.width() + x) * 4] |
                          aFrame[((size_t)y * canvas.width() + x) * 4 + 1] |
                          aFrame[((size_t)y * canvas.width() + x) * 4 + 2]) > 0;
        return count;
    };
    {
        const int hidden = coveredIn(paged, 60, 300);
        const int visible = coveredIn(paged, 340, 580);
        expect(hidden == 0, QString("FM1 the paged frame must hide the matte's left half (%1 px)").arg(hidden));
        expect(visible > 70000, QString("FM1 the paged frame must show its right half (%1 px)").arg(visible));
        const int controlHidden = coveredIn(control, 60, 300);
        const int controlVisible = coveredIn(control, 340, 580);
        expect(
            controlHidden == 0,
            QString("FM1 the control must hide the matte's left half (%1 px)").arg(controlHidden)
        );
        expect(
            controlVisible > 70000,
            QString("FM1 the control must show its right half (%1 px)").arg(controlVisible)
        );
    }

    // THE RUBYLITH PREVIEW pages through the same stack (FolderNode's preview branch asks
    // virtualTexture() first). It marks the hidden half red and leaves the visible half alone,
    // and the paged overlay matches the full-mask one.
    const QColor tint(255, 0, 0, 128);
    std::vector<uint8_t> pagedPreview, fullPreview;
    for (int i = 0; i < 2; ++i)
        pagedPreview = fx.render(*pagedTree, camera, 0, black, false, false, pagedMask, pagedRoot, tint);
    for (int i = 0; i < 2; ++i)
        fullPreview = fx.render(*fullTree, camera, 0, black, false, false, fullMask, fullRoot, tint);

    const auto pixel = [&](const std::vector<uint8_t>& aFrame, int aX, int aY) {
        const size_t i = ((size_t)aY * canvas.width() + aX) * 4;
        return std::array<int, 4>{aFrame[i], aFrame[i + 1], aFrame[i + 2], aFrame[i + 3]};
    };
    const auto hiddenPx = pixel(pagedPreview, 150, 300);
    const auto visiblePx = pixel(pagedPreview, 450, 300);
    expect(
        hiddenPx[0] > hiddenPx[1] + 40,
        QString("FM1 the paged rubylith must tint the hidden half (r=%1 g=%2)")
            .arg(hiddenPx[0]).arg(hiddenPx[1])
    );
    expect(
        visiblePx[1] > 200,
        QString("FM1 the paged rubylith must leave the visible half white (g=%1)").arg(visiblePx[1])
    );

    int previewDelta = 0;
    for (int y = 16; y < canvas.height() - 16; ++y)
        for (int x = 16; x < canvas.width() - 16; ++x) {
            const size_t i = ((size_t)y * canvas.width() + x) * 4;
            for (int ch = 0; ch < 4; ++ch)
                previewDelta = std::max(
                    previewDelta, std::abs((int)pagedPreview[i + ch] - (int)fullPreview[i + ch])
                );
        }
    expect(
        previewDelta <= 3,
        QString("FM1 paged rubylith vs the full-mask overlay max delta %1 (tolerance 3)")
            .arg(previewDelta)
    );

    // NO HOLES WITH THE POOL EMPTIED: the coarse chain is the only fallback, and the matte must
    // still hide what it hides. The layer's image falls back too; both are coarse, neither is a
    // hole, so the visible half stays covered and the hidden half stays empty.
    {
        const qint64 savedBudget = gl::PagePool::instance().budgetBytes();
        gl::PagePool::instance().setBudgetBytes(1);
        gl::PagePool::instance().purge();
        std::vector<uint8_t> noPages;
        for (int i = 0; i < 3; ++i)
            noPages = fx.render(*pagedTree, camera, 0, black);
        gl::PagePool::instance().setBudgetBytes(savedBudget);

        const int hidden = coveredIn(noPages, 60, 300);
        const int visible = coveredIn(noPages, 340, 580);
        expect(hidden == 0, QString("FM1 with no page resident the matte still hides (%1 px)").arg(hidden));
        expect(
            visible > 70000,
            QString("FM1 with no page resident the coarse chain still shows the layer (%1 px)")
                .arg(visible)
        );
    }

    // THE DEMAND'S MAPPING, at a camera where only PART of the field is on screen. The camera's
    // view matrix is `screen = center + scale * (content - centerOffset())` with centerOffset =
    // imageSize/2, so scale 2 centred on (400, 480) shows content 120..440 x 80..400: level 0
    // needs pages 0..1 x 0..1, FOUR of the nine. A demand that marked the quad's whole screen rect
    // would say 9; a mirrored one would mark the complementary pages and leave one the pass
    // samples out of the pool. The paged frame must still match the control, which is what makes
    // a wrong page SET fail even when its count looks plausible.
    int zoomedDelta = 0;
    {
        core::CameraInfo zoomed = camera;
        zoomed.setScale(2.0f);
        zoomed.setCenter(QVector2D(400.0f, 480.0f));
        std::vector<uint8_t> zoomedPaged, zoomedControl;
        for (int i = 0; i < 8; ++i)
            zoomedPaged = fx.render(*pagedTree, zoomed, 0, black);
        const int zoomedWanted = maskVT->wantedPageCount();
        expect(
            zoomedWanted == 4,
            QString("FM1 a scale-2 view of content 120..440 x 80..400 needs 4 pages, not the grid's 9 (got %1)")
                .arg(zoomedWanted)
        );
        expect(
            maskVT->residentWantedCount() == zoomedWanted,
            QString("FM1 the zoomed matte's wanted pages must be resident (%1 of %2)")
                .arg(maskVT->residentWantedCount()).arg(zoomedWanted)
        );
        for (int i = 0; i < 8; ++i)
            zoomedControl = fx.render(*fullTree, zoomed, 0, black);

        for (int y = 2; y < canvas.height() - 2; ++y)
            for (int x = 2; x < canvas.width() - 2; ++x) {
                const size_t i = ((size_t)y * canvas.width() + x) * 4;
                for (int ch = 0; ch < 4; ++ch)
                    zoomedDelta = std::max(
                        zoomedDelta, std::abs((int)zoomedPaged[i + ch] - (int)zoomedControl[i + ch])
                    );
            }
        expect(
            zoomedDelta <= 1,
            QString("FM1 the zoomed folder matte must match the control (max %1)").arg(zoomedDelta)
        );
    }

    // THE EXPORT POLICY, from an EMPTY pool: a demand-only pass for the exported frame's camera,
    // a synchronous upload of what it marked, then ONE real pass. The folder matte's fullscreen
    // demand has to run in that pass too - FolderNode::render returns before renderComposite -
    // or the exported first frame samples the coarse chain and only the matte's edge is soft.
    int exportedDelta = 0;
    {
        // AND FORGET WHAT WAS WANTED FIRST. resolveDemand re-acquires every page still inside
        // kKeepFrames even without a fresh mark, so a purge alone leaves the previous frame's
        // wanted set ready to be re-uploaded on the next beginFrame - which made this block pass
        // with the demand-only folder mark deleted (measured: delta 0, 0 of 0 pages). Blank frames
        // age the timestamps out while the pages are still resident, THEN the purge releases them,
        // so the export really starts cold.
        for (int i = 0; i < core::VirtualTexture::kKeepFrames + 3; ++i) {
            core::VirtualTexture::beginFrame();
            core::VirtualTexture::endFrame();
        }
        expect(
            maskVT->wantedPageCount() == 0,
            QString("FM1 blank frames must empty the wanted set before the export (%1)")
                .arg(maskVT->wantedPageCount())
        );
        gl::PagePool::instance().purge();
        expect(
            gl::PagePool::instance().residentPages() == 0,
            QString("FM1 the export must start from an empty pool (%1 pages)")
                .arg(gl::PagePool::instance().residentPages())
        );

        const std::vector<uint8_t> exported = fx.renderExported(*pagedTree, camera, 0, black);
        const int exportedWanted = maskVT->wantedPageCount();
        const int exportedResident = maskVT->residentWantedCount();
        expect(
            exportedWanted == 9 && exportedResident == exportedWanted,
            QString("FM1 the export preparse must land every matte page (%1 of %2)")
                .arg(exportedResident).arg(exportedWanted)
        );

        int exportedMaxX = 0, exportedMaxY = 0;
        for (int y = 16; y < canvas.height() - 16; ++y)
            for (int x = 16; x < canvas.width() - 16; ++x) {
                const size_t i = ((size_t)y * canvas.width() + x) * 4;
                for (int ch = 0; ch < 4; ++ch) {
                    const int d = std::abs((int)exported[i + ch] - (int)control[i + ch]);
                    if (d > exportedDelta) {
                        exportedDelta = d;
                        exportedMaxX = x;
                        exportedMaxY = y;
                    }
                }
            }
        expect(
            exportedDelta <= 1,
            QString("FM1 the exported first frame must match the control (max %1 at %2,%3)")
                .arg(exportedDelta).arg(exportedMaxX).arg(exportedMaxY)
        );
    }

    // THE COST, in the queue's own units: the full chain this replaces against the pyramid the
    // pages come from and the always-resident coarse tier.
    qint64 chainBytes = 0;
    qint64 coarseBytes = 0;
    for (QSize level = QSize(640, 640);; level = QSize(std::max(1, level.width() / 2), std::max(1, level.height() / 2))) {
        chainBytes += (qint64)level.width() * level.height();
        if (level.width() == 1 && level.height() == 1)
            break;
    }
    for (QSize level = QSize(320, 320);; level = QSize(std::max(1, level.width() / 2), std::max(1, level.height() / 2))) {
        coarseBytes += (qint64)level.width() * level.height();
        if (level.width() == 1 && level.height() == 1)
            break;
    }
    expect(
        maskVT->pyramidBytes() == chainBytes - (qint64)640 * 640,
        QString("FM1 the pyramid holds levels 1.. only (%1 B of a %2 B chain)")
            .arg((qint64)maskVT->pyramidBytes()).arg(chainBytes)
    );

    // THE FRAME LINE'S NUMBER is this walk: a paged matte is its field plus the pyramid its page
    // fills read, a forced-full one is its field alone (its chain is a texture, counted by
    // tex_live). If the walk double-counted the shared holder or missed the pyramid, the two
    // numbers would be equal and the line would understate a paged document's RAM.
    expect(
        pagedTree->maskRamBytes() == (qint64)640 * 640 + maskVT->pyramidBytes(),
        QString("FM1 mask_bytes must count the field plus the pyramid (%1 B)")
            .arg(pagedTree->maskRamBytes())
    );
    expect(
        fullTree->maskRamBytes() == (qint64)640 * 640,
        QString("FM1 a forced-full matte counts its field only (%1 B)").arg(fullTree->maskRamBytes())
    );
    caseReport(
        "folder matte", gFails == 0,
        QString("max %1 mean %2 of 255; %3 pages resident; zoom(4 pages) max %4; rubylith max %5; "
                "export max %6; field %7 B, pyramid %8 B, coarse %9 B; chain was %10 B")
            .arg(maxDelta).arg(meanDelta, 0, 'f', 3).arg(resident).arg(zoomedDelta).arg(previewDelta)
            .arg(exportedDelta).arg((qint64)640 * 640).arg((qint64)maskVT->pyramidBytes())
            .arg(coarseBytes).arg(chainBytes)
    );

}

// DR1: THE DEMAND REPLAY. A still view repaints the same frame - a pointer moving over the
// canvas, a matte stroke, a panel animating - and asks for exactly the pages it asked for last
// time. The pass is skipped and the last page set is re-stamped instead. The trap the gate is
// built around: a replay that did not move the kKeepFrames window would keep the pixels right
// (the pages are still resident) while the wanted set silently decayed, and the first eviction
// would then blur a frame nobody had touched. So the gate holds the same frame for LONGER than
// that window and measures the wanted set, not the pixels.
//
// The camera case needs a resource with MORE THAN ONE pageable level: the demand walks every
// triangle, so a camera move alone never changes the page set of a layer whose only paged level
// is level 0. A 1200x1000 source has coarseLevel 2, so zooming out moves the foot against -1.35
// from level 0 (20 pages) to level 1 (6), and a replay that ignored the view matrix would leave
// the six level-1 pages unmarked - the frame would fall back to the coarse chain.
void suiteDemandReplay() {
    suiteHeader("DR1 the demand replay: a repeated frame walks no triangles");
    if (!gl::PagePool::enabled()) {
        std::printf("  [SKIP] AE_VIRTUAL_TEXTURE=0 is the control path\n");
        return;
    }
    if (qgetenv("AE_VT_DEMAND") == "0") {
        std::printf("  [SKIP] AE_VT_DEMAND=0 removes the demand pass itself\n");
        return;
    }

    const QSize canvas(320, 320);
    const uint8_t black[3] = {0, 0, 0};
    const QSize source(1200, 1000);
    const Img src = makeNoise(source.width(), source.height());

    RenderFixture fx;
    fx.init(canvas);

    core::CameraInfo camera;
    camera.reset(canvas, 1.0, canvas, QPoint(0, 0));

    // ONE RESOURCE, two trees: the oracle's difference from the replayed tree is then the paged
    // read's, never the source data's (see the slot-rounding note in the gate's report).
    img::ResourceNode* srcRes = makeResource("dr1_src", src);
    const auto buildTree = [&](std::unique_ptr<core::ObjectTree>& aTree) -> core::ImageKey* {
        aTree.reset(new core::ObjectTree());
        auto* root = new core::FolderNode("root");
        aTree->grabTopNode(root);
        core::LayerNode* layer = addScaledLayer(
            *aTree, root, srcRes, "layer",
            QVector2D(canvas.width() * 0.5f, canvas.height() * 0.5f), 0.3f, img::BlendMode_Normal
        );
        auto* imageKey =
            dynamic_cast<core::ImageKey*>(layer->timeLine()->baseValue(core::TimeKeyType_Image));
        if (imageKey)
            imageKey->setForceFullTexture(false);
        return imageKey;
    };

    std::unique_ptr<core::ObjectTree> tree;
    core::ImageKey* imageKey = buildTree(tree);
    expect(imageKey != nullptr, "DR1 the layer must have an image key");
    if (!imageKey)
        return;

    // FRAME 5, not 0: a still view is a frame that is already settled, and the skinning pass's own
    // cache (which is what keeps the mesh revision still) is the other half of the signature.
    // Three renders: the first MARKS, the second consumes that mark (the map is a frame behind),
    // the third is the first true replay.
    std::vector<uint8_t> frame;
    bool firstMarked = false;
    for (int i = 0; i < 3; ++i) {
        frame = fx.render(*tree, camera, 5, black);
        if (i == 0)
            firstMarked =
                core::VirtualTexture::demandMarks() >= 1 && core::VirtualTexture::demandReplays() == 0;
    }
    expect(firstMarked, "DR1 the first frame of a resource must MARK, not replay");

    const core::VirtualTexture* vt = imageKey->cache().virtualTexture();
    expect(vt != nullptr, "DR1 the 1200x1000 image must take the paged path");
    if (!vt)
        return;

    const int fitGrid = vt->pageGridX(0) * vt->pageGridY(0);
    const int landed = vt->wantedPageCount();
    expect(
        landed == fitGrid,
        QString("DR1 the fit view must demand its whole level-0 grid (%1 of %2 pages)")
            .arg(landed)
            .arg(fitGrid)
    );
    expect(
        vt->residentWantedCount() == landed,
        QString("DR1 every demanded page must land (%1 of %2)")
            .arg(vt->residentWantedCount())
            .arg(landed)
    );
    const std::vector<uint8_t> landedFrame = frame;

    // SIX MORE REPAINTS, twice the kKeepFrames window: a replay that did not re-stamp the pages
    // would leave the wanted set empty by the fourth of them.
    bool markedWhileStill = false;
    for (int i = 0; i < 6; ++i) {
        frame = fx.render(*tree, camera, 5, black);
        markedWhileStill = markedWhileStill || core::VirtualTexture::demandMarks() > 0;
    }
    expect(!markedWhileStill, "DR1 a repeated frame must not walk the triangles again");
    expect(
        core::VirtualTexture::demandReplays() >= 1, "DR1 a repeated frame must replay its pages"
    );
    const int stillWanted = vt->wantedPageCount();
    expect(
        stillWanted == landed,
        QString("DR1 the replay must keep every page wanted past kKeepFrames (%1 of %2)")
            .arg(stillWanted)
            .arg(landed)
    );
    expect(
        vt->residentWantedCount() == stillWanted,
        QString("DR1 the replayed pages must still be resident (%1 of %2)")
            .arg(vt->residentWantedCount())
            .arg(stillWanted)
    );
    expect(
        frame == landedFrame,
        "DR1 the replayed frame must be bit-identical to the one the mark produced"
    );

    // THE CAMERA MOVES, and this time the demanded LEVEL does too: at half scale the footprint is
    // 0.15 texels per pixel, lod = log2(1/0.15) - 1.35 = 1.39, so the fragment shader reads level 1
    // and the demand must name level 1's six pages instead of level 0's twenty.
    core::CameraInfo zoomed = camera;
    zoomed.setScale(0.5f);
    std::vector<uint8_t> moved = fx.render(*tree, zoomed, 5, black);
    expect(
        core::VirtualTexture::demandMarks() >= 1 && core::VirtualTexture::demandReplays() == 0,
        "DR1 a camera move must re-mark, not replay"
    );
    for (int i = 0; i < 5; ++i)
        moved = fx.render(*tree, zoomed, 5, black);
    const int levelOneGrid = vt->pageGridX(1) * vt->pageGridY(1);
    const int movedWanted = vt->wantedPageCount();
    // A RANGE, not the whole grid. Before the affine demand path (AE_VT_AFFINE_DEMAND) the walk
    // marked every page of the layer at the level the triangles computed, whatever the camera saw,
    // so the count was exactly level 1's grid. The quad marks the VISIBLE page extent - the same
    // union, clipped by the screen - so a view that shows part of the layer wants part of the
    // grid. What must still hold is the level: the whole level-0 grid is `fitGrid` pages, so a set
    // bigger than level 1's grid means the zoom did not take, and the page equality with the
    // oracle below is what pins the rest.
    expect(
        movedWanted >= 1 && movedWanted <= levelOneGrid,
        QString("DR1 the level-1 view must demand level 1's pages, not level 0's (%1, of at most "
                "%2; level 0 was %3)")
            .arg(movedWanted)
            .arg(levelOneGrid)
            .arg(fitGrid)
    );
    expect(
        vt->residentWantedCount() == movedWanted,
        QString("DR1 the level-1 view's pages must all be resident (%1 of %2)")
            .arg(vt->residentWantedCount())
            .arg(movedWanted)
    );

    // A CAMERA PAN RE-MARKS, and this pins a decision that was measured, not a limitation: the
    // page RECTS do not move with a translation, but the fragment's LEVEL comes from the
    // hardware's derivatives of the same screen positions, whose rounding follows the camera. A
    // mark from another camera therefore does not predict what the fragment samples - on nene the
    // fit view's mark reused across a pan measured 263 pixels reading the coarse chain, and at a
    // 280 px pan it happened to be SHARPER than a fresh mark (max 1 against the control, where the
    // fresh one measured max 223). Correct by luck is not correct. See the doc's open list.
    core::CameraInfo panned = zoomed;
    panned.setCenter(QVector2D(canvas.width() * 0.5f + 120.0f, canvas.height() * 0.5f));
    std::vector<uint8_t> pannedFrame = fx.render(*tree, panned, 5, black);
    expect(
        core::VirtualTexture::demandMarks() >= 1 && core::VirtualTexture::demandReplays() == 0,
        "DR1 a camera pan must re-mark, not replay (see the open list)"
    );
    for (int i = 0; i < 4; ++i)
        pannedFrame = fx.render(*tree, panned, 5, black);
    // THE SET MAY CHANGE WITH THE VIEW, and under the affine path it legitimately does: the quad
    // marks what the camera sees, so a pan slides the marked extent along the layer. What must
    // hold is that everything the panned frame samples is resident (the frame comparison below is
    // the real check) and that the fresh tree demands exactly the same set - a replay history must
    // not change the page set, only re-stamp it.
    const int pannedWanted = vt->wantedPageCount();
    expect(
        pannedWanted >= 1 && pannedWanted <= levelOneGrid,
        QString("DR1 the panned view must demand level 1's pages (%1, of at most %2)")
            .arg(pannedWanted)
            .arg(levelOneGrid)
    );
    expect(
        vt->residentWantedCount() == pannedWanted,
        QString("DR1 the panned frame's pages must all be resident (%1 of %2)")
            .arg(vt->residentWantedCount())
            .arg(pannedWanted)
    );

    // THE ORACLE: a fresh tree that never replayed anything, at the panned camera. Its page set
    // and its pixels must equal tree A's - the control differs in exactly the history.
    std::unique_ptr<core::ObjectTree> oracleTree;
    core::ImageKey* oracleKey = buildTree(oracleTree);
    expect(oracleKey != nullptr, "DR1 the oracle layer must have an image key");
    if (!oracleKey)
        return;
    std::vector<uint8_t> oracleFrame;
    for (int i = 0; i < 8; ++i)
        oracleFrame = fx.render(*oracleTree, panned, 5, black);
    const core::VirtualTexture* oracleVT = oracleKey->cache().virtualTexture();
    expect(oracleVT != nullptr, "DR1 the oracle image must take the paged path");
    if (!oracleVT)
        return;
    expect(
        oracleVT->wantedPageCount() == pannedWanted,
        QString("DR1 a replay history must not change the page set (%1 against %2)")
            .arg(oracleVT->wantedPageCount())
            .arg(pannedWanted)
    );
    int maxDelta = 0;
    int differing = 0;
    int maxX = 0, maxY = 0;
    for (int y = 0; y < canvas.height(); ++y) {
        for (int x = 0; x < canvas.width(); ++x) {
            const size_t i = ((size_t)y * canvas.width() + x) * 4;
            for (int ch = 0; ch < 4; ++ch) {
                const int d = std::abs((int)pannedFrame[i + ch] - (int)oracleFrame[i + ch]);
                if (d > 0)
                    ++differing;
                if (d > maxDelta) {
                    maxDelta = d;
                    maxX = x;
                    maxY = y;
                }
            }
        }
    }
    // ±1 BETWEEN TWO INDEPENDENTLY BUILT PAGED RESOURCES is the atlas read's sub-texel rounding:
    // each tree's pages land in different pool slots, and a bilinear tap's rounding follows the
    // slot's position. Measured with TWO oracle trees that never replayed (133 samples of 409,600
    // differ by 1), so the tolerance is that rounding and nothing else. The COUNT is asserted so a
    // systematic difference cannot hide under the tolerance: a replay that ignored the camera
    // leaves the six level-1 pages unmarked, the frame reads the coarse chain instead, and noise
    // turns that into tens of units over a large area (measured with the mutation, see
    // texture-residency.md).
    expect(
        maxDelta <= 1 && differing * 1000 <= canvas.width() * canvas.height() * 4,
        QString("DR1 the replayed frame must match a tree that never replayed (max %1 at "
                "(%2,%3), %4 of %5 samples differ; see the slot-rounding note)")
            .arg(maxDelta)
            .arg(maxX)
            .arg(maxY)
            .arg(differing)
            .arg(canvas.width() * canvas.height() * 4)
    );
    expect(
        imageKey->cache().texture().id() == 0,
        "DR1 the replayed tree must have no full texture (it stayed paged)"
    );

    caseReport(
        "demand replay", gFails == 0,
        QString("%1 level-0 pages wanted by the fit view, %2 level-1 after the zoom, %3 after a "
                "120 px pan (which re-marks; the affine path lets the set follow the view); 6 "
                "repaints marked nothing; oracle max %4")
            .arg(landed)
            .arg(movedWanted)
            .arg(pannedWanted)
            .arg(maxDelta)
    );
}

// A DEFORMED LAYER IS THE ONE CASE THE AFFINE QUAD CANNOT ANSWER, and until this gate there was
// no fixture for it: every paged case in this file builds a layer whose uv is one affine function
// of the screen, which is exactly the condition the fast demand path tests. The `AE_VT_DEMAND_SPLIT`
// measurement on nene could not see it either - none of that project's layers were deformed.
//
// The fixture warps the layer with an FFD key that TRANSLATES every grid vertex by three level-0
// pages. That is the smallest deformation with the property this gate needs: it is a per-triangle
// affine (so the fragment's uv is exact and a correct demand is exact too), it is invisible to the
// node's SRT - which is what the affine quad reads - and it moves the sampled texel by more than a
// page, so a quad's page extent cannot contain the pages the warp pulls in.
//
// TWO FAILURES THIS PINS, with their mutations recorded in texture-residency.md:
//   - the PREDICATE: an FFD-deformed layer must take the walk. It did not before 2026-11-07 - the
//     test looked for a TimeKeyType_FFD parent, and `ffdMeshParent()` names the ImageKey/MeshKey
//     the FFD keys deform, so the test was false by construction. With the quad in use the paged
//     frame sampled the rest grid's three-pages-away content - the coarse chain, since those pages
//     were never demanded;
//   - the CULL: the walk must mark what the camera sees, not the whole layer. Without the
//     screen-bounds cull the walk wanted the layer's entire level-0 grid (8x8 = 64 pages) from a
//     view that can sample a 2x2 corner of it, which at a magnified camera is the pool flood that
//     made zoomed views unpaged before the affine path (see the demand section).
void suiteDeformedDemand() {
    suiteHeader("DF1 an FFD-warped layer: demand follows the warp, and only what is on screen");
    if (!gl::PagePool::enabled()) {
        std::printf("  [SKIP] AE_VIRTUAL_TEXTURE=0 is the control path\n");
        return;
    }
    if (qgetenv("AE_VT_DEMAND") == "0") {
        std::printf("  [SKIP] AE_VT_DEMAND=0 removes the demand pass itself\n");
        return;
    }

    const QSize canvas(320, 320);
    const QSize source(2048, 2048);
    const Img src = makeNoise(source.width(), source.height());
    const uint8_t black[3] = {0, 0, 0};

    RenderFixture fx;
    fx.init(canvas);

    // 4x: the view shows 80x80 content pixels of a 2048^2 layer, so the demand is LEVEL 0 (the
    // magnified branch) and one page is 256 content px. Three pages of displacement is then far
    // outside the quad's own extent; at a fit view the whole layer would be visible and the error
    // would hide inside a page set that is a superset by luck.
    core::CameraInfo camera;
    camera.reset(canvas, 1.0, canvas, QPoint(0, 0));
    camera.setScale(4.0f);

    img::ResourceNode* res = makeResource("df1_src", src);

    struct Tree {
        std::unique_ptr<core::ObjectTree> tree;
        core::ImageKey* imageKey = nullptr;
        core::LayerNode* layer = nullptr;
    };
    // The same layer twice: the warp is proven against the second tree, which has no FFD key. A
    // parity check against its OWN full-texture control would pass whatever the warp did - the
    // demand only decides which pages are fetched, not whether the mesh is deformed - so the rest
    // tree is what makes "the warp is real" an assertion instead of an assumption.
    const auto buildTree = [&]() {
        Tree t;
        t.tree.reset(new core::ObjectTree());
        auto* root = new core::FolderNode("root");
        t.tree->grabTopNode(root);
        t.layer = addScaledLayer(
            *t.tree, root, res, "layer",
            QVector2D(canvas.width() * 0.5f, canvas.height() * 0.5f), 1.0f, img::BlendMode_Normal
        );
        t.imageKey =
            dynamic_cast<core::ImageKey*>(t.layer->timeLine()->baseValue(core::TimeKeyType_Image));
        if (t.imageKey)
            t.imageKey->setForceFullTexture(false);
        return t;
    };

    Tree warped = buildTree();
    Tree rest = buildTree();
    expect(
        warped.imageKey != nullptr && rest.imageKey != nullptr,
        "DF1 both layers must have an image key"
    );
    if (!warped.imageKey || !rest.imageKey)
        return;

    // the warp: every grid vertex moved by -768 content px (three level-0 pages)
    const core::LayerMesh& grid = warped.imageKey->data().gridMesh();
    const int vtxCount = grid.vertexCount();
    expect(vtxCount > 0, "DF1 the image key must have built its grid mesh");
    if (vtxCount <= 0)
        return;
    const float kShift = -768.0f;
    std::vector<gl::Vector3> displaced(grid.positions(), grid.positions() + vtxCount);
    for (gl::Vector3& p : displaced)
        p.x += kShift;
    core::FFDKey* ffd = new core::FFDKey();
    ffd->data().allocAndWrite(displaced.data(), vtxCount);
    ffd->data().easing(0) = util::Easing::defaultParam();
    warped.imageKey->children().pushBack(ffd);
    expect(
        warped.layer->timeLine()->adoptKey(core::TimeKeyType_FFD, 0, ffd),
        "DF1 the FFD key must install into the timeline's FFD map"
    );

    // A SECOND WARP AT FRAME 10, in the opposite direction. The frame-10 phase needs a
    // deformation change with NOTHING else moving - same camera, same SRT - which is what tells a
    // demand that follows the GEOMETRY (the image's walk, keyed on the skinning revision) from one
    // that follows the MAPPING (the matte's quad).
    {
        std::vector<gl::Vector3> back(grid.positions(), grid.positions() + vtxCount);
        for (gl::Vector3& p : back)
            p.x += 640.0f;
        core::FFDKey* ffd10 = new core::FFDKey();
        ffd10->data().allocAndWrite(back.data(), vtxCount);
        ffd10->data().easing(0) = util::Easing::defaultParam();
        warped.imageKey->children().pushBack(ffd10);
        expect(
            warped.layer->timeLine()->adoptKey(core::TimeKeyType_FFD, 10, ffd10),
            "DF1 the frame-10 FFD key must install"
        );
    }

    // THE MATTE, painted on its left half: the mask's uv is the node's SRT, so the canvas's left
    // half is hidden (content x < 1024) whatever the warp does. It is paged (2048^2 is pageable)
    // and its demand must be the screen's affine extent, not a walk of the warped triangles.
    auto* maskKey = new core::MaskKey();
    warped.layer->timeLine()->setBaseValue(core::TimeKeyType_Mask, maskKey);
    maskKey->data().alloc(
        source, QRectF(0, 0, source.width(), source.height()), core::MaskKey::kNeutral
    );
    {
        QImage& field = maskKey->data().field();
        for (int y = 0; y < field.height(); ++y)
            std::memset(field.scanLine(y), 0, field.width() / 2);
        maskKey->data().markDirty(QRect(0, 0, field.width() / 2, field.height()));
    }
    expect(maskKey->data().isPainted(), "DF1 the matte must be painted");

    // the rest tree first: it fills its own pages, and its frame is the warp's proof
    std::vector<uint8_t> restFrame;
    for (int i = 0; i < 4; ++i)
        restFrame = fx.render(*rest.tree, camera, 0, black);

    std::vector<uint8_t> paged;
    for (int i = 0; i < 8; ++i)
        paged = fx.render(*warped.tree, camera, 0, black);

    // THE WARP IS REAL: compare a centre band the matte leaves VISIBLE (content x >= 1024, i.e.
    // device x >= 160) - the matte's own edge must not stand in for the warp's proof.
    int centreDelta = 0;
    for (int y = canvas.height() / 2 - 32; y < canvas.height() / 2 + 32; ++y) {
        for (int x = canvas.width() / 2 + 8; x < canvas.width() / 2 + 72; ++x) {
            const size_t i = ((size_t)y * canvas.width() + x) * 4;
            for (int ch = 0; ch < 4; ++ch)
                centreDelta = std::max(centreDelta, std::abs((int)paged[i + ch] - (int)restFrame[i + ch]));
        }
    }
    expect(
        centreDelta > 64,
        QString("DF1 the FFD key must move what is drawn (centre max %1 from the rest tree)").arg(centreDelta)
    );

    const core::VirtualTexture* vt = warped.imageKey->cache().virtualTexture();
    expect(vt != nullptr, "DF1 the 2048^2 image must take the paged path");
    if (!vt)
        return;

    const int wanted = vt->wantedPageCount();
    const int gridPages = vt->pageGridX(0) * vt->pageGridY(0);
    const bool stayedPaged = warped.imageKey->cache().texture().id() == 0;
    expect(
        vt->residentWantedCount() == wanted,
        QString("DF1 every wanted page must be resident (%1 of %2)")
            .arg(vt->residentWantedCount())
            .arg(wanted)
    );
    expect(
        gridPages == 64,
        QString("DF1 a 2048^2 level 0 must be an 8x8 page grid (got %1)").arg(gridPages)
    );
    // THE CULL. A view that shows an 80x80 corner can sample only its 2x2 level-0 pages (the
    // rectangle plus the triangles that straddle its edges); the whole grid is 64. The bound has
    // slack for the layer's border triangles, not for an off-screen triangle - there are none:
    // the layer covers the whole canvas.
    expect(
        wanted >= 1 && wanted <= 8,
        QString("DF1 the magnified view must demand the pages it can sample, not the layer "
                "(%1 of at most 8; the whole level-0 grid is %2)")
            .arg(wanted)
            .arg(gridPages)
    );
    expect(
        wanted < gridPages,
        QString("DF1 the demand must not be the whole layer (%1 of 64)").arg(wanted)
    );

    // THE MATTE'S DEMAND IS THE SCREEN'S AFFINE EXTENT: at this camera the screen maps to 80x80
    // content px around (1024, 1024), so its level-0 pages are a 2x2 set that crosses the 1024
    // boundary - the whole 8x8 grid is 64, and a walk of the warped mesh would mark the warped
    // triangle boxes instead (the same 8x8 in the worst case, but changing with the warp).
    core::VirtualTexture* maskVT = maskKey->data().virtualTexture();
    expect(maskVT != nullptr, "DF1 the matte must take the paged path");
    int maskWanted = 0;
    if (maskVT) {
        expect(maskVT->isMask(), "DF1 the matte's resource must be an R8 mask");
        maskWanted = maskVT->wantedPageCount();
        expect(
            maskWanted >= 1 && maskWanted <= 8,
            QString("DF1 the matte's demand is the screen's affine extent (%1 pages, at most 8)")
                .arg(maskWanted)
        );
        expect(
            maskVT->residentWantedCount() == maskWanted,
            QString("DF1 every wanted matte page must be resident (%1 of %2)")
                .arg(maskVT->residentWantedCount())
                .arg(maskWanted)
        );
    }

    const auto frameDelta = [&](const std::vector<uint8_t>& aPaged,
                                const std::vector<uint8_t>& aControl, int* aMaxX, int* aMaxY,
                                int* aMaxC, double* aMean) {
        int maxDelta = 0;
        double sum = 0.0;
        int count = 0;
        for (int y = 0; y < canvas.height(); ++y) {
            for (int x = 0; x < canvas.width(); ++x) {
                const size_t i = ((size_t)y * canvas.width() + x) * 4;
                for (int ch = 0; ch < 4; ++ch) {
                    const int d = std::abs((int)aPaged[i + ch] - (int)aControl[i + ch]);
                    if (d > maxDelta) {
                        maxDelta = d;
                        *aMaxX = x;
                        *aMaxY = y;
                        *aMaxC = ch;
                    }
                    sum += d;
                    ++count;
                }
            }
        }
        *aMean = count ? sum / count : 0.0;
        return maxDelta;
    };

    // THE CONTROL AT FRAME 0: the same warped tree from full textures. Forcing the IMAGE full also
    // drops the draw's MASK_VIRTUAL_TEXTURE variation (it is only ever set WITH VIRTUAL_TEXTURE), so
    // the control's matte is its full chain too: this comparison covers BOTH resources.
    warped.imageKey->setForceFullTexture(true);
    const std::vector<uint8_t> control = fx.render(*warped.tree, camera, 0, black);
    warped.imageKey->setForceFullTexture(false);

    int maxX = 0, maxY = 0, maxC = 0, max10X = 0, max10Y = 0, max10C = 0;
    double meanDelta = 0.0, mean10 = 0.0;
    int maxDelta = frameDelta(paged, control, &maxX, &maxY, &maxC, &meanDelta);
    expect(
        maxDelta <= 1,
        QString("DF1 paged warped layer vs the full-resolution control: max %1 at (%2,%3) ch %4")
            .arg(maxDelta)
            .arg(maxX)
            .arg(maxY)
            .arg(maxC)
    );
    expect(
        meanDelta < 0.5,
        QString("DF1 mean delta %1 below 0.5 of 255").arg(meanDelta, 0, 'f', 3)
    );
    expect(
        stayedPaged,
        "DF1 the warped tree must have no full texture (it stayed paged)"
    );

    // THE SECOND WARP, SAME CAMERA. Only the geometry moves: the image's walk must re-mark (its
    // signature carries the skinning revision) and the matte's quad must REPLAY - a matte's pages
    // are a function of the screen->content affine, which a deformation cannot change. This is the
    // assertion that tells the quad from a mesh walk: a walk keyed on the revision would mark both.
    for (int i = 0; i < 8; ++i)
        fx.render(*warped.tree, camera, 0, black);   // rebuild the paged image after the control
    std::vector<uint8_t> frame10 = fx.render(*warped.tree, camera, 10, black);
    expect(
        core::VirtualTexture::demandMarks() == 1 && core::VirtualTexture::demandReplays() == 1,
        QString("DF1 a deformation-only change must re-mark the image and replay the matte "
                "(marks %1, replays %2)")
            .arg(core::VirtualTexture::demandMarks())
            .arg(core::VirtualTexture::demandReplays())
    );
    for (int i = 0; i < 8; ++i)
        frame10 = fx.render(*warped.tree, camera, 10, black);   // land the second warp's pages

    const int maskWanted10 = maskVT ? maskVT->wantedPageCount() : 0;
    if (maskVT) {
        expect(
            maskWanted10 == maskWanted,
            QString("DF1 the matte's page set must not move with the warp (%1 against %2)")
                .arg(maskWanted10)
                .arg(maskWanted)
        );
        expect(
            maskVT->residentWantedCount() == maskWanted10,
            QString("DF1 the second warp's matte pages must all be resident (%1 of %2)")
                .arg(maskVT->residentWantedCount())
                .arg(maskWanted10)
        );
    }

    warped.imageKey->setForceFullTexture(true);
    const std::vector<uint8_t> control10 = fx.render(*warped.tree, camera, 10, black);
    warped.imageKey->setForceFullTexture(false);
    const int max10 = frameDelta(frame10, control10, &max10X, &max10Y, &max10C, &mean10);
    expect(
        max10 <= 1,
        QString("DF1 the second warp, image + matte, vs the full-resolution control: max %1 at "
                "(%2,%3) ch %4")
            .arg(max10)
            .arg(max10X)
            .arg(max10Y)
            .arg(max10C)
    );
    expect(
        mean10 < 0.5,
        QString("DF1 the second warp's mean delta %1 below 0.5 of 255").arg(mean10, 0, 'f', 3)
    );

    caseReport(
        "FFD-warped demand", gFails == 0,
        QString("%1 pages wanted of the layer's %2-page level-0 grid, all resident; %3 matte "
                "pages; the warp moves the frame by %4 (centre); paged vs control max %5, mean "
                "%6 of 255; second warp max %7, mean %8; a deformation-only change marked the "
                "image and replayed the matte")
            .arg(wanted)
            .arg(gridPages)
            .arg(maskWanted)
            .arg(centreDelta)
            .arg(maxDelta)
            .arg(meanDelta, 0, 'f', 3)
            .arg(max10)
            .arg(mean10, 0, 'f', 3)
    );
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

    suitePremultiply();
    suiteChain();
    suiteChainDeterminism();
    suiteImageKey();
    suiteChainExactness();
    suiteRender();
    suitePagePool();
    suiteVirtualTexture();
    suiteClipper();
    suiteMaskPaged();
    suiteMaskPagedUpdate();
    suiteAnimatedMask();
    suiteMaskPyramid();
    suiteMaskPyramidUpdate();
    suiteMaskPyramidMemory();
    suiteFolderMask();
    suiteDemandReplay();
    suiteDeformedDemand();

    std::printf("\n%d checks, %d failures\n", gChecks, gFails);
    std::fflush(stdout);

    // tear GL objects down while the context is still current: the fixtures, and the widget's own
    // vertex array object (its destructor runs with the window, after the context is gone)
    clearRenderFixtures();
    glWidget->mDefaultVao.reset();
    glWidget->doneCurrent();
    return gFails ? 1 : 0;
}
