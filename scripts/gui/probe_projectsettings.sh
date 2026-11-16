#!/bin/bash
# The Project Settings dialog, ported to Qt Quick, driven in the REAL application.
#
# The port replaced the QFormLayout form that used to live inside MainMenuBar with a
# Qt Quick scene (src/gui/qml/ProjectSettingDialog.qml) inside a QDialog shell, and
# moved the class into its own files. A unit test can ask the view model and load the
# scene (tests/project_setting_form.cpp), but it cannot answer what this probe is for:
# does the Edit menu's "Project settings" still open a dialog, are the four rows and
# the two actions actually on screen and reachable by name, and does Cancel close it.
#
# Addressed BY NAME through the accessibility tree, never by pixel: the port moved
# every control, and a pixel click would pass while driving the wrong widget.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/_lib.sh" "$@"

rm -f /tmp/ps_dialog.txt /tmp/ps_after.txt

xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
  cd "$AE_PROBE_DIR"
  '"$APP"' "'"$PROJ"'" >/tmp/probe_projectsettings.log 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID
  wait_ready 120 || sleep 12
  W=$(xdotool search --name "AnimeEffects" | tail -1)
  xdotool windowactivate --sync $W 2>/dev/null

  # 1. The Edit menu opens the dialog. Both steps by name.
  ae_press "Edit" "the Edit menu opens"
  sleep 1
  ae_press "Project settings" "the Project settings entry is pressed"
  sleep 2

  # 2. The dialog carries the four rows the port defines and the two actions. The
  #    canvas and frame-range pairs are the union rows (width x height, start .. end),
  #    so each field is asked for by its own accessible name.
  ae_report /tmp/ps_dialog.txt "Canvas size" "Canvas width" "Canvas height" \
      "Frame range" "Frame start" "Frame end" "Loop animation" "Frames per second" \
      "Viewport background" "Choose…" "Use theme background" \
      "OK" "Cancel"
  xdotool mousemove 700 650; sleep 0.5
  import -window root '"$SHOTS"'/projectsettings_01_dialog.png

  # 3. Cancel closes it.
  ae_activate "Cancel" "the dialog is cancelled"
  sleep 1.5
  # "Edit" is asked about as well: it is on screen either way, and a report answers as
  # soon as ANY name it was asked about is up, so naming a present anchor keeps the
  # absences from costing the full report timeout.
  ae_report /tmp/ps_after.txt "Canvas size" "Frame range" "Frames per second" \
      "Viewport background" "Choose…" "OK" "Edit"
  xdotool mousemove 700 650; sleep 0.5
  import -window root '"$SHOTS"'/projectsettings_02_closed.png

  # ------------------------------------------------------- and the row DOES something
  # The first half proved the row is on screen; this half proves it works. The
  # colour is picked through the stock QColorDialog, which is in the accessibility
  # tree like any other widget dialog (measured: its HTML field is addressable by
  # name, so the probe does not have to find a swatch in a grid).
  xdotool mousemove 700 650; sleep 0.5
  import -window root '"$SHOTS"'/projectsettings_03_theme.png

  ae_press "Edit" "the Edit menu opens a second time"
  sleep 1
  ae_press "Project settings" "the dialog is opened again for the colour"
  sleep 2
  ae_activate "Use theme background" "unticking the box asks the shell for a colour"
  sleep 2
  # The chooser is the stock QColorDialog. Its basic-colour grid carries no accessible
  # names and its HTML field answers no Value interface (measured: `ae set "HTML:"` is
  # refused, and typing into it by geometry does not commit), so the probe uses the
  # route the dialog itself offers - "Pick Screen Color" - and picks the pixel under
  # the button it just pressed. That makes the colour independent of this probe: it is
  # whatever the widget style painted there, and the assertions below only need it to
  # differ from the theme.
  ae_press "Pick Screen Color" "the screen colour picker is armed"
  sleep 1
  PICK=$(ae_center "Pick Screen Color")
  xdotool mousemove $PICK; sleep 0.3; xdotool click 1
  sleep 2
  xdotool key --clearmodifiers Return
  sleep 2
  ae_report /tmp/ps_chooser.txt "HTML:" "Viewport background" "Edit"
  ae_activate "OK" "the settings dialog applies the colour"
  sleep 2
  xdotool mousemove 700 650; sleep 0.5
  import -window root '"$SHOTS"'/projectsettings_04_colour.png

  # One undo takes it back. The menu is the route the application itself offers, so
  # the probe does not depend on a keyboard shortcut reaching an unfocused window.
  ae_press "Edit" "the Edit menu opens for the undo"
  sleep 1
  ae_press "Undo" "the colour change is undone"
  sleep 2
  xdotool mousemove 700 650; sleep 0.5
  import -window root '"$SHOTS"'/projectsettings_05_undone.png
  kill $app 2>/dev/null; wait $app 2>/dev/null
