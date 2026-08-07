# Results — CSP "Add (Glow)" and "Glow Dodge", measured 2026-08-06

Source: one CSP export of the 4-layer PNG test kit (1064x3626), layers
`bg`, `add_glow` (mode Add (Glow) / 加算（発光）), `glow_dodge` (mode
Glow Dodge / 覆い焼き（グロー）), `normal_strip` (Normal). 960 grid cells per
mode (16 fg x 12 bg x 5 alphas) plus a 30-cell normal reference strip.

Data: `exports/export.csv`. Regeneration + verification: `make_test_images.py`,
`analyze.py` (self-test passes 48/48 simulated CSPs).

## Both modes share the same pipeline

- **Separable** per-channel blend, on 8-bit sRGB values directly (gamma space
  — no linear-light transfer).
- The layer color is **premultiplied by layer alpha and rounded to 8-bit
  before blending**:

      fg' = round(a * fg / 255)          # a = layer alpha 0..255

  Equivalent to `(a*fg + 127) // 255` (round-half-up; the exact half-up vs
  half-even tie-break is not distinguishable from 8-bit data).
- Fully transparent pixels are skipped (no blend applied), like Photoshop.

## Add (Glow)  —  identified exactly (max err 0.00 / 255)

    out = min(bg + fg', 255)

Plain additive with clamp at 255. NOT HDR-normalized, NOT "glow" tonemapped —
the glow difference vs plain Add is nothing but the name... (CSP's plain
"Add" mode uses the same math; verify with the plain-Add layer if you want a
screenshot comparison, but the measured formula is identical to what
AnimeEffects already implements for LinearDodge/Add.)

## Glow Dodge  —  identified exactly (max err 0.00 / 255)

    d   = max(255 - fg', 1)
    out = min( (bg * 255) // d , 255 )   # integer division: truncates

That is color dodge `min(bg/(1-fg), 1)` computed with truncating integer
division, with one deliberate edge case: at fg' = 255 the division is guarded
(d = 1), giving **255 only when bg > 0, else 0** — i.e. black stays black.
Photoshop's color dodge special-cases fg=1 to 255 regardless of bg; CSP does
not (white-on-black stays black).

## Notes for implementing in AnimeEffects (GLSL)

- Use `floor()` where CSP truncates: `floor(min(bg * 255.0 / max(255.0 - f, 1.0), 255.0))`.
- Premultiplied fg' rounding: `(a * fg + 127) / 255` (round-half-up) is
  consistent with every measured cell; plain round is fine (difference is
  unobservable with these alphas).
- The mode applies the blend after the layer's own alpha handling; in the
  AnimeEffects pipeline (straight colors, alpha composited after blend) the
  premultiply must happen before the blend, matching CSP's stored
  premultiplied layer pixels.
- Normal compositing in CSP rounds to nearest (observed in the reference
  strip), so only the blend step itself truncates.

## Verification artifacts

- `exports/export.png` — the CSP export used for these results.
- `exports/export.csv` — every measured cell (mode, alpha, fg, bg, out).
- `analyze.py --export ...` reproduces the report; `--simulate` runs the
  48-case self-test.
