#!/bin/bash
# THE PANEL'S TWO KEYS, PRESSED FOR REAL: Ctrl+A selects the shown channels' keys, Escape clears
# that selection - and NEITHER fires while another widget owns the keyboard.
#
# WHY THIS IS A PROBE AND NOT A TEST. tests/graph_drag.cpp pins what the two actions DO to the
# document (ctrl::GraphEditor::selectAllMarkers / clearSelection, called directly), and
# gui::GraphPanelWidget's binding to them is implemented behind two gates: THIS widget is the
# focus widget, and gui::ImGuiInputBridge::keyboardCaptured() is false (GraphPanelWidget.cpp,
# panelKeyAction). Neither gate, and neither key, has ever been exercised in the running
# application - the keys only exist if Qt routes them to the docked page, which is what this probe
# measures. Its subject is the BINDING: the selection it edits is the same shared set the timeline
# reads, so a pixel witness on either page is the same fact.
#
# THE PAGE HAS TO BE REPAINTED BEFORE ITS INK IS READ, and that is MEASURED, not assumed: the
# docked host draws the panel on its own events, so a key action that changes the selection changes
# 0 px of its plot (MEASURED: ctrl+a moved 0 px inside the published plot and 688 px in the dock
# header's easing chip, which names the shared selection) until something makes the page paint. The
# repaint this probe uses is ONE FRAME STEP FORWARD AND BACK - the dock connects the timeline's
# onFrameUpdated to the panel's refresh, so a frame step repaints the page, the key is not one the
# panel consumes, the focus stays where it was, and the playhead ends where it started. Two other
# routes were measured and rejected: a TAB SWITCH repaints but takes the KEYBOARD away from the
# panel (the tab bar becomes the focus widget), so the next Escape is not the panel's and the
# selection survives it - the binding working as designed and the probe getting its route wrong;
# and a POINTER MOVE into the plot and back does not repaint reliably (0px of the page changed on
# the keys, while a later Tab press flushed the same model). Every key below is therefore sent
# while the panel still holds the focus, with no click and no tab switch in between.
#
# WHY THE WITNESS IS THE PANEL'S OWN INK, AND WHAT THE INK IS. Both pages of the dock are stacked,
# so while the panel has the keyboard it is also the page on screen - the timeline's own focus
# rings cannot be seen at the same moment, and the accessibility tree publishes no selection count
# at all (buildAccessibilityModel names the selection only while it is exactly ONE key, which
# Ctrl+A never is). What the panel DOES draw for a selection, from the model's own fields:
#   * EMPHASIS: every curve whose channel owns a selected key is drawn at FULL strength, against
#     0.45 for the rest (gui::curveStrength, the design's decision table). Ctrl+A makes one
#     whole-key entry per key of every SHOWN channel (GraphEditor::selectAllMarkers), so every
#     channel the panel draws changes strength - the largest of the three effects and the one that
#     cannot be confused with chrome;
#   * the selection HULL, a 1px polyline in the marquee's edge ink around the selected markers
#     (GraphPanelModel's hull, from the same dots the markers are drawn at);
#   * its COUNT label, in the panel's label ink, in a box above the hull.
# The three are why "present after Ctrl+A" and "absent after Escape" are pixel questions: Escape
# must put the panel back to the pixel it was drawn at before, and that is asserted as an EXACT
# zero, not as a drop.
#
# AND THE NEGATIVE CONTROL, which is the half that matters: the binding is scoped to the panel, so
# an Escape sent while the OBJECT TREE owns the keyboard must not clear the selection. The probe
# clicks the tree row that is already selected (so the click itself changes no document state),
# captures the panel, sends Escape, and captures it again: the two must be identical AND the
# selection ink must still be there. A binding that fires whoever has focus would fail the second
# of those.
#
# THE ROUTE IS THE DOCK'S OWN, the one probe_graph_ink.sh and probe_graph_anchor.sh use: the dock's
# header page tabs are addressed BY NAME and the tab bar itself says which page is current (the
# current tab's rectangle is inflated to the strip - measured 120px against 60px); the dock is the
# AT-SPI rectangle of the widget named "Animation"; the panel's plot is the box it publishes for its
# own "Fit graph" action (gui::GraphPanelFrame sets that to the model's plot). The keys come from
# the application's own controls: "Add default keys" in the object tree's row menu, and the
# Properties dock's Current Keys knobs at a frame the playhead has been moved to - because a
# frame-0 marker is culled at the plot's left edge and a hull needs markers inside the plot.
#
# ONE APP INSTANCE, bounded waits that name what they waited for, clean teardown (see _lib.sh).
#
# WHAT THIS PROBE DOES NOT ASSERT: what the two actions do to the DOCUMENT (tests/graph_drag.cpp
# pins selectAllMarkers / clearSelection directly, including the whole-key composition), and the
# application's own Ctrl+A elsewhere (the timeline's own key commands are its page's business).
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/_lib.sh" "$@"

