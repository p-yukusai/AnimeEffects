# How a layer sample is filtered

One question: a screen pixel covers an area of the texture, not a point. How is that area turned into
one colour?

```svg
<svg viewBox="0 0 920 330" xmlns="http://www.w3.org/2000/svg" font-family="sans-serif" font-size="13">
  <defs>
    <marker id="a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>

  <!-- panel 1: the footprint -->
  <text x="14" y="20" font-weight="bold" fill="currentColor">1. what a pixel asks for</text>
  <g stroke="currentColor" stroke-width="1" fill="none" opacity="0.35">
    <rect x="14" y="40" width="240" height="240"/>
    <path d="M14,100 h240 M14,160 h240 M14,220 h240 M94,40 v240 M174,40 v240"/>
  </g>
  <text x="14" y="300" fill="currentColor" opacity="0.7">texture texels</text>
  <polygon points="150,120 250,150 220,235 120,205" fill="rgba(90,150,255,0.35)" stroke="rgba(90,150,255,1)" stroke-width="2"/>
  <text x="150" y="105" fill="rgba(90,150,255,1)">one pixel's footprint</text>
  <path d="M300,180 h60" stroke="currentColor" stroke-width="2" marker-end="url(#a)"/>
  <text x="300" y="168" fill="currentColor" opacity="0.7">≈6×3 texels,</text>
  <text x="300" y="200" fill="currentColor" opacity="0.7">2:1 stretched</text>

  <!-- panel 2: hardware trilinear -->
  <text x="380" y="20" font-weight="bold" fill="currentColor">2. hardware trilinear</text>
  <g stroke="currentColor" stroke-width="1" fill="none" opacity="0.35">
    <rect x="380" y="40" width="200" height="200"/>
    <path d="M380,90 h200 M380,140 h200 M380,190 h200 M430,40 v200 M480,40 v200 M530,40 v200"/>
  </g>
  <rect x="425" y="95" width="110" height="110" fill="rgba(255,190,60,0.18)" stroke="rgba(255,190,60,1)" stroke-dasharray="5 4"/>
  <g fill="rgba(255,190,60,1)">
    <circle cx="455" cy="125" r="5"/><circle cx="505" cy="125" r="5"/>
    <circle cx="455" cy="175" r="5"/><circle cx="505" cy="175" r="5"/>
  </g>
  <text x="380" y="262" fill="currentColor" opacity="0.8">ONE level, chosen from the LONG axis</text>
  <text x="380" y="280" fill="currentColor" opacity="0.8">then 2×2 bilinear inside a square that</text>
  <text x="380" y="298" fill="currentColor" opacity="0.8">does not match the footprint</text>

  <!-- panel 3: this renderer -->
  <text x="620" y="20" font-weight="bold" fill="currentColor">3. this renderer</text>
  <g stroke="currentColor" stroke-width="1" fill="none" opacity="0.35">
    <rect x="620" y="40" width="200" height="200"/>
    <path d="M620,90 h200 M620,140 h200 M620,190 h200 M670,40 v200 M720,40 v200 M770,40 v200"/>
  </g>
  <rect x="650" y="100" width="70" height="80" fill="rgba(120,200,120,0.18)" stroke="rgba(120,200,120,0.9)"/>
  <rect x="720" y="100" width="70" height="80" fill="rgba(120,200,120,0.10)" stroke="rgba(120,200,120,0.9)"/>
  <g fill="rgba(120,200,120,1)">
    <circle cx="671" cy="120" r="5"/><circle cx="699" cy="120" r="5"/>
    <circle cx="671" cy="160" r="5"/><circle cx="699" cy="160" r="5"/>
    <circle cx="741" cy="120" r="5"/><circle cx="769" cy="120" r="5"/>
    <circle cx="741" cy="160" r="5"/><circle cx="769" cy="160" r="5"/>
  </g>
  <text x="620" y="262" fill="currentColor" opacity="0.8">one level, chosen from the SHORT axis;</text>
  <text x="620" y="280" fill="currentColor" opacity="0.8">two 2×2 filtered taps split the LONG axis,</text>
  <text x="620" y="298" fill="currentColor" opacity="0.8">so the taps cover the footprint's length</text>
</svg>
```

| | hardware trilinear | this renderer |
|---|---|---|
| level | one level, from the **longer** axis | one level, from the **shorter** axis |
| taps | 2×2 bilinear | two 2×2 **filtered** taps along the footprint's longer axis |
| span | the level's own texel | each tap half the major extent and the whole minor extent, deconvolved ×0.8 above level 0 |
| sharpness with sub-pixel motion | swings (measured 24 % of the gradient) | constant for an affine draw (identical over a 24-step sweep) |
| cost, 1920×1080 full-screen layer | 0.247 ms | 0.298 ms |
| cost, 3840×2160 full-screen layer | 0.388 ms | 1.191 ms |

