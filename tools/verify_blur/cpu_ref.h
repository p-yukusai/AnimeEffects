// Independent CPU reference implementations used to verify the directional blur.
// Everything here is written from the documented semantics (see SCRATCHPAD.md and the
// comments in src/core/FilterFrame.cpp / src/core/WorldBlurMath.h), NOT by copying the
// production code, so a bug in production shows up as a mismatch.
//
// Coordinate conventions:
//  - Image rows follow GL readback order: row 0 is texture/window row 0 (the BOTTOM of the
//    canvas in AnimeEffects' world space, whose Y axis points down). World point (x, y)
//    maps to array (x, H - y), so a world-space direction (dx, dy) maps to the array/UV
//    direction (dx, -dy).
//  - Stored pixels are PREMULTIPLIED (the render targets accumulate src.rgb * src.a).
#ifndef VERIFY_BLUR_CPU_REF_H
#define VERIFY_BLUR_CPU_REF_H

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>
#include "img/BlendMode.h"

namespace ref {

constexpr double kPi = 3.14159265358979323846;

//-------------------------------------------------------------------------------------------------
// premultiplied float RGBA image
struct Image {
    int w = 0;
    int h = 0;
    std::vector<float> px; // 4 * w * h, row-major

    Image() = default;
    Image(int aW, int aH): w(aW), h(aH), px((size_t)aW * aH * 4, 0.0f) {}

    float* at(int aX, int aY) { return &px[((size_t)aY * w + aX) * 4]; }
    const float* at(int aX, int aY) const { return &px[((size_t)aY * w + aX) * 4]; }
};

using Vec4 = std::array<double, 4>;

// bytes are read back from a premultiplied render target: v = byte / 255 (no re-multiply)
inline Image imageFromBytes(const uint8_t* aData, int aW, int aH) {
    Image img(aW, aH);
    for (int y = 0; y < aH; ++y) {
        for (int x = 0; x < aW; ++x) {
            const uint8_t* s = aData + ((size_t)y * aW + x) * 4;
            float* d = img.at(x, y);
            for (int c = 0; c < 4; ++c)
                d[c] = s[c] / 255.0f;
        }
    }
    return img;
}

inline std::vector<uint8_t> imageToBytes(const Image& aImg) {
    std::vector<uint8_t> out((size_t)aImg.w * aImg.h * 4);
    for (size_t i = 0; i < aImg.px.size(); ++i) {
        const double v = std::floor(std::min(std::max((double)aImg.px[i], 0.0), 1.0) * 255.0 + 0.5);
        out[i] = (uint8_t)v;
    }
    return out;
}

//-------------------------------------------------------------------------------------------------
// GL bilinear sampling with CLAMP_TO_EDGE. (aX, aY) in texel coordinates; texel i spans
// [i, i+1) with its center at i + 0.5.
inline Vec4 sampleBilinear(const Image& aImg, double aX, double aY) {
    const double gx = aX - 0.5;
    const double gy = aY - 0.5;
    const int x0 = (int)std::floor(gx);
    const int y0 = (int)std::floor(gy);
    const double fx = gx - x0;
    const double fy = gy - y0;
    const int xs[2] = {std::min(std::max(x0, 0), aImg.w - 1), std::min(std::max(x0 + 1, 0), aImg.w - 1)};
    const int ys[2] = {std::min(std::max(y0, 0), aImg.h - 1), std::min(std::max(y0 + 1, 0), aImg.h - 1)};
    Vec4 out{0.0, 0.0, 0.0, 0.0};
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            const double w = (i ? fx : 1.0 - fx) * (j ? fy : 1.0 - fy);
            const float* s = aImg.at(xs[i], ys[j]);
            for (int c = 0; c < 4; ++c)
                out[c] += w * s[c];
        }
    }
    return out;
}

