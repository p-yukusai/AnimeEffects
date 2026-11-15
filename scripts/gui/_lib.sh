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
#   $PROJ   a project to open (arg 1, AE_PROJECT, or the smallest .anie found)
# and the helpers below, each of which FAILS LOUDLY rather than returning quietly.

set -u

# THE OUTER SHELL HAS NO DISPLAY, ON PURPOSE. Every probe runs its application
# inside `xvfb-run`, which sets DISPLAY (and an XAUTHORITY cookie) for that child
# only. This script keeps running in the caller's shell, which on a normal desktop
# HAS a DISPLAY - so any line that escapes the `bash -c` body (the apostrophe trap
# documented below) would reach the person's real session. Measured 2026-10-25: a
# leaked `import -window root` under a Wayland session raised a screen-capture
# permission prompt on the desktop while the probe was supposed to be offscreen.
# With the variables unset here, a leaked command has no display to reach and
# fails instead of touching anything.
unset DISPLAY
unset WAYLAND_DISPLAY

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
    # SMALLEST, not first. The probes need a project with layers and a frame range,
    # nothing more, but opening one is not free: measured 2026-10-08 the largest
    # project on this machine (`nene.anie`, 325 MB, a 15000x15000 canvas) costs ~80 s
    # of load and ~10 GB resident per instance, against well under a second and a few
    # hundred MB for one under a megabyte - and a sweep opens the project once per
    # probe. `head -1` took whichever file the filesystem happened to
    # return first, which is how the heaviest project here became the default for
    # every probe. Pass a path as arg 1 (or AE_PROJECT) to test against a specific one.
    PROJ=$(find "$HOME/Downloads" "$ROOT" -maxdepth 3 -name '*.anie' -printf '%s\t%p\n' \
               2>/dev/null | sort -n | head -1 | cut -f2-)
fi
if [ -z "$PROJ" ] || [ ! -f "$PROJ" ]; then
    echo "FAIL  no project file: pass one as arg 1, or set AE_PROJECT"
    exit 2
fi

# Xvfb + the xcb platform: the GL app cannot run offscreen, and Qt6 prefers
# Wayland when WAYLAND_DISPLAY is set, which yields black captures.
unset WAYLAND_DISPLAY
export QT_QPA_PLATFORM=xcb

# AND PIN THE PLATFORM THEME, OR THE FILE DIALOG APPEARS ON THE USER'S DESKTOP.
# A session that sets QT_QPA_PLATFORMTHEME (COSMIC sets "cosmic") makes Qt load that
# theme's plugin, and `libcutecosmictheme.so` opens org.freedesktop.portal.FileChooser:
# the PORTAL draws that window in the live session, while the probe types into Xvfb and
# waits for a dialog that is not there. Measured 2026-09-29: a sweep prompted for a
# filename over and over on the desktop and the journal logged
#   xdg-desktop-portal-cosmic ... file_chooser: no file chooser dialog with ID Id(642)
# Three things are worth knowing before "fixing" it differently:
#   * QT_NO_XDG_DESKTOP_PORTAL does NOT stop this - cutecosmic never reads that
#     variable (measured: zero occurrences of it in the plugin).
#   * Leaving the variable unset is not enough either: Qt's candidate list came out as
#     ("cosmic", "generic"), so the portal theme is offered even when we ask for
#     nothing.
#   * Of the four platform themes installed, only libcutecosmictheme.so (1) and
#     libqxdgdesktopportal.so (2) contain the FileChooser bus name; libqgtk3.so and
#     libqt6ct.so contain none.
# "generic" is Qt's own theme: it has no plugin file and no portal strings, so the
# dialog is an ordinary widget dialog rendered inside this Xvfb, where xdotool can
# reach it. This pins the HARNESS only - the shipped application still follows the
# user's session.
export QT_QPA_PLATFORMTHEME=generic

# AND MAKE THE APPLICATION'S OWN WORDS REACH THE LOG. Qt's default message handler
# writes to the systemd JOURNAL when stderr is not a console, and a probe redirects
# stderr to /tmp/probe_app.log -- so every Qt warning and every QML error has been
# going somewhere no probe log shows. Measured: with the output redirected, QML
# console.info produced 0 lines in the file without this and 1 line with it, and
# journalctl held the ones that went missing. Set it so a failing probe's log is
# the whole story rather than the part of it that happened to be a printf.
export QT_FORCE_STDERR_LOGGING=1
# The QML console.log category (qCDebug, "qml") is filtered by the default rules;
# `QT_LOGGING_RULES="qml.debug=true"` turns it on when a scene needs to explain
# itself. Deliberately NOT set here: *.debug=true drowns a probe log in Qt's own
# font-database and pointer-trace chatter.

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