The two failure modes of the hardware read are **under-integration** (two bilinear taps do not cover a
6×3-texel footprint, so how much of it lands in the result depends on the sub-pixel phase) and **the
level's own blur** (trilinear picks its level from the long axis, so that level's texel is already
wider than the footprint's short side). This renderer's read takes its level from the **short** axis
and spends every fetch on **one** level: two filtered taps splitting the long axis, so the taps - not
a level blend - supply the coverage. A blend spends half its fetches on a second level, and an
elongated footprint does not need a second level; it needs coverage along its major axis.

The footprint's two axes and its direction come from the 2×2 Jacobian of the texel-space uv (the
screen derivatives): its singular values are `major` and `minor`, its major eigenvector is the axis the
taps follow, and `lod = log2(minor) + uLayerLodBias`. Measured over **352 matrix cells** (8 artworks ×
4 zooms × 11 footprint ratios) plus an **isotropic corpus of 48 cells**, against the two-level read it
replaces:

- **anisotropic footprints, 2:1 to 8:1**: median `err_mean` **0.485** against **3.735**, lower in
  **12 of 12** cells;
- **near-isotropic, 1.3:1**: **23 of 24** cells, median difference **-0.1496** [CI -0.2404, -0.0675];
- **exactly isotropic**: a wash - **13 of 24** cells, CI crossing zero;
- **magnified**: bit-identical to the old sampler in **14 of 14** cells.

Cost: **eight fetches**, unchanged from the read it replaces - two levels × one 2×2 tap became one
level × two 2×2 taps. The covariance adds a few scalar ops. The two-level blend and the
`DERIVED_FOOTPRINT` variation are gone; the footprint is always the draw's own Jacobian.

The sampler lives in `assets/shaders/LayerSampling.glsl` and is included by the layer draw, the
clipper's singulation pass and the mask rubylith overlay. A clipper's mask used to be the
hardware's implicit-LOD read while the layer it clipped used this one - two filters at every
clipped edge, and a read no page stack could reproduce; the shared include is what lets clipper
layers be paged exactly. `sampleMask` is the same footprint/level rule and the same eight taps for
a node's own matte, with the mask's uv and chain: the old `texture(uLayerMask, ...)` was the same
kind of implicit read, so the full mask path changed with the paged one. Its border is
`CLAMP_TO_EDGE` (the mask chain's), and the paged form's gutter repeats the level's edge texel to
match - see `MaskPyramid::fillPage`.

## Anisotropy

- **The mip chain is isotropic.** It is a box-filtered pyramid, one level per halving, premultiplied
  so that colour and coverage average together. There is no anisotropic mipmap format - anisotropy is
  a *sampling* technique, not a chain.
- **The sampler asks for anisotropy 16** (`ImageKey::layerMaxAnisotropy`), which matters to any
  implicit-LOD read on these textures.
- **This read does not use it, and cannot**: it reads with `textureLod`, which is explicit and
  therefore never anisotropic, and one scalar level serves both axes. The `lod <= 0` branch is
  explicit too (`textureLod(..., 0)`): the branch is entered on the BIASED level, so the unbiased
  footprint can still be minifying, and an implicit `texture()` there would use the hardware's own
  LOD - a mip blend plus anisotropy the paged path cannot reproduce. Measured as a whole-layer
  mean of 0.149 of 255 (max 43) on test.anie until the branch was made explicit; under true
  magnification the two reads are identical, which is the 14/14 result above.
- **The tap geometry is still isotropic.** The taps follow the major axis, but there is no
  anisotropic mip chain, so a footprint stretched 4:1 is still over-blurred along its short axis by
  the ratio, as trilinear is. From `ImageKey.h`, error in 255ths against the exact area integral:

  | footprint | aniso 1 | aniso 4 | aniso 16 |
  |---|---|---|---|
  | 4:1 | 4.443 | 1.117 | 1.117 |
  | 8:1 | 6.971 | 1.838 | 0.741 |
  | 16:1 | 12.683 | 3.803 | 0.539 |

  The gain saturates at the footprint's own ratio, so 16 is an upper bound rather than a cost.

The read spends two taps along the major axis where hardware anisotropic filtering spends
`ceil(ratio)`; the short axis is bounded by the isotropic chain. That is where the remaining error on
squashed layers lives.
