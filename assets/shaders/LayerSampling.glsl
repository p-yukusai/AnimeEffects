// THE LAYER SAMPLER - shared by the layer draw, the clipper's singulation pass and the mask
// rubylith overlay.
//
// ONE READ, BOTH PASSES. The clipper used to sample its own alpha with texture() - the hardware's
// implicit-LOD, anisotropic read - while the layer it clipped drew through this reconstruction.
// The two filters disagreed at every clipped edge, and a paged clipper could not reproduce the
// hardware read at all. So the clipper pass takes the same sampler; with VIRTUAL_TEXTURE it takes
// the same pages and coarse fallback, which is what lets clipper layers be paged.
//
// THE MASK TAKES THE SAME RECONSTRUCTION (sampleMask). A matte is a field with hard edges and
// painted texture, exactly like an image, and a paged matte and its rubylith must agree with the
// full chain they replace - so the layer's own footprint rule is reused for the mask's uv space,
// with the mask's chain as the source. The mask's border is GL_CLAMP_TO_EDGE (MaskMipChain), not
// the image's border colour, and the page gutter repeats the edge texel to match it.
//
// The sampler's own rationale (the two-tap, one-level reconstruction) is the comment below.

#if VIRTUAL_TEXTURE || MASK_VIRTUAL_TEXTURE
uniform float uPageTexels;             // the page size, 256
uniform float uPageAtlasSize;          // 4096
uniform float uPagePitch;              // page + gutter, 260
uniform float uPageGutter;             // 2
uniform int uPageSlotsPerAxis;         // 4096 / 260 = 15
uniform int uAtlasCount;               // how many of the four below are real
#endif

// 0: an absent page samples the coarse chain (production). 1: it returns MAGENTA (no entry) or
// CYAN (an entry that names nothing usable). A RUNTIME uniform, not a compile-time variation, so
// the program is the SAME in both modes: a diagnostic that changed the shader's code would also
// change the frame's timing, and this defect is perturbed by timing (see the design doc).
uniform int uVtMissDebug;

#if VIRTUAL_TEXTURE
uniform sampler2D uCoarseTexture;      // unit 4: the always-resident levels
uniform usampler2D uPageTable;         // unit 5: (pageX, levelRow + pageY) -> slot
uniform int uPageLevelRow[16];         // the first row of each page level in that table
uniform vec2 uLayerSize;               // the CONTENT's size in level-0 texels
uniform int uCoarseLevel;              // the first level the coarse chain holds
uniform sampler2D uAtlas0;
uniform sampler2D uAtlas1;
uniform sampler2D uAtlas2;
uniform sampler2D uAtlas3;
#endif

#if MASK_VIRTUAL_TEXTURE
uniform sampler2D uMaskCoarseTexture;  // unit 10: the mask's always-resident levels
uniform usampler2D uMaskPageTable;     // unit 11
uniform int uMaskPageLevelRow[16];
uniform int uMaskCoarseLevel;
uniform sampler2D uMaskAtlas0;         // units 12..15, an R8 stack of their own
uniform sampler2D uMaskAtlas1;
uniform sampler2D uMaskAtlas2;
uniform sampler2D uMaskAtlas3;
#endif

#if MASK_VIRTUAL_TEXTURE
// THE PAGED MASK'S GRID AND DEEPEST LEVEL. The full path reads both from the texture itself, so
// a pass that pages only the mask (the rubylith) sets nothing else.
uniform vec2 uMaskSize;
uniform float uMaskMaxLod;
#endif

uniform float uLayerLodBias;    // the layer's LOD bias, in levels
uniform float uLayerMaxLod;     // the texture's deepest level
// AE_VT_DEBUG_LEVEL: 1 outputs the level this fragment CHOSE as red (level/16), on BOTH paths, so
// a control frame and a paged frame can be compared level for level. A uniform, so the program
// does not change with it and the two runs differ only in what the read returns.
uniform int uVtDebugLevel;
// AE_VT_TAG_LAYER: 2 in uVtDebugLevel makes every draw output uVtTag as an OPAQUE colour, so the
// composite shows which layer is on top at a pixel. The layer prints its own tag, so the colour at
// a pixel names the layer without a bisection over 130 of them.
uniform vec3 uVtTag;

