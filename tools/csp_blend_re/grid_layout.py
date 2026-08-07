"""Test-grid design shared by the PNG generator, the analyzer and the simulator.

One canvas, four PNG layers imported into Clip Studio Paint:

  bg.png          opaque background grid (whole canvas)
  add_glow.png    foreground patches, left half, blends over bg (mode: Add (Glow))
  glow_dodge.png  foreground patches, right half (mode: Glow Dodge)
  normal_strip.png normal-mode reference cells at the bottom

The canvas is organized in vertical BANDS, one per foreground alpha value
(0, 25, 50, 75, 100%). Each band repeats the full fg x bg grid in both halves,
so a single CSP export gives the whole (alpha, fg, bg) table for both modes.

The design discriminates:

1. SEPARABILITY - equal-luma foreground pairs (e.g. pure R vs gray 54) must
   give identical output for a luma-based (non-separable) mode, different for
   a channel-wise (separable) one.
2. ALPHA MODEL - blend-then-composite out = a*F(bg,fg) + (1-a)*bg  vs
   premultiplied  out = F(bg, a*fg), via the alpha bands.
3. GAMMA SPACE - mid-gray cells discriminate blending on sRGB values vs
   linear-space blending.
4. ABOVE-255 BEHAVIOR - bg+fg > 255 cells with asymmetric channels decide
   between clamp-at-255 and HDR-normalize (scale so max channel = 255).
5. EXTREMES - fg=0 / fg=255 columns and bg=0 rows separate dodge
   (bg/(1-fg)) from inverse-dodge (1-(1-bg)/fg) and additive variants.
"""

from __future__ import annotations

import numpy as np

# Rec.709 luma coefficients
LUM_R, LUM_G, LUM_B = 0.2126, 0.7152, 0.0722

# ---------------------------------------------------------------------------
# Patch colors. Each entry: (rgb, short label).
# ---------------------------------------------------------------------------

BG_COLORS = [
    ((0, 0, 0), "k"),
    ((255, 255, 255), "w"),
    ((18, 18, 18), "18"),          # luma = pure B
    ((54, 54, 54), "54"),          # luma = pure R
    ((128, 128, 128), "80"),
    ((182, 182, 182), "b6"),       # luma = pure G
    ((255, 0, 0), "R"),
    ((0, 255, 0), "G"),
    ((0, 0, 255), "B"),
    ((204, 153, 102), "c9a"),
    ((128, 0, 255), "80f"),
    ((51, 153, 230), "39e"),
]

FG_COLORS = [
    ((0, 0, 0), "k"),
    ((255, 255, 255), "w"),
    ((18, 18, 18), "18"),
    ((54, 54, 54), "54"),
    ((128, 128, 128), "80"),
    ((182, 182, 182), "b6"),
    ((255, 0, 0), "R"),
    ((0, 255, 0), "G"),
    ((0, 0, 255), "B"),
    ((0, 255, 255), "C"),
    ((255, 0, 255), "M"),
    ((255, 255, 0), "Y"),
    ((255, 102, 0), "f60"),        # luma ~128
    ((0, 178, 0), "0b2"),          # luma ~128
    ((128, 0, 255), "80f"),
    ((204, 153, 51), "c93"),
]

# (fg_idx, bg_idx) equal-luma fg pairs: a luma-based mode renders each pair
# identically on every background.
EQUAL_LUMA_FG_PAIRS = [
    (6, 3),   # red         vs gray 54  (luma 0.2126)
    (7, 5),   # green       vs gray 182 (luma 0.7152)
    (8, 2),   # blue        vs gray 18  (luma 0.0722)
    (12, 13), # (255,102,0) vs (0,178,0) (luma ~0.5)
]

# alpha values per band (0, 25, 50, 75, 100%)
ALPHAS = [0, 64, 128, 191, 255]

MODES = ["add_glow", "glow_dodge"]  # left half / right half

NORMAL_STRIP_FG = (200, 120, 40)
NORMAL_STRIP_BG = [(0, 0, 0), (54, 54, 54), (255, 255, 255)]

# ---------------------------------------------------------------------------
# Geometry (single canvas, bands stacked vertically).
# ---------------------------------------------------------------------------

CELL = 40
MARGIN_L = 56
HALF_GAP = 32
MARGIN_R = 16
GRID_COLS = len(BG_COLORS)      # 12
GRID_ROWS = len(FG_COLORS)      # 16
GRID_W = GRID_COLS * CELL       # 480
GRID_H = GRID_ROWS * CELL       # 640
LEFT_X = MARGIN_L
RIGHT_X = LEFT_X + GRID_W + HALF_GAP

BAND_HEADER = 24                # per-band header row (labels)
BAND_GAP = 12
BAND_H = BAND_HEADER + GRID_H + BAND_GAP   # 676

STRIP_GAP = 6
STRIP_H = 40

W = RIGHT_X + GRID_W + MARGIN_R            # 1064
H = len(ALPHAS) * BAND_H + len(ALPHAS) * (STRIP_H + STRIP_GAP) + 16  # 3624

DARK = (40, 40, 40)
TEXT = (220, 220, 220)
STROKE = (30, 30, 30)

BAND0_Y = 0  # bands start at y = band * BAND_H


def band_y(band: int) -> int:
    return band * BAND_H


def cell_rect(band: int, fg_idx: int, bg_idx: int, mode: str) -> tuple[int, int, int, int]:
    """(x, y, w, h) of grid cell (fg row, bg col) in `mode` half of `band`."""
    x0 = LEFT_X if mode == "add_glow" else RIGHT_X
    x = x0 + bg_idx * CELL
    y = band_y(band) + BAND_HEADER + fg_idx * CELL
    return (x, y, CELL, CELL)


def strip_rect(band: int, bg_idx: int, mode: str) -> tuple[int, int, int, int]:
    """(x, y, w, h) of normal-strip cell: alpha band x 3 bg colors x 2 halves."""
    x0 = LEFT_X if mode == "add_glow" else RIGHT_X
    x = x0 + bg_idx * CELL
    y = len(ALPHAS) * BAND_H + band * (STRIP_H + STRIP_GAP)
    return (x, y, CELL, STRIP_H)


def sample_center(img, rect: tuple[int, int, int, int], size: int = 8) -> tuple[int, int, int]:
    """Median of the central size x size block (robust to cell-border AA)."""
    x, y, w, h = rect
    cx, cy = x + w // 2, y + h // 2
    s = size // 2
    block = img[cy - s : cy + s, cx - s : cx + s]
    out = []
    for ch in range(3):
        vals = np.sort(block[..., ch].ravel())
        out.append(int(vals[len(vals) // 2]))
    return tuple(out)


def rgb_to_luma(c: tuple[int, int, int]) -> float:
    return LUM_R * c[0] + LUM_G * c[1] + LUM_B * c[2]


def srgb_to_lin(c):
    x = np.asarray(c, float) / 255.0
    return np.where(x <= 0.04045, x / 12.92, ((x + 0.055) / 1.055) ** 2.4)


def lin_to_srgb(x):
    x = np.asarray(x, float)
    v = np.where(x <= 0.0031308, 12.92 * x, 1.055 * (x ** (1 / 2.4)) - 0.055)
    return v * 255.0


def gamma22_to_lin(c):
    return (np.asarray(c, float) / 255.0) ** 2.2


def lin_to_gamma22(x):
    return (np.asarray(x, float) ** (1 / 2.2)) * 255.0
