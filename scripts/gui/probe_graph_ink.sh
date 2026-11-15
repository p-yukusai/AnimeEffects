#!/bin/bash
# THE GRAPH PANEL IN THE TIMELINE DOCK, ON A LIVE APPLICATION.
#
# THE PANEL MOVED. It used to be a full-viewport Dear ImGui overlay toggled from the Tools
# dock; it is now the dock's own Graph tab (gui::TimeLineHeader's tab bar, drawn by
# gui::GraphPanelWidget at the dock's size on the dock's opaque floor, with the object tree
# still beside it). The overlay goes away in the commit after this one, so every claim here is
# made through the DOCK's own route: the tab by name, the dock's AT-SPI rectangle, and the
# dock's own pixels. Nothing here touches the old toggle and nothing here reads a viewport
# crop: the panel is not on the viewport any more.
#
# WHAT THIS PROBE PROVES, AND WHY A PROBE AT ALL. tests/graph_panel_pixels.cpp renders the
# panel from a synthetic model and reads the framebuffer back, and tests/graph_panel_model.cpp
# pins the geometry the drawing layer walks; both pass whether or not the panel is ever put on
# a real dock, for the layers the object tree actually has selected. What only a running
# application can show is that composition, and that is what is asserted here:
#
#   1. THE DOCK'S TAB IS THE ROUTE, AND THE TAB BAR ITSELF SAYS SO. The header publishes two
#      AT-SPI page tabs, "Timeline" and "Graph"; pressing one by name makes it the current one,
#      which the tabs' own rectangles report (MEASURED: the two keep their own slots - Timeline
#      at x=8, Graph at x=68 - and the CURRENT one's rectangle is inflated to the whole strip,
#      120px against the other's 60px). The probe asserts the geometry AND the model's own state,
#      so a restyled tab bar still has to be a working switch.
#
#   2. THE PANEL PUBLISHES ITSELF, BY NAME, ONLY WHILE IT DRAWS. With the Graph tab current the
#      panel is a named Pane whose name IS the model's summary ("No keys on the selected
#      layers", "N channel(s) shown", ...) and whose children are that summary as a label and
#      "Fit graph" - the panel's ONE control, the channels button, is NOT among them: it lives
#      in the dock HEADER (TimeLineHeader's own Qt button), so the panel publishes no channel
#      control of its own; the fit button appears EXACTLY ONCE in the whole tree, which is what
#      rules a second publication out (the docked host takes the panel's ONE accessibility
#      installation - gui::a11y::install is called for one widget and kept - and two hosts
#      publishing one panel is exactly the bug that would hide here). With the Timeline tab
#      current the SAME node is still in the tree,
#      unnamed and childless: the hidden page publishes the empty model instead of leaving the
#      last drawn frame in the tree.
#
#   3. THE PIXELS ARE THE DOCK'S OWN. Switching the tab changes a bounded number of pixels and
#      ALL of them are inside the Animation dock's AT-SPI rectangle (MEASURED 2026-10-19 in the
#      grown layout: 10337px of the dock's 921x310, and exactly 0px anywhere else on the screen):
#      the panel is drawn in the dock's page area, not over the screen. This is the docked form of
#      the claim the old overlay probe made with a viewport crop, and it is stronger where it
#      matters: the docked panel paints its own OPAQUE floor (theme::Colors::base;
#      GraphPanelWidget::paintGL), so what changes inside that rectangle is the panel's own ink on
#      its own surface, with no artwork composited under it.
#      THE RECTANGLE IS NOT A CONSTANT, AND THE TWO CAPTURES MUST SHARE A LAYOUT. The header is
#      TimeLineHeader, and the Graph page's own controls in it (the four quick-easing check boxes
#      and the "N channel(s)" channels button) raise the dock's MINIMUM WIDTH when its tab is
#      pressed: MEASURED, the dock is [0 185 848 310] with the Timeline tab current and
#      [0 185 921 310] with the Graph tab current, the application (at its own minimum size under
#      this harness) grows the WINDOW with it, 1168px wide and then 1241px, the neighbouring
#      Properties dock is re-laid out beside it, and neither ever shrinks back. So the two pages
#      this claim compares are captured IN ONE LAYOUT - the Graph visit happens FIRST, and both the
#      "before" and the "after" capture are taken afterwards, against the rectangle re-read in that
#      layout - rather than across that growth, where 53390px outside the dock would be the
#      application re-laying itself out and not the panel (see "THE DOCK RECTANGLE IS NOT A
#      CONSTANT" in the body for the boxes those pixels occupied).
#
#   4. THE KEYED FRAME DRAWS, AND ITS INK IS THE MODEL'S. Two channels of different key TYPES
#      are coloured differently (theme::Colors::channelColor through theme::Graph, the same
#      tokens the timeline paints keys with), the DOCK HEADER's channels control is present and
#      names the count it changes (the panel no longer draws one, and no longer paints any
#      gesture numbers either - the drag readout it used to print at the pointer is gone). The
#      keys come from the application's own routes: "Add default keys" in the object tree's row
#      menu puts ten channels at frame 0, and the Properties dock's Current Keys knobs put a key
#      of TWO key TYPES at the frame the playhead has been moved to (a frame-0 marker is culled
#      at the plot's left edge, so the knobs are what makes the plot show keys at all). The two
#      COLOURS are read off the settled frame where every shown channel is drawn - the one the
#      hover phase's marker search also reads - because on the frame the knobs key all their
#      keys sit at one value and only the selected channel's stroke is visible (see "TWO
#      CHANNELS OF DIFFERENT KEY TYPES ARE TWO COLOURS" below).
#
# WHY THAT FRAME USED TO KILL THE APPLICATION, AND WHAT NOW PINS IT (measured 2026-10-18). The
# docked host used to abort on the first frame it had to STROKE: gui::GraphPanelWidget::paintGL
# -> ImGui_ImplOpenGL3_RenderDrawData -> gui::stroke's draw callback ->
# gl::EasyShaderProgram::makeSureVBO -> gl::BufferObject::resetRawData -> GL_CHECK_ERROR ->
# Q_ASSERT(0), "OpenGL Error (code:1282)". The mechanism was that gui::stroke's pass (its VAO
# above all) was a process-global created by whichever host ran a frame first - the viewport's
# overlay - and a vertex-array object is not shared between OpenGL contexts, while
# glBindVertexArray on a name the CURRENT context never generated is GL_INVALID_OPERATION. The
# empty panel drew no stroke, which is why every claim above it measured fine. The fix (the
# overlay host deleted, one context owning the panel's GL objects, and the frame driver
# rebuilding them if another context ever turns up) is what this phase now exercises: if the
# process dies on the keyed frame again, the crash's own log tail is printed beside the failure.
#
# WHAT THIS PROBE DELIBERATELY DOES NOT ASSERT, AND WHERE THOSE CLAIMS ARE GATED INSTEAD. The
# tick ALPHAS (0.85 / 0.60 / 0.20), the selection EMPHASIS (full strength for a channel that owns
# a selected key, 0.45 for every other curve, everything dimmed with an empty selection) and the
# marker's three INKS (selected / sibling / hovered) are NOT measurable on a live screen with
# this harness, and that is a property of the screen rather than of the panel:
#   * a frame tick and the frame NUMBER that belongs to it share one column, and on this build
#     their two bands merge into ONE run in a difference, so "which band is the ticks" would be
#     a heuristic;
#   * the marker's rings are 1px strokes at 0.95 and 0.35 over its own fill, which is the same
#     pixel count at the widths involved - a 1px arc is not separable by counting ink;
#   * the panel's text ink is neutral and thin, over a floor the drawing layer may re-tint
#     between frames, which a hue count cannot difference.
# Offscreen, where nothing depends on a live window, ALL of them ARE gated:
#   * tests/graph_panel_pixels.cpp - a model whose frame ticks carry all three levels, read off
#     the rendered image (the alphas); the key-marker pair plus the stroke group's
#     dimmed/full-strength split (the emphasis, in both states); and the three marker inks, read
#     through a two-row probe because ImGui strokes a circle as a POLYLINE;
#   * tests/graph_panel_model.cpp - the published tick levels, the per-channel classification and
#     the per-key selection flags the drawing layer reads.
# See "WHAT IS NOT ASSERTED HERE" at the foot of this file, beside the assertions dropped for
# that reason.
#
# WHERE EVERY NUMBER COMES FROM. The dock's rectangle is the AT-SPI rectangle of the widget named
# "Animation" (the dock itself) - READ ONCE PER STATE, because the header's Graph-page controls
# widen the dock (and the window) when that tab is pressed: MEASURED [0 185 848 310] with the
# Timeline tab current, [0 185 921 310] with the Graph tab current, and [0 185 927 310] once the
# header's channels control reads "10 channel(s)" - which is 6px more text than "0 channel(s)";
# the tabs are addressed by name; and the panel's PAGE AREA - used
# by the keyed phase only - is the dock's right-hand part, whose left edge is the timeline's,
# measured off the ruler's bottom hairline exactly as probe_timeline_ink.sh measures it (the
# hairline spans the timeline and not the tree column, and nothing publishes the tree's width
# (MEASURED: 189px for one project's row names, 226px for another's, which is why it is measured
# rather than assumed). Widgets are addressed by name (ae_ui.py); the row menu that
# adds the keys is driven the way these probes have always driven a QMenu: a top-level popup at the
# click point, focused by id, its entries reached with Up, and a menu too short to be the child row
# menu REFUSED rather than pressed blind (the short one is the top node's, and pressing it opens
# Create layer).
#
# WHICH PROJECT THIS NEEDS. Any .anie with at least two object rows, one of them a layer: the keys
# are created by the probe itself. A project whose rows have no timeLine leaves the panel with no
# channels, and the keyed phase then fails loudly naming that instead of skipping - the crash
# above is the only thing this probe skips on.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/_lib.sh" "$@"

GEOM=/tmp/probe_graph_ink.txt
SCRATCH=/tmp/probe_graph_ink
INK="$ROOT/scripts/gui/ae_ink.py"
APPLOG=/tmp/probe_graph_ink_app.log
rm -f "$GEOM"
rm -rf "$SCRATCH"; mkdir -p "$SCRATCH"

