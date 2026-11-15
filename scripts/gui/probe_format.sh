#!/bin/bash
# See _lib.sh: these drive the real app under Xvfb because the wiring they check
# is not reachable from a unit test.
#
# This probe is about the MANUAL ESCAPE HATCH: the mode a user gets when no
# destination in the table describes what they are making, where the container
# and the codec are their own decision. It checks the two things that mode has to
# get right - the Format row is really there, and the Codec row answers about the
# container that was just chosen rather than about the one before it - and then
# that a still-image format is honest about itself: a PNG sequence offers Image
# quality (the writer setting it actually uses) and withdraws the frame rate (a
# directory of stills has no timing).
#
# The previous version of this probe drove the Format row by pixel and asserted
# that a screenshot was not black, so it passed while the export flow was
# restructured underneath it. Everything here addresses widgets BY NAME: a combo
# item is chosen by clicking the ITEM the accessibility tree names, so the item
# order in the popup is not recorded anywhere either.
#
# What the form SAYS it will write is the other half of the evidence, and it is the
# half that names the pairing: the facts label states the codec AND the container
# the form has actually resolved ("Codec: VP9 in WebM"), which is exactly what the
# Codec row has to be answering about after the Format row changed. The row itself
# is asserted as a control separately, through its own visibility.
#
# The BODY only navigates and leaves evidence. The assertions run out here.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/_lib.sh" "$@"
# The revision of the export flow this probe's navigation was recorded against. See
# AE_FLOW_REVISION in _lib.sh: the guard refuses to run a probe that predates it.
export RECORDED_AGAINST=2

rm -f /tmp/fmt_manual.txt /tmp/fmt_webm.txt /tmp/fmt_webm_facts.txt \
      /tmp/fmt_mp4.txt /tmp/fmt_mp4_facts.txt /tmp/fmt_seq.txt /tmp/fmt_seq_facts.txt

xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
  '"$APP"' "'"$PROJ"'" >/tmp/probe_app.log 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID   # the window the tooling must address
  sleep 12
  W=$(xdotool search --name "AnimeEffects" | tail -1)
  xdotool windowactivate --sync $W 2>/dev/null

  # 1. Export asks WHERE first. The escape hatch is one of the two answers the
  #    picker offers for "no destination in the table describes this": a search
  #    that matches nothing is answered as "no destination known", which is the
  #    manual mode this probe is about. The other answer is the button the picker
  #    draws for it, and that button is visible and named in the accessibility
  #    tree - but its name carries apostrophes, and an apostrophe inside this
  #    single-quoted body closes the quote silently, so this probe takes the answer
  #    that is addressable without one.
  ae_press "File" "the File menu opens"
  ae_press "Export project as" "the export entry is there"
  require_export_flow
  ae set "Search destinations" "zzz no such platform" >/dev/null
  sleep 1.2
  ae_press "Use this destination" "an unmatched search answers with the manual form"
  ae wait "Format" >/dev/null 2>&1 || true
  sleep 2
  ae_report /tmp/fmt_manual.txt "Destination" "Format" "Codec" "Quality" \
      "Frame rate" "Image quality"
  xdotool mousemove 700 600; sleep 0.5
  import -window root '"$SHOTS"'/fmt_01_manual.png

  # Choosing a format is choosing the ITEM the popup names, not the Nth row: the
  # row under a name is the only thing a reordered list cannot silently change.
  choose() {  # $1 = combo to open, $2 = the item to pick, $3 = what that means
    ae_press "$1" "$1 opens"
    sleep 1.2
    ae_press "$2" "$3"
    sleep 1.8
  }

  # 2. WebM: the codec row must answer with a WebM codec.
  choose "Format" "WebM" "WebM is chosen in the Format row"
  ae_report /tmp/fmt_webm.txt "Format" "Codec" "Quality" "Frame rate" "Image quality"
  ae text > /tmp/fmt_webm_facts.txt 2>/dev/null
  xdotool mousemove 700 600; sleep 0.5
  import -window root '"$SHOTS"'/fmt_02_webm.png

  # 3. MP4: a DIFFERENT container, so the codec row has to have changed with it.
  choose "Format" "MP4" "MP4 is chosen in the Format row"
  ae_report /tmp/fmt_mp4.txt "Format" "Codec" "Quality" "Frame rate" "Image quality"
  ae text > /tmp/fmt_mp4_facts.txt 2>/dev/null
  xdotool mousemove 700 600; sleep 0.5
  import -window root '"$SHOTS"'/fmt_03_mp4.png

  # 4. A still-image format: one PNG per frame, written by Qt image writer.
  choose "Format" "PNG sequence" "the PNG sequence format is chosen"
  ae_report /tmp/fmt_seq.txt "Format" "Codec" "Image quality" "Frame rate" "Quality" "Advanced"
  ae text > /tmp/fmt_seq_facts.txt 2>/dev/null
  xdotool mousemove 700 600; sleep 0.5
  import -window root '"$SHOTS"'/fmt_04_png_sequence.png
  ae_press "Cancel" "the form closes on Cancel"
  sleep 1
  kill $app 2>/dev/null; wait $app 2>/dev/null
