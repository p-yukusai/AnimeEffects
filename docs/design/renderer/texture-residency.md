# Texture residency, deformation and the frame's cost model

**Status: design, written 2026-11-01 from measurements on this tree.** It exists so the next
session does not have to rediscover the argument. Numbers marked **[M]** were measured by
`gl::FrameStats` on this tree; **[D]** is arithmetic from measured inputs; **[O]** is open.

Companions in the build today: `src/gl/FrameStats.{h,cpp}` (the frame line), the density meter in
`core/MeshTransformer.cpp`, `tests/density_meter.cpp`, `scripts/gui/probe_camera_transform.sh`.

---

## 1. What the renderer does today

A painted frame: `ObjectTree::render` → `prerender` every node (skin) → sort by depth → draw every
visible layer into the frame FBO. A filtered layer isolates into a `FilterFrame` slot **at device
size** and runs the stack there; a non-Normal blend mode captures the destination first; clippers are
singulated into `ClippingFrame` and the clipped layers draw through that mask.

Textures: **one dense `GL_TEXTURE_2D` per image key, base level only** (`F_MAKE_MIPMAP 0`), RGBA8,
mutable `glTexImage2D`. Masks are `GL_R8` at the content's own resolution (2026-11-01). No screen
bounds test exists anywhere: the only culls are `!mIsVisible` and blended opacity 0.

Deformation: FFD/bone **key values blend on the CPU** into `expans.ffd().positions()`; one
transform-feedback pass per layer per painted frame applies SRT + dual-quaternion skinning
(`MeshTransformVert.glsl`), and the visible draw adds only `uViewMatrix`. Since 2026-11-01 that pass
is cached on `TimeKeyExpans::revision()`: a camera-only frame re-skins nothing **[M: 11 → 0 passes
for a pan on the small project; 128 layers on the big one]**. Playback still re-skins every visible
layer every frame, by definition.

Measured on `nene.anie` (15000x15000, 133 layers, 68 folders) at a 1364x717 view **[M]**:

| | |
|---|---|
| layers considered / drawn / culled | 130 / 128 / 2 (the 2 are the opacity test) |
| fill (sum of clipped screen quads) | 1,038,179 px |
| resident textures | 2,088,114,920 B = **1.94 GiB** (peak = live) |
| process RSS | 7.6 GiB (the decoded layer images) |

Small project, same view **[M]**: 11 layers, fill 259,122 px, 19.8 MB of textures.

Deformation density on `test.anie` during playback **[M]** (see §3 for the metric): 46.6 % of the UV
area at density 1.0-1.41 (undeformed region), a fat tail, maximum 105-220x. That tail is real, not a
meter artefact: the trace prints the same triangles' raw areas, and frame 50 renders identically on
`3cb05a2c` (before the recovered work) and on this tree.

## 2. The problem, stated exactly

Residency is **per layer, per base level, for the whole session**: every image key that has been
drawn keeps its full texture (`ImageKey::Cache`, no eviction). So VRAM tracks the *document*, while a
view can only ever display its own pixel count.

The corollary that decides the design: **a viewport samples at most `view width x height` texels per
layer**, no matter how large the layer is. At 1:1 a 1364x717 view shows 1364x717 content pixels. The
900 MB a 15000² layer's base level costs is memory no view can consume at once.

Two consequences:

1. **Memory**: the fix is residency (what is resident, not what exists).
2. **Quality**: with no mip chain at all, minification at zoom-out is a point-sampled
   `GL_LINEAR` fetch from a huge texture - aliasing plus cache thrash. Mips fix the *sampling*; they
   add 33 % to whatever is resident and do nothing for §2.1.

Masks are now full content resolution too (`core/MaskKey`, `core/FilterMask`), 225 MB for a 15000²
mask **[D: 15000² x 1 B]**. They are painted, so their pyramid must be maintained incrementally.

## 3. The metric: what a view needs

The level a region needs is not a function of the camera alone: vertices move, UVs do not, so density
varies across a layer. Per triangle:

```
J            = d(screen or mesh) / d(uv)          the 2x2 Jacobian
s_max, s_min = its singular values
density      = area(mesh) / area(uv) = s_max * s_min      (what is measured today)
anisotropy   = s_max / s_min
level        = L_screen + ceil(log2(s_max))       per region
```

**Anisotropy [O]**: the histogram carries `det(J)` only, i.e. the *product* - a 4:1 anisotropic
triangle with the same area needs one level sharper along its major axis than `det` implies
(`s_max = sqrt(det * ratio)`). Two consequences: the store must carry `s_max` (and ideally the ratio)
per triangle, and the sampler must use `GL_TEXTURE_MAX_ANISOTROPY_EXT` where available. Neither is in
the engine today **[M: no anisotropy occurrence in src/]**; the meter's next revision should emit
`dens_smax` bins and an anisotropy histogram (the same reduction, one more attachment).

**The statistics that matter** (measured on a real project, §1): the max is not a policy input (200x
-> 8 levels, and it is one sliver-ish boundary triangle), the mean hides the tail, and the
**area-weighted histogram** is the honest object. A whole-layer `p95` would leave ~5 % of the area up
to 4 levels coarse - visible on a small, genuinely stretched region (an eye highlight, a strap under
a pose). That is why the answer is per-region (pages), not a percentile on a whole layer; a
whole-layer fallback should be chosen from the histogram *with a cap* and its blur reported.

**Measurement**: per frame, in the skinning pass (the deformed positions are there), a geometry
shader draws the mesh once more into a 16x1 R32F target - additive blending sums UV areas per
`log2(density)` bin, a second attachment keeps the true max - read back two frames later through
ping-pong PBOs. Zero GL objects, draws or readbacks when `AE_FRAME_STATS` is unset. Slivers
(< 0.25 px² UV) are dropped (never clamped) and counted in the trace.

*Why the PBO, when the payload is 128 bytes*: `glReadPixels` without one **synchronises the
pipeline** - the CPU waits for every queued command, i.e. a frame of GPU work, to read a value the
frame does not need. The cost is the sync, not the bytes. (Worth measuring both ways once; the
`ms` field is the judge.)

## 4. The pyramid (authoring data, not runtime derivation)

- **Scope**: one pyramid per **image resource**, not per key - `ImageKey::serialize` stores only a
  resource id, so every layer using the same image shares one.
- **Storage: every chain is stored, without exception.** Images and masks, imported or painted in
  the app - what is in memory at save time is what the file holds, and a load decodes and uploads it
  with no GPU downsample at all. "Once per image" therefore means "once, ever": the cost lands on
  the import (or on the stroke that last changed it), never on an open, and the pixels are the ones
  the authoring session produced rather than whatever the driver's shader arithmetic yields today.
  Mutability is a separate axis: a mask being painted updates the boxes its stroke touched, in
  memory and in the file at the next save.
  The GPU downsample path therefore exists for exactly two cases, and neither is a load of a current
  file: (a) a legacy project with no stored chain (built once at load, after which it is authoring
  data), and (b) the stroke's dirty boxes while painting.
  Format note: masks were never released, so their chain needs no compatibility path; image chain
  chunks must be optional and version-gated, since images ARE released.
- **RAM**: decoded at load into RAM - all levels, coarse ones cost 1/3 of the base - so that no
  frame ever decodes. `nene` goes from ~1.94 GiB to ~2.6 GiB of decoded images **[D]**.
- **Content rules**: the `feat/mipmapping` branch's - premultiplied, halo-free, levels past the
  alignment cap rebuilt on the CPU, `GL_TEXTURE_LOD_BIAS = -0.5` as a policy on top.
- **Masks get mips too, for the same reason images do** - they are sampled minified exactly like a
  layer, so a mask without a chain aliases at fit zoom. And like an image, a mask is often IMPORTED
  (a PSD mask arrives through `ImageFileLoader::attachMask`), so the default case is identical: the
  chain is built once at import or creation, stored with the field, and decoded at load.
  The one difference is that a mask is not immutable: the first stroke makes it mutable, and from
  then on its chain is updated **incrementally from the dirty box**, level by level (each level's
  box halves) - never regenerated whole. So "built once" for a mask means "until the first stroke",
  and the incremental path is a continuation, not a separate mechanism.
  Deferring mask chains was a staging choice that cost real quality in the meantime; they belong in
  the same stage as the image chain, immediately after the import fix below, not in a later one.
- **The importer's 1-texel ring is GONE (done).** It copied the artwork at texel (1,1) with a
  transparent border, so every level from 1 down averaged across the content's own 2x2 blocks with a
  half-texel phase error (the branch measured 112/255 at 2x). `prepareForSampling` now copies the
  image size for size at (0,0), `mipAlignedTextureSize` and the 128-texel alignment are deleted, the
  upload is the content size, and `uImageSize` is that same size - so content texel (x,y) is texture
  texel (x,y), pinned by the probe's 1:1 checkerboard.
- **Resolution rule: ceil-div, NO padding, and the chain is built on the GPU.** GL's mip level sizes
  are `max(1, side >> l)`, so a size with no trailing zero bits makes level 1 already non-halving
  (a 15001^2 base, or every level of a 1023^2 one) - the case that decides the design, because a CPU
  rebuild there reads back the whole base. Levels are therefore built by a GPU area pass:
  - **the reach-back rule** (`gl::Texture::mipSourceLevel`, `kMipChainSpan = 3`): levels 1..3 are
    built from level 0 - exact for any size, odd included, three base passes - and level `l >= 4`
    from level `l - 3`, an 8:1 step whose source texels are themselves averages. Three levels back
    and not one, because a chained level's error is its SOURCE's error averaged over a wider box, so
    a one-level reach keeps the source's spikes.
  - **the study that chose 3** (`tools/mip_compare`): 50 files - photographs, illustrations, PSD
    composites, screenshots and text pages, flat-colour art, sprite sheets - each level built five
    ways (`exact` from level 0, `chain1`/`chain2`/`chain3` from level - 1/- 2/- 3, `bilinear` as a
    2-tap driver-style chain), the four non-reference ones measured against an exact area average of
    the premultiplied base in 8-bit code values. 45 produced metrics; 5 were dropped (one CMYK PSD
    and four files Qt cannot decode). Worst per-texel deviation per level band, `max / mean` in
    255ths:

    | policy | 1-2 | 3-4 | 5-6 | 7-8 | 9+ | worst sample |
    |---|---|---|---|---|---|---|
    | `chain1` (level - 1) | 53 / 0.47 | 65 / 3.09 | 52 / 2.42 | 38 / 1.28 | 38 / 10.67 | 65 (mean 5.16) |
    | `chain2` (level - 2) | 0 / 0.00 | 27 / 0.06 | 16 / 0.30 | 13 / 1.66 | 13 / 3.36 | 27 (mean 0.06 at that level) |
    | `chain3` (level - 3) | 0 / 0.00 | 8 / 0.12 | 7 / 0.03 | 7 / 0.32 | 3 / 0.97 | 8 (mean 0.34) |
    | `bilinear` (2-tap) | 128 / 0.61 | 178 / 1.38 | 143 / 0.75 | 188 / 16.44 | 182 / 123.50 | 188 |

    `exact` is the reference every column is measured against, so it is 0 by construction. Cost, in
    base reads: 1.33 for `chain1`, 3.33 for the shipped rule (levels 1-3 from the base, then base/4
    + base/16 + ...), 8.33 for the superseded rule that read the base through level 7.
  - **what the eye saw** decides it: contact sheets, x8-amplified delta maps and level images
    upscaled x4 nearest, at levels 2, 4 and 7, make `exact`/`chain1`/`chain2`/`chain3`
    indistinguishable; `chain1`'s delta map lights up broadly on flat-colour artwork and on text,
    `chain3`'s is almost black, and `bilinear` is visibly wrong. The criterion for the rule is
    "looks exact", and reaching back 3 meets it.
  Masks follow the same rule for a whole-field build; their per-stroke update takes the deepest
  exact level instead, because the box is box-sized and exactness is then nearly free.
  Re-running the study: `./build/tests/Debug/mip_compare <image> <outdir>` per sample (no GL
  context needed; it writes `metrics.json` and one PNG per level per policy), once per path in
  `tools/mip_compare/samples.txt`, whose header records how the 50 were selected.
  No readback, no sync, no per-upload CPU work; the pass saves and restores the full GL state
  (FBOs, viewport, VAO, program, blend/depth/scissor/cull) because it runs inside the caller's draw.
- **Open [O]**: premultiplied vs straight for the *stored* levels needs the written analysis against
  the branch's halo evidence, the blend path (`img::Util::makeTextureUpload` premultiplies today),
  the storage size and the export path.

## 5. The page pool, in detail

### 5.1 Units, addressing, table

- A resource is one image (a layer's `ImageKey` resource, or a mask field). Its levels come from the
  pyramid (§4). A **page** is 256x256 texels of one level.
- The pool is a **growable set of atlases**, not one fixed texture: atlases of 4096x4096 (16x16 = 256
  slots of 256^2) per format - RGBA8 for images, R8 for masks - allocated in steps as demand exceeds
  capacity, and released when the budget says so. A fixed single atlas is wrong for this engine: a
  4K viewport needs ~32 MB of RGBA pages at 1:1 **[D: 3840x2160 x 4 B]**, an 8K export target needs
  ~132 MB, and the GPUs in use differ by an order of magnitude in VRAM.
  Pages sit 4 texels apart and the gutter repeats the edge texel, so a bilinear tap inside a page can
  never reach a neighbour's content.
- A slot is therefore `(atlas, index)`: the table entry carries the atlas index and the slot index, or
  a flat page id that the pool resolves. `R16UI` still holds it (4 bits atlas, 8 bits slot, a valid
  bit).
- **Why separate 4096² textures rather than one bigger one** - measured reasoning, not taste:
  - *Resizing* costs an allocation of the new size PLUS a copy of the old contents (no
    `glCopyImageSubData` at GL 4.0: an FBO blit or a re-upload), with both textures live at the peak,
    i.e. 5x the old size momentarily, and every table entry rewritten. Adding an atlas costs one
    allocation and nothing else.
  - A single huge texture (16384² RGBA = 1 GB) is far more likely to be refused or to fragment VRAM,
    and `GL_MAX_TEXTURE_SIZE` is 8192/16384 on much of the hardware this must run on.
  - Sampling does not care how large the texture is; the differences are binds (one unit per atlas,
    cached per resource) and the shader's atlas selection - a dynamically uniform sampler index (legal
    from GLSL 4.00, our floor) or a short if-chain for <= 4 atlases.
  - Only whole atlases can be released when idle (the shrink of §5.5); one big texture cannot be
    partially freed.
  - A `GL_TEXTURE_2D_ARRAY` of 4096² layers is the named-and-rejected alternative: one bind and one
    sampler, but its depth is fixed at allocation, so growth is the same new-array-plus-copy trade.
  - The one thing to A/B rather than argue: `ms` with 1 atlas vs 3 on the same scene, once the page
    path exists - expected within noise, cheap to prove.
- **Page table**: per resource, an `R16UI` texture of `(levels, pagesX, pagesY)` mapping a page to a
  slot, with 0 meaning "not resident". A 15000² resource at 256² pages is 59x59 at level 0 and a
  handful at each level below - the whole table is tens of KB, not MB **[D]**.
- The shader replaces one `texture()` with: fragment -> content px (the existing `uScreenToLayer`) ->
  the level from a per-vertex Jacobian varying -> page coords -> table fetch -> pool fetch. Two extra
  fetches. Their cost is measured before anything ships (a dedicated `ms` A/B on the big project).

### 5.2 Demand is ANALYTIC, not sampled feedback

The density pass already walks every triangle of every drawn layer with the deformed positions. The
same data yields, per triangle, the level it needs and the page rectangle it covers; their union per
resource is this frame's demand.

- **No feedback pass, no lag.** Demand is exact for the frame being drawn, so nothing has to be
  conservative "because the answer is two frames old".
- **No GL 4.2 dependency.** Sampled feedback wants image load/store; analytic demand does not, and
  this engine targets `QOpenGLFunctions_4_0_Core`.
- The error is one-sided: a page fully occluded by another layer still counts as demanded. That costs
  a bounded over-fetch (measured with the counter), never a hole.
- Feedback remains a possible *later* optimisation to trim over-fetch; it is not on the path.

### 5.3 No holes, no pop-out: the always-resident coarse chain

Every resource keeps its **coarse chain resident and complete** (all levels at or below a size where
one level costs a few pages). A page that is not resident samples the coarse chain: **blur, never a
hole**. A page arriving is a quality *increase*, which is not a pop; the thing that would pop - a
resident page vanishing while still sampled - is prevented by 5.4.

### 5.4 Eviction, budget, and what staleness actually costs

- Capacity is pool slots, per format. Priority to keep: the coarse chain > pages demanded in this
  frame > pages demanded within the last N frames (N = 3) > everything else, oldest first.
- Demand is analytic, so "last demanded" is exact; the N-frame window exists only so that a one-frame
  occlusion or a jitter at a boundary cannot evict a page that is about to be sampled again.
- Under pressure the **sharpest, oldest** pages go: the result is local blur, bounded by the budget,
  never a hole and never a colour pop.
- A 10x zoom in one frame: demand is exact for that frame, so the top-priority upload starts
  immediately; until it lands the coarse chain is sampled (blur), and the per-frame budget bounds the
  landing rate. No N-frame rule, no oscillation.
- Eviction is capacity-driven, never change-driven. The cost asymmetry is the policy: a sharp page can
  only come back from RAM (an upload), a coarse page regenerates from a resident sharper one at
  almost no cost - so under pressure sharp pages are the ones worth protecting *only while demanded*.

### 5.5 Pool growth, budget and hardware variance

- **Budget**: one number, in bytes, enforced across formats. Default derived from the hardware where
  it can be read (`GL_MAX_TEXTURE_SIZE`, the renderer string) and, more usefully, **configurable** -
  the same project must run on a 2 GB and a 24 GB card, and the user is the one who knows.
- **Growth**: when demand exceeds capacity and the budget allows, allocate another atlas (a step of
  64 MB for RGBA8, 16 MB for R8) and add it to the free list. Growth is bounded by the budget, never
  by the driver: an allocation that fails with `GL_OUT_OF_MEMORY` is caught, the last atlas is
  dropped and the budget is lowered for the session - the frame degrades to blur, it never crashes.
- **Shrink**: an atlas with no demanded pages for a sustained period is released (its slots evicted
  first). A window that is resized smaller, or a project closed, returns memory without a restart.
- **Both paths share it**: the exporter is the same paged renderer with a policy object -
  `demand = synchronous`, `budget = export budget`, `target = output size` - not a second
  implementation. The viewport's policy is `demand = analytic/async`, `budget = viewport budget`.
  That is the whole difference; a project export renders at a different resolution, and its demand
  follows its target, not the canvas.

### 5.6 Where a new page goes, and how the frame stays consistent

Allocation is a slot in the pool's fixed grid; there is no size fragmentation because every page is
the same size.

1. **Free list first**: slots never used since the pool was created, then slots freed by eviction.
2. **Otherwise a victim**: the eviction order of §5.4 picks one; its table entry is cleared and the
   slot is reused.
3. **Write the texels**: `glTexSubImage2D` of the page's 256x256 from the RAM pyramid into the slot's
   rect **including its 4-texel gutter** (the gutter is filled from the same level's neighbours, so a
   bilinear tap at a page edge cannot bleed). Unpadded rows are handled the way the mask uploads do it
   (`GL_UNPACK_ROW_LENGTH`, `GL_UNPACK_ALIGNMENT 1`).
4. **Publish**: the resource's table texel `(level, px, py)` = slot, written after the texels, both
   before the frame's draws - GL executes in order, so the same frame samples the page it just
   uploaded. A page that is already resident is a no-op (the table already names a slot).
5. **Budget**: the queue is sorted by (deficit levels x visible area) and drained until the frame's
   byte budget is spent; the rest waits, and the coarse tier covers the wait.

