#!/bin/bash
# Run every GUI probe and report. These are the end-to-end checks that no unit
# test covers: the export dialog's wiring, the quick-export menu, the OS file
# dialog, and whether an export actually lands inside a platform's cap.
#
# They need a display (Xvfb is created per probe), a built Debug binary, and a
# real .anie project. Pass one, or set AE_PROJECT, or let them search - the search
# takes the SMALLEST project it can find, because the cost of opening one is not small
# (see _lib.sh: ~10 GB and ~80 s for the biggest here, against ~0.4 GB and seconds for a
# small one, and this sweep opens it once per probe).
#
#   scripts/gui/run_probes.sh [project.anie]
set -u
DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# The runner does NOT source _lib.sh (each probe does, which is what gives them $PROJ
# and the helpers), so the repository root it needs for the project search below has to
# be derived here too.
ROOT=$(cd "$DIR/../.." && pwd)
PROJ="${1:-${AE_PROJECT:-}}"

pass=0; fail=0; stale=0; failed=""; stalled=""
# ONE probe cannot run on a light project. probe_capped's cases assert that the NATURAL
# export lands OVER a destination's declared cap - that is its not-fitted proof, and its
# own comment says it must fail loudly rather than pass quietly when the source is too
# light to show it. Measured 2026-10-08: the projects light enough to be the default for
# the other fourteen (425 KB, 468 KB, 1.8 MB) produce 246 KB, 232 KB and 13 KB against
# WeChat sticker's 500 KB cap, so that case cannot observe anything.
#
# So it is handed a different project (one instance, still under _lib.sh's memory cap);
# override with AE_CAPPED_PROJECT. What drives the over-cap natural export is how the
# artwork reads at the DESTINATION's size (240x240 for the WeChat cases) — content, not
# metadata — so no file-size or canvas-size rule can be right on its own. The default is
# therefore a project that was MEASURED to satisfy it:
#
#   the before.anie (2145x1786, 4 MB)   natural 100-frame 240x240 GIF = 683 KB vs the
#                                       500 KB cap; whole probe 3m00s, EXIT=0
#   nene.anie (15000x15000, 310 MB)     satisfied it too, at ~10 GB decoded and 24m07s
#
# The second line is what the old rule ("the largest .anie") selected: it is a proxy for
# "detail at 240x240" that costs two orders of magnitude more than it needs to, and on a
# machine with its swap full the app died mid-export instead. The name list is tried
# first; the old search stays as the fallback so the probe still runs where none of the
# named projects exist. The probe fails loudly rather than vacuously if a source turns
# out lighter than the cap, so a wrong pick cannot pass quietly.
capped_names=("the before.anie")
capped_proj="${AE_CAPPED_PROJECT:-}"
if [ -z "$capped_proj" ]; then
    for capped_name in "${capped_names[@]}"; do
        capped_proj=$(find "$HOME/Downloads" "$ROOT" -maxdepth 4 -name "$capped_name" \
            -printf '%s\t%p\n' 2>/dev/null | sort -n | head -1 | cut -f2-)
        [ -n "$capped_proj" ] && break
    done
fi
if [ -z "$capped_proj" ]; then
    capped_proj=$(find "$HOME/Downloads" "$ROOT" -maxdepth 3 -name '*.anie' \
        -printf '%s\t%p\n' 2>/dev/null | sort -rn | head -1 | cut -f2-)
fi
echo "projects: default=${PROJ:-<search>}  probe_capped=${capped_proj:-<same as default>}"
# Per-probe wall budget. The blanket 900 s killed probe_capped mid-case in every
# sweep: its honest cases (72/100-frame real exports with fit bisections) measured
# 1120 s standalone on an idle machine (timed 2026-10-01, EXIT=0 green) and 24m07s
# with the largest .anie. On the measured light default above the whole probe is
# 3m00s (2026-10-11); the budget stays generous because the fallback project can
# still be the large one. The
# raise is PER PROBE so one probe's honest runtime cannot silently eat the sweep
# budget the others live in.
declare -A PROBE_TIMEOUT=( [probe_capped]=1800 [probe_weights_bake_paint]=1200 )
for probe in "$DIR"/probe_*.sh; do
    name=$(basename "$probe")
    budget=${PROBE_TIMEOUT["${name%.sh}"]:-900}
    proj_for="$PROJ"
    [ "$name" = "probe_capped.sh" ] && proj_for="${capped_proj:-$PROJ}"
    out=$(timeout "$budget" "$probe" "$proj_for" 2>&1)
    rc=$?
    # 3 = the probe's navigation no longer matches the application (it exits with
    # its own message). Neither a pass nor a failure of the application: a probe
    # that cannot reach the screen it is meant to assert on tests nothing, and
    # calling that a pass is how ten of these went green after the export form was
    # restructured. Reported as its own state so the debt stays visible.
    if [ $rc -eq 3 ]; then
        stale=$((stale+1)); stalled="$stalled $name"; echo "STALE $name"; continue
    fi
    if [ $rc -eq 0 ]; then
        pass=$((pass+1)); echo "PASS  $name"
    else
        fail=$((fail+1)); failed="$failed $name"
        echo "FAIL  $name"
        echo "$out" | grep -E "^(PASS|FAIL)" | sed 's/^/      /'
    fi
done
echo "=== probes: pass=$pass fail=$fail stale=$stale${failed:+ failed:$failed}${stalled:+ stale:$stalled} ==="
# STALE IS NOT A PASS. A probe that cannot reach the screen it asserts on tests
# nothing, so a runner that exits 0 while nine of twelve are stale reports a green
# suite that verified nothing - which is exactly what happened while every probe
# carried an always-set stale flag (see _lib.sh). A caller that is deliberately
# mid-re-record sets AE_ALLOW_STALE=1 and gets the old behaviour.
if [ "${AE_ALLOW_STALE:-0}" = "1" ]; then
    exit $((fail > 0 ? 1 : 0))
fi
exit $(((fail > 0 || stale > 0) ? 1 : 0))
