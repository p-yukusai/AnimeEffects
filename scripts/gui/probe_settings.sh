#!/bin/bash
# The General Settings dialog, ported to Qt Quick, driven in the REAL application.
#
# The port replaced the five-tab QTabWidget form with a Qt Quick scene
# (src/gui/qml/GeneralSettingDialog.qml) inside a QDialog shell. A unit test can ask
# the view model and load the scene (tests/general_setting_form.cpp), but it cannot
# answer what this probe is for: does Preferences -> Settings still open the dialog,
# are the General tab's controls on screen and reachable by name, does switching to
# QoL actually swap the pages, and does Cancel close it.
#
# The TAB assertion is a state change, not a name match: "General" and "Keyboard" are
# words the main window uses too, so finding a node by that name would prove nothing.
# The probe presses "QoL" (a word only this dialog has) and asserts the General page's
# controls are GONE and a QoL one is up - only a real page switch does that.
#
# Addressed BY NAME through the accessibility tree, never by pixel.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/_lib.sh" "$@"

rm -f /tmp/gs_general.txt /tmp/gs_qol.txt /tmp/gs_after.txt

xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
  cd "$AE_PROBE_DIR"
  '"$APP"' "'"$PROJ"'" >/tmp/probe_settings.log 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID
  wait_ready 120 || sleep 12
  W=$(xdotool search --name "AnimeEffects" | tail -1)
  xdotool windowactivate --sync $W 2>/dev/null

  # 1. Preferences opens the dialog.
  ae_press "Preferences" "the Preferences menu opens"
  sleep 1
  ae_press "Settings" "the Settings entry is pressed"
  sleep 2

  # 2. The General tab is up: a combo row, the theme and accent groups, the donation
  #    box, the reset action and the two buttons.
  ae_report /tmp/gs_general.txt "Language (needs restart)" "Theme" "Accent color" \
      "Allow donation menu" "Reset recent files list" "Cancel" "OK"
  xdotool mousemove 700 650; sleep 0.5
  import -window root '"$SHOTS"'/settings_01_general.png

  # 2b. The combo boxes, driven with REAL POINTER CLICKS - the accessible press
  #     action calls the control toggle directly and cannot see the defect these
  #     three facts are about (reported 2026-10-11): the list opens; pressing the
  #     SAME field again closes it (it used to stay open, because the modal popup
  #     overlay swallowed the press); and pressing a SECOND combo closes the first
  #     AND opens that one (it used to only close the first).
  #
  #     A combo is addressed by its VALUE ("Auto", "Linear"), not by its label: the
  #     marker reports the value as the accessible NAME (Qt rule) and the label as
  #     its description, and the AeFieldLabel beside it carries the same words as a
  #     name - an exact match that wins over the combo, whose rect is then the
  #     LABEL rect, which is widget-local (measured: [16 71 125 15] for a dialog at
  #     x=81). Clicking that lands outside the dialog and tests nothing. The combo
  #     marker rect IS screen-correct (gui/qml/QmlFormAccessibility).
  ae_press "Auto" "the language combo is pressed"
  sleep 1
  # "Japanese" is an option row, so it exists only while the list is open; "Auto" is
  # the value the closed field shows and is on screen either way (the report anchor).
  ae_report /tmp/gs_combo_open.txt "Japanese" "Auto"
  xdotool mousemove 700 650; sleep 0.5
  import -window root '"$SHOTS"'/settings_02b_combo_open.png

  ae_press "Auto" "the same field is pressed again"
  sleep 1
  ae_report /tmp/gs_combo_closed.txt "Japanese" "Auto"
  xdotool mousemove 700 650; sleep 0.5
  import -window root '"$SHOTS"'/settings_02c_combo_closed.png

  #     The pass-through is asserted in the direction where it is OBSERVABLE. An open
  #     list covers the fields BELOW it, so pressing the combo underneath an open
  #     list is a press on a list ROW - it chooses that row, which is what a dropdown
  #     does, and because the top row is usually the current value it reads as "the
  #     list just closed". The Easing list opens DOWNWARD, away from the language
  #     field above it, so pressing the language combo is a real press on that field.
  ae_press "Linear" "the easing combo is opened"
  sleep 1
  ae_report /tmp/gs_combo_open2.txt "Sine" "Linear"
  xdotool mousemove 700 650; sleep 0.5
  import -window root '"$SHOTS"'/settings_02c2_combo_easing_open.png

  ae_press "Auto" "a second combo is pressed while the first is open"
  sleep 1
  # "Japanese" only exists while the LANGUAGE list is open, "Sine" only while the
  # EASING list is open: together they say the first closed and the second opened.
  ae_report /tmp/gs_combo_switch.txt "Japanese" "Auto" "Sine"
  xdotool mousemove 700 650; sleep 0.5
  import -window root '"$SHOTS"'/settings_02d_combo_switch.png
  # Close the language list by pressing its own field again, so the tab switch below
  # is not the press that closes it. NOT Escape: with no list open the scene Escape
  # Shortcut cancels the whole dialog (measured - that is what made the tab
  # assertions fail on the first run of this section).
  ae_press "Auto" "the language combo is pressed again to close it"
  sleep 1

  # 3. QoL switches the page: the General controls are gone and a QoL one is up.
  ae_activate "QoL" "the QoL tab is pressed"
  sleep 1
  ae_report /tmp/gs_qol.txt "Automatically save your project" "Allow donation menu" "Cancel"
  xdotool mousemove 700 650; sleep 0.5
  import -window root '"$SHOTS"'/settings_02_qol.png

  # 4. Cancel closes it.
  ae_activate "Cancel" "the dialog is cancelled"
  sleep 1.5
  ae_report /tmp/gs_after.txt "Allow donation menu" "Automatically save your project" "Preferences"
  xdotool mousemove 700 650; sleep 0.5
  import -window root '"$SHOTS"'/settings_03_closed.png
  kill $app 2>/dev/null; wait $app 2>/dev/null