' 2>&1 | tail -14

assert_showing /tmp/ps_dialog.txt "Canvas size" "the dialog shows the Canvas size row"
assert_showing /tmp/ps_dialog.txt "Canvas width" "and its width field"
assert_showing /tmp/ps_dialog.txt "Canvas height" "and its height field"
assert_showing /tmp/ps_dialog.txt "Frame range" "and the Frame range row"
assert_showing /tmp/ps_dialog.txt "Frame start" "and its start field"
assert_showing /tmp/ps_dialog.txt "Frame end" "and its end field"
assert_showing /tmp/ps_dialog.txt "Loop animation" "and the Loop animation checkbox"
assert_showing /tmp/ps_dialog.txt "Frames per second" "and the Frames per second field"
assert_showing /tmp/ps_dialog.txt "Viewport background" "and the Viewport background row"
assert_showing /tmp/ps_dialog.txt "Choose…" "and its colour chooser"
assert_showing /tmp/ps_dialog.txt "Use theme background" "and its theme-default checkbox"
assert_showing /tmp/ps_dialog.txt "OK" "and the OK action"
assert_showing /tmp/ps_dialog.txt "Cancel" "and the Cancel action"
assert_absent /tmp/ps_after.txt "Canvas size" "cancelling the dialog closes it"
assert_absent /tmp/ps_after.txt "Frame range" "with the frame-range row gone too"
# "Loop animation" is the DIALOG's own name for the persisted interpolation flag: the
# playback bar's live toggle is "Loop playback" (PlayBackWidget.cpp), so an absence
# assertion on this one can now pass - and it is what distinguishes the two.
assert_absent /tmp/ps_after.txt "Frames per second" "and the rate row gone"
assert_absent /tmp/ps_after.txt "Loop animation" "and the loop row gone (the playback bar's own toggle is Loop playback)"
# "Viewport background" is the dialog's own string (the QColorDialog's title shares the
# words, but that dialog is not open here); "Choose…" exists nowhere else in the app.
assert_absent /tmp/ps_after.txt "Viewport background" "and the viewport row gone"
assert_absent /tmp/ps_after.txt "Choose…" "and its chooser gone"
assert_absent /tmp/ps_after.txt "OK" "and its actions gone"
assert_absent /tmp/ps_chooser.txt "HTML:" "accepting the chooser closes it"
assert_showing /tmp/ps_chooser.txt "Viewport background" "with the settings dialog back"

# The canvas really changes colour, and the undo really puts it back. ImageMagick's
# AE metric counts differing pixels, so this needs no knowledge of where the canvas
# is: the viewport fills the display around the image, and recolouring it repaints
# thousands of pixels. The pointer is parked in the same place before every capture,
# which keeps a hover highlight out of the comparison.
changed=$(compare -metric AE "$SHOTS/projectsettings_03_theme.png" "$SHOTS/projectsettings_04_colour.png" null: 2>&1 | awk '{print $1}')
restored=$(compare -metric AE "$SHOTS/projectsettings_03_theme.png" "$SHOTS/projectsettings_05_undone.png" null: 2>&1 | awk '{print $1}')
awk -v c="${changed:-0}" 'BEGIN { exit (c+0 > 4000) ? 0 : 1 }'
check $? "the chosen colour reaches the canvas ($changed pixels changed)"
awk -v r="${restored:-0}" 'BEGIN { exit (r+0 < 2000) ? 0 : 1 }'
check $? "and one undo puts the theme colour back (only $restored pixels differ from the theme shot)"
assert_rendered "$SHOTS/projectsettings_04_colour.png" "the window after the colour change"
assert_rendered "$SHOTS/projectsettings_05_undone.png" "the window after the undo"
assert_rendered "$SHOTS/projectsettings_01_dialog.png" "the Project settings dialog"
assert_rendered "$SHOTS/projectsettings_02_closed.png" "the window after cancelling"
summary probe_projectsettings
