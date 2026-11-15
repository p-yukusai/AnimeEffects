#!/bin/bash
# THE TIMELINE'S OWN INK, ON SCREEN: the selected row's rail, its lane tint, the focused
# key's ring and fill, the lane label, and the ruler number the playhead's pill owns.
#
# WHAT SHAPE THE REDESIGN PUT THE TIMELINE IN (dev/graph-timeline-redesign, sections 1 and 3),
# and the measurement each assertion rests on:
#
#   1. A SELECTED OBJECT IS ONE TREATMENT ACROSS TWO COLUMNS.
#      * The lane band is the accent at 0.06 - "a whisper, not a flood" - where it used to
#        be the accent at 0.40. Measured as a COLOUR, not as a change: the row's own rail
#        gives the accent A, the band area below the lanes gives the surface B, and the
#        band must equal A composited at 0.06 over B, with the 0.40 composite at least
#        three times further away than the 0.06 one. No theme constant is baked in: A is
#        read off the rail the application draws, B off the surface it fills with.
#      * The 2px accent-bright RAIL is the OBJECT TREE's, at the row's left edge, spanning
#        the row's height (ObjectTreeWidget's own paint; the lane carries only the tint).
#        Measured as a solid column: at least row_height - 3 pixels of ONE chromatic colour
#        in a column inside the row's rectangle, no wider than 3px, and NOTHING of the kind
#        at the same row the moment another row is selected.
#
#   2. THE FOCUSED KEY'S RING, and the fill that ring is separate ink from. A key selected
#      in the timeline keeps its own type colour, mixed toward the keyframe highlight (the
#      mock's mixWhite at 0.45, derived here from the hue the UNSELECTED marker measured),
#      and gains a 1px accent ring 2.6px clear of the marker. Measured as an annulus 5..8px
#      about the marker's own centre: accent-coloured with the selection, the lane's colour
#      without it, in both directions, on the same geometry.
#
#   3. A LANE'S CHANNEL LABEL IS ITS CHANNEL'S HUE AT 0.42 - one step quieter than the keys
#      - and it is drawn WHATEVER THE PLAYHEAD DOES. A lane's name is not the ruler's number:
#      the pill carries the frame in its own column, which is why the RULER blanks its number
#      there (item 4), but nothing carries the lane's name, so the label stays put and the
#      1px playhead line simply crosses the text. Measured by counting the pixels of ONE
#      predicted colour in the label's own box (the box the renderer centres on the track):
#      the 0.42 composite of the key hue the lane's own markers gave, which is a colour no
#      full-strength key and no surface can be (they are 0.58 of the hue's distance away from
#      it). Counted with the playhead clear of the box AND with the playhead parked on the
#      box's centre: the label must be present in BOTH states, and both counts are printed.
#
#   4. THE RULER'S NUMBER YIELDS TO THE PILL. The number under the playhead is not drawn;
#      its neighbours are. Measured on a box found IN THE PICTURE (an ink run in the ruler's
#      number row): the playhead is parked on that run's own tick with a click on the ruler,
#      then moved 90px away - the box holds ink, then none, then ink.
#
# WHERE EVERY NUMBER COMES FROM. The track's rectangle is derived the way
# probe_timeline_marquee.sh derives it, which is the measured one: the Animation dock's own
# AT-SPI rectangle, plus the object tree column on the left (72), the dock header bar above
# the ruler (40), the ruler together with the panel title (37) and the horizontal scrollbar
# strip below the lanes (13) - and NOTHING on the right, because the transport controls are
# the header's own group now, so the timeline reaches the panel's right edge. The ruler's
# height and its number row are
# the application's own constants (ctrl::TimeLineEditor::kHeaderHeight 22, kNumberTop 1,
# kNumberHeight 14). The theme's colours are measured off the running window (the rail, the
# lane, the marker, the surface), never restated from the palette. Widgets are addressed by
# name (ae_ui.py), and the row menu that adds the keys is driven the way this suite's own
# graph probes drive the same kind of menu: the popup is a top-level window that did not
# exist before, focused by xdotool, its entries reached with Up.
#
# WHICH PROJECT THIS NEEDS. Any .anie with at least TWO object rows: the rail and the tint
# are asserted on one row against the other, and the KEYS are created by the probe itself
# (the row menu's own "Add default keys", which knocks Move/Rotate/Scale/Depth/Opa/HSV at
# frame 0 on the row it is opened on), so a project without keys still exercises the ring
# and the label. A row whose node carries no timeLine leaves the lane with nothing to
# measure, and the probe then fails loudly naming that instead of asserting an empty box.
#
# WHAT THIS PROBE DOES NOT COVER: the graph panel (probe_graph_ink.sh), and the pill's own
# size - its half-width is measured by the application (QFontMetrics); this probe asserts
# only WHICH column it owns.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/_lib.sh" "$@"

GEOM=/tmp/probe_timeline_ink.txt
SCRATCH=/tmp/probe_timeline_ink
INK="$ROOT/scripts/gui/ae_ink.py"
rm -f "$GEOM"
rm -rf "$SCRATCH"; mkdir -p "$SCRATCH"

# ---- the numbers the readings are taken against ----
# A rail column is solid: it may lose only this many pixels of the row's height, so a glyph
# stem (about two thirds of a 22px row) can never be mistaken for it.
RAIL_SLACK=3
# The 0.06 tint of the accent over the surface is a small but exact change. The band is
# asserted BOTH as "within TINT_TOL of the 0.06 composite" and as "no more than TINT_SHARE
# of the accent's own distance from the surface" - the second makes this a statement about
# 0.06 rather than 0.40: the flood is 6.7x the whisper, so a band at the old value is 2.7x
# past the share bound.
TINT_TOL=6
TINT_SHARE=0.25
# The ring is drawn at 0.85 over the band, but it is a 1px ARC: MEASURED, its densest pixels
# land 50..55 from the accent (a full-coverage 0.85 blend would be 38, so the arc never quite
# reaches it) while the marker's own fill is 86 away and the label ink 90. 75 admits the whole
# arc and keeps the marker, the label and the band out.
RING_TOL=75
# ...and its bar is the arc's own count, MEASURED at 20-25 pixels on this build.
RING_MIN=14
# A key marker's fill is the type hue at FULL strength: its distance from the band is 1.0 of
# the hue's, while the label's 0.42 ink is 0.42 of it. 150 sits between them for every hue
# in the palette (the strongest label ink measured here is about 110).
MARKER_MIN_INK=150
# The label ink is a 0.42 composite of the channel hue over the band; a full-strength key or
# a surface is 0.58 of the hue's distance away from it, so 12 pins the alpha to +-0.05.
LABEL_TOL=12
# A drawn label and a drawn frame number are text runs: a handful of glyphs is dozens of
# pixels, so 8 cannot be antialiasing.
TEXT_MIN=8
export RAIL_SLACK TINT_TOL TINT_SHARE RING_TOL RING_MIN MARKER_MIN_INK LABEL_TOL TEXT_MIN GEOM SCRATCH INK

xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
  set -u
  cd "$AE_PROBE_DIR"
  "$APP" "${PROJ:-}" >/tmp/probe_timeline_ink_app.log 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID

  abort() { printf "ABORT=\"%s\"\n" "$1" >> "$GEOM"; kill $app 2>/dev/null; wait $app 2>/dev/null; exit 0; }
  # EVERY STEP IS RECORDED AS IT HAPPENS. The body runs in a child shell whose stdout is
  # piped through tail, so a hang shows nothing at all - a probe that died at step 7 and one
  # that never started look the same. The file is the only live witness, and it names the
  # step the run reached.
  STEPS=/tmp/probe_timeline_ink_steps.log
  : > "$STEPS"
  say() { echo "t+${SECONDS}s  $*" >> "$STEPS"; }

  wait_ready 120 || abort "the main window never came up (wait_ready timed out)"
  W=$(xdotool getactivewindow 2>/dev/null || xdotool search --name AnimeEffects | tail -1)
  xdotool windowactivate --sync $W 2>/dev/null
  sleep 0.6

  raw() { magick "$1" -crop "$2" +repage -depth 8 rgb:"$3"; }
  inkpx() { python3 "$INK" "$@"; }
  say "raw/ink helpers up"
  # The pointer rests at ONE fixed point (the File menu, found by name) for every capture: a
  # lane can have a hover treatment, and a capture taken with the pointer elsewhere would
  # measure that instead. The menu bar is outside every surface this probe measures, and
  # hovering a menu does not open it.
  FR=$(ae extents "File")
  [ -n "$FR" ] || abort "no File menu: this is not the application window"
  read -r FX FY FW FH <<< "$FR"
  POINTX=$((FX + 3)); POINTY=$((FY + FH / 2))
  park() { xdotool mousemove --sync $POINTX $POINTY; sleep 0.5; }
  shot() { park; import -window root "$1"; }
  # sel <row name>: select the row. BY ITS LEFT EDGE AND NOT ITS CENTRE: the cell rectangles
  # the tree publishes are WIDER THAN THE TREE ITSELF (AGENTS.md measured a cell of 81px for a
  # 70px-wide tree), so a click at the reported centre lands beside the row - which selects a
  # neighbour, silently. The y is the row own and the x is just inside where the row starts, both
  # from the same rectangle, which is the point the other probes in this repo click too.
  sel() {
    local r x y w h
    r=$(ae extents "$1" "table cell")
    if [ -n "$r" ]; then
      read -r x y w h <<< "$r"
      xdotool mousemove --sync $((x + 4)) $((y + h / 2)) click 1
      return 0
    fi
    ae click "$1" "table cell" >/dev/null 2>&1
  }

  P=$(ae extents "Animation")
  [ -n "$P" ] || abort "no visible widget named Animation: nothing to measure"
  read -r PX PY PW PH <<< "$P"
  shot "$SHOTS/tink_00_start.png"
  TX0=$((PX + 72)); TX1=$((PX + PW))
  TY0=$((PY + 37 + 40)); TY1=$((PY + PH - 13))
  CW=$((TX1 - TX0)); CH=$((TY1 - TY0))
  # THE RULER ROWS ARE MEASURED OFF THE SCREEN, not derived from kHeaderHeight. The lanes
  # start at TY0 (the row the tint first appears in - the anchor of everything below), and
  # the ruler above them measured on this build as: NUMBERS rows TY0-21..TY0-9, the TICK marks
  # TY0-8..TY0-2, the bottom hairline TY0-1. The tick band is ASSERTED below (three or more
  # marks in it), so a layout that moves the ruler fails loudly instead of reading the wrong
  # rows.
  NT=$((TY0 - 21)); NB=$((TY0 - 9)); TT=$((TY0 - 8)); TTB=$((TY0 - 2)); RT=$((TY0 - 22))
  RBARY=$((TY0 - 11))
  [ "$CW" -gt 160 ] || abort "the timeline strip is only ${CW}px wide: too narrow to measure"
  [ "$CH" -gt 60 ] || abort "the track is only ${CH}px tall: too short to measure"

  # THE TIMELINE LEFT EDGE, from the one row that draws it: the ruler own bottom hairline
  # spans the timeline and NOT the tree column, so the widest inked run in row TY0-1 IS the
  # timeline x range. The tree is a splitter width and nothing publishes it: MEASURED as
  # 189px of the panel here, while the older probes assume 72 - which is why this is measured
  # and not assumed.
  raw "$SHOTS/tink_00_start.png" "${PW}x1+${PX}+$((TY0 - 1))" "$SCRATCH/hairline.raw"
  HAIRC=$(inkpx modal "$SCRATCH/hairline.raw" $PW 1 0 0 $PW 1 | cut -d" " -f1-3)
  case "$HAIRC" in ""|none) abort "row $((TY0 - 1)) has no modal colour: nothing to locate the timeline with" ;; esac
  read -r HC1 HC2 HC3 <<< "$HAIRC"
  inkpx cols "$SCRATCH/hairline.raw" $PW 1 0 0 $HC1 $HC2 $HC3 2 1 > "$SCRATCH/hair_rows.txt"
  # the FIRST long run of that colour, not the longest: the playhead crosses the hairline
  # (me and everything else drawn over it breaks the run), so the timeline starts at the
  # smallest x of a run that is long by a ruler hairline standard.
  TL=0; TLL=0
  while read -r a b n; do
    case "$a" in ""|none) continue ;; esac
    if [ $((b - a + 1)) -ge 10 ] && [ "$TL" -eq 0 ]; then TL=$((PX + a)); fi
    if [ $((b - a + 1)) -gt "$TLL" ]; then TLL=$((b - a + 1)); fi
  done < "$SCRATCH/hair_rows.txt"
  [ "$TL" -gt 0 ] && [ "$TLL" -ge $((PW / 4)) ] || abort "row $((TY0 - 1)) holds no run of the panel modal colour rgb ${HAIRC} that could be the ruler bottom hairline (first long run at ${TL}, longest ${TLL} of ${PW}px)"
  TLW=$((PX + PW - TL))
  TXC=$((TL + TLW / 2))
  LBW=100; LBX=$((TXC - LBW / 2))
  PARKX=$((TL + TLW * 85 / 100))
  [ "$TLW" -gt 160 ] || abort "the timeline strip is only ${TLW}px wide: too narrow to measure"
  SCANX1=$((TL + TLW / 2 - 60))
  echo "GEOM=\"$PX $PY $PW $PH\""        >> "$GEOM"
  echo "TRACK=\"$TX0 $TY0 $TX1 $TY1\""   >> "$GEOM"
  echo "RULER=\"$RT $NT $NB $TT $TTB\""   >> "$GEOM"
  echo "TIMELINE=\"$TL $TLW\""             >> "$GEOM"
  echo "BOX=\"$LBX $LBW $TXC\""          >> "$GEOM"
  echo "PARK=\"$PARKX $RBARY\""          >> "$GEOM"
  say "geometry: panel $P $((PX+72))-$TX1 track $TX0,$TY0..$TX1,$TY1"

  # THE FIRST TREE ROW IS THE TOP NODE AND ITS LAYERS STAY INVISIBLE UNTIL IT IS EXPANDED.
  # The only expander is the branch indicator at (rowX - 12, rowY + rowH/2) on the row own
  # rectangle, and it is a TOGGLE: a second click collapses the folder again (AGENTS.md,
  # measured). It is clicked ONCE here and the row count is asserted to have GROWN, so an
  # expansion that did not happen is a loud failure and not a probe that measures one row.
  cells() { ae dump 2>/dev/null | sed -n "s/^ *table cell *| //p"; }
  ROWS=$(cells | sed -n 1,8p)
  NROWS=$(printf "%s\n" "$ROWS" | grep -c .)
  say "tree rows: $NROWS"
  if [ "$NROWS" -lt 3 ]; then
    TOP=$(printf "%s\n" "$ROWS" | sed -n 1p)
    FR=$(ae extents "$TOP" "table cell")
    [ -n "$FR" ] || abort "the first tree row ($TOP) has no rectangle to expand"
    read -r EX EY EW EH <<< "$FR"
    say "expanding $TOP at the branch indicator ($((EX - 12)),$((EY + EH / 2)))"
    xdotool mousemove --sync $((EX - 12)) $((EY + EH / 2)) click 1
    sleep 1.5
    ROWS=$(cells | sed -n 1,8p)
    NROWS_AFTER=$(printf "%s\n" "$ROWS" | grep -c .)
    say "tree rows after the expansion: $NROWS_AFTER"
    echo "EXPAND=\"$NROWS $NROWS_AFTER\"" >> "$GEOM"
    NROWS=$NROWS_AFTER
  else
    echo "EXPAND=\"$NROWS $NROWS\"" >> "$GEOM"
  fi
  [ "$NROWS" -ge 3 ] || abort "the object tree publishes only ${NROWS} row(s), and expanding its top node did not grow it: two CHILD rows are needed (the row menu offers Add default keys only for a node with a parent)"
  say "row menu will be driven on child rows only"
  # the CHILD rows: the top node is skipped because the row menu offers no Add default keys for
  # it at all (objItem->node().parent() is what gates those entries), so pressing the entries by
  # position on it would hit Create layer/folder and open a dialog instead.
  ROW_A=$(printf "%s\n" "$ROWS" | sed -n 2p)
  ROW_B=$(printf "%s\n" "$ROWS" | sed -n 3p)
  [ -n "$ROW_A" ] && [ -n "$ROW_B" ] || abort "the tree published no two child rows after the expansion"
  echo "ROW_A=\"$ROW_A\"" >> "$GEOM"
  echo "ROW_B=\"$ROW_B\"" >> "$GEOM"
  echo "ROWS=\"$(printf "%s" "$ROWS" | tr "\n" ";")\"" >> "$GEOM"
  say "row A=$ROW_A row B=$ROW_B"

  # ---- 1. the SAME row selected, not selected, and selected again ----
  sel "$ROW_A" || abort "row A ($ROW_A) is not addressable as a table cell"
  sleep 1.0; shot "$SHOTS/tink_01_rowA_sel.png"
  sel "$ROW_B" || abort "row B ($ROW_B) is not addressable as a table cell"
  sleep 1.0; shot "$SHOTS/tink_02_rowB_sel.png"
  sel "$ROW_A" || abort "row A ($ROW_A) stopped being addressable"
  sleep 1.0; shot "$SHOTS/tink_03_rowA_sel_again.png"
  say "three row-selection captures done"

  AROW=$(ae extents "$ROW_A" "table cell")
  [ -n "$AROW" ] || abort "row A ($ROW_A) has no rectangle after the selection"
  read -r AX AY AW AH <<< "${AROW:-}"
  [ "$AH" -ge 8 ] || abort "row A reports a ${AH}px rectangle: too short to hold a rail"
  echo "AROW=\"$AX $AY $AW $AH\"" >> "$GEOM"

  # THE RAIL. The strip is the row rectangle widened 6px left and 16px right (the rail sits
  # at the item column own viewport position, which is that rectangle left edge within a
  # couple of pixels), over the row full height. The test is SOLIDITY, not colour: the strip
  # modal colour is the row own fill, and a column qualifies when all but RAIL_SLACK of its
  # height is one colour away from that fill.
  # THE STRIP IS THE WHOLE TREE COLUMN over the row own rows: the tree is the panel left
  # 72px (the marquee probe measured that split), and scanning all of it is what makes the rail
  # findable without trusting the cell rectangle x - which the tree publishes WIDER than the
  # tree itself. The rail own x is then checked against that rectangle left edge.
  SW=16
  raw "$SHOTS/tink_01_rowA_sel.png" "${SW}x${AH}+${PX}+${AY}" "$SCRATCH/rail_sel.raw"
  RAIL_BASE=$(inkpx modal "$SCRATCH/rail_sel.raw" $SW $AH 0 0 $SW $AH | cut -d" " -f1-3)
  case "$RAIL_BASE" in ""|none) abort "the rail strip came out empty: the capture is wrong" ;; esac
  read -r BR BG BB <<< "$RAIL_BASE"
  echo "RAIL_BASE=\"$RAIL_BASE\"" >> "$GEOM"
  inkpx inkcols "$SCRATCH/rail_sel.raw" $SW $AH 0 $((AH - 1)) $BR $BG $BB 30 $((AH - RAIL_SLACK)) \
      > "$SCRATCH/rail_runs.txt"
  RAIL_LINE=$(sed -n 1p "$SCRATCH/rail_runs.txt")
  echo "RAIL_LINE=\"$RAIL_LINE\"" >> "$GEOM"
  echo "RAIL_ALL=\"$(tr "\n" ";" < "$SCRATCH/rail_runs.txt")\"" >> "$GEOM"
  RAILC=$(printf "%s" "$RAIL_LINE" | cut -d" " -f4-6)
  echo "RAIL_COLOUR=\"${RAILC:-none}\"" >> "$GEOM"
  [ -n "$RAILC" ] || abort "row A draws no solid accent column while selected: no rail on screen"

  # The same strip on the SAME row while row B is selected: the rail must be gone.
  raw "$SHOTS/tink_02_rowB_sel.png" "${SW}x${AH}+${PX}+${AY}" "$SCRATCH/rail_unsel.raw"
  read -r RR RG RB <<< "$RAILC"
  inkpx cols "$SCRATCH/rail_unsel.raw" $SW $AH 0 $((AH - 1)) $RR $RG $RB 30 $((AH - RAIL_SLACK)) \
      > "$SCRATCH/rail_gone.txt"
  echo "RAIL_NONE=\"$(tr "\n" ";" < "$SCRATCH/rail_gone.txt")\"" >> "$GEOM"

  # ---- 1b. the LANE BAND, as colour ----
  # B: the surface a lane is filled with. Read where row A is NOT the selection (so its own
  # band is plain) and over the bottom quarter of the track - below the rows, where the same
  # fill is - so a tinted band cannot be mistaken for the surface.
  BQH=$((CH / 4))
  raw "$SHOTS/tink_02_rowB_sel.png" "${CW}x${BQH}+${TX0}+$((TY1 - BQH))" "$SCRATCH/surface.raw"
  SURFACE=$(inkpx modal "$SCRATCH/surface.raw" $CW $BQH 0 0 $CW $BQH)
  echo "SURFACE=\"${SURFACE:-none}\"" >> "$GEOM"
  case "$SURFACE" in ""|none) abort "the strip below the lanes has no modal colour" ;; esac
  SR=$(printf "%s" "$SURFACE" | cut -d" " -f1)
  SG=$(printf "%s" "$SURFACE" | cut -d" " -f2)
  SB=$(printf "%s" "$SURFACE" | cut -d" " -f3)
  SSN=$(printf "%s" "$SURFACE" | cut -d" " -f4)
  [ "$SSN" -gt $((CW * BQH / 10)) ] \
      || abort "the strip below the lanes is not one surface colour (${SURFACE}): no band to compare against"

  # The tinted lane: rows of the track (with row A selected) whose fill is NOT that surface.
  # A thresh of 3 leaves the 8-bit rounding of one flat fill alone; runs separated by up to
  # 3 rows are merged, because a lane own separator hairlines sit inside it.
  raw "$SHOTS/tink_01_rowA_sel.png" "${CW}x${CH}+${TX0}+${TY0}" "$SCRATCH/track_sel.raw"
  inkpx rows "$SCRATCH/track_sel.raw" $CW $CH $SR $SG $SB 3 0.5 > "$SCRATCH/lane_rows.txt"
  echo "LANE_RUNS=\"$(tr "\n" ";" < "$SCRATCH/lane_rows.txt")\"" >> "$GEOM"
  LANE=""; LS=-1; LE=-1
  while read -r a b; do
    [ -n "$a" ] || continue
    if [ "$LS" -lt 0 ]; then LS=$a; LE=$b; continue; fi
    if [ $((a - LE)) -le 3 ]; then LE=$b; continue; fi
    if [ $((LE - LS + 1)) -ge 8 ]; then LANE="$LS $LE"; break; fi
    LS=$a; LE=$b
  done < "$SCRATCH/lane_rows.txt"
  if [ -z "$LANE" ] && [ "$LS" -ge 0 ] && [ $((LE - LS + 1)) -ge 8 ]; then LANE="$LS $LE"; fi
  [ -n "$LANE" ] || abort "no lane of row A changed colour when it was selected: no band tint on screen"
  read -r LY0 LY1 <<< "${LANE:-}"
  LH=$((LY1 - LY0 + 1))
  echo "LANE=\"$LY0 $LY1\"" >> "$GEOM"
  raw "$SHOTS/tink_01_rowA_sel.png" "${CW}x${LH}+${TX0}+$((TY0 + LY0))" "$SCRATCH/lane.raw"
  LANE_COLOUR=$(inkpx modal "$SCRATCH/lane.raw" $CW $LH 0 0 $CW $LH | cut -d" " -f1-3)
  echo "LANE_COLOUR=\"$LANE_COLOUR\"" >> "$GEOM"
  raw "$SHOTS/tink_02_rowB_sel.png" "${CW}x${LH}+${TX0}+$((TY0 + LY0))" "$SCRATCH/lane_off.raw"
  LANE_OFF=$(inkpx modal "$SCRATCH/lane_off.raw" $CW $LH 0 0 $CW $LH | cut -d" " -f1-3)
  echo "LANE_COLOUR_OFF=\"$LANE_OFF\"" >> "$GEOM"
  say "rail and lane measured: rail $RAILC lane $LANE_COLOUR off $LANE_OFF"

  say "phase: keys"
  # ---- 2. the keys, and then the ring ----
  # Add default keys through the OBJECT TREE row menu, the way the graph probes drive a menu
  # graph probes drive the same kind of menu: a right click inside the cell opens a top-level
  # popup at the click point; the popup is focused by window id; two UPs reach "Add default keys"
  # (the last entry is Delete and the separator above it is skipped) and Return presses it.
  #
  # EVERY ROW IS TRIED IN TURN, because the action does nothing at all on a FOLDER your
  # object tree starts with - obj::Item::cast refuses it, the menu closes, and the lane stays
  # empty. The loop stops at the first row whose lane shows KEY ink, and that row is the one
  # every later assertion is made on (its lane band is re-derived: the tint follows the
  # selection).
  keys_menu() { # keys_menu <row> <cell rect> -> echoes the popup window id, or nothing
    local row=$1 ax=$2 ay=$3 ah=$4 pop dx dy
    sel "$row" || return 1
    sleep 0.8
    dx=$((ax + 6)); dy=$((ay + ah / 2))
    xdotool mousemove --sync $dx $dy; sleep 0.4
    xdotool click 3; sleep 1.2
    pop=$(xwininfo -root -children 2>/dev/null | grep -E "[0-9]+x[0-9]+\+${dx}\+${dy}" | grep -oE "0x[0-9a-f]+" | head -1)
    [ -n "$pop" ] || return 1
    local ph
    ph=$(xwininfo -id "$pop" 2>/dev/null | sed -n "s/^ *Height: *//p" | tr -d " ")
    # The child row menu MEASURED 223px tall (nine entries and two separators). A menu
    # without the parent-gated entries is far shorter, and pressing ITS entries blind is how
    # a probe opens Create layer. Refuse it rather than guess.
    if [ -z "$ph" ] || [ "$ph" -lt 180 ]; then
      say "row menu is ${ph:-?}px tall: not the child row menu, refusing to press it"
      xdotool key --clearmodifiers Escape
      return 1
    fi
    echo "$pop"
    xdotool windowfocus --sync "$pop" 2>/dev/null
    xdotool key --clearmodifiers Up;     sleep 0.3
    xdotool key --clearmodifiers Up;     sleep 0.3
    xdotool key --clearmodifiers Return; sleep 2
    xdotool windowactivate --sync $W 2>/dev/null
    return 0
  }
  lane_of() { # lane_of <png>: echoes "y0 y1" for the lane the selection tinted, or nothing
    local a b ls le
    raw "$1" "${CW}x${CH}+${TX0}+${TY0}" "$SCRATCH/track.raw"
    inkpx rows "$SCRATCH/track.raw" $CW $CH $SR $SG $SB 3 0.5 > "$SCRATCH/lane_rows.txt"
    ls=-1; le=-1
    while read -r a b; do
      case "$a" in ""|none) continue ;; esac
      if [ "$ls" -lt 0 ]; then ls=$a; le=$b; continue; fi
      if [ $((a - le)) -le 3 ]; then le=$b; continue; fi
      if [ $((le - ls + 1)) -ge 8 ]; then echo "$ls $le"; return; fi
      ls=$a; le=$b
    done < "$SCRATCH/lane_rows.txt"
    if [ "$ls" -ge 0 ] && [ $((le - ls + 1)) -ge 8 ]; then echo "$ls $le"; fi
  }

  say "phase: ruler layout check"
  # The ruler layout is checked before anything is read off it: the ticks live at the bottom
  # of the ruler band (rows TT..TY0-1), so a run list there proves the constants the number
  # row is derived from still describe this widget.
  raw "$SHOTS/tink_03_rowA_sel_again.png" "${CW}x$((TTB - TT + 1))+${TX0}+${TT}" "$SCRATCH/ticks.raw"
  inkpx inkcols "$SCRATCH/ticks.raw" $CW $((TTB - TT + 1)) 0 $((TTB - TT)) $SR $SG $SB 30 3 \
      > "$SCRATCH/tick_runs.txt"
  echo "TICK_RUNS=\"$(tr "\n" ";" < "$SCRATCH/tick_runs.txt")\"" >> "$GEOM"

  # THE CHILD ROWS ONLY. The top node has no parent, so the row menu shows NEITHER Paste
  # key(s) NOR Add default keys NOR Delete there (obj::Item::cast plus node().parent() gate
  # them), and driving that menu by position lands on Create layer - which opens a dialog and
  # blocks every candidate after it.
  CAND=$(printf "%s\n" "$ROWS" | sed -n 2,8p)
  KEYROW=""; POP=""; LY0=0; LY1=0; LH=0
  while IFS= read -r cand; do
    [ -n "$cand" ] || continue
    [ -n "$KEYROW" ] && continue
    CR=$(ae extents "$cand" "table cell")
    [ -n "$CR" ] || continue
    read -r CX CY CWID CHGT <<< "$CR"
    POPT=$(keys_menu "$cand" "$CX" "$CY" "$CHGT")
    [ -n "$POPT" ] && POP="$POPT"
    say "candidate [$cand] rect $CR popup ${POPT:-none}"
    sleep 0.5
    sel "$cand" || continue
    sleep 0.8
    # park the playhead at 85% of the track: a click on the ruler sets the frame under the
    # cursor, and that column is clear of both the frame-0 keys (at the strip left) and the
    # label box (the track centre)
    xdotool mousemove --sync $PARKX $RBARY click 1; sleep 1.0
    shot "$SHOTS/tink_04_keys_parked.png"
    L=$(lane_of "$SHOTS/tink_04_keys_parked.png")
    [ -n "$L" ] || continue
    read -r LY0 LY1 <<< "$L"
    LH=$((LY1 - LY0 + 1))
    LX0=$((TL + 3)); LW=$((SCANX1 - LX0))
    [ "$LW" -gt 40 ] || abort "the key scan strip is only ${LW}px wide: too narrow to measure"
    raw "$SHOTS/tink_04_keys_parked.png" "${LW}x${LH}+${LX0}+$((TY0 + LY0))" "$SCRATCH/lane_scan.raw"
    M=$(inkpx far "$SCRATCH/lane_scan.raw" $LW $LH 0 0 $LW $LH $SR $SG $SB $MARKER_MIN_INK)
    say "candidate [$cand] lane ${L:-none} marker ${M:-none} (scan x $LX0..$((LX0 + LW)) y $((TY0 + LY0))..$((TY0 + LY1)))"
    MN=$(printf "%s" "$M" | cut -d" " -f1)
    case "${MN:-0}" in ""|0) continue ;; esac
    MARKER="$M"; KEYROW="$cand"
  done <<< "$CAND"
  [ -n "$KEYROW" ] || abort "no object row produced key ink in its lane: the row menu added no keys to any of the four rows tried"
  echo "KEYROW=\"$KEYROW\"" >> "$GEOM"
  echo "POP=\"${POP:-none}\"" >> "$GEOM"
  echo "MARKER=\"$MARKER\"" >> "$GEOM"
  HUE=$(printf "%s" "$MARKER" | cut -d" " -f4-6)
  # THE MARKER CENTRE IS ITS CENTRE OF MASS, not the farthest pixel: the farthest one is a
  # core pixel somewhere inside the disk, two or three pixels off its centre, and that is
  # enough to miss a 1px ring 6.35px out (measured: the annulus read 0 from the farthest
  # pixel and the ring is plainly there from the centroid).
  CENT=$(inkpx centroid "$SCRATCH/lane_scan.raw" $LW $LH 0 0 $LW $LH $(printf "%s" "$HUE" | tr " " " ") 20)
  MX=$(printf "%s" "$CENT" | cut -d" " -f1)
  MY=$(printf "%s" "$CENT" | cut -d" " -f2)
  case "$MX" in ""|none) abort "the marker ink has no centre of mass" ;; esac
  MSX=$((LX0 + MX)); MSY=$((TY0 + LY0 + MY))
  MLX=$((MSX - TX0))
  echo "LANE=\"$LY0 $LY1\"" >> "$GEOM"
  echo "HUE=\"$HUE\"" >> "$GEOM"
  echo "MARKER_AT=\"$MSX $MSY\"" >> "$GEOM"
  echo "MARKER_INK=\"$(inkpx count "$SCRATCH/lane_scan.raw" $LW $LH $((MX - 8)) $((MY - 8)) 17 17 $(printf "%s" "$HUE" | tr " " " ") 10)\"" >> "$GEOM"
  # the lane own band colour, re-read for the row the keys landed on (the label ink is a
  # composite of the hue over THAT band, not over the other row own)
  raw "$SHOTS/tink_04_keys_parked.png" "${CW}x${LH}+${TX0}+$((TY0 + LY0))" "$SCRATCH/lane_key.raw"
  LANE_COLOUR=$(inkpx modal "$SCRATCH/lane_key.raw" $CW $LH 0 0 $CW $LH | cut -d" " -f1-3)
  echo "LANE_COLOUR_KEY=\"$LANE_COLOUR\"" >> "$GEOM"

  # The ring, on the SAME marker, in the three states of the key selection. The playhead
  # line is accent too, so its column is measured as well and the probe moves it clear
  # before reading the annulus.
  ringcount() { # ringcount <png>
    raw "$1" "${CW}x${CH}+${TX0}+${TY0}" "$SCRATCH/ring.raw"
    inkpx ring "$SCRATCH/ring.raw" $CW $CH $MLX $((MSY - TY0)) 5 8 \
        $(printf "%s" "$RAILC" | tr " " " ") $RING_TOL
  }
  centre() { # centre <png>
    raw "$1" "1x1+${MSX}+${MSY}" "$SCRATCH/one.raw"
    inkpx px "$SCRATCH/one.raw" 1 1 0 0
  }
  playhead_x() { # playhead_x <png>: the track-local x of the accent column spanning the track
    raw "$1" "${CW}x${CH}+${TX0}+${TY0}" "$SCRATCH/ph.raw"
    inkpx cols "$SCRATCH/ph.raw" $CW $CH 0 $((CH - 1)) \
        $(printf "%s" "$RAILC" | tr " " " ") $RING_TOL $((CH / 3)) | head -1 | cut -d" " -f1
  }
  # clear_of_marker: walk the playhead right with the application own MoveRight command until
  # its column is far enough from the marker that it cannot touch the 8px annulus. The bar is
  # not load-bearing: a 1px line crossing the marker column can contribute at most 8 pixels
  # to the annulus, and the ring bar (>= 20) is above that, so a walk that runs out of frames
  # still cannot make the ring assertion pass on the playhead alone.
  clear_of_marker() { # clear_of_marker <png> <tries>
    local n=$1 png=$2 i=0 p d
    p=$(playhead_x "$png")
    while [ -n "$p" ] && [ "$p" != "none" ]; do
        d=$((p - MLX)); [ "$d" -lt 0 ] && d=$((-d))
        [ "$d" -ge 25 ] && break
        xdotool key --clearmodifiers --repeat 12 --repeat-delay 60 ctrl+Right
        sleep 0.6
        shot "$png"
        p=$(playhead_x "$png")
        i=$((i + 1)); [ "$i" -ge "$n" ] && break
    done
    echo "${p:-none}"
  }
  echo "PLAYHEAD_PARKED=\"$(playhead_x "$SHOTS/tink_04_keys_parked.png")\"" >> "$GEOM"
  echo "RING_UNSEL=\"$(ringcount "$SHOTS/tink_04_keys_parked.png")\"" >> "$GEOM"
  echo "CENTRE_UNSEL=\"$(centre "$SHOTS/tink_04_keys_parked.png")\"" >> "$GEOM"

  # SELECT the key: a left press inside the editor own hit radius (12 * 1.25 = 15px) of the
  # marker centre. That press also moves the playhead onto the key frame, hence the walk.
  xdotool mousemove --sync $MSX $MSY click 1; sleep 1.0
  shot "$SHOTS/tink_05_key_selected.png"
  PH=$(clear_of_marker 10 "$SHOTS/tink_05_key_selected.png")
  echo "PLAYHEAD_SELECTED=\"$PH\"" >> "$GEOM"
  echo "RING_SEL=\"$(ringcount "$SHOTS/tink_05_key_selected.png")\"" >> "$GEOM"
  echo "CENTRE_SEL=\"$(centre "$SHOTS/tink_05_key_selected.png")\"" >> "$GEOM"

  # DESELECT: a plain click on empty lane space at the far right (frame 0 is at the strip
  # left, so nothing is out there), which the editor reads as "clear".
  xdotool mousemove --sync $((TX1 - 20)) $MSY click 1; sleep 1.0
  shot "$SHOTS/tink_06_key_deselected.png"
  echo "PLAYHEAD_DESEL=\"$(playhead_x "$SHOTS/tink_06_key_deselected.png")\"" >> "$GEOM"
  echo "RING_DESEL=\"$(ringcount "$SHOTS/tink_06_key_deselected.png")\"" >> "$GEOM"
  echo "CENTRE_DESEL=\"$(centre "$SHOTS/tink_06_key_deselected.png")\"" >> "$GEOM"

  say "phase: lane label"
  # ---- 3. the lane label, with the playhead away from it and parked on it ----
  # The label box: the renderer centres a 100px text rect on the track, in the lane own rows.
  # The predicted ink is the 0.42 composite of the lane own key hue over the lane own band -
  # a colour a full-strength key CANNOT be, so a key inside the box does not count as a label.
  read -r H1 H2 H3 <<< "${HUE:-}"
  # the band the key row own label is drawn on: the lane colour read for the row the keys
  # landed on, which is the lane this box is measured in.
  read -r C1 C2 C3 <<< "${LANE_COLOUR_KEY:-$LANE_COLOUR}"
  LBL_R=$(awk -v h=$H1 -v c=$C1 "BEGIN{printf \"%d\", int(0.42*h + 0.58*c + 0.5)}")
  LBL_G=$(awk -v h=$H2 -v c=$C2 "BEGIN{printf \"%d\", int(0.42*h + 0.58*c + 0.5)}")
  LBL_B=$(awk -v h=$H3 -v c=$C3 "BEGIN{printf \"%d\", int(0.42*h + 0.58*c + 0.5)}")
  echo "LABEL_INK_EXPECTED=\"$LBL_R $LBL_G $LBL_B\"" >> "$GEOM"
  # THE LABEL BOX IS FOUND IN THE PICTURE: the renderer centres it on the timeline own
  # camera rect, and nothing publishes that centre, so the box IS the ink the predicted
  # colour makes in the lane rows - its columns are the label, and their centre is the
  # point the second measurement parks the playhead on.
  raw "$SHOTS/tink_04_keys_parked.png" "${PW}x${LH}+${PX}+$((TY0 + LY0))" "$SCRATCH/lblscan.raw"
  inkpx cols "$SCRATCH/lblscan.raw" $PW $LH 0 $((LH - 1)) $LBL_R $LBL_G $LBL_B $LABEL_TOL 2 \
      > "$SCRATCH/lblcols.txt"
  LB_X0=-1; LB_X1=-1
  while read -r a b n; do
    case "$a" in ""|none) continue ;; esac
    [ "$LB_X0" -lt 0 ] && LB_X0=$a
    LB_X1=$b
  done < "$SCRATCH/lblcols.txt"
  [ "$LB_X0" -ge 0 ] || abort "no ink of the predicted label colour rgb $LBL_R $LBL_G $LBL_B in the lane: the lane label is not drawn in its channel hue at 0.42"
  # the matched columns are the label ink CORE (the fully covered glyph pixels), so the box is
  # widened about them: any part of the label measures whether the name is drawn at all, and
  # the renderer now draws it unconditionally - the playhead only crosses it.
  LBMID=$((PX + (LB_X0 + LB_X1) / 2))
  LBX=$((LBMID - 25)); LBW=50
  echo "LABEL_BOX=\"$LBX $LBW $LBMID\"" >> "$GEOM"
  say "label box: x $LBX w $LBW centre $LBMID (ink rgb $LBL_R $LBL_G $LBL_B)"
  label_ink() { # label_ink <png>
    raw "$1" "${LBW}x${LH}+${LBX}+$((TY0 + LY0))" "$SCRATCH/label.raw"
    inkpx count "$SCRATCH/label.raw" $LBW $LH 0 0 $LBW $LH $LBL_R $LBL_G $LBL_B $LABEL_TOL
  }
  echo "LABEL_AWAY=\"$(label_ink "$SHOTS/tink_04_keys_parked.png")\"" >> "$GEOM"
  echo "LABEL_AWAY2=\"$(label_ink "$SHOTS/tink_04_keys_parked.png")\"" >> "$GEOM"
  AWAY_INK=$(label_ink "$SHOTS/tink_04_keys_parked.png")

  # Park the playhead ON the label box centre and read the same box again. The playhead
  # 1px line crosses the text now; the name is still drawn, so this box carries the label
  # ink in BOTH states. Nothing here asserts a suppression any more: the lane name is not
  # the ruler number (the pill carries the frame, nothing carries the lane name).
  xdotool mousemove --sync $LBMID $RBARY click 1; sleep 1.0
  shot "$SHOTS/tink_07_label_pill.png"
  PILL_INK=$(label_ink "$SHOTS/tink_07_label_pill.png")
  echo "LABEL_PILL=\"$PILL_INK\"" >> "$GEOM"
  echo "PLAYHEAD_ON_LABEL=\"$(playhead_x "$SHOTS/tink_07_label_pill.png")\"" >> "$GEOM"
  # and away again, as a fresh state rather than the same file read twice.
  xdotool mousemove --sync $PARKX $RBARY click 1; sleep 1.0
  shot "$SHOTS/tink_08_label_back.png"
  BACK_INK=$(label_ink "$SHOTS/tink_08_label_back.png")
  echo "LABEL_BACK=\"$BACK_INK\"" >> "$GEOM"
  say "label ink: playhead away ${AWAY_INK}px, parked on the box centre ${PILL_INK}px, away again ${BACK_INK}px - the name is drawn in every state, the playhead only crosses it"

  say "phase: ruler number"
  # ---- 4. the ruler number the pill owns ----
  # The number runs are found IN the picture, in the ruler number row of a capture whose
  # playhead is clear of the box that follows. The ink is "farther than 30 from the ruler
  # background" (the same surface the lanes are filled with); a run is 4+ inked columns wide
  # and its own colour must not be the accent, because the pill is a solid accent block.
  raw "$SHOTS/tink_04_keys_parked.png" "${TLW}x$((NB - NT + 1))+${TL}+${NT}" "$SCRATCH/numrow.raw"
  NRH=$((NB - NT + 1))
  inkpx inkcols "$SCRATCH/numrow.raw" $TLW $NRH 0 $((NRH - 1)) $SR $SG $SB 30 4 > "$SCRATCH/number_runs.txt"
  echo "NUMBER_RUNS=\"$(tr "\n" ";" < "$SCRATCH/number_runs.txt")\"" >> "$GEOM"
  PH_PARK=$(playhead_x "$SHOTS/tink_04_keys_parked.png")
  case "$PH_PARK" in ""|none) PH_PARK=-999 ;; esac
  NUM=""; BEST=-1
  while read -r a b n r g bl; do
    [ -n "$a" ] || continue
    [ "$n" -ge 4 ] || continue
    mid=$(((a + b) / 2))
    [ "$mid" -ge 40 ] || continue
    [ "$mid" -le $((TLW - 40)) ] || continue
    # the pill is solid accent: skip a run whose colour is the accent at all
    dr=$(( r > RR ? r - RR : RR - r )); dg=$(( g > RG ? g - RG : RG - g )); db=$(( bl > RB ? bl - RB : RB - bl ))
    if [ "$dr" -lt 40 ] && [ "$dg" -lt 40 ] && [ "$db" -lt 40 ]; then continue; fi
    dist_ph=$((mid - PH_PARK)); [ "$dist_ph" -lt 0 ] && dist_ph=$((-dist_ph))
    [ "$dist_ph" -ge 80 ] || continue
    if [ "$dist_ph" -gt "$BEST" ]; then BEST=$dist_ph; NUM="$a $b $n $r $g $bl"; fi
  done < "$SCRATCH/number_runs.txt"
  [ -n "$NUM" ] || abort "no ruler number run clear of the parked playhead: nothing to suppress"
  echo "NUMBER=\"$NUM\"" >> "$GEOM"
  read -r NX0 NX1 NN NCR NCG NCB <<< "$NUM"
  NBX=$((TL + NX0 - 3)); NBW=$((NX1 - NX0 + 7)); NMID=$((TL + (NX0 + NX1) / 2))
  echo "NUMBER_BOX=\"$NBX $NBW $NMID\"" >> "$GEOM"
  # the box holds a number ON THE RULER SURFACE, or - with the pill over it - the accent fill
  # and the pill own text. Both are measured, because THE SKIP ITSELF IS NOT OBSERVABLE: the
  # pill is opaque and covers the number box exactly, so a number drawn under it would look
  # identical. What IS observable is which ink owns the column, and that the number returns.
  number_ink() { # number_ink <png>
    raw "$1" "${NBW}x${NRH}+${NBX}+${NT}" "$SCRATCH/numbox.raw"
    inkpx far "$SCRATCH/numbox.raw" $NBW $NRH 0 0 $NBW $NRH $SR $SG $SB 30 | cut -d" " -f1
  }
  number_modal() { # number_modal <png>
    raw "$1" "${NBW}x${NRH}+${NBX}+${NT}" "$SCRATCH/numbox.raw"
    inkpx modal "$SCRATCH/numbox.raw" $NBW $NRH 0 0 $NBW $NRH | cut -d" " -f1-3
  }
  echo "NUM_AWAY=\"$(number_ink "$SHOTS/tink_04_keys_parked.png")\"" >> "$GEOM"
  echo "NUM_MODAL_AWAY=\"$(number_modal "$SHOTS/tink_04_keys_parked.png")\"" >> "$GEOM"
  # the number is centred on its own tick, so a ruler click at the run own centre parks the
  # playhead on exactly the frame that number belongs to.
  xdotool mousemove --sync $NMID $RBARY click 1; sleep 1.0
  shot "$SHOTS/tink_09_number_pill.png"
  echo "NUM_PILL=\"$(number_ink "$SHOTS/tink_09_number_pill.png")\"" >> "$GEOM"
  echo "NUM_PILL_X=\"$(playhead_x "$SHOTS/tink_09_number_pill.png")\"" >> "$GEOM"
  echo "NUM_MODAL_PILL=\"$(number_modal "$SHOTS/tink_09_number_pill.png")\"" >> "$GEOM"
  # and 90px away from it: the number is back.
  xdotool mousemove --sync $((NMID + 90)) $RBARY click 1; sleep 1.0
  shot "$SHOTS/tink_10_number_back.png"
  echo "NUM_BACK=\"$(number_ink "$SHOTS/tink_10_number_back.png")\"" >> "$GEOM"
  echo "NUM_MODAL_BACK=\"$(number_modal "$SHOTS/tink_10_number_back.png")\"" >> "$GEOM"

  say "body done"
  kill $app 2>/dev/null
  wait $app 2>/dev/null