The bookkeeping is one map per pool, slot -> (resource, level, x, y); it is what eviction and the
table both update, and it is what a fixture asserts (allocate, evict, reuse: the texel at the slot's
origin equals the page's origin, and the table names the slot).

**Two tiers, so the fallback is never evicted**: the always-resident coarse chain of §5.3 lives in a
*pinned* region of the same atlas (its slots are never in the free list and never victimised), while
the pool proper holds sharp pages. That makes "no hole" structural rather than a policy: the fallback
is resident by construction. Its cost is the coarse levels themselves - at a 512 px threshold, 1.33 x
512² x 4 B = 1.4 MB per resource **[D]**; a 256 px threshold costs 0.35 MB and is the knob for
projects with many resources.

### 5.7 What can regress, and the gates that catch it

The page path is shipped behind a switch, and the switch exists **only** as the control for these
gates and for A/B measurement - not as a user-facing fallback. Its removal criterion is stated in §6:
when every gate is green on every project and the counter shows no regression, the switch is deleted.

| regression | gate |
|---|---|
| a wrong pixel vs the full-resolution path | pixel equality against the control at a matrix of zooms and poses, including the extreme FFD/bone case from §1 |
| a hole (a sample with no resident level) | assert every sample resolves through the chain; a sentinel in the table must never reach the shader |
| a pop-out (resident page evicted while sampled) | a zoom/pan sequence asserts residency never drops a page demanded within N frames |
| upload spikes | `pages=` shows uploaded bytes per frame <= the cap, on the big project while playing |
| shader cost | `ms` A/B, page path vs control, same scene and view |
| editing a mask | the page path's result equals the control within the same frame budget, from the dirty box only |
| export | export reads the RAM pyramid and is independent of pool state (a gate with the pool emptied) |
| two formats in one frame | a layer and a mask painted in the same frame, both paged |

### 5.8 Risks and unknowns

- **Shader addressing cost**: two extra fetches per fragment; measure before committing to the design.
- **Trilinear across levels**: bilinear inside a page is the gutter's job; sampling between two levels
  needs both tables - the coarse chain covers it, at the cost of a hard transition at the boundary.
- **Table size for 15000²**: small, but the *page count* at level 0 is 3481 pages per resource, more
  than one pool: the budget and the coarse chain are what make that fine.
- **Anisotropy** (§3): page level from `s_max`; the sampler needs `GL_TEXTURE_MAX_ANISOTROPY_EXT`
  where available, and the gutter widens if it is used.

## 6. Staged path

- **Stage 1 - sampling foundations** (low risk, immediate quality): a real mip chain per texture
  with the branch's rules, correct MAX_LEVEL bookkeeping, `GL_TEXTURE_LOD_BIAS = -0.5`, and
  anisotropic filtering where the extension exists. Adds 33 % to resident memory; removes the
  minification aliasing. Gate: `verify_mipmap` (exists) plus an anisotropy case.
- **Stage 1.5 - the ring and mask chains**: drop the 1-texel import ring in favour of the border
  colour, fix `uImageSize` and the content rects, and give masks a chain maintained from the stroke's
  dirty box. Gate: `verify_mipmap` ported as a build target (its M1/M3/M17/M20 already measure
  exactly this), plus a mask-stroke case that asserts the chain tracks the paint.
- **Stage 2 - the pyramid: MEASURED AND DROPPED (2026-11-01).** The premise was that persisting the
  levels would save the chain build at load. It does not pay. Real projects, `roundtrip` in Release
  under Xvfb (llvmpipe, so the GPU side is if anything overstated): a 54 MB project loads in 16.7 s -
  resources decode 0.62 s, object-tree deserialize 5.0 s, solve+upload 0.45 s, twice (load and reload).
  A 3.6 MB project: 4.9 s, the same shape (object tree 0.63 s). With `buildMipTailOnGpu` compiled out the
  load moved by 0.03 s and 0.14 s: the chain is **< 1 % of load**. Storing the levels would add 33 %
  more pixels to compress, write, decode and upload, to avoid a build that costs less than decoding that
  copy would. So no stored pyramid, and no format bump for one.
  **Where that difference actually sits** (measured, because "the chain looks blurrier" is the report
  it produces): away from a discontinuity the chained level is bit-identical to the exact one - mean
  delta 0.017/255 and high-frequency energy -0.08 % on a chart, +0.01 % on a line drawing. Every texel
  ADJACENT to an edge moves by ~4.9/255, because the coarse texel straddles the edge and its average -
  part one side, part the other - is handed over whole. The visible loss is therefore the share of edge
  texels times that delta: 12.5 % of texels and -10.6 % high-frequency energy on a chart, 3.5 % and
  -0.8 % on a line drawing, 0 % on a smooth ramp. That is why line art and text read as globally
  softer while a photograph barely changes, and why the artefact looks global even though every texel
  that moves is at an edge. The EXACT level does not get softer on non-dividing sizes: its box is 2^L
  wide either way (measured high-frequency energy of exact level 2 from 1024/1023/1000/1001 crops of
  one image: 1.616 / 1.624 / 1.666 / 1.659 - content, not divisibility).

  **Divisibility is the switch, not the cause** - and it is why 04-AUTUMN.psd (1938x2531) is the
  corpus's worst offender at level 2. Its step-by-step chain, per level, against whether that step
  tiles (prev/target exactly 2):

  | level | size | step | tiles? | max delta | texels over 2/255 |
  |---|---|---|---|---|---|
  | 1 | 969x1265 | from level 0 | - | 0 | 0.00 % |
  | 2 | 484x632 | 2.0021 x 2.0016 | no | **39** | **4.82 %** |
  | 3 | 242x316 | 2.0000 x 2.0000 | yes | 19 | 3.13 % (inherited) |
  | 4 | 121x158 | 2.0000 x 2.0000 | yes | 6 | 0.38 % (decayed) |
  | 5 | 60x79 | 2.0167 x 2.0000 | no | **44** | **15.30 %** |
  | 6 | 30x39 | 2.0000 x 2.0256 | no | 24 | 20.77 % |

  A cutting step takes half of a source texel's *mixture* (part line, part background) where the exact
  level splits it correctly: the filter does not widen, one texel of a two-texel-wide line is replaced
  by that line's average with its neighbour. Cores stay (the darkest 1 % is identical), shoulders go 2
  lighter and single contour texels move up to 39/255 - which is why a drawing reads as globally softer
  while only 4.8 % of its texels change. The shipped reach-back does not do it at these levels: 0 delta
  at levels 1-3 and 6 at level 4 on the same image, because level 4's source is level 1 - an 8:1 step,
  where a straddling texel weighs an eighth instead of a half.

  **Is there a better step than the area box?** Yes and no, and both halves are measured on
  04-AUTUMN (the two candidate kernels are `half` - integer boxes, no split - and `cubic` - the parent
  reconstructed with Catmull-Rom and integrated, `tools/mip_compare`):

  | step | area box | cubic | integer box |
  |---|---|---|---|
  | level 2 from the exact level 1 (2:1, cuts) | 39, HF -2.19 % | **27, HF -0.50 %** | 182, HF +0.7 % |
  | level 4 from the exact level 1 (8:1, cuts) | **6, HF -0.50 %** | 18, HF +0.90 % | - |
  | repeated (a chain of that kernel) | damps: 39 - 19 - 6 | **compounds: 27 - 27 - 20** | drifts |

  So a reconstruction kernel does what it says - it restores the high frequencies the parent's rect
  removed - and at 2:1 from an exact source it also comes CLOSER to the truth (27 against 39). But at
  8:1, and on a repeated chain, it loses: the area box is a contraction (each level averages its
  source's error away, 39 -> 19 -> 6) while a sharpening reconstruction is an amplifier (27 -> 27 ->
  20). There is therefore no step that is both sharp and accurate without either reading the base
  (anchoring) or making the grids integral (padding). The shipped rule already spends its anchoring
  exactly where the straddle weight is worst - near 2:1 - so its first chained step is the 8:1 case,
  where the plain area box is the better kernel.

  **SHIPPED RULE (decided 2026-11-01): span 1, with the window tightened to 0.8 from level 2 up.**
  `gl::Texture::kMipChainSpan = 1` and `kMipStepShrink = 0.8f`; level 1 keeps the plain area average of
  the base, because a source that has not itself been averaged has no lost width to restore (measured:
  tightening level 1 overshoots - 000026 renders +2.65 % against the ideal at LOD 1 without it and
  +4.21 % with it, while 000169 goes -0.09 % -> +1.90 %). The mask chain's whole-field build calls the
  same rule, so an image and a mask cannot drift. Import cost falls from ~3.3 base reads to ~1.3 and the
  visible cost is 0.1-0.3 points of rendered sharpness at LOD 3-4. The probe holds the behaviour: 128
  checks, including the analytic ramp (the reference equals the closed form to 1/255), the hard edge
  (confined to its own column), and the shipped chain at levels 2 and 8 - the first is the box to 0.3
  when its source is exact and tiles, the second is not by design (10.7 of 255) and is bounded at 15.

  **Tightening the box** (a reviewer's suggestion worth its numbers): the extra blur is not sampling
  between texels - the taps are whole texels - it is that the overlap-weighted 3-tap average is
  slightly WIDER than the ideal 2.002-texel box. Mixing the area kernel with a 2-tap box snapped to
  the nearest integer start is a single knob `s` (0 = area, 1 = snapped), measured on 04-AUTUMN level
  2 against the exact level:

  | s | high-frequency energy vs exact | max delta | mean delta | texels over 2/255 |
  |---|---|---|---|---|
  | 0.00 (area, shipped shape) | -2.20 % | 38.9 | 0.334 | 5.30 % |
  | 0.50 | -1.42 % | 53.7 | 0.592 | 5.55 % |
  | 0.75 | -0.74 % | 75.2 | 0.806 | 5.53 % |
  | 1.00 (snapped) | +0.14 % | 98.2 | 1.045 | 5.66 % |

  So yes - the blur can be bought back exactly, and `s` is a clean dial between "uniformly soft" and
  "hard but displaced". What the dial really trades is error *shape*: the snapped kernel puts each
  destination box on the nearest integer lattice, so texels land up to half a source texel away from
  where the exact level puts them - a sawtooth across the image. In a still that reads as harder,
  slightly ragged edges on high contrast; in an animated layer scaling it would shimmer, because the
  offset pattern is fixed in texture space while the sample point moves. The texels that are wrong do
  not change with `s` (about 5.5 % at every setting); only their magnitude does. And a 3x3 (or 3x2)
  tap set is what the area step already is - more taps cannot help, because the ideal footprint is
  only 2.002 texels wide.

  **Tightening around the true centre** - narrowing the window instead of snapping it - is the version
  that works, and it is free (the same 3 taps; raising the centre tap IS narrowing). 04-AUTUMN, level 2
  (a 2:1 step), window width as a multiple of the exact footprint, same centre throughout:

  | window | HF energy vs exact | max delta | mean delta | texels over 2/255 |
  |---|---|---|---|---|
  | 1.00x (as shipped) | -2.20 % | 38.9 | 0.334 | 5.30 % |
  | 0.90x | -1.50 % | 33.3 | 0.260 | 4.77 % |
  | **0.80x** | **-0.62 %** | **27.9** | **0.233** | **4.48 %** |
  | 0.75x | -0.11 % | 33.2 | 0.245 | 4.38 % |
  | 0.70x | +0.42 % | 34.3 | 0.276 | 4.38 % |

  Unlike snapping, this improves sharpness AND accuracy together (it compensates the discrete
  approximation's extra width rather than displacing the box), and the error map dims visibly: in a
  4-up of exact / area / 0.80x / snapped on a line-art detail, the 0.80x difference map is the
  cleanest of the three approximations and the snapped one is by far the brightest - a shifted edge is
  plainly perceptible.

  **But it is a function of the step ratio, and it must not be a constant.** The same test at an 8:1
  step (level 4 from level 1, 9 taps over an 8-texel window, where the discrete overlap already
  matches the box) goes the other way: 1.00x is best (-0.54 %, max 5.7) and 0.90x is already worse
  (+3.08 %, max 17.9). The correction is needed only where the tap set is coarse for the footprint -
  i.e. near 2:1. That is also why the shipped rule needs none: it chains only from 8:1 upward, and
  anchoring levels 1..3 means the coarse 2:1 step is never chained at all. If anchoring is ever
  dropped, the chained 2:1 step should use the tightened window (derive the factor from the ratio,
  never a magic 0.8).

  **How far back should a chained level reach?** Measured on four images that actually exercise it
  (thin outline art, dense hairlines/text, an engraving's hatching, and cel art), level 3 against the
  exact level:

  | image | from level 2 (a 2:1 step, twice) | from level 1 (one 4:1 step) |
  |---|---|---|
  | 11-ae.psd (outlines) | 33.3 / 0.269 / HF -2.54 % | **11.4 / 0.077 / -0.41 %** |
  | 35-img-000 (text) | 36.4 / 0.930 / -6.80 % | **13.2 / 0.387 / -2.78 %** |
  | 43-example-44 (hatching) | 24.5 / 1.053 / -4.57 % | **19.9 / 0.664 / -2.08 %** |
  | 04-AUTUMN (cel art) | **0.8 / 0.026 / 0.00 %** | 19.2 / 0.173 / -1.27 % |

  (max delta / mean / high-frequency energy; AUTUMN is the exception because that particular 2:1 step
  tiles.) So stepping twice through an intermediate level is worse than one wider step from a level
  that is exact - by 2-3x on the content that shows it. Which is the argument for a SPAN rather than
  always stepping one level: fewer chained steps, each from a better source.

  The cost/quality curve this gives, in base reads and worst level-3 error: anchor 1..3 (span 3, ~3.3
  base reads) leaves level 3 exact and level 4 at <= 6; anchor 1..2 (span 2, ~2.4) makes level 3 the
  first chained one at 11-20; anchor 1 only (span 1, ~1.3) makes level 2 the first, at 20-40 with
  -1.4 to -7.3 % high-frequency energy. Span 3 is where the visible levels are still exact.

  **What makes an image useful for this test**: fine high-contrast structure at several scales -
  hairlines, text, hatching, outline art. The predictor is the share of texels adjacent to a strong
  gradient (12.5 % on a chart, 3.5 % on a line drawing, ~0 on a soft painting), not its subject:
  smooth painted work and flat-colour sprites show almost nothing at any level.

  **And the criterion that actually matters is the RENDER, not the level.** Comparing a level against the
  exact area average measures a reference nobody sees: the screen shows the layer sampled at a scale,
  through trilinear blending of two levels. Measured that way (000022, base 1500x1440, chain built up
  from level 0 so all three share one source, LOD 1.0 to 4.0 in quarter steps, sharpness = mean gradient
  of the rendered layer):

  | LOD | exact | chain1 (span 1) | chain3 (span 3) |
  |---|---|---|---|
  | 1.00 | 11.25 | 11.25 (0.00 %) | 11.25 (0.00 %) |
  | 2.00 | 14.26 | 14.25 (0.08 %) | 14.26 (0.00 %) |
  | 3.00 | 15.00 | 14.58 (2.77 %) | 15.00 (0.00 %) |
  | 4.00 | 17.81 | 17.19 (3.44 %) | 17.77 (0.22 %) |

  So the whole anchoring question is worth at most **~3.4 % of rendered sharpness** even for the cheapest
  chain, and ~0.2 % for the shipped one - while the sampling model itself moves sharpness by 30-40 %
  between LOD integers (11.25 at 1.0 against 14.26 at 2.0, the same jump in every policy). Sub-pixel
  phase sensitivity (a shimmer proxy: standard deviation of the render across four sub-pixel offsets) is
  identical to three decimals across policies (1.578 / 1.578 / 1.578 at LOD 2).

  Which means the span should be chosen on COST, not on perceived quality: span 1 is the cheapest and
  renders within 3.4 % of exact at the deepest scale measured. The anchor is not a correctness measure,
  it is buying a few percent of sharpness at depth - so if the chain's cost ever matters, drop it; keep
  it if the extra base pass is free, because it makes the levels exact for their own sake.

  **And the kernel is not open either: with span 1, TIGHTEN.** Rendered sharpness against the ideal (the
  base area-averaged at the same scale), 000022, trilinear, span-1 chains built from level 0:

  | LOD | ideal | exact (= span 3 here) | span 1 area | span 1 tightened 0.8 |
  |---|---|---|---|---|
  | 2 | 14.249 | 14.257 (+0.06 %) | 14.249 (0.00 %) | 14.249 (0.00 %) |
  | 3 | 16.829 | 14.996 (**-10.90 %**) | 14.585 (-13.33 %) | 14.977 (**-11.00 %**) |
  | 4 | 20.268 | 17.805 (**-12.15 %**) | 17.198 (-15.15 %) | 17.750 (**-12.43 %**) |

  So plain span 1 is 2.4-3.0 points softer than span 3 in the render, and tightening puts it back within
  0.1-0.3 points - at the same 3x3 taps, no extra work, no extra memory. Sub-pixel shimmer is unchanged
  across every policy (2.41-3.07, spread under 2 %). The factor is not fragile: 0.75 / 0.80 / 0.85 / 0.90
  give -10.46 / -11.00 / -11.56 / -12.11 % at LOD 3, i.e. the whole sensible range lands within ~1.5 % of
  span 3, with 0.8 the sweet spot and 0.75 beginning to overshoot.

  That is the recommendation: **span 1 with a tightened window** - cheapest chain, renders like the
  anchored one.

  WHICH RATIO, THOUGH. A span-1 step is nominally 2:1, but the actual per-axis ratio is
  `S / floor(S/2)`: exactly 2 only while the level's side is EVEN, and `2 + 1/C` from the first odd
  side onward, growing as levels shrink - 000022 runs 1500, 750, 375, 187, 93, 46, 23, 11, 5 and its
  ratios are 2, 2, 2.0054, 2.0108, 2.0217, 2, 2.0909, 2.2. So the factor is not a function of a
  constant 2:1, it is a function of how far above 2 that step's ratio sits.

  It does not need a formula, because the two regimes are already separable and tightening is FREE
  where it must not act: on a step that TILES, the centred window of 1.6 texels covers the same two
  texels with equal weights as the 2.0 window does, so narrowing is an exact no-op - measured on
  750x720 -> 375x360: 1.00 / 0.90 / 0.80 / 0.70 windows all give max 1.4, mean 0.223, HF -0.09 %,
  identical. On a step that CUTS it acts as measured (375x360 -> 187x180: HF -4.36 / -2.43 / -0.51 /
  +1.40 % for the same four windows).

  So the rule is a single constant: **tighten by 0.8 at every chained level**. It is inert wherever the
  step is exact and compensates wherever it is not. The 8:1 step measured earlier is the same story -
  its ratio is 8.008 and tightening there HURT, which is why the shipped span-3 tail uses the plain
  area box.

  The object-tree block WAS the target, and the profile found it: `ObjectTree::deserialize` was 10.2 s
  over the harness's four deserializes, of which 8.4 s was `new LayerNode` - each layer built its own
  `MeshTransformerResource`, i.e. compiled the same three mesh-transform programs 344 times. Sharing one
  resource per `ShaderHolder` (`ShaderHolder::meshTransformerResource`) took the load cycle from 16.7 s
  to 8.3 s on the 54 MB project and 4.99 s to 3.92 s on the 3.6 MB one; the ctor is now 0.06 s and what
  remains of the block is ~0.4 s per load, mostly blend-shader variants compiled once each.
- **Stage 3 - pages**: the pool, the table, analytic demand, the coarse chain and the eviction of
  §5, behind a switch. The switch is **a control, not a crutch**: it exists so the gates can compare
  the page path against the base-level path on the same frame, and so the two can be A/B'd for `ms`.
  It is not shipped as a user-facing fallback. **Removal criterion**: every gate in §5.6 green on the
  small, the mid-size and the 15000² project, and the counter showing no regression (residency,
  uploads within cap, `ms` within budget) - then the switch and the base-level draw path are
  deleted. If a project cannot pass, that is a design bug to fix, not a reason to keep the switch.
  Gate: residency tracks the screen-derived bytes on the 15000² project **while playing**, pixel
  equality against a full-chain reference, frame time inside budget.
- **Stage 4 - masks in the pool, BUILT.** The blocker was that a mask's levels ARE a GPU texture
  built incrementally by `MaskMipChain`, so the pages need the level boxes from a source that is
  not the full texture. The resolution is a hybrid: the GPU builds the field's chain ONCE (96 ms
  for 4096² against 3.6 s for a CPU whole build), the chain's levels are captured into
  `core::MaskPyramid` (byte-exact), the full chain is destroyed, and from then on a stroke
  rebuilds the CPU pyramid's boxes (span-1 two-pass with the chain's R16F half store, within 1-2
  LSB of a GPU rebuild) and `VirtualTexture::updateMaskBox` refreshes the coarse chain's levels
  and the RESIDENT pages the box touches. Page fills come from the pyramid
  (`MaskPyramid::fillPage`), and the mask pages live in the pool's R8 stack
  (`gl::PagePool::Format_R8`, atlases 4..7) with their own table and coarse chain.
  The field cannot be sampled through the image's stack: its uv is the node's content mapping, so
  the demand pass computes it per vertex from the same affine the fragment shader uses. Measured:
  a 900x700 painted mask is `MA1`-identical to the full chain within 1/255 (mean 0.047, the
  residual is the image's paging) with all 12 wanted pages resident, and a stroke painted into a
  live paged matte (`MX3`) is identical to the full chain rebuilt from the same field (max 0).
  The real PSD `000145.psd` (24 masks, 6 of them pageable) matches the control at max 1/255 after
  the pages land. The pyramid stores levels 1 and deeper only: level 0 IS the field, the same rule
  the image pyramid follows, so a 4096² mask's CPU levels are 5.33 MiB (against 21.33 for a full
  copy) and the GPU's resident floor is the 0.33 MiB coarse chain; `MX4` pins the split. A
  FOLDER's matte pages through the same stack (see "Folder mattes" below): its pass is
  fullscreen, so its demand is a quad rather than a mesh walk.
- **What to skip**: a per-layer level manager as a shipping state. It is throwaway once pages land,
  and Stage 3's coarse fallback gives the same behaviour at the right granularity. The one useful
  checkpoint is Stage 2 + "upload the whole needed level" as a *debug path* to validate upload and
  eviction plumbing without the page table.

## 7. Moving FFD to the GPU

Today the CPU blends FFD key positions into `expans.ffd().positions()` and uploads them; the pass then
skins. Worst case (animated FFD on a 15000² layer) that is ~890 k vertices walked on the CPU and
~10 MB/frame uploaded **[D: from `kDefaultMeshCellSize = 16`]**, which is the cost the GPU pass
exists to avoid.

Proposal: one pass does FFD blending (weights/positions in a buffer) and then SRT + skinning, so the
CPU stops touching vertices per frame. The CPU keeps the authoring copy for editing (a drag needs
positions: a readback of the edited mesh, or a small CPU mirror updated on edit, not per frame).
Consequences to design: the density meter and the clipper bounds read the same output; the
`TransformKey` cache's inputs change (the blend state moves into the pass).

Gate: fixture parity between the CPU blend and the GPU blend (bit-exact where the arithmetic is the
same, tolerance documented where it is not), playback cost from the counter, and drag latency.

## 8. Clipping, with GPU-side deformation measurements

Today clippers are singulated into `ClippingFrame` (a mask texture) and clipped layers draw through
it. There is no scissor and the clipper's own bounds are not used: the singulation pass and the frame
clears run at full device size.

With the deformed positions on the GPU (and the same reduction the density meter uses) a clipper's
**screen AABB** becomes available per frame. That allows: (a) scissoring the singulation pass and the
clipped layers to the AABB, (b) skipping clipping entirely when the AABB misses the viewport, (c)
smaller clears, (d) a precise cull for every layer, not just clippers - the same AABB signal answers
the "no screen-bounds test exists" gap in §1.

Costs: one reduction per clipper/layer per frame (the density meter's second attachment is the same
shape of work), and a policy for the one-frame staleness of an asynchronous readback. Gate: pixel
equality against the current path, and the counter showing the pixels saved.

## 9. Open questions

- **Anisotropy** (§3): metric fields, sampler support, ratio behaviour beyond the hardware limit.
- **Filters that need sharper-than-screen input**: a blur or wave samples the layer texture at the
  frame's resolution; a page/level chosen from the screen can starve the filter. Each filter needs a
  stated demand (measure with the counter), and a policy for layers that carry one.
- **Export has no frame budget, but a hard memory budget.** "Time is free" was the wrong phrase:
  there is no *frame* budget, so a blocking upload is acceptable - but GPU memory is finite, and a
  15000² layer's base level is 900 MB, so an export that uploads every layer at full resolution does
  not fit either. The export is the same paged renderer with a different **policy**, not a different
  implementation:
  - *synchronous demand*: the analytic demand of the frame being exported is uploaded immediately and
    the draw waits for it - time is free here, latency is not a concern;
  - *a hard memory cap*: the pool evicts by LRU between layers (a layer is drawn once and never
    needed again in that frame), so the peak is the render target plus the pages of the layers in
    flight, not the whole document;
  - *graceful degradation, never failure*: if one layer alone exceeds the cap, its level is reduced
    for that export and the reduction is reported - the same blur-vs-fail choice the viewport makes,
    made loudly instead of silently;
  - the render target itself remains bounded by the output size, which is the exporter's existing
    cost and is not affected by residency.
  The export gate still empties the pool first and asserts correctness from an empty pool, and it
  additionally asserts the peak stays under the cap while exporting a project whose layers together
  exceed it.
- **Premultiplied vs straight** storage (§4).
- **Format**: the pyramid chunk, its version, and the legacy build-on-load path.
- **Budgets**: what a project may spend, per platform; the counter is the referee.

## 10. Instruments (already in the tree)

| tool | what it answers |
|---|---|
| `gl::FrameStats` (`AE_FRAME_STATS=<path>`) | per-frame `ms`, layers considered/drawn/culled, `mesh` passes, `fill_px`, live/peak texture bytes, mask bytes, density histogram and max |
| `<path>.trace` (`AE_FRAME_STATS_DENSITY_TRACE=<n>`) | per layer: mesh source, uv vs rest vs mesh vs content areas, `det_wi`, sliver counts, the worst triangles |
| `AE_VT_LANDING_SPLIT=1` | `VTLANDING` per landing frame: the frame's `vt_frame_ms` divided into `pool` (the pool's frame stamp), `read` (the demand map's fence check + map), `parse` (every resource over the mapped bytes), `clear` (the array's clear), `acquire`, `fill`, `upload`, `table` and `other`, with the page count and `staged=` (how many pages went through the upload stage; fewer than `pages=` means the stage was skipped or full and those went direct) - how a landing change is measured against the parts it is not (and `read` is the frame's own submit surfacing, not addable; see Measured) |
| `tests/density_meter.cpp` | the metric itself: 1x/2x/4x exact, and an animated case where placement changes every frame |
| `scripts/gui/probe_camera_transform.sh` | a camera move skips the pass; a frame step re-runs it |
| `tests/verify_mipmap` (branch) | chain correctness: premultiplied averages, halo, the level a footprint reads |
| `tools/render_probe --repeat <n>` | the SAME frame repainted n times - a still view (a pointer move, a matte stroke) - for the demand replay's cost and `AE_VT_STATS`'s `marks=`/`replays=`; each repaint is written as `frame-<f>-r<i>.png` |
| `tools/render_probe --repeat <n> --pan <px>` | the same frame panning the camera N px per repaint - the camera-navigation case, and how the **page-content defect** (a page reading another page's texels while the pool fills) was found; the open list's first item |

## Sub-pixel shimmer is Nyquist content, not the chain and not anisotropy

Root-caused 2026-11-01 after a viewer reported hair strands flickering while zoomed out and playing.

The law. Sampling one texel pair `(a, b)` with GL_LINEAR at offset `d` gives `g(d) = a + d(b - a)`, so
over a full pixel of movement the temporal standard deviation is `|b - a| / sqrt(12) = 0.2887 |b - a|`.
For a sinusoid of frequency `w` the modulation is `|(1 - d) + d e^{iw}|`, which at the sampling Nyquist
(`w = pi`) is `|1 - 2d|` - the signal goes to ZERO at half a pixel. Content at the pixel frequency
therefore blinks at 100 % amplitude, whatever produced the image.

Measured on 000022, level 0 sampled 1:1, an 8-offset sub-pixel sweep: the std against adjacent-texel
contrast has slope **0.2859** (predicted 0.2887, 1 % apart) and correlation **0.999**; the 2.9 % of
texels whose neighbour differs by more than 40 of 255 carry **40.6 %** of all the shimmer - the hair
highlights and strand edges, exactly where it was seen. A 2x coarser representation sampled at the same
size shimmers **1.6x less**, which is the box filter's response at Nyquist.

Two things it is NOT. Not a mip property: level 0 sampled at its own size shimmers, with no chain
involved and no shrink at levels 0-1. Not anisotropy: a uniform zoom gives a square footprint (ratio 1),
where anisotropic filtering is a no-op by definition (it is on, 16x, `ImageKey::layerMaxAnisotropy`).

The only levers are those that change the RECONSTRUCTION, and the reconstruction turns out to be the
actual fault - a viewer's eye said so before the numbers did ("sharp at some offsets, blurrier at
others"), and the first metric I used hid it.

WHAT BILINEAR ACTUALLY DOES. At 4x minification, level 2 IS the exact 4-texel box, so sampling it poses
no chain question at all. Sharpness of the render (mean horizontal gradient) against the truth - the base
area-integrated over the moving footprint - as the art slides by fractions of a pixel:

| offset | truth | bilinear |
|---|---|---|
| 0.00 | 7.476 | 7.479 |
| 0.25 | 7.476 | 6.205 (-17 %) |
| 0.50 | 7.468 | 5.690 (-24 %) |
| 0.75 | 7.478 | 6.192 (-17 %) |

Bilinear does not TRANSLATE a sub-pixel shift, it BLENDS two texels: at half a pixel it is a two-texel
box, 24 % softer. The truth's sharpness is constant to 0.1 %. That oscillation of sharpness with phase
is what the eye calls roughening or flicker, and it is a defect of the filter, not of the content.

At the shipped LOD (bias -0.5 = a 50/50 blend of two levels) the swing is 14 %, and a 4-tap Catmull-Rom
read cuts it to ~3 % (with +7 % overshoot at the on-centre phase) for 16 taps per pixel instead of 4:

| offset | truth | bilinear | bicubic |
|---|---|---|---|
| 0.00 | 7.476 | 7.475 | 8.008 |
| 0.50 | 7.468 | 6.398 (-14 %) | 7.227 (-3 %) |

WHY THE FIRST METRIC WAS WRONG: mean per-pixel temporal standard deviation scored bilinear at 2.231 and
the truth at 2.540 - it rewards blur, because a blurred image changes less between phases. A metric for
this has to measure sharpness CONSISTENCY across phases, not variance, and it should be read against the
truth per phase. Anything that only reports variation will keep recommending blur.

## The sampling rule this line of experiments converged on

`tools/render_probe` made every one of these measurable on the real artwork (deterministic, one output
frame per project frame, no display - `QT_QPA_PLATFORM=offscreen`). What holds up, with the numbers:

- **level dominates.** With the sampling held fixed, level 0 gives error 0.18 of 255 against a 4x
  supersampled reference, the shipped trilinear blend (LOD 0.6, i.e. 40 % level 0 + 60 % level 1) gives
  1.96 even with 16 taps per level, and level 1 gives 0.90 at its best. The tap count, once the level is
  right, is worth 0.18 vs 1.15 (2 taps) vs 0.46 (3 taps).
- **2 taps at the finest level already halves it**: 4 texel fetches (the same count the hardware spends)
  take the error from 2.18 to 1.15; 3 taps (9 fetches) reach 0.46; 16 taps 0.18.
- **level transitions must be a blend, not a `ceil`.** Sweeping zoom, a discrete level choice reverses
  its sharpness trend exactly at zoom = 1/2^(L+1) (-12 % then -20.5 % at 0.5; -24 % then -37.6 % at
  0.25); blending two properly-tapped levels is monotone (-12.3 %, -12.1 %; -23.4 %, -26.9 %).
- **anisotropic footprint** (layer stretched 4:1 in the loaded project): candidate 0.26, hardware with
  16x anisotropy 0.40.
- **deformed layer** (a MeshKey built from the layer's own grid, displaced; candidate error 0.25,
  hardware 0.41).

The mechanism the numbers point at: pick the level from the footprint, take 2 taps per axis on each of
two adjacent levels, blend by the LOD fraction - 8 texel fetches, ~0.2 ms per screen-covering layer at
1080p, no LOD bias needed.

TOOL TRAPS FOUND WHILE MEASURING (each cost a wrong answer): `--zoom` is an absolute screen scale and
must be multiplied by the supersample factor, or a supersampled comparison is framed differently; a
shader edit does not rebuild `assets.qrc`, so `touch assets/assets.qrc` is required; a MeshKey must be
installed with `TimeLine::adoptKey` (the MAP) because the mesh blend reads `map(TimeKeyType_Mesh)`,
never the default-key slot; and a perturbation made after load must emit `Project::onTimeLineModified`
AFTER the Driver is constructed, because the Driver is what listens.

**IMPLEMENTED, MEASURED ON THE REAL PROJECT, AND REVERTED (2026-11-02).** The 4-tap path was built: the
layer fragment shader samples `sampleLayer()` (Catmull-Rom, 4 taps per axis on each of the two levels the
LOD falls between, hardware read below one texel of footprint), with `ImageKey::sharpLayerEnabled()` and
`AE_LAYER_SHARP` as the switch and the LOD taken from the layer's transform. Measured by dumping the
scene FBO during playback of a real project (12 frames, 603x679, layers at ~0.3 scale - hand-drawn
strokes, i.e. sub-pixel features):

| | mean rendered sharpness | per-frame std | per-pixel temporal std |
|---|---|---|---|
| 4-tap reconstruction | **4.430** | 0.018 (0.4 %) | 6.871 |
| hardware trilinear | 4.024 | 0.020 (0.5 %) | 6.601 |

So it is 10 % sharper and no steadier on actual content - the per-frame sharpness varies by half a
percent either way, because the artwork moves far more between frames (7.6 of 255) than the phase effect
it was built for. The owner's verdict, watching both builds side by side: "not really better in motion".

And it had a correctness defect: the LOD came from `-log2(cameraScale x layerScale)`, which is the
IMPOSTOR pattern - valid only for a planar, uniformly scaled draw. A layer with FFD, bone or pose
deformation has a different texel-to-pixel ratio across its own surface, and screen DERIVATIVES are the
mechanism every GPU and engine uses for exactly that reason (they are per-pixel correct under
deformation; they wobble with the sub-pixel phase, which trilinear blending is smooth enough to hide).
The app already computes the correct local scale per triangle (`s_max` of the uv->mesh Jacobian) in the
density pass, but only into a histogram, not into anything the draw can read.

Reverted for both reasons: no perceptible gain, and an LOD rule that would be wrong on every deformed
layer. What survives is the measurement: bilinear really does blend instead of translating (24 % of the
rendered gradient within a pixel of travel at 4x), the truth does have its own temporal variation, and a
4-tap read costs +0.43 ms per screen-covering layer at 1080p. If this is ever revisited, the LOD must
come from the per-triangle Jacobian, not from the transform.



WHICH FILTER, AND WHAT IT COSTS. Measured on the same phase sweep; "swing" is the peak-to-peak change of
rendered sharpness across a pixel of travel (the visible artefact), "err" the mean absolute difference
from the truth, "fetches" the texture reads per pixel (the built-in trilinear is 8 at a fractional LOD,
4 at an integer one):

| filter | fetches | swing | mean \|err\| |
|---|---|---|---|
| bilinear, one level | 4 | 23.9 % | 1.869 |
| two bilinear samples straddling the footprint ("box2") | 8 | 5.1 % | 1.869 - but uniformly at 6.0 sharpness against the truth's 7.48 |
| Catmull-Rom 4-tap, one level | 16 | 12.8 % | 1.692 |
| bilinear on both levels (ships) | 8 | 14.4 % | 0.913 |
| cubic on the dominant level, bilinear on the other | 20 | 14.9 % | 0.845 |
| Catmull-Rom (sharp cubic) on both levels | 32 | 9.7 % | 0.914 |
| Mitchell (B = C = 1/3) on both levels | 32 | **6.9 %** | **0.851**, at 6.71 sharpness against the truth's 7.47 |

The two 32-fetch kernels are the final dial: Catmull-Rom keeps the sharpness closest to the truth (7.23
against 7.47 at half phase) with a 9.7 % swing; Mitchell is the steadiest (6.9 %) and about 10 % softer.
Both beat bilinear on the axis that was visible (sharpness at half phase: 6.40 for bilinear).

Three conclusions. (1) 4 taps per axis is the cheapest INTERPOLATING kernel - a 3-tap kernel is a
B-spline, i.e. an approximating one, and measured it is soft everywhere (sharpness 4.8 -> 1.7 across the
sweep), so "cheaper than 4 taps" means "blurrier", not "as good for less". (2) The 20-fetch hybrid buys
nothing: the bilinear half still blends, and the swing stays at 14.9 % against 14.4 %. (3) The "box2"
trick removes the swing by being uniformly soft - the same trap the temporal-std metric fell into.

So cubic on both levels (32 fetches when the LOD is fractional) is the one that works, and its cost can
be gated: the layer's scale is known on the CPU, so the 4-tap path only needs to run when the layer is
MINIFIED (or rotated); magnification is exact with bilinear. A non-uniform scale can drop to 4 taps on
one axis only.

WHAT IT COSTS, MEASURED rather than estimated (`/tmp/texbench`: EGL pbuffer, 1920x1080, 200 frames per
case, a 2048^2 mipmapped texture on an AMD R9 380 / Mesa 26.2.2). The baseline matters: what ships today
is ONE trilinear `texture()` call, which the hardware implements internally - my first benchmark compared
against "8 taps" that the sampler never takes:

| sampling | ms per screen-covering layer (1080p) |
|---|---|
| 1 trilinear `texture()` - what ships | **0.196** |
| 2 `texture()` (the two levels of a trilinear blend) | 0.199 |
| 32 `texelFetch` (4-tap per axis on both levels) | **0.630** |
| 32 `textureLod` (filtered taps - do not do this) | 1.194 |
| 16 `texelFetch` (4-tap on one level) | 0.328 |

So a 4-tap implementation costs **+0.43 ms per screen-covering layer** at 1x coverage (0.85-1.7 ms at a
character sheet's 2-4x overdraw) - and, more usefully, it bounds the whole question: the ENTIRE texture
sampling of a full-screen layer is 0.2 ms of a 16.7 ms frame today, so sampling is not where this app's
frame time goes and the addition cannot move it much either way. Use `texelFetch`, not `textureLod`:
asking the hardware to filter each of 32 taps doubles the cost for nothing.

These are synthetic (warm cache, no blending). The figure that decides shipping is the same experiment in
the app - run the project with `AE_FRAME_STATS` on both shader variants and compare `ms`.
## Stage 3, built (2026-11-05)

**Status: implemented behind `AE_VIRTUAL_TEXTURE` (default on), with the full-resolution path kept
as the A/B control.** This section records what was built, where it differs from the design above,
and what the gates measure. The design text is left as written so the reasons survive; where the
implementation differs, the difference is stated here.

Stage 4 follows in the same section: clippers and LAYER masks were paged with it, and a FOLDER's
matte joined them on 2026-11-06 ("Folder mattes"), which leaves the filter-lane masks as the only
resource textures outside the pool.

### What a paged image is now

`core::VirtualTexture` (one per `ImageKey`, owned by `ImageKey::Cache`) is three things:

1. **The coarse chain.** Every level whose longest side is at most `AE_VT_COARSE` (default 512)
   down to 1x1, as one ordinary mipmapped texture. It is always resident: a page that is not
   there samples it, so a miss is blur. It is **copied level for level** from the production
   chain, not rebuilt, so the fallback and the control cannot disagree about a texel.
2. **The page table.** One `R16UI` texture of `(pagesX(0), sum of pagesY(level))`: 0 means "not
   resident", otherwise `(atlas << 8) | (slot + 1)`. Levels sharper than the coarse chain are
   paged; the rest are the coarse texture. It is rebuilt from `mEntries` in **one**
   `glTexSubImage2D` whenever the resident set changes, after **every** resource's uploads for
   the frame (a second resource's upload can evict a first one's page; publishing per resource
   would leave a table naming a slot someone else owns).
3. **The demand map.** One `R8` texel per page of the same grid, marked by a geometry pass that
   walks each drawn layer's own triangles with the skinned positions and the camera. It is one
   LAYER of a shared `GL_TEXTURE_2D_ARRAY` (one layer per resource), not a texture per resource:
   a clear, a copy and a fence per FRAME, however many resources there are. The per-resource map
   was built first and measured - on `nene` (105 paged resources) 105 `glGetTexImage` calls and
   105 fences cost ~40 ms of a 94 ms frame, against ~8 ms for the geometry pass itself. Two PBOs
   and a fence make the readback asynchronous; a page is wanted for `kKeepFrames = 3` frames
   after it was last marked.

The **pyramid** (`core::ImagePyramid`) is the page source: levels 1..coarse-1 captured from the
GPU chain into RAM with `glGetTexImage` at key creation, level 0 generated from the resource's own
pixels (premultiplied with the upload's exact rounding) when a level-0 page is asked for. Level 0 is
not stored: it is the larger half of the chain and the resource image already holds it, so storing
it again would double the cost of every paged image for a memcpy. The capture is once per resource,
at load; nothing reads a texture back during a frame.

### The pool

`gl::PagePool` is the slot allocator, deliberately knowing nothing about images or levels:

| | |
|---|---|
| page | 256x256 content texels |
| gutter | 2 texels on every side (one filtered read reaches one texel past its centre) |
| pitch | 260 |
| atlas | 4096x4096, 15x15 = 225 slots (3900 of 4096 texels used), 60.8 MB RGBA8 |
| atlases | up to 4 (the shader's four-way branch), 243 MB |
| budget | `AE_VT_BUDGET_MB`, default 256 MB, clamped to 4 atlases |
| upload cap | `AE_VT_UPLOAD_MB`, default 8 MB/frame (29 pages); 32 MB was measured to cost 60-140 ms on the frame that spent it |
| eviction | least recently touched page; every requested page is touched before any upload |

The gutter is 2, not the design's 4, and the slots are 15x15, not 16x16: 16 slots of 260 pitch do
not fit 4096. A slot that cannot have a gutter is not a slot.

**The first atlas is a fixed 4096^2 (60.8 MB), and that was re-examined and kept on 2026-11-08.**
A demand-sized first atlas (2048/3072/4096 by the first landing's wanted pages, with a purge and
re-land when a bigger demand outgrew the choice) was built, gated and measured: it saves 50.3 MB
on projects whose full chains are ~10-70 MB (`test.anie` 22.0 MB against 72.3 MB) and nothing on a
project whose chains dwarf the atlas. It was reverted by decision - the floor is not a VRAM
problem (61 MB is ~6 % of a 1 GB card and nothing on an integrated GPU), paging pays where the
chains are far larger than one atlas (`dinner lab` 307.6 MB, `ballerina` 401.3 MB against
88.9/83.8 paged), and the smallest project is still not made whole by it (`test.anie`'s whole
chain is 9.6 MB, below even the 15.2 MB small atlas). The alternative's variable geometry and its
purge-and-re-land on growth were judged more machinery than the saved bytes were worth. See
Measured, "the first-atlas floor was examined and kept", and open item 7 for the policy that would
actually fix the smallest projects. A fixed 2048 first atlas is worse: 57 pages then needed a
second 4096 atlas for 8, 77 MB against 61.

Growth is another atlas when the budget allows; an allocation that fails drops the budget to what
is already allocated and the frame falls back to the coarse chain. An atlas with no resident page
and no use for 600 frames is released, so a smaller window or a closed project returns memory
without a restart.

`PagePool` owns the pages' GL memory, not their content: the caller uploads the page's 260x260 (the
`VirtualTexture` fills the gutter from the level's neighbouring texels, or transparent outside the
resource) and then the table entry is published. **The bytes go through the pool's staging ring**:
`beginStaging()` maps one of two buffer objects, the landing fills its pages into it, and
`endStaging()` unmaps and copies them to the atlas with the buffer bound as
`GL_PIXEL_UNPACK_BUFFER`, which is one CPU copy fewer than uploading from client memory (Measured:
0.10-0.15 ms a frame against 3.1-4.3). A mapped buffer may not be read by GL, which is why a staged
frame has those two phases; a stage that is unavailable (its ring slot's fence has not signalled)
or full makes the page fall back to the direct call, the same pixels by the slower route. A pool
**generation** changes when the pool is purged or its context is recreated; a `VirtualTexture` that
sees a new generation clears its table before sampling, so a dead slot number can never reach the
shader.

### Demand

The design's "the density pass already walks the triangles" became a dedicated geometry pass,
because the density meter exists only under `AE_FRAME_STATS` and a paged renderer cannot have its
demand switched off. The pass is one indexed draw per layer, before the layer's own draw, reading
the same skinned position buffer; it does not read anything back synchronously. A **repeat of the
same inputs** skips the draw entirely and replays the pages the last one marked (see below).

Per triangle it computes `J = d(screen)/d(texel)` from the three screen positions and the three
uvs, takes `s_max` (the demand's level is the one the fragment shader's derivatives pick:
`floor(log2(1/s_max) + bias + 0.5)`), and rasterises the triangle's **page rectangle** at that
level into the demand map. The rectangle is the triangle's own uv box grown by the tap reach in
level texels - a tap centre is at most `0.25 * major` along the major axis plus `0.2 * major` of
its own block, so the page box grows by `0.5 * anisotropy + 1` page units, clipped to a page. The
pass marks **the triangle's own level and no other**: a 2x2 derivative quad that straddles two
triangles rounds to one of *their* levels, and both triangles mark their own, so no margin is
needed. One level of margin plus a full-page border was built and measured: 474 pages (128 MiB)
against 79 (20 MiB) on nene, and it did **not** bring the frame closer to the control (mean 0.83
of 255 against 0.61) - the extra coverage bought nothing, so it is not built.

The page coordinates are computed from the same `(inTexCoord + uTexCoordOffset)` the layer
shader divides by `uImageSize`, not from the mesh's raw uvs: a mesh origin offset (a custom
`MeshKey`) moves the sample by a part of the image, and demand on the untranslated uvs names the
wrong pages. The default grid mesh cancels the offset (`originOffset == imageOffset`), which is
why the first gate did not see it.

The pass runs with `uScreenSize` in **device pixels**: the camera's view matrix maps to NDC, so
the pixel scale is half the device screen. Leaving that out made every footprint read about
`log2(width/2)` levels too coarse - which is exactly the bug the first version had, and it is why
the gate asserts demanded pages, not just rendered pixels.

The array's layers are written with `gl_Layer` in a geometry shader and an FBO with the array
attached as a layered color target (GL 3.2); the per-layer grid is a sub-rect of the cell, and the
rest stays cleared. Reallocation (a new resource with a larger grid) recreates the array empty -
the map is cleared every frame anyway, so there is nothing to copy.

#### A deformed layer has no quad: the warp, the predicate, and the screen cull (2026-11-07)

The affine fast path is only for layers the transform can see. A layer with no deformation has one
Jacobian over its whole surface, so its uv is an affine function of the screen position and the
quad is exact. A layer deformed by an FFD, a mesh key, a bone binding or a wave is not: the
fragment's uv comes from the WARPED mesh, while the quad's `screenToContent` is the node's SRT,
which maps the REST grid. Measuring that case for the first time found two defects - and the second
was hidden by the first.

**The FFD test was dead.** The predicate claimed to count "an FFD key" and tested
`ffdMeshParent()->type() == TimeKeyType_FFD`. `ffdMeshParent()` names the ImageKey (or MeshKey)
whose grid mesh the FFD keys displace - it can never BE an FFD key - so the test was false by
construction and every FFD-warped layer took the quad. (The alternative that was originally
guarded against - testing `ffd()`, which the blender fills with the rest grid when there is no
key - is still the right thing to avoid.) The predicate now asks whether the timeline holds any
FFD key: an FFD key whose value happens to be the rest grid costs the walk and nothing else, while
comparing `ffd()` against the area mesh every frame would be a per-vertex CPU pass to save a
geometry pass.

**The bone test was on the wrong population.** The same reading found the predicate's other half:
it excluded `bone().isAffectedByBinding()` - the layers RIGIDLY BOUND to a bone, whose draw is one
matrix (`MeshTransformer::callGL` takes the outer matrix, inner identity) and whose quad would
therefore be exact - and accepted the layers that ARE skinned per vertex. A layer under an active
BoneKey is an influence target: the blender gives every such descendant an influence cache, so
`bone().targetMesh()` is its own mesh and `callGL` takes the `useInfluence` branch, applying the
pose palette per vertex. `AE_VT_DEMAND_SPLIT`'s new `infl=` field prints that population: on
`dinner lab` at frame 0, **nine layers report `infl=1` and seven of them were taking the quad**. The
predicate now excludes `bone().targetMesh() != nullptr` too - a conservative correction, since it
can only move layers from the quad to the walk - and that project's frames are unchanged (max 1,
the slot rounding, at frames 1-8) because the bone displacement there stays inside a page at that
camera; the case that would make it visible (a pageable part pulled more than a page by a pose)
still wants a bone fixture the harness does not have (see the open list).

**The walk demanded the whole layer.** With no screen test, every triangle marks its own uv box
whatever the camera sees, so the union is the layer's page grid - harmless at a fit view, where the
whole layer is visible, and ruinous at a magnified one. The geometry shader now culls a triangle
whose screen bounding box misses the target (a one-pixel margin keeps the straddler, the only one
that can produce a border fragment). Measured on ballerina at a 640x640 zoom-1 view, frames 8-13:
**482 pages wanted without the cull against 250 with it** - and the frames are **bit-identical**,
so the extra 232 were pure over-demand. The old quad path wanted 250 on the same view, so the
correct demand costs the same pages the approximation did; the fix is about which pages, not how
many.

No project in the local dataset provides that case, so `DF1` constructs it: a 2048x2048 noise layer
on a 320x320 canvas at **zoom 4**, with an FFD key that translates every grid vertex by **768
content px - three level-0 pages** - so the view samples a content region three pages away from
where the SRT says it is. Measured: **4 wanted pages of the layer's 64-page level-0 grid, all
resident**, and paged vs the full-resolution control **max 0, mean 0.000 of 255**; the warp moves
the frame by **217** at the centre, so the parity is not two rest-grid frames agreeing. The two
mutations have power: with the affine path forced (the old predicate) the same fixture measures
**max 138, mean 28.4**; with the cull off it wants **all 64 pages** and the frame is unchanged.

The walk's cost is proportional to the deformed layers' TRIANGLES, and the cull does not reduce it
(the geometry shader still runs per vertex) - it bounds the page set, which is what the pool sees.
On ballerina, whose ten FFD-keyed layers now walk, the frame time is inside the project's own
frame-to-frame variance (55-94 ms over frames 8-13 with the walk, 53-99 with the quad), so the
cost of correctness is not measurable there; it would be on a 15000^2 layer carrying bone motion,
which is still unmeasured (see the open list).

#### The replay: a frame whose page set did not change does not re-walk the triangles

A repaint of the same frame - a pointer moving over the canvas (the brush ring follows it), a matte
stroke, a dock animating, a panel asking for a redraw - wants exactly the pages it wanted last
time. The pass is a function of the deformed vertices, the camera mapping and device size, the uv
offset, the session's level bias, and for a matte the screen->content mapping and the rect;
`VirtualTexture::markDemand` (and its two matte forms) takes those as a `DemandSignature`, and an
exact repeat **replays** the pages the last consumed copy of the map marked instead of drawing. A
camera move is not an exact repeat, for a reason that took a measurement to find (second bullet).

- **"Same vertices" is a stamp, not a buffer.** `MeshTransformer::callGL` writes into a buffer
  whose id is stable while the values behind it change, so a buffer comparison would be a lie.
  `LayerNode::mMeshRevision` is bumped every time the skinning pass writes, exactly the reason
  `TimeKeyExpans::revision` exists one level up, and `LayerNode` passes it as `positions`. A
  resource marked by two layers (a duplicated layer) can only replay the one whose signature
  repeats last; the replay re-stamps the last copy's union, which is a superset.
- **A CAMERA MOVE RE-MARKS, and must.** The page rects do not move with a translation - the
  texcoords are the mesh's own and the level comes from the screen *edges*, where a translation
  cancels - so comparing only the linear part looked sound, and it was built and measured. It is
  not: the fragment's level comes from the hardware's derivatives of the very same screen
  positions, whose rounding follows the camera, so a mark made at one camera does not predict what
  the fragment samples at another. On nene, the fit view's mark reused across a 40 px pan left
  **263 pixels** differing from the control (**max 211**); at a 280 px pan the reused mark was
  *sharper* than a fresh one (**max 1** against the control, where the fresh mark measured
  **max 223**) - it happened to be a superset. Correct by luck is not correct, so the signature
  compares the whole matrix and every camera move re-marks.
- **The replay re-stamps, it does not skip.** The wanted set is the last `kKeepFrames` window of
  marks, so a mark that simply was not issued would let a still view's pages age out *while they
  are still sampled*: the pixels would look right until the first eviction, then blur a frame
  nobody touched. The replay sets the pages' timestamps to this frame, which is why the gate holds
  a still frame for **twice** the window and measures the wanted set: a broken replay would leave
  the pixels right until the first eviction.
- **What it does not cover.** Any camera move (see above), playback (a new time frame re-skins
  every layer, which bumps the revision), a layer move or rotate (the revision), and a device
  resize. The win is the interactive still frame - a pointer, a matte stroke, a panel redraw.

### The shader

`assets/shaders/LayerSampling.glsl` holds `sampleLayer` (the two-tap, one-level reconstruction) and
its `VIRTUAL_TEXTURE` fetch; both `LayerDrawingFrag.glsl` (the layer draw) and
`ClipperWritingFrag.glsl` (the clipper's singulation pass) `#include` it, which `gl::ExtendShader`
expands before the variation pass. The geometry is unchanged; only the fetch changes:

- one level texel -> page -> table -> slot -> atlas coordinate, with the gutter supplying the
  bilinear neighbours. Every tap resolves its own page: it can cross a page boundary, and the
  gutter only covers the read *inside* a page.
- the content's border is the gutter's zeros: a read within **one level texel** outside the content
  is still served by the page, because clamp-to-border blends the edge texel with the transparent
  outside at exactly that distance. Only a coordinate whose whole 2x2 footprint is outside returns
  0; zeroing every coordinate outside `[0,1]` dropped that half-texel blend for a reconstruction
  tap that steps just outside the content.
- a level the coarse chain owns (`level >= coarseLevel`) is resident in full, and is sampled
  from the coarse chain with the **same** two-tap reconstruction (the coarse chain's level index is
  shifted by `coarseLevel`).
  The coarse chain holds the full chain's own levels, so this is the control's read, not a
  fallback approximation. Taking one bilinear tap there instead was the whole residual difference
  from the control (816 px over 8/255, max 115, on nene's Layer 6 - see below); the cost is the
  control's eight fetches for those pixels, which the full path pays anyway.
- a table entry of 0 (a page **not** resident) samples the coarse chain at
  `max(0, level - coarseLevel)` with one filtered read: blur, never a hole, and never an undefined
  sampler. This is the transient case - the pages are wanted and arriving.
- magnification (`lod <= 0`) is one level-0 filtered read. Both paths state the level explicitly
  (`textureLod(..., 0)` on the control, the level-0 page on the paged path): the branch is entered
  on the *biased* level, so the unbiased footprint can still be minifying, and a `texture()` read
  would let the hardware blend mip levels (and filter anisotropically) that no page stack can
  reproduce. Under true magnification the two reads are identical.

The layer shader variant index gains one bit (`ShaderHolder`), and only pageable keys reserve the
VT programs.

**Clippers take the same sampler.** The clipper's singulation pass used the hardware's implicit-LOD
`texture()` read - a different filter from the layer's reconstruction at every clipped edge, and
one no page stack can reproduce (it blends mip levels and may filter anisotropically). It now
includes `LayerSampling.glsl`, so the mask and the layer it clips are one read. That changes the
mask's look on both paths (sharper, phase-consistent at the edge), and it is what makes a paged
clipper exact rather than approximate. The clipper's demand is marked by its own `render`, the
mask samples the same mesh, so the demand-only pass has nothing extra to do. The clipper's program
gains the same `VIRTUAL_TEXTURE` variation (four programs: `IS_CLIPPEE` x `VIRTUAL_TEXTURE`), and
`renderClipper` binds the coarse chain to unit 0 when the layer is paged.

**Masks take the same sampler, from their own page stack.** A matte is a field with hard edges and
painted texture, so it is sampled like the layer it masks: `LayerSampling.glsl`'s `sampleMask`
uses the same footprint/level rule and the same eight taps, with the mask's uv (the
screen->content affine, not the mesh's) and the mask's own size. Two consequences:

- **The full path changed too.** The old `texture(uLayerMask, uv)` was the hardware's implicit
  read, which a page stack cannot reproduce; both paths now read `textureLod` at the computed
  level, so a paged matte is a residency change and not a filter change (the same reasoning as the
  clipper).
- **The mask's border is `CLAMP_TO_EDGE`**, its chain's own policy, not the image's border colour:
  `MaskPyramid::fillPage` repeats the level's edge texel in the gutter outside the LEVEL, and
  `sampleMask` clamps each tap's texel position exactly as GL clamps the full chain's. A zero
  gutter would darken the matte's outermost texels against that read (the clamped coordinate's
  bilinear footprint still reaches the gutter texel); the clamp is the exact rule, not a tolerance.

The paged mask has its own R8 coarse chain, `R16UI` table and R8 atlases (units 10..15; the pool's
R8 stack is atlases 4..7, and the table stores the FORMAT-RELATIVE atlas index so one shader
branch serves both stacks), and its own demand: a **quad over the screen**, not a walk of the
layer's triangles (2026-11-07). A matte's uv is `(uScreenToLayer * vDestCoord - rect) / rect` at
every fragment - the node's SRT, never the mesh's texcoords and never the deformation - so every
fragment has the SAME Jacobian and the same level, and the pages one can sample are the affine
image of the target rect. That image contains every fragment the draw can produce wherever a
deformation puts it, and `DF1` pins both halves: the masked frame of a warped layer matches its
full-resolution control (max 0), and a deformation-only change re-marks the image while the matte
**replays** (its signature carries no mesh revision), which a triangle walk keyed on the revision
would not. A layer mask therefore uses the same entry point as a folder's matte
(`markScreenMaskDemand`); the mesh form (`markMaskDemand`) is gone.
The `MASK_VIRTUAL_TEXTURE` variation is only ever set together with
`VIRTUAL_TEXTURE`: a pageable mask on an unpaged image stays a full chain, which keeps the A/B
control a control for the mask as well (and a layer's two textures are always paged together).
The rubylith overlay is compiled from `MaskOverlayFrag.glsl` with the same variation, so the mask
tool paints a paged matte end to end - for a pageable layer mask there is no full chain at all.

### Folder mattes (2026-11-06)

A FOLDER's matte is not sampled inside a layer draw: the folder renders its subtree into an
isolated composite and multiplies that whole image by the field in one fullscreen pass
(`FilterFrame::maskTexture`, `Kind_LayerMask`). It was the last matte outside the page pool, and
it was outside it for two reasons that were both in the pass, not in the pool:

- **the read was the hardware's.** The pass took `texture(uLayerMask, uv)` - the implicit-LOD,
  possibly anisotropic read - while a layer mask had already moved to `sampleMask` on both paths.
  Two mattes of one engine therefore had different filters, and a matte's whole job is its edge,
  so the difference landed exactly where it is visible. A page stack cannot reproduce that read
  at all.
- **there was no demand source.** The layer/mask demand walks a layer's own triangles; the folder
  pass has no layer - it covers its composite. Nothing walked a quad on its behalf.

Both are closed the same way. The pass now lives in `assets/shaders/LayerMaskFrag.glsl`, includes
`LayerSampling.glsl` and calls `sampleMask`, compiled in two variations (`MASK_VIRTUAL_TEXTURE`
0/1) exactly like the rubylith; **the full path changed with the paged one**, so paging is a
residency change and not a filter change. The demand is a quad, not a mesh: a folder's pass
covers its whole target and its uv is the screen->content affine the fragment shader applies, so
`VirtualTexture::markScreenMaskDemand` draws the target rect through the SAME demand program with
`uUvFromScreen` - no second Jacobian implementation that could drift from the shader.

Two consequences the implementation had to state:

- **The target size, not the device size.** The pass's coord space is the FilterFrame's size, and
  the mapping that must be inverted is built with that same size (`waveContentToScreen(camera,
  frame.size(), ...)`). The viewport's frame is device-sized, so the distinction does not show
  there; an export or a resized frame is where it would.
- **The export preparse must mark it too.** `FolderNode::render`'s `demandOnly` branch returns
  before `renderComposite`, so the folder's own matte got no demand from the exporter - measured,
  with that call removed: the exported frame's matte was the coarse chain, max **64** of 255 at
  the matte's edge, and the wanted set was **0 pages**. The call now precedes the children walk
  in that branch. (The gate also had to age the wanted set out before purging: `resolveDemand`
  re-acquires every page still inside `kKeepFrames` even without a fresh mark, so a purge alone
  left the previous frame's set ready to be re-uploaded and the block passed with the mark
  deleted. That is a property of the pool worth knowing, not a test artefact.)

The folder matte's rubylith preview pages through the same stack (`FolderNode`'s preview branch
asks `virtualTexture()` first, like `LayerNode`'s): a pageable matte has no full chain, and the
tool paints one end to end. The control for all of this is `MaskKey::Data::setForceFullTexture`,
the mask twin of `ImageKey::setForceFullTexture`; it rides the value (`assign` copies it) because
the renderer reads an expansion's copy, not the key.

`AE_FRAME_STATS`'s `mask_bytes` is fed now (`ObjectTree::maskRamBytes`, once per frame and only
while the counter is on): each node's blended field plus the pyramid a paged form keeps, and each
filter lane's field. For a paged matte that is field + levels; for a forced-full one, field only
(its chain is a texture and belongs to `tex_live`).

### What is deliberately not paged yet

- **Filter-lane masks are not paged** (`core::FilterMask`: the HSV amount mask, the wave's two
  fields, the blur's mix mask). They are the last resource textures outside the pool. Unlike a
  node matte they are sampled by three different passes at content-space uvs, and their demand
  would have to follow each pass's screen area; the layer/folder mattes were the ones the PSD
  dataset and the target case produce, and they are done. A large painted HSV mask on a 15000²
  canvas is still a ~300 MB chain.
- **A matte's coarse tail is stored twice on the CPU.** `MaskPyramid` keeps levels 1..max, which
  includes the levels the resident coarse chain owns (they are both the page source and the
  mirror a stroke patches). At 4096² that is 5.33 MiB against a 0.33 MiB coarse tier; at 640² the
  whole pyramid is the tail (136,530 B against a 546,130 B full chain) and the duplication is
  trivial. Dropping it would mean patching the coarse chain from the field with a second CPU
  rule. Not worth it now; stated so the number is not a surprise.
- **The pyramid is per ImageKey, not per resource.** Duplicated layers that share an image still
  build two pyramids. Sharing is a RAM optimisation with a lifetime question (the resource may be
  reloaded under both keys); it is not needed for correctness. **MEASURED 2026-11-08 and left
  undone**: `render_probe --list-layers` now prints the shared-image id (`res=N`), and the corpus
  has almost no duplicates - nene has **133 resources for 133 layers**, `dinner lab` 37/37,
  `ballerina` 34 layers over 33 resources (**one** duplicated layer), `test.anie` 1/1. On this
  corpus the sharing would save one pyramid (a few hundred KB); a project that duplicates large
  layers is where it would pay, and the item stays open only for that case.
- **The exporter has a different policy, not a different renderer.** A frame is written once, so
  the exporter runs a `RenderInfo::demandOnly` pass for that frame's camera, calls
  `VirtualTexture::synchronize()` (wait on the fence, map, upload), and then draws. The first
  exported frame is as sharp as the last; the viewport keeps the asynchronous two-frame lag.

### Controls

| variable | meaning |
|---|---|
| `AE_VIRTUAL_TEXTURE=0` | the full-resolution control path (the gates' A/B) |
| `AE_VT_BUDGET_MB` | pool budget, default 256, clamped to 4 atlases (243 MB) |
| `AE_VT_UPLOAD_MB` | per-frame upload cap, default 8 |
| `AE_VT_LANDING_SPLIT=1` | `VTLANDING` per landing frame: the landing time split into pool, read (the demand map's fence interaction), parse, clear, acquire, fill, upload and table, with the page count and how many of them the stage carried (see Instruments) |
| `AE_VT_STAGE=0` | control: fill pages into a private buffer and upload them with `glTexSubImage2D` from client memory instead of through the staging buffer. Measured: upload 3.1-4.3 ms a frame against 0.10-0.15 with the stage, same pixels |
| `AE_VT_COARSE` | longest side of the always-resident chain, default 512 |
| `AE_AM1_FIELD=<n>` | gate measurement, not a control: the AM1 animated matte's field is n x n (default 900x700). Release: view 2.76 ms against materialized 2.39 at 900x700, 4.38 against 7.60 at 2048², 6.56 against 31.33 at 4096² (see Measured) |
| `AE_VT_STATS=1` | one line per frame: resources, wanted, resident, pool pages/bytes |
| `AE_VT_DEMAND=0` | diagnostic: skip the demand pass and its readback (every paged layer samples the coarse chain through the VT shader) - the control for "is the cost the shader or the demand machinery" |
| `AE_VT_DEMAND=nodraw` | diagnostic: keep the pass's state changes, uniforms and per-layer buffer uploads and skip only the draw - the control for "is the cost the setup or the geometry" (measured: the geometry) |
| `AE_VT_STATS=1` also prints `marks=` and `replays=` | how the frame got its demand: a mark is one layer's triangles walked, a replay is a signature repeat that walked nothing |
| `AE_VT_VERIFY_PAGES=table` | diagnostic: reads every resource's page table back and compares it with the CPU mirror, entry by entry (cheap; the open list's first item) |
| `AE_VT_VERIFY_PAGES=1` | the same plus every resident page's slot against the page's own bytes, and the entries against the pool; SLOW (a readback per page), inside the frame |
| `AE_VT_VERIFY_PAGES=end` | the slot + entry-vs-pool comparison **once, after the last frame**, with nothing in flight - the form that can run on a defective frame. Prints `VTPAGECHECK ... checked= bad= mirrorBad=` per resource |
| `AE_VT_DUMP_PAGES=1` | with `=end`: the final frame's page->slot layout, one `VTPAGEMAP` line per page (114 on nene). Two runs of one command are supposed to print the same lines |
| `AE_VT_MISS_MAGENTA=1` | diagnostic: the paged read's coarse fallback returns MAGENTA (no table entry) or CYAN (an entry that names nothing usable) instead of the coarse chain - "which pixels did not find their page" as a picture, with no readback and no stall |
| `AE_VT_ATLAS_SENTINEL=1` | diagnostic: every new atlas is filled with MAGENTA, so a read of a texel no page upload wrote is unmistakable (`glTexImage2D` from a sentinel buffer, one upload per atlas) |
| `AE_VT_PAGE_HASH=1` | diagnostic: hashes each level-0+ page's fill against the pyramid's own bytes (the source side of the upload) |
| `AE_VT_AFFINE_DEMAND=0` | control: walk the mesh for a layer with no deformation instead of marking its quad (measured: 99.5-114.7 ms a playback frame against 90.3-97.5, and 5,834 pages demanded at 2x against 18) |
| `AE_VT_DEMAND_SPLIT=1` | diagnostic: per paged layer, its demand triangles, whether the affine predicate accepted it, and whether it is a bone INFLUENCE target (`VTDSPLIT layer= tris= affine= fast= infl=`); `infl=1 affine=1` is a skinned layer on the quad |
| `AE_VT_DEMAND_NOCULL=1` | control: disable the demand walk's screen-bounds cull, so every triangle marks its uv box whatever the camera sees. Measured on ballerina at a 640x640 zoom-1 view: 482 pages wanted against 250, with **bit-identical** frames - the control for "how much of the walk's demand was off-screen" |
| `AE_NO_MIP_TAIL=1` | diagnostic: skip the GPU area-weighted mip tail and keep `glGenerateMipmap`'s levels (the chain becomes reproducible and visibly coarser: 20,036 samples / max 52 from the control on nene) - the switch that confirmed the tail build as the nondeterminism's source |
| `AE_MIP_TAIL_OLD_PATH=1` | diagnostic: the MUTATION CONTROL for the fix - build the tail by rendering into the chain, the read-while-write the fix removes. Two runs of one command then differ (325/305 samples from the control, 109 apart, max 223 in the strip) |
| `AE_VT_HASH=<level>\|all` | diagnostic, at load: hashes one level (or every level) of EVERY GPU chain just after it is built (`VTHASH <W>x<H> level= chain=`). Two runs of one command are supposed to print the same lines - and did for 240 of 241 chains (see Measured) |
| `AE_VT_DUMP_CHAIN=<W>x<H>` (+ `AE_VT_DUMP_CHAIN_DIR`) | with `AE_VT_HASH`: writes that resource's level to a PNG, so two runs can be diffed TEXEL BY TEXEL |
| `AE_VT_HASH_COARSE=1` | diagnostic, after the last frame: every level of every GPU coarse chain (`VTCOARSE ... coarse= level= gpu=`), to be compared level by level with `AE_VT_HASH=all` |
| `AE_TRACE_DEST=1` | diagnostic, per layer draw: `VTDEST layer= blend= clippee= refreshed= mask= maskPaged=` - whether this draw refreshed the destination its blend reads, and whether it takes a clipped or masked variant |
| `AE_VT_TAG_LAYER=1` | diagnostic: each draw outputs an opaque colour derived from its name and prints `VTTAG layer= tag=`, so the colour at a pixel names the layer on top there (how `Layer 19 Copy` was found) |
| `AE_VT_DEBUG_LEVEL=1` | diagnostic: each fragment outputs `(level/16, lod/16)` instead of the read, on BOTH paths - the two programs' level fields can be compared pixel for pixel |
| `AE_VT_DEBUG_UV=1` | diagnostic: each fragment outputs the uv it samples, on BOTH paths - compares the read's GEOMETRY rather than its result |

`AE_FRAME_STATS`'s line gains `vt_pages`, `vt_bytes`, `vt_up`, `vt_evict`, and `vt_frame_ms` - the
time `VirtualTexture::beginFrame` spends this frame parsing the demand copy, filling and uploading
the pages the frame will sample, and publishing the tables. That is the LANDING cost; `ms` includes
it, and the shader cost is what remains.

### Gates (all in `tools/verify_mipmap`; a pixel gate runs under `scripts/gui/gpu_headless.sh`)

- **V1 paged layer vs the full-resolution control**, at scales 0.25, 0.0625x0.25 (a 4:1 footprint
  on the coarse tier), 0.3 (level 0 paged, the layer's edges on screen), 0.5, 0.6 (the magnified
  branch entered while the unbiased footprint is minifying) and 1.0, plus a no-holes case that
  purges the pool and forbids a new atlas. The paged and control frames are compared pixel for
  pixel; the scale sweep also asserts how many pages the paged levels demand, that all of them are
  resident after eight frames, and that the pool has not silently served the frame from the coarse
  chain. Tolerances are 1 unit (2 for the on-edge 0.3 case): the fixture measures 0 for the
  paged and magnified cases under `gpu_headless` and 1 on the Xvfb/xcb path's sub-texel rounding,
  while each bug class measures 23-53 on it (see Measured).
- **P0 the page fill's premultiply** (2026-11-08): the conversion every texture upload and every
  level-0 page goes through, against an independently written `(c*a + 127) / 255`, over all 65,536
  (channel, alpha) pairs, then over six whole pages - interior, right edge, below the image, a page
  whose right gutter holds the level's first two columns, and one wholly outside on each side -
  each destination wrapped in a canary. It is the gate the pixel comparisons cannot be: both sides
  of every paged/control frame share the conversion, so a wrong lane would move them together.
  Mutations that fail it: the old memset order in `fillPage` (2 checks, the two outside pages, and
  no crash) and a wrong alpha broadcast in the vector path (4 checks here, 19 across the suite).
- **P4 the upload stage** (2026-11-08): a page filled into the mapped stage must be the slot's
  bytes after `endStaging()`, for BOTH formats and at each one's own page size (so a wrong offset,
  stride or format cannot pass); a full stage must refuse the page and keep refusing, which is the
  direct fallback's path; and the counters the gates read must count exactly the copies. Mutation:
  shifting every staged copy by 16 bytes fails P4's two byte checks and the pixel gates
  (max 230-255). `AE_VT_STAGE=0` runs the suite green on the direct path with P4 skipped.
- **M1b chain determinism**: builds one source's chain twice with a full readback of the first in
  between and requires the two chains to be bit-identical (9 levels here). It is a STRUCTURAL
  guard, not this defect's gate: measured, the old tail construction passes it too (worst channel
  0), because inside one process the read-while-write is stable - the defect only appears across
  processes. The gate with power is the two-run comparison under Measured, with
  `AE_MIP_TAIL_OLD_PATH=1` as its control.
- **CL1 clipper singulation**: a 900x700 disc clipper over a 900x700 opaque clipped layer, both
  paged, against the full-resolution control. Asserts both keys took the paged path, the clipper's
  pages were demanded and are all resident, the paged pair is within 2/255 of the control (the
  disc's mask edge), and the mask is doing something: the clipped frame leaves a corner empty that
  an unclipped render covers. Without the clipper-demand check the gate would pass with the mask
  served from the coarse chain.
- **MA1 masked layer on the paged path**: a 900x700 painted mask on a paged layer against the
  full-resolution control, plus the assertion that the MATTE itself took the paged path (an R8
  resource at the field's grid, wanted pages > 0, every wanted page resident after eight frames).
  The frame delta is 1/255 (mean 0.047), the residual the image's paging.
- **MX3 paged matte stroke**: paints the middle band of a live paged mask hidden, re-blends the
  expansion (the editor's own notification), and compares against the full chain rebuilt from the
  same field: max 0, mean 0. The painted-over and previously revealed bands must be empty and the
  untouched part visible, so a stale matte or a skipped box update fails the gate. It also renders
  the tool's RUBYLITH (`FilterFrame::maskOverlayTexture` with a `MaskInput::paged`): the overlay
  must tint the hidden band and not the revealed part, and the paged rubylith must match the
  full-mask one within 3 units.
- **MX4 mask pyramid memory**: a production 4096² paged mask (`MaskKey::Data::virtualTexture`)
  asserts the CPU pyramid holds EXACTLY the chain's levels above 0 (5.33 MiB, not 21.33), that it
  is smaller than the level-0 field it no longer copies, and that the resident coarse chain is
  the 512² tier.
- **AM1 an animated mask** (2026-11-08): two mask keys on one grid (left-hidden at frame 0,
  right-hidden at 24), so every frame between them is the per-texel blend. Asserts the matte is
  **applied at all** against the same tree without a matte - the flag this pins was a regression
  that made an animated matte invisible - that the paged form keeps **one pool resource across
  frames** and materializes **no field**, that frame 6 equals the materialized full-chain control
  **pixel for pixel (max 0)**, and that a **stroke** on one key reaches the view (the next blend
  rebuilds that key's pyramid and re-points the view; the painted frame must still equal the
  control). It also reports the frame time of the view, of the same frame drawn again (the cached
  static cost) and of the materialized path - reported, not asserted, because a wall-clock gate is
  machine-dependent. `AE_AM1_FIELD=<n>` makes the field n x n for the scaling measurement; the
  shipped fixture is 900x700 (Debug sees ~5.7-5.9 against ~3.9-4.1 ms there, the view losing to
  the materialized path on a matte that is smaller than the view - see Measured).
- **P1/P2 page pool**: slot 0 at the gutter origin, slot 1 one pitch over, slot 15 the first of
  the second row, a slot's texels read back exactly; the per-frame upload cap; eviction order and
  the owner callback; cancel returning a slot and refunding its bytes; purge changing the
  generation and releasing every atlas.
- **FM1 a folder's matte on the paged path**: a composite folder (a painted matte, so the folder
  takes the isolated-composite path) over a 900x700 layer, 640x640 canvas and a 640² field at
  1:1, against a tree whose matte is forced full (`MaskKey::Data::setForceFullTexture`). It
  asserts the reasoned failures, not just "the frames agree":
  - **the pages are the demand pass's**: at 1:1 a 640² field needs exactly its level-0 3x3 grid,
    so `wanted == 9`, every one resident after eight frames, coarse level 1 of 9. A resource
    built by an accessor alone would have `wanted == 0`; the gate would fail before the compare.
  - **the paged frame equals the control** (max 0, mean 0.000) and the matte is active in both
    (left half empty, right half covered).
  - **the demand's mapping, not just its count**: a scale-2 camera centred on content (400, 480)
    shows content 120..440 x 80..400, so exactly 4 of the 9 level-0 pages are wanted (a demand
    that marked the quad's whole screen rect would say 9, a mirrored one would mark the
    complementary pages and leave one sampled page out of the pool), and the frame still
    matches the control (max 0).
  - **the rubylith pages too**: the paged preview tints the hidden half red, leaves the visible
    half white, and matches the full-mask overlay within 3 units (measured 0).
  - **no holes with the pool emptied** (budget 1, purge): the coarse chain still hides and still
    shows.
  - **the export preparse**: blank frames age the wanted set out, the pool is purged, and
    `RenderFixture::renderExported` runs the exporter's exact sequence (demand-only, synchro-
    nise, one draw). The matte's pages must all land and the exported frame match the control.
    With the demand-only folder mark removed this case measures **0 of 0 pages** and **max 64**
    at the matte's edge - the gate has power, checked by that mutation.
  - **`mask_bytes`**: the paged tree's `ObjectTree::maskRamBytes()` must equal field + pyramid,
    the forced-full one field only - the frame line's number is the walk, and the two would be
    equal if either side were miscounted.
- **DR1 the demand replay**: a 1200x1000 source (coarse level 2, so level 1 is pageable too) on a
  320 canvas with the layer at 0.3, so a camera change can move the demanded *level*. The first
  render must MARK; the fit view must want **exactly its level-0 grid** (20 pages) with all of it
  resident. **Six more repaints** (twice `kKeepFrames`) must mark **nothing**, must still replay,
  must keep the wanted set at 20 with every page resident, and must be bit-identical to the frame
  the mark produced. Then the camera goes to half scale - footprint 0.15 texels per pixel,
  `log2(1/0.15) - 1.35 = 1.39`, so the fragment shader reads level 1: that frame must MARK again,
  the wanted set must become **exactly level 1's grid** (6 pages, not level 0's 20), and the frame
  must match a tree that never replayed. A **120 px pan** of the same view must **re-mark** (not
  replay) and want the same pages - the decision the replay's measurement forced (see the replay
  section). The comparison's tolerance is **1 unit at under 0.1 % of samples**: two independently
  built paged resources of the same content land in different pool slots, and the atlas read's
  sub-texel rounding follows the slot (two oracle trees that never replayed differ by 1 at 133
  samples too). Mutating the signature to compare only the vertex stamp fails five checks, the
  pixel one at **max 28 over 75,904 samples**.
- **DF1 a deformed layer's demand** (the case the affine path cannot answer, and the one no project
  in the dataset provides): a 2048x2048 noise source on a 320 canvas at **zoom 4**, a paged matte on
  its left half, and **two** FFD keys - frame 0 translates every grid vertex by **768 content px**,
  frame 10 by **+640**, so the second phase is a deformation change with nothing else moving. It
  asserts, in this order: the FFD key moved what is drawn (the frame differs from an identical tree
  with no FFD key by **> 64** in a band the matte leaves visible - without it the parity check would
  compare two rest-grid frames and pass whatever the demand did); the paged frame matches the
  full-resolution control (**max 0, mean 0.000**), which covers the matte too because forcing the
  image full also drops the draw's mask variation to its full chain; the image's demand is the
  visible **2x2** page set and not the layer (**4 wanted of the 64-page level-0 grid**, all
  resident), which is the cull; the matte's demand is the screen's affine extent (**4 pages**, all
  resident); and the tree stayed paged (no full texture). Then the **frame-10 phase**, the property
  no pixel test can see: a deformation-only change must **mark the image and replay the matte**
  (`marks=1, replays=1`) - a matte's pages are a function of the screen->content affine, which a
  deformation cannot change - and the second warp's image + matte must match its full control
  (**max 0, mean 0.000**). The mutations fail it: the affine path forced measures **max 138, mean
  28.4**; the cull off wants **all 64 pages**; and keying the matte's signature on the skinning
  revision (a stand-in for the mesh walk it replaced) measures **marks 2, replays 0**.
- `media_exporter_e2e` runs the export path with the demand preparse.

### Measured

- **nene.anie** (15000x15000, 130 layers, now 108 paged resources - the three clipper layers
  included, 640x640 fit view), the same frames with `AE_FRAME_STATS`, after the demand set
  settled:

  | | full resolution | paged |
  |---|---|---|
  | `tex_live` | 2,778,072,136 B (2.59 GiB) | 133,814,840 B (128 MiB) |
  | steady `ms` | 93.5 | 96.4-98.0 |
  | load (project load + frames, wall) | 79.4 s | 74.9 s |
  | max RSS | 8.08 GB | 8.55 GB |

  The VRAM is **20.8x lower**. The paged `tex_live` is one 64 MiB atlas holding 30.8 MB of pages,
  the always-resident coarse chains, and the images too small to page: the **443 MiB of clipper
  layers** (one 4343x4351, two 2702x12604) that dominated the old 569 MiB figure are now paged.
  They add 35 pages (114 in all) and 115 MB of pyramids (summed from their level sizes), and the
  paged RSS measured 8.55 GB - lower than the 8.63 GB from before they were paged, although the
  pyramid total rose from 609 to 724 MB; that measurement is one session's, and the row's control
  figure is an earlier one.
  The paged steady state is 96.4-98.0 ms (the last three frames of a `--frames 3..10` run, after
  all 114 pages landed) against the control's 93.5 ms for the same frames and view. The ~3 ms gap
  is compared across sessions (a later same-session control run was unusable under the machine's
  browser load) and includes the clipper pass's new reconstruction, which now costs the layer
  draw's eight taps over the masks' screen area instead of the old single hardware read.
  The demand is the triangle's OWN level and a page box grown by the tap reach in level texels, not
  by a whole page. The first version marked the level either side and a full-page border: 474
  pages (128 MiB) on nene, and (measured before the coarse-tier fix) it did **not** bring the frame
  closer to the control (mean 0.83 of 255) than the tighter set did (0.61). The page set is 114
  (the layers' 79 plus 35 the clippers demand); at the 8 MB/frame default they land in four frames
  (31/62/93/114 measured), with the landing frames at 100-104 ms against the 96-98 steady. A 32 MB
  cap was measured first and is why the default is 8: the frames that spent it cost 60-140 ms. The
  page fill copies the level's rows (one memcpy per row) instead of a memcpy per texel, which was
  most of that upload frame.
  The pyramids are 609 MB for the layer resources plus 115 MB for the three clippers (summed from
  their level sizes); a clipper no longer rebuilds a full chain and capture when the policy
  changes, because there is no policy. Load time did not regress (74.9 s against 79.4), and the
  pyramid capture is SYNCHRONOUS: it cost 1.47 s across the 136 keys, worst 130 ms for one
  3841x12658 image (a 15000² import scales to ~0.6 s, inference from the 48 MP sample). The three
  clippers add ~87 MP of capture, ~0.23 s at that rate (inference).
  **The residual difference was the coarse tier, not the page path.** With `AE_VT_DEMAND=0` (no
  page uploaded at all, every paged level falling back to the coarse chain) the same-frame
  difference from the control was byte-for-byte the same 816 px over 8/255, max 115; with every
  wanted page resident it was the same again; and the paged frame against the no-demand frame
  differed by max 2, mean 0.0023 of 255 - the atlas read was already exact. The shader was taking
  ONE bilinear tap for levels the coarse chain owns, where the full path runs the two-tap
  reconstruction: a different filter, visible only on fine hatching. The coarse tier now takes the
  same eight taps from the coarse texture, at the level index shifted by `coarseLevel`. Isolated to
  Layer 6 exactly (`--only-layer '=Layer 6'`, 4408x4370, 640x640) it is **bit-identical**. The
  magnified-branch read below closed the last 1-unit edge pixels, and the whole 640x640 frame now
  measures **0 pixels over 8/255, max 1, mean 0.00041 of 255** (168 pixels differ by 1) once the
  114 pages have landed (frames 7-8). The landing frames (5-6) show up to 3072 px over 8 - the
  page transient, not a sampling difference. The plan's pixel-equality gate is met on this project,
  clipper layers included.
- **The demand replay** (`DR1`): the fit view wants exactly **20 of 20** level-0 pages, all
  resident; six repaints of the same frame mark **nothing** (one replay each), keep the wanted set
  at 20 - past twice `kKeepFrames` - and are bit-identical to the frame the mark produced. At half
  scale the demand is exactly level 1's **6 pages**, and the frame matches a fresh tree within
  **max 1 at 97 of 409,600 samples** (the slot-rounding tolerance above). A **120 px pan re-marks**
  and wants the same 6 pages - the gate pins that a camera move is not replayed (see the replay
  section for why). With the signature reduced to the vertex stamp the gate fails five checks: the
  level-1 view wants **20 pages instead of 6** and the frame differs by **max 28 over 75,904 of
  409,600 samples**.

- **The affine demand path: a quad instead of 1.74 M triangles** (2026-11-07). A layer with no
  deformation has ONE Jacobian over its whole surface, so the layer shader's uv is an affine
  function of the screen position and the pages its fragments can sample are the **quad's page
  extent** - the same union the mesh walk computes, because every triangle of a grid shares that
  Jacobian - at four vertices instead of the grid's triangles. `AE_VT_DEMAND_SPLIT=1` measures the
  split on nene at frame 6: **all 107 paged layers qualify, and they own all 1,746,491 triangles**
  the pass walked every frame (the predicate is deliberately conservative - a mesh key, an FFD
  *key* (not `ffd()`'s position storage, which the blender always fills), a bone binding or a
  non-identity wave each count as deformation - and a degenerate screen->content mapping falls back
  to the walk). `markScreenDemand` reuses the folder matte's fullscreen quad path, so the demand
  takes the SAME screen->content affine the fragment shader applies (plus the draw's own texel
  offset, without which an offset image marks a shifted rect - measured at 2x, where it cost
  92,701 samples).
  - **Cost**: a playback frame (nene, `--frames 5..12`, `AE_FRAME_STATS`) went from
    **99.5-114.7 ms** with the walk to **90.3-97.5 ms** with the quad - the pass's 5.7-11.1 ms is
    gone, and it is the demand's whole geometry, not a fraction of it;
  - **it also fixes paging at magnified cameras**: at `--zoom 2` the walk demanded **5,834 pages**
    of which the pool could ever land **217**, so most of the view fell back to the coarse chain -
    **92,701 samples, max 90** from the full-resolution control - while the quad demanded **18**
    (all resident) and matched the control within **max 1**. The walk marked every page of a layer
    at the level its triangles computed whatever the camera saw; the quad marks what the camera
    sees;
  - at the fit view the quad still matches the control (**max 1**, 153 samples of 1) and wants
    **123** pages against the walk's 114 - the difference is the per-layer bbox being a superset of
    the triangles' union at the edges, which is safe by construction.
  `AE_VT_AFFINE_DEMAND=0` walks the mesh instead and is the control. `DR1` was updated with it: a
  pan still re-marks, but the page SET may follow the view, and the gate now pins the level
  (never level 0's grid), residency, and the oracle's set equality instead of the old
  whole-grid count, which was an artefact of the walk. `V1` gained a **2.0 (magnified)** case.
- **The landing is measured, and its two CPU terms are gone: the fill vectorised and the upload
  staged** (2026-11-08; the fix for both levers of the open list's item 3). `vt_frame_ms` dated a
  landing but could not divide it, so `AE_VT_LANDING_SPLIT=1` prints the split the same way
  (`VTLANDING frame= pages= staged= pool= read= parse= clear= acquire= fill= upload= table= other=
  total=`, ms). On ballerina at a 640x640 zoom-1 view, Release, EVERY landing page is a level-0
  page (31 a frame at the 8 MB cap), and a landing frame went like this:

  | 31 pages a frame | 2026-11-07 | fill vectorised | fill + stage |
  |---|---|---|---|
  | fill | 6.4-13.6 ms | 2.2-3.1 ms | 2.8-3.2 ms |
  | upload | 3.2-3.5 ms | 3.1-4.3 ms | **0.10-0.15 ms** |
  | read (the demand map's fence interaction) | 1-10 ms | 0.1-9.6 ms | 0.1-9.8 ms, NOT addable |
  | parse / clear / pool / acquire / table / other | < 0.7 ms | < 0.7 ms | < 0.7 ms |
  | `vt_frame_ms` | 13.0-25.9 ms | 7.0-16.1 ms | **4.2-13.3 ms** |

  **THE UPLOAD'S COST WAS THE DRIVER'S STAGING COPY, and the fix is to stop making it.** A
  standalone benchmark of gl::PagePool's own atlas shape (4096^2 RGBA8, 260-pitch slots, LINEAR,
  no mips; 31 pages a frame, the uploads timed, a glFinish after them) measured four things:
  31 `glTexSubImage2D` calls from client memory cost **2.8 ms of CPU and 4.0 ms with the finish**
  (0.091 ms a page); the same 31 pages with the atlas SAMPLED by a draw first cost the same
  (2.81-2.96 ms either way - the write-after-read hazard is not the cost, so an atlas ping-pong
  would buy nothing); batching a whole atlas row into one call, 3 calls for 31 pages, costs
  **1.8-2.0 ms** (0.059-0.065 a page); and 31 calls **from a buffer object** cost
  **0.00 ms of CPU and 1.05-1.12 ms with the finish** - the GPU does the copy and the CPU only
  queues it. Filling into the mapped buffer itself is as cheap as filling into cached memory
  (0.88 ms against 0.91 for 8.4 MB of 4-byte stores), which is what makes the stage possible.
  So a landing frame now fills its pages into a MAPPED staging buffer and copies them to the atlas
  from there (`AE_VT_STAGE=0` is the direct-upload control): in situ the upload phase went from
  **3.1-4.3 ms to 0.10-0.15 ms a frame** (31 pages, `staged=31`), and the fill from 2.2-3.1 to
  2.8-3.2 (writing into the map is a little dearer than writing into a hot 270 KB buffer). Net:
  **~3 ms a frame off the landing's CPU**, and the frame's own `ms` improves by 1-3 ms where the
  frame is CPU-bound (on ballerina it is often GPU-bound, so the landing's `vt_frame_ms` shows the
  saving more clearly than `ms` does). `P4` in `verify_mipmap` is the gate: a page filled into the
  stage must be the slot's bytes after `endStaging` for BOTH formats at each one's own page size, a
  full stage must refuse (and keep refusing) so the caller's direct fallback is exercised, and the
  counters must count exactly the copies; shifting every staged copy by 16 bytes fails it AND the
  pixel gates (max 230-255), and `AE_VT_STAGE=0` runs the whole suite green on the direct path
  with P4 skipped.

  **The `read` row is not CPU work the landing costs.** Splitting it further put it in the SYNC
  QUERY, not the copy or the parse: the PBO map is 0.02 ms and the per-resource parse 0.02-0.05 ms,
  while `glClientWaitSync(fence, 0, 0)` - a zero timeout, i.e. a poll by the spec - measured
  2.9-4.5 ms and returned `GL_CONDITION_SATISFIED`, and `glGetSynciv(GL_SYNC_STATUS)` measured
  2.8-4.4 ms and returned `GL_SIGNALED`: the driver surfaces the previous frame's command buffer at
  the first fence interaction. That it is a RELOCATION and not a cost was then measured directly:
  removing the sync entirely and mapping regardless (a measurement-only control, never shipped)
  leaves the frame's `ms` unchanged across four alternating runs - sync 51.7/33.8/26.5/26.7/31.7/
  28.0/22.4 and skip 52.8/32.4/26.7/27.2/32.2/28.3/22.6, then sync 53.3/31.8/26.7/26.8/32.0/28.0/
  22.2 and skip 52.6/31.2/27.5/26.7/32.9/28.6/23.3. So the landing's addable cost is fill + upload
  + ~0.3 ms, and a one-frame demand lag would buy nothing.

  **The `read` row is not CPU work the landing costs.** Splitting it further put it in the SYNC
  QUERY, not the copy or the parse: the PBO map is 0.02 ms and the per-resource parse 0.02-0.05 ms,
  while `glClientWaitSync(fence, 0, 0)` - a zero timeout, i.e. a poll by the spec - measured
  2.9-4.5 ms and returned `GL_CONDITION_SATISFIED`, and `glGetSynciv(GL_SYNC_STATUS)` measured
  2.8-4.4 ms and returned `GL_SIGNALED`: the driver surfaces the previous frame's command buffer at
  the first fence interaction. That it is a RELOCATION and not a cost was then measured directly:
  removing the sync entirely and mapping regardless (a measurement-only control, never shipped)
  leaves the frame's `ms` unchanged across four alternating runs - sync 51.7/33.8/26.5/26.7/31.7/
  28.0/22.4 and skip 52.8/32.4/26.7/27.2/32.2/28.3/22.6, then sync 53.3/31.8/26.7/26.8/32.0/28.0/
  22.2 and skip 52.6/31.2/27.5/26.7/32.9/28.6/23.3. So the landing's addable cost is fill + upload
  + ~0.3 ms, and a one-frame demand lag would buy nothing.

  Debug, the same view and frames: fill 12.7-21.3 ms and a 21.9-28.4 ms landing, against the 49-83
  ms the item used to record. The fill column is the whole change. `ImagePyramid::fillPage`
  premultiplied level 0 one byte at a time; the conversion is now `img::premultiply`, four texels a
  vector (SSE2; the scalar form is the fallback and the byte-for-byte reference). Measured per page,
  THREE-WAY A/B/A IN ONE BINARY so the session's load cancels:

  | one level-0 page's fill | min | median | max |
  |---|---|---|---|
  | the branchful loop it replaced | 0.091 | 0.239 | 0.295 |
  | the branchless scalar form | 0.090 | 0.239 | 0.305 |
  | SSE2 | **0.052** | **0.087** | 0.160 |

  Two readings worth keeping. **The `a == 255` branch was not the cost in situ**: the scalar form is
  indistinguishable from the branchful loop - although on a hot 4096^2 image in a standalone
  benchmark the branchful form measured 0.273 against 0.092, a gap that was mostly the per-texel
  bounds assert it carried (0.167 with the assert hoisted) rather than the branch. **The vector path
  wins because the source rows are cold**: a page reads 256 rows 1 KB wide out of a 77 MB layer, strided by the layer's own width,
  and four texels a load hide a latency that one texel a load exposes. The fill is now at the price
  of the level-1+ path's memcpy of the same 270,400 bytes (~0.09 ms a page in situ), so the
  conversion has no headroom left on this lever.
  `P0` in `verify_mipmap` is the gate, and it exists because the pixel gates cannot be: BOTH sides
  of every paged-versus-control frame go through this one conversion, so a wrong lane or rounding
  would move both paths together. It compares the dispatched form and the scalar fallback against
  an independently written reference over **every (channel, alpha) pair** (65,536 texels), then
  six whole pages - interior, the right edge, below the image, one whose right gutter holds the
  level's first two columns, and one wholly outside on each side - against a reference page built in
  the probe, with a CANARY on each side of the destination. The canary earned its keep immediately:
  for a page wholly OUTSIDE the level the row's run is empty in a way that makes the memsets' own
  lengths negative or over-long, and `fillPage` ran them BEFORE the empty-run check - a page to the
  right (negative `x1`) wrote up to 840 bytes before the row, i.e. corrupted the heap, and one to
  the left (`x0` past the row) wrote past it. The demand pass only ever marks in-grid pages, so the
  pool never asks for either; the fill still must not overrun its buffer, and
  `MaskPyramid::fillPage`'s edge-repeat loops got the same clamp. Mutations run: the old memset
  order fails the canary check cleanly (2 failures, one per outside page, and no crash), and a wrong
  alpha broadcast in the vector path fails the pair check (19 failures across the suite).
  `verify_mipmap` is **379 checks, 0 failures** in Debug and Release.
- **A deformed layer's demand: the FFD predicate and the screen cull** (2026-11-07, the residual
  the affine path left open). The affine path's FFD test was dead code - it asked the type of the
  key `ffdMeshParent()` returns, which is the ImageKey or MeshKey the FFD keys displace, never an
  FFD key itself - so every FFD-warped layer took the quad, whose mapping is the rest grid.
  `DF1` is the fixture that shows what that costs: a 2048^2 layer at zoom 4 under an FFD
  translation of three level-0 pages measures **max 138 / mean 28.4** against the full-resolution
  control with the quad, and **max 0 / mean 0.000** with the predicate fixed (4 wanted pages of
  the 64-page level-0 grid, all resident, and the warp moves the frame by 217 at the centre).
  The walk then over-demanded, because its union is the LAYER whatever the camera sees, so the
  geometry shader now culls a triangle whose screen bounding box misses the target
  (`AE_VT_DEMAND_NOCULL=1` is the control): on ballerina at a 640x640 zoom-1 view, frames 8-13,
  the same walk wants **482 pages without the cull against 250 with it**, and the frames are
  **bit-identical** - the extra 232 were off-screen triangles of the FFD layers, pure
  over-demand. The old quad path wanted 250 on that view too, and its frames are also identical
  to the walk's, so on that project the fix changed no pixel and no page count; the warp there is
  small enough to stay inside the quad's extent, which is why DF1, not a project, is the gate.
  The walk's frame-time cost on ballerina is inside the project's own variance (55-94 ms with the
  walk against 53-99 with the quad over the same six frames); the cull does not reduce it, because
  the geometry shader still runs per vertex - it reduces the page set the pool is asked for.
  `verify_mipmap` is 351 checks, 0 failures; both mutations above were run and fail the gate.
  The same reading found the predicate's BONE half, and measured its population: `infl=` on
  `dinner lab` at frame 0 reports **nine influence-skinned layers, seven of them on the quad**
  (every descendant of an active BoneKey is skinned per vertex by `callGL`'s `useInfluence` path,
  while `isAffectedByBinding` only names the rigidly bound ones - whose draw is a matrix and whose
  quad would be exact). Adding `bone().targetMesh() != nullptr` moves all nine to the walk;
  **wanted 7 -> 5 pages**, and the project's frames are unchanged (max 1 at 7-18 samples, the slot
  rounding, frames 1-8) - so no project in the dataset makes that half visible in pixels at these
  cameras, and it rests on the draw path plus the population count, not on a failing pixel gate
  (a bone fixture is the open work).
- **A matte's demand is a quad, not a mesh walk** (2026-11-07). The mask's uv is
  `(uScreenToLayer * vDestCoord - rect) / rect` at every fragment - the node's SRT, not the mesh's
  texcoords and not the deformation - so it has ONE Jacobian and ONE level, and the pages a
  fragment can sample are the affine image of the target rect. `markMaskDemand` (the per-triangle
  walk) is deleted; a layer mask and a folder matte now share `markScreenMaskDemand`, which is also
  why the matte's signature carries no mesh revision: a deformation-only change re-marks the image
  and **replays** the matte (`DF1` asserts `marks=1, replays=1`; keying the matte's signature on
  the revision measures `marks 2, replays 0`). The quad marks a superset of the walk's set - the
  whole target's affine extent - which is bounded by the screen's own page footprint (a 640px
  view is 2-3 level-0 pages per axis whatever the mask's size), so the over-demand is a handful of
  pages, and `MA1`/`MX3`/`FM1` keep their measured parity (MX3's 900x700 matte still wants the
  same **9 pages**). The real masked PSD (`000145.psd`, 24 masks, 6 pageable) re-measures
  **max 1/255, 0 px over 8, 75 samples of 1** once the pages land (frame 4 on; frames 0-3 are the
  landing transient), the same slot-rounding class as before the change.
- **The mip tail's read-while-write is what the panned camera exposed** (found while testing
  whether a pan could replay; **fixed**). `buildMipTailOnGpu` built each level of a chain with the
  chain attached as its own render target and the source level fetched from the same texture. The
  spec's feedback rule is per IMAGE, so that is legal on paper - but this driver resolves a
  whole-texture write-while-sample by renaming, and the result then depends on what else is in the
  command stream. The paged path's pyramid capture reads every level back immediately after the
  build, and that readback is what exposed it:
  - with virtual texturing, `AE_VT_HASH=all` prints the same hashes on two runs for **240 of 241
    chains but not for `384x1715`**, where **all ten levels differ** (`level=2`
    `cde1be62...` against `e59d2916...`); with `AE_VIRTUAL_TEXTURE=0` the same two runs print
    **identical hashes at every level** (0 differing lines);
  - with the tail build switched off (`AE_NO_MIP_TAIL=1`) the paged frames of two runs become
    **bit-identical** (0 samples) - but they then sit **20,036 samples / max 52** from the control,
    because `glGenerateMipmap`'s tail is the wrong filter, which is why the tail exists;
  - a `glTextureBarrier()` between the passes and a single `glFinish()` after the build were both
    tried and **did not remove it** (the hashes still differed at all ten levels).
  The fix: each level is rendered into a **scratch texture** (the size of the largest tail level,
  1/4 of the base at most) and blitted into the chain level, so the pass samples the chain and
  writes the scratch and the blit reads the scratch and writes the chain. Measured on nene at
  `--offset 280 --repeat 8`: the two runs are now **bit-identical**, both sit **max 1 (169 samples
  of 1) from the full-resolution control**, and the **18x69 strip is empty** - against **max 223
  with the runs 109 samples apart** before. The mutation control `AE_MIP_TAIL_OLD_PATH=1` (render
  into the chain, as before) reproduces the defect on both runs (**325 and 305 samples, max 223**,
  **109 apart**), so the comparison has power.
  What is *not* the cause, each measured on a defective frame before the fix: the page table, the
  entries against the pool, the slot contents, the page->slot layout (two runs printed identical
  114-line layouts), the tier (`AE_VT_DEMAND=0` and `AE_VT_COARSE=64` both still showed it), the
  layer (named `Layer 19 Copy` with `AE_VT_TAG_LAYER=1`; its uv and level were bit-identical
  between the paths and its coarse chain matched 9 of 9 chain levels byte for byte), the
  destination texture (all 130 layers are Normal, so `blendColor` cancels it), and the mask tier
  (nene registers zero mask resources).
- **A panned camera exposed a TIMING-SENSITIVE wrong read** (pre-existing; found while testing
  whether a pan could replay; superseded by the finding above - this is the record of how it was
  chased and what each step ruled out). On nene at the 640x640 fit view the paged frame against the
  full-resolution control is **max 1 of 255** at the default centre and **max 223-238** at
  `--offset 280`, over 296-463 samples in an **18x69 strip** at x 532..549, y 84..152 (the earlier
  16x27 note measured a subset of the same strip). Within a run the frame is STABLE (repaints 4..7
  bit-identical); between runs the strip's CONTENT differs. It is a **composite** difference, not a
  sampling one: the control draws a grey ring and blade there, the paged frame draws the **pink
  layer behind them** - the grey object's coverage is missing or diluted, and how much of it is
  missing differs per run.

  **THE SHAPE OF THE FAILURE IS "ONE CLEAN STATE, A FAMILY OF WRONG ONES."** Ten runs of the same
  command were compared pixel for pixel:

  | runs | vs the control | vs each other |
  |---|---|---|
  | `AE_VIRTUAL_TEXTURE=0` (control) x3 | - | **0 samples** (bit-identical) |
  | clean paged x4 (`AE_VT_UPLOAD_MB=999`, `AE_VT_ATLAS_SENTINEL`, x2, `AE_VT_MISS_MAGENTA`) | max 1 | **0 samples** (bit-identical with each other) |
  | defective paged x6 (plain, x2; `=end` dump, x2; `UPLOAD_MB=999`; sentinel) | max 223-238 | **82-294 samples differ**, all inside the strip |

  So the correct frame is reproducible to the pixel and the wrong ones are not even equal to each
  other. Everything else in every frame is identical across all ten runs.

  Ruled out ON A DEFECTIVE FRAME, each by a measurement:
  - **not the page table**: `AE_VT_VERIFY_PAGES=table` compares the GPU table with the CPU mirror
    entry by entry; the defect still measures max 223 and every entry matches;
  - **not the slot contents**: `AE_VT_VERIFY_PAGES=end` (the slot comparison, moved out of the frame
    so its stalls cannot re-time anything - it runs once after the last draw) reports, for all 15
    paged resources, `checked == wanted` **114 of 114 pages, bad=0, max=0** on a defective run: every
    slot holds the bytes its own page's fill produces;
  - **not the entries against the pool**: the same run's `mirrorBad=0` (the table's entry for each
    page names the slot the pool really maps), so the table, the CPU mirror and the pool agree;
  - **not the layout**: `AE_VT_DUMP_PAGES=1` prints the page->slot layout of the final frame (114
    lines). Two defective runs print **identical** layouts (0 differing lines) and still differ
    from each other by 107 samples inside the strip, so which page sits in which slot is not the
    variable;
  - **not unwritten atlas memory**: `AE_VT_ATLAS_SENTINEL=1` creates each atlas filled with MAGENTA,
    so any read of a texel no page upload wrote is unmistakable. A defective run with the sentinel
    measures max 223 at the strip and contains **0 magenta pixels**, anywhere;
  - **not the tier**: `AE_VT_DEMAND=0` - no page uploaded at all, every paged level on the coarse
    chain - is **also nondeterministic**: three runs differ from each other by 100-113 samples
    (max 129-175), **all inside the strip**, and sit 223 from the control at the strip (the 23,232
    samples of 1-2 units elsewhere are the coarse tier's designed one-level blur);
  - **not the page depth**: with `AE_VT_COARSE=64` (deeper paged levels, so the strip's pixels read
    pages instead of the coarse chain) two runs still differ by **182 samples, max 223**, while the
    frame is max 1 from the control - the nondeterminism follows the PIXELS, not the tier.
  - **not Layer 6**: `--only-layer '=Layer 6'` (the layer the coarse-tier measurement used) is
    **bit-identical** to its control, so the strip needs the rest of the composite.

  **ONE REAL NONDETERMINISM WAS FOUND UPSTREAM OF EVERY VERIFIER HERE, AND IT IS NOT THE STRIP.**
  `AE_VT_HASH=2` (the GPU chain's level 2, hashed right after the chain is built) prints **241 lines;
  240 are identical across two runs and one is not**: `384x1715 level=2` (the layer named "Layer 19
  Copy"). `AE_VT_DUMP_CHAIN=384x1715` shows what differs - **758 of 41,088 texels, all in the
  near-transparent fringe**: `(24,14,26,126)` against `(24,14,26,3)`, `(16,6,12,14)` against
  `(16,6,12,6)`, `(220,219,186,1)` against `(220,219,154,2)`. So the **GPU mip-chain build** (the
  area-weighted tail pass in `gl/Texture.cpp`) is nondeterministic in the sub-unit-alpha fringe of
  one chain, which means the *input* every paged path copies is not always the same bytes. This is a
  real defect and it deserves its own fix - but it is **not** the strip: that resource is 384x1715
  and out of view at this camera (it has no pages and does not appear in `VTPAGECHECK`), and the
  `VTCOARSE` hashes of all 108 coarse chains are identical across runs **except** that one resource.
  It does mean the chain build cannot be assumed byte-reproducible, and a paged path that copies a
  chain inherits whatever the copy took.

  Two of the earlier claims were wrong and are struck out here: a fully coarse frame is **not**
  "max 73 from the control" at this camera (that number was measured at the default centre; at
  `--offset 280` it is 223), so "the strip reads a resident page - sharp and wrong" does not follow;
  and the *diagnostics* do not "hide it because they are slow" - a change as small as a shader
  source line (`AE_VT_MISS_MAGENTA`, which changes no behaviour where it is not taken) turned five
  plain runs' worth of defect into a clean run. What the diagnostics share is *perturbation*, not
  duration.

  **THE LAYER AT THE STRIP IS NAMED: `Layer 19 Copy` (384x1715).** `AE_VT_TAG_LAYER=1` makes every
  draw output an opaque colour derived from its name and prints the mapping (`VTTAG layer= tag=`),
  so the colour at a pixel names the topmost layer there without a bisection over 130 of them: at
  the strip that colour is `Layer 19 Copy`'s (`tag=244,88,162`), the same in both paths. Isolated
  with `--only-layer '=Layer 19 Copy'` it alone shows the defect - **123 samples, max 68, all inside
  the strip** - so the composite story above is that one layer's own read. And every input of that
  read was then measured equal:
  - **the same level**: `AE_VT_DEBUG_LEVEL=1` outputs `(level/16, lod/16)`. On the isolated layer the
    two fields are **bit-identical** (0 px of 409,600), level 3, lod 3.2, at the strip included;
  - **the same uv**: `AE_VT_DEBUG_UV=1` outputs the sampled uv. **Bit-identical** again (0 px), so
    the taps are at the same texels in both programs;
  - **the same content**: `AE_VT_HASH=all` hashes every chain level and `AE_VT_HASH_COARSE` now
    hashes every coarse level. For that layer **9 of 9 levels match byte for byte** (level 2 =
    `61b4fd8a...` through level 10 = `714991b1...`), so the coarse chain - the tier the paged path
    samples at this camera - is a faithful copy of what the control samples;
  - **no mask, no clipper, no destination**: `AE_TRACE_DEST=1` reports
    `blend=0 clippee=0 refreshed=0 mask=0 maskPaged=0` for that draw.

  So for this layer the paged read is *provably the same read*, and its isolated frame still differs
  in the strip. What is left is a pass `--only-layer` does NOT disable: **folder mattes** are
  fullscreen composite passes that draw regardless of layer visibility, and a matte's coarse chain
  is the one copy in the whole system this document has never compared against its source (the
  image's is byte-exact at every level now; `MaskPyramid`'s has no equivalent check). That is the
  next instrument: hash `mMaskPyramid` level by level against the mask coarse chain's copies, the
  same way `AE_VT_HASH=all` + `AE_VT_HASH_COARSE` does for images, and isolate the folder rather
  than the layer.

  The measured content of the failure - a layer's coverage missing from a **composite**, with the
  pages, the table, the slots and the layout all exact, and the defect surviving with **no page
  read at all** (`AE_VT_DEMAND=0`) - leaves exactly one class of input unmeasured: the **draw's own
  geometry**, i.e. the vertex buffer the pass used. `AE_TRACE_DEST=1` struck out the other
  candidate: it prints, per draw, whether the destination texture the fragment shader's blend reads
  was refreshed, and on this frame **all 130 layers are `blend=0 clippee=0 refreshed=0`** - every
  layer blends Normal, `blendColor` therefore returns the source identically whatever the
  destination holds, and `DestinationTexturizer::update` is not called at all. (Two real defects
  remain in that code, worth their own fix but unrelated to this frame: the copy is drawn with the
  PREVIOUS draw's `GL_BLEND` still enabled, and it is refreshed only when `blendMode != Normal`
  although `blendColor` is every layer's output.) With the geometry as the last variable, the next
  instrument is a hash of the positions/texcoords the draw binds, printed once per frame: two runs
  of one command either print the same buffer or they do not, and that is a five-line diagnostic
  rather than another theory. This is the open list's first item.
- **Still-view cost** (nene, 640x640, 130 layers, `--frames 6..6 --repeat 8`, `AE_VT_STATS`): the
  eight renders are one still frame repainted, which is what a pointer over the canvas asks for.
  The first marks **107** resources and costs 165 ms (it also lands the first 8 MB of pages); the
  seven after it **replay 107 of 107** and never mark. With all 114 pages resident the last three
  repaints cost **9.2-9.8 ms**, against **15.2-16.0 ms** with the replay disabled (the same
  mutation) - the demand pass's geometry is **36-41 %** of that still frame. SUPERSEDED by the
  affine demand path (Measured): a still frame now marks 107 QUADS rather than 1.75 M triangles,
  so this figure is the record of what the geometry cost, not what it costs now. Playback is untouched by design: a
  new time frame re-skins every layer, so the revision moves and every frame marks.
- **The pass's cost is the geometry, not its setup**: `AE_VT_DEMAND=nodraw` - state changes,
  uniforms and per-layer buffer uploads kept, only `glDrawElements` skipped - costs
  **91.2-92.4 ms** a frame on nene's playback, the same as **91.1-91.8 ms** with the whole demand
  machinery off, against **96.8-102.3 ms** with it. Hoisting or batching the pass's setup would
  buy nothing; the only win is not walking the triangles - which the affine demand path now does
  for every layer without deformation (99.5-114.7 ms against 90.3-97.5 ms a playback frame).
- **V1 parity**: 900x700 source, 320x320 canvas, coarse level 1, 12 level-0 pages wanted and all
  resident. The source is deterministic per-texel **noise**, not the old smooth curve: a ramp
  hides a filtering difference in 1/255 rounding, noise turns the same difference into tens. At
  0.25 (isotropic) and 0.0625x0.25 (4:1, level 1) - both coarse-tier cases - the paged and control
  frames measure **max 1 and max 0**; at 0.5 and 1.0, **max 1 and max 0** (over the interior; the
  layer is larger than the canvas). The 1-unit pixels are the atlas read's sub-texel rounding on
  the Xvfb/xcb path: under `gpu_headless` the 0.5 and 1.0 cases measure max 0, so the tolerance
  is 1 for driver rounding, not for the read. 0.3 (level 0 paged, the layer's own edges on screen, every border
  tap exercised) is **max 2** on the edge row; 0.6 - a footprint of 1.67 texels, where
  `lod = log2(minor) - 1.35 <= 0` enters the magnified branch while the unbiased footprint is
  still minifying - is **max 1**. The gate has power: with the two old reads temporarily restored
  the same source measures **max 23 / mean 1.495** (the coarse single tap), **max 53 / mean 0.883**
  (the 4:1 coarse case) and **max 31 / mean 4.429** (the implicit-LOD magnified branch), so the
  1-unit tolerances excuse only driver rounding, not a wrong read.
- **test.anie** (six paged resources, 640x640 fit view): 52 pages, 14,060,800 B of pages in one
  atlas, all wanted pages resident after the second frame; steady `ms` 4.9-5.1 against the
  control's 4.5-6.2. Same-frame paged vs control: **bit-identical, 0 pixels differ**. The
  6873-px difference that used to be here (mean 0.076, max 43; mostly 1 unit across each layer,
  with full-width rows at the layer edges) was the magnified branch: with the -1.35 bias it is
  entered while the hardware LOD can still be minifying, and the control's `texture()` then
  blended mip levels (with anisotropy) where the page path read level 0. The control now takes
  `textureLod(..., 0)` - the same read as the page path, and the same read as `texture()` under
  true magnification - so the band is gone rather than excused.
  **The first-atlas floor was examined and KEPT (2026-11-08).** A demand-sized first atlas
  (2048/3072/4096 by the first landing's wanted pages, plus the purge-and-re-land a size change
  needs) was built, gated (`P5`/`P6`, reverted with the feature) and measured, then reverted by
  decision. `tex_live`, Release, 640x640 fit view, frames 0..3, the same binary:

  | project | full chains (control) | paged, fixed 4096 | paged, demand-sized |
  |---|---|---|---|
  | test.anie (one 1102x792 layer) | 9.6 MB | 72.3 MB | 22.0 MB |
  | dinner lab | 307.6 MB | 88.9 MB | 38.6 MB |
  | ballerina | 401.3 MB | 83.8 MB | 83.8 MB |

  The alternative saved exactly the atlas-size difference (50.3 MB) on every project on the small
  rung (`test.anie`, `dinner lab`, `EmiliHappyIdle`) and nothing on a full-rung one (`ballerina`).
  It did not make paging a win for the smallest project: `test.anie`'s whole chain is 9.6 MB, less
  than the 15.2 MB small atlas alone, so even 22.0 MB is 2.3x its chains - the right answer there
  is a policy that does not page such a project at all (open item 7). The growth transient was
  measured too (Release, 234 wanted pages): the growth frame 10.0 ms and the re-land frames
  7.0-9.3 against a 4.0 ms steady state, fully resident after 8 frames at 31 pages a frame, the
  coarse tier covering the wait. The floor is not a VRAM problem (61 MB is ~6 % of a 1 GB card and
  nothing on an integrated GPU), so the fixed 4096 is the simpler choice and stays.
  RE-CONFIRMED 2026-11-06 after the folder-matte change, with `render_probe` on the same project
  (640x640 fit view, frames 0..5, `AE_VIRTUAL_TEXTURE=0` as the control): frames 2-5 are
  **bit-identical** (0 px differ), frame 1 is max 1 / mean 0.0025, and frame 0 is max 176 / mean
  5.70 over 79,142 px - the first frame's coarse transient, which the viewport's two-frame lag
  predicts and which the export's synchronous preparse exists to avoid.
- **Masks** (`MA1`): a 900x700 layer mask on a *paged* layer matches the full-resolution control
  within 1/255 (mean 0.047), and the mask is proven active (its hidden half is empty, its revealed
  half covered). The mask's cost is its R8 chain: 900x700 = 839,797 B, **4096x4096 = 22,369,621 B
  (21.3 MiB)**. A 15000² mask is ~300 MB by the same arithmetic (the design's 225 MB was level 0
  alone). `MX1` compares a CPU area-average pyramid (mirroring the chain's two axis passes and its
  R16F half store) with the GPU chain: **max 2 LSB, 2-4 % of texels**. The GPU's x pass stores
  1/255-normalized values in half, which the CPU cannot reproduce bit-for-bit without mirroring the
  shader's exact arithmetic, so a CPU-sourced pager carries a 2-LSB mask tolerance (or needs that
  mirror). Building the full 4096² chain costs **96 ms on the GPU and 3.6 s on the CPU** (Debug;
  the ratio is the signal), which decides the split: the GPU builds the chain once, the pyramid is
  captured to the CPU (the image pyramid's pattern), and strokes update the CPU's boxes. `MX2`
  checks that split: the capture is **byte-exact** against the chain, and after a painted box the
  CPU pyramid equals a **fresh whole build** of the same field within **1 LSB** (72 of 65984 and
  1640 of 839797 texels) - the chain's own box path uses the cheaper exact-anchor source, so the
  CPU rebuild is the more consistent reference.
- **A real masked PSD** (`~/Downloads/kemono_psd_1/000145.psd`, imported through the app's
  `ctrl::ImageFileLoader`): 24 masked layers; paged vs the full-resolution control is **0 px over
  8/255, max 1, mean 0.00017**. That dataset's masks are small (the masked layers sum to ~2.2 M
  texels, ~2.9 MB of chains by their content sizes), so it is functional coverage, not the memory
  target; the target remains a large-canvas painted mask.
- **Clippers**: nene's three clipper layers are paged (443 MiB of full textures gone; 35 more
  pages, all resident; 0 px over 8 on the steady clipped frames). The synthetic `CL1` pair - a
  900x700 disc clipper over a 900x700 opaque layer, both paged - is **max 2, mean 0.003 of 255**
  against the full-resolution control, the 2 units on the disc's mask edge (the atlas read's
  sub-texel rounding on a coverage field); the clipped layer is demanded (not left to the coarse
  fallback), the mask is proven active by an unclipped render that covers the corner the clipped
  one leaves empty. The mask's read changed from the hardware's implicit-LOD `texture()` to the
  layer's own two-tap sampler on BOTH paths: that is what makes paging exact, and it removes the
  old mismatch between the mask's filtering and the clipped layer's.
- **Masks** (`MA1`, `MX3`): a 900x700 mask on a paged layer is 12 pages (all resident) and
  matches the full chain within 1/255 (mean 0.047). A stroke painted into the live paged mask is
  identical to the full chain rebuilt from the same field (max 0, mean 0) after the re-blend, which
  exercises `MaskPyramid::updateBox`, the coarse chain's box uploads and the resident pages'
  re-fills. The rubylith overlay pages through the same stack and matches the full-mask overlay
  within 3 units. `MX4`: for a 4096² mask the full chain would have been 21.33 MiB; the paged
  form's resident floor is the coarse chain (0.33 MiB), its CPU pyramid holds only levels 1..10
  (5.33 MiB - level 0 is the 16 MiB field, not a copy), and its pages land on demand.
- **A real masked PSD** (`~/Downloads/kemono_psd_1/000145.psd`, imported through the app's
  `ctrl::ImageFileLoader`, 640x640, frames 0..9): 24 masked layers, 6 of them pageable; the paged
  and full-resolution frames are **max 1/255, mean 0.00005, 0 px over 8** once the pages land
  (frame 3 onward; frame 0 is the coarse fallback, as the viewport's two-frame lag implies). The
  dataset's masks are decorative and small (2.15 M texels in all, 2.73 MiB of chains), so it is
  functional coverage of both paths (18 masks below the coarse threshold, 6 paged), not the memory
  target; the target remains a large-canvas painted mask.
- **Folder matte** (`FM1`): a 640² field on a 640x640 canvas at 1:1, over a 900x700 layer. The
  paged frame against the forced-full control is **max 0 of 255, mean 0.000**; the demand is
  exactly **9 pages** (its level-0 3x3 grid, no other level), all resident after eight frames, and
  the frame is **bit-identical** to the control. At the scale-2 partial view the demand is exactly
  **4** of those pages and the frame is bit-identical too. The rubylith preview is bit-identical
  to the full-mask overlay (**max 0** against a tolerance of 3). With the pool emptied the coarse
  chain still hides and shows. The export sequence (demand-only, `synchronize`, one draw) from a
  cold pool lands all 9 pages and matches the control (**max 0**); with the demand-only folder mark
  removed the same case measures **0 of 0 pages** and **max 64** at the matte's edge.

  The cost at this size: field 409,600 B, pyramid 136,530 B, resident coarse 136,530 B, against a
  546,130 B full chain. (The pyramid equals the coarse tier here because the 640² field's coarse
  level is already level 1 - see the residual list; a 4096² matte's pyramid is 5.33 MiB against a
  0.33 MiB coarse tier.)

  **No project in the local dataset has a folder matte** (checked with `render_probe
  --list-layers`, which now prints folders and sees a matte on animated keys as well as on the
  default key), so this gate builds its own field: the gain is structural - the pass joined the
  sampler and the pool - rather than measured on existing content. The target case is the one the
  design named: a folder matte covers the CANVAS, so a 15000² project's folder matte would have
  held a ~300 MB chain and now holds its coarse tier (0.33 MiB) plus the pages it samples.
- **An animated matte** (`AM1`, 2026-11-08): the two keys' fields are the animated value, and every
  frame between them used to be a NEW field - a fresh chain build on the GPU, a readback of every
  level (`MaskPyramid::capture`), a fresh pool resource and table. It was worse than that: the
  blended expansion never set the "painted" flag that "a neutral matte costs nothing" introduced
  (`MaskKey::Data::mPainted`), so an animated matte was drawn **unmasked** - and therefore never
  paged. No `.anie` in the local dataset has a mask key at all (a byte scan for the `Mask` timeline
  token finds only object NAMES containing the word), so no gate could have caught it. The fix has
  two halves: the flag (`TimeKeyBlender::blendMaskKey` sets it from the two keys - a texel either
  key hides is still hidden in a lerp), and the **blend view**: every level is a linear filter, so
  `level(lerp(A, B, t))` is `lerp(level(A), level(B), t)` at the same integer weight, and the matte
  is drawn as the blend of the two KEYS' own pyramids (`MaskPyramid::setBlend`) instead of a
  materialized field. Each key's pyramid is built once (the static matte's one-time cost); the
  view's pool resource keeps its pages across frames; per frame only the always-resident coarse
  chain and the RESIDENT pages are re-read from the two sources at that frame's weight
  (`VirtualTexture::refreshMaskView`), and only when an INPUT changed (the weight, either field's
  stamp, or the pair) - a paused or repeated frame refreshes nothing (0.61-0.78 ms against the
  2.5-6.5 ms of an animated one).
  Release, `AM1`, 320x320 canvas, layer at 0.6, medians of frames 1..24:

  | field | view | same frame again | materialized |
  |---|---|---|---|
  | 900x700 | **2.76** | 0.54 | 2.39 |
  | 2048² | **4.38** | 0.63 | 7.60 |
  | 4096² | **6.56** | 0.84 | 31.33 |

  The view's cost is nearly flat (the visible pages plus the coarse tier) where the materialized
  path scales with the field: 4.8x apart at 4096², and the view *losing* at 900x700, where the
  matte is smaller than the screen and the materialized path is one blend and one chain. At 4096²
  the refresh measured 3.9-4.8 ms: coarse 0.7, fill 1.5 (16 pages, two source fills and a lerp) and
  direct page uploads 1.6 - the last is the one term the landing's stage does not carry (see the
  open list). The pixels are **max 0** against the materialized control at every size and after a
  stroke on one key: level 0 is the same integer lerp, and the levels above it are the lerp of the
  two source chains, whose half-float rounding could differ by the 1-2 LSB the CPU pyramid shows
  (`MX1`) - this view reads level 0 only, so it did not.
  Entering the interval costs both keys' pyramid builds and draws the **coarse tier for one
  frame** (the paging latency every paged resource has); the first animated frame measured up to
  **~22 ms at 4096²**. Each key's pyramid is 5.33 MiB there - **10.7 MiB for a two-key matte**, on
  top of the keys' own 16 MiB fields - and the view adds a 67 KiB page scratch plus the resource's
  coarse chain. Debug measures the same fixture at 5.66-5.85 against 3.87-4.07: the view's two
  source fills and client uploads are CPU work, the materialized path's is GPU-side.
- **Export**: `media_exporter_e2e` passes with the demand preparse; the first exported frame is
  the one that exercises it.

### Open, in priority order

1. ~~**The timing-sensitive wrong read at a panned camera**~~ - **FOUND AND FIXED**: the mip tail
   build rendered each level into the texture it was sampling. See Measured, "the mip tail's
   read-while-write". The defect was never in the paged read: everything the paged path samples
   was proven identical to the control (uv, level, content byte for byte), and the chain that both
   paths share was the thing that changed from run to run. The fix builds each tail level into a
   scratch texture and blits it into the chain, so no command samples a texture it writes; the
   two-run comparison on nene went from **max 223 in an 18x69 strip with the two runs 109 samples
   apart** to **bit-identical runs, max 1 from the control, 0 samples in the strip**, and the
   mutation control (`AE_MIP_TAIL_OLD_PATH=1`) brings the defect back on demand.
2. ~~**The demand pass on the frames that still re-mark**~~ - **DONE for the geometry**: the affine
   fast path above turns the whole pass into one quad per layer (100 % of nene's 1,746,491 demand
   triangles), ~9 ms a playback frame, and it also fixes the over-demand that made 2x views fall
   back to the coarse chain. ~~The residual - a DEFORMED layer still walks its grid, unmeasured -
   needs a project or a fixture with bone/FFD motion.~~ **DONE 2026-11-07**: `DF1` is that
   fixture, and measuring it found the affine path's FFD test was dead (every FFD-warped layer
   took the quad) and that the walk demanded the whole layer whatever the camera saw. Both are
   fixed - the timeline-key predicate and the screen-bounds cull - with DF1 as the gate and
   `AE_VT_DEMAND_NOCULL` as the cull's control (see Demand and Measured). What remains of the
   item: (a) ~~a layer MASK's demand still walks on a deformed layer even though the mask's uv is
   the screen->content affine, so a quad over the deformed mesh's SCREEN coverage would replace
   its triangle walk exactly.~~ **DONE 2026-11-07**: the matte's demand is now a quad over the
   TARGET (`markScreenMaskDemand`, the folder matte's entry point) - the affine image of the target
   rect contains every fragment the draw can produce, so it is conservative over the geometry, and
   `DF1` pins both the masked parity under a warp and the replay across a deformation-only change.
   The mesh form (`markMaskDemand`) is deleted: no path walks triangles for a mask any more;
   (b) the walk's geometry cost on a 15000^2 bone-animated layer
   is still unmeasured (ballerina's FFD layers are small enough to hide in the frame variance);
   (c) **a bone-influence fixture.** The predicate now excludes the influence targets
   (`bone().targetMesh() != nullptr`) and the population is measured - `dinner lab` frame 0 has
   nine, seven of which were on the quad - but no gate builds one: a `BoneKey` computes its
   influence map from a `Project` (`resetCaches`) and the harness has no Project, so a fixture
   would have to build the bones, the caches and a `PoseKey` before it could pull a pageable layer
   by more than a page. Until then that half rests on the draw path (`callGL`'s `useInfluence`
   branch) plus the count, and no project in the dataset shows it in pixels at the cameras tried.
3. The pool's landing rate - **DONE for both CPU terms (2026-11-08); the demand readback is
   measured and CLOSED, and what is left is the cap's policy.** `FrameStats` records
   **`vt_frame_ms`** and `AE_VT_LANDING_SPLIT=1` divides it (`VTLANDING`), so this item is measured
   rather than reasoned. On ballerina at a 640x640 zoom-1 view, Release, 31 level-0 pages a landing
   frame at the default 8 MB cap:
   - **fill 2.8-3.2 ms** (was 6.4-13.6) and **upload 0.10-0.15 ms** (was 3.2-3.5), everything else
     < 0.7 ms, `vt_frame_ms` **4.2-13.3** (was 13.0-25.9) - and the total also carries `read=`,
     which the next bullet shows is not addable. The fill's fix is the vectorised
     `img::premultiply` (Measured: 0.239 -> 0.087 ms a page at the median), gated by `P0`; it is at
     the price of a bare memcpy of the same bytes, so lever (a) has no room left on this machine.
     The upload's fix is the **staging buffer** (Measured, the landing bullet): the direct call was
     costing a CPU copy of every page into the driver's staging, and filling into mapped buffer
     memory instead removed it; `P4` gates it and `AE_VT_STAGE=0` is the control. **Batching, the
     other shape this item used to propose, is now not worth building**: it measured 0.059-0.065 ms
     a page against 0.091 direct, i.e. ~1 ms a frame, and the stage removes the copy altogether.
     The atlas ping-pong idea is measured DEAD: sampling the atlas before the uploads changed the
     direct cost by nothing (2.84 against 2.81 ms).
   - **The demand map's readback is measured and CLOSED: it is a relocation, not a cost.**
     `VTLANDING` divides it into `read` (the fence check + the map), `parse` (every resource over the
     mapped bytes) and `clear` (the array's clear). On the landing frames: **parse 0.02-0.05 ms,
     clear 0.01-0.02 ms, read 0.1-9.6 ms** - and the read is the SYNC QUERY itself (the map is 0.02
     ms): `glClientWaitSync(fence, 0, 0)` costs 2.9-4.5 ms and returns `CONDITION_SATISFIED`,
     `glGetSynciv(GL_SYNC_STATUS)` costs 2.8-4.4 ms and returns `SIGNALED`. Both are the driver
     flushing the previous frame's command buffer at the first fence interaction. The control that
     settles it: REMOVING the sync and mapping regardless leaves the frame's `ms` unchanged over four
     alternating runs (51.7/33.8/26.5/26.7/31.7/28.0/22.4 against 52.8/32.4/26.7/27.2/32.2/28.3/22.6,
     then 53.3/31.8/26.7/26.8/32.0/28.0/22.2 against 52.6/31.2/27.5/26.7/32.9/28.6/23.3). A one-frame
     demand lag would therefore buy nothing, and the readback is only worth revisiting if a future
     driver stops paying the submit here.
   - **The cap is now a policy question with numbers.** Re-measured with the new fill at this view:
     62 pages a frame cost **15.4-23.8 ms** and 124 pages **30.6-30.8 ms** (the same caps cost
     27.4-43.3 and 60-140 ms before). 8 MB remains the default because the hitch it buys is the
     smaller one; a 16 MB cap would land the same 250 pages in half the frames at roughly the hitch
     the OLD 8 MB cap had. A TIME-budgeted cap is the principled form, but the gate-visible page
     counts (V1/DR1 assert them) make a wall-clock budget machine-dependent, so bytes stay.
   - **Debug**, the same view and frames: fill 12.7-21.3 ms, landing 21.9-28.4 ms (was 49-83).
   - **Unchanged**: a large import's synchronous capture is a separate visible pause (1.47 s across
     136 keys on nene, worst 130 ms); an asynchronous capture is still the obvious reduction there.
     And there is no zero-upload control: `AE_VT_UPLOAD_MB=0` does NOT mean "no uploads" -
     `PagePool`'s `envInt` maps a non-positive value to the default 8 (a trap that cost one A/B).
4. ~~**An animated mask**~~ - **DONE (2026-11-08), and it was not the item this was.** The item
   assumed the per-frame rebuild was the problem. It was, but the blended expansion also never set
   the "painted" flag that "a neutral matte costs nothing" introduced, so an animated matte was not
   drawn AT ALL - and therefore never paged. Both are fixed: the flag is set from the two keys
   (`blendMaskKey`), and the linearity property above is the shipped design - a per-key pyramid plus
   a `MaskPyramid` blend VIEW, so no field is materialized and the pool resource survives the
   frame. See Measured, "an animated matte", for the numbers and `AM1` for the gate. What remains:
   - the refresh's PAGE uploads are direct client uploads (1.6 ms of the 3.9 ms refresh at 4096²,
     16 pages). The landing's staging ring cannot carry a mid-frame phase: its two slots are a
     frame apart, and a second `endStaging` per frame would reuse a slot whose fence is one frame
     old, so `mapStaging` would sometimes refuse and push the LANDING back to direct uploads. A
     small stage of the refresh's own is the reduction worth measuring next.
   - the first frame of the interval pays two pyramid builds and draws the coarse tier for one
     frame (measured up to 21.2 ms at 4096²). An async or pre-emptive build would hide the pause -
     the same shape as the import's capture.
   - the FOLDER matte shares the whole path (`FolderNode`'s paged matte) but has no animated
     fixture; `AM1` covers the mechanism and `FM1` the folder wiring, and no dataset project has a
     folder matte at all.
   - the rubylith PREVIEW while painting at a blended frame reads the key being painted (the
     editor's own field), not the blended view. That is deliberate - the artist paints a key - but
     it is not gated.
   - two pyramids per animated matte (10.7 MiB at 4096²) is the view's memory price. A key drawn
     statically and used as a blend source shares ONE holder and pyramid, so the cost is per KEY,
     not per use.
   Folder mattes and `mask_bytes` are done; filter-lane masks are the remaining full-chain
   textures (above).
5. ~~**Per-resource pyramids for duplicated layers**~~ - **MEASURED (2026-11-08) and not worth
   building on this corpus.** `--list-layers` reports the shared image per layer (`res=N`): nene
   133/133, `dinner lab` 37/37, `ballerina` 34 layers over 33 resources (one duplicate),
   `test.anie` 1/1. Sharing one pyramid between two keys keeps its design question (which key's
   holder owns it when the resource is reloaded), and here it would save one pyramid. If a project
   with many duplicated large layers appears, the measurement starts from `--list-layers`' `res=`,
   and the pyramid is the easy half - the paged RESOURCE (coarse chain + pages) is the larger prize
   and a harder lifetime question.
6. ~~**The first-atlas floor (61 MB)**~~ - **MEASURED AND THE FLOOR KEPT (2026-11-08).** A
   demand-sized first atlas was built, gated (`P5`/`P6`) and measured: 50.3 MB less texture memory
   on every project on the small rung (`test.anie` 22.0 against 72.3 MB), nothing on a full-rung one
   (`ballerina` 83.8 both ways), and a purge-and-re-land when a session's demand outgrows the
   choice (measured: growth frame 10.0 ms, re-land frames 7.0-9.3 against 4.0 steady, 8 frames to
   fully resident, the coarse tier covering the wait). It was reverted by decision: the floor is
   not a VRAM problem, the projects that page have chains far larger than one atlas, and the
   variable geometry plus the growth event are more machinery than 50 MB is worth. Whoever wants
   that 50 MB has the measured shape in the feature's own commit (`c3ab0971`); the smallest
   projects need item 7, not a smaller atlas.
7. **Paging is a memory loss for the smallest projects** (found 2026-11-08): `test.anie`'s full
   chain is 9.6 MB and the paged path is 72.3 MB at the fixed floor - 22.0 MB even at the smallest
   atlas the alternative could choose, because the atlas alone is 15.2 MB plus its pages. The right
   shape is a POLICY that leaves a project whose whole pageable set is smaller than the pool on
   full chains (the item's original sentence, now with numbers), not a smaller atlas. It needs the
   set's chain bytes (a tree walk at project load) and a hook so the pixel gates can force paging
   for their fixtures (`AE_VIRTUAL_TEXTURE=0` is the opposite control). Not implemented.

The parity gaps are closed - the nene residual (the coarse tier's single tap) and the test.anie
band (the control's implicit-LOD read in the biased magnified branch) - and the clipper layers are
paged through the same sampler, with `CL1` gating a paged clipped pair. The pixel-equality gate is
met on both projects, and `FM1` extends it to a folder's matte.
