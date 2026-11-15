#!/bin/bash
# A drag that starts on a scrubbing number field scrubs, and does NOT scroll the
# form (item 32 of docs/export-dialog-behavior.md, re-ruled 2026-10-08).
#
# Reported: "the scroll area in the export menu has lmb drag to move down/up, but the
# scrubbers themselves have drag left right and when dragging the scrubbers, the drag
# also propagates to the scroll area. This leads to the user accidentally scrolling
# while changing values."
#
# WHY THIS IS A PROBE AND NOT A UNIT TEST. The contract was first written as an
# offscreen assertion in tests/form_layout.cpp, and that harness CANNOT decide it:
# with the fix removed, the synthetic press-drag still scrubbed (1000 -> 1008) with
# contentY at 0, and the one "steal" signal it appeared to show - ScrubSpinBox's
# `scrubbing` going false - was the test reading the AeNumberField WRAPPER, which has
# no such property (a missing property reads as an invalid QVariant, i.e. false). A
# Flickable steals the mouse on the real pointer path, so the gate has to be a real
# pointer. See the note left in form_layout.cpp where that block used to be.
#
# WHAT IS ASSERTED, and why each half is needed:
#   * the WHEEL moves the form (and the reading tracks it, both ways) - without this
#     "the form did not scroll" could just be a dead reading;
#   * the drag on the field CHANGES ITS VALUE - the scrub still works;
#   * and the form did NOT move: two other rows' rectangles are in the same place
#     before and after, so a stolen gesture (which is what the report describes)
#     cannot hide as "the value changed anyway".
#
# Addressed BY NAME, never by pixel: the field is found through the accessibility
# tree, its rectangle comes from the application, and only the DRAG needs the dialog's
# window origin added (a QML item inside a QQuickWidget reports its rect in the
# widget's coordinates - the trap probe_scroll documents).
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/_lib.sh" "$@"
export RECORDED_AGAINST=2

GEOM=/tmp/probe_scrub_geom.txt
POS=/tmp/probe_scrub_pos.txt
rm -f "$GEOM" "$POS"

xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
  '"$APP"' "'"$PROJ"'" >/tmp/probe_scrub_app.log 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID
  # wait_ready also starts the memory ceiling (see ae_memory_cap): nothing this
  # probe runs may grow past it.
  if ! wait_ready 120; then
      echo "TIMEOUT waiting for the main window" > '"$GEOM"'
      kill $app 2>/dev/null; wait $app 2>/dev/null; exit 0
  fi
  W=$(xdotool getactivewindow 2>/dev/null || xdotool search --name AnimeEffects | tail -1)
  xdotool windowactivate --sync $W 2>/dev/null
  W_BEFORE=$(xdotool search --onlyvisible --name "." 2>/dev/null | sort | tr "\n" " ")

  ae_press "File" "the File menu opens"
  ae_press "Export project as" "the export entry is there"
  require_export_flow
  sleep 1.5

  DW=""
  for w in $(xdotool search --onlyvisible --name "." 2>/dev/null); do
    case " $W_BEFORE " in *" $w "*) ;; *) DW=$w ;; esac
  done
  if [ -z "$DW" ]; then
      echo "NO NEW WINDOW for the dialog (before: $W_BEFORE)" > '"$GEOM"'
      kill $app 2>/dev/null; wait $app 2>/dev/null; exit 0
  fi
  eval $(xdotool getwindowgeometry --shell "$DW" 2>/dev/null)
  OX=${X:-0}; OY=${Y:-0}

  # A destination whose form is long enough to scroll: the size/limits facts plus the
  # frame rows do not fit 560x640 (the shell floor the D25 measurements use).
  ae set "Search destinations" "discord video" >/dev/null
  sleep 1.2
  ae_activate "Continue" "a destination is chosen, so the form is the real one"
  sleep 1.5
  # Make the form long enough to scroll, from INSIDE the application: three more frame
  # ranges plus the Advanced section is the D25 worst case (measured 702px of content
  # in a 558px page). Resizing the window is not an option here - there is no window
  # manager under Xvfb and `xdotool windowsize` is refused (measured: X_ConfigureWindow
  # failed request), so the overflow has to come from the content.
  ae_activate "Advanced" "the Advanced section opens, adding its rows"
  sleep 1.0
  for i in 1 2 3; do
    ae_activate "Add range" "frame range $((i + 1)) is added"
    sleep 0.8
  done

  # The scrub target is a FRAME field: it is one row below the facts, so it is on
  # screen in this window without scrolling - and it is the same ScrubSpinBox the
  # report is about. The two rows whose rectangles stand in for the scroll position
  # are read from the same accessibility tree.
  TARGET="First frame 1"
  REF_A="Frame rate"
  REF_B="Destination"

  box=$(ae extents "$TARGET")
  refa=$(ae extents "$REF_A")
  refb=$(ae extents "$REF_B")
  if [ -z "$box" ] || [ -z "$refa" ] || [ -z "$refb" ]; then
      echo "NOT ADDRESSABLE target=[$box] refA=[$refa] refB=[$refb]" > '"$GEOM"'
      kill $app 2>/dev/null; wait $app 2>/dev/null; exit 0
  fi
  read -r SX SY SW SH <<< "$box"
  DX=$((SX + SW / 2 + OX))
  DY=$((SY + SH / 2 + OY))
  echo "dialog=${OX},${OY} target=$SX,$SY,${SW}x${SH} drag=$DX,$DY" > '"$GEOM"'

  value_of() { ae value "$1" 2>/dev/null | head -1; }
  y_of() { ae_y "$1"; }

  v0=$(value_of "$TARGET")
  a0=$(y_of "$REF_A"); b0=$(y_of "$REF_B")
  t0=$(y_of "$TARGET")

  # (a) the wheel scrolls the form, and the reading follows it - so "nothing moved"
  #     below cannot be a dead reading. Click 5 = wheel down, click 4 = wheel up.
  xdotool mousemove --sync $((OX + 280)) $((OY + 320)); sleep 0.3
  xdotool click 5; sleep 0.9
  a1=$(y_of "$REF_A"); b1=$(y_of "$REF_B")
  xdotool click 4; sleep 0.9
  a2=$(y_of "$REF_A"); b2=$(y_of "$REF_B")

  # (b) the real drag on the scrubbing field: sideways (what a scrub is) plus a
  #     vertical component, which is what a hand does and what used to tip the
  #     enclosing Flickable over its drag threshold.
  ae_drag $DX $DY 34 -64 10
  sleep 0.6

  v1=$(value_of "$TARGET")
  a3=$(y_of "$REF_A"); b3=$(y_of "$REF_B")
  t1=$(y_of "$TARGET")

  echo "v0=$v0 v1=$v1 t0=$t0 t1=$t1 a0=$a0 a1=$a1 a2=$a2 a3=$a3 b0=$b0 b1=$b1 b2=$b2 b3=$b3" > '"$POS"'
  xdotool mousemove $((OX + 280)) $((OY + 30)); sleep 0.4
  import -window root '"$SHOTS"'/scrub_01_after_drag.png
  kill $app 2>/dev/null; wait $app 2>/dev/null
