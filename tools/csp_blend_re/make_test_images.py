"""Generate the CSP blend-mode test as four PNGs (one canvas, layers stacked).

  bg.png           opaque background grid, whole canvas
  add_glow.png     fg patches at each alpha band, LEFT half  (set CSP mode
                   "Add (Glow)" / "加算（発光）" on this layer)
  glow_dodge.png   same patches, RIGHT half (mode "Glow Dodge" / "覆い焼き（グロー）")
  normal_strip.png normal-mode reference cells at the bottom (no blend mode)

All PNGs are untagged (no ICC) so CSP does no color conversion on import;
create the canvas as sRGB RGB/8-bit and export PNG from the whole canvas.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

import grid_layout as G


def _font(size: int = 16):
    try:
        return ImageFont.load_default(size=size)
    except TypeError:
        return ImageFont.load_default()


def build_bg() -> np.ndarray:
    rgba = np.zeros((G.H, G.W, 4), dtype=np.uint8)
    rgba[..., :3] = G.DARK
    rgba[..., 3] = 255

    # grid cells (all bands, both halves) + 1px stroke
    for band in range(len(G.ALPHAS)):
        for fg_idx in range(G.GRID_ROWS):
            for bg_idx, (bgc, _) in enumerate(G.BG_COLORS):
                for mode in G.MODES:
                    x, y, w, h = G.cell_rect(band, fg_idx, bg_idx, mode)
                    rgba[y : y + h, x : x + w, :3] = bgc
                    rgba[y, x : x + w, :3] = G.STROKE
                    rgba[y + h - 1, x : x + w, :3] = G.STROKE
                    rgba[y : y + h, x, :3] = G.STROKE
                    rgba[y : y + h, x + w - 1, :3] = G.STROKE

    # normal strip cells
    for band in range(len(G.ALPHAS)):
        for bg_idx, bgc in enumerate(G.NORMAL_STRIP_BG):
            for mode in G.MODES:
                x, y, w, h = G.strip_rect(band, bg_idx, mode)
                rgba[y : y + h, x : x + w, :3] = bgc
                rgba[y, x : x + w, :3] = G.STROKE
                rgba[y + h - 1, x : x + w, :3] = G.STROKE
                rgba[y : y + h, x, :3] = G.STROKE
                rgba[y : y + h, x + w - 1, :3] = G.STROKE

    # labels
    pil = Image.fromarray(rgba[..., :3], "RGB")
    d = ImageDraw.Draw(pil)
    f16, f14 = _font(16), _font(14)
    for band, alpha in enumerate(G.ALPHAS):
        y0 = G.band_y(band)
        d.text((4, y0 + 4), f"a={alpha // 255 * 100}%", font=f16, fill=(255, 200, 100))
        for bg_idx, (_, lab) in enumerate(G.BG_COLORS):
            d.text((G.LEFT_X + bg_idx * G.CELL + 4, y0 + 5), lab, font=f16, fill=G.TEXT)
            d.text((G.RIGHT_X + bg_idx * G.CELL + 4, y0 + 5), lab, font=f16, fill=G.TEXT)
        for fg_idx, (_, lab) in enumerate(G.FG_COLORS):
            y = y0 + G.BAND_HEADER + fg_idx * G.CELL + 11
            d.text((4, y), lab, font=f14, fill=G.TEXT)
        d.text((G.LEFT_X + 8, y0 + 5), "ADD (GLOW)", font=f14, fill=(255, 200, 100))
        d.text((G.RIGHT_X + 8, y0 + 5), "GLOW DODGE", font=f14, fill=(255, 200, 100))
    for band, alpha in enumerate(G.ALPHAS):
        y = len(G.ALPHAS) * G.BAND_H + band * (G.STRIP_H + G.STRIP_GAP) + 13
        d.text((4, y), f"N{alpha // 255 * 100}%", font=_font(11), fill=G.TEXT)
    rgba[..., :3] = np.array(pil)
    return rgba


def build_blend_layer(mode: str) -> np.ndarray:
    """fg patches for one half, all alpha bands; transparent elsewhere."""
    rgba = np.zeros((G.H, G.W, 4), dtype=np.uint8)
    for band, alpha in enumerate(G.ALPHAS):
        for fg_idx in range(G.GRID_ROWS):
            for bg_idx in range(G.GRID_COLS):
                x, y, w, h = G.cell_rect(band, fg_idx, bg_idx, mode)
                rgba[y : y + h, x : x + w, :3] = G.FG_COLORS[fg_idx][0]
                rgba[y : y + h, x : x + w, 3] = alpha
    return rgba


def build_normal_strip() -> np.ndarray:
    rgba = np.zeros((G.H, G.W, 4), dtype=np.uint8)
    for band, alpha in enumerate(G.ALPHAS):
        for bg_idx in range(3):
            for mode in G.MODES:
                x, y, w, h = G.strip_rect(band, bg_idx, mode)
                # inset 4px so the bg stroke ring stays visible (anchor for
                # registration and a sanity check that layers are aligned)
                rgba[y + 4 : y + h - 4, x + 4 : x + w - 4, :3] = G.NORMAL_STRIP_FG
                rgba[y + 4 : y + h - 4, x + 4 : x + w - 4, 3] = alpha
    return rgba


def save_rgba(path: Path, arr: np.ndarray) -> None:
    Image.fromarray(arr, "RGBA").save(path)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(Path(__file__).parent / "out"))
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    for name, arr in [
        ("bg", build_bg()),
        ("add_glow", build_blend_layer("add_glow")),
        ("glow_dodge", build_blend_layer("glow_dodge")),
        ("normal_strip", build_normal_strip()),
    ]:
        p = out / f"{name}.png"
        save_rgba(p, arr)
        print(f"wrote {p} ({p.stat().st_size / 1024:.0f} KiB)")


if __name__ == "__main__":
    main()