' 2>&1 | tail -4

. "$GEOM" 2>/dev/null
if [ -n "${ABORT:-}" ]; then
    check 1 "$ABORT"
    summary probe_timeline_ink
fi
if [ -z "${TRACK:-}" ] || [ -z "${ROW_A:-}" ]; then
    check 1 "the probe body produced no geometry (see $GEOM and /tmp/probe_timeline_ink_app.log)"
    summary probe_timeline_ink
fi

read -r TX0 TY0 TX1 TY1 <<< "${TRACK:-}"
read -r RT NT NB TT TTB <<< "${RULER:-}"
read -r PX PY PW PH <<< "${GEOM:-}"
read -r LBX LBW LBMID <<< "${LABEL_BOX:-}"
read -r AX AY AW AH <<< "${AROW:-}"
read -r SR SG SB SSN <<< "${SURFACE:-}"
read -r RR RG RB <<< "${RAIL_COLOUR:-}"
read -r LY0 LY1 <<< "${LANE:-}"
read -r C1 C2 C3 <<< "${LANE_COLOUR:-}"
read -r O1 O2 O3 <<< "${LANE_COLOUR_OFF:-none}"
read -r MSX MSY <<< "${MARKER_AT:-}"
read -r H1 H2 H3 <<< "${HUE:-}"
read -r L1 L2 L3 <<< "${LABEL_INK_EXPECTED:-0 0 0}"
read -r NX0 NX1 NN NCR NCG NCB <<< "${NUMBER:-}"
read -r NBX NBW NMID <<< "${NUMBER_BOX:-}"
read -r NMA1 NMA2 NMA3 <<< "${NUM_MODAL_AWAY:-none}"
read -r NMP1 NMP2 NMP3 <<< "${NUM_MODAL_PILL:-none}"
read -r NMB1 NMB2 NMB3 <<< "${NUM_MODAL_BACK:-none}"

