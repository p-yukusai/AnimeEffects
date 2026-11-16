#!/bin/bash
# A box-selection marquee in the timeline must not survive the mouse button
# release (reported 2026-10-11).
#
# TWO DEFECTS, ONE SYMPTOM, both confirmed with a real pointer under Xvfb:
#   1. gui::TimeLineWidget runs the pan itself and its release test was
#          mIsPanning && (button == Middle || button == Left)
#      so a left press arriving while a middle-drag pan was in flight - which the
#      editor DOES see, because that press is not a pan press - had its release
#      eaten by the pan branch. The editor stayed in State_EncloseKeys with an
#      active Marquee, so its dashed box was redrawn on every repaint and kept
#      following the pointer. The fix remembers the button that started the pan.
#   2. the editor's own release path (ctrl::TimeLineEditor::updateCursor,
#      State_EncloseKeys) cleared the state but asked for NO view update, while
#      the drag had repainted the box on every move. The box's pixels therefore
#      stayed on the surface until something unrelated repainted it - the plain
#      marquee case of the report, and the reason the symptom is "sometimes".
#      The fix sets UpdateFlag_ModView on that release.
#
# WHY THIS IS A PROBE AND NOT A UNIT TEST. The defect lives in which Qt button
# press/release pairs reach the editor while another button is held down, through
# the implicit grab and two widgets' event handlers, and in whether a repaint is
# requested. Only a real pointer produces that sequence.
#
# HOW THE BOX IS MEASURED:
#   * each gesture's screenshot is compared against marquee_04, the state after
#     an ordinary marquee AND a forced repaint - the same marquee (same
#     selection) with no box. Comparing against a plain click instead would
#     confuse the marquee's selection highlight with the box.
#   * the shot right after the release, before any repaint, is the assertion for
#     defect 2; a shot after a forced repaint proves the state is not stuck
#     (defect 1). The repaint is a net-zero horizontal scroll: Right then Left,
#     24px each via TimeLineWidget::keyPressEvent, so the view returns to its
#     exact offset - measured to change not one pixel on its own.
#   * the box drawn mid-drag is asserted too, so a pass cannot be an empty
#     comparison (a press that landed on a key draws no box and fails loudly).
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/_lib.sh" "$@"

rm -f /tmp/tlm_geom.txt

xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
  cd "$AE_PROBE_DIR"
  "$APP" "'"$PROJ"'" >/tmp/probe_timeline_marquee.log 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID
  if ! wait_ready 120; then
      echo "TIMEOUT waiting for the main window" > /tmp/tlm_geom.txt
      kill $app 2>/dev/null; wait $app 2>/dev/null; exit 0
  fi
  W=$(xdotool getactivewindow 2>/dev/null || xdotool search --name AnimeEffects | tail -1)
  xdotool windowactivate --sync $W 2>/dev/null
  sleep 0.5

  # The timeline track has no accessible name, so its rectangle is derived from
  # the Animation panel it sits in. The offsets are the panel-local ones for the
  # object tree on the left (72px), the panel title plus the ruler above the lanes
  # (37px, measured) plus the dock header bar above the ruler (40px), and the
  # horizontal scrollbar strip below the lanes (13px). There is no offset on the
  # right: the transport is no longer a 32px column beside the splitter but the
  # header's own left group, so the timeline reaches the panel's right edge.
  P=$(ae extents "Animation")
  if [ -z "$P" ]; then
      echo "NO GEOMETRY: no visible widget named Animation" > /tmp/tlm_geom.txt
      kill $app 2>/dev/null; wait $app 2>/dev/null; exit 0
  fi
  read -r PX PY PW PH <<< "$P"
  TX0=$((PX + 72)); TX1=$((PX + PW))
  TY0=$((PY + 37 + 40)); TY1=$((PY + PH - 13))
  CW=$((TX1 - TX0)); CH=$((TY1 - TY0))
  # Track-local press/drag points, in the empty lanes below the top row.
  AX=$((TX0 + CW / 4));  AY=$((TY1 - 110))
  BX=$((TX0 + CW * 4 / 5)); BY=$((TY1 - 30))
  echo "TX0=$TX0 TY0=$TY0 CROP=${CW}x${CH}+${TX0}+${TY0} A=$AX,$AY B=$BX,$BY" > /tmp/tlm_geom.txt

  # The forced repaint: a net-zero horizontal scroll, so only a redraw differs.
  repaint() { xdotool key --clearmodifiers Right; sleep 0.25; xdotool key --clearmodifiers Left; sleep 0.5; }

  # Press at A, drag to B in steps (a real drag, not a click), keep the pointer
  # at B. The steps matter: one move plus a release is a click, which starts no
  # box at all.
  marquee() {
    xdotool mousemove --sync $AX $AY; sleep 0.3
    xdotool mousedown 1; sleep 0.2
    for i in 1 2 3 4 5 6; do
      xdotool mousemove --sync $((AX + (BX - AX) * i / 6)) $((AY + (BY - AY) * i / 6)); sleep 0.05
    done
    sleep 0.35
  }

  # 1. the clean track: a plain click on empty space ends any gesture and clears
  #    the selection, then the pointer rests at B.
  xdotool mousemove --sync $BX $BY; sleep 0.3
  xdotool mousedown 1; sleep 0.2; xdotool mouseup 1; sleep 0.7
  import -window root "'"$SHOTS"'/marquee_01_clean.png"

  # 2. an ordinary left-drag marquee: the box is up while the button is held...
  marquee
  import -window root "'"$SHOTS"'/marquee_02_box_drawn.png"

  # 3. ...and the release itself must take it down, with no repaint in between.
  xdotool mouseup 1; sleep 0.7
  import -window root "'"$SHOTS"'/marquee_03_plain_released.png"

  # 4. the reference the other shots are compared against: the same marquee after
  #    a forced repaint, i.e. the correct final state - no box, selection kept.
  repaint
  import -window root "'"$SHOTS"'/marquee_04_plain_repaint.png"

  # 5. the bug: a left-drag marquee with a MIDDLE press arriving while the left
  #    button is still down, then the left release (which the pan branch used to
  #    eat) and the middle release.
  marquee
  xdotool mousedown 2; sleep 0.3
  xdotool mouseup 1; sleep 0.5
  xdotool mouseup 2; sleep 0.6
  import -window root "'"$SHOTS"'/marquee_05_bug_released.png"

  # 6. ...and the repaint that used to redraw the box for as long as the stuck
  #    state lived.
  repaint
  import -window root "'"$SHOTS"'/marquee_06_bug_repaint.png"

  # 7. the same interference in the reported order: the MIDDLE press first, then
  #    a left press the editor does see. That left marquee never grows while the
  #    pan is in flight (moves pan instead), so the eaten release paints nothing
  #    by itself - but the cursor still reports the left button as held, so the
  #    NEXT pointer move grows a box out of the press point and paints it.
  xdotool mousemove --sync $AX $AY; sleep 0.3
  xdotool mousedown 2; sleep 0.25
  xdotool mousedown 1; sleep 0.3
  xdotool mouseup 1; sleep 0.4
  xdotool mouseup 2; sleep 0.5
  xdotool mousemove --sync $BX $BY; sleep 0.7
  import -window root "'"$SHOTS"'/marquee_07_after_release.png"

  # 8. the pan branch must still pan: a middle drag moves the view, so the track
  #    pixels change. Done last - it leaves the view moved.
  xdotool mousemove --sync $((TX0 + 60)) $((TY1 - 60)); sleep 0.3
  xdotool mousedown 2; sleep 0.2
  for i in 1 2 3 4 5; do xdotool mousemove --sync $((TX0 + 60 + 24 * i)) $((TY1 - 60)); sleep 0.05; done
  sleep 0.3
  xdotool mouseup 2; sleep 0.6
  import -window root "'"$SHOTS"'/marquee_08_pan.png"

  kill $app 2>/dev/null; wait $app 2>/dev/null