# THE TAB SWITCH IS A REPAINT AND NOT A TRANSIENT: the panel re-reads the document when it
# becomes visible (TargetWidget::onSurfaceChanged -> GraphPanelWidget::refresh), and the diff
# between the two pages is taken from settled captures. MEASURED on this layout, BOTH pages
# captured in one window layout (the body re-reads the dock's rectangle for that reason):
# 10337px changed inside the dock's 921x310 rectangle (3.6%) with no keys yet and nothing
# selected, and 0px anywhere else on the screen - a diff taken across the layout growth the Graph
# tab itself causes measures the application instead (MEASURED: 10932px inside the 848px-wide
# rectangle the app starts in and 53390px outside it, all of it the re-laid-out window); the bar
# is a fifth of the smaller of the two inside readings.
DOCK_CHANGE_MIN=2000
# ...and it is BOUNDED: a change that swallowed the whole dock would make "the panel is in there"
# unfalsifiable, and it would mean the measurement caught a repaint of everything (MEASURED 3.6%
# and 8.7% in the two states above).
DOCK_CHANGE_SHARE=0.50
# A channel hue on the panel's floor. 20 admits the antialiased edge of a stroke (the token the
# timeline's own ink probe uses for the same colours).
HUE_TOL=20
# The floor of the chromatic-ink scan: any colour this far from the floor with this much spread is
# ink the panel drew, and every one of them is measured. A hue needing a curve's share of the plate
# (HUE_CURVE_MIN below) is what the two-colour claim uses; the insert uses ANY of them, because a
# point on any stroke will do.
HUE_MIN_PX=6
# A CURVE'S share of the panel: a channel's curve is a stroke across most of the plot and measures
# MEASURED 475..886 px at 0.45 strength (two curves of one hue when the channel has two
# components), while a hue that is only an antialiasing edge of one measures single digits. 100 is
# five times above that noise and a fifth of the smallest curve.
HUE_CURVE_MIN=100
# ...and its width: a curve spans hundreds of COLUMNS, while the playhead is ONE 1px column. A hue
# that only ever owns a handful of columns is not a channel's curve, which is how the playhead
# stays out of the pair below without hardcoding the accent.
HUE_COLS_MIN=5
# Two key TYPES are two entries of theme::Graph::keyColor, and the palette's nearest pair is
# further apart than this. The timeline's own ink probe uses the same bar for the same token.
HUE_SEP=40
# THE HOVER'S OWN BOX, AND ITS BAR. A hovered marker brightens its FILL (gui::drawPanel:
# mixedToward(curve.color, axisText, kKeyHoverFillMix), 0.4 of the way to the text ink) and that
# fill is a disk of radius ctrl::graph::kKeyDotRadius - 3.75px, so about 44px of ink - inside the
# edge stroke the pointer never touches. The box below is the 13x13 square that holds that disk,
# and the bar is under a fifth of it: a state change this phase can clear, and a bar a same-state
# control pair in the same page clears with 0px cannot fake. The reading is asked of the marker's
# OWN box and not of a share of the page, because a page-wide bar would pass on any repaint that
# had nothing to do with the marker.
HOVER_BOX_PX=13
HOVER_BOX_MIN=8
# ...and it is CLAMPED to the dock's own rectangle before it is read, because a key whose channel
# value sits at the plot's floor has its dot drawn ON the plate's last row: MEASURED, the dot this
# phase hovers is centred 1px above the dock's bottom edge, so the square around it hangs one row
# below the dock. The clamp is asserted to be at most two rows (HOVER_CLAMP_MAX), so the box is
# still the marker's own box and the reading is still the docked panel's own ink.
HOVER_CLAMP_MAX=2
# ...and the SHAPE of the run of columns that is a dot, in the panel's own ink: a dot of
# ctrl::graph::kKeyDotRadius (3.75px) is 7.5px across, so its disk is at most 8 columns wide. The
# run is asked of the hue the page ACTUALLY draws the dot in - on the frame the search reads, with
# no selection, that is toImU32Strength(curve.color, 0.45): the SAME blended colour as the curve.
# MEASURED on the current frame (2026-10-19, the plate [342 243 587 248] of the hover base capture):
# the dot at plate (28,28) owns 4 columns (27..30) holding 4 pixels of its hue each, the dot at
# (136,28) owns 2 columns holding 4 each, and every column of the two flat curves away from a dot
# holds 1 - the 2px stroke leaves one full-strength core pixel and one antialiased neighbour, and
# the neighbour is outside HUE_TOL. A run wider than 8 columns is a ruler line or the playhead
# (MEASURED: the accent hue owns the 32 columns of its frame pill), and a run of ONE column is a
# near-vertical segment of a curve or a ring's edge - neither is a dot.
DOT_RUN_MIN=2
DOT_RUN_MAX=8
# THE EMPTY-SELECTION PROOF'S OWN BAR, and it is the bar probe_graph_keys.sh sets for the same ink:
# Ctrl+A selects the shown channels' keys and the page draws it (MEASURED 14157px of the whole page
# and 7753px of the panel's page in that probe's run of these gestures). The hover phase applies
# the same two gestures to ESTABLISH that nothing is selected before it hovers a marker, because
# hover ink is given ONLY to an unselected marker (the drawing pass sets
# hoveredHere = hovered && !selected): a phase that hovered a selected key would read the control
# pair's own 0px and call a design property a regression.
SEL_INK_MIN=300
# THE HEADER VIEW BUTTON'S OWN BAR, on the panel's page. One zoom-in notch is one rung of the
# frame ladder (x1.1, ctrl::time::Scaler::kZoomPerNotch): the shared frame window shrinks about
# the playhead's frame, so every tick, number, curve sample and key marker away from the anchor
# lands on a new pixel and the plate is re-stroked - hundreds of the page's pixels. 200 is the
# floor below which the re-map would have had to do almost nothing, and the same page's
# same-state control pair (section 4) measures 0.
ZOOM_PAGE_MIN=200
# The readout's constants (RO_*) went with the readout itself: the panel draws no gesture numbers
# at the pointer any more (the drag deltas are still computed for the edit - only the printed
# readout is gone), so there is no box to measure and no bar to compare against.
export GEOM SCRATCH INK APPLOG DOCK_CHANGE_MIN DOCK_CHANGE_SHARE HUE_TOL HUE_CURVE_MIN \
       HUE_COLS_MIN HUE_SEP HUE_MIN_PX HOVER_BOX_PX HOVER_BOX_MIN ZOOM_PAGE_MIN \
       HOVER_CLAMP_MAX SEL_INK_MIN DOT_RUN_MIN DOT_RUN_MAX

xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
  set -u
  cd "$AE_PROBE_DIR"
  "$APP" "${PROJ:-}" >"$APPLOG" 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID

  abort() { printf "ABORT=\"%s\"\n" "$1" >> "$GEOM"; kill $app 2>/dev/null; wait $app 2>/dev/null; exit 0; }
  # EVERY STEP IS RECORDED AS IT HAPPENS: the body stdout is piped through tail, so a hang prints
  # nothing at all and a run that died at step 7 looks like one that never started.
  STEPS=/tmp/probe_graph_ink_steps.log
  : > "$STEPS"
  say() { echo "t+${SECONDS}s  $*" >> "$STEPS"; }
  alive() { kill -0 "$app" 2>/dev/null; }

  wait_ready 120 || abort "the main window never came up (wait_ready timed out)"
  W=$(xdotool getactivewindow 2>/dev/null || xdotool search --name AnimeEffects | tail -1)
  xdotool windowactivate --sync $W 2>/dev/null
  sleep 0.6

  raw() { magick "$1" -crop "$2" +repage -depth 8 rgb:"$3"; }
  inkpx() { python3 "$INK" "$@"; }
  # adiff <a> <b>: how many pixels differ, the reading the reporting half takes with the same tool.
  adiff() { local d; d=$(magick compare -metric AE "$1" "$2" null: 2>&1); d=${d%% *}; [ -n "$d" ] || d=0; echo "$d"; }
  # nudge: make the page paint the document AS IT IS NOW without touching the keyboard focus, the
  # selection or the frame it ends on - ONE frame step forward and back. The docked host does NOT
  # repaint itself on a selection change (MEASURED, and why probe_graph_keys.sh nudges between its
  # own Ctrl+A and its captures), while the timeline own onFrameUpdated IS connected to the refresh
  # of the panel (TargetWidget), so a frame step repaints this page; the key is not one the panel
  # consumes, so it propagates to the application key map and the FOCUS STAYS on the panel, and the
  # step back puts the playhead where it was. The HOVER phase 4 measures does NOT use this: a hover
  # that repaints itself is exactly what that phase asserts, so nudging there would paint the page
  # whether or not the marker lit and the reading would stop meaning anything.
  nudge() {
    xdotool key --clearmodifiers ctrl+Right; sleep 0.5
    xdotool key --clearmodifiers ctrl+Left;  sleep 0.8
  }
  # The pointer rests on the File menu for every capture the probe takes: the panel has hover
  # states of its own (a marker brightens, a chip appears, tangent handles highlight), so a
  # capture taken with the pointer over the plot would measure one of them.
  FR=$(ae extents "File")
  [ -n "$FR" ] || abort "no File menu: this is not the application window"
  read -r FX FY FW FH <<< "$FR"
  POINTX=$((FX + 3)); POINTY=$((FY + FH / 2))
  park() { xdotool mousemove --sync $POINTX $POINTY; sleep 0.5; }
  shot() { park; import -window root "$1"; }
  # settle_shot <png>: capture AT THE POINTER THE CALLER PUT IT (shot() parks first, this one
  # does not, because a hover capture must be taken with the pointer still on its target) and
  # repeat until two captures in a row agree, so a frame caught mid-repaint is never the reading
  # a phase is decided on. Bounded, and false when the page never settles - a page that keeps
  # repainting under a stationary pointer is itself worth failing on rather than measuring.
  settle_shot() {
    local i=0 d
    while [ "$i" -lt 10 ]; do
      import -window root "$1"
      if [ "$i" -gt 0 ]; then
        d=$(magick compare -metric AE "$SCRATCH/settle_prev.png" "$1" null: 2>&1); d=${d%% *}
        if [ "${d:-x}" = "0" ]; then cp "$1" "$SCRATCH/settle_prev.png"; return 0; fi
      fi
      cp "$1" "$SCRATCH/settle_prev.png"
      i=$((i + 1)); sleep 0.4
    done
    return 1
  }
  # sel <row name>: AT THE ROW LEFT EDGE, never its reported centre - the cell rectangles the tree
  # publishes are WIDER THAN THE TREE, so a click at the centre lands beside the row.
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
  # tab <name>: press the dock header page tab BY NAME, from its own rectangle.
  tab() {
    local r x y w h
    r=$(ae extents "$1")
    [ -n "$r" ] || return 1
    read -r x y w h <<< "$r"
    xdotool mousemove --sync $((x + w / 2)) $((y + h / 2)) click 1
    sleep 1.8
    return 0
  }

  # ---- the dock, and the two tabs its header publishes ----
  DOCK0=$(ae extents "Animation")
  [ -n "$DOCK0" ] || abort "no visible widget named Animation: this is not the docked layout, so there is no dock to measure the panel in"
  read -r DX DY DW DH <<< "$DOCK0"
  echo "DOCK_START=\"$DOCK0\"" >> "$GEOM"
  say "dock $DX,$DY ${DW}x${DH} (the layout the application starts in)"

  echo "TAB_T_AT_TL=\"$(ae extents "Timeline")\"" >> "$GEOM"
  echo "TAB_G_AT_TL=\"$(ae extents "Graph")\"" >> "$GEOM"

  # ---- THE DOCK RECTANGLE IS NOT A CONSTANT: THE GRAPH PAGE WIDENS IT ----
  # The header is TimeLineHeader, and the Graph page own controls in it (the four quick-easing
  # check boxes and the "N channel(s)" channels button) raise the MINIMUM WIDTH of the dock, so
  # pressing the dock own Graph tab grows the dock - and the application is at its own minimum size
  # under this harness, so it grows the WINDOW with it. MEASURED here, in the tree and in the pixels:
  # the dock is [0 185 848 310] with the Timeline tab current and [0 185 921 310] with the Graph tab
  # current, the window is 1168px wide and then 1241px (the same +73px), the neighbouring
  # Properties dock (320px wide in both states) is re-laid out beside it, the viewport above the
  # dock widens with the column (which re-lays out the dashed guide of the canvas by 2..4px), and
  # the application does NOT shrink any of it back when the Timeline tab returns: a SECOND Timeline
  # capture is 57997px away from the first, and every one of those pixels is that re-laid-out
  # window - MEASURED against the first capture, the changed pixels sit in boxes x 1164..1240
  # y 0..495 (the content of the Properties dock itself, moved), x 847..920 y 21..430 (the strip the
  # dock gained) and x 411..847 y 21..183 (the dashed guide of the canvas, re-laid out inside the
  # wider viewport: 604px, all of it neutral canvas colours), and the Properties dock pixels are the
  # SAME pixels translated by 73px (0px of difference when the two crops are compared with that
  # shift).
  # THAT IS WHY THE TWO PAGES THE PIXEL CLAIM COMPARES ARE CAPTURED AFTER THIS FIRST GRAPH VISIT, in
  # the one layout they share, and why the rectangle the claim crops and masks with is re-read here
  # instead of being taken from the reading above: a diff between a page captured at 848px and one
  # captured at 921px measures the application re-laying itself out, not the panel.
  tab "Graph" || abort "the dock header has no pressable tab named Graph: the panel cannot be shown by the dock own route"
  echo "TAB_T_AT_G=\"$(ae extents "Timeline")\"" >> "$GEOM"
  echo "TAB_G_AT_G=\"$(ae extents "Graph")\"" >> "$GEOM"
  DOCKG=$(ae extents "Animation")
  WING=$(ae extents "AnimeEffects")
  echo "DOCK_G=\"$DOCKG\"" >> "$GEOM"
  echo "WIN_G=\"$WING\"" >> "$GEOM"
  shot "$SHOTS/gink_20_dock_graph.png"
  ae dump > "$SCRATCH/dump_graph.txt" 2>/dev/null
  say "graph state captured; the dock is [$DOCKG] now"

  # ...and back to the timeline, WHERE THE LAYOUT STAYS AS THE GRAPH PAGE LEFT IT: the pair the
  # pixel claim is built from is these captures of the two pages at ONE window width, and the control
  # is the same state captured twice. The dock own rectangle is read again here to show it did not
  # move between the two pages - the reporting half asserts the two readings are equal.
  tab "Timeline" || abort "the dock header stopped answering its Timeline tab"
  DOCKT=$(ae extents "Animation")
  echo "DOCK_T=\"$DOCKT\"" >> "$GEOM"
  shot "$SHOTS/gink_10_dock_timeline.png"
  shot "$SHOTS/gink_11_dock_timeline_b.png"
  shot "$SHOTS/gink_12_dock_timeline_c.png"
  ae dump > "$SCRATCH/dump_timeline.txt" 2>/dev/null
  say "timeline state captured twice (the control pair), in the same layout as the Graph capture: dock [$DOCKT]"
  DOCK="$DOCKG"
  echo "DOCK=\"$DOCK\"" >> "$GEOM"
  read -r DX DY DW DH <<< "$DOCK"

  # ---- the tree edge, which is where the panel page starts ----
  # The ruler bottom hairline spans the timeline and NOT the tree column, so the first long run of
  # the panel modal colour in row TY0-1 IS the timeline x range (probe_timeline_ink.sh measures it
  # this way; the tree is MEASURED as 189px of this dock while older probes assume 72). The capture
  # below is the GROWN layout, which is the layout every phase after this one measures in: the
  # tree column redistributes with the dock, MEASURED 243px of the 848px dock the application starts
  # in and 264px of the 921px one both captures of the pixel claim are taken at (the splitter line
  # is at x=263 there, and at x=265 in the 927px dock the keys make - 2px, inside the 13px box this
  # phase reads and well inside the page crop below).
  TY0=$((DY + 37 + 40))
  raw "$SHOTS/gink_10_dock_timeline.png" "${DW}x1+${DX}+$((TY0 - 1))" "$SCRATCH/hairline.raw"
  HAIRC=$(inkpx modal "$SCRATCH/hairline.raw" $DW 1 0 0 $DW 1 | cut -d" " -f1-3)
  case "$HAIRC" in ""|none) abort "row $((TY0 - 1)) has no modal colour: nothing to locate the tree edge with" ;; esac
  read -r HC1 HC2 HC3 <<< "$HAIRC"
  inkpx cols "$SCRATCH/hairline.raw" $DW 1 0 0 $HC1 $HC2 $HC3 2 1 > "$SCRATCH/hair_rows.txt"
  TREE_W=0; TLL=0
  while read -r a b n; do
    case "$a" in ""|none) continue ;; esac
    if [ $((b - a + 1)) -ge 10 ] && [ "$TREE_W" -eq 0 ]; then TREE_W=$a; fi
    if [ $((b - a + 1)) -gt "$TLL" ]; then TLL=$((b - a + 1)); fi
  done < "$SCRATCH/hair_rows.txt"
  [ "$TREE_W" -gt 0 ] && [ "$TLL" -ge $((DW / 4)) ] || abort "row $((TY0 - 1)) holds no run of the panel modal colour rgb ${HAIRC} that could be the ruler bottom hairline (first long run at ${TREE_W}, longest ${TLL} of ${DW}px)"
  echo "TREE_W=\"$TREE_W\"" >> "$GEOM"
  say "the object tree is ${TREE_W}px of the dock; the panel page starts there"

  # ---- keys, through the object tree own row menu ----
  # THE FIRST TREE ROW IS THE TOP NODE AND ITS LAYERS STAY INVISIBLE UNTIL IT IS EXPANDED; the only
  # expander is the branch indicator at (rowX - 12, rowY + rowH/2) on that row own rectangle, and
  # it is a TOGGLE. It is clicked ONCE and the row count is asserted to grow.
  cells() { ae dump 2>/dev/null | sed -n "s/^ *table cell *| //p"; }
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
  echo "ROWS=\"$(printf "%s" "$ROWS" | tr "\n" ";")\"" >> "$GEOM"
  [ "$NROWS" -ge 2 ] || abort "the tree publishes ${NROWS} row(s) and expanding its top node did not grow it: the row menu offers Add default keys only for a node WITH a parent, so no row can be keyed"
  # EVERY CHILD ROW IS TRIED IN TURN, because Add default keys does nothing at all on a FOLDER
  # (obj::Item::cast refuses it, the menu closes and the layer stays keyless): the loop stops at
  # the first row whose keys reach the panel, and switches back to the timeline for the next one.
  # The top node is skipped: it answers the row menu with no Add default keys at all, so pressing
  # the entries by position on it would open Create layer instead.
  CAND=$(printf "%s\n" "$ROWS" | sed -n 2,8p)
  say "candidate child rows: $(printf "%s" "$CAND" | tr "\n" ";")"
  ROW_A=""; KEYSUM=""; CBTN=""; CRASH=""; CRASHROW=""
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
    # a menu that is not there is a REFUSAL, not a failure of the run: a probe that presses entries
    # blind on a menu it cannot see is how Create layer gets opened.
    if [ -z "$POP" ]; then
      say "candidate [$cand]: no row menu opened at $dx,$dy"
      continue
    fi
    PH=$(xwininfo -id "$POP" 2>/dev/null | sed -n "s/^ *Height: *//p" | tr -d " ")
    # the child row menu MEASURED 223px tall; a menu without the parent-gated entries is far
    # shorter, and pressing ITS entries blind is how a probe opens Create layer.
    if [ -z "$PH" ] || [ "$PH" -lt 180 ]; then
      xdotool key --clearmodifiers Escape
      say "candidate [$cand]: the row menu is ${PH:-?}px tall, not the child row menu: refusing it"
      continue
    fi
    echo "POP=\"$POP ${PH}px\"" >> "$GEOM"
    xdotool windowfocus --sync "$POP" 2>/dev/null
    xdotool key --clearmodifiers Up;     sleep 0.3
    xdotool key --clearmodifiers Up;     sleep 0.3
    xdotool key --clearmodifiers Return; sleep 2
    xdotool windowactivate --sync $W 2>/dev/null
    say "Add default keys pressed on row [$cand]"
    # ---- the keyed frame: the repaint the docked host has to survive ----
    # THE CRASH LANDS IN A PAINT, NOT IN THE CLICK, so the state is given time to settle and the
    # application is checked on both sides of the capture and of the dump: on this tree the docked
    # host aborts a second or two after the switch (see THE BLOCKED CLAIMS), and a probe that read
    # the tree in between would report "no channel count" for a frame that was never drawn.
    tab "Graph" || abort "the dock header stopped answering its Graph tab after the keys were added"
    sleep 3
    if ! alive; then CRASH="yes"; CRASHROW="$cand"; ROW_A="$cand"; break; fi
    shot "$SHOTS/gink_30_dock_graph_keys.png"
    if ! alive; then CRASH="yes"; CRASHROW="$cand"; ROW_A="$cand"; break; fi
    ae dump > "$SCRATCH/dump_graph_keys.txt" 2>/dev/null
    if ! alive; then CRASH="yes"; CRASHROW="$cand"; ROW_A="$cand"; break; fi
    KEYSUM=$(sed -n "s/^ *label *| \([0-9]\{1,\}\) channel(s) shown.*$/\1/p" "$SCRATCH/dump_graph_keys.txt" | head -1)
    # The "N channel(s)" PUSH BUTTON of the whole tree: the dock header own control (the panel
    # publishes none - see the channels claim in the reporting half). Read here so the geometry
    # below is that control own rectangle, not a guess.
    CBTN=$(sed -n "s/^ *button *| \([0-9]\{1,\} channel(s)\)$/\1/p" "$SCRATCH/dump_graph_keys.txt" | head -1)
    say "candidate [$cand]: the model publishes [${KEYSUM:-nothing}] channel(s) shown, the dock header control reads [${CBTN:-none}]"
    if [ -n "${KEYSUM:-}" ]; then
      ROW_A="$cand"
      echo "PANEL_COUNT=\"${KEYSUM}\"" >> "$GEOM"
      echo "CHBTN=\"${CBTN:-}\"" >> "$GEOM"
      [ -n "${CBTN:-}" ] && echo "CHBTN_RECT=\"$(ae extents "${CBTN}")\"" >> "$GEOM"
      break
    fi
    tab "Timeline" || abort "the dock header stopped answering its Timeline tab between candidates"
  done <<< "$CAND"
  echo "ROW_A=\"$ROW_A\"" >> "$GEOM"
  if [ -n "$CRASH" ]; then
    echo "CRASH=\"yes\"" >> "$GEOM"
    echo "CRASH_ROW=\"$CRASHROW\"" >> "$GEOM"
    echo "CRASH_TAIL=\"$(tail -2 "$APPLOG" | tr "\n" "|")\"" >> "$GEOM"
    say "the application DIED on row [$CRASHROW]"
  fi
  if [ -n "${KEYSUM:-}" ] && [ -z "$CRASH" ]; then
      # ---- A SECOND KEY SET, AT A FRAME THE PLOT SHOWS ----
      # EVERY key the row menu adds is at frame 0, and the plot maps the timeline window, which
      # starts at frame 1: a frame-0 marker lands at the plot LEFT EDGE and is culled, so the key
      # set alone publishes no marker to drag. The dock own Current Keys knobs add a key for ONE
      # channel at the CURRENT frame: the playhead is moved into the plot (a click on the ruler at
      # TL+150) and the Move and Rotate knobs are pressed, which is TWO DIFFERENT KEY TYPES at a
      # frame the plot shows. The knobs sit below the fold of the Properties dock, so the two
      # groups above them are collapsed first, through their own title labels.
      tab "Timeline" || abort "the dock header stopped answering its Timeline tab before the knobs"
      RULER_X=$((DX + TREE_W + 150)); RULER_Y=$((DY + 37 + 40 - 11))
      xdotool mousemove --sync $RULER_X $RULER_Y click 1; sleep 1.2
      park
      say "the playhead was set from the ruler at $RULER_X"
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
        if [ -z "$KL" ]; then KNOBS="$KNOBS ${kn}:missing"; continue; fi
        read -r KKX KKY KKW KKH <<< "$KL"
        xdotool mousemove --sync $((KKX + KKW / 2)) $((KKY + KKH / 2)) click 1
        sleep 1.6
        park
        KNOBS="$KNOBS ${kn}:${KL}"
        say "the $kn knob was pressed at $KL"
      done
      echo "KNOBS=\"$KNOBS\"" >> "$GEOM"

      tab "Graph" || abort "the dock header stopped answering its Graph tab after the knobs"
      sleep 1.5
      shot "$SHOTS/gink_30_dock_graph_keys.png"
      if ! alive; then
        echo "CRASH=\"yes\"" >> "$GEOM"
        echo "CRASH_TAIL=\"$(tail -2 "$APPLOG" | tr "\n" "|")\"" >> "$GEOM"
        say "the application DIED on the keyed frame"
      fi
      # THE DOCK RECTANGLE MOVES ONCE MORE WHEN THE HEADER CHANNELS CONTROL NAMES A COUNT: the
      # "N channel(s)" button is text, so it is 6px wider reading "10 channel(s)" than "0
      # channel(s)", and MEASURED the dock (and the window with it) is [0 185 927 310] here against
      # [0 185 921 310] before the keys existed. The page rect below is measured with THAT reading,
      # so the page this phase scans and the zoom phase reads is the page of the panel itself rather
      # than the still narrower strip the starting rectangle of the application would give.
      DOCKK=$(ae extents "Animation")
      echo "DOCK_K=\"$DOCKK\"" >> "$GEOM"
      read -r KX KY KW KH <<< "$DOCKK"
      say "the keyed page is up: the dock is [$DOCKK]"

      # ---- the panel PAGE, its floor, and the channel hues on it ----
      PX2=$((KX + TREE_W)); PY2=$((KY + 40))
      PW2=$((KX + KW - PX2)); PH2=$((KY + KH - PY2))
      echo "PANELRECT=\"$PX2 $PY2 $PW2 $PH2\"" >> "$GEOM"
      raw "$SHOTS/gink_30_dock_graph_keys.png" "${PW2}x${PH2}+${PX2}+${PY2}" "$SCRATCH/panel.raw"
      FLOOR=$(inkpx modal "$SCRATCH/panel.raw" $PW2 $PH2 0 0 $PW2 $PH2 | cut -d" " -f1-3)
      echo "PANEL_FLOOR=\"$FLOOR\"" >> "$GEOM"
      read -r F1 F2 F3 F4 <<< "$FLOOR"
      # A CHANNEL HUE IS CHROMATIC INK ON THE PANEL OWN FLOOR: the floor is one opaque colour, so
      # the only chromatic ink on it is a channel hue (theme::Graph::keyColor) and the accent the
      # playhead and the selection read as. The tree column is outside this crop by construction,
      # so its accent-tinted row band cannot be read as a channel colour. The distance bar is 60
      # and not the 150 a full-strength marker would need: with nothing selected every curve is
      # drawn at 0.45, i.e. HALF its token distance from the floor.
      # EVERY chromatic ink on the floor is measured once: its pixel count, the columns it owns and
      # the LEFTMOST of them. Which HUE a channel is drawn in is the application business (it is
      # theme::Colors::channelColor(type, component) - an axis hue where the channel has one - and
      # this probe never states it), so the probe reads whatever it finds.
      # THIS LIST IS THE KEYED FRAME INVENTORY, NOT THE PAIR THE TWO-COLOUR CLAIM COMPARES.
      # On this frame the keys the knobs wrote sit at one value (both knobs write 0), so the
      # channels they key draw ONE coincident flat stroke on the bottom row of the plot, and the
      # channel whose key the knobs left SELECTED is drawn at full strength over the others
      # (GraphPanelFrame curveStrength). MEASURED on the run that exposed this: the only two
      # qualifying hues are that selected channel rotateKey purple at full strength
      # (192 132 252, 531px over 533 columns) and the playhead accent (167 139 250: its 1px line
      # plus the 32-column pill, 578px over 32 columns) - 26 apart, and NEITHER pair there is two
      # channels, because the page draws one channel hue on that frame. The two-colour claim is
      # therefore read from the PLATE OWN list (hues_plate.txt), the settled no-selection page
      # the marker search reads, where every shown channel is drawn at 0.45 and at its own value:
      # see the claim itself.
      # The marker search below measures that same frame for the same reason - the selection
      # gestures change the strength every curve and every unselected marker is drawn at.
      inkpx inkhues "$SCRATCH/panel.raw" $PW2 $PH2 0 0 $PW2 $PH2 "$F1" "$F2" "$F3" 60 40 "$HUE_MIN_PX" > "$SCRATCH/hues_raw.txt"
      : > "$SCRATCH/hues.txt"
      : > "$SCRATCH/hues_all.txt"
      while read -r r g b n; do
        case "$r" in ""|none) continue ;; esac
        inkpx cols "$SCRATCH/panel.raw" $PW2 $PH2 0 $((PH2 - 1)) "$r" "$g" "$b" $HUE_TOL 1 > "$SCRATCH/hue_runs.txt"
        ncols=0; left=-1
        while read -r a bb nn; do
          case "$a" in ""|none) continue ;; esac
          ncols=$((ncols + (bb - a + 1)))
          if [ "$left" -lt 0 ] || [ "$a" -lt "$left" ]; then left=$a; fi
        done < "$SCRATCH/hue_runs.txt"
        # the playhead is ONE column wide, and the tree column is not in this crop: a hue that owns
        # two or more columns is ink the panel drew inside its plot
        [ "$ncols" -ge 2 ] || continue
        printf "%s %s %s %s %s %s\n" "$r" "$g" "$b" "$n" "$ncols" "$left" >> "$SCRATCH/hues_all.txt"
        if [ "$n" -ge "$HUE_CURVE_MIN" ] && [ "$ncols" -ge "$HUE_COLS_MIN" ]; then
          printf "%s %s %s %s %s\n" "$r" "$g" "$b" "$n" "$ncols" >> "$SCRATCH/hues.txt"
        fi
      done < "$SCRATCH/hues_raw.txt"
      head -6 "$SCRATCH/hues.txt" > "$SCRATCH/hues_u.txt"
      head -8 "$SCRATCH/hues_all.txt" > "$SCRATCH/hues_cand.txt"
      echo "PANEL_HUES=\"$(tr "\n" ";" < "$SCRATCH/hues_u.txt")\"" >> "$GEOM"
      echo "PANEL_HUES_ALL=\"$(tr "\n" ";" < "$SCRATCH/hues_cand.txt")\"" >> "$GEOM"
      say "chromatic hues on the keyed frame: those with a curve share of ink [$(tr "\n" ";" < "$SCRATCH/hues_u.txt")], every chromatic hue owning 2+ columns [$(tr "\n" ";" < "$SCRATCH/hues_cand.txt")]"

      # ---- PHASE 4: A HOVER REPAINTS THE KEY MARKER IT LIGHTS ----
      # WHAT A REGRESSION LOOKS LIKE: the pointer sits on a key dot and NOTHING is clicked, the
      # marker never lights on the page, and the box this phase reads stays at the control pair
      # 0px - which is exactly what GraphPanelWidget::updateHoverRepaint not asking for a repaint
      # when the hovered target identity changes leaves behind.
      #
      # FIRST: THE MARKER THIS PHASE HOVERS MUST BE UNSELECTED, AND THE PHASE ESTABLISHES THAT
      # BEFORE IT HOVERS ANYTHING. Hover ink is given ONLY to an unselected marker - the drawing
      # pass sets hoveredHere = hovered && !selected - so a selected key changes NO ink when the
      # pointer sits on it (MEASURED 2026-10-19: a phase that hovered one read 0px of the own box of
      # the marker while the SAME page changed 792px elsewhere, all of it the parking ink of the
      # File menu, and its return-to-pre-hover check passed at 0px: nothing about the marker had
      # changed). The state is therefore made empty of any selection and the emptiness is PROVED,
      # with the two gestures probe_graph_keys.sh measured for exactly this binding and this panel:
      #   * a press inside the plot gives the panel the keyboard AND clears the selection (the editor
      #     clears on an empty marquee; a press in the ruler band of the panel does NOT leave the
      #     panel the focus widget - MEASURED in that probe, which is why it presses this point);
      #   * Ctrl+A selects every key of the shown channels and the page DRAWS it (the bar this phase
      #     reads below), and the Escape that follows must put the page back to the pixel it had
      #     BEFORE the Ctrl+A - an exact zero, the rule probe_graph_keys.sh applies to the same
      #     binding.
      # Both gestures are followed by a NUDGE (one frame step forward and back): the docked host does
      # not repaint itself on a selection change, so without it the captures below would show the page
      # as it was before the key was sent (MEASURED, and why probe_graph_keys.sh nudges exactly the
      # same way). The HOVER itself is not nudged: a hover that asks for its own repaint is the thing
      # this phase exists to measure.
      # Only then is the page a no-selection page, and the marker the search below finds - whichever
      # marker that is, of whichever channel - is an UNSELECTED one. Both readings are printed in the
      # "selection:" line below and both are asserted below it, so a run that cannot establish the
      # empty selection fails loudly instead of measuring a hover that never had ink to give.
      PLOT=$(ae extents "Fit graph")
      [ -n "$PLOT" ] || abort "the docked panel published no Fit graph box while its Graph page is up: there is no measured plate to find a key marker in"
      read -r PLX PLY PLW PLH <<< "$PLOT"
      echo "PLOT=\"$PLOT\"" >> "$GEOM"
      FOCUSX=$((PLX + 12)); FOCUSY=$((PLY + PLH - 12))
      xdotool mousemove --sync $FOCUSX $FOCUSY; sleep 0.3
      xdotool click 1; sleep 0.8
      xdotool key --clearmodifiers Escape; sleep 0.4
      nudge
      park
      settle_shot "$SCRATCH/hov_clear.png" || abort "the panel never settled after the press that gives it the keyboard and clears the key selection"
      xdotool key --clearmodifiers ctrl+a; nudge; park
      settle_shot "$SCRATCH/hov_selected.png" || abort "the panel never settled after Ctrl+A selected the shown channels keys"
      xdotool key --clearmodifiers Escape; nudge; park
      settle_shot "$SCRATCH/hov_escaped.png" || abort "the panel never settled after the Escape that clears the selection"
      SEL_INK=$(adiff "$SCRATCH/hov_clear.png" "$SCRATCH/hov_selected.png")
      ESC_RET=$(adiff "$SCRATCH/hov_clear.png" "$SCRATCH/hov_escaped.png")
      echo "SEL_INK=\"$SEL_INK\"" >> "$GEOM"
      echo "ESC_RET=\"$ESC_RET\"" >> "$GEOM"
      say "the selection was made (Ctrl+A: ${SEL_INK}px) and cleared (Escape: ${ESC_RET}px against the page before the Ctrl+A)"
      shot "$SHOTS/gink_40_hover_base.png"
      shot "$SHOTS/gink_40b_hover_control.png"

      # ---- THE MARKER IS FOUND IN THE INK OF THE PANEL ITSELF ----
      # WHAT A MARKER IS DRAWN AS ON THIS FRAME, and why the ink is measured HERE instead of reused:
      # with nothing selected every curve is stroked at 0.45 (kUnselectedAlpha), and a marker that is
      # neither selected nor the sibling of a selected key has the ink of an unselected one - its
      # fill is toImU32Strength(curve.color, 0.45), the SAME blended colour as the curve, inside a
      # keyEdge() stroke that is the floor colour. A SELECTED marker is a different ink (the fill
      # mixed 0.45 toward axisText, plus a 1px hue ring at kKeyDotRadius + 2.6), and the phase has
      # just PROVED the selection empty (SEL_INK/ESC_RET above), so no ring and no brightened fill
      # exist on the frame this search reads. THE HUE LIST MUST COME FROM THIS FRAME: the scan above
      # ran on the keyed page BEFORE the selection gestures, when the page was drawn in another
      # state - MEASURED 2026-10-19, it names 172 163 72 and 178 131 221, and `cols` finds ZERO
      # columns of either on the plate of this frame, which is the abort this section fixes.
      # A key marker is where the ink of its channel is DEEPER than its curve: gui::stroke::kWidthPx
      # is 2px, and at 0.45 strength a column of a flat curve holds ONE full-strength pixel of the
      # blended hue (the antialiased neighbour is outside HUE_TOL), while the 3.75px dot adds a
      # disk. MEASURED on this frame: the columns the dot owns hold 4 of the hue, the curve beside
      # it holds 1. So the columns holding MORE THAN THE STROKE are the columns of the dot - the
      # `cols` mode of ae_ink returns them as x-runs of columns with at least 3 pixels of the hue -
      # and the ROW of the dot is the centre of mass of that hue in a 13-column strip about them
      # (`centroid`). The two things a run has to be are the two things a disk is: BOUNDED (a run
      # wider than the 7.5px disk is the playhead pill or a ruler line, and a run of one column is a
      # near-vertical segment of a curve or a ring edge, so neither is a dot) and a CHANNEL hue
      # (from the scan of this very plate, which keeps hues with a CURVE share of it - the marker
      # fill and the curve are one ink here, so the dot hue is in that list by construction). The
      # row is MEASURED and not computed from the first and last inked ROW of the column, because
      # that span mixes the top frame line of the plate with the dot: MEASURED, every column of the
      # plate is inked at row 0 and at the floor of the plot, so a span midpoint put the pointer 123
      # rows above the dot (empty floor) and the hover it then measured lit nothing.
      # THE PROOF THAT A RUN IS A MARKER AND NOT THE CURVE is two-part and both parts are asserted:
      # the run is a compact 2..8-column body of one channel hue whose columns are deeper than the
      # stroke (above), and the phase then moves the pointer ONTO it and reads a repaint of the
      # 13x13 box around it - hover ink exists for a key dot and for nothing on a curve, so a run
      # that was a steep curve segment reads 0 in that box and fails the HOVER_BOX_MIN check below.
      # WHY THE MODEL ROUTE IS NOT USED HERE (the "Key menu" node probe_graph_keys.sh can read):
      # that node is published ONLY while the selection names exactly ONE key, and its rectangle is
      # the dot of that key (buildAccessibilityModel, kPanelKeyMenuItemId). Selecting a key to learn
      # its dot would make the marker SELECTED, and a selected marker takes NO hover ink (hoveredHere
      # = hovered && !selected) - the state this phase must measure is the opposite one. The pixel
      # route is therefore the PRIMARY: it finds an UNSELECTED marker in the very ink the hover
      # changes, and the only selection gesture it needs is the one that proves the selection empty.
      raw "$SHOTS/gink_40_hover_base.png" "${PLW}x${PLH}+${PLX}+${PLY}" "$SCRATCH/plate.raw"
      PLFLOOR=$(inkpx modal "$SCRATCH/plate.raw" $PLW $PLH 0 0 $PLW $PLH | cut -d" " -f1-3)
      read -r PF1 PF2 PF3 <<< "$PLFLOOR"
      inkpx inkhues "$SCRATCH/plate.raw" $PLW $PLH 0 0 $PLW $PLH "$PF1" "$PF2" "$PF3" 60 40 "$HUE_MIN_PX" > "$SCRATCH/plate_hues_raw.txt"
      : > "$SCRATCH/hues_plate.txt"
      while read -r r g b n; do
        case "$r" in ""|none) continue ;; esac
        inkpx cols "$SCRATCH/plate.raw" $PLW $PLH 0 $((PLH - 1)) "$r" "$g" "$b" $HUE_TOL 1 > "$SCRATCH/plate_hue_runs.txt"
        ncols=0
        while read -r a bb nn; do
          case "$a" in ""|none) continue ;; esac
          ncols=$((ncols + (bb - a + 1)))
        done < "$SCRATCH/plate_hue_runs.txt"
        [ "$ncols" -ge "$HUE_COLS_MIN" ] || continue
        [ "$n" -ge "$HUE_CURVE_MIN" ] || continue
        printf "%s %s %s %s %s\n" "$r" "$g" "$b" "$n" "$ncols" >> "$SCRATCH/hues_plate.txt"
      done < "$SCRATCH/plate_hues_raw.txt"
      echo "PLATE_FLOOR=\"$PLFLOOR\"" >> "$GEOM"
      echo "PLATE_HUES=\"$(tr "\n" ";" < "$SCRATCH/hues_plate.txt")\"" >> "$GEOM"
      say "chromatic hues on the plate the marker search reads (floor [$PLFLOOR]): [$(tr "\n" ";" < "$SCRATCH/hues_plate.txt")]"
      HKX=""; HKY=""; HKINK=0; HKRUN=""
      while read -r hr hg hb hn hcols; do
        [ -n "${hr:-}" ] || continue
        inkpx cols "$SCRATCH/plate.raw" $PLW $PLH 0 $((PLH - 1)) "$hr" "$hg" "$hb" $HUE_TOL 3 > "$SCRATCH/dot_runs.txt"
        while read -r rx0 rx1 rn; do
          case "$rx0" in ""|none) continue ;; esac
          rw=$((rx1 - rx0 + 1))
          { [ "$rw" -ge "$DOT_RUN_MIN" ] && [ "$rw" -le "$DOT_RUN_MAX" ]; } || continue
          rcx=$(( (rx0 + rx1) / 2 ))
          read -r ccx ccy ccn <<< "$(inkpx centroid "$SCRATCH/plate.raw" $PLW $PLH $((rcx - HOVER_BOX_PX / 2)) 0 $HOVER_BOX_PX $PLH "$hr" "$hg" "$hb" $HUE_TOL)"
          case "${ccx:-none}" in none|"") continue ;; esac
          [ "${ccn:-0}" -gt "$HKINK" ] || continue
          HKINK=$ccn; HKX=$((PLX + ccx)); HKY=$((PLY + ccy)); HKRUN="$hr $hg $hb $rx0 $rx1 $rw $rn"
        done < "$SCRATCH/dot_runs.txt"
      done < "$SCRATCH/hues_plate.txt"
      [ -n "$HKX" ] || abort "no key marker ink was found on the panel plate [${PLOT:-none}] (floor [$PLFLOOR]): a dot is a run of ${DOT_RUN_MIN}..${DOT_RUN_MAX} columns of a channel hue each holding 3 or more pixels of it (more than the 1 full-strength pixel the 2px stroke of the curve leaves at 0.45), and this frame draws none of the plate hues [$(tr "\n" ";" < "$SCRATCH/hues_plate.txt")] - the keys the knobs made may lie outside the plate window, no marker was inked, or the ink of a marker is not one of the hues this plate scan found"
      # THE BOX IS THE 13x13 SQUARE AROUND THE DOT, CLAMPED TO THE DOCK RECTANGLE: a key whose
      # channel value sits at the floor of the plot has its dot drawn ON the last row of the plate,
      # so the square around it can hang a row or two below the dock - and this phase reads the ink
      # the DOCKED panel drew, so it clamps. The reporting half asserts the clamp moved it by at most
      # HOVER_CLAMP_MAX, which is what keeps the box the box of the marker itself.
      HRBX=$((HKX - HOVER_BOX_PX / 2)); HRBY=$((HKY - HOVER_BOX_PX / 2))
      HBX=$HRBX; HBY=$HRBY
      [ "$HBX" -lt "$KX" ] && HBX=$KX
      [ $((HBX + HOVER_BOX_PX)) -gt $((KX + KW)) ] && HBX=$((KX + KW - HOVER_BOX_PX))
      [ "$HBY" -lt "$KY" ] && HBY=$KY
      [ $((HBY + HOVER_BOX_PX)) -gt $((KY + KH)) ] && HBY=$((KY + KH - HOVER_BOX_PX))
      echo "HOVER_AT=\"$HKX $HKY\"" >> "$GEOM"
      echo "HOVER_RAW=\"$HRBX $HRBY $HOVER_BOX_PX $HOVER_BOX_PX\"" >> "$GEOM"
      echo "HOVER_BOX=\"$HBX $HBY $HOVER_BOX_PX $HOVER_BOX_PX\"" >> "$GEOM"
      echo "HOVER_INK=\"$HKINK\"" >> "$GEOM"
      echo "HOVER_RUN=\"$HKRUN\"" >> "$GEOM"
      read -r HKHR HKG HKB HKR0 HKR1 HKRW HKRT <<< "$HKRUN"
      say "key marker at $HKX,$HKY: a ${HKRW}-column run (x ${HKR0}..${HKR1}) of the channel hue [$HKHR $HKG $HKB] on the plate, ${HKRT}px of it in the run and ${HKINK}px in the 13-column strip the dot is centred in - the curve beside it leaves 1 full-strength pixel per column - so the pointer moves onto it with NO click"
      xdotool mousemove --sync $HKX $HKY
      settle_shot "$SHOTS/gink_41_hover.png" || abort "the docked panel never settled with the pointer on the key marker at $HKX,$HKY"
      park
      settle_shot "$SHOTS/gink_42_hover_off.png" || abort "the docked panel never settled with the pointer parked off every key"
      say "the hover was captured, and so was the page with the pointer off every key"

      # ---- PHASE 5: THE DOCK HEADER VIEW BUTTON REPAINTS THE PAGE IT DRIVES ----
      # WHAT A REGRESSION LOOKS LIKE: the header zoom-in moves the timeline own view (the button
      # works, the ruler and the lanes move) while the docked panel keeps the frame window it had,
      # so the panel own page changes by 0px - which is what MainDisplayWidget::
      # syncGraphPanelSharedState no longer refreshing the Graph page on a view-range change
      # leaves behind. The button is pressed BY NAME, through the header own accessibility
      # publication and its own rectangle.
      ZR=$(ae extents "Zoom in")
      [ -n "$ZR" ] || abort "the dock header publishes no button named Zoom in (TimeLineHeader own view button, tooltip Zoom in): the header view action cannot be pressed by name"
      echo "ZOOM_BTN=\"$ZR\"" >> "$GEOM"
      read -r ZBX ZBY ZBW ZBH <<< "$ZR"
      xdotool mousemove --sync $((ZBX + ZBW / 2)) $((ZBY + ZBH / 2)) click 1
      park
      settle_shot "$SHOTS/gink_50_zoom_after.png" || abort "the docked panel never settled after the header zoom-in was pressed"
      say "the header zoom-in was pressed at [$ZR] and the page captured again"
  fi

  say "body done"
  kill $app 2>/dev/null
  wait $app 2>/dev/null