# ---- refuse to drive a REAL display ----------------------------------------
#
# xdotool talks to $DISPLAY, and NOTHING in this file sets it. Every probe is
# wrapped in `xvfb-run`, which does; a command run OUTSIDE that wrapper inherits the
# developer's own DISPLAY and drives the real desktop - the clicks, keystrokes and
# typed text land wherever the cursor happens to be. It happened: the journal of
# Sep 28 11:23:27 records an unwrapped
#     xdotool mousemove 100 40 click 1; xdotool key Down; xdotool type "ab"
# reaching the live session, and it was followed (11:25:02) by the corrected version
# wrapped in `xvfb-run -n 55` - the defect found by accident, not by a check.
#
# Note what is NOT the fix: `unset WAYLAND_DISPLAY` (above) does not touch DISPLAY.
# Qt's platform choice and xdotool's target are two different variables, and only
# the first was guarded. `QT_QPA_PLATFORM=xcb` changes which server Qt connects to;
# it changes nothing about where xdotool sends input.
#
# The test is therefore "is an Xvfb SERVING this display", never "is DISPLAY set":
# a real session has Xorg or Xwayland on :0, and that is precisely what must not be
# driven. Exits 97 rather than returning, because a probe that silently skips its
# clicks passes while testing nothing.
ae_guard_display() {
    local d=${DISPLAY:-} n pid
    n=${d#*:}; n=${n%%.*}
    if [ -z "$d" ] || [ -z "$n" ]; then
        echo "FAIL  ae: DISPLAY is unset, so there is nothing to drive." >&2
        echo "FAIL  ae: run this under xvfb-run, as the probes do." >&2
        exit 97
    fi
    for pid in $(pgrep -x Xvfb 2>/dev/null); do
        if tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null | grep -qE "(^| ):${n}( |$)"; then
            return 0
        fi
    done
    echo "FAIL  ae: DISPLAY=$d is not an Xvfb display. Synthetic input sent here would" >&2
    echo "FAIL  reach the REAL desktop, not the test window. Wrap this command in:" >&2
    echo "FAIL    xvfb-run -a --server-args=\"-screen 0 1440x900x24\" <command>" >&2
    exit 97
}

# The centre of a named widget, as "x y". Empty when the widget is not there.
ae_center() {
    local box
    box=$(ae extents "$1") || return 1
    [ -n "$box" ] || return 1
    echo $box | awk '{printf "%d %d", $1 + $3 / 2, $2 + $4 / 2}'
}

# Click a named widget, by asking it where it is.
ae_press() { # ae_press <name> <what-the-caller-expects-to-happen>
    ae_guard_display
    local at
    at=$(ae_center "$1")
    if [ -z "$at" ]; then
        check 1 "$2 (no visible widget named '$1')"
        return 1
    fi
    xdotool mousemove $at click 1
    check 0 "$2"
}

# The screen y of a named widget, from its AT-SPI rectangle. A LIST ROW's rect is
# usable (measured: the picker's rows report x=14 w=520, which is the list), unlike a
# QML control's rect inside a dialog that is not at the screen origin - see the note
# in ae_activate. Empty when the widget is not on screen, which is also how "this row
# scrolled out of the viewport" reads.
ae_y() { ae extents "$1" 2>/dev/null | awk 'NR==1 {print $2}'; }

# A real pointer drag: move, press, move in STEPS, release. The steps are the point -
# one mousemove followed by a mouseup is a click, which is why a drag with no motion
# in it cannot tell a working scroll bar from a decorative one.
ae_drag() { # ae_drag <x> <y> <dx> <dy> [steps]
    ae_guard_display
    local x=$1 y=$2 dx=$3 dy=$4 steps=${5:-8} i
    xdotool mousemove --sync "$x" "$y"; sleep 0.2
    xdotool mousedown 1; sleep 0.2
    for i in $(seq 1 "$steps"); do
        xdotool mousemove --sync $((x + dx * i / steps)) $((y + dy * i / steps))
        sleep 0.05
    done
    sleep 0.2
    xdotool mouseup 1; sleep 0.4
}

# A hard ceiling on the application's resident memory, started from wait_ready so
# every probe is covered without touching any probe body.
#
# WHY THIS EXISTS (measured 2026-10-08, the hard way): an AnimeEffects instance was
# found holding ~38 GB resident and ~95 GB virtual on a 46 GB machine, which is what
# took the owner's desktop (and this session) down twice. Nothing in these probes had
# ever bounded a run, and the sum was four instances of one project alive at once: a
# long-lived app being tree-walked, the 24-test suite, and a probe sweep.
#
# The ceiling has to sit ABOVE what a legitimate project needs or it fails honest runs.
# Measured on the biggest project here (`nene.anie`: 15000x15000 canvas, ~112 image
# layers, 2.4 GB of decoded images): ONE instance -- GUI or the headless round-trip --
# climbs to ~10 GB in ~80 s and then stays there for minutes. A run that keeps growing
# past 12 GB is not loading a project, it is leaking; override with AE_MAX_RSS_KB when
# a real job needs more. When the cap fires the app is killed and the probe reports a
# failure rather than letting the machine swap itself to death.
ae_memory_cap() { # ae_memory_cap <pid> [limit_kb]
    local pid=$1 limit=${2:-${AE_MAX_RSS_KB:-12582912}} # 12 GB (a big project needs ~10)
    ( while kill -0 "$pid" 2>/dev/null; do
          local rss
          rss=$(awk '/VmRSS/{print $2}' "/proc/$pid/status" 2>/dev/null)
          if [ -n "$rss" ] && [ "$rss" -gt "$limit" ]; then
              echo "MEMORY CAP: pid $pid hit ${rss} kB > ${limit} kB; killing it" >&2
              kill -9 "$pid" 2>/dev/null
              break
          fi
          sleep 2
      done ) &
    # The subshell's output is deliberately NOT discarded any more. It has exactly
    # one message - the cap firing - and swallowing it is how a run that kills the
    # application reads as "the application died for no reason": measured
    # 2026-10-14, a probe lost its application AND its X server at the first
    # accessibility query (the moment the accessible tree spikes) with no message
    # anywhere, because both the kill and the report were silenced here.
}

# Wait until the MAIN WINDOW is up, by asking the accessibility tree, instead of
# sleeping a guessed number of seconds. The probes all did `sleep 12`, which is not a
# readiness check: the same project opened in 4s on one run and took longer than 25s on
# another (llvmpipe, plus whatever else the machine is doing), and every assertion
# after the sleep then runs against the loading splash - a slow machine reported as a
# broken application.
# SIZE THE WINDOW, because nothing else will.
#
# There is no window manager under Xvfb, so nothing maximises the application: it maps
# at whatever its own size hint asks for with a FRESH settings sandbox (this file points
# XDG_CONFIG_HOME at a temp dir, so there is no remembered geometry either). Measured
# 2026-10-26 on a 1600x1000 Xvfb: the window came up 795x337 - less than half the screen
# - which is why a screenshot taken without this step shows a cramped layout, and why a
# probe that aims at a pixel computed from the SCREEN lands outside the window.
#
# The toplevel is found by GEOMETRY, not by name: `xdotool search --name AnimeEffects`
# returns the Qt selection-owner (3x3) and a 1x1 helper first, and `windowsize` on
# those quietly does nothing (the same trap the resize note in AGENTS.md records).
# `xwininfo -root -tree` lists them all, so the largest one wins.
#
# Default: fill the screen, which is what a maximised window looks like and what the
# screenshots a reviewer reads should show. AE_WINDOW_SIZE="WxH" overrides it;
# AE_NO_RESIZE=1 keeps the application's own size for a probe that tests it.
ae_size_window() { # ae_size_window [WxH]
    if [ "${AE_NO_RESIZE:-}" = "1" ]; then
        return 0
    fi
    local want="${1:-${AE_WINDOW_SIZE:-}}"
    if [ -z "$want" ]; then
        # the screen's own size, read from the X server rather than assumed
        local sw sh
        sw=$(xdotool getdisplaygeometry 2>/dev/null | awk '{print $1}')
        sh=$(xdotool getdisplaygeometry 2>/dev/null | awk '{print $2}')
        [ -n "${sw:-}" ] && [ -n "${sh:-}" ] && want="${sw}x${sh}"
    fi
    [ -n "$want" ] || return 0

    local win
    win=$(xwininfo -root -tree 2>/dev/null | awk '
        /[0-9]+x[0-9]+[+-]/ {
            geom = ""
            for (i = 1; i <= NF; ++i)
                if ($i ~ /^[0-9]+x[0-9]+[+-]/) geom = $i
            if (geom == "") next
            split(geom, a, "x")
            w = a[1] + 0
            h = a[2]
            sub(/[+-].*/, "", h)
            h = h + 0
            if (w * h > best) { best = w * h; bestline = $1 }
        }
        END { print bestline }')
    [ -n "${win:-}" ] || return 0

    local have
    have=$(xwininfo -id "$win" 2>/dev/null | awk '/Width:/ {print $2} /Height:/ {print $2}' | paste -sd x -)
    [ "$have" = "$want" ] && return 0

    xdotool windowsize "$win" "${want%x*}" "${want#*x}" 2>/dev/null || return 0
    # the application needs one event-loop pass to relayout its docks
    sleep 1
    local now
    now=$(xwininfo -id "$win" 2>/dev/null | awk '/Width:/ {print $2} /Height:/ {print $2}' | paste -sd x -)
    echo "window: was ${have:-unknown}, asked ${want}, now ${now:-unknown}"
}

wait_ready() { # wait_ready [seconds]
    # Guarded HERE rather than only in ae_press/ae_drag: every probe calls this
    # inside its xvfb-run body BEFORE its first raw `xdotool`, and the probes use
    # raw xdotool (mousemove, key, type, search) more often than they use the
    # helpers. One check at this point covers the input that would otherwise go
    # unchecked.
    ae_guard_display
    # ...and the run gets a memory ceiling at the same moment, because this is the
    # point every probe has the application up: see ae_memory_cap. The process is
    # found by NAME (`-x`), not by command line, so the `bash -c` wrapper that
    # carries the same path in its argv is not mistaken for the app.
    local app_pid
    app_pid=$(pgrep -n -x AnimeEffects 2>/dev/null)
    [ -n "$app_pid" ] && ae_memory_cap "$app_pid"
    local limit=${1:-120} i
    for i in $(seq 1 "$limit"); do
        if ae wait "File" >/dev/null 2>&1; then
            # THE WINDOW IS SIZED HERE, once per run, before any probe aims at a pixel:
            # see ae_size_window for why nothing else does it under Xvfb.
            ae_size_window
            return 0
        fi
        sleep 1
    done
    return 1
}

# Press a named widget through the accessibility ACTION instead of at its
# coordinates. Required for Qt Quick controls, because Qt reports a QML item's
# screen extents in the coordinates of its QQuickWidget and not of the window it
# sits in: a dialog that is not at the screen origin has every reported rectangle
# shifted by exactly its window offset, and a coordinate click lands beside the
# button (measured: the picker's "Cancel" reported x=333..405 while the dialog sat
# at x=61, so the reported centre missed it and probe_menu's "dismissing the
# picker closes it" failed; the click that worked was at x=400, where the button
# really is). An action cannot be shifted by a window position.
#
# It is a SEPARATE helper rather than the default because a popup item (a menu
# entry, a combo row) can answer a coordinate click where the action path is
# refused - and the reverse - so each probe says which route it means.
ae_activate() { # ae_activate <name> <what-the-caller-expects-to-happen> [role]
    local ok=0
    if [ -n "${3:-}" ]; then
        # A combo OPTION needs the role: a Qt Quick combo reports its current value
        # as its own accessible name, so with the popup open the closed field and
        # the item share a name, and the field comes first.
        ae click "$1" "$3" >/dev/null 2>&1 && ok=1
    else
        ae click "$1" >/dev/null 2>&1 && ok=1
    fi
    if [ "$ok" = 1 ]; then
        check 0 "$2"
        return 0
    fi
    check 1 "$2 (no accessibility action on '$1'${3:+ as a $3})"
    return 1
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
    if ae wait "Continue" >/dev/null 2>&1; then
        return 0
    fi
    echo "FAIL  the export flow did not open the destination picker: no widget named" >&2
    echo "FAIL  'Continue' appeared within the wait. This probe is recorded" >&2
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
export -f ae ae_center ae_press ae_activate ae_report ae_report_asked check require_export_flow
# ae_memory_cap is here because wait_ready CALLS it from inside the child shell: it was
# defined and never exported, so every probe's wait_ready printed
# "environment: line 3: ae_memory_cap: command not found" and the resident-memory
# ceiling described above was started by nobody. The cap the comment block above says
# every probe is covered by was, in fact, applied to no probe at all.
# ae_size_window is called by wait_ready from the same child shell, for the same reason.
export -f ae_y ae_drag wait_ready ae_guard_display ae_memory_cap ae_size_window