' 2>&1 | tail -4

assert_showing /tmp/gs_general.txt "Language (needs restart)" "the dialog shows the language row"
assert_showing /tmp/gs_general.txt "Theme" "and the theme group"
assert_showing /tmp/gs_general.txt "Accent color" "and the accent group"
assert_showing /tmp/gs_general.txt "Allow donation menu" "and the donation checkbox"
assert_showing /tmp/gs_general.txt "Reset recent files list" "and the reset action"
assert_showing /tmp/gs_general.txt "OK" "and the OK action"
assert_showing /tmp/gs_general.txt "Cancel" "and the Cancel action"
assert_showing /tmp/gs_combo_open.txt "Japanese" "a pointer click on the field opens the list"
assert_absent /tmp/gs_combo_closed.txt "Japanese" "and pressing the same field again closes it"
assert_showing /tmp/gs_combo_open2.txt "Sine" "a pointer click opens a second combo's list"
assert_showing /tmp/gs_combo_switch.txt "Japanese" "pressing another combo opens that combo"
assert_absent /tmp/gs_combo_switch.txt "Sine" "while the first combo's list closes"
assert_rendered "$SHOTS/settings_02b_combo_open.png" "the combo with its list open"
assert_rendered "$SHOTS/settings_02c2_combo_easing_open.png" "the second combo with its list open"
assert_rendered "$SHOTS/settings_02d_combo_switch.png" "the first combo open after the switch"
assert_showing /tmp/gs_qol.txt "Automatically save your project" "the QoL tab shows a QoL row"
# HIDDEN, not absent: the General page is still in the scene (each page is a real
# layout toggled by its own bound `visible`), so its controls keep their AT-SPI nodes
# and stop showing. Measured on the running dialog.
assert_hidden /tmp/gs_qol.txt "Allow donation menu" "and the General page stops showing"
assert_showing /tmp/gs_qol.txt "Cancel" "while the footer stays"
assert_absent /tmp/gs_after.txt "Allow donation menu" "cancelling the dialog closes it"
assert_absent /tmp/gs_after.txt "Automatically save your project" "with the pages gone too"
assert_rendered "$SHOTS/settings_01_general.png" "the Settings dialog on its General tab"
assert_rendered "$SHOTS/settings_02_qol.png" "the dialog after switching to QoL"
assert_rendered "$SHOTS/settings_03_closed.png" "the window after cancelling"
summary probe_settings