//-------------------------------------------------------------------------------------------------
// Exact replica of the separable Gaussian pass (kBlurFrag in src/core/FilterFrame.cpp):
// sigma = max(radius * 0.5, 0.001), taps = ceil(3 * sigma), normalized Gaussian weights,
// sampling at integer offsets along the pass direction through GL_LINEAR. The epsilon
// floor keeps small radii identity-like so interpolations ramp in from zero.
inline Image gaussPass(const Image& aSrc, double aDirX, double aDirY, double aRadiusTexels) {
    const double sigma = std::max(aRadiusTexels * 0.5, 0.001);
    const int taps = (int)std::ceil(sigma * 3.0);
    std::vector<double> weights((size_t)taps * 2 + 1);
    double wsum = 0.0;
    for (int i = -taps; i <= taps; ++i) {
        const double w = std::exp(-((double)i * i) / (2.0 * sigma * sigma));
        weights[(size_t)(i + taps)] = w;
        wsum += w;
    }
    Image dst(aSrc.w, aSrc.h);
    for (int y = 0; y < aSrc.h; ++y) {
        for (int x = 0; x < aSrc.w; ++x) {
            Vec4 acc{0.0, 0.0, 0.0, 0.0};
            for (int i = -taps; i <= taps; ++i) {
                const Vec4 s = sampleBilinear(aSrc, x + 0.5 + aDirX * i, y + 0.5 + aDirY * i);
                const double w = weights[(size_t)(i + taps)];
                for (int c = 0; c < 4; ++c)
                    acc[c] += w * s[c];
            }
            float* d = dst.at(x, y);
            for (int c = 0; c < 4; ++c)
                d[c] = (float)(acc[c] / wsum);
        }
    }
    return dst;
}

//-------------------------------------------------------------------------------------------------
// Replica of a Kind_Resample drawQuad pass: the destination pixel (x, y) samples the source
// at texel coords ((x+0.5) * srcW/dstW, srcH * (flipY ? 1-(y+0.5)/dstH : (y+0.5)/dstH)).
inline Image resamplePass(const Image& aSrc, int aDstW, int aDstH, bool aFlipY) {
    Image dst(aDstW, aDstH);
    for (int y = 0; y < aDstH; ++y) {
        for (int x = 0; x < aDstW; ++x) {
            const double tx = (x + 0.5) * aSrc.w / aDstW;
            const double ty = aFlipY ? aSrc.h * (1.0 - (y + 0.5) / aDstH) : (y + 0.5) * aSrc.h / aDstH;
            const Vec4 s = sampleBilinear(aSrc, tx, ty);
            float* d = dst.at(x, y);
            for (int c = 0; c < 4; ++c)
                d[c] = (float)s[c];
        }
    }
    return dst;
}

// Replica of FilterFrame::blurApply: downsample ladder (first level flips Y), separable
// Gaussian at the reduced level with reduced radii, upsample back (last pass flips Y).
// aFullW/aFullH is the composite size (level l buffers are full >> l).
inline Image ladderBlur(
    const Image& aSrc, int aFullW, int aFullH, double aMajorDirX, double aMajorDirY, double aRadius1,
    double aMinorDirX, double aMinorDirY, double aRadius2, int aLevel
) {
    const float scale = (float)(1 << aLevel);
    // 1. downsample through the ladder
    std::vector<Image> down((size_t)aLevel + 1);
    down[0] = aSrc;
    for (int l = 1; l <= aLevel; ++l) {
        down[(size_t)l] = resamplePass(down[(size_t)l - 1], aFullW >> l, aFullH >> l, l == 1);
    }
    // 2. Gaussian at the reduced level (major pass along the major axis, then the minor one),
    // matching blurApply's h/v BlurParams
    Image blurred = gaussPass(down[(size_t)aLevel], aMajorDirX, aMajorDirY, aRadius1 / scale);
    blurred = gaussPass(blurred, aMinorDirX, aMinorDirY, aRadius2 / scale);
    // 3. upsample back up
    Image cur = blurred;
    for (int l = aLevel - 1; l >= 1; --l) {
        cur = resamplePass(cur, aFullW >> l, aFullH >> l, false);
    }
    return resamplePass(cur, aFullW, aFullH, true);
}

//-------------------------------------------------------------------------------------------------
// 2x2 matrix (row-major) and an independent SVD for the blur ellipse reference.
struct Mat2 {
    double m00 = 1.0, m01 = 0.0, m10 = 0.0, m11 = 1.0;
};

