#!/bin/bash
# See _lib.sh: these drive the real app under Xvfb because the wiring they check
# is not reachable from a unit test.
#
# THE REPEAT, which is the whole iteration loop of an animation tool: the same sticker
# gets exported a dozen times while the art is adjusted. "Export again" repeats the
# last export - destination, quality, range, size, fps, artifact - WITHOUT re-walking
# the menu and the destination, and asks for a NAME instead.
#
# What this probe establishes is the SHAPE of the second run, which is what the old
# one could not see: the destination picker must NOT be required again (it is asserted
# absent while the repeat is asking for its name), and the run must still reach the
# export path and say what it wrote. The version this replaces clicked recorded pixels
# for the menu row and asserted that the screenshot was not black, so a repeat that had
# quietly started asking for a destination again would have gone on passing.
#
# The destination is preselected through the sandboxed settings rather than by clicking
# a row: choosing one is a typed search in the picker now, and this probe is not about
# that choice (probe_menu and probe_dest are). One real export is needed to enable
# "Export again" at all - it is disabled until an export succeeds - and it is kept as
# short as the form allows: four frames.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/_lib.sh" "$@"
# The revision of the export flow this probe's navigation was recorded against. See
# AE_FLOW_REVISION in _lib.sh: the guard refuses to run a probe that predates it.
export RECORDED_AGAINST=2

CFG=$(mktemp -d)
OUT=$(mktemp -d)
export XDG_CONFIG_HOME="$CFG"
mkdir -p "$CFG/AnimeEffectsProject"
printf '[General]\nexport_destination=chat.discord.file\nexport_quality_bucket=1\n' \
    > "$CFG/AnimeEffectsProject/AnimeEffects.conf"
export OUT
rm -f "$OUT/repeat1.mp4" "$OUT/repeat2.mp4" /tmp/re_set.txt /tmp/re_status.txt /tmp/re_size1.txt /tmp/re_after.txt

xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
  # The frame range, by name, through the Value interface: ae_ui.py can click what it
  # finds, but this Qt build answers EditableText with an EMPTY string for a QSpinBox,
  # so `ae set` writes text that no value is taken from - and the label beside the box
  # reads "- First frame -", which contains the same words. What is asked for is the
  # node that answers to the name AND HOLDS A NUMBER. (probe_inputs.sh carries the same
  # helper with a reader; it is duplicated rather than shared because the only file the
  # two could share is _lib.sh, which is not this probe to change.)
  set_range() { # set_range <first> <last>
    python3 - "$1" "$2" <<PY
import os, sys, warnings
warnings.filterwarnings("ignore")
import gi
gi.require_version("Atspi", "2.0")
from gi.repository import Atspi as A

want = os.environ.get("AE_UI_PID", "").strip()
wanted = {"First frame 1": sys.argv[1], "Last frame 1": sys.argv[2]}

def walk(node, depth=0):
    if depth > 16:
        return
    try:
        count = node.get_child_count()
    except Exception:
        return
    for i in range(count):
        try:
            child = node.get_child_at_index(i)
        except Exception:
            continue
        yield child
        yield from walk(child, depth + 1)

def application():
    desktop = A.get_desktop(0)
    best = None
    for i in range(desktop.get_child_count()):
        try:
            app = desktop.get_child_at_index(i)
            if "AnimeEffects" not in (app.get_name() or ""):
                continue
            if want and str(app.get_process_id()) != want:
                continue
            if best is None or app.get_child_count() > best.get_child_count():
                best = app
        except Exception:
            continue
    return best

app = application()
if app is None:
    print("NO APPLICATION NODE")
    sys.exit(1)
for node in walk(app):
    try:
        shown = node.get_name() or ""
        described = node.get_description() or ""
    except Exception:
        continue
    for name in list(wanted):
        if name != shown and name != described and name not in shown:
            continue
        iface = node.get_value_iface()
        if iface is None:
            continue
        try:
            iface.set_current_value(float(wanted.pop(name)))
            print("set " + name + "=" + str(int(iface.get_current_value())))
        except Exception as exc:
            print("FAILED " + name + " " + str(exc))
            sys.exit(1)
        break
if wanted:
    print("MISSING " + ",".join(wanted))
    sys.exit(1)
PY
  }

  # The application is launched FROM the output directory and the file dialogs are
  # given a bare name: the platform file dialog is a different toolkit in the same
  # process, so its widgets are not in the application tree and it cannot be addressed
  # by name - a relative name resolves against the working directory instead.
  cd "$OUT"
  '"$APP"' "'"$PROJ"'" >/tmp/probe_reexport.log 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID   # the window the tooling must address
  wait_ready 120 || sleep 12
  W=$(xdotool search --name "AnimeEffects" | tail -1)
  xdotool windowactivate --sync $W 2>/dev/null

  # WHAT THE EXPORT SAID IS NOT ADDRESSABLE BY NAME. The obvious completion signal is
  # the status bar message ("Exported repeat1.mp4 (2 KB) ..."), and it cannot be had:
  # QStatusBar PAINTS its temporary message rather than putting a widget there, so the
  # accessibility tree carries a nameless "status bar" node and no label with the text
  # (measured: twelve polls over two minutes found nothing while the message was on
  # screen, and a dump of the live application names no node with the file in it). What
  # is waited for instead is the FILE, until its size stops moving - the muxer writes
  # its header the moment the export opens, so a file that exists is not a finished
  # export. What the status bar held is recorded beside this as evidence, and the note
  # printed below the assertions says whether the tree could see it at all.
  wait_file() { # wait_file <path>
    local size="" prev=""
    local i
    for i in $(seq 1 150); do
      if [ -s "$1" ]; then
        size=$(stat -c %s "$1" 2>/dev/null)
        if [ -n "$prev" ] && [ "$size" = "$prev" ]; then return 0; fi
        prev=$size
      fi
      sleep 2
    done
    return 1
  }

  # 1. The first export, through the flow as it is now: WHERE first, then the form.
  ae_press "File" "the File menu opens"
  ae_press "Export project as" "the export entry is there"
  require_export_flow
  ae_activate "Continue" "the remembered destination is accepted"
  ae wait "Advanced" >/dev/null 2>&1 || true
  # H41: the frame range lives under Advanced, so the section has to be OPEN before
  # its rows can be addressed by name.
  ae_activate "Advanced" "the Advanced section opens, adding its rows"
  sleep 1.5
  # Short, so that the first export is seconds and not minutes: this probe is about the
  # repeat, not about the encode. If the range cannot be set, the body stops HERE
  # rather than encoding the whole project - a probe that quietly ran for a quarter of
  # an hour would be worse than one that fails.
  if set_range 1 4 > /tmp/re_set.txt; then
    check 0 "the exported range is set by name ($(tr "\n" " " < /tmp/re_set.txt))"
  else
    check 1 "the exported range is set by name (see /tmp/re_set.txt: $(tr "\n" " " < /tmp/re_set.txt))"
    xdotool mousemove 700 600; sleep 0.5
    import -window root '"$SHOTS"'/re_00_range_failed.png
    kill $app 2>/dev/null; wait $app 2>/dev/null
    exit 1
  fi
  sleep 1.2
  xdotool mousemove 700 600; sleep 0.5
  import -window root '"$SHOTS"'/re_01_form.png
  ae_activate "Export" "the first export is asked for"
  sleep 4
  xdotool key --clearmodifiers ctrl+a; sleep 0.4
  xdotool type --delay 25 "repeat1.mp4"; sleep 0.6
  xdotool key Return
  # The wait is asserted, not assumed: a file that never finished being written would
  # otherwise show up as a short file that "exists", which is the shape of failure the
  # old probe was built on.
  wait_file "$OUT/repeat1.mp4" || check 1 "the first export finished writing its file"
  # What the status bar holds, recorded rather than asserted (see wait_file above).
  ae_report /tmp/re_status.txt "Exported repeat1.mp4" "File"
  # The first file as it stands, so the repeat can be held to leaving it alone.
  stat -c %s "$OUT/repeat1.mp4" > /tmp/re_size1.txt 2>/dev/null
  xdotool mousemove 700 600; sleep 0.5
  import -window root '"$SHOTS"'/re_02_first_done.png

  # 2. Export again. It must ask for a NAME and not for a destination: no picker, and
  #    the run still reaches the export path. "File" is asked about as well, because a
  #    report answers as soon as ANY name it was asked about is up - naming something
  #    that is on screen either way keeps the two absences from costing the full report
  #    timeout each.
  ae_press "File" "the File menu opens for the repeat"
  ae_press "Export again" "Export again is enabled by the first export"
  sleep 4
  ae_report /tmp/re_repeat.txt "Search destinations" "Continue" "File"
  xdotool mousemove 700 600; sleep 0.5
  import -window root '"$SHOTS"'/re_03_name_prompt.png
  xdotool key --clearmodifiers ctrl+a; sleep 0.4
  xdotool type --delay 25 "repeat2.mp4"; sleep 0.6
  xdotool key Return
  wait_file "$OUT/repeat2.mp4" || check 1 "the repeat finished writing its file"
  sleep 1
  # A failed export raises a message box titled "Export Error": the one thing the repeat
  # must not do quietly instead of writing the file.
  ae_report /tmp/re_after.txt "Export Error" "File"
  xdotool mousemove 700 600; sleep 0.5
  import -window root '"$SHOTS"'/re_04_again_done.png
  kill $app 2>/dev/null; wait $app 2>/dev/null
