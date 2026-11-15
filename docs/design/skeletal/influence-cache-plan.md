# Bone influence cache: storage, load, and build plan

Status: **Phases 0-6 landed.** Phases 1-2 measured 2026-11-10, Phases 3, 5 and 6
measured 2026-11-11. The format is 0.21, the attributes are packed per map
(1-7 slots sparse, 8 dense), the rebuild is 3.4-4.5x faster in Release with
byte-identical arrays, and the resident attribute bytes are **3.3-5.7x smaller**
on the three measured projects. The skeleton bone cap is **256**, the ceiling of
the influence map's uint8 bone index, and the palette travels in a uniform
block.
Scope: the per-layer envelope-weight cache (`core::BoneInfluenceMap`), its
on-disk record, when it is built, how its resident attributes are stored, the
skeleton bone cap those attributes index, and the format/versioning rules that
follow from them.

Phase 1 outcome, in one paragraph: `BoneKey::serialize` writes bones only;
`BoneKey::deserialize` consumes a <= 0.20 cache section with
`Deserializer::skipBytes` and discards it; `ProjectLoader` rebuilds every
`BoneKey`'s maps after `IDSolver::solve()`, so the GUI, `render_probe` and the
round-trip harness now share one path; `System::openProject`'s duplicate rebuild
loop is gone; the format is 0.21; `tests/bonekey_regression` is now a
stream-compatibility gate (legacy discard + sentinel, 0.21 has no section,
damaged counts fail). Measured numbers are in §7. The important nuance found
there: a **fresh** 0.20 cache is pixel-identical to its rebuild, but the old
0.8 files in the dataset carry a stale cache (2.anie is missing one zero-vertex
cache), so `render_probe`'s output moves by at most 1 LSB on 6 pixels - toward
what the GUI already drew, because the GUI always rebuilt on open.

Phase 2 outcome: the packed **index** attribute (4 × uint8, `ivec4`) is exact and
shipped; the planned **unorm16 weights** were built and measured, but exceeded
the 1-LSB pixel gate on 2.anie (max 12 on 61 pixels of one frame), so the
shipped representation keeps float32 weights: **40 B/vertex, 64 -> 40 (1.6×)**.
The fallback is bit-identical to Phase 1 on 10 frames across three projects. The
2-4× resident goal was still open at that point; Phase 5 (D7) closes it with the
sparse slot layout. See §7 for the numbers and D3 for what was rejected.

Phase 3 outcome: the cost lived in the polygon containment and the weight math,
not in the bone sweep. `core::BonePolygonIndex` precomputes a bone's influence
polygon edges and buckets them by y, reproducing Qt's odd-even scan exactly;
`BoneShape::getBoneWeight` skips its two `atan2` calls when a bend range is
invalid (the skipped factor is exactly 1.0). On `2.anie` the influence join went
from **36.3-38.4 ms to 8.6-10.8 ms in Release (3.4-4.5x)** and 60.8-62.0 to
42.7-43.9 ms in Debug, with **SHA-256 identity of all four arrays** for every map
of `2.anie`, `dinner lab` and `tart`. The planned uniform bone grid and
per-map task chunking were both built and measured, and both were **rejected**
(the grid lost ~5 ms in Debug; chunking moved wait into scheduling and left the
total wall unchanged). `tests/bone_polygon_index` pins the index against
`QPolygonF::containsPoint` on 77,889 points (including three non-finite ones, so
the bucket lookup cannot turn a NaN into an out-of-range float-to-int
conversion).

Phase 5 outcome: a map now stores only the influence slots its busiest vertex
uses, so the 40 B/vertex dense record survives only at the eight-slot cap.
Measured on `2.anie` (262,165 cached vertices): **214,974 vertices use one
influence**, 33,997 use two, 9,814 use three, 1,643 use four and 1,737 use five;
**no vertex uses six to eight**. The per-map maximum is just as skewed - 102 of
130 maps never exceed one slot, 8 max at two, 14 at three and 5 at five. Storage
is slot-major (one uint8 index per vertex per slot, plus one float32 weight per
slot only when there are two or more); a one-slot map's weight is exactly 1 and
is not stored. The resident arrays went **10,486,600 -> 2,277,514 B on 2.anie
(4.60x)**, 124,000 -> 37,693 B on `tart` (3.29x) and 391,160 -> 68,207 B on
`dinner lab` (5.73x). The logical eight-slot arrays are **SHA-256 identical**
before and after on all 160 maps of the three projects, and the rendered PNGs
are **bit-identical on all 10 sampled frames**. Build cost is unchanged
(`2.anie` Debug wait 41.6-45.3 ms against 42.7-43.9). `tests/bone_influence_storage`
now drives the real writer with synthetic work attributes and pins slot counts
1-8, the default values, the one-slot implicit weight and the dense group
layout. See D7 and §7.

Phase 6 outcome: the skeleton bone cap is **256**, the ceiling of the influence
map's one-byte bone index, and the palette is delivered in an 8192-byte uniform
block (`BonePalette`, 2 vec4 per bone) instead of a default-block uniform array.
The old 32 was not a document limit: the palette, the influence holder
(`BoneKey::Cache`) and the shader each carried a copy, and a skeleton past it
lost bones silently. A census of the 36 loadable dataset projects found exactly
one key above 32 - `skel_practice_12.anie`'s "wings 1", 33 bones - and the raise
changes exactly one of its five influence maps, with frames 1-9 moving only in
the wing. A bigger default could not have done this: a plain uniform array is
charged against the guaranteed 1024 vertex-uniform components, and 128 bones is
that whole budget. The block path is bit-identical to the old uniform array on
all ten sampled frames of `2.anie` and `skel_practice_12`, and measures the same
(Release: `2.anie` 53.5-54.0 ms against 53.8 for the uniform array; `skel` 12.2
both). Two things had to be made explicit: a palette with no pose at a frame is
identity-padded to the map's bone count (`2.anie` has seven such layers, hidden
until now by the fixed array's identity tail), and a skeleton above 256 gets a
load warning through `ProjectLoader::warnings()`. See D8 and §7.

Read `AGENTS.md` first — it carries the build traps (one configuration at a
time, stale binaries, `cp -a` mtimes) and the verification commands referenced
below.

## 1. What exists today (verified after Phase 1)

| claim | anchor |
|---|---|
| a map stores only the slots its busiest vertex uses: one slot = **1 B/vertex** (index only; weight implicit), 2-7 slots = **5 B per slot/vertex**, eight slots = the dense two-set form, **40 B/vertex** | `src/core/BoneInfluenceMap.h` |
| the skinning shader has one variation per slot count (1-7 sparse scalar attributes, 8 dense `ivec4`/`vec4`); missing slots were exactly-zero weights and are not stored | `assets/shaders/MeshTransformVert.glsl`, `core::MeshTransformerResource` |
| the build's containment is an exact y-bucketed copy of Qt's odd-even polygon scan | `core::BonePolygonIndex`, `tests/bone_polygon_index` |
| **legacy (<= 0.20) record** was `int32 vertexCount`, `int32 maxBoneCount`, `indices0[4V]`, `indices1[4V]`, `weights0[4V]`, `weights1[4V]`, little-endian; it is now only **skipped**, never stored | `BoneKey::deserializeLegacyCaches` |
| **0.21 writes bones only.** The old cache owner / per-node / binding sections are gone from the writer | `src/core/BoneKey.cpp` (`BoneKey::serialize`) |
| weights are built by a background task when the bone key or a mesh changes | `src/core/BoneKey.cpp`, `src/core/BoneInfluenceMap.cpp` |
| when all 8 slots are full the **smallest stored weight is replaced** — selection is top-8 by magnitude, not bone order | `src/core/BoneInfluenceMap.cpp` |
| used weights are normalised (sum 1); 0 hits → index 0 weight 1; 1 hit → weight 1 | `src/core/BoneInfluenceMap.cpp` |
| **every loader path rebuilds after the ID solver** | `ctrl::ProjectLoader::load` (`rebuildBoneInfluenceCaches`) |
| `System::openProject` no longer rebuilds; it only clears the timeline blend/master/pose caches | `src/ctrl/System.cpp` |
| `resetCaches` reconstructs the cache list from the object tree and rewrites every influence map | `src/core/BoneKey.cpp` |
| the headless renderer now rebuilds too, because it loads through `ProjectLoader` | `tools/render_probe/render_probe.cpp` |
| `Deserializer::skipBytes` exists for the legacy discard (overflow/remaining-bytes guards, chunked) | `src/core/Deserializer.h/.cpp` |
| format version is 0.21; the loader rejects newer and older-than-0.4 files | `src/core/FormatVersion.h`, `src/ctrl/ProjectLoader.cpp` |