' 2>&1 | tail -6

. "$GEOM" 2>/dev/null
if [ -n "${ABORT:-}" ]; then
    check 1 "$ABORT"
    summary probe_graph_ink
fi
if [ -z "${DOCK:-}" ] || [ -z "${TAB_T_AT_TL:-}" ] || [ -z "${TREE_W:-}" ]; then
    check 1 "the probe body produced no geometry (see $GEOM and $APPLOG)"
    summary probe_graph_ink
fi

read -r DX DY DW DH <<< "${DOCK:-0 0 0 0}"
DOCK_AREA=$((DW * DH))
TABLOGS="$SCRATCH/dump_timeline.txt"
TABGLOG="$SCRATCH/dump_graph.txt"
KEYSLOG="$SCRATCH/dump_graph_keys.txt"

# The panel Pane, out of a dump, by the ONE thing that identifies it: the "Fit graph" button it
# owns. Prints "<indent>|<name>" for the pane whose button that was, so the name can be read
# beside the indent that says WHICH node it is.
panelnode() { # panelnode <dump>
    awk '
      /^ *panel *\|/ { pind = match($0, /[^ ]/) - 1; pname = $0; sub(/^ *panel *\| */, "", pname) }
      /^ *button *\| Fit graph$/ { printf "%d|%s\n", pind, pname; exit }
    ' "$1"
}
# The panels inside the dock: the dump lines of the Animation subtree, which is where both the
# panel Pane and the tabs live. NAMED panes are counted beside the unnamed ones, because the
# hidden page publishes an EMPTY model - the node stays, its name and children do not.
anim_panes() { # anim_panes <dump> -> "<indent> <name-or-empty>" per line
    awk '
      !inb { if ($0 ~ /^ *panel +\| Animation$/) { inb = 1; base = match($0, /[^ ]/) - 1 } ; next }
      /^ *$/ { next }
      { ind = match($0, /[^ ]/) - 1; if (ind <= base) exit }
      /^ *panel *\|/ { nm = $0; sub(/^ *panel *\| */, "", nm); printf "%d %s\n", ind, nm }
    ' "$1"
}
aediff() { local d; d=$(magick compare -metric AE "$1" "$2" null: 2>&1); d=${d%% *}; [ -n "$d" ] || d=0; echo "$d"; }
# topcols <capture> <WxH+X+Y>: the commonest colours inside a box, most frequent first.
# A failing pixel reading has to name the INK it found, not only its size: this is how the
# hover phase's return diff says which drawing pass was left changed.
topcols() {
    magick "$1" -crop "$2" +repage -format %c histogram:info:- 2>/dev/null |
        sort -rn | head -4 |
        sed -E 's/^ *([0-9]+): .*#([0-9A-Fa-f]{6}).*/#\2 x\1/' | paste -sd' ' -
}
dist() { awk -v a1="$1" -v a2="$2" -v a3="$3" -v b1="$4" -v b2="$5" -v b3="$6" \
        'BEGIN{d1=a1-b1; d2=a2-b2; d3=a3-b3; printf "%.1f", sqrt(d1*d1+d2*d2+d3*d3)}'; }

