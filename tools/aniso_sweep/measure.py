#!/usr/bin/env python3
"""Measure a candidate render against a reference render.

Metrics (all computed on greyscale luma in 0..255 units):

  err_mean   mean absolute difference over the reference interior
  err_p99    99th percentile of that absolute difference
  mse        mean squared difference over the same interior
  mse_p99    99th percentile of that squared difference
  rmse       sqrt(mse); always >= err_mean for the same residuals
  sharp      mean absolute horizontal gradient of the candidate over the interior
  sharp_err  |sharp(candidate) - sharp(reference)|

The *interior* is every pixel whose full 7x7 neighbourhood (edge-clamped) in the
reference is above ``--threshold`` (default 30).  That excludes background and
fades, so the numbers describe the drawn content rather than the empty canvas.
The gradient uses forward differences, so it is evaluated on interior pixels
whose right-hand neighbour exists.

Both images are converted to a single float greyscale plane before anything
else, so 8-bit / 16-bit / float PNGs and any Pillow-readable format (PNG, JPEG,
TIFF, ...) can be mixed freely.  Alpha is ignored, exactly like Pillow's
``convert("L")``; a transparent backdrop therefore reads as black and is
dropped by the interior mask.  Images must have identical pixel dimensions --
they are never rescaled, because resampling would corrupt the metric.

Usage
-----
    python3 measure.py --ref REF.png --cand NAME=PATH [NAME=PATH ...]
                       [--threshold 30]

    python3 measure.py --ref-glob 'sweep/ss-*/frame-0.png' \
                       --cand-glob 'sweep/b*-*/frame-0.png'

``--ref`` / ``--cand`` and the glob forms may be combined; a single explicit
reference applies to every candidate.  Rows are printed one per candidate.

Filename convention for the glob forms
--------------------------------------
Labels live in the path components that the glob wildcards expand, and are read
left to right, taking the first component that looks like

    <label>          e.g.  b1
    <label>-<ratio>  e.g.  b1-4      (separator '-' or '_')
    <label>_<ratio>  e.g.  b1_4

so ``sweep/b*-*/frame-0.png`` matching ``sweep/b1-4/frame-0.png`` yields label
``b1`` and ratio ``4`` (the ``-<ratio>`` part is optional).  For a flat
directory the same token may live in the file stem:
``sweeps/b1-4.png`` -> label ``b1``, ratio ``4``.

Pairing: each candidate is measured against the reference whose ``<ratio>`` is
identical.  When only one reference matches, it is used for every candidate,
and a candidate without a ratio falls back to that same single reference.  If
several references share a ratio the pairing is ambiguous and the run aborts --
pass ``--cand NAME=PATH`` (or one reference) instead.

Example -- a supersample sweep where ``ss-<ratio>`` is the reference and
``b<N>-<ratio>`` the candidates:

    python3 measure.py \
        --ref-glob 'sweep/ss-*/frame-0.png' \
        --cand-glob 'sweep/b*-*/frame-0.png'

prints, for ratio 4,

    cand   ratio  ref                  err_mean   err_p99       mse   mse_p99      rmse     sharp  sharp_err
    b1         4  ss-4/frame-0.png        2.195    16.000    19.290   256.000     4.392    12.244      0.678
    b2         4  ss-4/frame-0.png        2.195    16.000    19.290   256.000     4.392    12.244      0.678
    ...

`mse` / `mse_p99` are the mean and 99th percentile of the *squared* residual
over the very same interior, and `rmse` is their square root, so `rmse` is
always at least `err_mean`.

Rows are ordered by ratio, then label, so output is byte-for-byte repeatable.
No network access and no repository-specific paths; fully deterministic.
"""

from __future__ import annotations

import argparse
import math
import re
import sys
from glob import glob
from pathlib import Path, PurePath

import numpy as np
from PIL import Image
from numpy.lib.stride_tricks import sliding_window_view

WINDOW = 7  # side of the interior neighbourhood, fixed by the metric definition

# <label>[-_]<ratio>, label being a plain identifier-ish token.
TOKEN_RE = re.compile(r"^(?P<label>[A-Za-z0-9][A-Za-z0-9_.+]*?)[-_](?P<ratio>\d+)$")
PLAIN_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.+]*$")
MAGIC_RE = re.compile(r"[*?\[]")


class MeasureError(Exception):
    """Bad input or unusable image pair; reported without a traceback."""


# --------------------------------------------------------------------------- #
# image loading
# --------------------------------------------------------------------------- #
def load_gray(path: str) -> np.ndarray:
    """Load any Pillow-supported image as a float64 greyscale plane in 0..255."""
    try:
        with Image.open(path) as im:
            im.load()
            mode = im.mode
            if mode.startswith("I;16") or mode == "I":
                # 16-bit integer grey; Pillow's convert("L") would clip to 255.
                img = np.asarray(im).astype(np.float64)
                if mode == "I" and img.size and float(img.max()) > 65535.0:
                    # 32-bit container, unknown scaling: normalise by its peak.
                    peak = float(img.max())
                    img = img * (255.0 / peak) if peak > 0 else img
                else:
                    img = img * (255.0 / 65535.0)
                return img
            if mode == "F":
                img = np.asarray(im).astype(np.float64)
                peak = float(img.max()) if img.size else 0.0
                if peak <= 1.0 + 1e-9:
                    img = img * 255.0
                elif peak > 255.0:
                    img = img * (255.0 / peak)
                return img
            if mode in ("L", "1"):
                return np.asarray(im.convert("L"), dtype=np.float64)
            return np.asarray(im.convert("RGB").convert("L"), dtype=np.float64)
    except OSError as exc:
        raise MeasureError(f"cannot read image '{path}': {exc}") from exc


