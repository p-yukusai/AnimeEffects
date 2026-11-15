#!/usr/bin/env python3
"""Audit a directory of PSDs for the features AnimeEffects cannot reproduce.

`notes.md` asks for a compatibility check over a corpus, with two halves: render each
PSD through AnimeEffects and diff it against the reference composite, and — the half
that comes first — know WHICH files contain something the importer degrades, so a
difference has a name instead of being a mystery. This script is that first half. It
needs only psd-tools (no display, no GPU, no build) and it reads the same layer kinds
`src/img/PSDUtil.h` classifies at import time.

The kinds are the importer's own vocabulary as of 2026-10-26:

  fill        solid / gradient / pattern fill layers - imported EMPTY, with a warning
  adjust      adjustment layers (levels, curves, hue/sat, ...) - imported EMPTY
  text        type layers - imported as a flattened raster (pixels right, not editable)
  smart       smart objects - imported as a flattened raster
  mask        a layer or group mask - PARSED AND NEVER COMPOSITED, so the pixels differ
  clip        a clipping layer - honoured (ClippingFrame), listed for completeness
  group       a group - pass-through is the PSD default; a non-pass-through group is
              composited by the importer

`verdict` is the worst thing found, because that is what decides whether the user will
see a difference: `lossy` when a layer will import empty, `approx` when pixels are
flattened or a mask is dropped, `clean` when nothing in the list applies.

Usage:
  scripts/psd/audit.py <dir-or-file>... [--csv out.csv] [--limit N]

Exit code is 0 whenever the audit RAN; it is not a verdict. Read the table.
"""
import argparse
import csv
import sys
from pathlib import Path

try:
    from psd_tools import PSDImage
    from psd_tools.constants import ColorMode
except ImportError:  # pragma: no cover - the message is the point
    sys.exit("psd-tools is required: pip install psd-tools (1.26.0 was used here)")

# Layer kinds the importer cannot draw at all.
FILL_KINDS = {"solidcolorfill", "gradientfill", "patternfill"}
# Every adjustment kind psd-tools reports. Named explicitly so an unknown kind lands in
# `other` below instead of being silently counted as fine.
ADJUST_KINDS = {
    "brightnesscontrast", "levels", "curves", "exposure", "vibrance", "huesaturation",
    "colorbalance", "blackandwhite", "photofilter", "channelmixer", "gradientmap",
    "invert", "posterize", "threshold", "selectivecolor",
}
TEXT_KINDS = {"type"}
SMART_KINDS = {"smartobject"}


def _kind(layer):
    """psd-tools' layer kind as a lower-case token, whatever spelling this version uses."""
    kind = getattr(layer, "kind", None)
    return str(kind).lower().replace(" ", "").replace("_", "") if kind is not None else ""


def audit(path):
    psd = PSDImage.open(path)
    counts = {k: 0 for k in ("fill", "adjust", "text", "smart", "mask", "clip", "group", "other")}
    layers = 0
    for layer in psd.descendants():
        layers += 1
        kind = _kind(layer)
        if layer.is_group():
            counts["group"] += 1
        elif kind in FILL_KINDS:
            counts["fill"] += 1
        elif kind in ADJUST_KINDS:
            counts["adjust"] += 1
        elif kind in TEXT_KINDS:
            counts["text"] += 1
        elif kind in SMART_KINDS:
            counts["smart"] += 1
        elif kind in ("pixel", "shape", "raster", "group"):
            pass
        else:
            counts["other"] += 1
        if getattr(layer, "mask", None) is not None:
            counts["mask"] += 1
        if bool(getattr(layer, "clipping", False)):
            counts["clip"] += 1
    if counts["fill"] or counts["adjust"]:
        verdict = "lossy"
    elif counts["mask"] or counts["text"] or counts["smart"] or counts["other"]:
        verdict = "approx"
    else:
        verdict = "clean"
    return {
        "file": Path(path).name,
        "size": f"{psd.width}x{psd.height}",
        # The number alone is unreadable: psd-tools reports the PSD colour mode as its
        # enum VALUE (3 = RGB), and RGB is the only one this importer reads at all.
        "mode": str(getattr(psd.color_mode, "name", psd.color_mode)),
        "depth": psd.depth,
        "layers": layers,
        **counts,
        "verdict": verdict,
    }


def lines_of(path, recursive=True):
    p = Path(path)
    if p.is_dir():
        return sorted(p.rglob("*.psd") if recursive else p.glob("*.psd"))
    return [p]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--csv", help="write the rows here as CSV")
    ap.add_argument("--limit", type=int, default=0, help="stop after N files")
    args = ap.parse_args()

    files = []
    for raw in args.paths:
        files.extend(lines_of(raw))
    if args.limit:
        files = files[: args.limit]
    if not files:
        sys.exit("no .psd files found")

    rows, failed = [], 0
    for path in files:
        try:
            rows.append(audit(path))
        except Exception as exc:  # a broken file must not hide the rest of the corpus
            failed += 1
            print(f"UNREADABLE {Path(path).name}: {exc}", file=sys.stderr)

    cols = ["file", "size", "mode", "depth", "layers", "fill", "adjust", "text", "smart",
            "mask", "clip", "group", "other", "verdict"]
    widths = {c: max(len(c), max((len(str(r[c])) for r in rows), default=0)) for c in cols}
    print("  ".join(c.ljust(widths[c]) for c in cols))
    for r in rows:
        print("  ".join(str(r[c]).ljust(widths[c]) for c in cols))

    tally = {v: sum(1 for r in rows if r["verdict"] == v) for v in ("clean", "approx", "lossy")}
    print()
    print(f"{len(rows)} files ({failed} unreadable)  "
          + "  ".join(f"{k}={v}" for k, v in tally.items()))
    print("totals: " + "  ".join(f"{k}={sum(r[k] for r in rows)}"
                                 for k in ("layers", "fill", "adjust", "text", "smart",
                                           "mask", "clip", "group", "other")))
    if any(r["other"] for r in rows):
        print("note: `other` counts a layer kind this table does not name - read the file "
              "before assuming it is harmless")

    if args.csv:
        with open(args.csv, "w", newline="") as fh:
            writer = csv.DictWriter(fh, fieldnames=cols)
            writer.writeheader()
            writer.writerows(rows)
        print(f"wrote {args.csv}")


if __name__ == "__main__":
    main()