mix() { awk -v a1="$1" -v a2="$2" -v a3="$3" -v b1="$4" -v b2="$5" -v b3="$6" -v t="$7" \
        'BEGIN{printf "%d %d %d", a1*t+b1*(1-t)+0.5, a2*t+b2*(1-t)+0.5, a3*t+b3*(1-t)+0.5}'; }
dist() { awk -v a1="$1" -v a2="$2" -v a3="$3" -v b1="$4" -v b2="$5" -v b3="$6" \
        'BEGIN{d1=a1-b1; d2=a2-b2; d3=a3-b3; printf "%.1f", sqrt(d1*d1+d2*d2+d3*d3)}'; }
share() { awk -v a="$1" -v b="$2" 'BEGIN{ printf "%.3f", (b > 0) ? a/b : 0 }'; }
spread() { awk -v a="$1" -v b="$2" -v c="$3" 'BEGIN{m=a; if(b>m)m=b; if(c>m)m=c; n=a; if(b<n)n=b; if(c<n)n=c; print m-n}'; }

echo "geometry: track ${TX0},${TY0}..${TX1},${TY1}; ruler rows ${RT}..$((TY0-1)) (numbers ${NT}..${NB}, ticks ${TT}..$((TY0-1))); row $ROW_A at ${AX},${AY} ${AW}x${AH}; the keyed row is ${KEYROW:-?}, its lane rows ${LY0}..${LY1}, its marker ${MSX},${MSY} rgb ${HUE:-?}; label box ${LBX}+${LBW}; parked playhead x ${PARK:-?}"