def check_pair(ref: np.ndarray, cand: np.ndarray, ref_path: str, cand_path: str) -> None:
    if ref.shape != cand.shape:
        raise MeasureError(
            f"size mismatch: '{ref_path}' is {ref.shape[1]}x{ref.shape[0]} but "
            f"'{cand_path}' is {cand.shape[1]}x{cand.shape[0]}"
        )
    if ref.size == 0:
        raise MeasureError(f"empty image: '{ref_path}'")


# --------------------------------------------------------------------------- #
# metrics
# --------------------------------------------------------------------------- #
def interior_mask(ref: np.ndarray, threshold: float) -> np.ndarray:
    """Pixels whose full WINDOWxWINDOW neighbourhood is above `threshold`."""
    r = WINDOW // 2
    padded = np.pad(ref, r, mode="edge")
    # Separable sliding minimum: cheaper and far less memory than a 2-D window.
    lo = sliding_window_view(padded, WINDOW, axis=1).min(axis=-1)
    lo = sliding_window_view(lo, WINDOW, axis=0).min(axis=-1)
    return lo > threshold


def horizontal_gradient(img: np.ndarray) -> np.ndarray:
    if img.shape[1] < 2:
        return np.zeros((img.shape[0], 1), dtype=np.float64)
    return np.abs(np.diff(img, axis=1))


def measure(ref: np.ndarray, cand: np.ndarray, mask: np.ndarray) -> dict[str, float]:
    resid = (cand - ref)[mask]
    diff = np.abs(resid)
    sq = resid * resid
    if diff.size == 0:
        raise MeasureError(
            "reference interior is empty; lower --threshold or check the reference"
        )
    gm = mask[:, :-1] if mask.shape[1] >= 2 else np.zeros((mask.shape[0], 1), bool)
    gref = horizontal_gradient(ref)
    gcand = horizontal_gradient(cand)
    if not gm.any():
        sharp_ref = sharp_cand = math.nan
    else:
        sharp_ref = float(gref[gm].mean())
        sharp_cand = float(gcand[gm].mean())
    mse = float(sq.mean())
    return {
        "err_mean": float(diff.mean()),
        "err_p99": float(np.percentile(diff, 99.0)),
        "mse": mse,
        "mse_p99": float(np.percentile(sq, 99.0)),
        "rmse": math.sqrt(mse),
        "sharp": sharp_cand,
        "sharp_err": abs(sharp_cand - sharp_ref),
    }


# --------------------------------------------------------------------------- #
# labels and pairing
# --------------------------------------------------------------------------- #
def parse_token(text: str) -> tuple[str, int | None] | None:
    """Split 'b1-4' into ('b1', 4); 'b1' into ('b1', None); else None."""
    text = PurePath(text).stem
    m = TOKEN_RE.match(text)
    if m:
        return m.group("label"), int(m.group("ratio"))
    if PLAIN_RE.match(text):
        return text, None
    return None


def label_for(pattern: str, path: str) -> tuple[str, int | None]:
    """Derive (label, ratio) for `path` from what the wildcards of `pattern` matched."""
    pat_parts = PurePath(pattern).parts
    path_parts = PurePath(path).parts
    if len(pat_parts) == len(path_parts):
        for pat, got in zip(pat_parts, path_parts):
            if not MAGIC_RE.search(pat):
                continue
            token = parse_token(got)
            if token:
                return token
    # Fallback: the matched file's own name, e.g. a flat sweep of b1-4.png files.
    token = parse_token(PurePath(path).stem)
    if token:
        return token
    stem = PurePath(path).stem
    return stem, None


def expand_glob(pattern: str, side: str) -> list[tuple[str, int | None, str]]:
    paths = sorted(glob(pattern))
    if not paths:
        raise MeasureError(f"--{side}-glob matched nothing: {pattern}")
    out = []
    for p in paths:
        label, ratio = label_for(pattern, p)
        out.append((label, ratio, p))
    return out


# --------------------------------------------------------------------------- #
# CLI
# --------------------------------------------------------------------------- #
def parse_args(argv: list[str]) -> argparse.Namespace:
    ap = argparse.ArgumentParser(
        prog="measure.py",
        description="Compare candidate renders with a reference and report "
                    "error / sharpness metrics over the reference content.",
    )
    ap.add_argument("--ref", metavar="PATH", help="reference image")
    ap.add_argument("--ref-glob", metavar="PATTERN",
                    help="glob matching references named <label>[-<ratio>]")
    ap.add_argument("--cand", metavar="NAME=PATH", nargs="+", action="extend",
                    default=[], help="candidate as NAME=PATH")
    ap.add_argument("--cand-glob", metavar="PATTERN", action="append", default=[],
                    help="glob matching candidates named <label>[-<ratio>]")
    ap.add_argument("--threshold", type=float, default=30.0,
                    help="interior luminance threshold on the 0..255 grey scale "
                         "(default: 30)")
    return ap.parse_args(argv)


