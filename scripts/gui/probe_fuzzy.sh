#!/bin/bash
# See _lib.sh: these drive the real app under Xvfb because the wiring they check
# is not reachable from a unit test.
#
# This probe is about the SEARCH in the destination picker: it has to answer a query
# a person actually types, not only one the table happens to contain verbatim. Nobody
# types "discord.emoji"; they type "telegrm" and miss a key, or "discordemoji" because
# the space is the awkward part. The search is therefore fuzzy, and the only honest
# witness of that is end to end: type an imprecise query, accept the default answer,
# and read back WHICH destination the form opened for. A unit test can check a score;
# only this can check that the row the user gets is the row the label states.
#
# HOW IT FAILS ON THE PRE-CHANGE CODE. The search used to be a plain case-insensitive
# substring test, so "telegrm" is a substring of no label and matches NO row: the
# picker's current row stays -1, activating "Continue" opens nothing, and
# the form never appears at all. Both reads of the form's "Destination" row then come
# back EMPTY, and the assertions below are about that label - an empty file is a loud
# FAIL, not a pass on a screenshot that merely is not black (probe_dest's lesson: it
# spent a rewrite asserting exactly that while the flow it navigated was gone).
#
# The BODY only navigates and leaves evidence (screenshots, the label files). The
# assertions run out here, where the helpers and the failure count live.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/_lib.sh" "$@"
# The revision of the export flow this probe's navigation was recorded against. See
# AE_FLOW_REVISION in _lib.sh: the guard refuses to run a probe that predates it.
export RECORDED_AGAINST=2

rm -f /tmp/fz_picker.txt /tmp/fz_label1.txt /tmp/fz_label2.txt

xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
  '"$APP"' "'"$PROJ"'" >/tmp/probe_app.log 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID   # the window the tooling must address
  wait_ready 120 || sleep 12
  W=$(xdotool search --name "AnimeEffects" | tail -1)
  xdotool windowactivate --sync $W 2>/dev/null

  # 1. Export asks WHERE first, and the picker is the screen this probe answers on.
  ae_press "File" "the File menu opens"
  ae_press "Export project as" "the export entry is there"
  require_export_flow
  ae_report /tmp/fz_picker.txt "Search destinations" "Continue"

  # 2. PASS 1, A TYPO. "telegrm" is a substring of nothing in the table, so the old
  #    substring search narrowed the list to zero rows and left the current row at -1.
  #    A fuzzy search has to land on a Telegram row; both of them ("Telegram sticker",
  #    "Telegram custom emoji") contain the word, so the assertion names the word and
  #    not the row.
  ae set "Search destinations" "telegrm" >/dev/null
  sleep 1.2
  xdotool mousemove 700 600; sleep 0.5
  import -window root '"$SHOTS"'/fz_01_search.png

  # 3. The default button answers with that row, and the form states WHICH row it was
  #    given: `ae value "Destination"` prints the LABEL of the destination that was
  #    chosen (measured: "A video for a chat app" when the picker was left on the
  #    floor row).
  #    The read is retried, because a query against an application that is still
  #    building the plan can come back empty - and an empty file here would be read as
  #    "the form stated nothing", a failure of the READ reported as a failure of the
  #    application. The assertions are about the fact, so the fact has to arrive first.
  ae_activate "Continue" "the typo is answered with a destination"
  ae wait "Destination" >/dev/null 2>&1 || true
  sleep 1
  for i in 1 2 3 4 5 6 7 8; do
    ae value "Destination" > /tmp/fz_label1.txt 2>/dev/null
    [ -s /tmp/fz_label1.txt ] && break
    sleep 1
  done
  xdotool mousemove 700 600; sleep 0.5
  import -window root '"$SHOTS"'/fz_02_dest.png

  # 4. PASS 2, AN UNSPACED QUERY, in the same session: the destination row on the
  #    form reopens the picker (probe_axes navigates exactly this way). The query is
  #    "discordemoji" - the two words of the table id discord.emoji (label "Discord
  #    emoji") run together, which is what a person types when the space is the
  #    awkward part. It is a substring of neither the id nor the label, so the old
  #    search matched nothing again; a fuzzy search has to pick that one row out of
  #    the three Discord destinations.
  ae_activate "Destination" "the destination row reopens the picker"
  sleep 1.5
  ae set "Search destinations" "discordemoji" >/dev/null
  sleep 1.2
  ae_activate "Continue" "the unspaced query is answered with a destination"
  ae wait "Destination" >/dev/null 2>&1 || true
  sleep 1
  for i in 1 2 3 4 5 6 7 8; do
    ae value "Destination" > /tmp/fz_label2.txt 2>/dev/null
    [ -s /tmp/fz_label2.txt ] && break
    sleep 1
  done
  ae_activate "Cancel" "the form closes on Cancel"
  sleep 1
  kill $app 2>/dev/null; wait $app 2>/dev/null
' 2>&1 | tail -4

assert_showing /tmp/fz_picker.txt "Search destinations" "Export asks where first, in a searchable picker"
assert_showing /tmp/fz_picker.txt "Continue" "and that picker can be answered"
assert_rendered "$SHOTS/fz_01_search.png" "the picker after an imprecise query"
# The typo has to reach a Telegram row. Which of the two is the fuzzy score's business;
# what the user needs is that the word they typed - misspelled as it is - lands on the
# platform they meant, and the LABEL is where that arrives.
if grep -q Telegram /tmp/fz_label1.txt 2>/dev/null; then
    check 0 "the typo 'telegrm' is answered by a Telegram destination"
else
    check 1 "the typo 'telegrm' is answered by a Telegram destination (got: $(tr -d '\n' < /tmp/fz_label1.txt 2>/dev/null))"
fi
assert_rendered "$SHOTS/fz_02_dest.png" "the form the typo led to"
# The unspaced query names ONE row out of the three Discord destinations, and the answer
# is compared against that row's own label= from assets/export/ExportTargets.txt
# (id="discord.emoji" label="Discord emoji"), read whole rather than grep'd, so a row
# that merely contains the words cannot stand in for it.
got2=$(tr -d '\n' < /tmp/fz_label2.txt 2>/dev/null)
if [ "$got2" = "Discord emoji" ]; then
    check 0 "the unspaced query 'discordemoji' is answered by discord.emoji"
else
    check 1 "the unspaced query 'discordemoji' is answered by discord.emoji (got: $got2)"
fi
summary probe_fuzzy
