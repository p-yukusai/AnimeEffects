#ifndef IMG_PREMULTIPLY_H
#define IMG_PREMULTIPLY_H

#include "XC.h"

// SSE2 is baseline on x86-64 and available on 32-bit x86 with -msse2; every other target takes the
// scalar reference below, which is the same formula (and the same bytes).
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define IMG_PREMULTIPLY_SSE2 1
#include <emmintrin.h>
#endif

namespace img {

/// STRAIGHT RGBA8 -> ALPHA-PREMULTIPLIED RGBA8 for one run of aCount texels, round to nearest
/// ((c*a + 127) / 255), alpha unchanged - the scalar reference, and the only implementation on a
/// target without SSE2.
inline void premultiplyScalar(const uint8* aSrc, uint8* aDst, int aCount) {
    for (int i = 0; i < aCount; ++i) {
        const uint32 a = aSrc[3];
        aDst[0] = (uint8)((aSrc[0] * a + 127) / 255);
        aDst[1] = (uint8)((aSrc[1] * a + 127) / 255);
        aDst[2] = (uint8)((aSrc[2] * a + 127) / 255);
        aDst[3] = (uint8)a;
        aSrc += 4;
        aDst += 4;
    }
}

#ifdef IMG_PREMULTIPLY_SSE2
/// FOUR TEXELS AT A TIME: each texel's alpha is broadcast over its three colour lanes, the product
/// goes through the same (x + 127) / 255 as a multiply-high (exact: (x * 0x8081) >> 23 is x / 255
/// for the 16-bit products here), and the alpha lanes multiply by 255 so they come back unchanged.
/// Validated against premultiplyScalar over every (channel, alpha) pair by verify_mipmap's P0.
inline void premultiplySse2(const uint8* aSrc, uint8* aDst, int aCount) {
    const __m128i zero = _mm_setzero_si128();
    const __m128i magic = _mm_set1_epi16((short)0x8081);
    const __m128i round = _mm_set1_epi16(127);
    const __m128i keepAlpha = _mm_setr_epi16(0, 0, 0, -1, 0, 0, 0, -1);
    const __m128i full = _mm_set1_epi16(255);
    int i = 0;
    for (; i + 4 <= aCount; i += 4) {
        const __m128i v = _mm_loadu_si128((const __m128i*)aSrc);
        __m128i lo = _mm_unpacklo_epi8(v, zero); // R0 G0 B0 A0 R1 G1 B1 A1, one channel per lane
        __m128i hi = _mm_unpackhi_epi8(v, zero); // the same for texels 2 and 3
        // each texel's alpha over its three colour lanes
        __m128i mulLo = _mm_shufflehi_epi16(_mm_shufflelo_epi16(lo, 0xFF), 0xFF);
        __m128i mulHi = _mm_shufflehi_epi16(_mm_shufflelo_epi16(hi, 0xFF), 0xFF);
        // an alpha lane multiplies by 255, so it lands unchanged
        mulLo = _mm_or_si128(_mm_and_si128(keepAlpha, full), _mm_andnot_si128(keepAlpha, mulLo));
        mulHi = _mm_or_si128(_mm_and_si128(keepAlpha, full), _mm_andnot_si128(keepAlpha, mulHi));
        lo = _mm_add_epi16(_mm_mullo_epi16(lo, mulLo), round);
        hi = _mm_add_epi16(_mm_mullo_epi16(hi, mulHi), round);
        lo = _mm_srli_epi16(_mm_mulhi_epu16(lo, magic), 7);
        hi = _mm_srli_epi16(_mm_mulhi_epu16(hi, magic), 7);
        _mm_storeu_si128((__m128i*)aDst, _mm_packus_epi16(lo, hi));
        aSrc += 16;
        aDst += 16;
    }
    premultiplyScalar(aSrc, aDst, aCount - i);
}
#endif // IMG_PREMULTIPLY_SSE2

/// THE CONVERSION THE TEXTURES STORE, for both of its callers: the texture upload
/// (Util::makeTextureUpload) and a paged image's level-0 page fill (core::ImagePyramid::fillPage).
/// They have to agree to the byte, or a page is not the texels the full-resolution chain holds, so
/// the formula lives once. verify_mipmap's P0 grades both the dispatched form and the fallback
/// against an independent reference over every (channel, alpha) pair.
///
/// WHY THE VECTOR PATH IS THE ONE THAT MATTERS, measured on ballerina at a 640x640 zoom-1 view
/// (31 level-0 pages a landing frame, per-page fill from AE_VT_LANDING_SPLIT): the branchful loop
/// this file replaced and the branchless scalar here are INDISTINGUISHABLE (median 0.239 ms a page
/// both - the `a == 255` branch was not the cost in situ, whatever it is on hot data), while the
/// SSE2 path measures **0.087 ms median, 0.052 minimum** - about 2.7x. A landing frame's fill went
/// from 6.4-13.6 ms to 2.2-3.1 ms (docs/design/renderer/texture-residency.md, open item 3).
inline void premultiply(const uint8* aSrc, uint8* aDst, int aCount) {
#ifdef IMG_PREMULTIPLY_SSE2
    premultiplySse2(aSrc, aDst, aCount);
#else
    premultiplyScalar(aSrc, aDst, aCount);
#endif
}

} // namespace img

#endif // IMG_PREMULTIPLY_H