# ---- 0. the geometry the probe derives its boxes from ----
TICKS=$(printf "%s" "${TICK_RUNS:-}" | tr ";" "\n" | grep -c .)
[ "$TICKS" -ge 3 ]
check $? "the ruler is where the probe measured it: ${TICKS} tick marks in rows ${TT}..${TTB} (its numbers are rows ${NT}..${NB})"
awk -v p="${PLAYHEAD_PARKED:-none}" -v m="$MSX" -v x0="$TX0" \
    'BEGIN{ if (p == "none" || p == "") exit 0; d = p - (m - x0); if (d < 0) d = -d; exit !(d > 20) }'
check $? "the parked playhead was found clear of the marker column (playhead x ${PLAYHEAD_PARKED:-none}, marker column ${MSX})"

# ---- 1. the rail ----
read -r RW0 RW1 RWN RW_R RW_G RW_B <<< "$RAIL_LINE"
RAIL_X=$((PX + RW0)); RWID=$((RW1 - RW0 + 1))
RAIL_CHROMA=$(spread "${RW_R:-0}" "${RW_G:-0}" "${RW_B:-0}")
[ -n "$RAIL_LINE" ] && [ "${RWN:-0}" -ge $((AH - RAIL_SLACK)) ] && [ "$RWID" -le 3 ] \
    && [ "$RAIL_X" -le $((PX + 3)) ] && [ "$RAIL_CHROMA" -ge 60 ]
