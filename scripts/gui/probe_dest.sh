#!/bin/bash
# See _lib.sh: these drive the real app under Xvfb because the wiring they check
# is not reachable from a unit test.
#
# This probe is about the FLOW: Export asks WHERE first, the picker is a tree of
# platforms, typing narrows it, and the form that follows states the destination it
# was given. Every step addresses widgets by NAME (ae_ui.py): the previous version
# clicked recorded pixels, so it kept passing after the flow changed underneath it,
# which is how this probe spent the destination-first rewrite asserting nothing.
#
# The BODY only navigates and leaves evidence (screenshots, report files). The
# assertions run out here, where the helpers and the failure count live.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/_lib.sh" "$@"
# The revision of the export flow this probe's navigation was recorded against. See
# AE_FLOW_REVISION in _lib.sh: the guard refuses to run a probe that predates it.
export RECORDED_AGAINST=2

rm -f /tmp/dst_picker.txt /tmp/dst_form.txt /tmp/dst_label.txt

xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
  '"$APP"' "'"$PROJ"'" >/tmp/probe_app.log 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID   # the window the tooling must address
  wait_ready 120 || sleep 12
  W=$(xdotool search --name "AnimeEffects" | tail -1)
  xdotool windowactivate --sync $W 2>/dev/null

  # 1. Export asks WHERE, before anything else. One click on the menu, then the item
  #    by name: nothing here depends on where a menu happens to be drawn.
  ae_press "File" "the File menu opens"
  ae_press "Export project as" "the export entry is there"
  require_export_flow
  ae_report /tmp/dst_picker.txt "Search destinations" "Use this destination"
  xdotool mousemove 700 600; sleep 0.5
  import -window root '"$SHOTS"'/dst_01_picker.png

  # 2. Typing narrows the tree, opens the platform it found and selects a
  #    destination inside it, so Enter is a complete answer.
  ae set "Search destinations" "discord" >/dev/null
  sleep 1.2
  xdotool mousemove 700 600; sleep 0.5
  import -window root '"$SHOTS"'/dst_02_search.png

  # 3. The default button answers with that destination, and the form that follows
  #    states it: what the picker chose must be what the dialog shows.
  ae_activate "Use this destination" "the chosen destination is accepted"
  # Wait for the form to have applied the destination before snapshotting it. A
  # report answers as soon as ANY name it was asked about is on screen, so asking
  # about "Destination" (visible the moment the form opens) can return a snapshot of
  # the form BEFORE the plan for that destination has been applied - which is how
  # this probe once reported "Advanced: hidden" for a dialog whose screenshot showed
  # it plainly on screen.
  ae wait "Advanced" >/dev/null 2>&1 || true
  sleep 1
  ae_report /tmp/dst_form.txt "Destination" "Width" "Height" "Format" "Quality" \
                                      "Advanced" "Keep aspect ratio"
  # Read with a retry. A single query against an application that is still working
  # (the plan runs a short probe encode when the codec has a quality knob) can come
  # back with an EMPTY tree, and an empty file here is read as "the form stated
  # nothing": a failure of the read reported as a failure of the application. The
  # assertions below are about the facts, so the facts have to arrive first.
  for i in 1 2 3 4 5 6 7 8; do
    ae text > /tmp/dst_form_text.txt 2>/dev/null
    [ -s /tmp/dst_form_text.txt ] && break
    sleep 1
  done
  # Which words on screen belong to which field. This is the OTHER half of
  # addressing a form: a control can answer to a name and still have nothing beside
  # it that reads as its label.
  # A QML Text has no label-for relation (see probe_dialog): read the words from
  # the tree, exactly, so the facts row "Frame rate:" cannot stand in for the
  # label of the field.
  ae text | grep -x "Resolution" > /tmp/dst_labeled_width.txt
  ae text | grep -x "Frame rate" > /tmp/dst_labeled_fps.txt
  xdotool mousemove 700 600; sleep 0.5
  import -window root '"$SHOTS"'/dst_03_form.png
  ae_activate "Cancel" "the form closes on Cancel"
  sleep 1
  kill $app 2>/dev/null; wait $app 2>/dev/null
' 2>&1 | tail -4

assert_showing /tmp/dst_picker.txt "Search destinations" "Export asks where first, in a searchable picker"
assert_showing /tmp/dst_picker.txt "Use this destination" "and that picker can be answered"
assert_rendered "$SHOTS/dst_01_picker.png" "the destination picker"
assert_rendered "$SHOTS/dst_02_search.png" "the picker after typing insta"
assert_showing /tmp/dst_form.txt "Destination" "the export form opens once a destination is chosen"
# The destination DICTATES the format, so the form states it as a fact instead of
# offering a control: asserting "Format is showing" would be asserting the bug this
# work removed. What the facts must carry is the consequence - Discord caps an upload
# at 20 MB - which is the destination's own data arriving through planExport(), so
# this checks the wiring and not the wording.
assert_hidden /tmp/dst_form.txt "Format" "the destination dictates the format, so there is no Format control"
if grep -q "20 MB" /tmp/dst_form_text.txt; then
    check 0 "and the facts state what the chosen destination requires (20 MB)"
else
    check 1 "and the facts state what the chosen destination requires (got: $(tr '\n' ' ' < /tmp/dst_form_text.txt 2>/dev/null | head -c 200))"
fi
# A CEILING IS NOT A DECISION. Discord's rule is "no bigger than 1280x720", so the
# resolution stays the user's, and a destination is not a reason to lose the Advanced
# section: what it merely DEFAULTS (audio, and the codec's own quality value) has to
# remain reachable, or the export cannot be finished.
assert_showing /tmp/dst_form.txt "Width" "a size ceiling leaves the resolution editable"
assert_showing /tmp/dst_form.txt "Height" "both of them"
assert_showing /tmp/dst_form.txt "Advanced" "and the disclosure stays available with a destination chosen"
# The expectation is the words ON SCREEN, which are not the accessible name:
# the fps box answers to "Frame rate" and is labelled "FPS:" on the form.
for pair in "Resolution:/tmp/dst_labeled_width.txt" "Frame rate:/tmp/dst_labeled_fps.txt"; do
    name=${pair%%:*}
    file=${pair#*:}
    if grep -qi "$name" "$file"; then
        check 0 "the $name field is labelled on screen by its own label"
    else
        check 1 "the $name field is labelled on screen by its own label (got: $(tr -d '\n' < "$file" 2>/dev/null))"
    fi
done
assert_rendered "$SHOTS/dst_03_form.png" "the export form carrying the chosen destination"
summary probe_dest