# ---- 1. THE DOCK'S OWN ROUTE ----
# WHICH TAB IS CURRENT IS READABLE FROM THE RECTANGLES THE TAB BAR PUBLISHES, and that is what
# makes the switch measurable rather than assumed. MEASURED on this tab bar: the two tabs keep
# their own slots (Timeline at x=8, Graph at x=68) whatever is current, and the CURRENT tab's
# rectangle is inflated to the whole strip - 120px, the union of both slots - while the other
# keeps its own 60px. So "the current tab is the wide one" reads the index straight off the tree,
# in both directions.
read -r t1x t1y t1w t1h <<< "${TAB_T_AT_TL:-0 0 0 0}"
read -r g1x g1y g1w g1h <<< "${TAB_G_AT_TL:-0 0 0 0}"
read -r t2x t2y t2w t2h <<< "${TAB_T_AT_G:-0 0 0 0}"
read -r g2x g2y g2w g2h <<< "${TAB_G_AT_G:-0 0 0 0}"
awk -v x="$t2x" -v X="$g1x" 'BEGIN{ exit !(x < X) }'
check $? "the dock header's two page tabs keep their own slots whatever is current - Timeline in the left one (x ${t2x:-?} while Graph is current) and Graph in the right (x ${g1x:-?} while Timeline is): the probe presses the control it names"
awk -v w="$t1w" -v W="$g1w" 'BEGIN{ exit !(w > W) }'
check $? "with the Timeline tab current, the Timeline tab is the one whose rectangle spans the strip: [${TAB_T_AT_TL:-none}] against Graph [${TAB_G_AT_TL:-none}]"
awk -v w="$g2w" -v W="$t2w" 'BEGIN{ exit !(w > W) }'
check $? "and pressing the tab BY NAME made the Graph tab the current one: [${TAB_G_AT_G:-none}] against Timeline [${TAB_T_AT_G:-none}] - the tab bar itself says which page the dock is showing"
awk -v x="$t1x" -v y="$t1y" -v w="$t1w" -v h="$t1h" -v X="$DX" -v Y="$DY" -v W="$DW" -v H="$DH" \
    'BEGIN{ exit !(x >= X && y >= Y && x + w <= X + W && y + h <= Y + H) }'
