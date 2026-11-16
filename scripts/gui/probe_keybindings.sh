#!/bin/bash
# The Key Bindings dialog, ported to Qt Quick, driven in the REAL application.
#
# The port replaced the QTabWidget-of-QLineEdits form with a Qt Quick scene
# (src/gui/qml/KeyBindingDialog.qml) inside a QDialog shell. A unit test can ask the
# view model and load the scene (tests/key_binding_form.cpp), but it cannot answer
# what this probe is for: does Preferences -> Keybindings still open the dialog, is
# the group tab strip there, does switching a tab actually change the rows on screen,
# and does Cancel close it.
#
# The TAB assertion is a state change, not a name match. "Timeline" is a group name
# the main window also has words for, so finding a node called "Timeline" would prove
# nothing; the probe presses it and asserts that the General tab's rows are GONE and a
# Timeline row is up, which only a real tab switch does.
#
# Addressed BY NAME through the accessibility tree, never by pixel.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/_lib.sh" "$@"

rm -f /tmp/kb_dialog.txt /tmp/kb_timeline.txt /tmp/kb_after.txt

xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
  cd "$AE_PROBE_DIR"
  '"$APP"' "'"$PROJ"'" >/tmp/probe_keybindings.log 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID
  wait_ready 120 || sleep 12
  W=$(xdotool search --name "AnimeEffects" | tail -1)
  xdotool windowactivate --sync $W 2>/dev/null

  # 1. Preferences opens the dialog.
  ae_press "Preferences" "the Preferences menu opens"
  sleep 1
  ae_press "Keybindings" "the Keybindings entry is pressed"
  sleep 2

  # 2. The General tab is up: its rows, the reset action and the two buttons.
  ae_report /tmp/kb_dialog.txt "Undo last action" "Redo last action" "Reset keybinds" \
      "Cancel" "Ok"
  xdotool mousemove 700 650; sleep 0.5
  import -window root '"$SHOTS"'/keybindings_01_general.png

  # 3. The Timeline tab switches the rows: the General rows are gone and a Timeline
  #    row is up. This is what proves the tab strip is real and wired to the model.
  ae_activate "Timeline" "the Timeline tab is pressed"
  sleep 1
  ae_report /tmp/kb_timeline.txt "Copy selected key(s)" "Undo last action" "Reset keybinds"
  xdotool mousemove 700 650; sleep 0.5
  import -window root '"$SHOTS"'/keybindings_02_timeline.png

  # 4. Cancel closes it.
  ae_activate "Cancel" "the dialog is cancelled"
  sleep 1.5
  ae_report /tmp/kb_after.txt "Reset keybinds" "Undo last action" "Preferences"
  xdotool mousemove 700 650; sleep 0.5
  import -window root '"$SHOTS"'/keybindings_03_closed.png
  kill $app 2>/dev/null; wait $app 2>/dev/null
' 2>&1 | tail -4

assert_showing /tmp/kb_dialog.txt "Undo last action" "the dialog shows a General row"
assert_showing /tmp/kb_dialog.txt "Redo last action" "and the next one"
assert_showing /tmp/kb_dialog.txt "Reset keybinds" "and the reset action"
# "Ok", not "OK": this dialog's shell keeps the widget form's own word (EasyDialog
# spelled it "Ok"), where the other two dialogs say "OK".
assert_showing /tmp/kb_dialog.txt "Ok" "and the Ok action"
assert_showing /tmp/kb_dialog.txt "Cancel" "and the Cancel action"
assert_showing /tmp/kb_timeline.txt "Copy selected key(s)" "the Timeline tab shows a Timeline row"
# HIDDEN, not absent: the rows of the tab that is not showing are still in the scene
# (one ListView over every group's rows, each delegate visible only for its tab), so
# the AT-SPI node exists and is simply not showing. `absent` would be a stronger claim
# than the design makes.
assert_hidden /tmp/kb_timeline.txt "Undo last action" "and the General rows stop showing"
assert_showing /tmp/kb_timeline.txt "Reset keybinds" "while the tab strip's reset action stays"
assert_absent /tmp/kb_after.txt "Reset keybinds" "cancelling the dialog closes it"
assert_absent /tmp/kb_after.txt "Undo last action" "with the rows gone too"
assert_rendered "$SHOTS/keybindings_01_general.png" "the Key Bindings dialog on its General tab"
assert_rendered "$SHOTS/keybindings_02_timeline.png" "the dialog after switching to Timeline"
assert_rendered "$SHOTS/keybindings_03_closed.png" "the window after cancelling"
summary probe_keybindings
