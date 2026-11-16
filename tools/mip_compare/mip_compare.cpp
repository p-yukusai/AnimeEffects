// Mipmap POLICY comparison harness.
//
// WHAT THIS MEASURES
//
// gl::Texture::mipSourceLevel (src/gl/Texture.h) is the rule this tool studies and the tree ships:
// a level is built from a FINER level with the area weights the grids imply, and that finer level is
// chosen by kMipChainSpan - levels 1..3 come from level 0 (exact for any size, odd sides included),
// level >= 4 comes from level - 3, an 8:1 step whose source texels are themselves averages. That is
// the reach-back rule. Building from level 0 is the expensive choice (a base-sourced level re-reads
// the whole content), so the question this tool answers empirically is: how much worse is a chain
// whose source is one, two or three levels up, and how does a driver-style bilinear chain compare?
//
// The answer is a per-level, per-policy error against the analytic reference. The reference is the
// DEFINITION of a mip level: an exact area average of the PREMULTIPLIED level 0 over that texel's
// footprint - the level an infinitely expensive base-sourced build would produce. The rule's
// criterion is "looks exact", and the shipped span 3 meets it: worst 8 of 255 (mean 0.34) against 65
// (mean 5.16 on its worst sample) for a one-level chain, over the 45-sample corpus this tool ran.
// Levels are 1:1 with the content (level 0 texel = content texel), so the footprint of level L texel
// j is [j * base / size(L), (j+1) * base / size(L)) of the content; that is why the weight function
// has to handle arbitrary, non-integer ratios, and it does (axisWeights, below, is the verified CPU
// reference from tools/verify_mipmap/mip_probe.cpp).
//
// The pixel data is PREMULTIPLIED throughout: averaging straight colour mixes the RGB stored under
// alpha 0 into the silhouette (the mipmap dark halo), so a comparison run on straight data would
// measure the halo instead of the policy.
//
// POLICIES (all built on premultiplied data, all reaching level L's own size)
//   exact     level L = area average of level 0                     (the box; the reference)
//   chain1    level L = area average of level L-1                   (a one-level reach-back)
//   chain2    level L = area average of level L-2, or of the deepest level that exists
//   chain3    level L = area average of level L-3, likewise          (the shipped reach-back span)
//   bilinear  level L = level L-1 through a 2-tap separable bilinear, i.e. a texture-unit
//             linear filter: dst texel centre maps to (i+0.5)*src/dst - 0.5 in the source
//             (what a driver's own generateMipmap chain does)
//
// OUTPUT
//
// For a sample <in> and an output directory <out>: metrics.json (per level, per policy: maxAbs,
// meanAbs, p999Abs, countOver2, countOver8 against the exact level, plus the level size), one
// level-<L>-<policy>.png per level and policy (un-premultiplied for viewing, at the level's own
// size), and one SAMPLE line on stdout.
//
// Run: ./build/tests/Debug/mip_compare <input-image> <outdir>
// Formats: PNG/JPEG/WebP/BMP through QImage, PSD through img::PSDReader (the flattened merged
// image, not the layer stack). A PSD whose composite cannot be decoded is reported as SKIP and
// exits 0, so a batch run can carry on past a file this tool does not read.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <map>

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QString>

#include "XC.h"
#include "img/PSDFormat.h"
#include "img/PSDReader.h"
#include "img/PSDUtil.h"

//-------------------------------------------------------------------------------------------------
// The libs (img, util) reference these globals; the app defines them in src/gui/Main.cpp. A
// research harness gains nothing from an assert handler, so both stay null: XC_ASSERT is then only
// its Q_ASSERT in a debug build and XC_FATAL_ERROR is a no-op.
XCAssertHandler* gXCAssertHandler = nullptr;
XCErrorHandler* gXCErrorHandler = nullptr;

namespace {

//-------------------------------------------------------------------------------------------------
// images: RGBA8, 4 bytes per texel, row major. The base arrives from a loader STRAIGHT; every
// level built here is PREMULTIPLIED, which is the form the comparison and the metrics use.

struct Img {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> d;

