# verify_mipmap — mipmap verification probe

Verifies that layer artwork textures are mipmapped **correctly**, against the two things a mip chain
can get wrong:

1. **the data** — it must be alpha-premultiplied, or every minified level drags the RGB stored under
   `alpha = 0` into the silhouette (the classic mipmap dark halo, and its mirror, the light halo);
2. **the geometry** — the artwork must start at the texture's texel (0, 0) and the texture must be the
   content's own size, because level *l* is a grid of 2^*l*-texel boxes anchored to multiples of
   2^*l* **in the texture**. An import ring or an alignment pad moves level 0's texels off that grid,
   and a minified pixel is then answered by a box twice as wide as the level asked for.

## Run

Registered as a CMake target in `tests/CMakeLists.txt`, so it is part of the standard suite:

```sh
./scripts/run_suites.sh Debug                                  # includes verify_mipmap
QT_QPA_PLATFORM=offscreen ./build/tests/Debug/verify_mipmap    # on its own
```

Built by CMake rather than hand-rolled in `tools/` on purpose: `tools/verify_blur` rotted into not
compiling at all (it still references `Project::Attribute::setMaxFrame`, `Exporter::CommonParam::frame`
and `TimeInfo::frameMax`, all removed since) precisely because nothing kept building it. A registered
target cannot drift out of the build.

Offscreen GL is enough here. Exit code: 0 = all pass, 1 = test failures, 2 = GL bootstrap failure. GL
objects are freed by `clearRenderFixtures()` while the context is still current — a static destructor
runs after the context is gone and crashes in `glDeleteFramebuffers`, which this harness originally
did.

## Why the ground truth is analytic here

There is no third-party author to diff against: Krita and GIMP (which author the `tests/golden`
PSD/ORA goldens) do not do GL mip selection, so they cannot say what mip level *l* of a given input
should contain. The content of a mip level is instead **defined** — it is the area-weighted average of
the level above over that texel's footprint — so the probe computes it directly, in both
representations, and compares. The composited consequence is likewise closed-form: over an opaque
black backdrop a correct chain shows `source * coverage`, and a chain that keeps straight data shows
`source * coverage * coverage`.

## Suites

- **M1 — the generated chain vs the analytic CPU reference.** Two patterns (a bright shape on
  transparent black, and a dark shape on transparent white — the two halo directions), each as a
  checkerboard and as an inset square. For every case the probe computes, on the CPU and independently
  of the product's helpers, what averaging *straight* data yields and what averaging *premultiplied*
  data yields, then asserts the GPU chain is the premultiplied one, that the composited result over an
  opaque black backdrop equals `source_color * coverage`, and that the level count runs down to 1x1.
- **M2 — the production `core::ImageKey` texture.** Builds a real `img::ResourceNode` carrying a
  129x77 image (odd width, no power of two in either side) the way the loaders do, lets `ImageKey`
  build its texture, and asserts it is **the content's own size** (an alignment pad would have made it
  256x128), the level count and the sampling parameters, that every level is the area average of the
  premultiplied content, and that level 1 is the premultiplied chain *and not* the straight one — on
  the pattern where the two differ (transparent texels carrying white; with transparent black they are
  numerically identical, so a bright shape on transparent black cannot see a missing premultiply at
  all).
- **M18 — every level, every size, odd sides included.** Sizes 64, 100, 129, 300, 384, 501 and 513,
  against the area average of the level-0 upload, texel by texel. `gl::Texture::create` builds
  levels 1..3 from level 0 (exact for any size, odd sides included) and level 4+ from level - 3, an
  8:1 step whose source texels are themselves averages; the probe asserts exactness on the
  base-sourced levels and the documented bound (8/255, from the 50-sample corpus in
  `tools/mip_compare`) on the chained tail.
  Plus the **mutation control**: the same step swept through a 384-texel side, level 8, read out of a
  RAW `glGenerateMipmap` chain (plain GL, no tail builder) must NOT be the box of its own footprint —
  this driver weights rows [128, 256), the middle third, and none of the rest. Without that control
  the exactness assertions would not be shown to have power. Flat content is checked too: a uniform
  region must reduce to itself, gaining no halo from the level's shape.
