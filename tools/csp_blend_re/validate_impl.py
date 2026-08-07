"""Validate the AnimeEffects implementation against the measured CSP data.

Compares the composite rendered by the production pipeline
(build-asan/blend_render.png via tools/verify_blend) cell by cell against the
CSP measurements in exports/export.csv, for both new blend modes and all
alpha bands.

Usage: uv run python validate_impl.py [render.png]
"""

from __future__ import annotations

import csv
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import grid_layout as G

ROOT = Path(__file__).parent
RENDER = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT.parents[1] / "build-asan" / "blend_render.png"


def load_csv(path: Path):
    rows = {}
    with open(path) as f:
        for r in csv.DictReader(f):
            key = (r["mode"], int(r["alpha"]), tuple(map(int, r["fg_rgb"].split("/"))),
                   tuple(map(int, r["bg_rgb"].split("/"))))
            rows[key] = (int(r["out_r"]), int(r["out_g"]), int(r["out_b"]))
    return rows


def main() -> int:
    render = np.asarray(Image.open(RENDER).convert("RGB"), np.uint8)
    if render.shape[:2] != (G.H, G.W):
        raise SystemExit(f"render size {render.shape[:2]} != expected {(G.H, G.W)}")
    measured = load_csv(ROOT / "exports" / "export.csv")

    worst = {"add_glow": (0, None), "glow_dodge": (0, None)}
    cells = {"add_glow": 0, "glow_dodge": 0}
    mismatches = {"add_glow": [], "glow_dodge": []}
    for mode in G.MODES:
        for band in range(len(G.ALPHAS)):
            for fg_idx in range(G.GRID_ROWS):
                for bg_idx in range(G.GRID_COLS):
                    x, y, _, _ = G.cell_rect(band, fg_idx, bg_idx, mode)
                    got = G.sample_center(render, (x, y, G.CELL, G.CELL))
                    key = (mode, G.ALPHAS[band], G.FG_COLORS[fg_idx][0], G.BG_COLORS[bg_idx][0])
                    exp = measured[key]
                    d = max(abs(a - b) for a, b in zip(got, exp))
                    cells[mode] += 1
                    if d > worst[mode][0]:
                        worst[mode] = (d, key)
                    if d > 1:
                        mismatches[mode].append((key, got, exp, d))

    fails = 0
    for mode in G.MODES:
        w, wk = worst[mode]
        print(f"{mode}: {cells[mode]} cells, worst diff {w} at {wk}, "
              f"{len(mismatches[mode])} cells off by >1")
        if w > 1:
            fails += 1
        for key, got, exp, d in mismatches[mode][:6]:
            print(f"   {key}: render={got} csp={exp} diff={d}")
    return 1 if fails else 0


if __name__ == "__main__":
    raise SystemExit(main())