Two corrections to the earlier chat analysis, so this document does not
propagate them:

1. Influence selection is **not** "first eight in bone order". The replacement
   rule keeps the eight largest, which makes the bone-order worry moot. Do not
   "fix" it.
2. The 64 B/vertex record used to be load-bearing only for non-GUI loaders
   (`render_probe`, `tests/roundtrip_main.cpp`). After Phase 1 no loader keeps
   it; the bytes in old files are consumed only to keep the stream in sync.

## 2. Problem statement

- The file carries a derived cache, 64 B/vertex per cached layer. On the real
  open path it is deserialised and then immediately rebuilt, so it costs I/O,
  load time and file size for nothing.
- Its layout sits inside the authored document's stream, so changing it forces
  a project-format bump and a compatibility branch — a derived cache dictating
  document compatibility.
- Resident memory is 64 B/vertex per cached layer (indices `int32` for a ≤32
  value space, weights `float32` for normalised values).
- The build is O(bones × vertices) with a per-bone bounding-rect early-out, on
  one worker task, and it runs whenever a mesh or a layer transform changes.

## 3. Goals and non-goals

Goals, in priority order:

1. Derived state must not be required to open a project, and must not gate
   document compatibility.
2. Load-time behaviour must be identical on every loader path (GUI, headless
   renderer, round-trip harness).
3. Resident memory per cached layer should drop 2–4× without changing rendered
   output. (Phase 2 shipped the exact fallback at 1.6×; the 2–4× target is still
   open because unorm16 weights failed the pixel gate - see D3 and §8.)
4. Rebuild cost should drop, with byte-identical weights as the proof.
5. A written rule for what bumps the project version and what must be
   skippable.

Non-goals: skinning math (dual-quaternion blending stays as is), envelope
falloff (`BoneShape::getBoneWeight`), the 8-influence cap, the 32-bone palette
cap (tracked as an open question), and anything touching authored geometry,
bones or keys.

## 4. Design decisions

### D1 — Derived state leaves the file

`BoneKey::serialize` writes bones only. The cache owner, per-node cache records
(node id, inner matrix, obsolete origin offset, frame sign) and the binding
caches are no longer written. `BoneInfluenceMap::serialize/deserialize` are
deleted: after this phase there is no caller.

**Landed.** Reader: minor ≥ 0.21 expects no cache section. Minor ≤ 0.20 must
still load, so it consumes the legacy section and **discards** it, then relies on
the rebuild (D2). Discarding reads the scalar fields into scratch locals and
skips the influence arrays with `Deserializer::skipBytes(size_t)` (`64 *
vertexCount` bytes per cache: 32 B indices + 32 B weights per vertex; plus 92 B
of fixed record per cache and 72 B per binding cache). No allocation on the
discard path. `skipBytes` checks the remaining stream first, rejects a negative
or overflowing count, and chunks a large run so `StreamReader::skip`'s `int`
does not cap it.

Bump the minor to 0.21 and add the history line to `FormatVersion.h`. The
version bump is required because the writer stops emitting a section the reader
must distinguish; it is not a "no release, so no bump" case. The general
versioning rule now sits there too (authored data gates; derived data is skipped
and recomputed).

Retirement rule for the legacy branch: delete it once the first release that
writes 0.21 ships. Until then it is the only way an unreleased dev file opens.
`tests/bonekey_regression` is the sentinel gate that keeps the skip honest.

### D2 — One rebuild point, after the ID solver

**Landed.** Every loader entry point ends with the same derived-cache rebuild:

- After `idSolver.solve()` in `ctrl::ProjectLoader::load`, a reported section
  ("Rebuilding bone influence caches...") runs `resetCaches(project, node)` for
  every node and every bone key of that node. Projects with no bone keys leave
  the load report untouched.
- The loop was removed from `System::openProject`; the separate
  `clearCaches`/`clearMasterCache`/`posePalette().clear()` block stays
  (different caches, different reason).
- `render_probe` and `tests/roundtrip_main.cpp` get skinning from the same path
  with no change of their own.

The GL-context question is settled by measurement: **no context is needed.**
`resetCaches`/`BoneInfluenceMap::build` are CPU-only; the rebuild ran from
`ProjectLoader` under `render_probe` and `roundtrip` (GL) and under
`bonekey_regression` (no GL) with no GL function touched. `render_probe`'s
frames prove the result; see §7.

The rebuild is scheduled on the project's paralleler, exactly as the old GUI
loop did: the loader returns once the tasks are pushed, and the first
`accessor()` waits. The progress section is therefore a scheduling phase, not a
place to join the workers. Cancellation is checked between bone keys; a canceled
load returns false and the project's destruction cancels any queued task (a new
`~BoneInfluenceMap` cancel/join closes the old "idle task outlives the map"
hole).

### D3 — Pack the resident attributes (landed with the fallback)

Only after D1: with nothing on disk, packing is purely about resident memory
and upload bandwidth.

The planned 24 B/vertex packing was built and measured:

- Indices: 4 × `uint8` per set (8 B/vertex), uploaded with
  `glVertexAttribIPointer(location, 4, GL_UNSIGNED_BYTE, 0, nullptr)`; the
  shader input stays `ivec4` and `MeshTransformVert.glsl` is unchanged.
- Weights: 4 × `uint16` normalized per set (16 B/vertex, 24 B total), uploaded
  with a normalized `glVertexAttribPointer`; the shader input stays `vec4`.
  **Rejected on the equality gate.** Over `2.anie` frames 0-2 the per-frame max
  channel delta was 3 / 12 / 4 with 7 / 61 / 13 pixels over 1 LSB; `tart` stayed
  at 1 LSB and `dinner lab` at 2. The differences are isolated to
  high-contrast edges where a ~1e-5 relative vertex shift changes a sample -
  visually tiny (mean <= 0.0007), but the plan's gate is <= 1 LSB, so the
  weights did not ship as unorm16.
- Shipped fallback: 4 × `float32` weights (32 B/vertex) + packed indices = **40
  B/vertex** (37.5 % less than 64; 1.6x). It is bit-identical to Phase 1 on all
  10 sampled frames across `tart`, `2.anie` and `dinner lab` (max 0). The
  fallback changes ONLY the weight representation - the packed indices are
  identical in both builds - so the unorm16 deltas above are attributable to
  the weights, not to the index packing. The shader is unchanged.

Work that landed:

- `gl::EasyShaderProgram`: a packed `GLubyte` 4-tuple form (integer attribute)
  and a `GLfloat` 4-tuple form. The existing raw I-array entry point already
  covered `GL_UNSIGNED_BYTE`; the normalized entry point was added for the
  experiment and removed with it.
- `BoneInfluenceMap`: `IndicesType = GLubyte`, `WeightsType = GLfloat`; the
  `Accessor` returns tightly packed raw pointers; `setMaxBoneCount` asserts the
  uint8 ceiling (255; the palette caps at 32 today).