    uint8_t* at(int x, int y) { return d.data() + ((size_t)y * w + x) * 4; }
    const uint8_t* at(int x, int y) const { return d.data() + ((size_t)y * w + x) * 4; }
    size_t texels() const { return (size_t)w * h; }
    bool valid() const { return 0 < w && 0 < h && d.size() == texels() * 4; }
};

// The premultiply the production upload does (img::Util::makeTextureUpload): round to nearest, so
// an opaque-adjacent texel does not drift darker. Level 0 has to be the same premultiplied content
// the GPU starts from, otherwise every policy is compared against the wrong reference.
Img toPremultiplied(const Img& aStraight) {
    Img out = aStraight;
    for (size_t i = 0; i + 3 < out.d.size(); i += 4) {
        uint8_t* p = out.d.data() + i;
        const uint32_t a = p[3];
        if (a == 255)
            continue;
        p[0] = (uint8_t)((p[0] * a + 127) / 255);
        p[1] = (uint8_t)((p[1] * a + 127) / 255);
        p[2] = (uint8_t)((p[2] * a + 127) / 255);
    }
    return out;
}

//-------------------------------------------------------------------------------------------------
// the exact area average, copied from tools/verify_mipmap/mip_probe.cpp (the verified reference):
// destination texel j covers [j/dst, (j+1)/dst] of the same span the source covers, so it is the
// average of the source texels it overlaps, with fractional weights at the ends. This is the
// DEFINITION of a mip level, and it is valid for ANY src->dst ratio.
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

std::vector<uint8_t> areaLevel(const std::vector<uint8_t>& aSrc, int aSrcW, int aSrcH, int aDstW, int aDstH) {
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

Img resampleArea(const Img& aSrc, int aDstW, int aDstH) {
    Img out;
    out.w = aDstW;
    out.h = aDstH;
    out.d = areaLevel(aSrc.d, aSrc.w, aSrc.h, aDstW, aDstH);
    return out;
}

//-------------------------------------------------------------------------------------------------
// THE INTEGER BOX: every destination texel owns a whole number of source texels - the ranges
// [floor(j*S/C), floor((j+1)*S/C)) partition the source, so no texel is ever SPLIT and no fractional
// weight exists anywhere. That is the difference that matters: the area version above has to hand a
// partially-covered source texel over whole (its value is only an average), which is a second
// filtering pass - the blur. An integer box never does: it filters once per level, over whole texels.
//-------------------------------------------------------------------------------------------------
// THE CUBIC STEP: the parent is NOT treated as piecewise constant - it is reconstructed with a
// Catmull-Rom cubic and that reconstruction is integrated over the destination footprint. The box
// chain multiplies the base spectrum by the parent's rect AND its own, which IS the extra low-pass
// (the blur); a cubic reconstruction restores what the parent's rect took away, for 16 taps per
// output texel instead of 9 at a 2:1 step.
double cubicKernel(double aX) {
    const double x = std::fabs(aX);
    if (x < 1.0)
        return 1.5 * x * x * x - 2.5 * x * x + 1.0;
    if (x < 2.0)
        return -0.5 * x * x * x + 2.5 * x * x - 4.0 * x + 2.0;
    return 0.0;
}

std::vector<std::map<int, double>> cubicAxisTaps(int aSrc, int aDst, int aSub = 4) {
    std::vector<std::map<int, double>> acc((size_t)aDst);
    for (int j = 0; j < aDst; ++j) {
        const double lo = (double)j * aSrc / aDst;
        const double hi = (double)(j + 1) * aSrc / aDst;
        for (int s = 0; s < aSub; ++s) {
            const double x = lo + (s + 0.5) * (hi - lo) / aSub - 0.5; // source texel centres
            const int i0 = (int)std::floor(x) - 1;
            double sum = 0.0;
            for (int t = 0; t < 4; ++t)
                sum += cubicKernel(x - (i0 + t));
            for (int t = 0; t < 4; ++t) {
                const double w = cubicKernel(x - (i0 + t)) / (sum != 0.0 ? sum : 1.0);
                acc[(size_t)j][std::min(aSrc - 1, std::max(0, i0 + t))] += w / aSub;
            }
        }
    }
    return acc;
}

Img resampleCubic(const Img& aSrc, int aDstW, int aDstH) {
    const auto wx = cubicAxisTaps(aSrc.w, aDstW);
    const auto wy = cubicAxisTaps(aSrc.h, aDstH);
    Img out;
    out.w = aDstW;
    out.h = aDstH;
    out.d.assign((size_t)aDstW * aDstH * 4, 0);
    std::vector<double> rows((size_t)aDstW * aSrc.h * 4, 0.0);
    for (int y = 0; y < aSrc.h; ++y)
        for (int j = 0; j < aDstW; ++j) {
            double acc[4] = {0.0, 0.0, 0.0, 0.0};
            for (const auto& t : wx[(size_t)j]) {
                const uint8_t* p = aSrc.d.data() + ((size_t)y * aSrc.w + t.first) * 4;
                for (int c = 0; c < 4; ++c) acc[c] += p[c] * t.second;
            }
            for (int c = 0; c < 4; ++c)
                rows[((size_t)y * aDstW + j) * 4 + c] = acc[c];
        }
    for (int i = 0; i < aDstH; ++i)
        for (int x = 0; x < aDstW; ++x) {
            double acc[4] = {0.0, 0.0, 0.0, 0.0};
            for (const auto& t : wy[(size_t)i]) {
                const double* p = rows.data() + ((size_t)t.first * aDstW + x) * 4;
                for (int c = 0; c < 4; ++c) acc[c] += p[c] * t.second;
            }
            uint8_t* q = out.d.data() + ((size_t)i * aDstW + x) * 4;
            for (int c = 0; c < 4; ++c)
                q[c] = (uint8_t)std::min(255.0, std::max(0.0, std::floor(acc[c] + 0.5)));
        }
    return out;
}

Img resampleIntegerBox(const Img& aSrc, int aDstW, int aDstH) {
    Img out;
    out.w = aDstW;
    out.h = aDstH;
    out.d.assign((size_t)aDstW * aDstH * 4, 0);
    for (int y = 0; y < aDstH; ++y) {
        const int y0 = (int)((long long)y * aSrc.h / aDstH);
        const int y1 = (int)((long long)(y + 1) * aSrc.h / aDstH);
        for (int x = 0; x < aDstW; ++x) {
            const int x0 = (int)((long long)x * aSrc.w / aDstW);
            const int x1 = (int)((long long)(x + 1) * aSrc.w / aDstW);
            double acc[4] = {0.0, 0.0, 0.0, 0.0};
            int n = 0;
            for (int sy = y0; sy < y1; ++sy)
                for (int sx = x0; sx < x1; ++sx) {
                    const uint8_t* p = aSrc.d.data() + ((size_t)sy * aSrc.w + sx) * 4;
                    for (int c = 0; c < 4; ++c) acc[c] += p[c];
                    ++n;
                }
            uint8_t* q = out.d.data() + ((size_t)y * aDstW + x) * 4;
            for (int c = 0; c < 4; ++c)
                q[c] = n ? (uint8_t)std::min(255.0, std::floor(acc[c] / n + 0.5)) : 0;
        }
    }
    return out;
}

//-------------------------------------------------------------------------------------------------
// the driver-style chain: two taps per axis, around the destination texel's centre mapped back into
// the source ((i+0.5)*src/dst - 0.5, clamped). GL_LINEAR_MIPMAP samples one level the same way, so
// this is the "what does the hardware's own chain look like" arm of the comparison.

struct Tap {
    int i0 = 0;
    int i1 = 0;
    double w0 = 1.0;
    double w1 = 0.0;
};

std::vector<Tap> linearTaps(int aSrc, int aDst) {
    std::vector<Tap> taps((size_t)aDst);
    for (int j = 0; j < aDst; ++j) {
        const double centre = (j + 0.5) * (double)aSrc / (double)aDst - 0.5;
        const double clamped = std::min((double)(aSrc - 1), std::max(0.0, centre));
        const int i0 = (int)std::floor(clamped);
        const double frac = clamped - (double)i0;
        Tap& t = taps[(size_t)j];
        t.i0 = i0;
        t.i1 = std::min(aSrc - 1, i0 + 1);
        t.w0 = 1.0 - frac;
        t.w1 = frac;
    }
    return taps;
}

Img resampleBilinear(const Img& aSrc, int aDstW, int aDstH) {
    const std::vector<Tap> tx = linearTaps(aSrc.w, aDstW);
    const std::vector<Tap> ty = linearTaps(aSrc.h, aDstH);

    std::vector<double> rows((size_t)aDstW * aSrc.h * 4, 0.0);
    for (int y = 0; y < aSrc.h; ++y) {
        const uint8_t* line = aSrc.d.data() + (size_t)y * aSrc.w * 4;
        for (int j = 0; j < aDstW; ++j) {
            const Tap& t = tx[(size_t)j];
            const uint8_t* p0 = line + (size_t)t.i0 * 4;
            const uint8_t* p1 = line + (size_t)t.i1 * 4;
            for (int c = 0; c < 4; ++c)
                rows[((size_t)y * aDstW + j) * 4 + c] = p0[c] * t.w0 + p1[c] * t.w1;
        }
    }

    Img out;
    out.w = aDstW;
    out.h = aDstH;
    out.d.assign((size_t)aDstW * aDstH * 4, 0);
    for (int i = 0; i < aDstH; ++i) {
        const Tap& t = ty[(size_t)i];
        const double* r0 = rows.data() + (size_t)t.i0 * aDstW * 4;
        const double* r1 = rows.data() + (size_t)t.i1 * aDstW * 4;
        uint8_t* dst = out.d.data() + (size_t)i * aDstW * 4;
        for (size_t x = 0; x < (size_t)aDstW * 4; ++x) {
            const double v = r0[x] * t.w0 + r1[x] * t.w1;
            dst[x] = (uint8_t)std::min(255.0, std::max(0.0, std::floor(v + 0.5)));
        }
    }
    return out;
}

//-------------------------------------------------------------------------------------------------
// A chain policy holds only the levels it can still reach: chain_k at level L reads level L-k, so
// k+1 consecutive levels are enough. That keeps a 4096^2 base comparable along four policies
// without holding every level of every chain (they would be ~5x the base's own memory, for levels
// nothing can read any more).

struct Chain {
    int span = 1; // how many levels down this policy reads from
    int firstLevel = 0; // level index of hist[0]
    std::vector<Img> hist; // levels firstLevel .. firstLevel + hist.size() - 1

    void reset(const Img& aBase) {
        firstLevel = 0;
        hist.assign(1, aBase);
    }

    // The level this policy filters `aLevel` from. min(span, level) is the documented fallback: a
    // chain that does not have `span` levels yet uses the deepest level it has, which is level 0 at
    // the top - never a base-sourced level, which would defeat the comparison.
    const Img& source(int aLevel) const {
        const int off = std::min(span, aLevel);
        const int idx = aLevel - off - firstLevel;
        if (idx < 0 || idx >= (int)hist.size()) {
            std::fprintf(stderr, "internal error: policy level %d outside its window\n", aLevel);
            std::abort();
        }
        return hist[(size_t)idx];
    }

    void push(const Img& aImg, int aLevel) {
        hist.push_back(aImg);
        while ((int)hist.size() > span + 1)
            hist.erase(hist.begin());
        firstLevel = aLevel - (int)hist.size() + 1;
    }
};

//-------------------------------------------------------------------------------------------------
// metrics: every comparison is PREMULTIPLIED RGBA against the exact level of the same size.

struct Metrics {
    int maxAbs = 0; // largest per-channel absolute difference, 0..255
    double meanAbs = 0.0; // mean over all channel samples
    int p999Abs = 0; // 99.9th percentile of the per-texel max-channel delta
    int countOver2 = 0; // texels whose max channel delta exceeds 2
    int countOver8 = 0; // ... and 8
};

Metrics compare(const Img& aRef, const Img& aTest) {
    Metrics m;
    if (aRef.w != aTest.w || aRef.h != aTest.h) {
        std::fprintf(stderr, "internal error: comparing %dx%d against %dx%d\n", aRef.w, aRef.h, aTest.w, aTest.h);
        std::abort();
    }
    const size_t n = aRef.texels();
    if (n == 0)
        return m;

    std::vector<uint8_t> perTexel(n, 0);
    size_t sum = 0;
    for (size_t t = 0; t < n; ++t) {
        int local = 0;
        for (int c = 0; c < 4; ++c) {
            const int d = std::abs((int)aTest.d[t * 4 + c] - (int)aRef.d[t * 4 + c]);
            local = std::max(local, d);
            sum += (size_t)d;
        }
        perTexel[t] = (uint8_t)local;
        m.maxAbs = std::max(m.maxAbs, local);
        if (local > 2)
            ++m.countOver2;
        if (local > 8)
            ++m.countOver8;
    }
    m.meanAbs = (double)sum / (double)(n * 4);

    // nearest-rank percentile: the smallest delta that covers 99.9% of the texels
    const size_t rank = (size_t)std::ceil(0.999 * (double)n);
    const size_t idx = rank ? std::min(rank - 1, n - 1) : 0;
    std::vector<uint8_t> sorted = perTexel;
    std::nth_element(sorted.begin(), sorted.begin() + (ptrdiff_t)idx, sorted.end());
    m.p999Abs = sorted[idx];
    return m;
}

//-------------------------------------------------------------------------------------------------
// output

// Level PNGs are un-premultiplied so they look like the artwork in a viewer; a texel with no
// coverage has no colour to recover and is written as transparent black.
bool writeLevelPng(const QString& aPath, const Img& aPremultiplied) {
    QImage out(aPremultiplied.w, aPremultiplied.h, QImage::Format_RGBA8888);
    if (out.isNull())
        return false;
    if (out.bytesPerLine() != (qsizetype)aPremultiplied.w * 4) {
        std::fprintf(stderr, "error: unexpected QImage stride %lld\n", (long long)out.bytesPerLine());
        return false;
    }
    for (int y = 0; y < aPremultiplied.h; ++y) {
        uint8_t* dst = out.scanLine(y);
        const uint8_t* src = aPremultiplied.at(0, y);
        for (int x = 0; x < aPremultiplied.w; ++x) {
            const uint8_t* p = src + (size_t)x * 4;
            uint8_t* q = dst + (size_t)x * 4;
            const unsigned a = p[3];
            for (int c = 0; c < 3; ++c) {
                if (a == 0) {
                    q[c] = 0;
                } else {
                    const unsigned v = ((unsigned)p[c] * 255u + a / 2u) / a;
                    q[c] = (uint8_t)std::min(255u, v);
                }
            }
            q[3] = (uint8_t)a;
        }
    }
    return out.save(aPath, "PNG");
}

std::string jsonEscape(const QString& aText) {
    const QByteArray utf8 = aText.toUtf8();
    std::string out;
    out.reserve((size_t)utf8.size() + 8);
    for (const char ch : utf8) {
        const unsigned char c = (unsigned char)ch;
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
                out += buf;
            } else {
                out += (char)c;
            }
            break;
        }
    }
    return out;
}

