#!/bin/bash
# The New Project dialog, ported to Qt Quick, driven in the REAL application.
#
# The port replaced a QFormLayout widget form with a Qt Quick scene
# (src/gui/qml/NewProjectDialog.qml) inside a QDialog shell. A unit test can ask the
# view model and load the scene (tests/new_project_form.cpp), but it cannot answer
# what this probe is for: does the File menu's "New project" still open a dialog,
# are the five rows and the live-link checkbox actually on screen and reachable by
# name, does ticking Live link write through the scene, and does Cancel close it.
#
# Addressed BY NAME through the accessibility tree, never by pixel: the port moved
# every control, and a pixel click would pass while driving the wrong widget.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/_lib.sh" "$@"

rm -f /tmp/newproject_dialog.txt /tmp/newproject_ticked.txt /tmp/newproject_after.txt

xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
  cd "$AE_PROBE_DIR"
  '"$APP"' "'"$PROJ"'" >/tmp/probe_newproject.log 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID
  wait_ready 120 || sleep 12
  W=$(xdotool search --name "AnimeEffects" | tail -1)
  xdotool windowactivate --sync $W 2>/dev/null

  # 1. The File menu opens the dialog. Both steps by name.
  ae_press "File" "the File menu opens"
  sleep 1
  ae_press "New project" "the New project entry is pressed"
  sleep 2

  # 2. The dialog is up and carries the five rows the port defines, the live-link
  #    checkbox, its helper sentence, and the two actions.
  ae_report /tmp/newproject_dialog.txt "Initial asset" "Frame count" "Frames per second" \
      "Specify canvas size" "Canvas width" "Canvas height" "Live link" \
      "Reload when the asset file changes on disk" "Browse…" "Create" "Cancel"
  xdotool mousemove 700 650; sleep 0.5
  import -window root '"$SHOTS"'/newproject_01_dialog.png

  # 3. Ticking Live link reaches the scene (the action path; a QML control answers
  #    it where a coordinate click would be shifted by the QQuickWidget origin).
  ae_activate "Live link" "the Live link checkbox is pressed"
  sleep 0.5
  # Ticking it must not close, crash or rebuild the dialog: the same rows are still
  # there, so the click reached the control and nothing tore down.
  ae_report /tmp/newproject_ticked.txt "Live link" "Create" "Cancel"
  xdotool mousemove 700 650; sleep 0.5
  import -window root '"$SHOTS"'/newproject_02_ticked.png

  # 4. Cancel closes it and the dialog is gone.
  ae_activate "Cancel" "the dialog is cancelled"
  sleep 1.5
  ae_report /tmp/newproject_after.txt "Initial asset" "Live link" "Create" "File"
  xdotool mousemove 700 650; sleep 0.5
  import -window root '"$SHOTS"'/newproject_03_closed.png
  kill $app 2>/dev/null; wait $app 2>/dev/null
' 2>&1 | tail -4

assert_showing /tmp/newproject_dialog.txt "Initial asset" "the dialog shows the Initial asset row"
assert_showing /tmp/newproject_dialog.txt "Frame count" "and the Frame count row"
assert_showing /tmp/newproject_dialog.txt "Frames per second" "and the Frames per second row"
assert_showing /tmp/newproject_dialog.txt "Specify canvas size" "and the canvas-size checkbox"
assert_showing /tmp/newproject_dialog.txt "Canvas width" "and the Canvas width field"
assert_showing /tmp/newproject_dialog.txt "Canvas height" "and the Canvas height field"
assert_showing /tmp/newproject_dialog.txt "Live link" "and the Live link checkbox"
assert_showing /tmp/newproject_dialog.txt "Reload when the asset file changes on disk" "and its helper sentence"
assert_showing /tmp/newproject_dialog.txt "Browse…" "and the Browse button"
assert_showing /tmp/newproject_dialog.txt "Create" "and the Create action"
assert_showing /tmp/newproject_dialog.txt "Cancel" "and the Cancel action"
assert_showing /tmp/newproject_ticked.txt "Live link" "the Live link checkbox is still there after ticking"
assert_absent /tmp/newproject_after.txt "Initial asset" "cancelling the dialog closes it"
assert_absent /tmp/newproject_after.txt "Live link" "with the live-link checkbox gone too"
assert_absent /tmp/newproject_after.txt "Create" "and its actions gone"
assert_rendered "$SHOTS/newproject_01_dialog.png" "the New Project dialog"
assert_rendered "$SHOTS/newproject_02_ticked.png" "the dialog after ticking Live link"
assert_rendered "$SHOTS/newproject_03_closed.png" "the window after cancelling"
summary probe_newproject
