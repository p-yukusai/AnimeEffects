#!/usr/bin/env python3
"""Emit a BLIND mutation spec for scripts/mutation_check.sh.

The point is the blindness. Sites are found by walking the production source and
applying first-order mutation operators at every one of them; the sample is drawn with
a seeded RNG. Nothing here knows what any gate asserts, so no mutation can be chosen
because it looks likely to be caught - a mutation that SURVIVES is the useful result,
and picking the ones a test already covers would hide exactly those.

    scripts/mutation_generate.py [--sample N] [--seed S] [--files P ...] > /tmp/mut.tsv

Operators (one mutant per site, first order - never two at once):
    int-lit       an integer literal -> 0 / 1 / n+1 / n-1
    float-lit     a float literal -> 2x
    compare       < -> <= , <= -> < , > -> >= , >= -> > , == -> != , != -> ==
    logic         && -> || , || -> &&
    boolean       true -> false , false -> true
    arith         a spaced + -> - and a spaced - -> + (never through -> , += , ++)
    drop-call     a bare call statement -> ;      (statement deletion)
    drop-return   return; / return X; -> ;        (guard removal; may not compile)

Gate scope is fixed by PATH, never by the mutant: the graph feature is watched by the
five graph test binaries, the wiring by those plus the app-level probe.
"""

import argparse
import random
import re
import sys
from pathlib import Path

GRAPH_FILES = [
    "src/ctrl/GraphEditor.cpp",
    "src/ctrl/GraphEditor.h",
    "src/ctrl/graph/graph_Channel.cpp",
    "src/ctrl/graph/graph_Channel.h",
    "src/ctrl/graph/graph_Curve.cpp",
    "src/ctrl/graph/graph_Curve.h",
    "src/ctrl/graph/graph_Renderer.cpp",
    "src/ctrl/graph/graph_Renderer.h",
    "src/ctrl/graph/graph_Scaler.cpp",
    "src/ctrl/graph/graph_Scaler.h",
    "src/gui/theme/Graph.cpp",
    "src/gui/theme/Graph.h",
]
# The wiring: how the panel is toggled, fed, drawn and allowed to consume input.
WIRING_FILES = [
    "src/ctrl/Driver.cpp",
    "src/ctrl/Driver.h",
    "src/gui/MainWindow.cpp",
    "src/gui/MainDisplayWidget.cpp",
    "src/gui/tool/tool_ViewPanel.cpp",
    "src/gui/tool/tool_ViewPanel.h",
    "src/gui/ToolWidget.cpp",
]

GRAPH_TARGETS = ",".join(
    "tests/Debug/" + n
    for n in (
        "graph_curve",
        "graph_drag",
        "graph_render_pixels",
        "graph_image_step",
    )
)
WIRING_TARGETS = GRAPH_TARGETS + ",src/gui/Debug/AnimeEffects"

# Mirrors scripts/run_suites.sh: the headless gates run offscreen, graph_image_step needs
# a real GL context and runs under Xvfb with WAYLAND_DISPLAY unset and xcb pinned.
#
# `overlay_engine_primitives` is deliberately NOT in this scope, and that is a linkage
# fact rather than a preference: it links no graph-scope object at all (it pulls only
# ctrl::PainterHandle), so running it per mutant would cost an Xvfb launch and could never
# change its verdict. It stays in the suite as the gate for the paint engine the renderer
# draws through.
GRAPH_CMD = (
    "for t in graph_curve graph_drag graph_render_pixels; do "
    "QT_QPA_PLATFORM=offscreen timeout 180 build/tests/Debug/$t >/dev/null 2>&1 "
    "|| { echo FAIL $t; exit 1; }; done; "
    "env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb timeout 600 xvfb-run -a "
    "build/tests/Debug/graph_image_step >/dev/null 2>&1 || { echo FAIL graph_image_step; exit 1; }"
)
# The wiring files (MainWindow/MainDisplayWidget/ToolWidget/tool_ViewPanel and, by archive
# semantics, Driver) are compiled only into the AnimeEffects executable - no unit binary
# links them - so the app-level probe is the only gate that can see a wiring mutant.
PROBE_CMD = GRAPH_CMD + "; bash scripts/gui/probe_graph_ink.sh"