// THE FOOTPRINT. The footprint comes from the draw itself: J = [dFdx dFdy] of the texel-space uv
// (aUv * size0) is the 2x2 Jacobian of the draw, and its singular values are the footprint's
// axes. J^T J is symmetric 2x2, so its eigenvalues are the squared singular values
// (major >= minor); its major eigenvector is a SCREEN-space direction, and the footprint's major
// axis in TEXEL space - the space the taps offset - is J times that vector. A derivative is exact
// for an affine draw - phase-independently, measured identical to the transform's footprint over
// a 24-step sub-pixel sweep - and it is the only thing that follows a mesh/FFD/bone deformation,
// which the transform cannot see, so nothing else is needed.
//
// THE LEVEL comes from the MINOR axis, because the taps integrate the MAJOR one: a footprint of
// 8 x 1 texels is one texel thick across, so floor(log2(minor)) is the level whose texels the
// taps match across, and the major axis is covered by the taps' extent rather than by the
// level's blur.
struct LayerFootprint {
    float major;    // the footprint's major axis, level-0 texels
    float minor;
    vec2 majorDir;  // unit vector, IN TEXEL SPACE
    float lod;      // the biased level, unclamped
};

LayerFootprint layerFootprint(vec2 aUv, vec2 aSize0)
{
    vec2 tuv = aUv * aSize0;
    vec2 dx = dFdx(tuv);
    vec2 dy = dFdy(tuv);
    // J^T J is symmetric 2x2: its eigenvalues are the squared singular values of J
    float jxx = dot(dx, dx);
    float jxy = dot(dx, dy);
    float jyy = dot(dy, dy);
    float halfTrace = (jxx + jyy) * 0.5;
    float disc = sqrt(max(halfTrace * halfTrace - (jxx * jyy - jxy * jxy), 0.0));
    LayerFootprint f;
    f.major = sqrt(max(halfTrace + disc, 0.0));
    f.minor = sqrt(max(halfTrace - disc, 0.0));
    // THE MAJOR AXIS, IN TEXEL SPACE. J^T J acts on SCREEN offsets (J is the screen->texel map), so
    // its major eigenvector is the footprint's long axis as a SCREEN direction. The taps below
    // offset aUv, which is TEXEL space, so that screen vector cannot be used as an offset directly:
    // the texel-space axis is the left singular vector, J times the right one (normalised). The two
    // coincide only when J is symmetric - for an anisotropic draw that is rotated relative to the
    // texture, the screen eigenvector turns with the draw while the footprint's texel-space axis
    // stays put, so using it directly rotates the taps away from the major axis by exactly the
    // draw's rotation (at 90 degrees the taps ran ACROSS the footprint, which is the measured
    // rotation-dependent error). An eigenvector's scale and sign are arbitrary; both cancel.
    vec2 eig = vec2(jxy, halfTrace + disc - jxx);
    if (dot(eig, eig) < 1e-20)
        eig = vec2(halfTrace + disc - jyy, jxy);
    vec2 majorDir = dx * eig.x + dy * eig.y;    // J * eig: the screen axis in texel space
    if (dot(majorDir, majorDir) < 1e-20)
        majorDir = vec2(1.0, 0.0);              // isotropic (J ~ c * rotation): any axis will do
    f.majorDir = normalize(majorDir);

    // The bias is applied to the UNCLAMPED level and magnification returns before any tap.
    f.lod = log2(max(f.minor, 1e-6)) + uLayerLodBias;
    return f;
}

// THE TAPS: TWO taps on ONE level, each a 2x2 block of filtered reads spanning major/2 along the
// major axis and minor across it, placed at t = +-0.25 * major along the major axis. The per-level
// span is deconvolved by 0.8 for levels above 0, because a level texel is already a box average of
// the level below. That is eight texel fetches - the same count as the hardware trilinear read's
// two levels of one 2x2 tap - with no mix between levels, so none of the level blend's blur
// enters.
struct LayerTaps {
    vec2 majorDir;
    vec2 stepMaj;   // the raw, component-wise divided step (the caller applies majorDir)
    vec2 stepMin;
};

LayerTaps layerTaps(const LayerFootprint aFootprint, float aLevel, vec2 aSizeLf)
{
    float scale = exp2(aLevel);
    float majorL = aFootprint.major / scale;
    float minorL = aFootprint.minor / scale;
    // A level texel is already a box average of the level below, so a tap's span is deconvolved by
    // 0.8 above level 0. One tap covers major/2 along the major axis and minor across it.
    float levelSpan = (aLevel < 0.5) ? 1.0 : 0.8;
    LayerTaps t;
    t.majorDir = aFootprint.majorDir;
    t.stepMaj = majorL * levelSpan / 2.0 / aSizeLf;
    t.stepMin = minorL * levelSpan * 0.5 / aSizeLf;
    return t;
}

