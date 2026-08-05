# verify_blur — directional blur verification harness

Verifies the directional blur feature (`BlurKey` blurX/blurY/angle, `core/WorldBlurMath.h`,
`core/FilterFrame` separable Gaussian + downsample ladder) by rendering real scenes through
the production pipeline and comparing against an independent CPU reference, under ASan.

## Run

```sh
tools/verify_blur/build_and_run.sh        # rebuild libs + harness, then run
tools/verify_blur/build_and_run.sh --no-build
```

Must run from the repo root (the script cds there; shaders load from `./data/shader`).
Tries `QT_QPA_PLATFORM=offscreen` first, falls back to X11 (`DISPLAY=:0`) when offscreen
GL is unavailable. Exit code: 0 = all pass, 1 = test failures, 2 = GL bootstrap failure
(triggers the X11 fallback).

## Suites

- **S1 math** — `worldBlurEllipse` over real node chains (via the production
  `TimeCacheAccessor`) vs an independent double-precision Jacobi SVD: directed edge cases
  (zero/negative scales, degenerate radii, angle wrap, nesting) and 800 randomized chains,
  plus invariants (orthonormality, `|det M| == s1*s2`) and a brute-force max-stretch check.
- **S2 ladder** — `FilterFrame::blurLadderLevel` vs an independent reference over a
  boundary grid (radii around 16*2^k, tiny/odd buffer sizes), with level invariants.
- **S3 blending** — `TimeKeyBlender` blur segments: radii lerp, accumulated angle (multi-turn spins keep their turns, like the rotate key)
  easing, exact-keyframe/after-last/no-key cases.
- **S4 render** — full production renders vs the CPU reference blur (premultiplied,
  GL bilinear, CLAMP_TO_EDGE, ladder replica): plain/transformed layers, the edge case of
  a **layer inside a folder with transforms on both**, blur on transformed folders, three
  levels of nesting, ladder radii, camera zoom, **camera rotation and x-flip**, odd/tiny
  canvases, zero minor axis.
- **S5 orientation** — GPU ground truth: second-moment angle of a blurred dot (including
  folder+layer rotations and **camera rotation/flip** — the measured screen angle must
  equal the world angle mapped by `R(camRot) * diag(flip ? -1 : 1, 1)`), the
  `(rx,ry,a) == (ry,rx,a+90)` identity, isotropic `amount` vs directional equal-radii
  byte identity, layer-blur vs folder-blur byte identity.
- **S6 gating** — blended radius <= 0.5 must render byte-identical to no key (layer and
  folder); keys activate at their frame.
- **S7 interactions** — blur crossed with other features: a clippee pair inside a blurred
  folder and a blurred layer that is itself clipped (clipping propagates into the
  composite), **folder blur nested inside folder blur** (inner ladder/direct sequencing),
  Multiply/Screen blend modes over a captured background (`DestinationTexturizer` +
  `blendColor` + the `SRC_ALPHA` framebuffer blend replicated), and HSV ordering: a
  layer's HSV is baked before the blur (`ref = blur(plain)`), a folder's HSV is applied
  at presentation after the blur (`ref = hsv(blur(plain))`). NOTE: the clip cells push
  the **clippee first, clipper second** — children are walked back-to-front
  (`collectRenderUnits` iterates `rbegin`→`rend`), so the first-pushed sibling renders
  on top, and `isClipper` requires the clippee to be the clipper's `prevSib()`. Getting
  this backwards silently skips the clippee (no node is a clipper), which made the
  folder case pass vacuously until the order was fixed.
- **S8 serialization** — blur keys through the binary project format:
  `TimeLine::serialize`/`deserialize` round-trip at the current version (radii, angle,
  directional flag, easing per key, alongside another key type), plus the flat-layout
  check (same bytes read back identically under the pre-blur 0.8 version label; blur
  shipped as a single feature with one bump, 8 -> 9, so no historical layout exists).
  The clipboard JSON path is file-local
  (`gui/TimeLineEditorWidget.cpp` copy, `ctrl/TimeLineEditor.cpp` paste) and was
  verified by inspection: both sides use Amount/BlurX/BlurY/Angle; paste falls back to
  Amount when BlurX is absent (the directional flag is inferred, not serialized).
- **S9 export** — the real `ctrl::Exporter` image-sequence path (no ffmpeg) on a
  `core::Project` with a blurred clippee, a blurred transformed folder and a
  frame-varying blur: exported PNGs must equal the interactive-path render at the same
  frames (PNG is straight-alpha, the scene is premultiplied — premultiply back before
  comparing), and the two frames must differ across the key change.
- **S10 performance** — informational snapshot: filtered vs unfiltered ms/frame at
  1920x1080 (direct-path and ladder radii), 10 timed renders each including one
  readback.

## Layout

- `verify_blur.cpp` — main + all suites.
- `scene.h/.cpp` — offscreen GL fixture (mirrors `MainDisplayWidget::paintGL`) and tree
  builders (transforms via default SRT keys, blur keys via `TimeLine::createPusher`).
- `cpu_ref.h` — the independent CPU reference (Gaussian pass, resample/ladder replicas,
  Jacobi SVD, second-moment measurement, HSV adjust and blend-present replicas).
  Written from the documented semantics, not from the production code, so production
  bugs surface as mismatches.

Coordinates: readbacks are GL-ordered (row 0 = canvas bottom). The composite slot holds
the scene as rendered through the camera — view linear part `R(rotate) * diag(flip ? -1
: 1, 1)` — y-flipped in texture rows. So a world-space direction `(dx, dy)` maps to the
slot as `flipY(R(rot) * (flip ? -dx : dx, dy))`, which reduces to `(dx, -dy)` for an
untransformed camera. The S4/S7 replicas apply this mapping independently of production
(via `scene::screenDirFromWorld`), so a sign/convention bug in production fails S4/S5.