std::string policyJson(const Metrics& aMetrics) {
    // Locale-independent: QCoreApplication calls setlocale(LC_ALL, ""), so printf's %.6f would use
    // the user's decimal comma and produce JSON that no parser accepts.
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"maxAbs\": " << aMetrics.maxAbs << ", \"meanAbs\": " << std::fixed << std::setprecision(6)
        << aMetrics.meanAbs << ", \"p999Abs\": " << aMetrics.p999Abs << ", \"countOver2\": " << aMetrics.countOver2
        << ", \"countOver8\": " << aMetrics.countOver8 << "}";
    return out.str();
}

//-------------------------------------------------------------------------------------------------
// loading

enum class LoadResult {
    Ok,
    Skip, // the sample has no reachable flattened picture: a batch run should carry on
    Error, // the file or the arguments are broken
};

// PNG/JPEG/WebP/BMP, and anything else Qt has a reader for. convertToFormat(RGBA8888) gives
// STRAIGHT alpha, which toPremultiplied() turns into the form every level is built in.
LoadResult loadThroughQImage(const QString& aPath, Img& aOut, QString& aMessage) {
    QImage image(aPath);
    if (image.isNull()) {
        aMessage = "cannot decode as an image";
        return LoadResult::Error;
    }
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    if (rgba.isNull()) {
        aMessage = "cannot convert to RGBA8";
        return LoadResult::Error;
    }

    aOut.w = rgba.width();
    aOut.h = rgba.height();
    aOut.d.assign((size_t)aOut.w * aOut.h * 4, 0);
    for (int y = 0; y < aOut.h; ++y) {
        std::memcpy(aOut.d.data() + (size_t)y * aOut.w * 4, rgba.constScanLine(y), (size_t)aOut.w * 4);
    }
    return aOut.valid() ? LoadResult::Ok : LoadResult::Error;
}