- `MeshTransformer::callGL`: binds four byte tuples through
  `glVertexAttribIPointer` and four float tuples through the ordinary pointer;
  the shader still reads `ivec4`/`vec4`.

Resident bytes: `2.anie` 16,778,560 -> 10,486,600 B (-6,291,960), `tart`
198,400 -> 124,000 B (-74,400). Process max RSS did not resolve the 6 MB
difference against the ~1.28 GB image-decode peak (the ranges overlap; see §7),
so the array-byte count is the precise number. **D7 (Phase 5) later makes this
40 B/vertex the eight-slot cap only**; the per-map sparse form is 1 B/vertex at
one influence and 5 B per slot above that.

### D4 — Make the build cheaper without changing a single weight (landed)

The reference oracle came first: `render_probe --dump-influence <file>` prints
the SHA-256 of every built cache's four arrays, and `--bench-influence <n>`
forces fresh rebuilds and reports the schedule and join separately. The arrays
are **SHA-256 identical** before and after on all 130 maps of `2.anie`, 19 of
`dinner lab` and 11 of `tart`, so every stored byte survived the rewrite.

Profiling (Debug, per-phase timers, since removed) found the cost in two
operations per vertex inside a polygon, not in the bone sweep: Qt's
`QPolygonF::containsPoint` over polygons of ~27 edges (one had 475), and
`getBoneWeight`, which computed two `atan2` angles even when a bend range was
invalid.

Landed:

1. `core::BonePolygonIndex` precomputes the polygon edges, drops horizontal
   edges with Qt's own `qFuzzyCompare`, buckets the rest by y, and evaluates the
   same crossing arithmetic. Only the parity of the integer winding sum is used,
   so bucket order cannot change the result. `tests/bone_polygon_index` compares
   it with `QPolygonF::containsPoint(..., OddEvenFill)` on 77,889 points: grid
   points (including vertices and edge midpoints), 300 random polygons, a
   475-point circle, and three non-finite points.
2. `BoneShape::getBoneWeight` skips the root/tail angle work when the bend range
   is invalid. `BendRange::getWeight` returns exactly `1.0f` there, so the
   skipped `twistWeight *= 1.0f * 1.0f` is exact; a leaf bone has no tail range.

Rejected, each built and measured:

1. **Uniform grid over bone bounding boxes.** It visited 13.5% of the
   bone-vertex pairs (412,176 of 3,042,594 for one 2.anie rebuild), but the
   calls it saves are the cheap out-of-envelope ones and the expensive work is
   inside the envelope. Debug join got worse (~44 -> ~49 ms); the code and its
   two temporary environment switches were removed.
2. **Splitting a map into vertex-range tasks.** Debug join improved
   (42.7-43.9 -> 39.9-41.0 ms) but the extra `push` calls moved the same amount
   into `resetCaches` scheduling (13.2 -> 16.0 ms), so total wall was unchanged
   (56 vs 57 ms), and Release was a wash. The per-cache tasks already fill the
   four-worker pool on a project with many maps; the single `BuildTask` stayed.

Both rejections are measurements of these projects, not proofs: a project with
one huge layer could still prefer chunking, but no project in the dataset does,
and the simpler build is the one that ships.

`thr::Task::wait` polls with `QThread::msleep(1)`, so an influence join under
~1 ms reads as ~1.08 ms (tart, and dinner lab in Release, sit on that floor).
Do not read that floor as build cost; it is the join's granularity.

### D5 — Versioning discipline

Write the rule into `FormatVersion.h` next to the bump history, and make the
legacy skip (D1) the worked example:

- A change to **authored** document data bumps the minor version and may gate
  the reader.
- **Derived** state (anything recomputable from authored data) is never required
  to load. If a future derived block is persisted, it carries a tag and a byte
  length, and an unknown tag/version is skipped and recomputed — never a load
  failure.
- Do not add chunk helpers speculatively. The only helper added now is
  `Deserializer::skipBytes`, because D1 needs it. Introduce a chunk header when
  the first skippable block appears.

### D6 — Invariants instead of load validation

Nothing is read from disk after D1, so post-load validation reduces to build-time
invariants: in `writeVertexAttribute`, assert in debug that every emitted index
is < the bone count used to build the map, that weights are finite, and that the
used slots sum to 1 within 1e-6. These catch a broken spatial index or a broken
selection rule in the harness, where a silent bad index would otherwise only
show as a visually wrong limb.

### D7 — Store only the slots a map uses (landed)

The 8-slot dense record is the worst case, not the common one. On `2.anie`,
214,974 of 262,165 vertices use exactly one influence; only 1,737 use five, and
none use six to eight. At the map level the same skew holds: 102 of 130 maps
never exceed one slot, 8 max at two, 14 at three, and 5 at five. Eighty-six
percent of vertices are in maps whose busiest vertex uses at most three
influences.

Storage is chosen per map from the busiest vertex's count (`slotCount`, clamped
to 1..8):

- **1 slot:** one `uint8` index per vertex and **no weight array**. Every weight
  in such a map is exactly `1.0` by the writer's own rule (no hit -> bone 0
  weight 1; one hit -> its bone weight 1), so the shader supplies the constant.
  1 B/vertex.
- **2-7 slots:** one slot-major `uint8` array per slot and one slot-major
  `float32` array per slot. 5 B per slot/vertex. The shader declares one scalar
  `int` and one scalar `float` per slot; the 7-slot case is 15 vertex
  attributes, inside the guaranteed 16.
- **8 slots:** the historical dense two-set layout and shader code, unchanged.
  The scalar form would need 17 attributes, above the guaranteed minimum.

Exactness is structural, not a tolerance: slot `k` of a vertex with `count <= k`
held exactly `0.0` in the dense form, so omitting its `0.0 * matrix` term adds
nothing (adding a zero matrix to a finite sum is exact). The normalisation loop
is the dense writer's term for term, so the stored float weights are identical.
A one-slot map's implicit 1 is the value the dense writer stored. Missing-slot
indices are never read; the shader omits their terms.

The shader is compiled lazily per slot count (`INFLUENCE_SLOTS` variation) and
cached in `MeshTransformerResource`; a project usually touches one to three
variants. `MeshTransformer::callGL` binds the sparse scalar arrays for 1-7 and
the dense groups at 8.

Measured resident arrays (not process RSS, which the image-decode peak still
dominates): **2.277 MB against 10.487 MB on 2.anie (4.60x)**, **37.7 KB against
124.0 KB on tart (3.29x)** and **68.2 KB against 391.2 KB on dinner lab
(5.73x)**. The logical eight-slot arrays are SHA-256 identical on all 160 maps,
and the sampled frames are pixel-identical. See §7.

Not built: a layer-granular "one set or two" layout. Calculated from the same
distribution, the five maps that need five influences hold 36,674 vertices, so
they would still pay 40 B and the whole project would reach only **1.75x** - it
does not meet the 2x goal, while D7 costs nothing on those vertices beyond the
slots they use. A per-influence compact table behind a per-vertex offset (or a
texture fetch) was considered and not built: D7 reaches the goal with vertex
attributes and no new GL resource.

The residual: no dataset project has a map at the eight-slot cap. The dense
layout is the pre-D7 code, its group order and weights are pinned by
`tests/bone_influence_storage` (all eight slots), and the sparse shader was
pixel-gated at 1, 2, 3 and 5 slots. If a real eight-slot project appears, add
it to the pixel gate before changing the dense path. (Phase 6 found one:
`skel_practice_12.anie` has an eight-slot map, and its before/after frames now
exercise the dense path through GL. See D8.)

### D8 — The palette is a uniform block; the cap is the uint8 ceiling (landed)