check $? "and both tabs live inside the dock's own AT-SPI rectangle [${DOCK:-none}] - the header whose page the probe then measures"

# THE PANEL, NAMED, AND PUBLISHED ONCE. "Fit graph" appearing twice in the tree would mean two
# hosts publishing one panel, which is exactly what the docked host takes the ONE installation to
# prevent.
PN=$(panelnode "$TABGLOG")
PANEL_IND=${PN%%|*}
PANEL_NAME=${PN#*|}
FIT_G_ALL=$(grep -c "^ *button *| Fit graph$" "$TABGLOG" 2>/dev/null) || FIT_G_ALL=0
FIT_T_ALL=$(grep -c "^ *button *| Fit graph$" "$TABLOGS" 2>/dev/null) || FIT_T_ALL=0
G_NAMED=$(anim_panes "$TABGLOG" | grep -cE "^[0-9]+ .")
T_NAMED=$(anim_panes "$TABLOGS" | grep -cE "^[0-9]+ .")
T_IND=$(anim_panes "$TABLOGS" | sed -n "s/^\([0-9]*\) *$/\1/p" | head -1)
echo "tree: with the Graph tab up the dock holds ${G_NAMED} named panel pane(s), with the Timeline tab ${T_NAMED}; the panel node is [${PANEL_NAME:-none}] at indent ${PANEL_IND:-?}, and the unnamed pane the timeline state holds is at indent ${T_IND:-?}"
[ "${FIT_G_ALL:-0}" -eq 1 ]
check $? "the panel publishes \"Fit graph\" EXACTLY ONCE in the whole accessibility tree while its tab is up (${FIT_G_ALL:-0} occurrence(s)): one panel, one publication"
[ -n "${PANEL_NAME:-}" ] && [ "${PANEL_NAME:-}" != "unknown" ]
check $? "and the Pane that owns it is NAMED with the model's own summary rather than left anonymous: [${PANEL_NAME:-none}]"
case "${PANEL_NAME:-}" in
    *"channel(s) shown"*|"No keys on the selected layers"|"Select a layer to show its curves"|"All channels of the selected layers are hidden"|"No room for the graph in this viewport")
        ok=0 ;;
    *) ok=1 ;;
esac
check "$ok" "and that name is one of the model's own sentences, not a leftover label ([${PANEL_NAME:-none}])"
if [ "${FIT_T_ALL:-0}" -eq 0 ]; then ok=0; else ok=1; fi
check "$ok" "with the TIMELINE tab current the same panel publishes NOTHING of its own: \"Fit graph\" appears ${FIT_T_ALL:-0} time(s) in that dump, because the hidden page publishes the EMPTY model"
if [ "${T_NAMED:-1}" -eq 0 ] && [ "${G_NAMED:-0}" -eq 1 ] && [ "${T_IND:-x}" = "${PANEL_IND:-y}" ]; then ok=0; else ok=1; fi
check "$ok" "and the panel's Pane is still in the tree, UNNAMED and at the SAME position, while its page is hidden (${T_NAMED:-?} named pane(s) then against ${G_NAMED:-?} now; indent ${T_IND:-?} against ${PANEL_IND:-?})"
if grep -q "gui::a11y: the viewport was queried for accessibility before install" "$APPLOG" 2>/dev/null; then ok=1; else ok=0; fi
check "$ok" "and the application never warned that its viewport was queried for accessibility before the factory was installed - the ordering the panel's ONE publication depends on (gui::a11y::adoptViewportInterface)"

# ---- 2. THE DOCK'S OWN PIXELS ----
# INSIDE, on the two tab states; OUTSIDE, on the same two captures with the dock's rectangle
# blanked in both, so the number is a measurement rather than a subtraction; and the CONTROL pair,
# which is what rules a repaint out.
# THE RECTANGLE IS THE ONE BOTH CAPTURES WERE TAKEN IN. It is NOT the rectangle the application
# starts in: the dock's own header carries the Graph page's controls (the quick-easing check boxes
# and the "N channel(s)" channels button), so the first press of the dock's own Graph tab raises the
# dock's minimum width and the application - at its own minimum size under this harness - grows the
# WINDOW with it, and neither comes back when the Timeline tab returns. MEASURED on these captures:
# [0 185 848 310] and a 1168px window before that visit, [0 185 921 310] and a 1241px window after
# it. Masking the STARTING rectangle against two captures of the grown layout is what leaves 53390px
# "outside" - and those pixels are the layout, not ink: their boxes are x 1164..1240 y 0..495 (the
# content of the neighbouring Properties dock, MOVED: the same pixels, 0px of difference against the
# Timeline capture's neighbour when the two crops are compared with that 73px shift), x 847..920
# y 21..430 (the width the dock gained) and x 411..847 y 21..183 (the dashed guide of the canvas,
# re-laid out inside the wider viewport: 604px, all of it neutral canvas colours). So the probe reads
# the rectangle once per state in the body and asserts here that the two pages it compares were
# captured in ONE layout.
read -r _ _ s0w _ <<< "${DOCK_START:-0 0 0 0}"
read -r _ _ gw _ <<< "${DOCK_G:-0 0 0 0}"
[ -n "${DOCK_T:-}" ] && [ "$DOCK_T" = "$DOCK_G" ]
check $? "the two pages the pixel claim compares were captured in ONE window layout: the dock's own rectangle is [${DOCK_G:-none}] with the Graph page up and [${DOCK_T:-none}] with the Timeline page up. The application grows that rectangle from [${DOCK_START:-none}] to [${DOCK_G:-none}] - $((gw - s0w))px - the FIRST time its Graph tab is pressed, and it does not shrink it back (the window it grows with it is [${WIN_G:-none}])"
magick "${SHOTS}/gink_10_dock_timeline.png" "${SHOTS}/gink_20_dock_graph.png" \
    -crop "${DW}x${DH}+${DX}+${DY}" +repage "${SCRATCH}/dock_%d.png" 2>/dev/null
magick "${SHOTS}/gink_10_dock_timeline.png" -fill black -draw "rectangle ${DX},${DY} $((DX + DW - 1)),$((DY + DH - 1))" "${SCRATCH}/tl_masked.png"
magick "${SHOTS}/gink_20_dock_graph.png" -fill black -draw "rectangle ${DX},${DY} $((DX + DW - 1)),$((DY + DH - 1))" "${SCRATCH}/gr_masked.png"
DOCK_DIFF=$(aediff "$SCRATCH/dock_0.png" "$SCRATCH/dock_1.png")
OUTSIDE_DIFF=$(aediff "$SCRATCH/tl_masked.png" "$SCRATCH/gr_masked.png")
CONTROL_DIFF=$(aediff "${SHOTS}/gink_11_dock_timeline_b.png" "${SHOTS}/gink_12_dock_timeline_c.png")
# the bounding box of what is left outside, printed whether or not the check below clears: if the
# panel ever paints over the window, this is the box and the region that has to be named.
OUTSIDE_BOX=""
[ "${OUTSIDE_DIFF:-0}" -eq 0 ] || OUTSIDE_BOX=$(magick "$SCRATCH/tl_masked.png" "$SCRATCH/gr_masked.png" -compose difference -composite -threshold 0 -format "%@" info: 2>/dev/null)
SHARE=$(awk -v a="$DOCK_DIFF" -v b="$DOCK_AREA" 'BEGIN{ printf "%.3f", (b > 0) ? a / b : 0 }')
echo "pixels: the Graph tab against the Timeline tab changes ${DOCK_DIFF}px inside the dock (${SHARE} of its ${DOCK_AREA}px, both captures in the grown layout [${DOCK:-none}]) and ${OUTSIDE_DIFF}px outside it (their bounding box ${OUTSIDE_BOX:-none}); the same state captured twice differs by ${CONTROL_DIFF}px (the tree is ${TREE_W:-?}px of the dock, so the panel's page is the other $((DW - ${TREE_W:-0}))px)"
[ "${CONTROL_DIFF:-1}" -eq 0 ]
check $? "the control pair is the SAME state twice: ${CONTROL_DIFF:-?}px differ between two captures taken with nothing forced between them, so the change below is the tab switch and not a repaint"
[ "${DOCK_DIFF:-0}" -ge "$DOCK_CHANGE_MIN" ]
check $? "switching the dock's tab to Graph repaints a substantial part of the dock's own rectangle: ${DOCK_DIFF:-0}px changed inside [${DOCK:-none}] (bar ${DOCK_CHANGE_MIN})"
[ "${OUTSIDE_DIFF:-1}" -eq 0 ]
check $? "and NOTHING changes outside that rectangle: ${OUTSIDE_DIFF:-?}px differ elsewhere on the screen (their bounding box is ${OUTSIDE_BOX:-none}), so the panel is the dock's PAGE and not an overlay over the window"
awk -v s="$SHARE" -v m="$DOCK_CHANGE_SHARE" 'BEGIN{ exit !(s <= m) }'
check $? "and the change is BOUNDED - ink on the dock's page, not a repaint of the whole dock: share ${SHARE} <= ${DOCK_CHANGE_SHARE}"
assert_rendered "${SHOTS}/gink_20_dock_graph.png" "the docked panel's page with the Graph tab up"

