#!/bin/bash
# Mutation testing for this repo's gates, done the honest way round: plant a break that
# was chosen WITHOUT looking at what the gate asserts, run the fixed gate scope, and see
# whether anything goes red. A mutation that survives is the measurement; a mutation
# that is caught only tells you the gate was already doing its job.
#
# WHY THIS WAY ROUND. Hand-picking "mutations the test must catch" is the same error as
# testing only what you expect to pass: it selects the sample by the outcome. The sample
# here comes from `scripts/mutation_generate.py`, which walks the production source and
# applies first-order mutation operators at every site it finds, then samples them
# blindly with a fixed seed. Nothing in the selection knows what any gate checks.
#
# USAGE
#   scripts/mutation_check.sh <spec.tsv> [id-substring]
#
# SPEC FORMAT - tab-separated, '#' at the start of a line is a comment, one mutant per
# row, five columns (the generator writes them; a hand-written row is a smell):
#
#   1 id             short name for the report
#   2 file           repo-relative path to mutate
#   3 sed expr       an extended-regex `sed -i -E` expression; MUST change the file
#   4 build targets  comma-separated ninja targets to build before running, or '-'
#   5 gate command   shell command run with `bash -c` from the repo root; NON-ZERO exit
#                    is the only thing that counts as "caught", so the command must
#                    fail when any gate in its scope fails
#
# VERDICTS
#   CAUGHT         the gate scope went red; the failing gate and its output are printed
#   SURVIVED       every gate passed: the gate scope is blind to this break - the finding
#   NOT-APPLIED    the sed expression changed nothing (a broken spec)
#   COMPILE-ONLY   the build failed: a compile error is not a mutation test
#   REVERT-FAILED  the file did not come back identical (the whole run aborts)
#
# SAFETY. Each file is copied to a temp dir and restored from that copy, verified with
# `cmp` and by re-running the gate scope, which must be green again - a mutation run that
# leaves a planted bug behind is worse than no run. Nothing is stashed or committed, so a
# dirty tree survives unchanged. A trap restores the in-flight file on interrupt.
#
# ONE WRITER AT A TIME. Mutation and build must own the tree: the harness refuses to
# start while another ninja builds or while the application is running (a mutation
# replaces the binary a concurrently launched probe would be testing), and it takes a
# lock so two copies cannot interleave.
set -u

usage() {
    sed -n '2,42p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
}