`uBoneDualQuat` is the skinning vertex shader's bone palette: each bone is a
unit dual quaternion, two `vec4` (real and dual), and a vertex blends the one to
eight entries its influence attributes name. The old cap came from how that
palette was passed, not from the document: a **default-block uniform array** is
charged against `GL_MAX_VERTEX_UNIFORM_COMPONENTS`, whose guaranteed minimum is
1024 components, and two `vec4` per bone makes **128 bones the entire budget**
(64 leaves half for the two matrices and the rest). Three copies of "32" - the
palette, `BoneKey::Cache` and the shader - meant a bigger rig silently lost its
last bones.

The options, and why the block won:

| delivery | capacity | why not |
|---|---|---|
| bigger default-block array | 128 bones is 1024 components (the guaranteed budget); 256 needs 2048 | not portable, and still a cap |
| several arrays / tiers | each tier is another compiled program and the draw must pick one per index | compile-time caps again, with more programs |
| **uniform block (`BonePalette`)** | guaranteed 16384 B = **512 dual quaternions**; 256 bones is 8192 B | the one used |
| texture buffer | tens of thousands of bones | per-vertex texture fetches, unnecessary once the index width caps at 256 |

The remaining cap is the **index width**: the influence map stores a bone index
as one `uint8`, so 256 bones is the data structure's own ceiling, not a uniform
budget. (More would need a 16-bit index attribute and a new storage layout; no
rig in the dataset is within an order of magnitude of 256 - the largest is 33.)

Landed:

- `core::PosePalette::kMaxCount` is **256**, the only literal. `BoneKey::Cache`
  passes it to the influence map and `MeshTransformerResource` sets the shader's
  `MAX_BONES` from it.
- The palette's CPU arrays are sized to the skeleton it actually built
  (`usedCount()`), not to the cap: a two-bone key neither allocates nor uploads
  256 entries. A palette that has not been built holds one identity entry.
- `MeshTransformVert.glsl` declares `layout(std140) uniform BonePalette { vec4
  uBoneDualQuat[MAX_BONES * 2]; }`. `MeshTransformerResource` binds the block to
  a fixed binding point once per program, after link.
- `MeshTransformer` backs the whole declared block once (8 KB, allocated lazily
  on the first skinned draw; ~1 MB across `2.anie`'s 137 mesh passes) and uploads
  only the **required** prefix per draw. Required is the larger of the palette and
  `BoneInfluenceMap::boneCount()`: a palette can be **cleared** while the map
  still addresses its bones (a frame with no pose key), and those bones are in
  rest, so they must read identity. The old fixed array supplied that as an
  identity tail; sizing the palette to its used bones made it explicit.
  `TimeKeyBlender::setBindingMatrices` takes the same fallback for a binding
  index past the palette.
- No format bump: every bone was already authored and saved; the cap was a
  runtime limit.

The identity gap was found on `2.anie`, not reasoned: seven skinned layers there
have a cleared palette while their map addresses bone 1, and the pre-padding
build rendered them from stale uniform memory (they differed from the old build
by 36k pixels per frame). With the padding, every frame from 1 on is
bit-identical to the pre-change build.

Measured (Release, 640x640, frames 0-29, reps 1-3, frames 5-29 aggregated):

| project | uniform array (control) | uniform block |
|---|---|---|
| `2.anie` | mean 53.79 ms, median 53.59, sd 1.61 | mean 54.03, median 53.89, sd 1.81 (53.46/53.28 in the forced-block run) |
| `skel_practice_12` | mean 12.22 ms, median 12.01, sd 0.74 | mean 12.22, median 12.17, sd 0.65 |

The delivery is not a measurable cost. A two-path design (uniform array up to
64 bones, block above) was built and measured first, and then removed: it bought
nothing on any project and kept a second program variant plus a threshold.

Two gates pin the result. `tests/bone_cap` drives the production holders on
synthetic chains: 33 bones keep index 32 in both the influence map and the
palette, 65 bones keep all 65, 257 bones stop at 256 with the 257th's vertex on
the default influence, and a 32-bone control reproduces the old defect.
`tests/mesh_palette_block` (GL, under xvfb) compiles the real shader with the
production variations and a **201-bone uniform block**, and skins two points
through transform feedback: index **200** with a 90-degree Z rotation turns
(1,0,0) into (0,1,0), and index 1 in the same block stays identity. That is the
range a default-block array could not hold (201 bones is 402 vec4, past the
guaranteed 256), proven through the shader, not by arithmetic. No dataset
project exercises an index above 32, so the real-project side is the 33-bone
rig's bit-identical frames.

## 5. Work plan

Phases are ordered; each ends with a green focused gate and a number recorded in
this document (the repo rule: a measured claim names its number).

### Phase 0 — Baseline numbers (done 2026-11-10)

Projects used: `tart.anie` (small skinned character, 15 layers, 11 BoneKeys) and `2.anie` (a large project, 129 persisted caches / 262,165 cached
vertices, 38 BoneKeys). Instruments were a temporary, uncommitted stopwatch and
cache counter inside `render_probe`, plus `/usr/bin/time -v`; a separate
worktree built at HEAD supplied the pre-change control. Numbers are in §7.

### Phase 1 — Derived state out of the file, rebuild in the loader (D1, D2) — done

- `src/core/BoneKey.cpp`: drop the cache-owner / cache-list / binding-cache
  sections from `serialize`; in `deserialize`, keep the ≤ 0.20 discard path.
- `src/core/BoneInfluenceMap.{h,cpp}`: delete `serialize`/`deserialize`, the
  `Accessor` stays.
- `src/core/Deserializer.{h,cpp}`: add `skipBytes(size_t)` with the same
  overflow/`maxFileSize` guards as the other reads.
- `src/ctrl/ProjectLoader.cpp`: rebuild after `solve()` (D2).
- `src/ctrl/System.cpp`: remove the now-duplicated loop.
- `src/core/FormatVersion.h`: bump to 0.21, add the history line.
- `tests/bonekey_regression.cpp`: keep the harness, repurpose the assertion.
  The file's premise (binding caches resolved after deserialise) is replaced by:
  a 0.20 stream whose legacy section is skipped leaves the stream in sync (read
  a sentinel after it), and a 0.21 stream has no cache section at all. Keep the
  ≥ 2 cache entries so the original dangling-pointer shape is still exercised on
  the discard path under ASan.
- `tests/roundtrip_main.cpp`: unchanged, used as the load-path smoke.

Acceptance (all measured; see §7):

- Re-saving a skinned project shrinks it by 64 B × cached-layer vertices; the
  measured deltas are 199,544 B for `tart` and 16,790,884 B for `2.anie`.
- `render_probe` frames are **pixel-identical** when the persisted cache is
  fresh: a 0.20 file written by the pre-change build immediately after its
  rebuild matches the new build's loader rebuild at every sampled frame (both
  projects, max 0). On the original 0.8 files the before/after delta is at most
  **1 LSB on 6 pixels** (2.anie frames 1-2; tart is bit-identical), because the
  serialized cache is stale - 2.anie's file is missing one zero-vertex cache
  that a rebuild creates. The GUI always rebuilt that cache on open, so this is
  `render_probe` moving toward the user-visible path, not a regression.
- A 0.20 file still opens; a 0.21 save/reload rebuild is bit-identical to the
  original rebuild.
- Focused gates pass; the full Debug suite is 75 pass / 1 fail, the failure
  (`object_tree_panel`) reproduced at HEAD and unrelated (see §7).

### Phase 2 — Packed attributes (D3) — done with the fallback

- `src/core/BoneInfluenceMap.{h,cpp}`: storage types and `Accessor` (uint8
  indices; float32 weights after the unorm16 rejection).
- `src/core/MeshTransformer.cpp`: byte indices through
  `glVertexAttribIPointer`; float weights through the ordinary pointer.