check $? "the selected object's tree row carries a rail at its left edge: ${RWID}px of one chromatic colour (rgb ${RW_R:-?} ${RW_G:-?} ${RW_B:-?}, chroma ${RAIL_CHROMA}) spanning ${RWN:-0} of the row's ${AH}px at x ${RAIL_X} (the tree starts at ${PX})"
case "${RAIL_NONE:-none}" in "none"|"none;"|"") ok=0 ;; *) ok=1 ;; esac
check "$ok" "and NOTHING of the kind while another row is selected: the same row's strip holds no accent column ([${RAIL_NONE:-none}])"
D_CTRL=$(magick compare -metric AE "$SHOTS/tink_01_rowA_sel.png" "$SHOTS/tink_03_rowA_sel_again.png" null: 2>&1)
D_CTRL=${D_CTRL%% *}
[ "${D_CTRL:-1}" -eq 0 ]
check $? "selecting that row twice draws the same screen, so the reading is a state and not a transient (${D_CTRL:-?} px differ)"
D_TREE=$(magick compare -metric AE "$SHOTS/tink_01_rowA_sel.png" "$SHOTS/tink_02_rowB_sel.png" null: 2>&1)
D_TREE=${D_TREE%% *}
[ "${D_TREE:-0}" -gt 100 ]
check $? "and selecting the OTHER row moves the screen (${D_TREE:-?} px differ), so the two states are not the same picture"