[ $# -ge 1 ] || usage
SPEC="$1"
FILTER="${2:-}"

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT" || exit 2
[ -f "$SPEC" ] || { echo "no such spec: $SPEC" >&2; exit 2; }

LOCK=/tmp/ae_mutation.lock
mkdir "$LOCK" 2>/dev/null || { echo "another mutation run holds $LOCK" >&2; exit 2; }
CURRENT_FILE=""; CURRENT_KEEP=""
cleanup() {
    if [ -n "$CURRENT_FILE" ] && [ -n "$CURRENT_KEEP" ] && [ -f "$CURRENT_KEEP" ]; then
        cp -p "$CURRENT_KEEP" "$CURRENT_FILE" && touch "$CURRENT_FILE" && echo "restored $CURRENT_FILE on exit"
    fi
    rmdir "$LOCK" 2>/dev/null
}
trap cleanup EXIT INT TERM

pgrep -x ninja >/dev/null 2>&1 && { echo "a ninja build is running: one writer at a time" >&2; exit 2; }
# `pgrep -x` on the process NAME, not `-f` on a command line: a `-f` pattern also matches
# any shell that happens to quote it (measured: the wrapper invoking this harness refused
# itself), and the app's comm is exactly "AnimeEffects".
pgrep -x AnimeEffects >/dev/null 2>&1 && { echo "the application is running: kill it first" >&2; exit 2; }

KEEP=$(mktemp -d)
caught=0; survived=0; notapplied=0; compileonly=0
survivors=""

row=0
while IFS=$'\t' read -r id file expr targets cmd; do
    case "$id" in ''|'#'*|[[:space:]]*'#'*) continue;; esac
    [ -n "$FILTER" ] && case "$id" in *"$FILTER"*) ;; *) continue;; esac
    row=$((row + 1))
    [ -f "$file" ] || { echo "SPEC ERROR: $id: no such file $file" >&2; exit 2; }

    keep="$KEEP/$row.keep"
    cp -p "$file" "$keep"
    CURRENT_FILE="$file"; CURRENT_KEEP="$keep"
    before=$(sha256sum "$file" | awk '{print $1}')

    sed -E -i "$expr" "$file"
    if [ "$before" = "$(sha256sum "$file" | awk '{print $1}')" ]; then
        cp -p "$keep" "$file"; CURRENT_FILE=""; CURRENT_KEEP=""
        printf 'NOT-APPLIED    %-30s %s\n' "$id" "$file"
        notapplied=$((notapplied + 1))
        continue
    fi

    if [ "$targets" != "-" ]; then
        if ! ninja -C build ${targets//,/ } >"$KEEP/$row.build" 2>&1; then
            cp -p "$keep" "$file"; touch "$file"; CURRENT_FILE=""; CURRENT_KEEP=""
            printf 'COMPILE-ONLY   %-30s %s (build failed)\n' "$id" "$file"
            grep -m2 -E "error:" "$KEEP/$row.build" | sed 's/^/    | /'
            compileonly=$((compileonly + 1))
            continue
        fi
    fi

    rc=0
    bash -c "$cmd" >"$KEEP/$row.out" 2>&1 || rc=$?
    if [ "$rc" -eq 0 ]; then
        printf 'SURVIVED       %-30s %s\n' "$id" "$file"
        survived=$((survived + 1))
        survivors="$survivors\n  $id  ($file: sed $expr)"
    else
        printf 'CAUGHT         %-30s %s (rc=%s)\n' "$id" "$file" "$rc"
        grep -m3 -E "^(FAIL|MISSING|===)" "$KEEP/$row.out" | sed 's/^/    | /'
        caught=$((caught + 1))
    fi

    # --- restore, and prove it -------------------------------------------------------
    cp -p "$keep" "$file"
    CURRENT_FILE=""; CURRENT_KEEP=""
    cmp -s "$keep" "$file" || { echo "REVERT-FAILED  $id: $file differs from its backup - STOPPING" >&2; exit 3; }
    # `cp -p` gives the file back its ORIGINAL mtime, which is OLDER than the objects the
    # mutation build just made from it, so ninja answers "no work to do" and the recheck
    # below would grade the MUTATED binary - measured: the first caught mutant aborted the
    # run with REVERT-FAILED for exactly this reason. Bumping the mtime makes the rebuild
    # on the next line actually happen, which is the whole point of this proof.
    touch "$file"
    if [ "$targets" != "-" ]; then
        ninja -C build ${targets//,/ } >"$KEEP/$row.rebuild" 2>&1 ||
            { echo "REVERT-FAILED  $id: the reverted tree does not build - STOPPING" >&2; exit 3; }
    fi
    rc=0
    bash -c "$cmd" >"$KEEP/$row.recheck" 2>&1 || rc=$?
    if [ "$rc" -ne 0 ]; then
        echo "REVERT-FAILED  $id: the reverted tree is not green - STOPPING" >&2
        grep -m4 -E "^FAIL" "$KEEP/$row.recheck" | sed 's/^/    | /' >&2
        exit 3
    fi
done < "$SPEC"

echo
echo "=== mutants: caught=$caught survived=$survived not-applied=$notapplied compile-only=$compileonly"
echo "per-mutant build logs, gate output and the pre-mutation copies: $KEEP"
if [ -n "$survivors" ]; then
    echo "--- survivors (breaks no gate in scope noticed):"
    printf "%b\n" "$survivors"
fi
bad=$((survived + notapplied))
[ "$bad" -eq 0 ] || exit 1
exit 0