' 2>&1 | tail -4

if grep -q TIMEOUT "$GEOM" 2>/dev/null; then
    check 1 "the application became ready ($(cat "$GEOM"))"
    summary probe_scrub
    exit 1
fi

eval "$(cat "$POS" 2>/dev/null)"
echo "geometry: $(cat "$GEOM" 2>/dev/null)"
echo "positions: $(cat "$POS" 2>/dev/null)"

if [ -z "${v1:-}" ]; then
    check 1 "the form was driven ($(cat "$GEOM" 2>/dev/null))"
    summary probe_scrub
    exit 1
fi

# The reading is alive: the wheel moved the form and coming back put it back.
if [ -n "${a1:-}" ] && [ -n "${a0:-}" ] && [ "$a1" != "$a0" ]; then
    check 0 "the wheel scrolls the form, so the scroll reading below is a live one"
else
    check 1 "the wheel scrolls the form (refA: a0=$a0 a1=$a1)"
fi
if [ -n "${a2:-}" ] && [ "$a2" = "$a0" ]; then
    check 0 "and the wheel back returns it to where it was (the reading tracks both ways)"
else
    check 1 "and the wheel back returns it (refA: a0=$a0 a2=$a2)"
fi

# The scrub itself: the drag changed the field's value.
if [ -n "${v0:-}" ] && [ "$v1" != "$v0" ]; then
    check 0 "the drag on the scrubbing field changed its value ($v0 -> $v1)"
else
    check 1 "the drag on the scrubbing field changed its value (got v0=$v0 v1=$v1 - a stolen gesture stops editing mid-drag)"
fi

# The report itself: nothing scrolled. Both reference rows AND the dragged field
# must be exactly where they were; a steal moves the content, which moves all three.
if [ "${a3:-}" = "$a2" ] && [ "${b3:-}" = "$b2" ] && [ "${t1:-}" = "$t0" ]; then
    check 0 "and the form did NOT scroll under it (a=$a2->$a3 b=$b2->$b3 target=$t0->$t1)"
else
    check 1 "and the form did NOT scroll under it (a=$a2->$a3 b=$b2->$b3 target=$t0->$t1): the drag propagated to the scroll area"
fi

assert_rendered "$SHOTS/scrub_01_after_drag.png" "the form after the drag"

summary probe_scrub