GEOM=/tmp/probe_graph_keys.txt
SCRATCH=/tmp/probe_graph_keys
STEPS=/tmp/probe_graph_keys_steps.log
INK="$ROOT/scripts/gui/ae_ink.py"
APPLOG=/tmp/probe_graph_keys_app.log
rm -f "$GEOM"
rm -rf "$SCRATCH"; mkdir -p "$SCRATCH"

# THE INK A SELECTION PUTS ON THE PANEL. MEASURED on this fixture: the two channels' curves go from
# 0.45 to full strength and the hull and its count appear, which is a large change in the plot's
# own pixels; the bar is a small fraction of it, and the run prints what it measured. The changed
# pixels must also all be INSIDE the dock: a selection glyph drawn anywhere else would not be the
# panel's.
KEYS_INK_MIN=300
# A same-state control pair must be exact: two captures with nothing forced between them.
export GEOM SCRATCH STEPS APPLOG INK KEYS_INK_MIN

xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
  set -u
  cd "$AE_PROBE_DIR"
  "$APP" "${PROJ:-}" >"$APPLOG" 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID
  : > "$STEPS"
  say() { echo "t+${SECONDS}s  $*" >> "$STEPS"; }
  abort() { printf "ABORT=\"%s\"\n" "$1" >> "$GEOM"; say "ABORT: $1"; kill $app 2>/dev/null; wait $app 2>/dev/null; exit 0; }

  wait_ready 120 || abort "the main window never came up (wait_ready timed out)"
  W=$(xdotool getactivewindow 2>/dev/null || xdotool search --name AnimeEffects | tail -1)
  xdotool windowactivate --sync $W 2>/dev/null
  sleep 0.6

  FR=$(ae extents "File")
  [ -n "$FR" ] || abort "no File menu: this is not the application window"
  read -r FX FY FW FH <<< "$FR"
  POINTX=$((FX + 3)); POINTY=$((FY + FH / 2))
  park() { xdotool mousemove --sync $POINTX $POINTY; sleep 0.5; }
  shot() { park; import -window root "$1"; }
  raw() { magick "$1" -crop "$2" +repage -depth 8 rgb:"$3"; }
  inkpx() { python3 "$INK" "$@"; }
  tab() {
    local r x y w h
    r=$(ae extents "$1")
    [ -n "$r" ] || return 1
    read -r x y w h <<< "$r"
    xdotool mousemove --sync $((x + w / 2)) $((y + h / 2)) click 1
    sleep 1.8
    return 0
  }
  cells() { ae dump 2>/dev/null | sed -n "s/^ *table cell *| //p"; }
  sel() {
    local r x y w h
    r=$(ae extents "$1" "table cell")
    [ -n "$r" ] || return 1
    read -r x y w h <<< "$r"
    xdotool mousemove --sync $((x + 4)) $((y + h / 2)) click 1
    return 0
  }
  # key <name>: send a key to whatever owns the keyboard, with no modifiers left over.
  key() { xdotool key --clearmodifiers "$1"; sleep 1.0; }

  # ---- the dock and its header ----
  DOCK=$(ae extents "Animation")
  [ -n "$DOCK" ] || abort "no visible widget named Animation: this is not the docked layout"
  read -r DX DY DW DH <<< "$DOCK"
  echo "DOCK=\"$DOCK\"" >> "$GEOM"
  say "dock $DX,$DY ${DW}x${DH}"

  # ---- the tree edge, off the timeline page own ruler hairline (probe_timeline_ink.sh) ----
  TY0=$((DY + 37 + 40)); RBARY=$((TY0 - 11))
  shot "$SHOTS/gkeys_00_timeline.png"
  raw "$SHOTS/gkeys_00_timeline.png" "${DW}x1+${DX}+$((TY0 - 1))" "$SCRATCH/hairline.raw"
  HAIRC=$(inkpx modal "$SCRATCH/hairline.raw" $DW 1 0 0 $DW 1 | cut -d" " -f1-3)
  TREE_W=0; TREE_RUN=0
  case "$HAIRC" in
    ""|none) say "row $((TY0 - 1)) has no modal colour: nothing to locate the page edge with" ;;
    *)
      read -r HC1 HC2 HC3 <<< "$HAIRC"
      inkpx cols "$SCRATCH/hairline.raw" $DW 1 0 0 "$HC1" "$HC2" "$HC3" 2 1 > "$SCRATCH/hair_rows.txt"
      while read -r a b n; do
        case "$a" in ""|none) continue ;; esac
        len=$((b - a + 1))
        [ "$len" -ge 10 ] && [ "$TREE_W" -eq 0 ] && TREE_W=$a
        [ "$len" -gt "$TREE_RUN" ] && TREE_RUN=$len
      done < "$SCRATCH/hair_rows.txt"
      ;;
  esac
  [ "$TREE_W" -gt 0 ] && [ "$TREE_RUN" -ge $((DW / 4)) ] \
      || abort "row $((TY0 - 1)) holds no run of the panel modal colour that could be the ruler bottom hairline (first long run ${TREE_W}, longest ${TREE_RUN} of ${DW}px)"
  echo "TREE_W=\"$TREE_W\"" >> "$GEOM"
  say "the panel page starts at $((DX + TREE_W))"

  # ---- keys at frame 0, through the object tree own row menu ----
  ROWS=$(cells | sed -n 1,8p)
  NROWS=$(printf "%s\n" "$ROWS" | grep -c .)
  say "tree rows: $NROWS"
  if [ "$NROWS" -lt 3 ]; then
    TOP=$(printf "%s\n" "$ROWS" | sed -n 1p)
    R=$(ae extents "$TOP" "table cell")
    [ -n "$R" ] || abort "the first tree row ($TOP) has no rectangle to expand"
    read -r EX EY EW EH <<< "$R"
    xdotool mousemove --sync $((EX - 12)) $((EY + EH / 2)) click 1
    sleep 1.5
    ROWS=$(cells | sed -n 1,8p)
    NROWS=$(printf "%s\n" "$ROWS" | grep -c .)
    say "tree rows after the expansion: $NROWS"
  fi
  [ "$NROWS" -ge 2 ] || abort "the tree publishes ${NROWS} row(s) and expanding its top node did not grow it: no row can be keyed"
  ROW_A=""; KEYSUM=""
  CAND=$(printf "%s\n" "$ROWS" | sed -n 2,8p)
  while IFS= read -r cand; do
    [ -n "$cand" ] || continue
    [ -n "$ROW_A" ] && break
    R=$(ae extents "$cand" "table cell")
    [ -n "$R" ] || continue
    read -r AX AY AW AH <<< "$R"
    sel "$cand"; sleep 0.8
    dx=$((AX + 6)); dy=$((AY + AH / 2))
    xdotool mousemove --sync $dx $dy; sleep 0.4
    xdotool click 3; sleep 1.2
    POP=$(xwininfo -root -children 2>/dev/null | grep -E "[0-9]+x[0-9]+\+${dx}\+${dy}" | grep -oE "0x[0-9a-f]+" | head -1)
    [ -n "$POP" ] || { say "candidate [$cand]: no row menu"; continue; }
    PH=$(xwininfo -id "$POP" 2>/dev/null | sed -n "s/^ *Height: *//p" | tr -d " ")
    if [ -z "$PH" ] || [ "$PH" -lt 180 ]; then
      xdotool key --clearmodifiers Escape
      say "candidate [$cand]: the menu is ${PH:-?}px tall, not the child row menu: refusing it"
      continue
    fi
    xdotool windowfocus --sync "$POP" 2>/dev/null
    xdotool key --clearmodifiers Up;     sleep 0.3
    xdotool key --clearmodifiers Up;     sleep 0.3
    xdotool key --clearmodifiers Return; sleep 2
    xdotool windowactivate --sync $W 2>/dev/null
    tab "Graph" || abort "the dock header stopped answering its Graph tab"
    ae dump > "$SCRATCH/dump_graph.txt" 2>/dev/null
    KEYSUM=$(sed -n "s/^ *label *| \([0-9]\{1,\}\) channel(s) shown.*$/\1/p" "$SCRATCH/dump_graph.txt" | head -1)
    if [ -n "$KEYSUM" ]; then ROW_A="$cand"; break; fi
    say "candidate [$cand]: the panel publishes no channel count"
    tab "Timeline" || abort "the dock header stopped answering its Timeline tab"
  done <<< "$CAND"
  [ -n "$ROW_A" ] || abort "no row Add default keys reached the panel: nothing to select"
  echo "ROW_A=\"$ROW_A\"" >> "$GEOM"
  echo "PANEL_COUNT=\"$KEYSUM\"" >> "$GEOM"
  echo "TAB_G_AT_G=\"$(ae extents "Graph")\"" >> "$GEOM"
  echo "TAB_T_AT_G=\"$(ae extents "Timeline")\"" >> "$GEOM"
  say "row [$ROW_A] is keyed: the panel publishes $KEYSUM channel(s) shown"

  # ---- keys INSIDE the plot, so the selection has markers to enclose ----
  # The knobs add a key for ONE channel at the CURRENT frame (see probe_graph_ink.sh): the playhead
  # is moved into the plot from the ruler, the two groups above Current Keys are collapsed through
  # their own titles, and Move and Rotate are pressed.
  tab "Timeline" || abort "the dock header stopped answering its Timeline tab"
  xdotool mousemove --sync $((DX + TREE_W + 150)) $RBARY click 1; sleep 1.2
  park
  for title in "Constants" "Default Keys"; do
    TR=$(ae extents "$title" "label")
    [ -n "$TR" ] || continue
    read -r TX3 TY3 TW3 TH3 <<< "$TR"
    xdotool mousemove --sync $((TX3 + TW3 / 2)) $((TY3 + TH3 / 2)) click 1
    sleep 1.2
    park
  done
  KNOBS=""
  for kn in Move Rotate; do
    KL=$(ae extents "$kn" "label")
    [ -n "$KL" ] || { KNOBS="$KNOBS ${kn}:missing"; continue; }
    read -r KKX KKY KKW KKH <<< "$KL"
    xdotool mousemove --sync $((KKX + KKW / 2)) $((KKY + KKH / 2)) click 1
    sleep 1.6
    park
    KNOBS="$KNOBS ${kn}:${KL}"
  done
  echo "KNOBS=\"$KNOBS\"" >> "$GEOM"
  say "the knobs were pressed: ${KNOBS}"

  tab "Graph" || abort "the dock header stopped answering its Graph tab"
  PLOT=$(ae extents "Fit graph")
  [ -n "$PLOT" ] || abort "the docked panel published no widget named Fit graph: no plot to click in"
  read -r PLX PLY PLW PLH <<< "$PLOT"
  echo "PLOT=\"$PLOT\"" >> "$GEOM"
  # THE FOCUS POINT IS INSIDE THE PLOT, and the press is also the panel own clear: that is how
  # this probe reaches a known state (no selection) AND the keyboard, in one gesture. MEASURED: a
  # press in the panel RULER BAND (above the plot, inside the widget) does NOT leave the panel the
  # focus widget - Ctrl+A still acted but the Escape after it did not - while a press in the plot
  # does, and the editor clears the selection on an empty marquee, which is the state the base
  # capture wants. (Why the band behaves differently is Qt focus grant on the press, not
  # something this probe can see: the probe uses the gesture that measurably works.)
  FOCUSX=$((PLX + 12)); FOCUSY=$((PLY + PLH - 12))
  echo "FOCUS=\"$FOCUSX $FOCUSY\"" >> "$GEOM"
  say "plot $PLOT; the focus point is $FOCUSX,$FOCUSY (inside the plot, and the press clears the selection)"
  # focus_panel: give the panel the keyboard without touching the selection.
  focus_panel() { xdotool mousemove --sync $FOCUSX $FOCUSY; sleep 0.3; xdotool click 1; sleep 0.6; }
  # nudge: make the page paint the document AS IT IS NOW without touching the keyboard, the
  # selection or the frame it ends on: ONE frame step forward and back. The dock connects the
  # timeline own onFrameUpdated to the panel refresh (TargetWidget), so a frame step repaints
  # this page - MEASURED by probe_graph_anchor.sh, whose frame-step readings are what checks that
  # connection - the key is not one the panel consumes, so it propagates to the application key
  # map and the FOCUS STAYS on the panel, and the step back puts the playhead where it was. A
  # pointer nudge was tried first and does NOT repaint reliably (MEASURED: 0px of the page changed
  # on the keys, while a Tab press later flushed the same model), and a tab switch would take the
  # keyboard away (MEASURED - see the header).
  nudge() {
    xdotool key --clearmodifiers ctrl+Right; sleep 0.5
    xdotool key --clearmodifiers ctrl+Left;  sleep 0.8
  }

  # ---- the state the two keys are measured against ----
  # The Escape right after the focus press clears whatever the previous gestures left selected, so
  # the base capture is a panel with NO selection - and that Escape is the first exercise of the
  # binding itself.
  # EVERY KEY IS SENT WHILE THE PANEL HOLDS THE FOCUS, and the only thing between a key and its
  # capture is a nudge - no click, so the focus is never taken away (see the header).
  focus_panel
  key Escape
  nudge
  shot "$SHOTS/gkeys_10_base.png"
  shot "$SHOTS/gkeys_11_base_control.png"
  say "base captured twice (no selection)"
  key ctrl+a
  nudge
  shot "$SHOTS/gkeys_20_ctrl_a.png"
  say "ctrl+a sent, the page nudged into repainting"
  key Escape
  nudge
  shot "$SHOTS/gkeys_30_escape.png"
  say "escape sent, the page nudged"
  key ctrl+a
  nudge
  shot "$SHOTS/gkeys_40_ctrl_a_again.png"
  say "ctrl+a sent again (the selection is up for the control below)"
  # THE KEYBOARD IS TAKEN AWAY FROM THE PANEL WITHOUT TOUCHING THE DOCUMENT: a Tab moves Qt focus
  # to the next widget in the chain, and no CLICK is made - MEASURED, a click on the object tree
  # row is NOT a valid way to do this: the application clears the key selection when the tree
  # selection changes, so the selection would be gone for a reason that has nothing to do with the
  # binding (the page came back without the selection ink, 0px against the base).
  xdotool key --clearmodifiers Tab; sleep 0.8
  key Escape
  nudge
  shot "$SHOTS/gkeys_60_offpanel_escape.png"
  say "tab moved the focus off the panel, escape sent, the page nudged"
  # ...and back, to show the key is the panel own and not a dead binding
  focus_panel
  key ctrl+a
  nudge
  shot "$SHOTS/gkeys_70_panel_again.png"
  say "the panel was focused again and ctrl+a sent"

  if kill -0 "$app" 2>/dev/null; then echo "ALIVE=\"yes\"" >> "$GEOM"; else
    echo "ALIVE=\"no\"" >> "$GEOM"
    echo "APP_TAIL=\"$(tail -2 "$APPLOG" | tr "\n" "|")\"" >> "$GEOM"
  fi
  kill $app 2>/dev/null
  wait $app 2>/dev/null
  say "body done"