# ---- 3. THE KEYED CLAIMS ----
# The keys and the two key TYPES are all made by the application's own controls, so the
# routes are asserted beside the ink: which row was keyed and which two knobs were pressed. The
# crash's own log tail is printed with any failure below, because a host that dies drawing the
# keyed frame is the defect this phase exists to catch.
KNOB_MOVE=""; KNOB_ROT=""
for pair in ${KNOBS:-}; do
    case "$pair" in
        Move:*) KNOB_MOVE=${pair#Move:} ;;
        Rotate:*) KNOB_ROT=${pair#Rotate:} ;;
    esac
done
if [ -n "${CRASH:-}" ]; then
    echo "crash: the application died on the keyed frame; its log ended [${CRASH_TAIL:-none}]"
fi
[ -z "${CRASH:-}" ]
check $? "the docked host drew a keyed frame WITHOUT DYING${CRASH:+ (its log ended [${CRASH_TAIL:-none}]; this is the stroke-pass defect the one-context fix removed)}"
[ -n "${ROW_A:-}" ] && [ -n "${PANEL_COUNT:-}" ]
check $? "the keys the object tree's own row menu added to [${ROW_A:-none}] reached the panel's model: it publishes [${PANEL_COUNT:-nothing}] channel(s) shown"
[ -n "${KNOB_MOVE:-}" ] && [ -n "${KNOB_ROT:-}" ]
check $? "and the Properties dock's CURRENT KEYS knobs put a key of TWO different types at the frame the playhead was moved to: Move [${KNOB_MOVE:-missing}] and Rotate [${KNOB_ROT:-missing}]"
# THE PANEL'S ONE CONTROL IS NOT THE PANEL'S ANY MORE. The design moved it into the dock HEADER
# (TimeLineHeader's own Qt button), and the panel's accessibility publication carries no channel
# control at all: the ONE "N channel(s)" push button in the whole tree is therefore the header's
# own. The dump cannot tell the two apart by name - both read "N channel(s)" - so the count is
# what separates them: the panel publishing one as well would make this 2, and the rectangle check
# below places the survivor in the dock's header row rather than in the panel's page.
CHBTN_LINES=$(grep -c "^ *button *| [0-9]\{1,\} channel(s)$" "$KEYSLOG" 2>/dev/null) || CHBTN_LINES=0
[ "${CHBTN_LINES:-0}" -eq 1 ]
check $? "the panel publishes NO channels control of its own: the tree holds exactly ONE \"N channel(s)\" button (${CHBTN_LINES:-0}), and it is the DOCK HEADER's ([${CHBTN:-none}])"
# THE DOCK'S RECTANGLE WITH THE KEYED PAGE UP: the header's channels control reads "10 channel(s)"
# by now, which is 6px more text than "0 channel(s)", so the dock this phase's rectangle is read
# from is [0 185 927 310] and not the 921px one the tab-switch captures are taken at.
read -r KX2 KY2 KW2 KH2 <<< "${DOCK_K:-${DOCK:-0 0 0 0}}"
read -r CX2 CY2 CW2 CH2 <<< "${CHBTN_RECT:-0 0 0 0}"
awk -v x="$CX2" -v y="$CY2" -v w="$CW2" -v h="$CH2" -v X="$KX2" -v Y="$KY2" -v W="$KW2" -v H="$KH2" \
    'BEGIN{ exit !(x >= X && y >= Y && x + w <= X + W && y + h <= Y + H) }'
check $? "and that header control is a real one inside the dock's own rectangle ([${CHBTN_RECT:-none}] in [${DOCK_K:-none}])"
echo "panel: page area [${PANELRECT:-none}], floor [${PANEL_FLOOR:-none}]; the channel hues on the keyed frame (rgb px columns): [${PANEL_HUES:-none}]; on the settled no-selection frame the marker search reads: [${PLATE_HUES:-none}]"
# ---- TWO CHANNELS OF DIFFERENT KEY TYPES ARE TWO COLOURS ----
# The pair is chosen from the hues a frame of this keyed layer is DRAWN with, not from a list of
# tokens, and it must be two CHANNELS: a hue that covers a curve's share of ink (HUE_CURVE_MIN px)
# over a curve's share of columns (HUE_COLS_MIN), and the two HUE_SEP apart. Measured in both
# directions below.
# WHY THE PLATE'S LIST AND NOT THE KEYED CAPTURE'S. On the keyed frame the knobs' keys sit at one
# value, so the channels they key draw ONE coincident stroke on the plot's bottom row, and the
# selected channel's stroke (full strength) covers the others (0.45) - MEASURED there: the purple
# rotateKey hue at 531px over 533 columns and the playhead's accent at 578px over 32 columns, 26
# apart. That list holds no two channel hues at all, so the old pair loop compared the accent with
# ONE channel and found no pair (the failure this section fixes). The plate is the same keyed
# layer's page after the phase has established an EMPTY selection (SEL_INK/ESC_RET below), where
# every shown channel is drawn at 0.45 and at its own value - a frame that really carries two
# channels' curves, which is the property this check names. None of the bars moves: the same
# HUE_CURVE_MIN, HUE_COLS_MIN and HUE_SEP judge it.
HA=""; HB=""; HD=""
while read -r r g b n cols; do
    [ -n "${r:-}" ] || continue
    if [ -z "$HA" ]; then HA="$r $g $b $n $cols"; continue; fi
    [ -n "$HB" ] && continue
    D=$(dist "$r" "$g" "$b" $(printf "%s" "$HA" | cut -d" " -f1-3))
    awk -v a="$D" -v m="$HUE_SEP" 'BEGIN{ exit !(a >= m) }' || continue
    HB="$r $g $b $n $cols"; HD="$D"
done < "$SCRATCH/hues_plate.txt"
NH=$(grep -c . "$SCRATCH/hues_plate.txt" 2>/dev/null) || NH=0
read -r A1 A2 A3 A4 A5 <<< "${HA:-0 0 0 0 0}"
read -r B1 B2 B3 B4 B5 <<< "${HB:-0 0 0 0 0}"
# THE TWO HUES AND THE DISTANCE THE CHECKS COMPARE, printed before them so neither reading is
# inferred from a PASS/FAIL line alone.
echo "two hues: rgb ${A1:-?},${A2:-?},${A3:-?} at ${A4:-0}px over ${A5:-0} columns against rgb ${B1:-?},${B2:-?},${B3:-?} at ${B4:-0}px over ${B5:-0} columns - ${HD:-unmeasured} apart (bar ${HUE_SEP}); ${NH:-0} qualifying hue(s) on the plate [${PLATE_HUES:-none}]"
[ "${NH:-0}" -ge 2 ]
check $? "the keyed layer shows at least two CHANNELS on the plate the marker search reads - the settled page with the selection proved empty, where every shown channel is drawn as a stroke on the panel's own floor: ${NH:-0} qualifying hue(s) ([${PLATE_HUES:-none}]; the keyed frame itself draws one channel hue plus the playhead's accent, see the two-colour note)"
[ -n "${B1:-}" ] && [ "${B4:-0}" -ge "$HUE_CURVE_MIN" ] && [ "${A4:-0}" -ge "$HUE_CURVE_MIN" ]
check $? "and the two of them are different colours, each with a curve's share of ink (rgb ${A1:-?},${A2:-?},${A3:-?} at ${A4:-0}px over ${A5:-0} columns; rgb ${B1:-?},${B2:-?},${B3:-?} at ${B4:-0}px over ${B5:-0} columns)"
awk -v a="${HD:-0}" -v m="$HUE_SEP" 'BEGIN{ exit !(a >= m) }'
check $? "and two different CHANNELS carry different colours: the two are ${HD:-?} apart (bar ${HUE_SEP}), so the panel does not paint every channel of this layer the same. WHICH hue a channel gets is the application business - theme::Colors::channelColor(type, component), an axis hue where the channel has one and its type hue otherwise - and this probe never states it: it measures the hue the panel actually drew"

# ---- 4. A HOVER REPAINTS THE KEY MARKER IT LIGHTS ----
# A REGRESSION LOOKS LIKE this: the pointer sits on a key's dot with NO click and the marker never
# lights, because GraphPanelWidget::updateHoverRepaint stopped asking for a repaint when the
# hovered target's identity changes. The reading is then the control pair's 0px. The bar is set
# from the ink the marker itself owns - its fill is a disk of radius ctrl::graph::kKeyDotRadius
# (3.75px, about 44px of ink) taken 0.4 of the way to the text ink by kKeyHoverFillMix - and it is
# asked of the marker's OWN box rather than of a share of the page, so a page-wide repaint that
# left the marker alone cannot clear it. A missing capture reads 0 through magick compare and
# fails the box check, and the capture's existence and non-blackness are asserted beside it.
# AND IT IS ASKED OF A MARKER THAT IS PROVABLY UNSELECTED, which is why the two readings below come
# first: hover ink is given ONLY to an unselected marker, so the phase empties the selection with
# the panel's own gestures and proves it emptied - Ctrl+A selects the shown channels' keys and the
# page draws it, and the Escape after it returns the page to the pixel it had before the Ctrl+A.
# The BAR for the selection's own ink is the one probe_graph_keys.sh sets for the same ink on the
# same panel (MEASURED 14157px of the whole page there). A run that cannot establish the empty
# selection FAILS HERE, before the box reading, rather than measuring a state in which the marker it
# found could never light.
HOVER_BASE="${SHOTS}/gink_40_hover_base.png"
HOVER_CTRL="${SHOTS}/gink_40b_hover_control.png"
HOVER_ON="${SHOTS}/gink_41_hover.png"
HOVER_OFF="${SHOTS}/gink_42_hover_off.png"
read -r HBX HBY HBW HBH <<< "${HOVER_BOX:-0 0 0 0}"
read -r RBX RBY _ _ <<< "${HOVER_RAW:-0 0 0 0}"
echo "selection: Ctrl+A on the keyed page drew ${SEL_INK:-?}px of the screen, and the Escape after it returned the page to the pixel it had before the Ctrl+A (${ESC_RET:-?}px) - the state the marker below is hovered in has no key selected, which is the only state in which hover ink exists (the drawing pass sets hoveredHere = hovered && !selected)"
[ "${SEL_INK:-0}" -ge "$SEL_INK_MIN" ]
check $? "the phase's own precondition is measurably a state: Ctrl+A selected the shown channels' keys and the page DREW it - ${SEL_INK:-0}px of the screen changed (bar ${SEL_INK_MIN}) - so there are keys to hover at all"
[ "${ESC_RET:-1}" -eq 0 ]
check $? "and the Escape that followed cleared that selection back to the pixel: ${ESC_RET:-?}px between the page before the Ctrl+A and the page after the Escape (an exact zero, the rule probe_graph_keys.sh applies to the same binding). THIS is how the phase knows the marker it is about to hover is UNSELECTED - a selected marker legitimately gives a hover NO ink to change, and hovering one would read the control pair's 0px and call a design property a regression (MEASURED 2026-10-19: exactly that, 0px of the box while the same page changed 792px elsewhere and the return check passed at 0px)"
assert_exists "$HOVER_BASE" "the panel's page captured before the hover"
assert_rendered "$HOVER_BASE" "the panel's page captured before the hover"
assert_exists "$HOVER_CTRL" "the panel's page captured again, for the same-state pair"
assert_exists "$HOVER_ON" "the panel's page with the pointer ON a key's dot"
assert_rendered "$HOVER_ON" "the panel's page with the pointer ON a key's dot"
assert_exists "$HOVER_OFF" "the panel's page with the pointer back off every key"
assert_rendered "$HOVER_OFF" "the panel's page with the pointer back off every key"
[ -n "${HOVER_BOX:-}" ] && [ -n "${HOVER_AT:-}" ]
check $? "the probe hovers a marker it FOUND in the panel's own ink rather than a guessed point: a dot read off the plate at [${HOVER_AT:-none}] as a run of columns holding MORE of a channel hue than its curve's stroke leaves in a column (the plate's own hues were measured on the capture this search reads: [${PLATE_HUES:-none}], floor [${PLATE_FLOOR:-none}]), measured as the box [${HOVER_BOX:-none}] around it"
read -r HR1 HR2 HR3 HRX0 HRX1 HRW HRT <<< "${HOVER_RUN:-0 0 0 0 0 0 0}"
[ "${HRW:-0}" -ge "$DOT_RUN_MIN" ] && [ "${HRW:-0}" -le "$DOT_RUN_MAX" ] && [ "${HOVER_INK:-0}" -gt "$HOVER_BOX_PX" ]
check $? "and the run it was read from is a MARKER and not the curve: rgb (${HR1:-?} ${HR2:-?} ${HR3:-?}) owns columns ${HRX0:-?}..${HRX1:-?} (${HRW:-0}-wide, bar ${DOT_RUN_MIN}..${DOT_RUN_MAX}) holding ${HRT:-0}px of the hue - 3 or more pixels per column, where the 2px stroke of a flat curve leaves exactly 1 full-strength pixel per column (MEASURED on this frame) - and its 13-column strip holds ${HOVER_INK:-?}px, more than the one-per-column a stroke can leave there (bar ${HOVER_BOX_PX})"
awk -v x="$HBX" -v y="$HBY" -v w="$HBW" -v h="$HBH" -v X="$KX2" -v Y="$KY2" -v W="$KW2" -v H="$KH2" \
    'BEGIN{ exit !(x >= X && y >= Y && x + w <= X + W && y + h <= Y + H) }'
check $? "and that box lies inside the dock's own rectangle [${DOCK_K:-none}], so the ink it measures is the docked panel's own"
awk -v a="$HBX" -v A="$RBX" -v b="$HBY" -v B="$RBY" -v m="$HOVER_CLAMP_MAX" \
    'BEGIN{ d1=a-A; d2=b-B; if (d1<0) d1=-d1; if (d2<0) d2=-d2; exit !(d1 <= m && d2 <= m) }'
check $? "and the box is still the marker's OWN box: it is the 13x13 square around the dot [${HOVER_RAW:-none}], clamped to the dock's rectangle by at most ${HOVER_CLAMP_MAX} rows/columns - a key whose channel value sits at the plot's floor is drawn ON the plate's last row, so its square hangs below the dock (MEASURED: 1 row)"
magick "$HOVER_BASE" -crop "${HBW}x${HBH}+${HBX}+${HBY}" +repage "$SCRATCH/hov_base.png" 2>/dev/null
magick "$HOVER_ON" -crop "${HBW}x${HBH}+${HBX}+${HBY}" +repage "$SCRATCH/hov_on.png" 2>/dev/null
HOVER_BOX_DIFF=$(aediff "$SCRATCH/hov_base.png" "$SCRATCH/hov_on.png")
HOVER_PAGE_DIFF=$(aediff "$HOVER_BASE" "$HOVER_ON")
HOVER_CTRL_DIFF=$(aediff "$HOVER_BASE" "$HOVER_CTRL")
HOVER_RET_DIFF=$(aediff "$HOVER_BASE" "$HOVER_OFF")
# WHERE AND IN WHAT COLOUR the return diff lives, printed whether or not it clears: the box the
# changed pixels occupy and the colours each capture carries inside it. A return that is an
# exact zero prints no box; anything else names the ink, which is what separates "a marker left
# lit" from "a chip the pointer was last seen near" from a whole-page state difference.
HOVER_RET_BOX=""
HOVER_RET_BEFORE=""
HOVER_RET_AFTER=""
if [ "${HOVER_RET_DIFF:-0}" -ne 0 ] && [ -f "$HOVER_BASE" ] && [ -f "$HOVER_OFF" ]; then
    HOVER_RET_BOX=$(magick "$HOVER_BASE" "$HOVER_OFF" -compose difference -composite -threshold 0 -format "%@" info: 2>/dev/null)
    case "$HOVER_RET_BOX" in
        *x*+*+*) ;;
        *) HOVER_RET_BOX="" ;;
    esac
    if [ -n "$HOVER_RET_BOX" ]; then
        HOVER_RET_BEFORE=$(topcols "$HOVER_BASE" "$HOVER_RET_BOX")
        HOVER_RET_AFTER=$(topcols "$HOVER_OFF" "$HOVER_RET_BOX")
    fi
