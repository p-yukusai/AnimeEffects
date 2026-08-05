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
// glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE) into the RGBA8
// target). aSrc/aDst are the stored PREMULTIPLIED contents of the composite and of the
// scene behind it (the DestinationTexturizer capture); opacity rides the fragment alpha.
enum class BlendOp { Normal, Multiply, Screen };
inline double blendChannel(BlendOp aOp, double a, double b) { // A = background, B = top
    switch (aOp) {
    case BlendOp::Multiply: return a * b;
    case BlendOp::Screen: return 1.0 - (1.0 - a) * (1.0 - b);
    default: return b;
    }
}
inline Image blendPresent(const Image& aSrc, const Image& aDst, BlendOp aOp, double aOpacity) {
    Image dst(aSrc.w, aSrc.h);
    for (int y = 0; y < aSrc.h; ++y) {
        for (int x = 0; x < aSrc.w; ++x) {
            const float* s = aSrc.at(x, y);
            const float* t = aDst.at(x, y);
            float* d = dst.at(x, y);
            const double da = t[3];
            const double sa = (double)s[3] * aOpacity; // uColor folds opacity into alpha only
            const double inv = 1.0 / std::max((double)s[3], 0.001);
            for (int c = 0; c < 3; ++c) {
                const double straight = s[c] * inv;
                const double blended = da * blendChannel(aOp, t[c], straight) + (1.0 - da) * straight;
                d[c] = (float)(blended * sa + t[c] * (1.0 - sa));
            }
            d[3] = (float)std::min(1.0, sa + da); // GL_ONE, GL_ONE, clamped by RGBA8
        }
    }
    return dst;
}

} // namespace ref

#endif // VERIFY_BLUR_CPU_REF_H
