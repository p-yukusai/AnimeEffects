#!/bin/bash
# See _lib.sh: these drive the real app under Xvfb because the wiring they check
# is not reachable from a unit test.
#
# The File menu, addressed BY NAME. Two entries about export live in it:
# "Export project as" (the form, after the destination) and "Quickly export as..."
# (the same picker, and then the export runs with no further dialogs). The quick
# entry is NOT a submenu tree of fifty-five rows any more - it is the picker - so
# what this probe has to establish is that the entry is there and that the picker is
# what it opens.
#
# The version this replaces clicked recorded pixels (a menu at 16,10 and the entry
# at 60,182) and then asserted that the screenshot was not black. Both clicks landed
# on whatever had moved underneath them after the flow changed, and "the picture is
# not black" is true of a window showing something else entirely: the probe kept
# passing while driving a menu row that was no longer the quick-export entry.
#
# The BODY only navigates and leaves evidence (screenshots, report files); every
# assertion runs out here, where the helpers and the failure count live.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/_lib.sh" "$@"
# The revision of the export flow this probe's navigation was recorded against. See
# AE_FLOW_REVISION in _lib.sh: the guard refuses to run a probe that predates it.
export RECORDED_AGAINST=2

rm -f /tmp/menu_file.txt /tmp/menu_picker.txt /tmp/menu_after.txt

xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
  # cd as its OWN command, not as `cd X && App &`: a background job whose body is a
  # compound command is a SUBSHELL, so $! would name the subshell instead of the
  # application and AE_UI_PID would match no accessible root.
  cd "$AE_PROBE_DIR"
  '"$APP"' "'"$PROJ"'" >/tmp/probe_menu.log 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID   # the window the tooling must address
  sleep 12
  W=$(xdotool search --name "AnimeEffects" | tail -1)
  xdotool windowactivate --sync $W 2>/dev/null

  # 1. Both export entries, by name, in the File menu: nothing here depends on where
  #    a menu happens to be drawn.
  ae_press "File" "the File menu opens"
  sleep 1
  ae_report /tmp/menu_file.txt "Export project as" "Quickly export as..." "Export again"
  xdotool mousemove 700 600; sleep 0.5
  import -window root '"$SHOTS"'/menu_01_file.png

  # 2. The quick entry opens the destination picker: WHERE comes first even on the
  #    shortcut, and the picker is the screen that has to answer.
  ae_press "Quickly export as..." "the quick export entry is pressed"
  require_export_flow
  ae_report /tmp/menu_picker.txt "Search destinations" "Use this destination" "Cancel"
  xdotool mousemove 700 600; sleep 0.5
  import -window root '"$SHOTS"'/menu_02_quick_picker.png

  # 3. Dismissed. The quick action leads to the picker and then to an export: it does
  #    NOT open the parameter form, which is the whole difference between it and
  #    "Export project as".
  ae_press "Cancel" "the picker is dismissed"
  sleep 1.5
  # "File" is asked about as well: it is on screen either way, and a report answers as
  # soon as ANY name it was asked about is up, so naming a present anchor is what keeps
  # the three absences below from costing the full report timeout each.
  ae_report /tmp/menu_after.txt "Search destinations" "Use this destination" "Destination" "File"
  xdotool mousemove 700 600; sleep 0.5
  import -window root '"$SHOTS"'/menu_03_dismissed.png
  kill $app 2>/dev/null; wait $app 2>/dev/null
' 2>&1 | tail -4

assert_showing /tmp/menu_file.txt "Export project as" "the File menu carries Export project as"
assert_showing /tmp/menu_file.txt "Quickly export as..." "and Quickly export as... beside it"
assert_showing /tmp/menu_file.txt "Export again" "and the repeat entry, which a completed export enables"
assert_showing /tmp/menu_picker.txt "Search destinations" "the quick export opens the destination picker"
assert_showing /tmp/menu_picker.txt "Use this destination" "with the picker's own answer button"
assert_showing /tmp/menu_picker.txt "Cancel" "and its own way out"
assert_absent /tmp/menu_after.txt "Search destinations" "dismissing the picker closes it"
assert_absent /tmp/menu_after.txt "Use this destination" "both of the picker's own buttons gone with it"
assert_absent /tmp/menu_after.txt "Destination" "and the quick action never opened the parameter form"
assert_rendered "$SHOTS/menu_01_file.png" "the File menu"
assert_rendered "$SHOTS/menu_02_quick_picker.png" "the picker the quick export opened"
assert_rendered "$SHOTS/menu_03_dismissed.png" "the window after dismissing the picker"
summary probe_menu