' 2>&1 | tail -4
. "$GEOM" 2>/dev/null
if [ -n "${ABORT:-}" ]; then
    echo "steps: $(tr "\n" ";" < "$STEPS" 2>/dev/null)"
    check 1 "the probe reached a keyed panel through the dock own route (it stopped: ${ABORT})"
    summary probe_graph_keys
fi
if [ -z "${PLOT:-}" ] || [ -z "${PANEL_COUNT:-}" ]; then
    echo "steps: $(tr "\n" ";" < "$STEPS" 2>/dev/null)"
    check 1 "the probe reached a keyed panel with a plot (see $GEOM and $APPLOG)"
    summary probe_graph_keys
fi

read -r DX DY DW DH <<< "${DOCK:-0 0 0 0}"
read -r t2x t2y t2w t2h <<< "${TAB_T_AT_G:-0 0 0 0}"
read -r g2x g2y g2w g2h <<< "${TAB_G_AT_G:-0 0 0 0}"
aediff() { local d; d=$(magick compare -metric AE "$1" "$2" null: 2>&1); d=${d%% *}; [ -n "$d" ] || d=0; echo "$d"; }

# ---- 0. THE ROUTE, AND THE FIXTURE ----
# The tab bar says which page is current (the current tab owns the whole strip), and the panel says
# how many channels it draws: with no keys there is nothing Ctrl+A could select, and a 0px change
# below would prove nothing.
g_ok=0
[ "$g2w" -gt "$t2w" ] && g_ok=1
check "$((1 - g_ok))" "the dock's Graph tab is the current page when the keys are sent: [${TAB_G_AT_G:-none}] against Timeline [${TAB_T_AT_G:-none}] - the tab bar itself says so"
[ "${PANEL_COUNT:-0}" -ge 1 ]
check $? "and the layer the tree selected is keyed, so there is a selection for the keys to edit: the panel publishes ${PANEL_COUNT:-0} channel(s) shown (keys added through the row menu, plus Move and Rotate at a frame the plot shows)"

