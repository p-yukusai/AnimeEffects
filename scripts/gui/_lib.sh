#!/bin/bash
# Shared setup for the GUI probes.
#
# These probes drive the real application under Xvfb with xdotool. They exist
# because the parts that keep breaking are the parts no unit test can reach: the
# export dialog's wiring, the quick-export menu, the file dialog, and whether a
# long note is actually visible. Every bug they found was invisible to the suite:
# a remembered palette blocking every export, a Quality control wired to nothing,
# size caps stated but never applied.
#
# Sourced, not executed. Callers get:
#   $ROOT   repository root (derived, so this works from any checkout)
#   $APP    the Debug application binary
#   $SHOTS  where screenshots are written (gitignored; override with AE_SHOTS)
#   $PROJ   a project to open (arg 1, or the first .anie found, or AE_PROJECT)
# and the helpers below, each of which FAILS LOUDLY rather than returning quietly.

set -u

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
APP="$ROOT/build/src/gui/Debug/AnimeEffects"
SHOTS="${AE_SHOTS:-$ROOT/build/shots}"
FAILURES=0

mkdir -p "$SHOTS"

# Exported because the probes interpolate these INSIDE the single-quoted
# `bash -c` string that xvfb-run executes: without this the inner shell sees an
# empty $SHOTS and writes screenshots to "/" instead.
export ROOT APP SHOTS PROJ

if [ ! -x "$APP" ]; then
    echo "FAIL  no application binary at $APP (build the Debug target first)"
    exit 2
fi

PROJ="${1:-${AE_PROJECT:-}}"
if [ -z "$PROJ" ]; then
    PROJ=$(find "$HOME/Downloads" "$ROOT" -maxdepth 3 -name '*.anie' 2>/dev/null | head -1)
fi
if [ -z "$PROJ" ] || [ ! -f "$PROJ" ]; then
    echo "FAIL  no project file: pass one as arg 1, or set AE_PROJECT"
    exit 2
fi

# Xvfb + the xcb platform: the GL app cannot run offscreen, and Qt6 prefers
# Wayland when WAYLAND_DISPLAY is set, which yields black captures.
unset WAYLAND_DISPLAY
export QT_QPA_PLATFORM=xcb

# SANDBOX THE SETTINGS. A probe drives the real application, and the application
# writes QSettings on every export: the remembered destination, the last output
# directory and name, the per-destination folders, the remembered frame range and
# size. Without this the probes edited the DEVELOPER'S OWN settings -- they set
# export_destination to whatever the probe was testing and pointed the next real
# export at /tmp -- which is both a rude thing to do and a way to make a passing
# probe depend on state a previous probe left behind. One sandbox here covers every
# probe, because they all source this file.
if [ -z "${AE_KEEP_CONFIG:-}" ]; then
    export XDG_CONFIG_HOME="$(mktemp -d)"
fi
# Probes that export also need a directory that is NOT the repository: the OS file
# dialogs default to the process working directory, and a probe that forgets to
# change it writes its test output into the checkout.
export AE_PROBE_DIR="$(mktemp -d)"

# ---- driving the application by NAME -------------------------------------
#
# The probes used to click hardcoded pixels. A hardcoded pixel misses SILENTLY
# after a layout change: the click lands on a neighbour, the probe's screenshot is
# still not black, and it passes while testing the wrong widget - which is how ten
# probes kept "passing" after the export form was restructured (the Format row
# moved 70px and every one of them went on clicking the old spot). Everything below
# asks the widget where it is, or that it is there at all, so a layout change makes
# the probe fail loudly instead of quietly.
#
# The application must expose its accessibility tree, and the probe must say WHICH
# instance is its own: the developer's running app is on the same bus, and matching
# by name alone could drive that window instead. AE_UI_PID is set by the probe from
# the pid it launched - each probe does this on the line after its launch, because
# the contract used to be documented here and implemented nowhere.
AE_UI="$ROOT/scripts/gui/ae_ui.py"
# Exported for the same reason ROOT/APP/SHOTS are: the probes interpolate it inside
# the single-quoted `bash -c` string xvfb-run executes, and without the export the
# inner shell sees an empty path and every ae_* helper fails at once (37 assertions
# failing loudly is the good outcome; silently is what the old pixel clicks did).
export AE_UI
# The PATH of a flag file that does not exist yet. require_export_flow creates it when
# a probe cannot reach the screen it asserts on, check() goes quiet while it is there,
# and summary() reports STALE.
#
# It used to be `$(mktemp)`, which CREATES the file. The flag was therefore set from
# the moment this file was sourced: every check() returned without asserting anything,
# every probe printed STALE, and the marker that exists to make a dead probe visible
# was the only thing hiding the fact that ten of them had stopped testing. Measured,
# not guessed: the trace of a probe run shows check() taking the early return on the
# first assertion and summary() reporting STALE with no FAIL line anywhere.
export AE_STALE_FLAG="$(mktemp -u)"
export QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1