inline Mat2 mul(const Mat2& aA, const Mat2& aB) {
    Mat2 r;
    r.m00 = aA.m00 * aB.m00 + aA.m01 * aB.m10;
    r.m01 = aA.m00 * aB.m01 + aA.m01 * aB.m11;
    r.m10 = aA.m10 * aB.m00 + aA.m11 * aB.m10;
    r.m11 = aA.m10 * aB.m01 + aA.m11 * aB.m11;
    return r;
}

// R(rotRad) * diag(sx, sy), the linear part of one node's local transform
inline Mat2 rotScale(double aRotRad, double aSX, double aSY) {
    const double c = std::cos(aRotRad);
    const double s = std::sin(aRotRad);
    Mat2 r;
    r.m00 = c * aSX;
    r.m01 = -s * aSY;
    r.m10 = s * aSX;
    r.m11 = c * aSY;
    return r;
}

// the content-space blur ellipse R(angleDeg) * diag(blurX, blurY)
inline Mat2 blurEllipse(double aBlurX, double aBlurY, double aAngleDeg) {
    const double rad = aAngleDeg * (kPi / 180.0);
    const double c = std::cos(rad);
    const double s = std::sin(rad);
    Mat2 r;
    r.m00 = c * aBlurX;
    r.m01 = -s * aBlurY;
    r.m10 = s * aBlurX;
    r.m11 = c * aBlurY;
    return r;
}

struct Ellipse {
    double majorX = 1.0, majorY = 0.0; // unit vector (the principal axis line; sign ambiguous)
    double minorX = 0.0, minorY = 1.0; // orthogonal to major
    double majorRadius = 0.0;
    double minorRadius = 0.0;
};

// Singular decomposition of M via a Jacobi rotation of A = M * M^T (a different
// computational path than production's closed-form discriminant).
inline Ellipse svd(const Mat2& aM) {
    const double a00 = aM.m00 * aM.m00 + aM.m01 * aM.m01;
    const double a01 = aM.m00 * aM.m10 + aM.m01 * aM.m11;
    const double a11 = aM.m10 * aM.m10 + aM.m11 * aM.m11;
    const double theta = 0.5 * std::atan2(2.0 * a01, a00 - a11);
    const double c = std::cos(theta);
    const double s = std::sin(theta);
    const double l1 = c * c * a00 + 2.0 * c * s * a01 + s * s * a11; // eigenvalue for (c, s)
    const double l2 = s * s * a00 - 2.0 * c * s * a01 + c * c * a11; // eigenvalue for (-s, c)
    Ellipse e;
    if (l1 >= l2) {
        e.majorX = c;
        e.majorY = s;
        e.minorX = -s;
        e.minorY = c;
        e.majorRadius = std::sqrt(std::max(l1, 0.0));
        e.minorRadius = std::sqrt(std::max(l2, 0.0));
    } else {
        e.majorX = -s;
        e.majorY = c;
        e.minorX = c;
        e.minorY = s;
        e.majorRadius = std::sqrt(std::max(l2, 0.0));
        e.minorRadius = std::sqrt(std::max(l1, 0.0));
    }
    return e;
}

// Reference world blur ellipse: nodes ordered leaf -> root, each (rotRad, scaleX, scaleY);
// the content-space blur ellipse folds in on the right as M * E.
inline Ellipse worldBlurEllipseRef(
    const std::vector<std::array<double, 3>>& aChain, double aBlurX, double aBlurY, double aAngleDeg
) {
    Mat2 m;
    for (const auto& n : aChain)
        m = mul(rotScale(n[0], n[1], n[2]), m);
    if (aBlurX != 1.0 || aBlurY != 1.0 || aAngleDeg != 0.0)
        m = mul(m, blurEllipse(aBlurX, aBlurY, aAngleDeg));
    return svd(m);
}

inline double det(const Mat2& aM) { return aM.m00 * aM.m11 - aM.m01 * aM.m10; }

// largest |M * u| over densely sampled unit vectors u (brute-force sigma_1 cross-check)
inline double maxStretch(const Mat2& aM) {
    double best = 0.0;
    for (int i = 0; i < 4096; ++i) {
        const double a = i * (2.0 * kPi / 4096.0);
        const double x = aM.m00 * std::cos(a) + aM.m01 * std::sin(a);
        const double y = aM.m10 * std::cos(a) + aM.m11 * std::sin(a);
        best = std::max(best, std::hypot(x, y));
    }
    return best;
}

