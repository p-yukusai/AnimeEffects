#!/usr/bin/env python3
"""Measure INK in a raw crop: the pixel arithmetic probe_timeline_ink.sh and
probe_graph_ink.sh rest on.

Same contract as ae_pix.py, and the same reason for existing: the caller writes a raw
file and passes its size -

    magick shot.png -crop WxH+X+Y +repage -depth 8 rgb:file

- so every coordinate here is inside that crop and nothing here knows about themes,
widgets, or the application. The two ink probes ask the same questions of different
crops ("what colour is this box", "how much of it is THIS ink", "which rows or columns
carry ink"), and the arithmetic is here once rather than twice in shell and awk.

TWO SENSES, and the mode names say which is which:
  * a TOKEN's pixels are WITHIN `tol` of a colour - a lane's tint, the accent, a hue.
    `count`, `ring`, `cols`.
  * INK pixels are FARTHER than `thr` from a colour - a label's glyphs, a tick, a key
    marker over the surface it is drawn on. `far`, `rows`, `inkcols`.
Colors are "r g b", 0..255, and every distance is the euclidean one over the three
channels (so a tolerance of 12 admits a 12-step change in one channel, or 7 in each of
two, which is what "this colour, within the antialiasing" means in practice).

  px <raw> <w> <h> <x> <y>
      The colour at one pixel: "r g b". An off-crop coordinate is an error (a probe bug),
      not a measurement of nothing.
  modal <raw> <w> <h> <x> <y> <bw> <bh>
      The most common colour in the box, and how many pixels carry it: "r g b n". The box
      is clipped to the crop; an empty box prints "none". Ties go to the largest count,
      then the smallest r, g, b, so the answer is deterministic.
  count <raw> <w> <h> <x> <y> <bw> <bh> <r> <g> <b> <tol> [<ex> <ey> <ew> <eh>]...
      How many pixels of the box are within tol of the colour: "n". A tol of 0 counts
      exact matches, which is what "this colour, and nothing like it" means for a stroke
      drawn with antialiasing OFF. Any number of exclusion RECTANGLES may follow (four
      numbers each, also in crop coordinates): a pixel inside them does not count. That is
      how a probe counts a curve while ignoring the legend swatch painted in the same
      colour elsewhere in the plot.
  inkcolmap <raw> <w> <h> <br> <bg> <bb> <thr>
      One line per column that holds any pixel farther than thr from (br,bg,bb), in x order:
      "x y0 y1 n r g b", where y0/y1 are the first and last inked row, n the number of inked
      pixels and rgb the column's most common ink colour. A panel's marks are told apart by
      how DEEP they are: a frame tick is 3..6 rows, a frame number about 13, a line spans the
      plot - and a probe that knows which is which never has to guess a row band.
  inkhues <raw> <w> <h> <x> <y> <bw> <bh> <br> <bg> <bb> <thr> <chroma> <min>
      The DISTINCT colours in the box that are farther than thr from (br,bg,bb) AND have a
      channel spread of at least `chroma`, each with its pixel count, most frequent first:
      "r g b count" per line, entries with fewer than `min` pixels dropped; "none" for a box
      with no such ink. A small saturated disk (a key marker) next to a large neutral text run
      is why this exists: the modal colour of the run that covers both is the text, and the
      marker's hue would be lost.
  centroid <raw> <w> <h> <x> <y> <bw> <bh> <r> <g> <b> <tol>
      The centre of mass of the pixels in the box that are within tol of the colour, and how
      many there are: "cx cy n"; "none 0" when the box holds none. This is how a small disk's
      real CENTRE is found - the single farthest pixel is a core pixel somewhere inside it,
      which is two or three pixels off and enough to miss a 1px ring.
  ring <raw> <w> <h> <cx> <cy> <r0> <r1> <r> <g> <b> <tol>
      How many pixels of the ANNULUS r0..r1 about (cx,cy) are within tol of the colour:
      "n". r0 is exclusive and r1 inclusive, so a 1px ring can be asked for without
      touching the marker it surrounds.
  neutral <raw> <w> <h> <x> <y> <bw> <bh> <br> <bg> <bb> <thr> <chroma> [<tol> <er> <eg> <eb>]...
      The NEUTRAL ink of a box: every pixel farther than thr from (br,bg,bb) whose own
      channels span no more than chroma, minus any excluded colour (the same
      [<tol> <er> <eg> <eb>] groups `far` takes). Prints the line `far` prints: the count,
      then the one farthest from the floor with its position, colour and distance. The
      panel's TEXT ink is what this is for: the readout and the labels are neutral, and a
      chromatic curve crossing their box is not text and must not be counted as any. A
      single channel spread is the whole test, because a key-type hue is saturated and an
      antialiased glyph pixel of a neutral ink is not.
  far <raw> <w> <h> <x> <y> <bw> <bh> <br> <bg> <bb> <thr> [<tol> <er> <eg> <eb>]...
      The INK of a box: every pixel farther than thr from (br,bg,bb) and NOT within tol of
      any excluded colour (the last four arguments, read as often as they are given).
      Prints "n x y r g b dist": the count of such pixels, then the one farthest from
      (br,bg,bb) with its position, colour and distance; "0 none 0 0 0 0" for a box with
      none. One mode rather than two because "is the ink gone" (n == 0) and "what colour
      is the ink" (the pixel) are the same scan, and two scans are two answers that can
      disagree.
  rows <raw> <w> <h> <r> <g> <b> <thr> <frac>
      The y-runs of rows at least `frac` of whose pixels are FARTHER than thr from the colour:
      one "y0 y1" line per run, ascending; "none" when no row qualifies. This is how a tinted
      lane (every row of it is off the plain surface) and a ruler's tick band (a few rows
      carrying many marks) are told from the quiet rows around them. A row of width w needs
      frac*w matching pixels.
  rowruns <raw> <w> <h> <r> <g> <b> <tol> <mincols>
      The y-runs of rows holding at least mincols pixels within tol of the colour: one
      "y0 y1 n" per run (n = the run's total such pixels), ascending; "none". The fill-sense
      mirror of `inkcols`, and the way a SOLID shape's rows are found: a row of the shape
      carries many of its columns, so a COUNT threshold separates it both from the marks
      that carry one column (the playhead line under the ruler pill) and from a glyph that
      splits a row's middle - without ever assuming a particular column is the shape for
      its whole height, which is what a single-column test does and what the digits on a
      pill make false.
  cols <raw> <w> <h> <y0> <y1> <r> <g> <b> <tol> <min>
      The x-runs of columns holding at least `min` pixels within tol of the colour anywhere
      in rows y0..y1: "x0 x1 n" per run, n the run's total over those rows; "none" when no
      column does. A vertical token (the playhead's line) is one run.
  inkcols <raw> <w> <h> <y0> <y1> <r> <g> <b> <thr> <min>
      The x-runs of columns holding at least min pixels FARTHER than thr from the colour in
      rows y0..y1, each with the run's most common ink colour: "x0 x1 n r g b". A glyph or
      a mark is a run of inked columns, and the colour is what tells a ruler number from
      the playhead's pill covering it.
"""
import sys