fi
echo "hover: the pointer on the marker changes ${HOVER_BOX_DIFF}px of its own ${HBW}x${HBH} box at [${HOVER_BOX:-none}] (${HOVER_PAGE_DIFF}px of the whole page); the same state twice differs by ${HOVER_CTRL_DIFF}px, and the page with the pointer back off every key differs from the pre-hover one by ${HOVER_RET_DIFF}px${HOVER_RET_BOX:+ - those pixels are all inside [${HOVER_RET_BOX}], where the pre-hover capture carries (${HOVER_RET_BEFORE:-none}) and the post-park capture carries (${HOVER_RET_AFTER:-none})}"
[ "${HOVER_CTRL_DIFF:-1}" -eq 0 ]
check $? "the keyed Graph page is a STATE and not a repaint: two captures of it taken with nothing forced between them differ by ${HOVER_CTRL_DIFF:-?}px, so what the next reading sees is the pointer"
[ "${HOVER_BOX_DIFF:-0}" -ge "$HOVER_BOX_MIN" ]
check $? "moving the pointer ONTO a key's dot, with no click, repaints the docked panel: ${HOVER_BOX_DIFF:-0}px changed inside the marker's own box (bar ${HOVER_BOX_MIN}) - a stale or missing capture reads 0 here and fails"
[ "${HOVER_RET_DIFF:-1}" -eq 0 ]
check $? "and taking the pointer OFF every key puts the page back to the pre-hover pixel: ${HOVER_RET_DIFF:-?}px between the capture before the hover and the one after the pointer was parked (an exact zero, the rule the selection probe applies to Escape)${HOVER_RET_BOX:+ - they differ only inside [${HOVER_RET_BOX}], where the pre-hover capture carries (${HOVER_RET_BEFORE:-none}) and the post-park capture carries (${HOVER_RET_AFTER:-none})} - a marker left lit, or a hover that repainted something else and left it changed, fails here"

# ---- 5. THE DOCK HEADER'S VIEW BUTTON REPAINTS THE PAGE IT DRIVES ----
# A REGRESSION LOOKS LIKE this: the zoom-in button works (the timeline's own ruler and lanes move)
# while the docked panel keeps the frame window it had, so the panel's own page changes by 0px -
# which is what MainDisplayWidget::syncGraphPanelSharedState not refreshing the Graph page on a
# view-range change leaves behind. The button is addressed BY NAME through the header's own
# accessibility publication, and the reading is the dock's right-hand page ([${PANELRECT:-none}]),
# so neither the header's own ink nor the object tree beside it can move the number. A missing
# capture reads 0 through magick compare and fails the bar.
ZOOM_ON="${SHOTS}/gink_50_zoom_after.png"
read -r ZPX ZPY ZPW ZPH <<< "${PANELRECT:-0 0 0 0}"
assert_exists "$ZOOM_ON" "the panel's page captured after the header zoom-in"
assert_rendered "$ZOOM_ON" "the panel's page captured after the header zoom-in"
magick "$HOVER_OFF" -crop "${ZPW}x${ZPH}+${ZPX}+${ZPY}" +repage "$SCRATCH/zoom_before.png" 2>/dev/null
magick "$ZOOM_ON" -crop "${ZPW}x${ZPH}+${ZPX}+${ZPY}" +repage "$SCRATCH/zoom_after.png" 2>/dev/null
ZOOM_DIFF=$(aediff "$SCRATCH/zoom_before.png" "$SCRATCH/zoom_after.png")
echo "zoom: the dock header's zoom-in [${ZOOM_BTN:-none}] changed ${ZOOM_DIFF}px of the panel's page [${PANELRECT:-none}]"
[ -n "${ZOOM_BTN:-}" ]
check $? "the probe pressed the dock header's view button BY NAME: [${ZOOM_BTN:-none}] (TimeLineHeader's own zoom-in - the same ladder the timeline's wheel takes), not a coordinate"
[ "${ZOOM_DIFF:-0}" -ge "$ZOOM_PAGE_MIN" ]
check $? "and pressing it repaints the docked panel's own page: ${ZOOM_DIFF:-0}px of [${PANELRECT:-none}] changed (bar ${ZOOM_PAGE_MIN}) - one notch is one rung (x1.1) of the frame ladder, so the window the panel maps shrinks about the playhead and every tick, number, curve sample and key marker away from the anchor lands on a new pixel. This is the panel's page and not the header's: the before capture is the same settled page section 4 left behind"

# bone, pose, mesh and FFD are NOT asserted here: they are structural key types and are not
# graphable at all (ctrl::graph::Channel::componentCount returns 0), so no project can put them in
# this panel.
# ---- THE DRAG READOUT IS GONE, WITH ITS INK ----
# The panel used to draw the key move's live deltas AT the pointer, and this probe used the panel's
# own Ctrl-insert (Ctrl-press on a curve) to make the key it dragged and measured the text ink in
# the box the readout would own. The design decision that removed the readout removed the phase
# with it: there is no number drawn at the pointer any more, so there is nothing to measure and no
# bar to compare against. The gestalt it leaves behind is still covered above and below: the key
# the insert writes (and its SELECTION) is what the marker/hue phases read, and the drag's own
# arithmetic stays gated offscreen (tests/graph_drag.cpp drives the move through the editor's
# cursor path). Nothing is left to assert about ink the panel no longer paints.

# ---- WHAT IS NOT ASSERTED HERE, AND WHERE IT IS GATED ----
# The panel's tick ALPHAS and its selection EMPHASIS are deliberately NOT asserted above, and
# neither is a statement about the product here:
#   * a frame tick and the frame NUMBER that belongs to it share one column, and their two bands
#     merge into ONE run in a panel-on/off difference on this build, so "which band is the ticks"
#     would be a heuristic;
#   * the marker's rings are 1px strokes at 0.95 and 0.35 over the marker's own fill, and the
#     panel's text ink is neutral and thin: at those widths a pixel count cannot separate them.
# They ARE gated, offscreen, where nothing depends on a live screen:
#   * tests/graph_panel_pixels.cpp - a model whose frame ticks carry all three levels, read off the
#     rendered image (the alphas); the key-marker pair plus the stroke group's dimmed/full-strength
#     split (the emphasis, in both states); and the marker's three inks, read through a two-row
#     probe because ImGui strokes a circle as a POLYLINE;
#   * tests/graph_panel_model.cpp - the published tick levels, the per-channel classification and
#     the per-key selection flags the drawing layer reads.
# What THIS probe adds to those is what only a running application can show: that the dock's own
# tab puts the panel on the dock's own page, that the panel publishes itself exactly once and
# stops publishing its items when its page is hidden, that its pixels land inside the dock's
# rectangle, and that a keyed frame draws at all - two key types as two colours on the panel's
# own floor.

summary probe_graph_ink