- `src/gl/EasyShaderProgram.{h,cpp}`: the packed byte tuple and the float tuple
  forms. The normalized form was used for the experiment and removed.
- Shader: no change (`MeshTransformVert.glsl` is absent from the diff).

Acceptance, measured: per-frame PNG comparison against Phase 1 on `tart`
(frames 0-3), `2.anie` (0-2) and `dinner lab` (0-2): **max 0, bit-identical** for
the shipped fallback. The unorm16 attempt (24 B/vertex) failed the <= 1 LSB gate
on 2.anie (3/12/4 max per frame, 61 pixels over 1 LSB on frame 1), so the
fallback is `float32` weights, 40 B/vertex (1.6×, not the 2-4× goal). VmRSS
maxima overlap at the image-decode peak; the exact resident array bytes are
recorded in §7. `tests/bone_influence_storage` pins the 40 B/vertex layout, the element sizes
and the default weights. Suite: 75 pass / 1 fail, the pre-existing `object_tree_panel`
failure documented in §7.

### Phase 3 — Build cost (D4) — done

- Reference oracle first: `render_probe --dump-influence` (SHA-256 per cache) and
  `--bench-influence` (schedule/join split).
- Landed: the exact polygon edge index and the invalid-bend-range skip.
- Built, measured and rejected: the uniform bone grid and per-map chunking (D4).
- Acceptance met: byte-identical arrays on every map of three projects; join
  36.3-38.4 -> 8.6-10.8 ms on `2.anie` in Release and 60.8-62.0 -> 42.7-43.9 ms in
  Debug; cancellation still checks between bones (the grid's per-vertex check
  never shipped).

### Phase 4 — Documentation and versioning rule (D5, D6) — done

- `src/core/FormatVersion.h`: the rule and the 0.21 line.
- This document: §7 has the measured numbers and D4 records what was rejected.
- `AGENTS.md`: the derived-state rule, plus the containment index and the two
  rejected build accelerations so they are not rebuilt from scratch.

### Phase 5 — Store only the used slots (D7) — done

- `src/core/BoneInfluenceMap.{h,cpp}`: per-map `slotCount`; sparse slot-major
  arrays for 1..7 slots; the one-slot form has no weight array; the dense
  two-set form is retained at 8. `Accessor::slotCount()/indices()/weights()/
  denseIndices()/denseWeights()` and `storedBytes()` are the storage contract.
- `assets/shaders/MeshTransformVert.glsl`: `INFLUENCE_SLOTS` variation with the
  scalar sparse path (1..7) and the unchanged dense path (8).
- `src/core/MeshTransformerResource.{h,cpp}`: lazy per-(skinning, dual-quat,
  slots) program cache. `MeshTransformer::callGL` binds the sparse scalar
  attributes or the dense groups from the accessor.
- `tools/render_probe`: `--dump-influence` still writes the **logical**
  eight-slot hashes, so a pre-Phase-5 dump diffs directly; the summary line
  reports `bytes=`, `logical_bytes=` and the per-map slot histogram.
- `tests/bone_influence_storage.cpp`: drives the real writer with synthetic
  work attributes (a narrow friend probe) and pins slots 1..8, defaults,
  normalisation, the implicit one-slot weight, the slot-major layout, the dense
  group order and the byte counts.

Acceptance, measured: logical hashes identical on all 160 maps of the three
projects; PNGs bit-identical on 10 frames; resident bytes 4.60x / 3.29x / 5.73x;
build cost unchanged; `tests/bone_influence_storage` passes all 1..8 cases.

### Phase 6 — The bone palette and its cap (D8) — done

- `src/core/PosePalette.h`: `kMaxCount` 32 -> **256** (the uint8 index
  ceiling), and the palette arrays are sized to the built skeleton, with
  `usedCount()`.
- `src/core/BoneKey.cpp`: `BoneKey::Cache` uses `PosePalette::kMaxCount`
  instead of its own literal.
- `assets/shaders/MeshTransformVert.glsl`: the palette is
  `layout(std140) uniform BonePalette { vec4 uBoneDualQuat[MAX_BONES * 2]; }`.
  The unused LBS variation keeps its 32 matrices with the reason stated.
- `src/core/MeshTransformerResource.cpp`: sets `MAX_BONES` from
  `PosePalette::kMaxCount` and binds the block once per program.
- `src/core/MeshTransformer.cpp`: backs the whole block once, uploads the
  required prefix each skinned draw, and identity-pads a palette shorter than
  `BoneInfluenceMap::boneCount()` (a cleared palette). `TimeKeyBlender`'s
  binding path uses the same fallback.
- `src/ctrl/ProjectLoader.{h,cpp}` / `src/ctrl/System.cpp` /
  `src/gui/MainWindow.cpp`: `warnings()` on the loader, returned on a successful
  open, shown once through the existing warning popup (and the app log) on every
  open path. A skeleton over 256 names the node, its frame and the count.
- `tools/render_probe`: `--bone-stats` (the census instrument) and loader
  warnings printed after a successful load.
- `tests/bone_cap.cpp`: the 33/65/257/32-bone gate described in D8; registered
  in `tests/CMakeLists.txt`, so the suite picks it up.
- `tests/mesh_palette_block.cpp`: the GL gate (201-bone block, index 200);
  registered in `tests/CMakeLists.txt` and in `run_suites.sh`'s xvfb list.

Acceptance, measured: the census (36 projects) found one key above the old 32;
its map hash changed alone; its frames 1-9 move only in the wing region (max 52
on 1097 pixels); 65 bones keep all 65; 257 stops at 256; the 32-bone control
reproduces the old defect; the block path is bit-identical to the uniform array
on ten frames each of `2.anie` and `skel_practice_12`, at the same frame time;
`tests/bone_cap` is green; the loader's warning was exercised with a temporary
32-cap build. See §7.

## 6. Verification commands

```bash
# build (one configuration at a time)
cmake --build build --config Debug -j"$(nproc)"
ninja -C build 'src/gui/update_translations:Debug'   # only if any tr() text changed

# focused gates while iterating
./build/tests/Debug/bonekey_regression
./build/tests/Debug/bone_cap           # the skeleton cap; no project, no GL
xvfb-run -a ./build/tests/Debug/mesh_palette_block   # the block at bone index 200
./build/tests/Debug/bone_polygon_index   # exact containment; no project, no GL
QT_QPA_PLATFORM=offscreen ./build/tests/Debug/bonekey_regression   # if it constructs widgets
xvfb-run -a ./build/tests/Debug/<target>                           # any target that builds GL objects

# the build's reference oracle and cost (joins every cache; no display)
QT_QPA_PLATFORM=offscreen ./build/tests/Debug/render_probe --project <file.anie> \
    --dump-influence /tmp/influence.txt
QT_QPA_PLATFORM=offscreen ./build/tests/Debug/render_probe --project <file.anie> \
    --bench-influence 6

# the rig-size census: one line per bone key, then BONE_STATS (cap and over-cap count)
QT_QPA_PLATFORM=offscreen ./build/tests/Debug/render_probe --project <file.anie> --bone-stats

# the whole suite at the end of a coherent change
scripts/run_suites.sh Debug

# end-to-end skinning proof (needs a project with bones)
xvfb-run -a ./build/tests/Debug/render_probe --project <file.anie> --out /tmp/before --size 640x360 --frames 0..3
# compare PNGs with a local script; report max and mean channel delta, do not eyeball
```

Traps that have already cost runs here: a stale binary reads as "no effect"
(check the binary mtime or `strings`), and GL gates need a display
(`xvfb-run`, or `scripts/gui/gpu_headless.sh` when the question involves real
GPU sampling).

## 7. Measured results