def load(path, w, h):
    with open(path, "rb") as fh:
        data = fh.read()
    if len(data) < w * h * 3:
        sys.exit(f"{path}: {len(data)} bytes is short of {w}x{h}x3")
    return data


def dist(a, b):
    return ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2) ** 0.5


def clip(x, y, bw, bh, w, h):
    """The box, intersected with the crop (a probe's box can hang off an edge)."""
    x0, y0 = max(0, x), max(0, y)
    x1, y1 = min(w, x + bw), min(h, y + bh)
    return x0, y0, x1, y1


def pixel(data, w, x, y):
    i = (y * w + x) * 3
    return data[i], data[i + 1], data[i + 2]


def runs_of(xs):
    """Contiguous runs of sorted ints, as (first, last) pairs."""
    out = []
    for v in xs:
        if out and v == out[-1][1] + 1:
            out[-1][1] = v
        else:
            out.append([v, v])
    return out


def main():
    a = sys.argv[1:]
    if not a:
        sys.exit(__doc__)
    mode = a[0]

    if mode == "px":
        _, raw, w, h, x, y = a[0], a[1], int(a[2]), int(a[3]), int(a[4]), int(a[5])
        if len(a) != 6:
            sys.exit(__doc__)
        data = load(raw, w, h)
        if not (0 <= x < w and 0 <= y < h):
            sys.exit(f"px: ({x},{y}) is outside {w}x{h}")
        print("%d %d %d" % pixel(data, w, x, y))
        return

    if mode == "modal":
        _, raw, w, h = a[0], a[1], int(a[2]), int(a[3])
        x, y, bw, bh = (int(v) for v in a[4:8])
        if len(a) != 8:
            sys.exit(__doc__)
        data = load(raw, w, h)
        tally = {}
        for yy in range(y, y + bh):
            for xx in range(x, x + bw):
                if 0 <= xx < w and 0 <= yy < h:
                    c = pixel(data, w, xx, yy)
                    tally[c] = tally.get(c, 0) + 1
        if not tally:
            print("none")
            return
        best = max(tally.items(), key=lambda kv: (kv[1], -kv[0][0], -kv[0][1], -kv[0][2]))
        print("%d %d %d %d" % (best[0][0], best[0][1], best[0][2], best[1]))
        return

    if mode == "count":
        _, raw, w, h = a[0], a[1], int(a[2]), int(a[3])
        x, y, bw, bh = (int(v) for v in a[4:8])
        want = (int(a[8]), int(a[9]), int(a[10]))
        tol = float(a[11])
        excl = []
        if len(a) < 12 or (len(a) - 12) % 4:
            sys.exit(__doc__)
        excl = [tuple(int(v) for v in a[12 + 4 * i:16 + 4 * i]) for i in range((len(a) - 12) // 4)]
        data = load(raw, w, h)
        n = 0
        for yy in range(y, y + bh):
            for xx in range(x, x + bw):
                if not (0 <= xx < w and 0 <= yy < h):
                    continue
                if any(ex <= xx < ex + ew and ey <= yy < ey + eh for ex, ey, ew, eh in excl):
                    continue
                if dist(pixel(data, w, xx, yy), want) <= tol:
                    n += 1
        print(n)
        return

    if mode == "inkcolmap":
        _, raw, w, h = a[0], a[1], int(a[2]), int(a[3])
        base = (int(a[4]), int(a[5]), int(a[6]))
        thr = float(a[7])
        if len(a) != 8:
            sys.exit(__doc__)
        data = load(raw, w, h)
        for xx in range(w):
            ys, tally = [], {}
            for yy in range(h):
                c = pixel(data, w, xx, yy)
                if dist(c, base) > thr:
                    ys.append(yy)
                    tally[c] = tally.get(c, 0) + 1
            if not ys:
                continue
            c = max(tally.items(), key=lambda kv: (kv[1], -kv[0][0]))[0]
            print("%d %d %d %d %d %d %d" % (xx, ys[0], ys[-1], len(ys), c[0], c[1], c[2]))
        return

    if mode == "inkhues":
        _, raw, w, h = a[0], a[1], int(a[2]), int(a[3])
        x, y, bw, bh = (int(v) for v in a[4:8])
        base = (int(a[8]), int(a[9]), int(a[10]))
        thr = float(a[11])
        chroma = int(a[12])
        mn = int(a[13])
        if len(a) != 14:
            sys.exit(__doc__)
        data = load(raw, w, h)
        tally = {}
        for yy in range(y, y + bh):
            for xx in range(x, x + bw):
                if not (0 <= xx < w and 0 <= yy < h):
                    continue
                c = pixel(data, w, xx, yy)
                if dist(c, base) <= thr:
                    continue
                if max(c) - min(c) < chroma:
                    continue
                tally[c] = tally.get(c, 0) + 1
        got = [(c, n) for c, n in tally.items() if n >= mn]
        if not got:
            print("none")
            return
        got.sort(key=lambda kv: (-kv[1], kv[0]))
        for c, n in got:
            print("%d %d %d %d" % (c[0], c[1], c[2], n))
        return

    if mode == "centroid":
        _, raw, w, h = a[0], a[1], int(a[2]), int(a[3])
        x, y, bw, bh = (int(v) for v in a[4:8])
        want = (int(a[8]), int(a[9]), int(a[10]))
        tol = float(a[11])
        if len(a) != 12:
            sys.exit(__doc__)
        data = load(raw, w, h)
        sx = sy = n = 0
        for yy in range(y, y + bh):
            for xx in range(x, x + bw):
                if 0 <= xx < w and 0 <= yy < h and dist(pixel(data, w, xx, yy), want) <= tol:
                    sx += xx; sy += yy; n += 1
        if n == 0:
            print("none 0")
        else:
            print("%d %d %d" % (round(sx / n), round(sy / n), n))
        return

    if mode == "ring":
        _, raw, w, h = a[0], a[1], int(a[2]), int(a[3])
        cx, cy, r0, r1 = (float(v) for v in a[4:8])
        want = (int(a[8]), int(a[9]), int(a[10]))
        tol = float(a[11])
        if len(a) != 12:
            sys.exit(__doc__)
        data = load(raw, w, h)
        n = 0
        for yy in range(int(cy - r1) - 1, int(cy + r1) + 2):
            for xx in range(int(cx - r1) - 1, int(cx + r1) + 2):
                if not (0 <= xx < w and 0 <= yy < h):
                    continue
                d = ((xx - cx) ** 2 + (yy - cy) ** 2) ** 0.5
                if r0 < d <= r1 and dist(pixel(data, w, xx, yy), want) <= tol:
                    n += 1
        print(n)
        return

    if mode in ("far", "neutral", "rows", "rowruns", "cols", "inkcols"):
        raw = a[1]
        w, h = int(a[2]), int(a[3])
        data = load(raw, w, h)

        if mode in ("far", "neutral"):
            x, y, bw, bh = (int(v) for v in a[4:8])
            base = (int(a[8]), int(a[9]), int(a[10]))
            thr = float(a[11])
            # `neutral` carries the channel spread bound before the exclusions, and `far`
            # carries no bound at all: one walk, two tests.
            chroma = int(a[12]) if mode == "neutral" else None
            first = 13 if mode == "neutral" else 12
            # [<tol> <er> <eg> <eb>]...: the tolerance first, then the colour it excludes
            excl = [(float(a[first + 4 * i]), (int(a[first + 1 + 4 * i]), int(a[first + 2 + 4 * i]),
                                               int(a[first + 3 + 4 * i])))
                    for i in range((len(a) - first) // 4)]
            n, best, bp = 0, -1.0, None
            for yy in range(y, y + bh):
                for xx in range(x, x + bw):
                    if not (0 <= xx < w and 0 <= yy < h):
                        continue
                    c = pixel(data, w, xx, yy)
                    d = dist(c, base)
                    if d <= thr:
                        continue
                    if chroma is not None and max(c) - min(c) > chroma:
                        continue
                    if any(dist(c, ec) <= etol for etol, ec in excl):
                        continue
                    n += 1
                    if d > best:
                        best, bp = d, (xx, yy, c)
            if bp is None:
                print("0 none 0 0 0 0")
            else:
                print("%d %d %d %d %d %d %d" % (n, bp[0], bp[1], bp[2][0], bp[2][1], bp[2][2], int(best)))
            return

        if mode == "rows":
            want = (int(a[4]), int(a[5]), int(a[6]))
            thr = float(a[7])
            frac = float(a[8])
            hits = []
            for yy in range(h):
                n = 0
                for xx in range(w):
                    if dist(pixel(data, w, xx, yy), want) > thr:
                        n += 1
                if n >= frac * w:
                    hits.append(yy)
            got = runs_of(hits)
            print("none" if not got else "\n".join("%d %d" % (r[0], r[1]) for r in got))
            return

        if mode == "rowruns":
            if len(a) != 9:
                sys.exit(__doc__)
            want = (int(a[4]), int(a[5]), int(a[6]))
            tol = float(a[7])
            mn = int(a[8])
            n_of = {}
            for yy in range(h):
                n = 0
                for xx in range(w):
                    if dist(pixel(data, w, xx, yy), want) <= tol:
                        n += 1
                if n >= mn:
                    n_of[yy] = n
            got = runs_of(sorted(n_of))
            if not got:
                print("none")
                return
            for r in got:
                print("%d %d %d" % (r[0], r[1],
                                    sum(n_of[y] for y in range(r[0], r[1] + 1))))
            return

        y0, y1 = int(a[4]), int(a[5])
        want = (int(a[6]), int(a[7]), int(a[8]))
        thr = float(a[9])
        mn = int(a[10])
        if len(a) != 11:
            sys.exit(__doc__)
        n_of, col_of = {}, {}
        for xx in range(w):
            n, tally = 0, {}
            for yy in range(max(0, y0), min(h, y1 + 1)):
                c = pixel(data, w, xx, yy)
                d = dist(c, want)
                if (d <= thr) if mode == "cols" else (d > thr):
                    n += 1
                    tally[c] = tally.get(c, 0) + 1
            if n >= mn:
                n_of[xx] = n
                col_of[xx] = max(tally.items(), key=lambda kv: (kv[1], -kv[0][0]))[0] if tally else want
        got = runs_of(sorted(n_of))
        if not got:
            print("none")
            return
        for r in got:
            total = sum(n_of[x] for x in range(r[0], r[1] + 1))
            if mode == "cols":
                print("%d %d %d" % (r[0], r[1], total))
            else:
                # the run's most common ink colour: the modal one among its columns
                tally = {}
                for x in range(r[0], r[1] + 1):
                    tally[col_of[x]] = tally.get(col_of[x], 0) + n_of[x]
                c = max(tally.items(), key=lambda kv: (kv[1], -kv[0][0]))[0]
                print("%d %d %d %d %d %d" % (r[0], r[1], total, c[0], c[1], c[2]))
        return

    sys.exit(__doc__)


main()
