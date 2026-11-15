#!/bin/bash
# See _lib.sh: these drive the real app under Xvfb because the wiring they check
# is not reachable from a unit test.
#
# The parameter form itself, reached the way the flow now reaches it: Export asks
# WHERE first, and the form that follows states what that destination requires. The
# destination is Telegram sticker, whose own declarations are unusually explicit
# (512 pixels on the long side, 256 KB, 3 seconds, 30 fps), so the facts box can be
# held to numbers that come from the data file rather than from this probe's idea of
# a sensible default.
#
# Two structural claims, both by NAME:
#   * The form is NOT a tab widget. It used to be one with a single tab ("General
#     Parameters"); it is one scrollable stack of sections now, which is why BOTH
#     section headings are on screen at once and why "General Parameters" - the old
#     tab title - is absent. A tab widget cannot show two pages at the same time, so
#     the pair of assertions is the shape of the form, not a wording preference.
#   * A destination that DICTATES the format has no Format control: asserting that
#     "Format is showing" would be asserting the bug this work removed (see
#     probe_dest, which makes the same point about Discord).
#
# The version this replaces clicked recorded pixels and asserted that the screenshot
# was not black, which is how it went on passing after the form was restructured.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/_lib.sh" "$@"
# The revision of the export flow this probe's navigation was recorded against. See
# AE_FLOW_REVISION in _lib.sh: the guard refuses to run a probe that predates it.
export RECORDED_AGAINST=2

rm -f /tmp/dlg_form.txt /tmp/dlg_text.txt /tmp/dlg_tree.txt /tmp/dlg_subtree.txt \
      /tmp/dlg_labeled_width.txt /tmp/dlg_labeled_fps.txt

xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
  # cd as its OWN command, not as `cd X && App &`: a background job whose body is a
  # compound command is a SUBSHELL, so $! would name the subshell instead of the
  # application and AE_UI_PID would match no accessible root.
  cd "$AE_PROBE_DIR"
  '"$APP"' "'"$PROJ"'" >/tmp/probe_dialog.log 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID   # the window the tooling must address
  sleep 12
  W=$(xdotool search --name "AnimeEffects" | tail -1)
  xdotool windowactivate --sync $W 2>/dev/null

  # 1. Export asks where first. The destination is named by its ID, not by its label:
  #    "Telegram" is one of several rows whose search text contains it, and a query
  #    that matches several answers with whichever the tree happens to list first.
  ae_press "File" "the File menu opens"
  ae_press "Export project as" "the export entry is there"
  require_export_flow
  ae set "Search destinations" "telegram.sticker" >/dev/null
  sleep 1.2
  ae_press "Use this destination" "the destination is accepted"

  # 2. The form, once the plan for that destination has been applied. Asking about
  #    "Destination" alone can answer with a snapshot of the form BEFORE the plan
  #    lands, so the disclosure is waited for first (see probe_dest).
  ae wait "Advanced" >/dev/null 2>&1 || true
  sleep 1
  # Every name asserted below is asked about HERE, in this one snapshot.
  ae_report /tmp/dlg_form.txt "Destination" "Width" "Height" "Frame rate" "Advanced" \
                              "Keep aspect ratio" "Export" "Cancel" "Quality" \
                              "What to export" "How to encode" "Format" "Codec" \
                              "General Parameters"
  # What the form SAYS, in words. The facts box is a label, so its own lines arrive
  # through the same query as every other label on screen.
  ae text > /tmp/dlg_text.txt 2>/dev/null
  # Which words on screen belong to which field: a control can answer to a name and
  # still have nothing beside it that reads as its label.
  ae labeled "Width" > /tmp/dlg_labeled_width.txt 2>/dev/null
  ae labeled "Frame rate" > /tmp/dlg_labeled_fps.txt 2>/dev/null
  # The accessibility tree itself, for the structural claim: the export dialog is one
  # node ("Export parameters"), and the subtree below it is what the user is looking at.
  ae dump > /tmp/dlg_tree.txt 2>/dev/null
  awk "/Export parameters/{found=1} found" /tmp/dlg_tree.txt > /tmp/dlg_subtree.txt
  xdotool mousemove 700 600; sleep 0.5
  import -window root '"$SHOTS"'/dlg_01_form.png
  ae_press "Cancel" "the form closes on Cancel"
  sleep 1
  kill $app 2>/dev/null; wait $app 2>/dev/null
' 2>&1 | tail -4