// The flattened document: PSDFormat::imageData() is the merged image Photoshop stores at the end of
// the file (channel planes, Raw or RLE, already decoded by PSDReader into Channel::data), so no
// layer walk and no GL is needed. A mode or depth with no straight RGBA8 composite here is reported
// as SKIP rather than guessed at.
LoadResult loadPsd(const QString& aPath, Img& aOut, QString& aSkipReason, QString& aErrorMessage) {
    std::ifstream stream(std::string(aPath.toLocal8Bit().constData()), std::ios::binary);
    if (!stream) {
        aErrorMessage = "cannot open the file";
        return LoadResult::Error;
    }

    img::PSDReader reader(stream);
    if (reader.resultCode() != img::PSDReader::ResultCode_Success) {
        aSkipReason = QString::fromStdString(reader.resultMessage());
        return LoadResult::Skip;
    }
    if (!reader.format()) {
        aSkipReason = "no PSD format";
        return LoadResult::Skip;
    }

    const img::PSDFormat& format = *reader.format();
    const img::PSDFormat::Header& header = format.header();
    if (header.mode != img::PSDFormat::ColorMode_RGB) {
        aSkipReason =
            QString("mode %1 (%2)")
                .arg((int)header.mode)
                .arg(QString::fromUtf8(img::PSDFormat::colorModeName((img::PSDFormat::ColorMode)header.mode)));
        return LoadResult::Skip;
    }
    if (header.depth != 8) {
        aSkipReason = QString("depth %1").arg((int)header.depth);
        return LoadResult::Skip;
    }
    const int w = (int)header.width;
    const int h = (int)header.height;
    if (w <= 0 || h <= 0) {
        aSkipReason = QString("empty document %1x%2").arg(w).arg(h);
        return LoadResult::Skip;
    }

    // Mirror the importer's rule for the merged alpha (ctrl::ImageFileLoader::createMergedResource):
    // the alpha plane is only real when the file declares transparency, otherwise Photoshop writes a
    // dummy zero there that would blank the whole composite.
    const img::PSDFormat::ImageData& imageData = format.imageData();
    img::PSDFormat::ChannelList channels;
    for (const img::PSDFormat::ChannelPtr& channel : imageData.channels) {
        if (!channel)
            continue;
        if (!imageData.hasTransparency && channel->id < 0)
            continue;
        if (!channel->data || channel->dataLength == 0)
            continue;
        auto copy = new img::PSDFormat::Channel();
        copy->id = channel->id;
        copy->compressionId = channel->compressionId;
        copy->dataLength = channel->dataLength;
        copy->data.reset(new uint8[channel->dataLength]);
        std::memcpy(copy->data.get(), channel->data.get(), channel->dataLength);
        channels.push_back(img::PSDFormat::ChannelPtr(copy));
    }

    XCMemBlock mem = img::PSDUtil::makeInterleavedImage(header, channels, img::PSDUtil::ColorFormat_RGBA8, w, h);
    if (!mem.data) {
        // makeInterleavedImage's small failure codes, in its own order
        static const char* const kReasons[] = {
            "unknown",
            "colour mode is not RGB",
            "bit depth is not 8",
            "no RGB planes",
            "RLE decode failed",
            "unsupported compression",
            "alpha decode failed",
            "unsupported alpha compression",
        };
        const size_t code = mem.size;
        aSkipReason = QString("flattened composite unavailable: %1")
                          .arg(code < sizeof(kReasons) / sizeof(kReasons[0]) ? kReasons[code] : "unknown");
        return LoadResult::Skip;
    }

    const size_t bytes = (size_t)w * h * 4;
    if (mem.size != bytes) {
        delete[] mem.data;
        aSkipReason = "flattened composite has an unexpected size";
        return LoadResult::Skip;
    }
    aOut.w = w;
    aOut.h = h;
    aOut.d.assign(mem.data, mem.data + bytes);
    delete[] mem.data;
    return aOut.valid() ? LoadResult::Ok : LoadResult::Error;
}