//-------------------------------------------------------------------------------------------------
// Principal axis of the alpha channel distribution via second moments (a measurement of the
// ACTUAL rendered blur shape, translation-invariant). Angle is in array coordinates
// (x right, y DOWN the array, i.e. toward the canvas bottom in world space: world dir
// (dx, dy) appears here as (dx, -dy)).
inline double alphaPrincipalAngle(const Image& aImg, double& aOutMajor, double& aOutMinor) {
    double wsum = 0.0, cx = 0.0, cy = 0.0;
    for (int y = 0; y < aImg.h; ++y) {
        for (int x = 0; x < aImg.w; ++x) {
            const double a = aImg.at(x, y)[3];
            wsum += a;
            cx += a * x;
            cy += a * y;
        }
    }
    if (wsum <= 0.0) {
        aOutMajor = aOutMinor = 0.0;
        return 0.0;
    }
    cx /= wsum;
    cy /= wsum;
    double cxx = 0.0, cxy = 0.0, cyy = 0.0;
    for (int y = 0; y < aImg.h; ++y) {
        for (int x = 0; x < aImg.w; ++x) {
            const double a = aImg.at(x, y)[3];
            const double dx = x - cx;
            const double dy = y - cy;
            cxx += a * dx * dx;
            cxy += a * dx * dy;
            cyy += a * dy * dy;
        }
    }
    cxx /= wsum;
    cxy /= wsum;
    cyy /= wsum;
    const double theta = 0.5 * std::atan2(2.0 * cxy, cxx - cyy);
    const double c = std::cos(theta), s = std::sin(theta);
    aOutMajor = c * c * cxx + 2.0 * c * s * cxy + s * s * cyy;
    aOutMinor = s * s * cxx - 2.0 * c * s * cxy + c * c * cyy;
    return theta;
}

// smallest signed difference a - b on the angle circle of period kPeriod (degrees)
inline double angleDiffDeg(double aA, double aB, double aPeriod) {
    double d = std::fmod(aA - aB, aPeriod);
    if (d > aPeriod * 0.5)
        d -= aPeriod;
    if (d < -aPeriod * 0.5)
        d += aPeriod;
    return d;
}

//-------------------------------------------------------------------------------------------------
// Exact replica of the presentation-pass HSV adjust (LayerDrawingFrag.glsl, USE_HSV with
// PREMULTIPLIED_INPUT=1): the stored premultiplied texel is un-multiplied, adjusted in HSV
// space, and presented onto an empty target, i.e. re-multiplied by the same alpha. Uniform
// mapping follows FilterFrame::drawQuad / LayerNode: hue = key[0]/360 turns,
// sat = key[1]/100, val = key[2]/100, setColor = key[3] != 0.
inline Image hsvAdjust(const Image& aSrc, double aHueTurns, double aSatMul, double aValMul, bool aSetColor) {
    Image dst(aSrc.w, aSrc.h);
    for (int y = 0; y < aSrc.h; ++y) {
        for (int x = 0; x < aSrc.w; ++x) {
            const float* s = aSrc.at(x, y);
            float* d = dst.at(x, y);
            const double a = s[3];
            const double inv = 1.0 / std::max(a, 0.001);
            const double r = s[0] * inv, g = s[1] * inv, b = s[2] * inv;
            // RGBtoHSV (GLSL): K = (0, -1/3, 2/3, -1); mix/step semantics replicated
            double px, py, pz, pw;
            if (g < b) { px = b; py = g; pz = -1.0; pw = 2.0 / 3.0; }
            else { px = g; py = b; pz = 0.0; pw = -1.0 / 3.0; }
            double qx, qy, qz, qw;
            if (r < px) { qx = px; qy = py; qz = pw; qw = r; }
            else { qx = r; qy = py; qz = pz; qw = px; }
            const double dd = qx - std::min(qw, qy);
            const double e = 1.0e-10;
            double hue = std::abs(qz + (qw - qy) / (6.0 * dd + e));
            double sat = dd / (qx + e);
            double val = qx;
            hue = aSetColor ? aHueTurns : hue + aHueTurns;
            hue -= std::floor(hue); // fract
            sat = std::min(1.0, sat * aSatMul);
            val = std::min(1.0, val * aValMul);
            // HSVtoRGB (GLSL): K = (1, 2/3, 1/3, 3); p = abs(fract(h + K.xyz)*6 - 3)
            const double kk[3] = {1.0, 2.0 / 3.0, 1.0 / 3.0};
            double rgb[3];
            for (int c = 0; c < 3; ++c) {
                const double f = hue + kk[c];
                const double p = std::abs((f - std::floor(f)) * 6.0 - 3.0);
                const double m = std::min(std::max(p - 1.0, 0.0), 1.0);
                rgb[c] = val * ((1.0 - sat) + m * sat);
            }
            d[0] = (float)(rgb[0] * a);
            d[1] = (float)(rgb[1] * a);
            d[2] = (float)(rgb[2] * a);
            d[3] = (float)a;
        }
    }
    return dst;
}