# ---- 1b. the lane tint ----
TINT=$(mix "$RR" "$RG" "$RB" "$SR" "$SG" "$SB" 0.06)
FLOOD=$(mix "$RR" "$RG" "$RB" "$SR" "$SG" "$SB" 0.40)
read -r T1 T2 T3 <<< "$TINT"
read -r F1 F2 F3 <<< "$FLOOD"
D_06=$(dist "$C1" "$C2" "$C3" "$T1" "$T2" "$T3")
D_40=$(dist "$C1" "$C2" "$C3" "$F1" "$F2" "$F3")
D_SURF=$(dist "$C1" "$C2" "$C3" "$SR" "$SG" "$SB")
D_ACC=$(dist "$RR" "$RG" "$RB" "$SR" "$SG" "$SB")
echo "lane: selected rgb ${LANE_COLOUR:-?}; 0.06 composite rgb ${TINT} (${D_06} away); 0.40 flood rgb ${FLOOD} (${D_40} away); surface rgb ${SURFACE} (${D_SURF} away); accent rgb ${RAIL_COLOUR} (${D_ACC} from the surface)"
awk -v a="$D_06" -v b="$D_40" 'BEGIN{ exit !(a <= '"$TINT_TOL"' && b > 3*a) }'
check $? "the selected lane's band is the accent at 0.06 over the surface: rgb ${LANE_COLOUR} is ${D_06} from the 0.06 composite and ${D_40} from the old 0.40 flood"
awk -v a="$D_SURF" -v hi="$D_ACC" -v s="$TINT_SHARE" 'BEGIN{ exit !(a > 2 && a <= hi*s + 6) }'
check $? "a whisper, not a flood: the band moved ${D_SURF} of the accent's own ${D_ACC} from the surface (share $(share "$D_SURF" "$D_ACC") <= ${TINT_SHARE})"
[ "$O1" != "none" ] || { check 1 "the de-selected lane could not be read"; summary probe_timeline_ink; }
D_OFF=$(dist "$C1" "$C2" "$C3" "$O1" "$O2" "$O3")
awk -v a="$D_OFF" -v d="$D_06" 'BEGIN{ exit !(a > d + 4) }'
check $? "and the SAME rows read differently once another row is selected: rgb ${LANE_COLOUR_OFF} against rgb ${LANE_COLOUR} (${D_OFF} apart), so the band follows the selection"
awk -v a="$(dist "$O1" "$O2" "$O3" "$SR" "$SG" "$SB")" 'BEGIN{ exit !(a <= 6) }'
check $? "and what they read then is the plain surface rgb ${SURFACE} ($(dist "$O1" "$O2" "$O3" "$SR" "$SG" "$SB") away)"