# THE REVISION OF THE EXPORT FLOW THE PROBES ARE RECORDED AGAINST.
#
# Bump this whenever the flow changes SHAPE. A probe declares the revision it was
# recorded against with RECORDED_AGAINST (exported, because its body runs in a child
# shell), and one that has not been re-recorded reports STALE naming both numbers.
#
# This exists because the old stale marker could not tell the two cases apart. It
# waited for the destination picker, and it was blind for reasons that had nothing to
# do with navigation: showing() walked the ancestor chain past the application to the
# DESKTOP frame, which reports showing=False under Xvfb (no window manager), so every
# widget in the process read as hidden; and no probe set AE_UI_PID, so with a second
# AnimeEffects on the bus the tooling addressed that instance instead, where the menu
# is disabled for want of a project. Ten probes declared stale for two tooling faults
# while the question "is this probe's navigation out of date" went unanswered.
#
# 1: the parameter sheet opened first, and the picker was behind a button on it.
# 2: the destination picker opens first and decides the form that follows.
AE_FLOW_REVISION=2
export AE_FLOW_REVISION
# What a new probe is recorded against. An OLD probe sets nothing, which is 0, and
# that is what makes it report itself stale instead of clicking pixels that now mean
# something else.
export RECORDED_AGAINST="${RECORDED_AGAINST:-0}"

ae() { python3 "$AE_UI" "$@" 2>/dev/null; }

# The centre of a named widget, as "x y". Empty when the widget is not there.
ae_center() {
    local box
    box=$(ae extents "$1") || return 1
    [ -n "$box" ] || return 1
    echo $box | awk '{printf "%d %d", $1 + $3 / 2, $2 + $4 / 2}'
}

# Click a named widget, by asking it where it is.
ae_press() { # ae_press <name> <what-the-caller-expects-to-happen>
    local at
    at=$(ae_center "$1")
    if [ -z "$at" ]; then
        check 1 "$2 (no visible widget named '$1')"
        return 1
    fi
    xdotool mousemove $at click 1
    check 0 "$2"
}

# A probe must be re-recorded when the export flow changes SHAPE, and it must SAY SO
# rather than click pixels that now belong to another widget: Export opens the
# destination picker FIRST, so a probe that expects the parameter sheet at that point
# is driving something else, and its assertions (mostly "the screenshot is not black")
# pass anyway. Called right after the navigation prefix; exit 3 is STALE, which
# run_probes.sh reports as neither a pass nor a failure of the application.
#
# The revision check comes FIRST and needs no application: it is a property of the
# probe, so it is decided before anything is clicked. What follows it is a genuine
# assertion about the application (the flow opens the picker), and it is a FAILURE
# rather than a stale marker, because a probe recorded against the current revision
# that cannot reach the picker is reporting on the application.
# Exported as a FUNCTION, not just defined: the probes call it inside the
# single-quoted `bash -c` body xvfb-run runs, which is a new shell.
require_export_flow() {
    if [ "${RECORDED_AGAINST:-0}" != "$AE_FLOW_REVISION" ]; then
        echo "STALE  this probe is recorded against flow revision ${RECORDED_AGAINST:-0}; the current revision is $AE_FLOW_REVISION." >&2
        echo "STALE  Its navigation and its clicks belong to the previous flow, so what it" >&2
        echo "STALE  asserts on now is not what it was written for. Re-record it against" >&2
        echo "STALE  scripts/gui/ae_ui.py, which addresses widgets by name, not by pixel," >&2
        echo "STALE  then set RECORDED_AGAINST=$AE_FLOW_REVISION at the top of the probe." >&2
        : > "$AE_STALE_FLAG"
        exit 3
    fi
    if ae wait "Use this destination" >/dev/null 2>&1; then
        return 0
    fi
    echo "FAIL  the export flow did not open the destination picker: no widget named" >&2
    echo "FAIL  'Use this destination' appeared within the wait. This probe is recorded" >&2
    echo "FAIL  against the current flow revision, so this is the application." >&2
    check 1 "the export click opens the destination picker"
    return 1
}



# One line per name: "showing", "hidden" or "absent". The shell asserts on it.
#
# The names asked about are recorded beside the answers, and the assert helpers below
# check them, because a report and the assertions that read it are two lists that have
# to agree and nothing made them. A probe that asserted on "Advanced" while its report
# never asked about it failed forever with the same message as a control that was
# genuinely hidden (measured; the fix looked like an application bug both times). A
# name that was never asked about is now its own loud, distinct failure.
ae_report() { # ae_report <file> <name>...
    local file=$1; shift
    ae report "$@" > "$file"
    : > "$file.names"
    for n in "$@"; do printf '%s\n' "$n" >> "$file.names"; done
}