assert_showing /tmp/dlg_form.txt "Destination" "the form opens once a destination is chosen, stating where it goes"
# The destination's own numbers, through planExport(): Telegram sticker forces 512 on
# the long side, so the size boxes report it rather than offering the project's own.
assert_showing /tmp/dlg_form.txt "Width" "a destination that pins the size still shows it"
assert_showing /tmp/dlg_form.txt "Height" "both sides of it"
assert_showing /tmp/dlg_form.txt "Frame rate" "and the frame rate stays a control it merely caps"
assert_showing /tmp/dlg_form.txt "Advanced" "the disclosure holding what the destination leaves to the user"
assert_showing /tmp/dlg_form.txt "Keep aspect ratio" "and the rest of the size row"
# The codec is the destination's, but the trade INSIDE it is the user's, so the intent
# knob stays on the form instead of being stated as a fact.
assert_showing /tmp/dlg_form.txt "Quality" "the quality intent stays a control the user owns"
assert_showing /tmp/dlg_form.txt "Export" "the form can be answered"
assert_showing /tmp/dlg_form.txt "Cancel" "and abandoned"
# The stated facts, which are the destination's data arriving through the plan and not
# this probe's idea of a default.
for pair in "256 KB:its upload cap" \
            "Limits:the limits line, where a cap is stated as a cap" \
            "VP9:the format it resolved, by the name a person uses" \
            "WebM:the container it resolved"; do
    needle=${pair%%:*}
    what=${pair#*:}
    if grep -qF "$needle" /tmp/dlg_text.txt; then
        check 0 "the facts state $what"
    else
        check 1 "the facts state $what (no '$needle' in: $(grep -E 'x [0-9]+,|Limits|Codec' /tmp/dlg_text.txt | tr '\n' '|' | head -c 200))"
    fi
done
# The destination own size rule is maxExactSide 512, so 512 is the LONG side of the
# stated size - which side that is depends on the project's aspect, so what is asserted
# is the number the destination declares rather than a pair this probe worked out.
if grep -qE "(512 x [0-9]+|[0-9]+ x 512)" /tmp/dlg_text.txt; then
    check 0 "the facts state the size rule the destination declares (512 on the long side: $(grep -E '(512 x [0-9]+|[0-9]+ x 512)' /tmp/dlg_text.txt | head -1))"
else
    check 1 "the facts state the size rule the destination declares (got: $(grep -E 'x [0-9]+,' /tmp/dlg_text.txt | head -1))"
fi
# NOT A TAB WIDGET, in two independent ways:
#   * the old tab title is absent, and
#   * both section headings are on screen at the same time - a tab widget shows one
#     page at a time, so "What to export" and "How to encode" together is the shape.
assert_absent /tmp/dlg_form.txt "General Parameters" "the old single-tab page is gone, not renamed"
assert_showing /tmp/dlg_form.txt "What to export" "the form is one stack: the first section is on screen"
assert_showing /tmp/dlg_form.txt "How to encode" "and so is the second, which a tab widget could not show at once"
if grep -q "Export parameters" /tmp/dlg_subtree.txt && ! grep -q "page tab" /tmp/dlg_subtree.txt; then
    check 0 "and the export dialog subtree contains no tab widget at all"
else
    check 1 "and the export dialog subtree contains no tab widget at all (tabs: $(grep -c "page tab" /tmp/dlg_subtree.txt), subtree lines: $(wc -l < /tmp/dlg_subtree.txt))"
fi
# The destination dictates the format, so there is no Format control to disagree with
# the facts above it (the same assertion probe_dest makes about Discord).
assert_hidden /tmp/dlg_form.txt "Format" "the destination dictates the format, so there is no Format control"
assert_hidden /tmp/dlg_form.txt "Codec" "and no codec control beside it"
# The expectation is the words ON SCREEN, which are not the accessible name: the fps
# box answers to "Frame rate" and is labelled "FPS:" on the form.
for pair in "Width:/tmp/dlg_labeled_width.txt" "FPS:/tmp/dlg_labeled_fps.txt"; do
    name=${pair%%:*}
    file=${pair#*:}
    if grep -qi -- "$name" "$file"; then
        check 0 "the $name field is labelled on screen by its own label"
    else
        check 1 "the $name field is labelled on screen by its own label (got: $(tr -d '\n' < "$file" 2>/dev/null))"
    fi
done
assert_rendered "$SHOTS/dlg_01_form.png" "the parameter form for the chosen destination"
summary probe_dialog