' 2>&1 | tail -4

# ---- the escape hatch states the artifact -------------------------------------
assert_showing /tmp/fmt_manual.txt "Format" "with no destination the Format row is the users decision"
assert_showing /tmp/fmt_manual.txt "Codec" "and the Codec row is there beside it"

# ---- the codec row answers about the container that was chosen ----------------
# The pairing is what has to hold: a Codec row still reading the previous
# container codec would be the form answering a question about a format it is no
# longer making.
for pair in "WebM:/tmp/fmt_webm_facts.txt:Codec: VP9 in WebM" "MP4:/tmp/fmt_mp4_facts.txt:Codec: H.264 in MP4"; do
    container=${pair%%:*}
    rest=${pair#*:}
    file=${rest%%:*}
    text=${rest#*:}
    if grep -qF "$text" "$file"; then
        check 0 "choosing $container makes the codec row follow the container ($text)"
    else
        check 1 "choosing $container makes the codec row follow the container ($text) (got: $(grep -i codec "$file" 2>/dev/null | tr '\n' ' ' | head -c 200))"
    fi
done
if grep -qE "^Codec: [^ ]+ in " /tmp/fmt_mp4_facts.txt && ! grep -q "VP9" /tmp/fmt_mp4_facts.txt; then
    check 0 "and the MP4 form no longer names the WebM codec"
else
    check 1 "and the MP4 form no longer names the WebM codec (got: $(grep -i codec /tmp/fmt_mp4_facts.txt 2>/dev/null | tr '\n' ' ' | head -c 200))"
fi
assert_showing /tmp/fmt_webm.txt "Codec" "WebM carries more than one codec, so that row is a real choice there"

# ---- a still-image format is honest about what it can use --------------------
if grep -qE "^PNG sequence of [0-9]+ frames" /tmp/fmt_seq_facts.txt; then
    check 0 "the PNG sequence format is the one selected, and the form says what it will write ($(grep -E "^PNG sequence of [0-9]+ frames" /tmp/fmt_seq_facts.txt | head -1))"
else
    check 1 "the PNG sequence format is the one selected, and the form says what it will write (got: $(tr '\n' ' ' < /tmp/fmt_seq_facts.txt 2>/dev/null | head -c 200))"
fi
assert_showing /tmp/fmt_seq.txt "Image quality" "a sequence of stills offers Image quality, the writer own setting"
assert_hidden /tmp/fmt_seq.txt "Frame rate" "and withdraws the frame rate, which a directory of stills has not got"
assert_hidden /tmp/fmt_seq.txt "Codec" "and the Codec row, since Qt writes these files rather than a video encoder"
assert_rendered "$SHOTS/fmt_01_manual.png" "the escape hatch"
assert_rendered "$SHOTS/fmt_02_webm.png" "the escape hatch stating a WebM artifact"
assert_rendered "$SHOTS/fmt_03_mp4.png" "and an MP4 one"
assert_rendered "$SHOTS/fmt_04_png_sequence.png" "a still-image format on the same form"

summary probe_format