# ---- 1. THE PANEL'S OWN PAGE, AND THE CONTROL PAIR ----
# EVERY READING IS MADE ON THE PANEL'S PAGE: the dock's right-hand part, from the timeline's own
# left edge (the ruler hairline, see the body) to the dock's edges, below the dock's 40px header.
# The object tree and the dock header are outside it by construction - and so is the menu bar, which
# matters: MEASURED, the 792px a first version of this probe counted "outside the dock" were the
# File menu's own hover ink, which this probe's own parking produces. The panel's ink is the
# subject; nothing else is asserted about the rest of the window.
PAGEX=$((DX + TREE_W)); PAGEY=$((DY + 40))
PAGEW=$((DX + DW - PAGEX)); PAGEH=$((DY + DH - PAGEY))
pagecrop() { magick "$1" -crop "${PAGEW}x${PAGEH}+${PAGEX}+${PAGEY}" +repage "$2" 2>/dev/null; }
[ "$PAGEW" -gt 0 ] && [ "$PAGEH" -gt 0 ]
check $? "the panel's page is the dock's right-hand part, and the readings are made inside it: [${PAGEX:-?} ${PAGEY:-?} ${PAGEW:-?}x${PAGEH:-?}] inside the dock [${DOCK:-none}], with the object tree (${TREE_W:-?}px) beside it"
pagecrop "$SHOTS/gkeys_10_base.png" "$SCRATCH/base.png"
pagecrop "$SHOTS/gkeys_11_base_control.png" "$SCRATCH/base2.png"
pagecrop "$SHOTS/gkeys_20_ctrl_a.png" "$SCRATCH/ctrla.png"
pagecrop "$SHOTS/gkeys_30_escape.png" "$SCRATCH/escape.png"
pagecrop "$SHOTS/gkeys_40_ctrl_a_again.png" "$SCRATCH/again.png"
pagecrop "$SHOTS/gkeys_60_offpanel_escape.png" "$SCRATCH/offpanel.png"
pagecrop "$SHOTS/gkeys_70_panel_again.png" "$SCRATCH/back.png"
BASE_DIFF=$(aediff "$SCRATCH/base.png" "$SCRATCH/base2.png")
[ "${BASE_DIFF:-1}" -eq 0 ]
check $? "two captures of the base state, with nothing forced between them, are identical (${BASE_DIFF:-?}px of the panel's page): the readings below are states, not repaints"