// The eight tap positions, in uv. ONE function so the layer and the mask cannot disagree about
// the reconstruction: only the read behind a tap differs.
void layerTapCentres(
    vec2 aUv, const LayerFootprint aFootprint, const LayerTaps aTaps, float aLevel, vec2 aSizeLf,
    out vec2 aCentres[8]
)
{
    float scale = exp2(aLevel);
    float majorL = aFootprint.major / scale;
    vec2 acrossDir = vec2(-aTaps.majorDir.y, aTaps.majorDir.x);
    int n = 0;
    for (int t = 0; t < 2; ++t) {
        // the same expression order as the pre-refactor loop: `major/scale` first, then the tap's
        // fraction, so this function cannot move a tap by a rounding step
        float along = ((float(t) + 0.5) / 2.0 - 0.5) * majorL;
        vec2 centre = aUv + (aTaps.majorDir * along) / aSizeLf;
        for (int k = 0; k < 2; ++k)
            for (int j = 0; j < 2; ++j) {
                vec2 off = aTaps.majorDir * ((float(j) + 0.5) * aTaps.stepMaj - aTaps.stepMaj)
                    + acrossDir * ((float(k) + 0.5) * aTaps.stepMin - aTaps.stepMin);
                aCentres[n] = centre + off;
                ++n;
            }
    }
}

#if VIRTUAL_TEXTURE
// THE PAGED READ. A level is no longer one texture: page (px, py) of it lives at the slot the
// table names, inside one of up to four atlases, with a 2-texel gutter of the level's
// neighbouring texels around it. So a filtered read at a level texel is: which page owns it, is
// that page resident, where is its slot, and what are the atlas coordinates of the texel.
//
// EVERY TAP RESOLVES ITS OWN PAGE, not the pixel's: a tap is offset by up to the footprint's
// anisotropy ratio in level texels, which can cross a page boundary. The gutter is what makes the
// read INSIDE a page seamless; the per-tap lookup is what makes the read across one correct.
//
// A MISS SAMPLES THE COARSE CHAIN: the always-resident levels from uCoarseLevel down. Its level
// k is the full chain's level uCoarseLevel + k, so the relative level is the same content at
// the same texel grid - a blur, never a hole. The transition at a page boundary is hard by
// design (the two tiers are separate reads, not a level blend), and a page arriving is a
// quality increase rather than a pop.
vec4 sampleCoarse(vec2 aUv, float aLevel)
{
    return textureLod(uCoarseTexture, aUv, max(0.0, aLevel - float(uCoarseLevel)));
}

vec4 sampleAtlas(int aAtlas, vec2 aAtlasTexel)
{
    vec2 uv = aAtlasTexel / uPageAtlasSize;
    if (aAtlas == 0) return textureLod(uAtlas0, uv, 0.0);
    if (aAtlas == 1) return textureLod(uAtlas1, uv, 0.0);
    if (aAtlas == 2) return textureLod(uAtlas2, uv, 0.0);
    return textureLod(uAtlas3, uv, 0.0);
}