ae_report_asked() { # ae_report_asked <file> <name> <what>
    if [ -f "$1.names" ] && grep -qx -- "$2" "$1.names"; then
        return 0
    fi
    echo "FAIL  $3 (this report never asked about '$2')"
    FAILURES=$((FAILURES + 1))
    return 1
}

# The three states are three different assertions, and they mean different things:
# "showing" is what the user can act on, "hidden" is a control the artifact cannot
# use (the export form hides those rather than greying them out), and "absent" means
# the widget is not in this dialog at all.
assert_showing() { ae_report_asked "$1" "$2" "$3" || return 1; grep -qx "showing $2" "$1"; check $? "$3"; }
assert_hidden()  { ae_report_asked "$1" "$2" "$3" || return 1; grep -qx "hidden  $2" "$1"; check $? "$3"; }
assert_absent()  { ae_report_asked "$1" "$2" "$3" || return 1; grep -qx "absent  $2" "$1"; check $? "$3"; }

check() { # check <ok:0|1> <message>
    # A probe that never reached its screen asserts nothing: see require_export_flow.
    [ -n "${AE_STALE_FLAG:-}" ] && [ -f "$AE_STALE_FLAG" ] && return 0
    if [ "$1" -eq 0 ]; then
        echo "PASS  $2"
        return 0
    fi
    echo "FAIL  $2"
    FAILURES=$((FAILURES + 1))
    # In the probe BODY (a child shell) the count above belongs to that shell and
    # dies with it, so the failure is also written where the outer summary can see
    # it. AE_OUTER_PID is the shell that sourced this file.
    if [ -n "${AE_OUTER_PID:-}" ] && [ "$$" != "$AE_OUTER_PID" ] && [ -n "${AE_FAIL_FLAG:-}" ]; then
        echo "$2" >> "$AE_FAIL_FLAG"
    fi
    return 1
}

# A capture that is entirely black means the app never rendered (crashed, or the
# window was never mapped). This is the cheapest possible smoke assertion and it
# caught a genuine crash during this work.
assert_rendered() { # assert_rendered <png> <what>
    local mean
    mean=$(magick "$1" -format "%[fx:mean]" info: 2>/dev/null || echo "0")
    local ok
    ok=$(awk -v m="$mean" 'BEGIN { print (m > 0.001) ? 0 : 1 }')
    check "$ok" "$2 rendered (mean brightness $mean, 0 would be a black frame)"
}

assert_exists() { # assert_exists <path> <what>
    [ -s "$1" ]; check $? "$2 exists and is non-empty ($(stat -c %s "$1" 2>/dev/null || echo 0) bytes)"
}

assert_under() { # assert_under <path> <max bytes> <what>
    local size
    size=$(stat -c %s "$1" 2>/dev/null || echo -1)
    [ "$size" -gt 0 ] && [ "$size" -le "$2" ]
    check $? "$3 ($size <= $2 bytes)"
}

summary() { # summary <probe name>
    if [ -n "${AE_STALE_FLAG:-}" ] && [ -f "$AE_STALE_FLAG" ]; then
        rm -f "$AE_STALE_FLAG"
        echo "=== $1: STALE (recorded against an older revision of the export flow) ==="
        exit 3
    fi
    # Failures from the probe BODY, which ran in a child shell and could not count
    # them here. Printed, not just counted: a number that disagrees with the lines
    # above it is how a failure goes unnoticed.
    if [ -n "${AE_FAIL_FLAG:-}" ] && [ -s "$AE_FAIL_FLAG" ]; then
        while IFS= read -r line; do
            echo "FAIL  $line (from the probe body)"
            FAILURES=$((FAILURES + 1))
        done < "$AE_FAIL_FLAG"
    fi
    rm -f "$AE_FAIL_FLAG" "${AE_STALE_FLAG:-/dev/null}" 2>/dev/null
    if [ "$FAILURES" -eq 0 ]; then
        echo "=== $1: OK ==="
    else
        echo "=== $1: $FAILURES FAILED ==="
    fi
    exit $((FAILURES > 0 ? 1 : 0))
}

# ---- the two shells a probe runs in --------------------------------------
#
# A probe's BODY runs in a child shell (`bash -c` under xvfb-run) and its ASSERTIONS
# run in the outer script, because that is where the helpers and the failure count
# live. Two things follow, and both used to be got wrong:
#
#   * the body needs the by-name helpers, so they are exported as FUNCTIONS (export
#     -f, after their definitions - an export before the `def` exports nothing and
#     says so out loud: "export: ae_report: not a function");
#   * a FAILURE inside the body has to reach the outer verdict. It used to be
#     printed by the child and counted by nobody, which is an invisible failure.
#     check() therefore appends it to AE_FAIL_FLAG when it is running in the child,
#     and summary() counts and prints those lines as its own.
export AE_OUTER_PID=$$
export AE_FAIL_FLAG="$(mktemp -u)"
export -f ae ae_center ae_press ae_report ae_report_asked check require_export_flow