# ---- 2. the key: its ring, its fill, and both directions ----
echo "key: marker at ${MSX},${MSY}; hue rgb ${HUE}; annulus accent pixels unslected/selected/deselected ${RING_UNSEL:-?} / ${RING_SEL:-?} / ${RING_DESEL:-?}; centres ${CENTRE_UNSEL:-?} / ${CENTRE_SEL:-?} / ${CENTRE_DESEL:-?}"
[ "${MARKER_INK:-0}" -ge 8 ]
check $? "the marker everything here rests on is a drawn marker: ${MARKER_INK:-0} pixels of its hue in a 17x17 box at its own centre (rgb ${HUE})"
[ "${RING_SEL:-0}" -ge "$RING_MIN" ]
check $? "the selected key wears the accent ring: ${RING_SEL:-0} accent pixels in the 5..8px annulus about ${MSX},${MSY} (bar ${RING_MIN})"
[ "${RING_UNSEL:-999}" -le 5 ]
check $? "and the SAME annulus around the untouched key is the lane: ${RING_UNSEL:-?} accent pixels before the selection"
[ "${RING_DESEL:-999}" -le 5 ]
check $? "and the ring is gone again after a click on empty lane: ${RING_DESEL:-?} accent pixels"
awk -v a="${RING_SEL:-0}" -v b="${RING_UNSEL:-0}" -v c="${RING_DESEL:-0}" -v m="$RING_MIN" 'BEGIN{ exit !(a > b + c + m) }'
check $? "the ring is ink that arrives and leaves with the selection (${RING_UNSEL:-?} -> ${RING_SEL:-?} -> ${RING_DESEL:-?})"
# the app own mixWhite is round(c + (255-c)*0.45), i.e. c*0.55 + 255*0.45 = mix() with t=0.55
MIXED=$(mix "$H1" "$H2" "$H3" 255 255 255 0.55)
read -r M1 M2 M3 <<< "$MIXED"
read -r CS1 CS2 CS3 <<< "$CENTRE_SEL"
read -r CU1 CU2 CU3 <<< "$CENTRE_UNSEL"
read -r CD1 CD2 CD3 <<< "$CENTRE_DESEL"
D_MIX=$(dist "$CS1" "$CS2" "$CS3" "$M1" "$M2" "$M3")
D_HUE_SEL=$(dist "$CS1" "$CS2" "$CS3" "$H1" "$H2" "$H3")
D_ACC_C=$(dist "$CS1" "$CS2" "$CS3" "$RR" "$RG" "$RB")
awk -v a="$D_MIX" -v b="$D_HUE_SEL" 'BEGIN{ exit !(a <= 12 || b <= 12) }'
check $? "the focused key's centre is its own type colour, mixed toward the keyframe highlight at 0.45: rgb ${CENTRE_SEL} is ${D_MIX} from the mix ${MIXED} and ${D_HUE_SEL} from the plain hue ${HUE}"
awk -v a="$D_ACC_C" 'BEGIN{ exit !(a > 60) }'
check $? "and it is NOT the accent, so the ring is separate ink around the marker (${D_ACC_C} from the accent rgb ${RAIL_COLOUR})"
awk -v a="$(dist "$CU1" "$CU2" "$CU3" "$H1" "$H2" "$H3")" 'BEGIN{ exit !(a <= 6) }'
check $? "an unfocused key is drawn in its plain hue: centre rgb ${CENTRE_UNSEL} against the type hue rgb ${HUE}"
awk -v a="$(dist "$CD1" "$CD2" "$CD3" "$CU1" "$CU2" "$CU3")" 'BEGIN{ exit !(a <= 6) }'
check $? "and clearing the selection puts that exact colour back: rgb ${CENTRE_DESEL} against rgb ${CENTRE_UNSEL} ($(dist "$CD1" "$CD2" "$CD3" "$CU1" "$CU2" "$CU3") apart)"
awk -v a="$(dist "$CS1" "$CS2" "$CS3" "$CU1" "$CU2" "$CU3")" 'BEGIN{ exit !(a > 20) }'
check $? "and the focused centre differs from the plain one, so the fill change is real ($(dist "$CS1" "$CS2" "$CS3" "$CU1" "$CU2" "$CU3") apart)"

# ---- 3. the lane label ----
echo "label: expected ink rgb ${LABEL_INK_EXPECTED} (0.42 x hue ${HUE} over the band ${LANE_COLOUR}); pixels of that ink in the label box: playhead away ${LABEL_AWAY:-?} (read again ${LABEL_AWAY2:-?}), parked on the box centre ${LABEL_PILL:-?} (playhead x ${PLAYHEAD_ON_LABEL:-?}, box centre ${LBMID:-?}), away again ${LABEL_BACK:-?}"
echo "label: present with the playhead away (${LABEL_AWAY:-?}px) AND present with it parked on the label's own column (${LABEL_PILL:-?}px) - a lane's name is not the ruler's number: the pill carries the frame, nothing carries the lane's name"
[ "${LABEL_AWAY:-0}" -ge "$TEXT_MIN" ]
check $? "the lane label is drawn in its channel's hue at 0.42, with the playhead clear of it: ${LABEL_AWAY:-0} pixels of rgb ${LABEL_INK_EXPECTED} at the label box"
[ "${LABEL_AWAY2:-0}" -eq "${LABEL_AWAY:-0}" ]
check $? "and that reading is the same taken twice, so the box is a state and not a transient (${LABEL_AWAY:-?} then ${LABEL_AWAY2:-?})"
[ "${LABEL_PILL:-0}" -ge "$TEXT_MIN" ]
check $? "and the label is STILL drawn once the playhead is parked on its own column: ${LABEL_PILL:-0} pixels of that ink in the same box (the 1px playhead line crosses the text, it does not delete it)"
awk -v p="${PLAYHEAD_ON_LABEL:-none}" -v c=$((LBMID - TX0)) \
    'BEGIN{ if (p == "none" || p == "") exit 1; d = p - c; if (d < 0) d = -d; exit !(d <= 3) }'
check $? "the second measurement really parks the playhead on the label column: playhead x ${PLAYHEAD_ON_LABEL:-none} against the box centre ${LBMID} (track-local $((LBMID - TX0)))"
awk -v a="${LABEL_PILL:-0}" -v b="${LABEL_AWAY:-0}" 'BEGIN{ exit !(2*a >= b) }'
check $? "and parking the playhead there takes nothing away from the name: ${LABEL_PILL:-0} of ${LABEL_AWAY:-0} pixels of the label ink survive the line (at least half; a 1px line can only cover the glyph columns it crosses)"
[ "${LABEL_BACK:-0}" -ge "$TEXT_MIN" ]
check $? "and the label is back with the playhead away again: ${LABEL_BACK:-0} pixels"
awk -v a="${LABEL_BACK:-0}" -v b="${LABEL_AWAY:-0}" 'BEGIN{ exit !(a >= b - 4) }'
check $? "the same name reads the same after the playhead leaves (${LABEL_AWAY:-0} -> ${LABEL_BACK:-0} pixels), so the label is a constant of the lane and not a function of the playhead"

# ---- 4. the ruler number ----
echo "ruler: the number tested is the run [${NUMBER:-none}], its box [${NUMBER_BOX:-none}], the playhead parked at ${NUM_PILL_X:-?} track-local (box centre ${NMID} screen); the box modal colour and ink pixels: away [${NUM_MODAL_AWAY:-?}] ${NUM_AWAY:-?}px, parked on its tick [${NUM_MODAL_PILL:-?}] ${NUM_PILL:-?}px, 90px away [${NUM_MODAL_BACK:-?}] ${NUM_BACK:-?}px"
[ "${NUM_AWAY:-0}" -ge "$TEXT_MIN" ]
check $? "a frame number is drawn while the playhead is elsewhere: ${NUM_AWAY:-0} ink pixels on the ruler surface [${NUM_MODAL_AWAY:-?}] in the box found in the picture"
D_FROM_SURF=$(dist "$NMP1" "$NMP2" "$NMP3" "$SR" "$SG" "$SB")
awk -v a="$D_FROM_SURF" 'BEGIN{ exit !(a > 40) }'
check $? "and with the playhead parked on that tick the PILL owns the column: the box modal colour is [${NUM_MODAL_PILL:-?}], ${D_FROM_SURF} from the ruler surface [${NUM_MODAL_AWAY:-?}] (the pill accent fill, not the number ink)"
awk -v a="${NUM_PILL:-0}" -v b="${NUM_AWAY:-0}" 'BEGIN{ exit !(a > 2*b) }'
check $? "and the box then holds the pill ink and not a number on the surface: ${NUM_PILL:-0} ink pixels against ${NUM_AWAY:-0}"
[ "${NUM_BACK:-0}" -eq "${NUM_AWAY:-0}" ] && [ "${NUM_MODAL_BACK:-none}" = "${NUM_MODAL_AWAY:-none}" ]
check $? "moving the playhead 90px away restores the number exactly: ${NUM_BACK:-0} ink pixels on [${NUM_MODAL_BACK:-?}], the same reading as before (${NUM_AWAY:-0} on [${NUM_MODAL_AWAY:-?}])"

assert_rendered "$SHOTS/tink_01_rowA_sel.png" "the timeline with the selected row's rail and band"
assert_rendered "$SHOTS/tink_05_key_selected.png" "the timeline with the focused key's ring"
summary probe_timeline_ink