Project A = `tart.anie` (small skinned character: 15 layers, 11 caches, 3,100
cached vertices, 11 BoneKeys). Project B = `2.anie` (large project: 38
BoneKeys; the file persists 129 caches / 262,165 vertices, a rebuild produces
130 caches / the same vertex total - the extra cache has zero vertices).
Both are 0.8 files from the user's dataset, so they also exercise the legacy
discard path. Debug build throughout; `render_probe --require-gpu` reported the
same radeonsi renderer in every run. "Old" is a worktree built at HEAD with the
throwaway counter; "new" is this change. Load time is `/usr/bin/time` around
`render_probe --list-layers` (3 runs each); rebuild time is the same
uncommitted stopwatch with the loader rebuild disabled in the new build, so
both sides run the same `resetCaches` code path.

| metric | project A (`tart`) | project B (`2.anie`) |
|---|---|---|
| file size, old-build re-save → new-build re-save | 1,312,116 → 1,112,572 B (**-199,544; -15.2 %**) | 112,332,888 → 95,542,004 B (**-16,790,884; -14.9 %**) |
| cache bytes in the old file (64 B/vertex payload) | 198,400 B | 16,778,560 B |
| load wall time before → after | 1.03-1.07 s → 1.02-1.10 s | 12.30-12.39 s → 12.18-12.35 s |
| VmRSS after load before → after (Phase 1; not the Phase 2 packing) | 157,864-158,352 KB → 157,816-158,076 KB | 1,302,228-1,306,064 KB → 1,285,484-1,286,544 KB |
| influence rebuild time before → after | 1-2 ms → 1-2 ms | 113-122 ms → 101-123 ms (same code; variance) |
| max PNG channel delta, Phase 1 | 0 (4 frames, bit-identical) | fresh 0.20 cache: 0; original stale 0.8 file: **1 LSB on 6 px** (frame 1) and 1 px (frame 2), 0 on frame 0 |

Phase 2 (packed attributes), same projects plus `dinner lab.anie` (19 caches,
9,779 vertices) as a third control:

| metric | project A (`tart`) | project B (`2.anie`) | `dinner lab` |
|---|---|---|---|
| resident attribute bytes per cached vertex | 64 → **40 B** (1.6×) | 64 → **40 B** (1.6×) | 64 → **40 B** |
| resident attribute bytes for the project | 198,400 → **124,000 B** (-74,400) | 16,778,560 → **10,486,600 B** (-6,291,960) | 625,856 → **391,160 B** (-234,696) |
| process max RSS, Phase 1 → Phase 2 | 158,324-158,652 → 158,244-158,548 KB (no resolved delta) | 1,284,896-1,286,848 → 1,286,128-1,286,892 KB (overlap) | not measured |
| max PNG delta, shipped fallback (frames 0..n) | **0** (4 frames) | **0** (3 frames) | **0** (3 frames) |
| max PNG delta, rejected unorm16 weights (24 B/vtx) | 1 LSB (3 / 3 / 7 px on frames 1-3) | 3 / 12 / 4 per frame; 7 / 61 / 13 px >1 LSB | 2 LSB (1 / 0 / 3 px >1) |

The shipped Phase 2 representation is packed `uint8` indices (bound with
`glVertexAttribIPointer`, shader `ivec4`) plus `float32` weights (a `vec4`):
40 B/vertex, bit-identical on all 10 frames. The Phase 2 rows were re-baselined
in the same session as its run; process RSS moves by a few MB between runs, and
the Phase 1 table above records a different session's ranges. The unorm16 weights (24 B/vertex)
were built and measured first; the deltas above are small in aggregate (2.anie
frame 1 mean 0.0006) and sit on high-contrast edges, but they exceed the plan's
1-LSB gate, so the weights stayed float32. The 2-4× memory goal is therefore
**not met** (1.6×); the array bytes are the precise delta because process max
RSS is dominated by the image-decode peak.

The pixel proof has three parts, so a round trip cannot excuse itself:

1. The old build's **fresh** 0.20 copy (`2_fresh020.anie`, written immediately
   after a control-side `resetCaches`) versus the new build's loader rebuild:
   **max 0** on every sampled frame of both projects. This is the statement
   that the persisted copy is redundant when fresh.
2. The original 0.8 file versus the new rebuild: `tart` max 0; `2.anie` max 1
   LSB on 6/1 pixels. The file is stale: it has 129 caches where a rebuild
   makes 130 (one zero-vertex cache), and a 0.20 save after the same rebuild is
   exactly 92 B larger - one zero-vertex record. The GUI rebuilt that cache on
   open before this change too, so the user-visible path is unchanged.
3. A 0.21 save → load → rebuild of `2.anie` is bit-identical to the original
   rebuild (3 frames, max 0), so the new format round-trips its derived state
   by recomputation.

Gates: `tests/bonekey_regression` passes (legacy skip + sentinel, 0.21 no
section, negative/huge counts rejected); `roundtrip` loads and re-saves
`tart.anie` and `2.anie` (0 failures); `render_probe --list-layers` gives the
same layer listing before and after on both.

`tests/bone_influence_storage` pins the packed layout and default weights on the CPU
side; the GL binding is exercised by the `render_probe` frames above.

### Phase 3 — build cost

Instrument: `render_probe --bench-influence 6` (rep 0 is cold and discarded;
reps 1-5 quoted), which calls every bone key's `resetCaches` and then joins every
cache. "Join" is the accessor wait; "total" also includes the cache-list setup
and task pushes. The same run with `--dump-influence` writes the SHA-256 of every
cache's four arrays: the files are **identical** before and after on all three
projects (`2.anie` 130 maps / 262,165 vertices, `dinner lab` 19 / 9,779, `tart`
11 / 3,100), so this is byte identity, not a tolerance.

| project | build | join before -> after | total before -> after |
|---|---|---|---|
| `2.anie` | Debug | 59.7-62.0 -> **42.7-43.9 ms** | 72.7-76.0 -> 55.7-57.2 ms |
| `2.anie` | Release | 36.3-38.4 -> **8.6-10.8 ms** | 41.0-42.6 -> 12.2-15.0 ms |
| `dinner lab` | Debug | not split | 7.0-7.2 -> 5.0-5.1 ms |
| `tart` | Debug | ~1.08 -> ~1.08 ms | 1.6-1.8 -> 1.56-1.61 ms |

The Release `2.anie` join is **3.4-4.5x** smaller. `tart`, and `dinner lab` in
Release, sit on `thr::Task::wait`'s 1 ms poll (D4), so a sub-millisecond build is
not resolvable there; both are already at the poll floor.

Rejected, measured: the bone grid made the Debug join worse (~44 -> ~49 ms); the
per-map chunking was a total-wall wash (56 vs 57 ms - wait 42.7-43.9 ->
39.9-41.0, but schedule 13.2 -> 16.0). See D4.

`tests/bone_polygon_index` passes 77,889 comparisons against
`QPolygonF::containsPoint(point, OddEvenFill)`.

### Phase 5 — sparse slots

The slot distribution was measured through `render_probe --dump-influence` on
the three projects. Per-vertex counts on `2.anie`: **214,974 vertices at one
influence, 33,997 at two, 9,814 at three, 1,643 at four, 1,737 at five, and none
at six to eight**. Per-map maximum (`maps / vertices`):

| project | max 1 | max 2 | max 3 | max 4 | max 5 | max 6-8 |
|---|---|---|---|---|---|---|
| `2.anie` | 102 / 142,979 | 8 / 3,999 | 14 / 78,513 | 0 / 0 | 5 / 36,674 | 0 / 0 |
| `tart` | 1 / 48 | 7 / 2,331 | 2 / 369 | 0 / 0 | 1 / 352 | 0 / 0 |
| `dinner lab` | 11 / 3,287 | 8 / 6,492 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |

Resident attribute arrays after D7:

| project | cached vertices | dense bytes | sparse bytes | ratio | saved |
|---|---|---|---|---|---|
| `2.anie` | 262,165 | 10,486,600 | **2,277,514** | **4.60x** | 8,209,086 B (78.3 %) |
| `tart` | 3,100 | 124,000 | **37,693** | **3.29x** | 86,307 B (69.6 %) |
| `dinner lab` | 9,779 | 391,160 | **68,207** | **5.73x** | 322,953 B (82.6 %) |

The numbers are the `INFLUENCE_STORAGE ... bytes= logical_bytes=` line the probe
now prints: the sum of `BoneInfluenceMap::storedBytes()`, which is the array
storage only (the build's work buffer is transient). The **2-4x resident goal is
met and exceeded**.

Equality proof, two independent gates:

1. **Logical hashes.** `--dump-influence` expands a sparse map back to the
   eight-slot form before hashing, so the map lines are the same oracle Phase 3
   used. All **160 map lines** (130 + 19 + 11) are byte-identical to the dump
   written before D7; the sparse form stores exactly the values the dense form
   did.
2. **Pixels.** `render_probe --require-gpu`, 640x640, `2.anie` frames 0-2,
   `tart` 0-3, `dinner lab` 0-2 (10 frames): every channel delta is **0**
   (max 0, mean 0, zero differing pixels) against the pre-D7 renders. That
   covers the sparse shader at slot counts 1, 2, 3 and 5.

Build cost is unchanged within run noise (`--bench-influence 6`, Debug, reps
1-5): total **54.8-60.8 ms**, wait **41.6-45.3 ms**, against the Phase 3
measurement's 55.7-57.2 / 42.7-43.9. The new writer adds one max-scan over the
work buffer and writes fewer bytes; the cost is still the polygon containment.

`tests/bone_influence_storage` drives the real writer with synthetic work
attributes at every slot count 1..8 and pins the layout, defaults, normalisation,
the implicit one-slot weight and the dense group order.

Residual, stated rather than excused: Phase 5's three projects have no map at
the eight-slot cap. The dense path is the pre-D7 code, unit-gated at eight slots,
but it was not pixel-gated through GL by that phase's frames. Phase 6 found a
broader-dataset project at the cap (`skel_practice_12.anie`) and its before/after
frames exercise the dense path; see below.

`scripts/run_suites.sh Debug`: **78 pass, 1 fail** (76 before `bone_cap` and
`mesh_palette_block` were registered). The one failure is
`object_tree_panel` (6 checks around row selection/rename). It reproduces on a
clean worktree built at HEAD with none of this change - the first failures are
the row's accessible press action and its mouse press, before any influence
code - so it is pre-existing and left for a separate fix. Every other target,
including the GL/Canvas gates, passes.

### Phase 6 — the palette block and the cap

Census: `render_probe --bone-stats` on the **36** projects that load (37 tried;
`gif COPY2 proj.anie` does not load through the probe). Per-key maximum:

| project / key | bones |
|---|---|
| `skel_practice_12.anie` / "wings 1" | **33** |
| `tart.anie` / "hair" | 30 |
| `EmiliHappyIdle.anie` | 25 |
| every other key | <= 16 |

The 33-bone key is node 12, 28,131 vertices, and its influence map is **dense**
(`slots8=1`): the eight-slot path, from a real file. With the cap raised from 32
to 256:

| measurement | cap 32 -> 256 |
|---|---|
| `--dump-influence` map lines | **1 of 5 changed** (node 12 only; the other 4 and `INFLUENCE_STORAGE` bytes identical) |
| frames 1-9, 640x640 | max **6 / 24 / 52** on frames 1/2/3 (346 / 807 / 1097 px, bbox 527,420-626,577); frames 4-9 move the same wing; frame 0 is the rest pose and identical in a like-for-like run |
| bones built for the key | 32 (33rd dropped) -> **33** |

The changed region is the wing the 33rd bone encloses. This is the intended fix,
not a tolerance: the old code did not put the bone in the map at all (the 32-cap
control in `tests/bone_cap` reproduces the default index-0 result).

**The block path is exact and free.** On ten frames each of `2.anie` and
`skel_practice_12`, the uniform-block build and the pre-change uniform-array
build are bit-identical (max 0, every frame). Frame time, Release, 640x640,
frames 0-29, reps 1-3, frames 5-29 aggregated:

| project | uniform array | uniform block |
|---|---|---|
| `2.anie` | mean 53.79 ms, median 53.59, sd 1.61 | mean 54.03, median 53.89, sd 1.81 |
| `skel_practice_12` | mean 12.22 ms, median 12.01, sd 0.74 | mean 12.22, median 12.17, sd 0.65 |

A two-path design (uniform array up to 64 bones, block above) was built and
measured beside it (forced-block runs: `2.anie` 53.46/53.28, `skel` 12.22) and
then removed: the block buys the capacity and costs nothing measurable, so the
second program variant and the threshold were not worth keeping.

**The identity gap.** The first dynamic-palette build was *not* bit-identical on
`2.anie`: seven layers there have a **cleared palette** (no pose key at the
frame) while their influence map addresses bone 1. The old fixed array carried
an identity tail for every index past the used bones, so those layers rendered
undeformed; sizing the arrays to `usedCount()` read past them. The fix pads the
upload to `max(palette.usedCount(), BoneInfluenceMap::boneCount())` with
identity, and `TimeKeyBlender::setBindingMatrices` takes the same fallback for a
binding index past the palette. After it, every frame from 1 on is bit-identical
to the old build.

**A probe trap this exposed**: with `AE_FRAME_STATS` set, the density meter's
first frame is blank for some projects (`skel_practice_12`, `tart`), while
without it the frame has content. A pixel gate that compares frame 0 must run
both sides in the same stats mode; frames 1+ are unaffected. Measured on the
current build: stats-on frame 0 has 15 non-black pixels, stats-off 405,996.

The loader warning was verified with a temporary 32-bone cap build (the control,
not a shipped state): opening `skel_practice_12.anie` printed
`warning: The skeleton on "wings 1" (frame 0) has 33 bones; only the first 32
can deform a mesh.` and `--bone-stats` reported `over_cap_keys=1`. With 256 it
reports `over_cap_keys=0` for the whole dataset.

`tests/bone_cap` (target registered in `tests/CMakeLists.txt`): 19 checks, all
green - 33-bone retention in the map and the palette, 65 bones kept, 257 stopped
at 256, and the 32-bone control. `tests/mesh_palette_block` (GL, xvfb): 5
checks, all green - the block is declared, the 201-bone palette is above the old
128-bone ceiling, no GL error, bone 200 skins (1,0,0) to (0,1,0), and bone 1
stays identity.

## 8. Risks and open questions

1. **Rebuild in the loader changes a contract.** `ProjectLoader` is now
   "read + rebuild", not "read". If a caller appears that wants a pure read, it
   needs a flag; do not pre-build that flag (no second caller exists).
2. **GL context during rebuild.** Resolved by measurement: no context is
   needed; see D2. The rebuild ran from `ProjectLoader` in the GL harnesses and
   in the non-GL gate.
3. **Legacy discard path correctness.** A mis-sized skip desynchronises the rest
   of the file and the failure surfaces much later. The sentinel gate is in
   `tests/bonekey_regression`; keep it until the legacy branch is deleted.
4. **A persisted cache can be stale, and a fresh one is redundant.** The
   dataset's 0.8 files do not hold the current rebuild's cache (2.anie misses a
   zero-vertex cache). That is the case FOR rebuilding at load, not against it:
   the old GUI path already did, and `render_probe` now matches it. Do not use
   the absence of a byte difference on an old file as a gate; use a fresh 0.20
   control (as §7 does) or a save/reload equality test.
5. **`unorm16` weights were built and rejected.** The 24 B/vertex variant
   exceeded the 1-LSB gate on 2.anie (max 12; 61 px over 1 LSB in frame 1,
   7/13 in frames 0/2), and 2 LSB on dinner lab; the per-vertex error is
   tiny (mean 0.0006) but the gate is a gate. Shipped: packed uint8 indices +
   float32 weights (40 B/vertex, bit-identical). Do not re-pack the weights
   without a new equality measurement.
6. **The 2-4x resident goal is met by D7 (sparse slots).** The exact fallback
   alone gave 1.6x; storing only the used slots gives **3.3-5.7x** on the three
   projects (4.60x on `2.anie`) with byte-identical arrays and pixels, because
   most maps need one to three influences. The one-slot form is 1 B/vertex and
   the 2-7 slot form is 5 B/slot; the eight-slot cap still costs 40 B/vertex.
   Do not re-pack the weights with unorm16 to chase the last case - it already
   failed the pixel gate (item 5) and the cap is not what the projects spend.
   The dense eight-slot path was unit-gated and not pixel-gated by Phase 5;
   Phase 6 found a broader-dataset project at the cap
   (`skel_practice_12.anie`) and exercises it through GL (D8, §7).
7. **The bone cap is the uint8 index ceiling, 256 (D8, Phase 6).** The census
   found one key above the old 32 (33 bones) in 36 dataset projects; with the
   old cap it lost its last bone silently (there was no debug assert on that
   path either). `PosePalette::kMaxCount`, `BoneKey::Cache` and the shader's
   `MAX_BONES` come from one constant, and the palette is a `BonePalette`
   uniform block (8192 B at 256 bones, inside the guaranteed 16384 B) instead
   of a default-block array, which could not have held 128 bones without
   spending the whole guaranteed 1024-component budget. `tests/bone_cap` pins
   33, 65, 257 and the old 32-cap control; a project above 256 gets a load
   warning. The LBS skinning variation is still declared at 32 matrices
   (`callGL` pins the dual-quaternion path, so it is not compiled) and is not
   on the block path. Going past 256 is a different change: it needs a 16-bit
   bone index and a new storage layout, not a bigger constant.
8. **No release yet.** That is why §D1 may delete the writer path outright and
   keep a discard-only reader. It is not why the version could stay at 0.20: the
   reader must distinguish two layouts, so the minor bumps to 0.21.
9. **The containment index is a reimplementation of Qt's scan.** It is exact
   only while it keeps Qt's horizontal-edge `qFuzzyCompare`, the implicit close
   and the `x <= point.x()` crossing rule. `QPolygonF::containsPoint` stays the
   reference (the gate compares against it); do not "simplify" or vectorise the
   index without re-running `tests/bone_polygon_index` and the hash dump.
10. **The 1 ms wait poll is a floor, not a cost.** Any future "the rebuild is
   slower than X" claim on a small project must first establish that it is above
   `thr::Task::wait`'s poll interval; a change here would touch every async task,
   not just this build.

## 9. Handoff checklist

- [x] Phase 0 numbers recorded in §7
- [x] Phase 1 landed: writer emits no derived state; loader rebuilds; <= 0.20 files still open
- [x] `bonekey_regression` repurposed, not deleted (legacy discard sentinel + 0.21 no-section;
      ASan not run in this session - the discard path allocates nothing, keep the gate in any
      sanitizer run)
- [x] `render_probe` equality proven for both projects (fresh 0.20 control: max 0; original stale
      0.8 files: max 1 LSB on 6 px, explained in §7)
- [x] Phase 2 equality gate: shipped fallback (packed uint8 indices + float32
      weights) bit-identical on 10 frames / 3 projects; unorm16 weights rejected
      (max 12 on 61 px of 2.anie frame 1)
- [x] Phase 2 resident bytes recorded: 64 -> 40 B/vertex (1.6x); process max RSS
      unresolved against the image-decode peak; the 2-4x goal remains open
- [x] `tests/bone_influence_storage` added: pins the 40 B/vertex layout, the
      element sizes and the default weight/index values
- [x] Phase 3 byte-identical weights: SHA-256 of all four arrays on every map of
      `2.anie`, `dinner lab` and `tart`
- [x] Phase 3 rebuild time recorded: `2.anie` Release join 36.3-38.4 ->
      8.6-10.8 ms (3.4-4.5x); Debug 59.7-62.0 -> 42.7-43.9 ms
- [x] `tests/bone_polygon_index` added: 77,889 comparisons against
      `QPolygonF::containsPoint(OddEvenFill)`
- [x] The bone grid and per-map chunking built, measured and rejected (D4)
- [x] `render_probe --dump-influence` / `--bench-influence` are the permanent
      oracle and cost instruments
- [x] `FormatVersion.h` rule and 0.21 history line
- [x] `AGENTS.md` note added
- [x] Legacy discard branch marked with the retirement rule (delete after first release)
- [x] Phase 5 distribution measured: 2.anie per-vertex 1/2/3/4/5 = 214,974 /
      33,997 / 9,814 / 1,643 / 1,737, none at 6-8; per-map maxima recorded in §7
- [x] Phase 5 landed: per-map slot count, slot-major sparse 1-7 slots, one slot
      is 1 B/vertex with an implicit weight, dense 8 kept unchanged
- [x] Logical SHA-256 identity on all 160 maps of `2.anie`, `dinner lab`, `tart`
- [x] PNG equality at slot counts 1, 2, 3 and 5: 10 frames, max 0 / mean 0
- [x] Resident bytes recorded: 4.60x / 3.29x / 5.73x (the 2-4x goal is met)
- [x] Build cost unchanged: 2.anie Debug wait 41.6-45.3 ms against 42.7-43.9
- [x] `tests/bone_influence_storage` drives the writer at slots 1-8
- [x] `render_probe --dump-influence` keeps the logical eight-slot oracle;
      `INFLUENCE_STORAGE` reports resident vs logical bytes and the slot histogram
- [x] Phase 6 census: all 36 dataset projects measured with `--bone-stats`; one
      33-bone key (`skel_practice_12`), next 30 and 25, every other <= 16
- [x] Phase 6 landed: `PosePalette::kMaxCount = 256` (the uint8 index ceiling),
      a `BonePalette` uniform block bound once per program, the palette sized to
      its skeleton, and identity padding to the map's bone count; no format bump
- [x] `tests/bone_cap` added and registered: 33-bone retention in both the
      influence map and the palette, 65 bones kept, 257 stopped at 256, and the
      32-bone control
- [x] Real-project proof: exactly 1 of 5 map hashes changed; frames 1-9 move by
      max 6/24/52 on 346/807/1097 px in the wing region (the dense 8-slot path)
- [x] Block path equality: bit-identical to the uniform array on 10 frames each
      of `2.anie` and `skel_practice_12`; frame time unchanged (53.8 vs 54.0 ms
      on `2.anie`, 12.2 both on `skel`)
- [x] The identity gap on `2.anie` (seven layers with a cleared palette) is
      padded to identity, and the binding path takes the same fallback
- [x] `tests/mesh_palette_block` added: the real shader skins through a 201-bone
      block, index 200 -> (0,1,0) and index 1 identity, no GL error
- [x] A >256 skeleton now warns through `ProjectLoader::warnings()` on load; the
      path was exercised with a temporary 32-cap build (control)
- [x] The `AE_FRAME_STATS` first-frame trap recorded (frame 0 is blank under the
      density meter for some projects; compare like with like, or use frames 1+)
- [x] `render_probe --bone-stats` is the permanent rig-size instrument
- [x] Suite: 78 pass / 1 fail (including `bone_cap` and
      `mesh_palette_block`), the pre-existing `object_tree_panel` failure