def build_jobs(args: argparse.Namespace):
    """Return rows of (label, ratio, cand_path, ref_path, ref_name)."""
    refs: list[tuple[str, int | None, str]] = []
    if args.ref:
        refs.append((Path(args.ref).stem, None, args.ref))
    if args.ref_glob:
        if args.ref:
            raise MeasureError("use either --ref or --ref-glob, not both")
        refs.extend(expand_glob(args.ref_glob, "ref"))

    cands: list[tuple[str, int | None, str]] = []
    for spec in args.cand:
        if "=" not in spec:
            raise MeasureError(f"--cand expects NAME=PATH, got '{spec}'")
        name, path = spec.split("=", 1)
        cands.append((name, None, path))
    for pattern in args.cand_glob:
        cands.extend(expand_glob(pattern, "cand"))

    if not refs:
        raise MeasureError("a reference is required: --ref or --ref-glob")
    if not cands:
        raise MeasureError("at least one candidate is required: --cand or --cand-glob")

    def row(label, ratio, path, ref_path):
        return (label, ratio, path, ref_path, ref_display(ref_path))

    if len(refs) == 1:
        return [row(label, ratio, path, refs[0][2]) for label, ratio, path in cands]

    by_ratio: dict[int, list[str]] = {}
    for _label, r, path in refs:
        if r is None:
            raise MeasureError(
                f"reference '{path}' has no <ratio>; several references need "
                "<label>-<ratio> names to pair by"
            )
        by_ratio.setdefault(r, []).append(path)

    pairs = []
    for label, ratio, path in cands:
        if ratio is None:
            raise MeasureError(
                f"candidate '{path}' has no <ratio>; cannot choose among "
                f"{len(refs)} references"
            )
        same = by_ratio.get(ratio, [])
        if len(same) != 1:
            raise MeasureError(
                f"{len(same)} references share ratio {ratio}; pairing is "
                "ambiguous -- pass --cand NAME=PATH instead"
            )
        pairs.append(row(label, ratio, path, same[0]))
    return pairs


def ref_display(path: str) -> str:
    """Short, unambiguous label for the reference column (last two path parts)."""
    parts = PurePath(path).parts
    return "/".join(parts[-2:]) if len(parts) >= 2 else (parts[-1] if parts else path)


def format_table(rows: list[tuple[str, int | None, float, float, float, float, float,
                                 float, str]]) -> str:
    wl = max([len(r[0]) for r in rows] + [len("cand")]) + 2
    wr = max([len(r[9]) for r in rows] + [len("ref")]) + 2
    lines = [
        f"{'cand':<{wl}}{'ratio':>6}  {'ref':<{wr}}"
        f"{'err_mean':>10}{'err_p99':>10}"
        f"{'mse':>10}{'mse_p99':>10}{'rmse':>10}"
        f"{'sharp':>10}{'sharp_err':>11}"
    ]
    for (label, ratio, err_mean, err_p99, mse, mse_p99, rmse, sharp, sharp_err,
         ref_name) in rows:
        ratio_txt = "-" if ratio is None else str(ratio)
        lines.append(
            f"{label:<{wl}}{ratio_txt:>6}  {ref_name:<{wr}}"
            f"{err_mean:>10.3f}{err_p99:>10.3f}"
            f"{mse:>10.3f}{mse_p99:>10.3f}{rmse:>10.3f}"
            f"{sharp:>10.3f}{sharp_err:>11.3f}"
        )
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        jobs = build_jobs(args)

        cache: dict[str, np.ndarray] = {}
        masks: dict[tuple[str, float], np.ndarray] = {}

        def gray(path: str) -> np.ndarray:
            if path not in cache:
                cache[path] = load_gray(path)
            return cache[path]

        results = []
        for label, ratio, cand_path, ref_path, ref_name in jobs:
            ref = gray(ref_path)
            cand = gray(cand_path)
            check_pair(ref, cand, ref_path, cand_path)
            key = (ref_path, args.threshold)
            if key not in masks:
                masks[key] = interior_mask(ref, args.threshold)
            m = measure(ref, cand, masks[key])
            results.append((label, ratio, m["err_mean"], m["err_p99"], m["mse"],
                            m["mse_p99"], m["rmse"], m["sharp"],
                            m["sharp_err"], ref_name))
    except MeasureError as exc:
        print(f"measure: {exc}", file=sys.stderr)
        return 2

    # Deterministic order: ratio (unknown last), then label.
    results.sort(key=lambda r: (r[1] is None, r[1] if r[1] is not None else 0, r[0]))
    print(format_table(results))
    return 0


if __name__ == "__main__":
    sys.exit(main())