vec4 samplePage(vec2 aUv, float aLevel)
{
    vec2 sizeL = max(vec2(1.0), floor(uLayerSize / exp2(aLevel)));
    vec2 p = aUv * sizeL;
    // THE CONTENT'S BORDER. The full texture's clamp-to-border returns 0 outside the content, but
    // its bilinear read still blends the edge texel when the coordinate is within ONE texel of
    // it - and a reconstruction tap placed just outside a layer's edge is exactly that case. The
    // page's gutter holds the level's neighbouring texels and its zeros beyond them, so a read
    // within one level texel is left to the atlas; only a coordinate whose whole 2x2 footprint is
    // outside returns 0. Zeroing every tap centre outside [0,1] instead measured as a one-pixel
    // band at each layer's top/bottom edge (max 40-44 of 255 on test.anie): the taps that should
    // have blended the edge texel contributed nothing.
    if (p.x < -1.0 || sizeL.x + 1.0 < p.x || p.y < -1.0 || sizeL.y + 1.0 < p.y)
        return vec4(0.0);

    vec2 grid = ceil(sizeL / uPageTexels);
    // p == sizeL can land one past the last page when the level divides evenly; the page's
    // out-of-content texels are zero, so clamping the PAGE and leaving `local` at the content
    // edge reproduces the border read
    vec2 page = clamp(floor(p / uPageTexels), vec2(0.0), grid - vec2(1.0));
    vec2 local = p - page * uPageTexels;
    int level = int(aLevel);

    uint entry = texelFetch(uPageTable, ivec2(page) + ivec2(0, uPageLevelRow[level]), 0).r;
    // THE FALLBACK MADE VISIBLE (AE_VT_MISS_MAGENTA): magenta is a page the table has no entry
    // for, cyan an entry that names nothing usable. A frame drawn with this on answers "which
    // pixels did not find their page" by inspection, with no readback and no stall - the two
    // properties every other page verifier here lacks.
    if (entry == 0u)
        return (uVtMissDebug != 0) ? vec4(1.0, 0.0, 1.0, 1.0) : sampleCoarse(aUv, aLevel);

    int atlas = int(entry >> 8);
    int slot = int(entry & 255u) - 1;
    if (slot < 0 || atlas >= uAtlasCount)
        return (uVtMissDebug != 0) ? vec4(0.0, 1.0, 1.0, 1.0)
                                   : sampleCoarse(aUv, aLevel);   // a stale entry: never a hole

    // the slot's CONTENT origin, in atlas texels, plus the gutter the tap reads from
    vec2 origin = vec2(float(slot % uPageSlotsPerAxis), float(slot / uPageSlotsPerAxis))
        * uPagePitch + vec2(uPageGutter);
    return sampleAtlas(atlas, origin + local);
}
#endif

vec4 sampleLayer(sampler2D aTexture, vec2 aUv)
{
#if VIRTUAL_TEXTURE
    vec2 size0 = uLayerSize;
#else
    vec2 size0 = vec2(textureSize(aTexture, 0));
#endif
    if (uVtDebugLevel == 2)
        return vec4(uVtTag, 1.0);
    // 3: the uv this fragment samples, as colour (R = u, G = v, B = level/16). A control frame and
    // a paged frame of the SAME layer then compare the read's geometry, not its result.
    if (uVtDebugLevel == 3)
        return vec4(aUv.x, aUv.y, 0.0, 1.0);
    LayerFootprint footprint = layerFootprint(aUv, size0);
    if (footprint.lod <= 0.0)
#if VIRTUAL_TEXTURE
        return samplePage(aUv, 0.0);      // magnified: level 0 bilinear is exact and free
#else
        // LEVEL 0 BY POLICY, not by the hardware's own LOD. With the negative layer bias this
        // branch is entered while the unbiased footprint can still be minifying, and the
        // hardware's texture() would then blend the next mip level (and may filter
        // anisotropically) - a read no page stack can reproduce. textureLod(..., 0) is exactly
        // the same read as texture() under true magnification, and is what the page path takes:
        // measured on test.anie as a mean of 0.149 of 255 (max 1 here; max 43 there) until the
        // two branches agreed.
        return textureLod(aTexture, aUv, 0.0);
#endif

    // THE LEVEL IS THE NEAREST one, not the one below: the taps reconstruct the MAJOR axis and the
    // level's own texel has to supply the MINOR one, so the level whose texel matches `minor` is the
    // one to take. `floor` biases that by up to a full level downward (the level is then narrower
    // than the footprint), which leaves the minor axis under-filtered - measured as the residual
    // error tail this read otherwise still has on thin features.
    float level = clamp(floor(footprint.lod + 0.5), 0.0, uLayerMaxLod);
    if (uVtDebugLevel != 0)
        return vec4(level / 16.0, footprint.lod / 16.0, 0.0, 1.0);
    float scale = exp2(level);
#if VIRTUAL_TEXTURE
    vec2 sizeLf = max(vec2(1.0), floor(size0 / scale));
#else
    vec2 sizeLf = vec2(textureSize(aTexture, clamp(int(level), 0, int(uLayerMaxLod))));
#endif
    LayerTaps taps = layerTaps(footprint, level, sizeLf);
    vec2 centres[8];
    layerTapCentres(aUv, footprint, taps, level, sizeLf, centres);
    vec4 acc = vec4(0.0);
    for (int i = 0; i < 8; ++i) {
#if VIRTUAL_TEXTURE
        // THE COARSE TIER IS SAMPLED LIKE THE CONTROL'S, reconstruction included. A level
        // the coarse chain owns (index >= uCoarseLevel) is resident in full, so there is
        // no page to resolve -
        // but returning one bilinear read there ran a DIFFERENT filter from the full
        // texture's two taps, and that difference (not residency, not the table) measured
        // 816 px over 8/255, max 115, on nene's Layer 6 while every page was resident.
        // The tap offsets are level texels either way; only the level index shifts.
        acc += (level >= float(uCoarseLevel))
            ? sampleCoarse(centres[i], level)
            : samplePage(centres[i], level);
#else
        acc += textureLod(aTexture, centres[i], level);
#endif
    }
    return acc / 8.0;
}