' 2>&1 | tail -4

# THE POINT OF THE PROBE: the repeat did not re-walk the destination flow.
assert_absent /tmp/re_repeat.txt "Search destinations" "Export again asks for no destination: the picker is not up"
assert_absent /tmp/re_repeat.txt "Continue" "not even its answer button, so the picker was not required"
# ...and both runs reached the export path, which is what the files say. The first one
# has to have been written for "Export again" to be enabled at all (it stays disabled
# until an export succeeds), and the second could not exist otherwise.
assert_exists "$OUT/repeat1.mp4" "the first export wrote the file it was named"
assert_exists "$OUT/repeat2.mp4" "and the repeat wrote a second file, so it reached the export path"
# The repeat repeats the SETTINGS, and it must not do it by overwriting the first
# result: the earlier version of this repeated the output path as well and defended
# itself with an overwrite confirmer on the same name, so answering yes destroyed the
# previous export with no way back.
first=$(stat -c %s "$OUT/repeat1.mp4" 2>/dev/null || echo 0)
at_first=$(cat /tmp/re_size1.txt 2>/dev/null || echo 0)
if [ "$first" -gt 0 ] && [ "$first" = "$at_first" ]; then
    check 0 "the repeat left the first export alone ($first bytes, unchanged)"
else
    check 1 "the repeat left the first export alone (was $at_first bytes, is $first)"
fi
assert_absent /tmp/re_after.txt "Export Error" "and no export error was raised"
# The status bar message is recorded, and deliberately NOT asserted: see wait_file in
# the body - QStatusBar paints it, so there is no node to find it by. Asserting it would
# report a limit of the tooling as a fault of the application, which is the one thing
# the stale marker in _lib.sh exists to prevent.
echo "NOTE  the status bar is not addressable by name; ae_ui.py reported: $(tr '\n' ' ' < /tmp/re_status.txt)"
assert_rendered "$SHOTS/re_01_form.png" "the form on the first export"
assert_rendered "$SHOTS/re_03_name_prompt.png" "the name prompt Export again opened instead of a destination picker"
assert_rendered "$SHOTS/re_04_again_done.png" "the application after the repeat"
rm -rf "$CFG" "$OUT"
summary probe_reexport