# ---- 2. Ctrl+A SELECTS, AND THE PANEL DRAWS IT ----
SEL_INK=$(aediff "$SCRATCH/base.png" "$SCRATCH/ctrla.png")
WHOLE=$(aediff "$SHOTS/gkeys_10_base.png" "$SHOTS/gkeys_20_ctrl_a.png")
echo "ctrl+a: ${SEL_INK}px of the panel's page changed (${PAGEX},${PAGEY} ${PAGEW}x${PAGEH}); ${WHOLE}px of the whole screen"
[ "${SEL_INK:-0}" -ge "$KEYS_INK_MIN" ]
check $? "Ctrl+A selects the shown channels' keys and the PANEL DRAWS IT: ${SEL_INK:-0}px of the panel's page changed (bar ${KEYS_INK_MIN}) - the curves of the channels that own a selected key go from 0.45 to full strength, and the hull and its count are drawn. (The page is nudged into repainting first - one frame step forward and back - because the docked host does not repaint itself on a selection change: see the header.)"

# ---- 3. Escape CLEARS IT, TO THE PIXEL ----
ESC_DIFF=$(aediff "$SCRATCH/base.png" "$SCRATCH/escape.png")
[ "${ESC_DIFF:-1}" -eq 0 ]
check $? "Escape clears the selection and the panel goes back to the pixel it was drawn at before: ${ESC_DIFF:-?}px of the page between the base capture and the one after the Escape"