KEYWORDS = ("if", "for", "while", "switch", "return", "case", "else", "do")


def code_of(line: str) -> str:
    """The part of a line before an unquoted // comment, with string literals blanked.

    Blanking (rather than dropping) keeps every column in place, so a site's index in
    the returned text is also its index in the original line.
    """
    out = []
    i = 0
    quote = None
    while i < len(line):
        ch = line[i]
        if quote:
            if ch == "\\":
                out.extend("  ")
                i += 2
                continue
            if ch == quote:
                quote = None
            out.append(" ")
            i += 1
            continue
        if ch in "\"'":
            quote = ch
            out.append(" ")
            i += 1
            continue
        if ch == "/" and i + 1 < len(line) and line[i + 1] == "/":
            break
        out.append(ch)
        i += 1
    return "".join(out)


def sites(line: str):
    """Yield (operator, start, end, replacement) for every site on one line."""
    code = code_of(line)
    stripped = code.strip()
    if not stripped or stripped.startswith(("*", "/*", "#")):
        return

    def add(op, m, repl):
        yield (op, m.start(), m.end(), repl)

    # integer literals (not part of a float, not inside an identifier)
    for m in re.finditer(r"(?<![\w.])-?\d+(?![\w.])", code):
        text = m.group(0)
        # a lone 0 or 1 before/after a cast, or a plain index, is still worth mutating;
        # dedupe the variants that would not change anything.
        for op, repl in (
            ("int0", "0"),
            ("int1", "1"),
            ("int-plus", str(int(text) + 1)),
            ("int-minus", str(int(text) - 1)),
        ):
            if repl != text:
                yield (op, m.start(), m.end(), repl)

    # float literals -> 2x (keeps the suffix: 0.4f -> 0.8f)
    for m in re.finditer(r"(?<![\w.])(\d+\.\d*|\.\d+)([fF]?)", code):
        try:
            value = float(m.group(1))
        except ValueError:
            continue
        yield ("float-x2", m.start(), m.end(), f"{value * 2:g}{m.group(2)}")

    for op, pattern, repl in (
        ("compare-le", r"<=", "<"),
        ("compare-lt", r"(?<!<)<(?![<=])", "<="),
        ("compare-ge", r">=", ">"),
        ("compare-gt", r"(?<![>=-])>(?![=>])", ">="),
        ("compare-eq", r"==", "!="),
        ("compare-ne", r"!=", "=="),
        ("logic-and", r"&&", "||"),
        ("logic-or", r"\|\|", "&&"),
        ("bool-true", r"\btrue\b", "false"),
        ("bool-false", r"\bfalse\b", "true"),
    ):
        for m in re.finditer(pattern, code):
            yield (op, m.start(), m.end(), repl)

    # arithmetic on spaced operands only: never through ->, +=, ++, and never a literal
    # minus (the int-literal operator above already owns those).
    if not re.search(r"->|\+=|-=|\+\+|--", code):
        for m in re.finditer(r" (?<![+\-*/=<>])[+\-] ", code):
            yield ("arith", m.start() + 1, m.end() - 1, "-" if code[m.start() + 1] == "+" else "+")

    # statement deletion: a bare call, and a guard's early return
    m = re.match(r"^(\s*)([A-Za-z_][\w:>.]*\(.*\));\s*$", code)
    if m and m.group(2).split("(")[0].split("::")[-1] not in KEYWORDS:
        yield ("drop-call", 0, len(line.rstrip()), m.group(1) + ";")
    m = re.match(r"^(\s*)return( [^;]+)?;\s*$", code)
    if m:
        yield ("drop-return", 0, len(line.rstrip()), m.group(1) + ";")


