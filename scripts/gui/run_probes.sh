#!/bin/bash
# Run every GUI probe and report. These are the end-to-end checks that no unit
# test covers: the export dialog's wiring, the quick-export menu, the OS file
# dialog, and whether an export actually lands inside a platform's cap.
#
# They need a display (Xvfb is created per probe), a built Debug binary, and a
# real .anie project. Pass one, or set AE_PROJECT, or let them search.
#
#   scripts/gui/run_probes.sh [project.anie]
set -u
DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJ="${1:-${AE_PROJECT:-}}"

pass=0; fail=0; stale=0; failed=""; stalled=""
# Per-probe wall budget. The blanket 900 s killed probe_capped mid-case in every
# sweep: its honest cases (72/100-frame real exports with fit bisections) measure
# 1120 s standalone on an idle machine (timed 2026-10-01, EXIT=0 green). The
# raise is PER PROBE so one probe's honest runtime cannot silently eat the sweep
# budget the others live in.
declare -A PROBE_TIMEOUT=( [probe_capped]=1800 )
for probe in "$DIR"/probe_*.sh; do
    name=$(basename "$probe")
    budget=${PROBE_TIMEOUT["${name%.sh}"]:-900}
    out=$(timeout "$budget" "$probe" "$PROJ" 2>&1)
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