# ---- 4. THE NEGATIVE CONTROL: THE KEYBOARD IS SOMEWHERE ELSE ----
AGAIN_INK=$(aediff "$SCRATCH/base.png" "$SCRATCH/again.png")
[ "${AGAIN_INK:-0}" -ge "$KEYS_INK_MIN" ]
check $? "the selection is up again before the control (a second Ctrl+A: ${AGAIN_INK:-0}px of the page against the base, bar ${KEYS_INK_MIN})"
OFF_INK=$(aediff "$SCRATCH/base.png" "$SCRATCH/offpanel.png")
echo "off the panel: Tab, then Escape, then the page nudged -> ${OFF_INK}px of the page against the base"
[ "${OFF_INK:-0}" -ge "$KEYS_INK_MIN" ]
check $? "with the keyboard moved OFF the panel (a Tab), an Escape does NOT clear the selection: the page still shows it (${OFF_INK:-0}px against the base, bar ${KEYS_INK_MIN}), so the binding is scoped to the widget that has the keyboard"

# ---- 5. ...AND IT WORKS AGAIN IN THE PANEL ----
BACK_INK=$(aediff "$SCRATCH/base.png" "$SCRATCH/back.png")
[ "${BACK_INK:-0}" -ge "$KEYS_INK_MIN" ]
check $? "and with the keyboard back in the panel the same key selects again (${BACK_INK:-0}px of the page against the base, bar ${KEYS_INK_MIN}), so the control above is about WHO had the keyboard and not about a dead binding"
[ "${ALIVE:-no}" = "yes" ]
check $? "the application survived every key (ALIVE=${ALIVE:-?}${APP_TAIL:+, its log ended [${APP_TAIL}]})"
assert_rendered "$SHOTS/gkeys_20_ctrl_a.png" "the docked panel with the selection Ctrl+A made"
echo "steps: $(tr "\n" ";" < "$STEPS" 2>/dev/null)"
summary probe_graph_keys