- **M3 — the production RENDER path.** Builds a real `core::ObjectTree` (a folder holding one layer
  with a real `ImageKey`) and renders it offscreen through the same wiring `MainDisplayWidget::paintGL`
  uses, over an opaque black backdrop, with the layer at half scale so the sampler reads mip level 1.
  Asserts the rendered silhouette equals `source * coverage` and is *not* the haloed value, and that it
  matches the same content prefiltered by hand. Then the **UV path at 1:1**: a 1-texel checkerboard
  drawn at scale 1.0 must come back at full contrast (255 of 255), because a bilinear filter returns a
  texel unchanged only when the sample sits on its centre — so a padded texture, an import ring or a
  `uImageSize` that disagrees with the texture shows up here as a collapse of contrast.

Not ported from the reference branch: its M4–M17, M19, M20, M21, the corpus evaluation and the figure
scripts (blend-mode sweep, feature paths, level sweep, LOD uniformity, export-like configuration, the
exporter, the texel phase sweep, the 2x mechanism, the filter-shape impulse, the margin parity/depth
measurements, the overlay path, the anisotropy and LOD-bias measurements). Those live in that branch's
history; this tree's gate is the four suites above.

## What was removed, and the measurements behind it

**The 1-texel import ring.** `img::Util::prepareForSampling` (formerly
`recreateForBiLinearSampling`) used to copy the artwork into a `(w+2)x(h+2)` block at texel (1, 1)
with a transparent ring, a pre-`GL_CLAMP_TO_BORDER` device for soft edges. The ring never was part of
the content rect — the importer paired it with a rect at `(-1, -1)`, so the content always landed at
the same document coordinates — but it put the artwork off the mip chain's level grids. Measured in
the reference branch: a 1-texel margin costs **112 of 255 at 2x**; a margin of 8 is aligned through
level 3 and then **111 of 255 off at 16x** (worst) on random artwork; **no ring at all is exact at
every level (worst 1.3, mean 0.66 of 255)**. The transparent neighbour a bilinear read needs at the
artwork's edge comes from the sampler instead: the layer texture is created with
`GL_CLAMP_TO_BORDER` and a transparent border colour, which the hardware applies to every mip level.

**The size padding.** The image was uploaded padded on the right and bottom up to a multiple of the
largest power of two that fits in the side (capped at 128), so `glGenerateMipmap` could generate every
level. That is a memory cost, not a correctness one: aligning a 144-texel side rounds it to 256
(+78%), and a side just above a multiple of 128 costs up to +98%. It is gone; `gl::Texture::create`
builds the levels `glGenerateMipmap` cannot be trusted for, on the GPU, with the area weights the
level grids imply. `uImageSize` is therefore the content's size, which is also what keeps the mesh's
content-pixel texture coordinates (§`core::GridMesh`) and the texture in the same units.

**The CPU mip rebuild.** The reference branch read the deepest exact level back with `glGetTexImage`
and resampled it per level on the CPU. That cannot serve a size where nothing halves: a 15001² image
would read back its whole 900 MB base, and a 1023² one would do it for each of its ten levels. The tail
is built by a shader instead (no readback, no sync), sampling the level-0 upload while the footprint is
short and the level above (a 2:1–3:1 step) beyond that.

What the tail builder fixed, measured on the reference driver by M18's mutation control: that driver
fills a level whose source does not halve with a **band** of it — level 8 of a 384-texel side is the
average of rows [128, 256), the middle third, for a worst deviation of **-83.7 of 255**; the deepest
level of a 1920-texel side weights rows [896, 1024), 7% of the side, for **-117.5**; and the first
level past a power of two on 1920 is +16.9 off. Those are the numbers the assertions above fail on if
the builder ever goes back to leaving those levels to `glGenerateMipmap`.

## What this gate cannot see

- **Sampling policy.** The fixtures pin `GL_TEXTURE_LOD_BIAS` to 0 and anisotropy to 1, because what
  they assert is which level a footprint reads and what kernel that level applies; the shipped -0.5
  bias and the anisotropy are policies on top of that, measured elsewhere in the reference branch
  (M17/M19) and documented on `core::ImageKey::layerLodBias` / `layerMaxAnisotropy`.
- **Real artwork.** The corpus evaluation and the figure scripts that judged the shipped chain against
  a supersampled truth over 116 files are in the reference branch, not here. This gate is analytic and
  synthetic by design: it can say a level *is* the box it claims to be, which a photo corpus cannot.