//-------------------------------------------------------------------------------------------------
// Exact replica of the blend presentation (LayerDrawingFrag.glsl blendColor with
// PREMULTIPLIED_INPUT=1, followed by the framebuffer blend
// glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA) into the
// RGBA8 target). aSrc/aDst are the stored PREMULTIPLIED contents of the composite and of the
// scene behind it (the DestinationTexturizer capture).
// Shader order (matters): the source is un-multiplied (color.rgb /= max(color.a, 0.001)), THEN
// `color *= uColor` where uColor is the app's QColor(255, 255, 255, quantizedOpacity) - the RGB
// factor is exactly 1.0 (white), so the opacity scales the ALPHA only, then the blend runs
// against the dest texel, then the over-rule applies on top. uColor.a is quantized like the
// app's 8-bit color, so a fractional present opacity must be passed pre-quantized
// ((int)(255 * op) / 255). The alpha channel accumulates with the over-rule
// (src.a + dst.a*(1-src.a)), never exceeding 1 for inputs <= 1, so 16F slots are safe with
// this operator.
// Per-channel blend math mirrors the BLEND_* macros of LayerDrawingFrag.glsl lines 7-28
// (A = background, B = top). The ternary guards (B == 0.0 / B == 1.0, A < 0.5, B < 0.5)
// evaluate exactly one branch like GLSL, so the guarded divisions never see a zero divisor
// and the exact-float comparisons behave identically on 8-bit quantized data.
inline double blendColorBurn(double a, double b) { return b == 0.0 ? b : std::max(0.0, 1.0 - (1.0 - a) / b); }
inline double blendColorDodge(double a, double b) { return b == 1.0 ? b : std::min(1.0, a / (1.0 - b)); }
inline double blendLinearBurn(double a, double b) { return std::max(0.0, a + b - 1.0); }
inline double blendLinearDodge(double a, double b) { return std::min(1.0, a + b); }
inline double blendChannel(img::BlendMode aMode, double a, double b) {
    switch (aMode) {
    case img::BlendMode_Darken: return a > b ? b : a;
    case img::BlendMode_Multiply: return a * b;
    case img::BlendMode_ColorBurn: return blendColorBurn(a, b);
    case img::BlendMode_LinearBurn: return blendLinearBurn(a, b);
    case img::BlendMode_Lighten: return a > b ? a : b;
    case img::BlendMode_Screen: return 1.0 - (1.0 - a) * (1.0 - b);
    case img::BlendMode_ColorDodge: return blendColorDodge(a, b);
    case img::BlendMode_LinearDodge: return blendLinearDodge(a, b);
    case img::BlendMode_Overlay: return a < 0.5 ? 2.0 * a * b : 1.0 - 2.0 * (1.0 - a) * (1.0 - b);
    case img::BlendMode_SoftLight:
        return a < 0.5 ? 2.0 * a * b + a * a * (1.0 - 2.0 * b)
                       : std::sqrt(a) * (2.0 * b - 1.0) + 2.0 * a * (1.0 - b);
    case img::BlendMode_HardLight: return b < 0.5 ? 2.0 * a * b : 1.0 - 2.0 * (1.0 - a) * (1.0 - b);
    case img::BlendMode_VividLight:
        return b < 0.5 ? blendColorBurn(a, 2.0 * b) : blendColorDodge(a, 2.0 * (b - 0.5));
    case img::BlendMode_LinearLight:
        return b < 0.5 ? blendLinearBurn(a, 2.0 * b) : blendLinearDodge(a, 2.0 * (b - 0.5));
    case img::BlendMode_PinLight:
        return b < 0.5 ? (a > 2.0 * b ? 2.0 * b : a) : (a > 2.0 * (b - 0.5) ? a : 2.0 * (b - 0.5));
    case img::BlendMode_HardMix:
        return (b < 0.5 ? blendColorBurn(a, 2.0 * b) : blendColorDodge(a, 2.0 * (b - 0.5))) < 0.5 ? 0.0 : 1.0;
    case img::BlendMode_Difference: return std::abs(a - b);
    case img::BlendMode_Exclusion: return a + b - 2.0 * a * b;
    case img::BlendMode_Subtract: return std::max(0.0, a - b);
    case img::BlendMode_Divide: return b == 0.0 ? b : std::min(1.0, a / b);
    // CSP Add (Glow) / Glow Dodge (mirror the BLEND_PREMULTIPLIED_SRC macros):
    // B is already the alpha-premultiplied source (see blendPresent's psrc), so
    // these are the plain per-channel formulas. Glow Dodge replicates CSP's 8-bit
    // integer math (out = min(255, (bg*255) // max(255-fg, 1)), truncating
    // division; floor + 0.001 guards float rounding like the shader).
    case img::BlendMode_AddGlow: return std::min(a + b, 1.0);
    case img::BlendMode_GlowDodge:
        return std::min(std::floor(a * 65025.0 / std::max(255.0 - b * 255.0, 1.0) + 0.001) / 255.0, 1.0);
    default: return b; // BlendMode_Normal
    }
}

