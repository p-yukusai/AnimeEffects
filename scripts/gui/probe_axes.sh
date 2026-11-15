#!/bin/bash
# See _lib.sh: these drive the real app under Xvfb because the wiring they check
# is not reachable from a unit test.
#
# This probe is about the CAPABILITY AXES. A destination is not a format hint, it
# is a set of facts about the artifact (does it carry alpha, may it carry audio,
# who decides the container), and the form has to state those facts and withdraw
# the controls they settle. The previous version of this probe clicked recorded
# pixels in the old parameter-first flow and asserted that its screenshot was not
# black, so it kept passing while driving widgets that no longer existed and
# testing nothing at all. Everything below addresses widgets BY NAME.
#
# Telegram sticker is the destination whose axes are ALL dictated by the table:
# alpha="required" audio="forbidden" class="fixed" codecSelect="fixed". The two
# halves of the probe are therefore the two answers the axes can give - stated as
# a fact with no control (the sticker), and handed to the user (manual mode).
#
# The BODY only navigates and leaves evidence (reports, the facts text, the
# screenshots). Every assertion runs out here, where the helpers and the failure
# count live.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/_lib.sh" "$@"
# The revision of the export flow this probe's navigation was recorded against. See
# AE_FLOW_REVISION in _lib.sh: the guard refuses to run a probe that predates it.
export RECORDED_AGAINST=2

rm -f /tmp/ax_sticker.txt /tmp/ax_sticker_facts.txt /tmp/ax_manual.txt \
      /tmp/ax_manual_facts.txt /tmp/ax_manual_format.txt

xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
  '"$APP"' "'"$PROJ"'" >/tmp/probe_app.log 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID   # the window the tooling must address
  sleep 12
  W=$(xdotool search --name "AnimeEffects" | tail -1)
  xdotool windowactivate --sync $W 2>/dev/null

  # 1. Export asks WHERE first, and the answer is a Telegram sticker: the
  #    destination whose every axis is in the table.
  ae_press "File" "the File menu opens"
  ae_press "Export project as" "the export entry is there"
  require_export_flow
  ae set "Search destinations" "telegram sticker" >/dev/null
  sleep 1.2
  ae_press "Use this destination" "the Telegram sticker destination is chosen"
  # The form builds itself per destination; wait for it rather than for a fixed
  # number of seconds, so the report cannot describe a half-applied plan.
  ae wait "Advanced" >/dev/null 2>&1 || true
  sleep 1.5
  # The facts are a LABEL, so the words themselves are evidence: ae text prints
  # every label, and the outer script greps what it must say.
  ae text > /tmp/ax_sticker_facts.txt 2>/dev/null
  ae_report /tmp/ax_sticker.txt "Destination" "Format" "Codec" "Export with transparency" \
      "Export with audio" "Quality" "Width" "Height" "Advanced"
  xdotool mousemove 700 600; sleep 0.5
  import -window root '"$SHOTS"'/ax_01_sticker.png

  # 2. The escape hatch, through the form own destination row. A search that
  #    matches nothing is answered as "no destination known", which is manual
  #    mode: the same dialog with the format handed back to the user.
  ae_press "Destination" "the destination row reopens the picker"
  sleep 1.5
  ae set "Search destinations" "zzz no such platform" >/dev/null
  sleep 1.2
  ae_press "Use this destination" "an unmatched search answers with the manual form"
  ae wait "Format" >/dev/null 2>&1 || true
  sleep 2
  ae text > /tmp/ax_manual_facts.txt 2>/dev/null
  # What the Format row states, so the facts can be read against the CONTROL rather
  # than against a container this probe recorded. `ae value` answers for a QComboBox
  # again: with no Value or EditableText interface it falls back to the node name, which
  # for a combo IS its current value.
  ae value "Format" > /tmp/ax_manual_format.txt 2>/dev/null
  ae_report /tmp/ax_manual.txt "Destination" "Format" "Codec" "Quality" "Width" "Height"
  xdotool mousemove 700 600; sleep 0.5
  import -window root '"$SHOTS"'/ax_02_manual.png
  ae_press "Cancel" "the form closes on Cancel"
  sleep 1
  kill $app 2>/dev/null; wait $app 2>/dev/null
' 2>&1 | tail -4

# ---- the axes it dictates, as facts and as withdrawn controls ----------------
assert_showing /tmp/ax_sticker.txt "Destination" "the form states the destination it is working under"
# The words, not the widgets: alpha is REQUIRED, and the destination says so
# itself rather than leaving the user to infer it from a frozen check box.
if grep -q "Transparency is required by this destination" /tmp/ax_sticker_facts.txt; then
    check 0 "the facts state that transparency is required"
else
    check 1 "the facts state that transparency is required (got: $(grep -i transparency /tmp/ax_sticker_facts.txt | tr '\n' ' ' | head -c 200))"
fi
if grep -q "Audio: not accepted by this destination" /tmp/ax_sticker_facts.txt; then
    check 0 "the facts state that audio is not accepted"
else
    check 1 "the facts state that audio is not accepted (got: $(grep -i audio /tmp/ax_sticker_facts.txt | tr '\n' ' ' | head -c 200))"
fi
# The axis the destination DECIDES is not offered as a control: a Format combo
# beside a stated "VP9 in webm" is two answers to one question.
assert_hidden /tmp/ax_sticker.txt "Format" "the destination dictates the format, so there is no Format control"
assert_hidden /tmp/ax_sticker.txt "Codec" "and no Codec control either"
# Dictating the artifact is not a reason to lose the disclosure: what the
# destination merely DEFAULTS (its own audio pre-set, the codec s quality value)
# has to stay reachable, or the export cannot be finished.
assert_showing /tmp/ax_sticker.txt "Advanced" "and what it only defaults stays reachable under Advanced"
assert_rendered "$SHOTS/ax_01_sticker.png" "the form for a destination with every axis dictated"

# ---- and the escape hatch hands the artifact back to the user ----------------
assert_showing /tmp/ax_manual.txt "Format" "the escape hatch offers the Format row"
assert_showing /tmp/ax_manual.txt "Codec" "and the Codec row beside it"
# The rows are only half of it: the facts have to name the container the Format row
# is actually showing, which is a cross-check between a control and the words beside
# it rather than a pattern that any "Codec: X in Y" line would satisfy. The form hands
# back the container it was carrying when the destination went away (WebM here, since
# the sticker had set it), so the container itself is read from the row instead of
# being hard-coded from an earlier run.
manualFormat=$(tr -d '\n' < /tmp/ax_manual_format.txt 2>/dev/null)
if [ -n "$manualFormat" ] && grep -qF "in $manualFormat" /tmp/ax_manual_facts.txt; then
    check 0 "and the facts name the container the Format row states ($manualFormat)"
else
    check 1 "and the facts name the container the Format row states (row said '$manualFormat', facts said: $(grep -i codec /tmp/ax_manual_facts.txt 2>/dev/null | tr '\n' ' ' | head -c 200))"
fi
assert_rendered "$SHOTS/ax_02_manual.png" "the same form in manual mode"

summary probe_axes