#if LAYER_MASK
// THE MATTE'S READ: the SAME reconstruction, its own chain. uLayerMask (unit 3) is the full R8
// chain when the mask is not paged - which is also the completeness binding when it is, so the
// paged branch never samples an unbound unit. A paged mask's coarse chain is sampled clamped
// (GL_CLAMP_TO_EDGE, like its full chain), and a page-gutter texel outside the level repeats the
// edge texel, so `maskRead` is bit-for-bit the full path's read of the same level.
#if MASK_VIRTUAL_TEXTURE
float maskRead(vec2 aUv, float aLevel)
{
    if (aLevel >= float(uMaskCoarseLevel))
        return textureLod(uMaskCoarseTexture, aUv, max(0.0, aLevel - float(uMaskCoarseLevel))).r;

    vec2 sizeL = max(vec2(1.0), floor(uMaskSize / exp2(aLevel)));
    // the tap's texel position, clamped exactly as GL's CLAMP_TO_EDGE clamps the full chain's
    vec2 p = clamp(aUv * sizeL, vec2(0.5), sizeL - vec2(0.5));
    vec2 grid = ceil(sizeL / uPageTexels);
    vec2 page = clamp(floor(p / uPageTexels), vec2(0.0), grid - vec2(1.0));
    vec2 local = p - page * uPageTexels;
    int level = int(aLevel);

    uint entry =
        texelFetch(uMaskPageTable, ivec2(page) + ivec2(0, uMaskPageLevelRow[level]), 0).r;
    if (entry == 0u)
        return textureLod(uMaskCoarseTexture, aUv, max(0.0, aLevel - float(uMaskCoarseLevel))).r;

    int atlas = int(entry >> 8);
    int slot = int(entry & 255u) - 1;
    if (slot < 0 || atlas >= uAtlasCount)
        return textureLod(uMaskCoarseTexture, aUv, max(0.0, aLevel - float(uMaskCoarseLevel))).r;

    vec2 origin = vec2(float(slot % uPageSlotsPerAxis), float(slot / uPageSlotsPerAxis))
        * uPagePitch + uPageGutter;
    vec2 uv = (origin + local) / uPageAtlasSize;
    if (atlas == 0) return textureLod(uMaskAtlas0, uv, 0.0).r;
    if (atlas == 1) return textureLod(uMaskAtlas1, uv, 0.0).r;
    if (atlas == 2) return textureLod(uMaskAtlas2, uv, 0.0).r;
    return textureLod(uMaskAtlas3, uv, 0.0).r;
}
#else
float maskRead(vec2 aUv, float aLevel)
{
    return textureLod(uLayerMask, aUv, aLevel).r;
}
#endif

float sampleMask(vec2 aUv)
{
#if MASK_VIRTUAL_TEXTURE
    vec2 size0 = uMaskSize;
    float maxLod = uMaskMaxLod;
#else
    vec2 size0 = vec2(textureSize(uLayerMask, 0));
    float maxLod = floor(log2(float(max(int(size0.x), int(size0.y)))));
#endif
    LayerFootprint footprint = layerFootprint(aUv, size0);
    if (footprint.lod <= 0.0)
        return maskRead(aUv, 0.0);

    float level = clamp(floor(footprint.lod + 0.5), 0.0, maxLod);
    float scale = exp2(level);
    vec2 sizeLf = max(vec2(1.0), floor(size0 / scale));
    LayerTaps taps = layerTaps(footprint, level, sizeLf);
    vec2 centres[8];
    layerTapCentres(aUv, footprint, taps, level, sizeLf, centres);
    float acc = 0.0;
    for (int i = 0; i < 8; ++i)
        acc += maskRead(centres[i], level);
    return acc / 8.0;
}
#endif