// Whole-RGB blends for the non-separable modes (Hue..LighterColor), mirroring the
// vec3 BLEND_* helpers of LayerDrawingFrag.glsl (W3C compositing spec, Rec.601 luma).
// A = background, B = top.
inline double blendLum(const double c[3]) { return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]; }
// ClipColor is deliberately NOT denominator-clamped (a follow-up review suggested
// clamping l-n / x-l in the replica): the replica must mirror the shader bit-for-bit
// (data/shader/LayerDrawingFrag.glsl BlendClipColor). The ratios are algebraically
// bounded anyway wherever the branches fire in the non-separable path: each mode ends
// in BlendSetLum(c, BlendLum(A)) with A a background color, so the target luma is
// L in [0,1]; the n<0 branch then has l-n > l >= 0 and the x>1 branch has x-l > 1-l,
// giving ratios in (0,1) that clamp output back into range. A clamped replica would
// diverge from the GPU on in-range inputs while buying nothing on out-of-range ones.
inline void blendClipColor(double c[3]) {
    const double l = blendLum(c);
    const double n = std::min(c[0], std::min(c[1], c[2]));
    const double x = std::max(c[0], std::max(c[1], c[2]));
    if (n < 0.0)
        for (int i = 0; i < 3; ++i)
            c[i] = l + (c[i] - l) * (l / (l - n));
    if (x > 1.0)
        for (int i = 0; i < 3; ++i)
            c[i] = l + (c[i] - l) * ((1.0 - l) / (x - l));
}
inline void blendSetLum(double c[3], double l) {
    const double d = l - blendLum(c);
    for (int i = 0; i < 3; ++i)
        c[i] += d;
    blendClipColor(c);
}
inline void blendSetSat(double c[3], double s) {
    const double lo = std::min(c[0], std::min(c[1], c[2]));
    const double hi = std::max(c[0], std::max(c[1], c[2]));
    for (int i = 0; i < 3; ++i)
        c[i] = hi > lo ? (c[i] - lo) * (s / (hi - lo)) : 0.0;
}
inline double blendSat(const double c[3]) {
    return std::max(c[0], std::max(c[1], c[2])) - std::min(c[0], std::min(c[1], c[2]));
}
// writes the blended RGB triple for any mode (separable modes per channel)
inline void blendRgb(img::BlendMode aMode, const double a[3], const double b[3], double out[3]) {
    double t[3];
    switch (aMode) {
    case img::BlendMode_Hue:
        for (int i = 0; i < 3; ++i) t[i] = b[i];
        blendSetSat(t, blendSat(a));
        blendSetLum(t, blendLum(a));
        break;
    case img::BlendMode_Saturation:
        for (int i = 0; i < 3; ++i) t[i] = a[i];
        blendSetSat(t, blendSat(b));
        blendSetLum(t, blendLum(a));
        break;
    case img::BlendMode_Color:
        for (int i = 0; i < 3; ++i) t[i] = b[i];
        blendSetLum(t, blendLum(a));
        break;
    case img::BlendMode_Luminosity:
        for (int i = 0; i < 3; ++i) t[i] = a[i];
        blendSetLum(t, blendLum(b));
        break;
    case img::BlendMode_DarkerColor: {
        const double* p = blendLum(b) <= blendLum(a) ? b : a;
        for (int i = 0; i < 3; ++i) t[i] = p[i];
        break;
    }
    case img::BlendMode_LighterColor: {
        const double* p = blendLum(b) > blendLum(a) ? b : a;
        for (int i = 0; i < 3; ++i) t[i] = p[i];
        break;
    }
    default:
        for (int i = 0; i < 3; ++i)
            t[i] = blendChannel(aMode, a[i], b[i]);
        break;
    }
    for (int i = 0; i < 3; ++i)
        out[i] = t[i];
}
inline Image blendPresent(const Image& aSrc, const Image& aDst, img::BlendMode aMode, double aOpacity) {
    Image dst(aSrc.w, aSrc.h);
    for (int y = 0; y < aSrc.h; ++y) {
        for (int x = 0; x < aSrc.w; ++x) {
            const float* s = aSrc.at(x, y);
            const float* t = aDst.at(x, y);
            float* d = dst.at(x, y);
            const double da = t[3];
            const double inv = 1.0 / std::max((double)s[3], 0.001);
            const double sa = (double)s[3] * aOpacity; // result.a = src.a after `color *= uColor`
            // shader order: straight = s[c] * inv, then `color *= uColor` scales the
            // alpha by the present opacity (uColor.rgb is white = 1.0), then the
            // blend, then the over-rule
            const double straight[3] = {s[0] * inv, s[1] * inv, s[2] * inv};
            // non-separable blends run on STRAIGHT colors: the dest is stored premultiplied
            // (t[c] = Cb * da), so the shader's BLEND_NONSEPARABLE branch recovers Cb by
            // dividing by the alpha first (a transparent pixel has no defined straight
            // color and contributes nothing, so 0 is its neutral input). Separable modes
            // keep the premultiplied dest as-is (mirrors the shader exactly).
            const double dest[3] = {t[0], t[1], t[2]};
            const double destStraight[3] = {
                da > 0.0 ? t[0] / da : 0.0, da > 0.0 ? t[1] / da : 0.0, da > 0.0 ? t[2] / da : 0.0};
            double blended[3];
            // CSP Add (Glow) / Glow Dodge (BLEND_PREMULTIPLIED_SRC): the blend input
            // is the alpha-premultiplied source rounded to 8-bit (psrc =
            // round(straight*sa*255)/255, sa = src alpha x present opacity), the
            // result replaces the backdrop outright, and the fragment alpha is 1.0
            // (the framebuffer alpha pass is bypassed) - mirroring the shader.
            if (img::isPremultipliedSrcBlendMode(aMode)) {
                const double psrc[3] = {std::floor(straight[0] * sa * 255.0 + 0.5) / 255.0,
                    std::floor(straight[1] * sa * 255.0 + 0.5) / 255.0, std::floor(straight[2] * sa * 255.0 + 0.5) / 255.0};
                blendRgb(aMode, destStraight, psrc, blended);
                for (int c = 0; c < 3; ++c)
                    d[c] = (float)blended[c];
                d[3] = 1.0f;
                continue;
            }
            blendRgb(aMode, img::isNonSeparableBlendMode(aMode) ? destStraight : dest, straight, blended);
            for (int c = 0; c < 3; ++c) {
                const double v = da * blended[c] + (1.0 - da) * straight[c];
                d[c] = (float)(v * sa + t[c] * (1.0 - sa));
            }
            d[3] = (float)(sa + da * (1.0 - sa)); // over-rule: GL_ONE, GL_ONE_MINUS_SRC_ALPHA
        }
    }
    return dst;
}

} // namespace ref

#endif // VERIFY_BLUR_CPU_REF_H