' 2>&1 | tail -4

if grep -q TIMEOUT /tmp/tlm_geom.txt 2>/dev/null; then
    check 1 "the application reached its main window"
    summary probe_timeline_marquee
fi
if grep -q "NO GEOMETRY" /tmp/tlm_geom.txt 2>/dev/null; then
    check 1 "the timeline was locatable (no visible Animation panel)"
    summary probe_timeline_marquee
fi

eval "$(cat /tmp/tlm_geom.txt 2>/dev/null)"
if [ -z "${CROP:-}" ]; then
    check 1 "the timeline geometry was readable"
    summary probe_timeline_marquee
fi
echo "geometry: $(cat /tmp/tlm_geom.txt 2>/dev/null)"

# Differing pixels between two shots, over the track only, ignoring the <=3%
# fuzz of antialiasing. The marquee's dashed accent edge and its tinted fill are
# thousands of pixels apart from the background; an identical render is 0.
crop_diff() { # crop_diff <a.png> <b.png>
    magick "$1" -crop "$CROP" +repage /tmp/tlm_a.png 2>/dev/null
    magick "$2" -crop "$CROP" +repage /tmp/tlm_b.png 2>/dev/null
    magick compare -metric AE -fuzz 3% /tmp/tlm_a.png /tmp/tlm_b.png null: 2>&1 | awk '{print $1+0}'
}
# A stale box measures ~12000 differing pixels over this crop; the same render
# measures 0. The gap is three orders of magnitude, so 100 is a threshold no
# rounding reaches.
BOX_PIXELS=100

d=$(crop_diff "$SHOTS/marquee_01_clean.png" "$SHOTS/marquee_02_box_drawn.png")
[ "$d" -gt "$BOX_PIXELS" ]; check $? "the left drag painted the marquee box ($d px)"

d=$(crop_diff "$SHOTS/marquee_03_plain_released.png" "$SHOTS/marquee_04_plain_repaint.png")
[ "$d" -le "$BOX_PIXELS" ]; check $? "an ordinary release takes the box down, no repaint needed ($d px)"

d=$(crop_diff "$SHOTS/marquee_04_plain_repaint.png" "$SHOTS/marquee_05_bug_released.png")
[ "$d" -le "$BOX_PIXELS" ]; check $? "the pan-interleaved release takes it down too ($d px)"

d=$(crop_diff "$SHOTS/marquee_04_plain_repaint.png" "$SHOTS/marquee_06_bug_repaint.png")
[ "$d" -le "$BOX_PIXELS" ]; check $? "and no repaint brings it back, state not stuck ($d px)"

d=$(crop_diff "$SHOTS/marquee_04_plain_repaint.png" "$SHOTS/marquee_07_after_release.png")
[ "$d" -le "$BOX_PIXELS" ]; check $? "with the middle press first, no box follows the pointer ($d px)"

d=$(crop_diff "$SHOTS/marquee_04_plain_repaint.png" "$SHOTS/marquee_08_pan.png")
[ "$d" -gt "$BOX_PIXELS" ]; check $? "a middle-drag still pans the timeline ($d px)"

assert_rendered "$SHOTS/marquee_02_box_drawn.png" "the timeline with the marquee box"
assert_rendered "$SHOTS/marquee_05_bug_released.png" "the timeline after the pan-interleaved marquee"
summary probe_timeline_marquee
