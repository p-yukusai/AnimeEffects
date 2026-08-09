"""Analyze a CSP export of the blend-test canvas.

Usage:
  uv run python analyze.py --export path/to/export.png
  uv run python analyze.py --simulate          # self-test against simulated CSPs

The export must be the composite of the four test PNGs as layers (bg,
add_glow with mode Add (Glow), glow_dodge with mode Glow Dodge, normal_strip
as Normal), exported from the full canvas.

Output: controls (alpha-0 band, normal strip), separability, candidate
formula ranking per mode, verdict, and a signature dump for manual analysis.
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import numpy as np
from PIL import Image

import grid_layout as G
import hypotheses as H

EXPORT_W, EXPORT_H = G.W, G.H
VERDICT_TOL = 2.0


def load_export(path: str) -> np.ndarray:
    img = Image.open(path)
    if img.size != (EXPORT_W, EXPORT_H):
        raise SystemExit(
            f"export size {img.size} != expected {(EXPORT_W, EXPORT_H)} - "
            "did you export the whole canvas at 100%?"
        )
    arr = np.asarray(img.convert("RGB"), dtype=np.uint8)
    return arr


# ---------------------------------------------------------------------------
# registration: find the layer offset so cells land on the grid
# ---------------------------------------------------------------------------

def _anchor_sets():
    """(kind, index arrays, expected values) for registration scoring."""
    sets = []
    # 1) alpha=0 grid cells must equal bg colors (blend of transparent fg)
    idx = []
    exp = []
    for mode in G.MODES:
        for fg_idx in range(G.GRID_ROWS):
            for bg_idx, (bgc, _) in enumerate(G.BG_COLORS):
                x, y, _, _ = G.cell_rect(0, fg_idx, bg_idx, mode)
                idx.append((y + 20, x + 20))
                exp.append(bgc)
    sets.append((np.array(idx), np.array(exp, np.uint8)))
    # 2) normal strip cells: expected straight-alpha composite
    idx = []
    exp = []
    for band, alpha in enumerate(G.ALPHAS):
        for bg_idx, bgc in enumerate(G.NORMAL_STRIP_BG):
            for mode in G.MODES:
                x, y, _, _ = G.strip_rect(band, bg_idx, mode)
                fg = G.NORMAL_STRIP_FG
                e = tuple(np.rint((alpha * np.array(fg) + (255 - alpha) * np.array(bgc)) / 255).astype(int))
                idx.append((y + 20, x + 20))
                exp.append(e)
    sets.append((np.array(idx), np.array(exp, np.uint8)))
    # 3) dark field points (right margin / band gaps): must stay (40,40,40)
    idx = []
    exp = []
    for band in range(len(G.ALPHAS)):
        y = G.band_y(band) + G.BAND_HEADER + 5
        idx.append((y, EXPORT_W - 8))
        idx.append((y + G.GRID_H // 2, 3))
        exp.append(G.DARK)
        exp.append(G.DARK)
    sets.append((np.array(idx), np.array(exp, np.uint8)))
    # 4) cell-border strokes of band-0 grid and strip cells: 1px (30,30,30)
    #    edges pin the offset to the pixel (flat cell interiors are periodic)
    idx = []
    exp = []
    for mode in G.MODES:
        for fg_idx in range(G.GRID_ROWS):
            for bg_idx in range(G.GRID_COLS):
                x, y, _, _ = G.cell_rect(0, fg_idx, bg_idx, mode)
                for (px, py) in [(x + 20, y), (x + 20, y + G.CELL - 1),
                                 (x, y + 20), (x + G.CELL - 1, y + 20)]:
                    idx.append((py, px))
                    exp.append(G.STROKE)
    for band in range(len(G.ALPHAS)):
        for bg_idx in range(3):
            for mode in G.MODES:
                x, y, _, _ = G.strip_rect(band, bg_idx, mode)
                for (px, py) in [(x + 20, y), (x + 20, y + G.STRIP_H - 1),
                                 (x, y + 20), (x + G.CELL - 1, y + 20)]:
                    idx.append((py, px))
                    exp.append(G.STROKE)
    sets.append((np.array(idx), np.array(exp, np.uint8)))
    return sets


def find_offset(arr: np.ndarray):
    """Brute-force (dx, dy) in [-24, 24] maximizing exact anchor matches,
    fully vectorized."""
    sets = _anchor_sets()
    offsets = np.array([(dy, dx) for dy in range(-24, 25) for dx in range(-24, 25)])
    total_score = np.zeros(len(offsets), dtype=np.int32)
    for idx, exp in sets:
        yy = idx[:, 0][None, :] + offsets[:, 0][:, None]
        xx = idx[:, 1][None, :] + offsets[:, 1][:, None]
        inside = (yy >= 0) & (yy < EXPORT_H) & (xx >= 0) & (xx < EXPORT_W)
        got = arr[np.clip(yy, 0, EXPORT_H - 1), np.clip(xx, 0, EXPORT_W - 1)]
        ok = np.all(got == exp[None, :, :], axis=2)
        ok = np.where(inside, ok, False)
        total_score += ok.sum(axis=1)
    best_i = int(np.argmax(total_score))
    dy, dx = int(offsets[best_i, 0]), int(offsets[best_i, 1])
    return int(total_score[best_i]), dx, dy


# ---------------------------------------------------------------------------
# dataset
# ---------------------------------------------------------------------------

def sample_dataset(arr: np.ndarray, dx: int, dy: int):
    """cells: list of (mode, band, fg_idx, bg_idx, out)"""
    cells = []
    for mode in G.MODES:
        for band in range(len(G.ALPHAS)):
            for fg_idx in range(G.GRID_ROWS):
                for bg_idx in range(G.GRID_COLS):
                    x, y, _, _ = G.cell_rect(band, fg_idx, bg_idx, mode)
                    out = G.sample_center(arr, (x + dx, y + dy, G.CELL, G.CELL))
                    cells.append((mode, band, fg_idx, bg_idx, out))
    return cells


def sample_strip(arr: np.ndarray, dx: int, dy: int):
    cells = []
    for band in range(len(G.ALPHAS)):
        for bg_idx in range(3):
            for mode in G.MODES:
                x, y, _, _ = G.strip_rect(band, bg_idx, mode)
                out = G.sample_center(arr, (x + dx, y + dy, G.CELL, G.STRIP_H))
                cells.append((band, bg_idx, mode, out))
    return cells


def normal_expected(band: int, bg_idx: int):
    bgc = G.NORMAL_STRIP_BG[bg_idx]
    a = G.ALPHAS[band]
    return tuple(np.rint((a * np.array(G.NORMAL_STRIP_FG) + (255 - a) * np.array(bgc)) / 255).astype(int))


# ---------------------------------------------------------------------------
# analysis
# ---------------------------------------------------------------------------

def fit_mode(cells, mode: str):
    """Fit every candidate (vectorized over all cells); sorted (max, mean, label)."""
    rows = [c for c in cells if c[0] == mode]
    bg = np.array([G.BG_COLORS[b][0] for (_, _, _, b, _) in rows], float)
    fg = np.array([G.FG_COLORS[f][0] for (_, _, f, _, _) in rows], float)
    a = np.array([G.ALPHAS[b] for (_, b, _, _, _) in rows], float)
    out = np.array([o for (_, _, _, _, o) in rows], float)

    results = []
    for label, fname, sname, mname in H.all_candidates(mode):
        formula = H.CANDIDATES[mode][fname]
        if fname in H.FORCED_SPACE:
            sname = H.FORCED_SPACE[fname]
        to_lin, from_lin = H.SPACES[sname]

        def f_wrapped(b, g, _f=formula, _tl=to_lin, _fl=from_lin, _s=sname):
            if _s == "gamma":
                return _f(b, g)
            bl, gl = _tl(b, g)
            return _fl(_f(bl, gl))

        pred = H.MODELS[mname](f_wrapped, bg, fg, a)
        e = np.abs(pred - out).max(axis=1)
        results.append((float(e.max()), float(e.mean()), label))
    results.sort(key=lambda r: (r[0], r[1]))
    return results


def separability_report(cells, mode: str):
    band = len(G.ALPHAS) - 1  # alpha=100%
    rows = [c for c in cells if c[0] == mode and c[1] == band]
    out_of = {}
    for (fa, fb) in G.EQUAL_LUMA_FG_PAIRS:
        diffs = 0
        worst = 0
        for bg_idx in range(G.GRID_COLS):
            oa = next(o for (_, _, f, b, o) in rows if f == fa and b == bg_idx)
            ob = next(o for (_, _, f, b, o) in rows if f == fb and b == bg_idx)
            d = max(abs(x - y) for x, y in zip(oa, ob))
            if d > 1:
                diffs += 1
            worst = max(worst, d)
        out_of[(fa, fb)] = (diffs, worst)
    return out_of


def signature_tables(cells, mode: str):
    band = len(G.ALPHAS) - 1
    rows = {(f, b): o for (m, ba, f, b, o) in cells if m == mode and ba == band}
    lines = []
    lines.append(f"--- {mode} alpha=100%: rows=fg colors, cols=bg colors (hex r,g,b) ---")
    lines.append("fg\\bg " + " ".join(f"{lab:>6}" for _, lab in G.BG_COLORS))
    for fg_idx, (_, flab) in enumerate(G.FG_COLORS):
        cells_ = " ".join(f"{r:02x}{g:02x}{b:02x}" for bg_idx, _ in enumerate(G.BG_COLORS)
                          for r, g, b in [rows[(fg_idx, bg_idx)]])
        lines.append(f"{flab:>5} {cells_}")
    # alpha sweep for key cells
    key = [(1, 0), (4, 4), (6, 3), (15, 9), (6, 0), (12, 0)]
    lines.append("")
    lines.append(f"--- {mode} alpha sweep (0,64,128,191,255) ---")
    for (fg_idx, bg_idx) in key:
        vals = [next(o for (m, ba, f, b, o) in cells
                     if m == mode and ba == k and f == fg_idx and b == bg_idx)
                for k in range(len(G.ALPHAS))]
        fg = G.FG_COLORS[fg_idx][0]
        bg = G.BG_COLORS[bg_idx][0]
        lines.append(f"fg{fg} bg{bg}: " + " ".join(f"{o[0]:3d},{o[1]:3d},{o[2]:3d}" for o in vals))
    return "\n".join(lines)


def run_analysis(arr: np.ndarray, out_lines) -> str:
    score, dx, dy = find_offset(arr)
    total_anchors = sum(len(i) for i, _ in _anchor_sets())
    out_lines.append(f"registration: offset=({dx},{dy}) anchors {score}/{total_anchors} exact")
    if score / total_anchors < 0.7:
        out_lines.append("WARNING: registration weak - export may be misaligned/scaled")

    cells = sample_dataset(arr, dx, dy)
    strip = sample_strip(arr, dx, dy)

    # controls
    a0_bad = 0
    for (mode, band, fg_idx, bg_idx, out) in cells:
        if band != 0:
            continue
        bg = G.BG_COLORS[bg_idx][0]
        if max(abs(a - b) for a, b in zip(out, bg)) > 1:
            a0_bad += 1
    strip_bad = 0
    for (band, bg_idx, mode, out) in strip:
        e = normal_expected(band, bg_idx)
        if max(abs(a - b) for a, b in zip(out, e)) > 2:
            strip_bad += 1
    out_lines.append(f"control alpha=0 cells vs bg: {a0_bad} mismatches (0 = transparent fg "
                     f"is skipped, like PS)")
    out_lines.append(f"control normal strip: {strip_bad} mismatches (0 = straight-alpha "
                     f"compositing + no color drift)")

    verdicts = {}
    for mode in G.MODES:
        out_lines.append("")
        out_lines.append(f"===== mode: {mode} =====")
        seps = separability_report(cells, mode)
        luma_mode = all(d == 0 for (d, _) in seps.values())
        for (fa, fb), (diffs, worst) in seps.items():
            out_lines.append(f"  equal-luma pair fg{G.FG_COLORS[fa][1]} vs "
                             f"fg{G.FG_COLORS[fb][1]}: {diffs}/{len(G.BG_COLORS)} "
                             f"bgs differ (worst {worst})")
        out_lines.append(f"  separability: {'NON-SEPARABLE (luma-based)' if luma_mode else 'separable (per-channel)'}")
        ranked = fit_mode(cells, mode)
        for i, (max_e, mean_e, label) in enumerate(ranked[:5]):
            out_lines.append(f"  #{i+1} {label:<55} max={max_e:6.2f} mean={mean_e:6.3f}")
        best_max, _, best_label = ranked[0]
        if best_max <= VERDICT_TOL:
            out_lines.append(f"  VERDICT: identified -> {best_label}")
            verdicts[mode] = best_label
            second_max, _, second_label = ranked[1]
            out_lines.append(f"  (runner-up: {second_label} max={second_max:.2f})")
        else:
            out_lines.append(f"  VERDICT: NO EXACT MATCH (best max={best_max:.2f}) - "
                             f"see signature tables below")
            verdicts[mode] = None
        out_lines.append(signature_tables(cells, mode))

    return verdicts


def dump_csv(cells, path: Path) -> None:
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["mode", "alpha", "fg_idx", "fg_rgb", "bg_idx", "bg_rgb", "out_r", "out_g", "out_b"])
        for (mode, band, fg_idx, bg_idx, out) in cells:
            w.writerow([mode, G.ALPHAS[band], fg_idx,
                        "/".join(map(str, G.FG_COLORS[fg_idx][0])),
                        bg_idx, "/".join(map(str, G.BG_COLORS[bg_idx][0])),
                        *out])


# ---------------------------------------------------------------------------
# self-test: simulate a CSP that applies a known (formula, space, model) and
# check the analyzer identifies it
# ---------------------------------------------------------------------------

def simulate_export(add_truth, dodge_truth):
    """Returns a composite array as if CSP blended with the given truths.

    truth: (formula_name, space_name, model_name) evaluated via the library,
    or a callable (bg, fg, alpha) -> pred for out-of-library formulas.
    Transparent pixels are skipped (like Photoshop/CSP)."""
    arr = np.asarray(Image.open(Path(__file__).parent / "out" / "bg.png").convert("RGB"), np.uint8).copy()
    truths = {"add_glow": add_truth, "glow_dodge": dodge_truth}
    for mode in G.MODES:
        truth = truths[mode]
        for band in range(len(G.ALPHAS)):
            for fg_idx in range(G.GRID_ROWS):
                for bg_idx in range(G.GRID_COLS):
                    x, y, w, h = G.cell_rect(band, fg_idx, bg_idx, mode)
                    bg = np.array(G.BG_COLORS[bg_idx][0], float)
                    fg = np.array(G.FG_COLORS[fg_idx][0], float)
                    a = np.array([G.ALPHAS[band]], float)
                    if callable(truth):
                        pred = truth(bg, fg, a)
                    else:
                        fname, sname, mname = truth
                        pred = H.evaluate(fname, sname, mname, bg, fg, a)
                    arr[y + 8 : y + h - 8, x + 8 : x + w - 8] = np.rint(pred).astype(np.uint8)
    for band in range(len(G.ALPHAS)):
        for bg_idx in range(3):
            for mode in G.MODES:
                x, y, w, h = G.strip_rect(band, bg_idx, mode)
                e = normal_expected(band, bg_idx)
                arr[y + 4 : y + h - 4, x + 4 : x + w - 4] = e
    return arr


def run_self_test() -> int:
    import itertools

    add_truths = [
        ("add_clamp", "gamma", "blend_then_composite"),
        ("add_hdr_max", "gamma", "blend_then_composite"),
        ("add_hdr_max", "gamma", "premultiplied"),
        ("add_clamp", "srgb_lin", "blend_then_composite"),
        ("add_fgscale", "gamma", "blend_then_composite"),
        ("add_hdr_max", "srgb_lin", "premultiplied"),
    ]
    dodge_truths = [
        ("dodge", "gamma", "blend_then_composite"),
        ("dodge", "gamma", "premultiplied"),
        ("dodge", "srgb_lin", "blend_then_composite"),
        ("dodge_inv", "gamma", "blend_then_composite"),
        ("dodge_hdr", "gamma", "blend_then_composite"),
        ("dodge_lum_fg", "gamma", "blend_then_composite"),
        ("dodge_soft", "gamma", "blend_then_composite"),
        ("dodge_addmix", "gamma", "blend_then_composite"),
    ]
    failures = 0
    total = 0
    for add_t, dodge_t in itertools.product(add_truths, dodge_truths):
        arr = simulate_export(add_t, dodge_t)
        out_lines = []
        verdicts = run_analysis(arr, out_lines)
        total += 1
        # find the verdict line for each mode
        for mode, truth in [("add_glow", add_t), ("glow_dodge", dodge_t)]:
            v = verdicts[mode]
            if v is None:
                print(f"FAIL {mode}: no match for truth {truth}")
                failures += 1
                continue
            # verdict label starts with formula name
            if not v.startswith(truth[0]):
                print(f"FAIL {mode}: truth {truth} got '{v}'")
                failures += 1
                continue
            # the space must match the formula's space (forced spaces included)
            label_space = "srgb_lin" if truth[0] in H.FORCED_SPACE else truth[1]
            model = truth[2]
            if not (label_space in v and model in v):
                print(f"FAIL {mode}: truth {truth} space/model mismatch got '{v}'")
                failures += 1
        if total % 12 == 0:
            print(f"  self-test {total}/48 done, failures so far: {failures}")
    print(f"self-test: {total} combos, {failures} failures")

    # offset robustness
    arr = simulate_export(("add_hdr_max", "gamma", "blend_then_composite"),
                          ("dodge", "gamma", "blend_then_composite"))
    shifted = np.zeros_like(arr)
    shifted[5:, 3:] = arr[:-5, :-3]  # shift down-right by (5,3), dark border
    out_lines = []
    verdicts = run_analysis(shifted, out_lines)
    ok = verdicts["add_glow"] is not None and verdicts["add_glow"].startswith("add_hdr_max") \
        and verdicts["glow_dodge"] is not None and verdicts["glow_dodge"].startswith("dodge")
    print("offset test:", "PASS" if ok else "FAIL")
    failures += 0 if ok else 1

    # unknown formula -> must NOT be identified (simulated without registering)
    def add_unknown(bg, fg, a):
        f = lambda b, g: np.minimum(b + g * 1.25, 255.0)
        return H.MODELS["blend_then_composite"](f, bg, fg, a)
    arr = simulate_export(add_unknown, ("dodge", "gamma", "blend_then_composite"))
    out_lines = []
    verdicts = run_analysis(arr, out_lines)
    ok = verdicts["add_glow"] is None
    print("unknown-formula test:", "PASS" if ok else "FAIL")
    failures += 0 if ok else 1
    return 1 if failures else 0


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--export", type=str, default=None)
    ap.add_argument("--simulate", action="store_true")
    ap.add_argument("--out-report", type=str, default=None)
    args = ap.parse_args()

    if args.simulate:
        raise SystemExit(run_self_test())

    if not args.export:
        ap.error("need --export path or --simulate")

    arr = load_export(args.export)
    out_lines = []
    run_analysis(arr, out_lines)
    report = "\n".join(out_lines)
    print(report)
    if args.out_report:
        Path(args.out_report).write_text(report + "\n")
    cells = sample_dataset(arr, *find_offset(arr)[1:])
    dump_csv(cells, Path(args.export).with_suffix(".csv"))


if __name__ == "__main__":
    main()
