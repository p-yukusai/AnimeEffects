#!/usr/bin/env python3
"""Find the graph panel's playhead in a raw screenshot crop, and follow it.

Why the playhead: it is the panel's only feature whose frame is known - the current frame - so
"the camera held the point under the cursor" and "a pan moved the content by the drag" are both
readable from its column. It is found by its COLOUR rather than looked for by shape: the column
with the most pixels of one colour, over the plot's rows. The colour is learned at runtime
(probe_graph_anchor.sh's pan pair) so no theme constant is baked in, and it is compared exactly:
the playhead is drawn un-antialiased in theme::Graph::selected(), which is what makes an exact
match possible at all.

Raw crops: `magick shot.png -crop WxH+X+Y +repage -depth 8 rgb:file` (see probe_graph_anchor.sh).

  ae_pix.py moved <rawA> <rawB> <w> <h> <min-count>
      The colour that tops a column in BOTH shots at a DIFFERENT column - the playhead, which
      moved - printed as "xA xB r g b". Static artwork columns stay at their own x and are
      excluded; "none" means nothing moved, or the feature is out of view.

  ae_pix.py line <raw> <w> <h> <r> <g> <b> <min-count>
      The column holding the most pixels of that colour, printed as "x count"; "none" when the
      colour tops no column.
"""
import sys


def load(path, w, h):
    with open(path, "rb") as fh:
        data = fh.read()
    if len(data) < w * h * 3:
        sys.exit(f"{path}: {len(data)} bytes is short of {w}x{h}x3")
    return data


def modal_columns(data, w, h, min_count):
    """Per column: its most common colour and that colour's count, when the count is enough."""
    out = []
    for x in range(w):
        tally = {}
        for y in range(h):
            i = (y * w + x) * 3
            c = (data[i], data[i + 1], data[i + 2])
            tally[c] = tally.get(c, 0) + 1
        c, n = max(tally.items(), key=lambda kv: kv[1])
        if n >= min_count:
            out.append((n, c, x))
    return out


def main():
    argv = sys.argv[1:]
    if not argv:
        sys.exit(__doc__)
    mode = argv[0]

    if mode == "line":
        # line <raw> <w> <h> <r> <g> <b> <min-count>
        if len(argv) != 8:
            sys.exit(__doc__)
        data = load(argv[1], int(argv[2]), int(argv[3]))
        w, h = int(argv[2]), int(argv[3])
        want = (int(argv[4]), int(argv[5]), int(argv[6]))
        cols = [(n, x) for n, c, x in modal_columns(data, w, h, int(argv[7])) if c == want]
        print(f"{max(cols)[1]} {max(cols)[0]}" if cols else "none")
        return

    if mode == "moved":
        # moved <rawA> <rawB> <w> <h> <min-count>
        if len(argv) != 6:
            sys.exit(__doc__)
        w, h, need = int(argv[3]), int(argv[4]), int(argv[5])
        a = modal_columns(load(argv[1], w, h), w, h, need)
        b = modal_columns(load(argv[2], w, h), w, h, need)

        # A candidate colour has to top EXACTLY ONE column in each shot: the playhead is one
        # line of its own colour, while the artwork and the docks are flat - their colours top
        # many columns at once, in both shots. Saturation excludes the grey chrome and background
        # even when they happen to be single columns.
        def unique(cols):
            counts = {}
            best = {}
            for n, c, x in cols:
                counts[c] = counts.get(c, 0) + 1
                if c not in best or n > best[c][0]:
                    best[c] = (n, x)
            return {c: v for c, v in best.items() if counts[c] == 1}

        ua = unique(a)
        ub = unique(b)
        pairs = []
        for c in set(ua) & set(ub):
            if c[0] == c[1] == c[2] or max(c) - min(c) < 30:
                continue  # grey: chrome, background, text
            na, xa = ua[c]
            nb, xb = ub[c]
            if xa != xb:
                pairs.append((min(na, nb), c, xa, xb))
        if not pairs:
            print("none")
            return
        _n, c, xa, xb = max(pairs)
        print(f"{xa} {xb} {c[0]} {c[1]} {c[2]}")
        return

    sys.exit(__doc__)


main()
