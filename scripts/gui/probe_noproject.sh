#!/bin/bash
# See _lib.sh: these drive the real app under Xvfb because the wiring they check
# is not reachable from a unit test.
#
# WHAT "NO PROJECT" ACTUALLY IS, measured rather than assumed. Launching with no
# argument does NOT leave the application without a project: it opens a New Project
# by itself, and every export entry is live on that empty project. The state this
# probe is named for is reached by closing it - File -> Close project - and there the
# entries are still IN the File menu and DISABLED, and pressing one does nothing.
#
# That distinction is the probe. The version this replaces clicked a recorded pixel
# for "Close project" (16,10 then 60,253), clicked another for the File menu, and
# asserted that the screenshot was not black - so it reported nothing at all about
# whether the export entries were reachable, and it would have gone on passing if
# closing the project had stopped working entirely.
#
# The BODY only navigates and leaves evidence; every assertion runs out here.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/_lib.sh" "$@"
# The revision of the export flow this probe's navigation was recorded against. See
# AE_FLOW_REVISION in _lib.sh: the guard refuses to run a probe that predates it.
export RECORDED_AGAINST=2

rm -f /tmp/np_*.txt

xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
  # The enabled/disabled state of the menu entries, BY NAME. ae_ui.py reports what is
  # SHOWING, and a disabled menu entry is still on screen - which is exactly the
  # difference this probe is about, so the state is read from the same accessibility
  # tree the rest of the tooling uses.
  states() { # states <output file>
    python3 - "$1" <<PY
import os, sys, warnings
warnings.filterwarnings("ignore")
import gi
gi.require_version("Atspi", "2.0")
from gi.repository import Atspi as A

want = os.environ.get("AE_UI_PID", "").strip()
names = ["New project", "Open project", "Save project", "Export project as",
         "Quickly export as...", "Export again", "Close project"]

def walk(node, depth=0):
    if depth > 14:
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

out = open(sys.argv[1], "w")
app = application()
if app is None:
    out.write("NO APPLICATION NODE\n")
    out.close()
    sys.exit(0)
seen = {}
for node in walk(app):
    try:
        name = node.get_name() or ""
    except Exception:
        continue
    if name not in names:
        continue
    st = node.get_state_set()
    usable = st.contains(A.StateType.ENABLED) and st.contains(A.StateType.SENSITIVE)
    seen.setdefault(name, []).append("enabled" if usable else "DISABLED")
for name in names:
    out.write(name + " | " + ",".join(seen.get(name, ["absent"])) + "\n")
out.close()
PY
  }

  cd "$AE_PROBE_DIR"
  # NO project argument: this is how the probe has always launched, and the point of
  # it is the state the application starts in when nobody hands it a file.
  '"$APP"' >/tmp/probe_noproject.log 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID   # the window the tooling must address
  sleep 13
  W=$(xdotool search --name "AnimeEffects" | tail -1)
  xdotool windowactivate --sync $W 2>/dev/null

  # 1. A fresh start is NOT the no-project state: the application opens a New Project
  #    of its own, and the export entries are live on it. Recorded here so that the
  #    assertions below can tell the two states apart instead of guessing which one
  #    "no argument" produces.
  ae_press "File" "the File menu opens on a fresh start"
  sleep 1
  ae_report /tmp/np_fresh_menu.txt "Export project as" "Quickly export as..." "Export again" "Save project" "New project"
  states /tmp/np_fresh_states.txt
  xdotool mousemove 700 600; sleep 0.5
  import -window root '"$SHOTS"'/np_01_fresh_start.png

  # 2. Close the project. This is what "no project" means in this application, and the
  #    File menu is the only way to it.
  ae_press "Close project" "the startup project is closed"
  sleep 3
  ae_press "File" "the File menu opens with no project"
  sleep 1.2
  ae_report /tmp/np_menu.txt "Export project as" "Quickly export as..." "Export again" "Save project" "New project"
  states /tmp/np_states.txt
  xdotool mousemove 700 600; sleep 0.5
  import -window root '"$SHOTS"'/np_02_no_project.png

  # 3. And the disabled entry does nothing when it is pressed: no picker, no form. A
  #    report answers as soon as ANY name it was asked about is up, so "File" is asked
  #    about too - it is on screen either way, which keeps the absences below from
  #    costing the full report timeout.
  ae_press "Export project as" "the export entry is pressed with no project"
  sleep 3
  ae_report /tmp/np_after_click.txt "Search destinations" "Use this destination" "Destination" "File"
  xdotool mousemove 700 600; sleep 0.5
  import -window root '"$SHOTS"'/np_03_after_click.png
  xdotool key Escape; sleep 0.5
  kill $app 2>/dev/null; wait $app 2>/dev/null
' 2>&1 | tail -4

# "Is it enabled" is not one of ae_report's three answers (a disabled entry is still
# on screen), so the states file is checked directly - one line per entry, and a name
# the query never found reports "absent" rather than passing silently.
expect_state() { # expect_state <file> <entry> <enabled|DISABLED> <what>
    if grep -qxF "$2 | $3" "$1"; then
        check 0 "$4"
    else
        check 1 "$4 (got: $(grep -F "$2 |" "$1" 2>/dev/null | tr -d '\n' | head -c 120))"
    fi
}

# A report that never asked about a name fails loudly (see ae_report_asked), so the
# names asserted below and the names asked about above are the same lists.
assert_showing /tmp/np_fresh_menu.txt "Export project as" "the entries are in the File menu on a fresh start"
# The measured truth, and the reason this probe closes the project itself: no argument
# does NOT mean no project. The application opens a New Project, so the export entry
# is usable - which is what the no-project state below has to be told apart from.
expect_state /tmp/np_fresh_states.txt "Export project as" enabled \
    "a fresh start opens a New Project, so Export project as is usable"

assert_showing /tmp/np_menu.txt "Export project as" "with the project closed the entry is still in the File menu"
assert_showing /tmp/np_menu.txt "Quickly export as..." "and so is the quick one"
expect_state /tmp/np_states.txt "Export project as" DISABLED "the export entry is DISABLED with no project"
expect_state /tmp/np_states.txt "Quickly export as..." DISABLED "and so is the quick export entry"
expect_state /tmp/np_states.txt "Export again" DISABLED "and the repeat, which needs an export to repeat"
expect_state /tmp/np_states.txt "Save project" DISABLED "nothing is open, so there is nothing to save"
expect_state /tmp/np_states.txt "New project" enabled "while the entries that do not need a project stay usable"

# The disabled entry, pressed: the flow must not open. This is the assertion the old
# probe was reaching for and never made - it had no way to say "nothing happened".
assert_absent /tmp/np_after_click.txt "Search destinations" "pressing it opens no destination picker"
assert_absent /tmp/np_after_click.txt "Use this destination" "so the picker is not there at all"
assert_absent /tmp/np_after_click.txt "Destination" "and the parameter form is not reached either"
assert_rendered "$SHOTS/np_01_fresh_start.png" "the File menu on a fresh start"
assert_rendered "$SHOTS/np_02_no_project.png" "the File menu with the project closed"
assert_rendered "$SHOTS/np_03_after_click.png" "the window after pressing the disabled entry"
summary probe_noproject