LoadResult loadSample(const QString& aPath, Img& aOut, QString& aSkipReason, QString& aErrorMessage) {
    if (QFileInfo(aPath).suffix().compare(QLatin1String("psd"), Qt::CaseInsensitive) == 0)
        return loadPsd(aPath, aOut, aSkipReason, aErrorMessage);
    return loadThroughQImage(aPath, aOut, aErrorMessage);
}

//-------------------------------------------------------------------------------------------------
// the comparison itself

constexpr int kMaxLevels = 12;
constexpr int kMaxBaseAxis = 4096;

std::string levelJson(
    int aLevel,
    int aW,
    int aH,
    const Metrics& aChain1,
    const Metrics& aChain2,
    const Metrics& aChain3,
    const Metrics& aBilinear,
    const Metrics& aHalf,
    const Metrics& aCubic
) {
    char head[160];
    std::snprintf(
        head,
        sizeof(head),
        "    {\n      \"level\": %d,\n      \"size\": [%d, %d],\n      \"policies\": {\n",
        aLevel,
        aW,
        aH
    );
    std::string out(head);
    out += "        \"chain1\": " + policyJson(aChain1) + ",\n";
    out += "        \"chain2\": " + policyJson(aChain2) + ",\n";
    out += "        \"chain3\": " + policyJson(aChain3) + ",\n";
    out += "        \"bilinear\": " + policyJson(aBilinear) + ",\n";
    out += "        \"half\": " + policyJson(aHalf) + ",\n";
    out += "        \"cubic\": " + policyJson(aCubic) + "\n";
    out += "      }\n    }";
    return out;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    if (argc != 3) {
        std::fprintf(stderr, "usage: mip_compare <input-image> <outdir>\n");
        return 2;
    }
    const QString inputPath = QString::fromLocal8Bit(argv[1]);
    const QString outDir = QString::fromLocal8Bit(argv[2]);

    Img loaded;
    QString skipReason;
    QString errorMessage;
    switch (loadSample(inputPath, loaded, skipReason, errorMessage)) {
    case LoadResult::Skip:
        std::printf("SKIP %s: %s\n", inputPath.toUtf8().constData(), skipReason.toUtf8().constData());
        return 0;
    case LoadResult::Error:
        std::fprintf(stderr, "error: %s: %s\n", inputPath.toUtf8().constData(), errorMessage.toUtf8().constData());
        return 2;
    case LoadResult::Ok:
        break;
    }

    if (!QDir().mkpath(outDir)) {
        std::fprintf(stderr, "error: cannot create %s\n", outDir.toUtf8().constData());
        return 2;
    }

    // Every level is built on premultiplied data, starting from the premultiplied content.
    Img base = toPremultiplied(loaded);
    std::vector<uint8_t>().swap(loaded.d);

    // Cap: the harness must not try to hold a chain of a poster-sized image. The reduction is the
    // same exact area average as every level, so it changes the sample's scale, not its character.
    bool baseReduced = false;
    if (base.w > kMaxBaseAxis || base.h > kMaxBaseAxis) {
        const double s = (double)kMaxBaseAxis / (double)std::max(base.w, base.h);
        const int nw = std::max(1, (int)std::floor((double)base.w * s));
        const int nh = std::max(1, (int)std::floor((double)base.h * s));
        std::fprintf(stderr, "note: base %dx%d exceeds %d, reducing to %dx%d\n", base.w, base.h, kMaxBaseAxis, nw, nh);
        base = resampleArea(base, nw, nh);
        baseReduced = true;
    }

    const int maxDim = std::max(base.w, base.h);
    const int lmax = maxDim >= 2 ? std::min(kMaxLevels, (int)std::floor(std::log2((double)maxDim))) : 0;

    Chain chain1;
    Chain chain2;
    Chain chain3;
    Chain bilinear;
    Chain half;
    Chain cubic;
    chain1.span = 1;
    chain2.span = 2;
    chain3.span = 3;
    bilinear.span = 1;
    half.span = 1;
    cubic.span = 1;
    chain1.reset(base);
    chain2.reset(base);
    chain3.reset(base);
    bilinear.reset(base);
    half.reset(base);
    cubic.reset(base);

    // worst maxAbs over all levels, per policy, for the SAMPLE line
    int worstChain1 = 0;
    int worstChain2 = 0;
    int worstChain3 = 0;
    int worstBilinear = 0;
    int worstHalf = 0;
    int worstCubic = 0;

    std::vector<std::string> levelObjects;
    int writtenLevels = 0;

    for (int level = 1; level <= lmax; ++level) {
        const int lw = std::max(1, base.w >> level);
        const int lh = std::max(1, base.h >> level);
        if (lw < 1 || lh < 1)
            break; // a level that has no texel is not a level

        // The reference: the exact area average of the premultiplied content, which is what a level
        // built from level 0 produces.
        const Img exact = resampleArea(base, lw, lh);

        const Img c1 = resampleArea(chain1.source(level), lw, lh);
        const Img c2 = resampleArea(chain2.source(level), lw, lh);
        const Img c3 = resampleArea(chain3.source(level), lw, lh);
        const Img bl = resampleBilinear(bilinear.source(level), lw, lh);
        const Img hf = resampleIntegerBox(half.source(level), lw, lh);
        const Img cb = resampleCubic(cubic.source(level), lw, lh);

        chain1.push(c1, level);
        chain2.push(c2, level);
        chain3.push(c3, level);
        bilinear.push(bl, level);
        half.push(hf, level);
        cubic.push(cb, level);

        const Metrics m1 = compare(exact, c1);
        const Metrics m2 = compare(exact, c2);
        const Metrics m3 = compare(exact, c3);
        const Metrics mb = compare(exact, bl);
        const Metrics mh = compare(exact, hf);
        const Metrics mc = compare(exact, cb);

        worstChain1 = std::max(worstChain1, m1.maxAbs);
        worstChain2 = std::max(worstChain2, m2.maxAbs);
        worstChain3 = std::max(worstChain3, m3.maxAbs);
        worstBilinear = std::max(worstBilinear, mb.maxAbs);
        worstHalf = std::max(worstHalf, mh.maxAbs);
        worstCubic = std::max(worstCubic, mc.maxAbs);

        // level 0 as well, so a render test has the unscaled content to supersample
        if (level == 1 && !writeLevelPng(QDir(outDir).filePath(QString("level-0-base.png")), base)) {
            std::fprintf(stderr, "error: cannot write level 0\n");
            return 2;
        }

        const char* const kPolicies[] = {"exact", "chain1", "chain2", "chain3", "bilinear", "half", "cubic"};
        const Img* const kImages[] = {&exact, &c1, &c2, &c3, &bl, &hf, &cb};
        for (int p = 0; p < 7; ++p) {
            const QString path =
                QDir(outDir).filePath(QString("level-%1-%2.png").arg(level).arg(QString::fromLatin1(kPolicies[p])));
            if (!writeLevelPng(path, *kImages[p])) {
                std::fprintf(stderr, "error: cannot write %s\n", path.toUtf8().constData());
                return 2;
            }
        }

        levelObjects.push_back(levelJson(level, lw, lh, m1, m2, m3, mb, mh, mc));
        ++writtenLevels;
    }

    std::string json;
    json += "{\n";
    json += "  \"input\": \"" + jsonEscape(inputPath) + "\",\n";
    {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "  \"base\": [%d, %d],\n", base.w, base.h);
        json += buf;
    }
    json += std::string("  \"baseReduced\": ") + (baseReduced ? "true" : "false") + ",\n";
    json += "  \"levels\": [";
    if (!levelObjects.empty()) {
        json += "\n";
        for (size_t i = 0; i < levelObjects.size(); ++i)
            json += levelObjects[i] + (i + 1 < levelObjects.size() ? ",\n" : "\n");
        json += "  ";
    }
    json += "]\n}\n";

    const QString metricsPath = QDir(outDir).filePath("metrics.json");
    {
        std::ofstream out(std::string(metricsPath.toLocal8Bit().constData()), std::ios::binary | std::ios::trunc);
        if (!out) {
            std::fprintf(stderr, "error: cannot write %s\n", metricsPath.toUtf8().constData());
            return 2;
        }
        out.write(json.data(), (std::streamsize)json.size());
        out.flush();
        if (!out) {
            std::fprintf(stderr, "error: cannot write %s\n", metricsPath.toUtf8().constData());
            return 2;
        }
    }

    const QString name = QFileInfo(inputPath).completeBaseName();
    std::printf(
        "SAMPLE %s %dx%d levels=%d worst_chain1=%d worst_chain2=%d worst_chain3=%d worst_bilinear=%d worst_half=%d worst_cubic=%d\n",
        name.toUtf8().constData(),
        base.w,
        base.h,
        writtenLevels,
        worstChain1,
        worstChain2,
        worstChain3,
        worstBilinear,
        worstHalf,
        worstCubic
    );
    return 0;
}