# Every ERE metacharacter, for turning a LITERAL slice of source into a literal pattern.
ERE_META = set(".^$*+?()[]{}|\\")


def ere_escape(text: str) -> str:
    """Backslash every ERE metacharacter, so the text matches itself and nothing else."""
    return "".join("\\" + ch if ch in ERE_META else ch for ch in text)


def sed_expr(line_no: int, line: str, start: int, end: int, repl: str) -> str:
    """A line-addressed sed expression whose search text is UNIQUE on that line.

    A bare token is not enough: `>` appears three times in `a > b ? c : d`, and sed
    without /g rewrites the first one, so the mutant would be a different break than its
    id claims - a mutation whose identity lies is worse than no mutation. So the search
    text is a window around the site, widened until it occurs exactly once.
    """
    lo, hi = max(0, start - 10), min(len(line), end + 10)
    frm = line[lo:hi]
    while line.count(frm) > 1 and (lo > 0 or hi < len(line)):
        if lo > 0:
            lo -= 1
        if hi < len(line):
            hi += 1
        frm = line[lo:hi]
    to = frm[: start - lo] + repl + frm[end - lo :]
    # The search text is a literal slice of a source line, so EVERY ERE metacharacter in
    # it must be escaped before sed sees it. Measured the hard way: an unescaped `(` is a
    # syntax error (sed fails, the file is unchanged, and the row reads NOT-APPLIED - five
    # rows of one sample did exactly that), and an unescaped `.` or `*` silently matches a
    # DIFFERENT character, so the row mutates a site its id does not name - the one
    # failure mode that would corrupt a mutation measurement without any symptom.
    # `|` is a fine delimiter once the metacharacters are escaped, because escaping them
    # escapes it too.
    pattern = ere_escape(frm)
    replacement = to.replace("\\", "\\\\").replace("&", "\\&").replace("|", "\\|")
    return f"{line_no}s|{pattern}|{replacement}|"


def parse_sed(expr: str):
    """Split a `LINEsFROMTOT` expression into (line, from, to, raw-pattern)."""
    m = re.match(r"^(\d+)s(.)(.*)$", expr)
    if not m:
        return None
    line_no, delim, rest = int(m.group(1)), m.group(2), m.group(3)
    parts, raws, cur, raw, i = [], [], [], [], 0
    while i < len(rest):
        if rest[i] == "\\" and i + 1 < len(rest):
            cur.append(rest[i + 1])
            raw.append(rest[i])
            raw.append(rest[i + 1])
            i += 2
            continue
        if rest[i] == delim:
            parts.append("".join(cur))
            raws.append("".join(raw))
            cur, raw = [], []
            i += 1
            continue
        cur.append(rest[i])
        raw.append(rest[i])
        i += 1
    parts.append("".join(cur))
    raws.append("".join(raw))
    if len(parts) != 3 or parts[2] != "":
        return None
    return line_no, parts[0], parts[1], raws[0]


