# Golden-image regression tests

Each subdirectory is one case:

```
<case>/case.json     manifest: input file, author, canvas, frame, diff tolerances, provenance
<case>/input.psd|ora generated import fixture (deterministic, committed)
<case>/expected.png  external ground truth, authored by a reference program (committed)
.scratch/            failed-case evidence dumps (git-ignored)
```

The S14 suite in `tools/verify_blur` imports `input.*` through the production
`ImageFileLoader`, renders one frame through the production composite wiring, and diffs
the render against `expected.png`. Cases without `expected.png` are reported as SKIP,
not failures.

## Workflow

```sh
# 1. (re)generate inputs + default case.json for new cases (never touches expected.png
#    or existing case.json):
tools/verify_blur/build_and_run.sh --no-build --golden-gen

# 2. (re)author goldens (dispatches per case.json "author"):
tests/golden/author_goldens.sh

# 3. run the whole harness (S14 included):
tools/verify_blur/build_and_run.sh
```

## Reference programs (case.json "author")

- `krita` (default) — `flatpak run --env=QT_QPA_PLATFORM=wayland org.kde.krita --export`.
  Strong PSD/ORA import. **Limitation: Krita ignores the opacity byte on PSD group
  records** (probe-verified on 5.3 flatpak: a group at opacity 0 still renders full
  strength), so fractional-opacity PSD group cases must not be Krita-authored.
- `gimp` — `flatpak run org.gimp.GIMP -i --batch-interpreter=plug-in-script-fu-eval`.
  Honors PSD group opacity (lerps like AE). Used for the fractional-opacity PSD group
  cases. Note GIMP 3.2's script-fu API differs from 2.10 (no drawable arg on
  `gimp-file-save`, drawable vectors in the car of multi-return procs).

## Adding a case

Add a `CaseSpec` to `caseSpecs()` in `tools/verify_blur/golden.cpp` (a PSD case, an ORA
case, or both from one shared `Doc`; pick `author` per the matrix above), then run steps
1-3. Eyeball the exported `expected.png` once after authoring — the golden is only as
trustworthy as the program that made it.

## case.json fields

- `input`, `canvas`, `frame` — what to import and render.
- `author` — which reference program produced `expected.png` (`krita` default, `gimp`).
- `borderCrop` (default 3) — border band excluded from the diff. Layer-mesh wedge
  artifacts live at the canvas edge because every generated layer bitmap is
  canvas-sized; keep it that way.
- `maxChannelDiff` (default 6), `meanDiff` (default 0.6) — per-channel tolerances
  absorbing 8-bit compositing quantization differences between AE and the reference
  render. Tune per case, never globally loosen.
- `xfail` — non-empty string marks a known, documented divergence (AE vs reference
  intent); the case reports XFAIL/XPASS and never fails the suite. Use sparingly and
  state the reason.

## Design constraints (enforced by the generator, keep them in mind when editing)

- Layers are full-canvas bitmaps with content painted inside (see borderCrop above).
- The bottom layer is fully opaque so the flatten has alpha 255 everywhere (sidesteps
  premultiplied-vs-straight alpha ambiguity between readback and PNG).
- Content rects are integer-aligned, no antialiasing: any divergence is blend math,
  not rasterization noise.
- Layer/group order in a `Doc` is top-first (matches the ORA spec; the PSD emitter
  reverses it into the format's bottom-first record order).
- PSD folder records mimic real writers (0x0 rect, flags 0x18, four empty RLE channels,
  blend key stamped on open and bounding records) — a zero-channel/full-canvas record
  loads in AE but Krita silently refuses to open the file.

## Authoring environment

Krita 5.3 and GIMP 3.2 (both flatpak) were used for the initial goldens. If a reference
program's PSD/ORA import changes, re-authoring may legitimately change goldens — review
the diffs like any code change.

