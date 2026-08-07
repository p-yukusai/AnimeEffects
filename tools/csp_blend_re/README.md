# CSP blend-mode reverse engineering: Add (Glow) and Glow Dodge

**Status: solved.** Both modes are identified exactly (max error 0.00/255 over
960 measured cells each). See `RESULTS.md` for the formulas and implementation
notes for `LayerDrawingFrag.glsl`; `exports/export.csv` has every measured
cell.

Goal: find the exact math of Clip Studio Paint's **Add (Glow)**
(加算（発光）) and **Glow Dodge** (覆い焼き（グロー）) blend modes, which are not
publicly documented. The measured data will be used to implement these two
modes in AnimeEffects (`LayerDrawingFrag.glsl`).

The test is one canvas made of four PNG layers:

| file            | layer content                              | blend mode in CSP        |
|-----------------|--------------------------------------------|--------------------------|
| `bg.png`        | opaque background grid (whole canvas)      | Normal (leave as is)     |
| `add_glow.png`  | foreground patches, LEFT half              | **Add (Glow)** / 加算（発光） |
| `glow_dodge.png`| foreground patches, RIGHT half             | **Glow Dodge** / 覆い焼き（グロー） |
| `normal_strip.png` | reference cells at the bottom (normal)  | Normal (leave as is)     |

The canvas is 1064 x 3626 px. It is split into 5 horizontal **bands**, one per
foreground layer alpha (0%, 25%, 50%, 75%, 100%). Each band repeats the full
grid (16 foreground colors x 12 background colors) in both halves. One single
export therefore gives the whole (alpha, fg, bg) table for both blend modes,
plus a normal-mode reference strip.

## What the grid discriminates

- **Separability**: fg pairs with equal Rec.709 luma but different channels
  (pure R vs gray 54, pure G vs gray 182, pure B vs gray 18,
  (255,102,0) vs (0,178,0)). A luma-based (non-separable) mode must render
  each pair identically; a channel-wise mode renders them differently.
- **Alpha handling**: the alpha bands separate "blend first, then composite
  with alpha" from "premultiply fg by alpha, then blend".
- **Gamma space**: mid-gray cells distinguish blending on sRGB values directly
  vs after sRGB->linear transfer.
- **Over-255 behavior**: bg+fg > 255 cells with asymmetric channels
  (e.g. (204,153,102) bg + (204,153,51) fg) decide between clamp-at-255 and
  HDR-normalize (scale so max channel = 255).
- **Extremes**: fg=0 / fg=255 columns and bg=0 rows separate dodge
  (bg/(1-fg)) from inverse-dodge (1-(1-bg)/fg).

## What to do in CSP (one time per test run)

1. Create a new document: **1064 x 3626 px**, RGB, 8-bit (any DPI). If CSP
   asks for a color profile, pick sRGB if available (default is fine).
2. Import the four PNGs in order: `File > Import > Image...` (ファイル > 読み込み >
   画像). They become raster layers; make sure the top-left of each lands at
   the canvas top-left (small offsets are auto-detected, just don't rotate or
   scale them).
3. Select the `add_glow` layer: Layer Property (レイヤープロパティ) > Blend mode
   (描画モード) > **Add (Glow)** / **加算（発光）**.
4. Select the `glow_dodge` layer: Blend mode > **Glow Dodge** / **覆い焼き（グロー）**.
5. Leave `normal_strip` and `bg` on Normal. All layers 100% opacity, all
   visible, no masks, no clipping.
6. Export the whole canvas: `File > Export > PNG...` (ファイル > 書き出し > PNG...)
   at 100%, save as `export.png`.
7. Send `export.png` back.

Do **not** flatten, resize, or change any layer opacity. If the colors in the
`a=0%` band do not exactly match the background grid, stop and report it -
that indicates a color-profile conversion on import/export (the analyzer also
flags this automatically).

## Analyzing the result

```sh
uv run python make_test_images.py          # (re)generate the PNGs
uv run python analyze.py --export path/to/export.png
uv run python analyze.py --simulate        # self-test (48 simulated CSPs)
```

The analyzer prints:

- registration offset + anchor quality,
- controls: alpha-0 band vs pure background (catches transparent-pixel
  blending or color drift), normal strip vs expected straight-alpha composite,
- separability verdict (separable vs luma-based) from the equal-luma pairs,
- ranking of every candidate formula x gamma-space x alpha-model, and a
  VERDICT when one fits within 8-bit rounding (max error <= 2/255),
- full signature tables (hex colors at alpha=100%, alpha sweeps for key
  cells) for manual inspection when no candidate fits,
- `export.csv` with every measured cell.

## Candidate library (`hypotheses.py`)

Add (Glow):
- `add_clamp`  min(bg+fg, 1)
- `add_hdr_max`  (bg+fg) scaled so max channel = 1 when above 1
- `add_fgscale`  min(bg+fg,1) * 1/(1+fg)
- `add_norm_sum`  (bg+fg) scaled so channel sum = 1 when above 1
- `add_min_luma_scale`  soft highlight compression by luma

Glow Dodge:
- `dodge`  min(bg/(1-fg), 1), fg=1 -> white
- `dodge_inv`  1 - (1-bg)/fg, fg=0 -> black
- `dodge_hdr`  dodge without clamp, HDR-normalized
- `dodge_soft`  bg/(1-0.9*fg)
- `dodge_addmix`  50/50 dodge+add
- `dodge_lum_fg` / `dodge_lum_bg`  luma-driven (non-separable)

Each is tried in gamma, sRGB-linear and gamma-2.2 space, under two alpha
models: `blend_then_composite` and `premultiplied`.