def validate(spec_path: str) -> int:
    """Read-only: prove every row can apply, before a build is spent on it."""
    lines = Path(spec_path).read_text().splitlines()
    cache, bad, rows = {}, [], 0
    for raw in lines:
        if not raw or raw.lstrip().startswith("#"):
            continue
        fields = raw.split("\t")
        if len(fields) != 5:
            bad.append((raw[:60], f"expected 5 tab-separated fields, got {len(fields)}"))
            continue
        mid, rel, expr = fields[0], fields[1], fields[2]
        rows += 1
        parsed = parse_sed(expr)
        if parsed is None:
            bad.append((mid, f"unparsable sed expression: {expr}"))
            continue
        line_no, frm, to, pattern = parsed
        if frm == to:
            bad.append((mid, "the expression would change nothing (from == to)"))
            continue
        if pattern != ere_escape(frm):
            # The point of the escaping: sed must see the search text as a LITERAL. An
            # unescaped metacharacter either makes sed fail (invalid ERE -> NOT-APPLIED) or,
            # worse, matches a different character and mutates a site the id does not name.
            bad.append((mid, f"the pattern is not a literal escape of the search text: {pattern!r}"))
            continue
        if rel not in cache:
            p = Path(rel)
            cache[rel] = p.read_text().splitlines() if p.is_file() else None
        body = cache[rel]
        if body is None:
            bad.append((mid, f"no such file: {rel}"))
            continue
        if line_no < 1 or line_no > len(body):
            bad.append((mid, f"line {line_no} is outside {rel} ({len(body)} lines)"))
            continue
        target = body[line_no - 1]
        if frm not in target:
            bad.append((mid, f"{frm!r} is not on {rel}:{line_no}"))
            continue
        if target.count(frm) > 1:
            # sed without /g stops after the first match, so this still applies - but a
            # site that is ambiguous is worth seeing, because the mutation may land on
            # the wrong one of the two.
            bad.append((mid, f"NOTE {frm!r} appears {target.count(frm)}x on {rel}:{line_no}"))
    notes = [b for b in bad if "NOTE " in b[1]]
    problems = [b for b in bad if "NOTE " not in b[1]]
    for mid, why in problems[:30]:
        print(f"INVALID  {mid}: {why}")
    for mid, why in notes[:10]:
        print(f"{why.replace('NOTE ', 'NOTE     ')}  [{mid}]")
    print(f"=== {rows} rows, {len(problems)} invalid, {len(notes)} ambiguous")
    return 1 if problems else 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--sample", type=int, default=0, help="0 = every mutant")
    ap.add_argument("--seed", type=int, default=20261012)
    ap.add_argument("--files", nargs="*", default=[], help="extra files to mutate")
    ap.add_argument("--stats", action="store_true", help="print per-file/operator counts")
    ap.add_argument("--validate", metavar="SPEC", help="read-only check of an emitted spec")
    args = ap.parse_args()

    if args.validate:
        return validate(args.validate)

    root = Path(__file__).resolve().parent.parent
    files = list(GRAPH_FILES) + list(WIRING_FILES) + list(args.files)

    mutants = []
    for rel in files:
        path = root / rel
        if not path.is_file():
            continue
        scope = "graph" if rel in GRAPH_FILES else "wiring"
        targets = GRAPH_TARGETS if scope == "graph" else WIRING_TARGETS
        cmd = GRAPH_CMD if scope == "graph" else PROBE_CMD
        for line_no, line in enumerate(path.read_text().splitlines(), 1):
            for op, start, end, repl in sites(line):
                mutants.append(
                    {
                        "id": f"{path.stem}-L{line_no}-{op}",
                        "file": rel,
                        "expr": sed_expr(line_no, line, start, end, repl),
                        "targets": targets,
                        "cmd": cmd,
                    }
                )

    if args.stats:
        per_file, per_op = {}, {}
        for m in mutants:
            per_file[m["file"]] = per_file.get(m["file"], 0) + 1
            op = m["id"].rsplit("-", 1)[-1]
            per_op[op] = per_op.get(op, 0) + 1
        for k, v in sorted(per_file.items()):
            print(f"# {v:5d}  {k}", file=sys.stderr)
        for k, v in sorted(per_op.items()):
            print(f"# {v:5d}  operator {k}", file=sys.stderr)

    if args.sample and args.sample < len(mutants):
        mutants = random.Random(args.seed).sample(mutants, args.sample)

    print("# generated by scripts/mutation_generate.py - sites found mechanically, sample "
          f"{'all' if not args.sample else args.sample} with seed {args.seed}")
    for m in mutants:
        print("\t".join([m["id"], m["file"], m["expr"], m["targets"], m["cmd"]]))
    print(f"# {len(mutants)} mutants", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
