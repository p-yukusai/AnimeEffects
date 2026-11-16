#!/bin/bash
# THE TIMELINE TAB AND THE GRAPH TAB ARE ONE CHROME - measured on the application's
# own pixels, at ONE window, in ONE state.
#
# The user's complaint is not "the panel is a bit off": it is that the graph's ruler,
# playhead pill and scroll bar do not look like the timeline's, and that no engine
# limitation explains it. So this probe does what the eye does when it switches tabs:
# it builds ONE state (a new 600-frame project, the playhead set in the ruler, the
# frame axis zoomed in until the visible window is much narrower than the range - the
# state where both views have travel and both bars must be drawn), captures the dock
# with the TIMELINE tab current, captures it again with the GRAPH tab current WITHOUT
# touching the state, and asserts that the SAME rows and boxes are drawn on both:
#
#   * THE RULER'S ROWS: the band's top and bottom rows, the rows the ruler NUMBERS ink
#     in, the rows the TICKS ink in, and the hairline row under the band. All are
#     measured on each page and compared as ROWS;
#   * THE PLAYHEAD PILL'S BOX: its row run (absolute rows), its height, and its column
#     run READ FROM ITS PAGE'S OWN LEFT EDGE (the two pages are not the same width when
#     the tab is current, so x is compared page-relative; rows are absolute);
#   * THE PILL'S INKS: the FILL (a solid token, read as the pill's modal colour) and the
#     NUMBER inside it - the glyph's core ink and the glyph's own ROW run and column
#     count. This is the assertion that fails for the shipped defect: the timeline's
#     number is theme::Colors::accentBrightText (dark on the violet fill) and the
#     panel's was theme::Colors::text (near-white - two lit pixels);
#   * BOTH SCROLL BAR LANES: the lane is the page's last 12 rows, its 4px pill is centred
#     in it (page.bottom()-7 .. page.bottom()-4), the lane's surface reaches both page
#     edges (the trough is invisible and paints nothing, so the sampled edge columns are
#     the lane's own base), and the pill's run lies inside the page's columns. The lane
#     rows are compared page-relative: they are the same rows of the same dock page;
#   * THE PLAYHEAD'S LINE, under each strip: the current frame's column on the axis both
#     views share. The probe seeks ONCE and never between the captures, so this is the
#     assertion that the two pages show the SAME frame at the SAME zoom - the pill's
#     number cannot be (the two rasterisers' faces have different advances), which is why
#     the pill is compared as a BOX and its fill/glyph as tokens. That box IS comparable:
#     it is the model's geometry, measured from the same string with the host's own font
#     (the dock's stylesheet font, the one TimeCursor measures with), so its start and
#     width are asserted EQUAL - there is no AA band left in that comparison.
#
# WHAT IS COMPARED AND WHAT IS NOT, when the two engines rasterise differently: both rulers
# are Canvas Painter on this tree, but they reach it through two TEXT paths (TimeRuler's own
# canvas pass and the panel's gui::InkCanvas), so a glyph's edge pixels and a 1px rule's
# coverage are still artefacts - and so is a glyph run's ROW, by up to 2 rows: measured
# 2026-10-23, the same frame number inks rows 243..252 on the timeline against 242..250 on
# the graph, which is the one property this probe still fails on (see the two FAIL lines). Where only AA can explain a difference the
# probe asserts the ROW RUN, COLUMN RUN or BOX (a one-row tolerance) and not the pixel
# value - the log says so beside those numbers. The two tokens that are NOT AA - the
# pill's solid fill and the glyph's core ink, both read as the colour farthest/most
# common inside a solid area - are compared as colours, with the tolerance of the AA
# band (the same bar the timeline's own ink probes use for these tokens). The pill's BOX
# is not read as ink: its column run is the fill's widest run, and the fill is drawn from
# the model's rect, so the run measures that rect.
#
# HOW EACH NUMBER IS FOUND, and no theme constant is baked in beyond the model's own
# row arithmetic: the accent fill and the scroll bar's pill are LEARNED from each
# capture. In the page's top rows the only chromatic ink on a keyless project is the
# playhead (ColorAccent), so `inkhues` names it; the pill's column run is the widest
# column run of that colour (the 1px line is inside it, and the line starts UNDER the
# pill), and its row run is the run of rows that carry that fill colour in at least
# PILL_MIN_COLS of the pill's own columns (ae_ink rowruns) - NOT a single padding column,
# which reads a digit's stem as the pill's height, and NOT a column maximum, which reads
# the playhead line hanging under the pill as pill+line. The colour test is
# PILL_FILL_TOL: the pill's outer row is a 93%-coverage blend, not the token.
#
# THE PILL'S ROW RUN IS FOUND TWICE, AND THE SECOND TIME OVER THE WHOLE STRIP. The dock's
# AT-SPI rectangle starts ABOVE the header it contains (the dock's own title bar), so the
# page-top estimate `dock.top() + kHeaderHeight` can sit above the strip; a 22-row window
# from that estimate then CLIPS the pill instead of framing it. That is not a hypothesis:
# an earlier version of this probe read the timeline's 14-row pill as 6 rows (rows
# 241..246 of a window 225..246) while reading the graph's correctly, because the graph's
# page top comes from the panel's own published rectangle. So the run is located in a
# TALLER window first (PILL_SEARCH_ROWS from the estimate, enough to hold the whole
# strip), the strip's top is that run's top minus frameRuler::kPillTop (2), and the pill
# is then MEASURED over the strip's own 22 rows (frameRuler::kBandHeight) where the model
# puts it: kPillTop, NUMBER_HEIGHT tall. A run that is not that box aborts with its rows,
# so a clipped or mislocated pill can never be compared. The hairline is the row under
# the band. The ruler's number
# band and tick band are the row runs of ink in the band with the pill's columns cut out:
# the LAST run ends on the band's bottom row (the tick baseline, 6 inked rows) and the
# first run above it is the number band. The lane's pill is the farthest NEUTRAL ink in
# the page's last 12 rows (`neutral`), and its rows are the rows of its own column run
# that are off the lane's base.
#
# WHY THE PAGE'S RECT IS MEASURED, NOT ASSUMED, AND WHY IT CANNOT LAND ON A BORDER: the
# timeline's page and the panel's page have different widths (the dock sizes itself to
# the header's controls, and the Graph page has a Channels button the Timeline page does
# not), so a column is only comparable WITHIN a page. The page's left edge is found per
# capture with the route probe_graph_anchor.sh uses for the same boundary: the tree
# splitter's hairline is a column inked in EVERY row of the dock's body, so `inkcols` is
# asked for columns with at least the WHOLE body height of ink and the FIRST such run is
# the splitter (the dock's own right border is another such column, but it is to the
# right of the splitter and is therefore never the answer); the page starts on the column
# after the run. A low ink count - the old route - returns the first column that happens
# to carry a dozen ink pixels, which on this layout is the dock's own border inside the
# page's columns, and the computed page width is then <= 0. The graph page's rectangle is
# ALSO checked against the panel's own published Pane (Model::panel, found by the Fit
# graph action it owns), so the graph's page edges are corroborated by the widget that
# draws the lane.
#
# THE TABS ARE ADDRESSED BY NAME AND ROLE, not by name alone: the dock's header publishes
# the page-tab LIST(s) under the same two names, and a name-only lookup resolves the list
# (whose press switches nothing), which is how an earlier version of this probe produced
# three byte-identical captures of one page. `ae extents "Graph" "page tab"` and
# `ae click ... "page tab"` press the BUTTON, and the press is proven by the CAPTURES, not
# by the tab geometry: on this build the two tabs keep their own slots and measure the
# same width whichever is current (the selected segment is INKED as a raised knob, not
# inflated - TimeLineHeader::refreshTabStyle), so a width comparison asserts nothing. The
# probe therefore asserts that the two tab buttons exist and were pressed in the order
# Timeline then Graph, and that the two captures differ by PAGES_CHANGE_MIN pixels (a
# press that resolved the tab LIST leaves the same page up and the count is 0).
#
# THE STATE. A new project (the application's own 600 frames) so there is room to zoom;
# a left press in the timeline's ruler band sets the playhead under the pointer (the
# ruler's own seek, the same gesture both views' bands drive), and six wheel notches
# with the pointer ON that playhead column zoom in about it (1.1^6 = 1.77x from the
# timeline's seeded 1.1 px/frame, i.e. a visible window around 320 frames of the 600 -
# a third of the range, so both the timeline and the panel have real travel). The wheel
# is sent with the pointer on the column because a notch goes to the widget under the
# pointer and every capture parks the pointer on the File menu (the panel has hover
# states of its own).
#
# ONE STATE, ONE FRAME, ONE ZOOM: the seek and the six notches happen ONCE, while the
# Timeline page is up, and the Graph capture is taken WITHOUT touching the state - the
# tab press is the only gesture between the two shots. That is asserted, not assumed: the
# playhead LINE under each band is the current frame's column on the shared frame axis,
# so its page-relative column must be inside each pill and must agree between the two
# captures (a tab switch that also seeked or re-zoomed would move it). The pill's NUMBER
# is not the witness - the two rasterisers' fonts have different advances, which is why
# only the pill's BOX is compared across pages (asserted equal: the model measures it with
# the host's font, the timeline's own) and the glyph is compared as rows and tokens.
#
# FAILING LOUDLY: a pill that is not found, a pill whose run is not the model's
# NUMBER_HEIGHT-row box on the strip's own kPillTop row, a band whose ink runs are not
# the number band and the tick band, a playhead line that is not under its pill or that
# moved between the captures, a lane whose pill is not 4 rows thick or is not flush with
# the page's bottom, or a panel whose published rectangle disagrees with the page it
# draws, all report ABORT with the measured numbers - the run never compares two unknowns
# and calls them equal.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/_lib.sh" "$@"

GEOM=/tmp/probe_graph_parity_geom.txt
SCRATCH=/tmp/probe_graph_parity
STEPS=/tmp/probe_graph_parity_steps.log
INK="$ROOT/scripts/gui/ae_ink.py"
APPLOG=/tmp/probe_graph_parity_app.log
rm -f "$GEOM"
rm -rf "$SCRATCH"; mkdir -p "$SCRATCH"

# The ruler band's height and the pill's row inside it, from the model's own constants
# (gui::frameRuler::kBandHeight == Scaler::kRulerBandHeight == 22, kPillTop == 2,
# kNumberHeight == 14). The band is the page's first 22 rows on both tabs - the timeline
# ruler's header at the page top and the panel's published rulerBand at the panel top - so
# the fill search is exactly those rows and a tinted TRACK row below them cannot be
# mistaken for the playhead.
BAND_HEIGHT=22
PILL_TOP=2
NUMBER_HEIGHT=14
# The rows a tick inks on both engines: the band's baseline row plus the five above it
# (TimeRuler.cpp's drawLine(pos, pos + QPoint(0, -kTickLength)) and the panel's
# baseline..baseline-5 span, which was matched to it).
TICK_ROWS=6
# The wheel notches that give the frame axis travel (see THE STATE above).
ZOOM_NOTCHES=6
# The page's bottom band the horizontal scroll bar lives in: Scaler::kScrollBarThickness,
# which is AppStyle's own lane (src/gui/AppStyle.cpp, PM_ScrollBarExtent).
LANE_ROWS=12
# AppStyle's pill thickness (kPillThickness): the bar's 4px slider.
PILL_ROWS=4
# The rows the pill's fill run is searched in, DOWN FROM the page-top estimate: the dock's
# AT-SPI rectangle includes a title bar above TimeLineHeader (measured: 14 rows), so the
# estimate `dock.top() + kHeaderHeight` can sit above the strip. The window must hold the
# whole strip (BAND_HEIGHT) plus that slack, or the run is read clipped (measured: the
# timeline's 14-row pill read as 6 rows when the window was BAND_HEIGHT rows from there).
PILL_SEARCH_ROWS=$((BAND_HEIGHT + 28))
# HOW THE PILL ROWS ARE READ, and why the tolerance is not the obvious one (measured
# 2026-10-23, on the Canvas-rendered ruler this tree draws - see docs/verification/
# graph-panel/pill-probe-step.png).
#
# PILL_FILL_TOL. The pill's own outermost row is a 93%-coverage blend of the token,
# (163,136,244), distance 7.8 from #a78bfa; its interior rows are exact, and the row
# above/below the pill is the band, 260 away. So a test for "equals the token" (tol 1)
# throws the pill's own first and last rows away and can never count more than 12 of the
# 14 rows the model draws. 40 sits in the middle of the 7.8..260 gap, so it admits the
# pill's coverage and nothing else.
PILL_FILL_TOL=40
# PILL_MIN_COLS. A row is the pill when at least this many of the pill own columns carry
# the fill. THREE, and the number is structural, not a taste: the only 1px thing in that
# colour near the pill is the playhead LINE, which hangs from the pill own bottom row, so
# a column-maximum test counts pill+line as one 23-row run at the pill centre while a
# count threshold counts the pill 14. A glyph splits a row middle but never down to three
# columns (measured on "281": the densest row of the pill kept 9 of its 28).
PILL_MIN_COLS=3
# The least number of changed pixels inside the pages that two DIFFERENT pages must
# produce. A stale/unsuitable click repaints none of them (byte-identical captures), and
# a page change redraws the ruler, the content and (on the graph) the whole floor.
PAGES_CHANGE_MIN=1000
export GEOM SCRATCH STEPS INK APPLOG BAND_HEIGHT PILL_TOP NUMBER_HEIGHT TICK_ROWS \
       ZOOM_NOTCHES LANE_ROWS PILL_ROWS PILL_SEARCH_ROWS PILL_FILL_TOL \
       PILL_MIN_COLS PAGES_CHANGE_MIN

xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
  set -u
  cd "$AE_PROBE_DIR"
  "$APP" "${PROJ:-}" >"$APPLOG" 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID
  : > "$STEPS"
  say() { echo "t+${SECONDS}s  $*" >> "$STEPS"; }
  abort() {
    printf "ABORT=\"%s\"\n" "$1" >> "$GEOM"
    say "ABORT: $1"
    kill $app 2>/dev/null
    wait $app 2>/dev/null
    exit 0
  }
  wait_ready 120 || abort "the main window never came up (wait_ready timed out)"
  W=$(xdotool getactivewindow 2>/dev/null || xdotool search --name AnimeEffects | tail -1)
  xdotool windowactivate --sync $W 2>/dev/null
  sleep 0.6

  inkpx() { python3 "$INK" "$@"; }
  raw() { magick "$1" -crop "$2" +repage -depth 8 rgb:"$3"; }
  crop() { magick "$1" -crop "$2" +repage "$3"; }
  modal() { inkpx modal "$1" "$2" "$3" "$4" "$5" "$6" "$7" | cut -d" " -f1-3; }
  # EVERY CAPTURE PARKS THE POINTER ON THE FILE MENU: the panel has hover states of its
  # own (a marker brightens, a chip appears), and a capture taken with the pointer over
  # the plot would measure one of them.
  FR=$(ae extents "File")
  [ -n "$FR" ] || abort "no File menu: this is not the application window"
  read -r FX FY FW FH <<< "$FR"
  POINTX=$((FX + 3)); POINTY=$((FY + FH / 2))
  park() { xdotool mousemove --sync $POINTX $POINTY; sleep 0.5; }
  shot() { park; import -window root "$1"; }
  # tab <name>: press the dock header page tab BY NAME AND ROLE. The name alone resolves
  # the page-tab LIST (which switches nothing); "page tab" is the BUTTON.
  tab() {
    local r x y w h
    r=$(ae extents "$1" "page tab")
    [ -n "$r" ] || return 1
    read -r x y w h <<< "$r"
    xdotool mousemove --sync $((x + w / 2)) $((y + h / 2)) click 1
    sleep 1.8
    return 0
  }

  # THE PROJECT, at the application own defaults (600 frames), so the frame axis can be
  # zoomed past the ladder floor without clamping and the content is wider than the
  # window at the zoom this probe uses.
  ae_press "File" "the File menu opens"
  sleep 1
  ae_press "New project" "the New project entry is pressed"
  sleep 2.5
  ae_activate "Create" "a default new project is created"
  sleep 4

  # ---- the TIMELINE page, and the page rect ----
  # The tabs are addressed BY NAME WITH THE ROLE page tab. Only the NON-current tab has a
  # rectangle that is its own slot: the CURRENT one is inflated to the whole strip
  # (probe_graph_ink.sh measured 120px against 60px), so its centre falls on the slot
  # boundary. The dock opens on the Timeline page, which the tab bar itself confirms. The
  # probe therefore presses Timeline ONLY if the tab bar says another page is current -
  # and then Timeline is the non-current one and its centre is its own slot.
  TAB_T_AT_TL=$(ae extents "Timeline" "page tab")
  TAB_G_AT_TL=$(ae extents "Graph" "page tab")
  [ -n "${TAB_T_AT_TL:-}" ] && [ -n "${TAB_G_AT_TL:-}" ] \
    || abort "the dock header publishes no page tab named Timeline/Graph (role \"page tab\")"
  read -r _ttx _tty ttw _tth <<< "$TAB_T_AT_TL"
  read -r _gax _gay gaw _gah <<< "$TAB_G_AT_TL"
  if [ "${ttw:-0}" -lt "${gaw:-0}" ]; then
    tab "Timeline" || abort "the dock header has no pressable page tab named Timeline (role \"page tab\")"
    TAB_T_AT_TL=$(ae extents "Timeline" "page tab")
    TAB_G_AT_TL=$(ae extents "Graph" "page tab")
  fi
  echo "TAB_T_AT_TL=\"${TAB_T_AT_TL:-}\"" >> "$GEOM"
  echo "TAB_G_AT_TL=\"${TAB_G_AT_TL:-}\"" >> "$GEOM"
  say "timeline page up; tabs Timeline [${TAB_T_AT_TL:-none}] Graph [${TAB_G_AT_TL:-none}]"

  DOCK_T=$(ae extents "Animation")
  [ -n "$DOCK_T" ] || abort "no visible widget named Animation: this is not the docked layout"
  read -r DX_T DY_T DW_T DH_T <<< "$DOCK_T"
  echo "DOCK_T=\"$DOCK_T\"" >> "$GEOM"
  # THE PAGE-TOP ESTIMATE, and why it is an ESTIMATE. The body starts below the dock
  # header, TimeLineHeader, whose fixed height is kHeaderHeight (40,
  # src/gui/TimeLineHeader.cpp), but the AT-SPI rectangle named Animation is the
  # QDockWidget own: it starts at the dock own title bar, ABOVE the header (measured: 14
  # rows, so the header runs from DY+14 and the page from DY+54 while this sum names
  # DY+40). The strip real top is therefore found FROM THE PILL below, in a window tall
  # enough to
  # hold it (PILL_SEARCH_ROWS); this sum only has to be above the strip, which it is.
  BODY_TOP_T=$((DY_T + 40))
  PAGE_BOTTOM_T=$((DY_T + DH_T - 1))
  RIGHT_T=$((DX_T + DW_T))
  say "dock $DOCK_T; body top $BODY_TOP_T, page bottom row $PAGE_BOTTOM_T"

  # page_left <shot> <dx> <dy> <dw> <dh> -> the first page column, in absolute x.
  # THE SPLITTER IS THE COLUMN INKED IN EVERY ROW OF THE BODY: `inkcols` is asked for at
  # least the WHOLE scanned height of ink, so the runs it returns are the page own
  # vertical rules - the tree splitter first, the dock own right border later - and the
  # FIRST one is the splitter by construction. (With a dozen pixels of ink the first
  # answer was the dock border, INSIDE the page own columns: the page width came out
  # <= 0.) The scan is BOTTOM-ANCHORED, 60 rows below the dock top and 20 rows above its
  # bottom, so it is inside the body whatever the header height is and never includes a
  # header row where the splitter is not drawn. The page starts on the column after the
  # first run.
  page_left() {
    local s=$1 dx=$2 dy=$3 dw=$4 dh=$5
    local btop=$((dy + 60))
    local bh=$((dh - 80))
    if [ "$bh" -le 40 ]; then echo ""; return; fi
    local f="$SCRATCH/pl.raw"
    raw "$s" "${dw}x${bh}+${dx}+${btop}" "$f"
    local bg
    bg=$(modal "$f" "$dw" "$bh" 0 0 "$dw" "$bh")
    case "$bg" in ""|none) echo ""; return ;; esac
    local br bgg bb
    read -r br bgg bb <<< "$bg"
    inkpx inkcols "$f" "$dw" "$bh" 0 $((bh - 1)) "$br" "$bgg" "$bb" 12 "$bh" \
      > "$SCRATCH/pl_cols.txt"
    local c0 c1 cn cr cg cb
    while read -r c0 c1 cn cr cg cb; do
      case "$c0" in ""|none) continue ;; esac
      echo $((dx + c0 + 1))
      return
    done < "$SCRATCH/pl_cols.txt"
    echo ""
  }

  shot "$SHOTS/parity_00_timeline_seek.png"
  PL_T=$(page_left "$SHOTS/parity_00_timeline_seek.png" "$DX_T" "$DY_T" "$DW_T" "$DH_T")
  [ -n "$PL_T" ] || abort "the timeline page own splitter hairline was not found (no column is inked in every body row)"
  echo "PL_T=\"$PL_T\"" >> "$GEOM"
  say "timeline page left $PL_T (dock $DX_T..$((DX_T + DW_T - 1)))"

  # THE PLAYHEAD, set with the ruler own seek: a left press in the timeline ruler band
  # puts the frame under the pointer. The y is the page-top ESTIMATE plus 20, which lands
  # inside the band own 22 rows for every title-bar height below 20 (measured: 6 rows
  # into the strip), so the gesture is the ruler own seek and not a track gesture.
  xdotool mousemove --sync $((PL_T + 300)) $((BODY_TOP_T + 20)) click 1
  sleep 1.2
  # ...and the ZOOM, with the pointer ON that playhead column: the seek put the frame
  # under the pointer and a wheel goes to the widget under the pointer, so the anchored
  # zoom holds that frame there and the pill does not walk out of the page.
  xdotool mousemove --sync $((PL_T + 300)) $((BODY_TOP_T + 20))
  sleep 0.4
  xdotool click --repeat $ZOOM_NOTCHES --delay 120 5
  sleep 1.2
  shot "$SHOTS/parity_01_timeline.png"

  # measure <shot> <tag> <page-left> <page-right> <page-top estimate> <page-bottom> :
  # every parity number for ONE page, into $SCRATCH/<tag>.* and into GEOM. Rows are
  # absolute screen rows; columns inside the page are page-relative; page-relative rows
  # are named where they are used. The page-top estimate only bounds the search for the
  # pill; the strip top row is DERIVED from the pill and written as the page top, so a
  # dock whose rectangle starts above its header cannot shift the page.
  measure() {
    local s=$1 tag=$2 pl=$3 right=$4 btop=$5 pbot=$6
    local w=$((right - pl))
    [ "$w" -gt 100 ] || abort "the $tag page [${s}] has ${w} columns after the splitter (left $pl, right $right): there is no page to measure"

    # ---- the playhead pill, in the page own top rows ----
    raw "$s" "${w}x${BAND_HEIGHT}+${pl}+${btop}" "$SCRATCH/$tag.band.raw"
    local base hue
    base=$(inkpx modal "$SCRATCH/$tag.band.raw" "$w" "$BAND_HEIGHT" 0 0 "$w" "$BAND_HEIGHT" | cut -d" " -f1-3)
    case "$base" in ""|none) abort "the $tag page has no modal band colour"; ;; esac
    read -r br bgg bb <<< "$base"
    hue=$(inkpx inkhues "$SCRATCH/$tag.band.raw" "$w" "$BAND_HEIGHT" 0 0 "$w" "$BAND_HEIGHT" \
                        "$br" "$bgg" "$bb" 10 40 40 | head -1)
    case "$hue" in ""|none) abort "the $tag page shows no chromatic ink in its ruler band: the playhead pill was not found"; ;; esac
    read -r hr hg hb hcount <<< "$hue"
    # The pill COLUMN run: the widest run of the fill colour (the 1px playhead line is
    # inside it, and the line starts under the pill, so the run is the pill own width).
    inkpx cols "$SCRATCH/$tag.band.raw" "$w" "$BAND_HEIGHT" 0 $((BAND_HEIGHT - 1)) "$hr" "$hg" "$hb" 2 1 \
      > "$SCRATCH/$tag.pill_cols.txt"
    local px0="" px1="" pw=0 c0 c1 cn
    while read -r c0 c1 cn; do
      case "$c0" in ""|none) continue ;; esac
      local len=$((c1 - c0 + 1))
      if [ "$len" -gt "$pw" ]; then pw=$len; px0=$c0; px1=$c1; fi
    done < "$SCRATCH/$tag.pill_cols.txt"
    [ -n "$px0" ] || abort "the $tag page has no pill column run of its fill colour"
    local absx0=$((pl + px0))
    # ---- THE PILL ROW RUN, part 1: WHERE THE STRIP IS ----
    # The pill is a filled ROUNDED rect with the frame number drawn on it, so its rows are
    # the rows of its own columns that carry the fill in at least PILL_MIN_COLS places
    # (ae_ink rowruns). Three spellings were measured on 2026-10-23 before this one
    # (docs/verification/graph-panel/pill-probe-step.png); the first two are what the step
    # got wrong:
    #   * ONE column (px0+4, "the padding column") - a column can run down a digit stem,
    #     which read a 30px pill as 3 rows tall;
    #   * a column MAXIMUM - the playhead LINE hangs from the pill own bottom row in the
    #     pill own colour, so at the pill centre pill+line is ONE 23-row run, longer than
    #     the pill, and the maximum picks it;
    #   * a row MAJORITY - a row through the number is mostly NOT the token, so the pill
    #     read as two runs with a 3-row longest.
    # The colour test is PILL_FILL_TOL, not "equals the token": the pill outer row is a
    # 93%-coverage blend 7.8 away from the token and the band is 260 away.
    # THE WINDOW IS PILL_SEARCH_ROWS, NOT BAND_HEIGHT: the page-top estimate can sit
    # above the strip (the AT-SPI rectangle named Animation includes a title bar, above
    # the header own 40 rows), and a BAND_HEIGHT-row window from there cut the timeline
    # 14-row pill to the 6 rows it had in the window.
    local search=$PILL_SEARCH_ROWS avail=$((pbot - btop + 1))
    [ "$avail" -ge "$search" ] || search=$avail
    [ "$search" -ge "$((BAND_HEIGHT + 8))" ] \
      || abort "the $tag page has ${avail} rows below ${btop}: too few to hold the ruler strip and find the pill in it"
    # (its own file: $tag.pillbox.raw is the EXACT pill box, cropped further down)
    raw "$s" "${pw}x${search}+${absx0}+${btop}" "$SCRATCH/$tag.pillsearch.raw"
    local pillruns sr0="" sr1="" sh=0 r0 r1 rn
    pillruns=$(inkpx rowruns "$SCRATCH/$tag.pillsearch.raw" "$pw" "$search" \
                        "$hr" "$hg" "$hb" "$PILL_FILL_TOL" "$PILL_MIN_COLS")
    while read -r r0 r1 rn; do
      case "$r0" in ""|none) continue ;; esac
      local len=$((r1 - r0 + 1))
      if [ "$len" -gt "$sh" ]; then sh=$len; sr0=$r0; sr1=$r1; fi
    done <<< "$pillruns"
    [ -n "$sr0" ] \
      || abort "the $tag page shows no row carrying ${PILL_MIN_COLS} of its own fill colour in rows ${btop}..$((btop + search - 1)): the playhead pill was not found"
    local sd=$((sh - NUMBER_HEIGHT)); [ "$sd" -ge 0 ] || sd=$((0 - sd))
    [ "$sd" -le 1 ] \
      || abort "the pill-coloured band of the $tag page is ${sh} rows (rows $((btop + sr0))..$((btop + sr1)) of the search window) over the ${pw} columns of the pill own fill run, not the ${NUMBER_HEIGHT}-row pill: another fill-coloured mark was read, or the window clipped it"
    local band_top=$((btop + sr0 - PILL_TOP))
    [ "$band_top" -ge "$btop" ] \
      || abort "the pill run of the $tag page starts at row $((btop + sr0)), above the page-top estimate ${btop}: the pill cannot be the first thing on the page"
    local band_bottom=$((band_top + BAND_HEIGHT - 1))
    local hair_y=$((band_bottom + 1))

    # ---- THE PILL ROW RUN, part 2: over the strip own rows ----
    # The strip is the model own 22 rows, so the pill is the run at kPillTop, NUMBER_HEIGHT
    # tall. Reading it HERE, in a window that starts above the pill and is derived from it,
    # is what makes the box trustworthy: the whole pill is inside the window by
    # construction, and a run that is not the model box aborts with its rows instead of
    # being compared. Same measurement as part 1 (maxrun over the pill own columns).
    raw "$s" "${pw}x${BAND_HEIGHT}+${absx0}+${band_top}" "$SCRATCH/$tag.stripcol.raw"
    local stripruns py0="" py1="" ph=0
    stripruns=$(inkpx rowruns "$SCRATCH/$tag.stripcol.raw" "$pw" "$BAND_HEIGHT" \
                         "$hr" "$hg" "$hb" "$PILL_FILL_TOL" "$PILL_MIN_COLS")
    while read -r r0 r1 rn; do
      case "$r0" in ""|none) continue ;; esac
      local len=$((r1 - r0 + 1))
      if [ "$len" -gt "$ph" ]; then ph=$len; py0=$r0; py1=$r1; fi
    done <<< "$stripruns"
    [ -n "$py0" ] \
      || abort "the $tag page shows no fill run in its own ruler strip (rows ${band_top}..${band_bottom})"
    local pd=$((py0 - PILL_TOP)); [ "$pd" -ge 0 ] || pd=$((0 - pd))
    local pdh=$((ph - NUMBER_HEIGHT)); [ "$pdh" -ge 0 ] || pdh=$((0 - pdh))
    [ "$pd" -le 1 ] && [ "$pdh" -le 1 ] \
      || abort "the pill of the $tag page, in its ${BAND_HEIGHT}-row strip (rows ${band_top}..${band_bottom}), is strip-relative rows ${py0}..${py1} (${ph} tall), not the model kPillTop ${PILL_TOP} and ${NUMBER_HEIGHT} tall: the pill is clipped or misplaced in the strip"
    local absy0=$((band_top + py0))
    echo "${tag}_PAGE=\"$pl $right $band_top $pbot\"" >> "$GEOM"
    echo "${tag}_FILL=\"$hr $hg $hb $hcount\"" >> "$GEOM"
    echo "${tag}_BAND=\"$band_top $band_bottom\"" >> "$GEOM"
    echo "${tag}_PILL=\"$px0 $pw $absx0 $absy0 $ph\"" >> "$GEOM"
    say "$tag: strip rows ${band_top}..${band_bottom} (its pill found as the ${sh}-row fill run rows $((btop + sr0))..$((btop + sr1)) of the ${search}-row search window from ${btop}); pill page-relative x ${px0}..$((px0 + pw - 1)) (absolute ${absx0}..$((absx0 + pw - 1))), rows ${absy0}..$((absy0 + ph - 1)) (${ph} tall, strip-relative ${py0}..${py1}), fill $hr,$hg,$hb ($hcount px)"

    # ---- the NUMBER glyph rows and the TICK rows, from the band own ink ----
    # The band is cropped exactly (its derived top, 22 rows) with the pill columns CUT
    # OUT, so the pill can never be mistaken for a number row. The row runs then belong
    # to the ruler own marks: the LAST run ends on the band bottom row (the tick
    # baseline, 6 inked rows) and the first run above it is the number band.
    local lw0=0 rw0=0 rx=$((px0 + pw + 12)) sw=0
    [ "$px0" -ge 52 ] && lw0=$((px0 - 12))
    if [ "$rx" -lt "$w" ] && [ $((w - rx)) -ge 40 ]; then rw0=$((w - rx)); fi
    if [ "$lw0" -ge 40 ]; then
      crop "$s" "${lw0}x${BAND_HEIGHT}+${pl}+${band_top}" "$SCRATCH/$tag.nl.png"
      sw=$((sw + lw0))
    fi
    if [ "$rw0" -ge 40 ]; then
      crop "$s" "${rw0}x${BAND_HEIGHT}+$((pl + rx))+${band_top}" "$SCRATCH/$tag.nr.png"
      sw=$((sw + rw0))
    fi
    [ "$sw" -ge 80 ] || abort "the $tag page has no band columns clear of the pill to read the ruler numbers and ticks in"
    if [ "$lw0" -ge 40 ] && [ "$rw0" -ge 40 ]; then
      magick "$SCRATCH/$tag.nl.png" "$SCRATCH/$tag.nr.png" +append "$SCRATCH/$tag.nums.png"
    elif [ "$lw0" -ge 40 ]; then
      cp "$SCRATCH/$tag.nl.png" "$SCRATCH/$tag.nums.png"
    else
      cp "$SCRATCH/$tag.nr.png" "$SCRATCH/$tag.nums.png"
    fi
    raw "$SCRATCH/$tag.nums.png" "${sw}x${BAND_HEIGHT}+0+0" "$SCRATCH/$tag.nums.raw"
    local nbase
    nbase=$(modal "$SCRATCH/$tag.nums.raw" "$sw" "$BAND_HEIGHT" 0 0 "$sw" "$BAND_HEIGHT")
    case "$nbase" in ""|none) abort "the $tag band has no modal colour to read the ruler marks against"; ;; esac
    local nbr nbg nbb
    read -r nbr nbg nbb <<< "$nbase"
    inkpx rows "$SCRATCH/$tag.nums.raw" "$sw" "$BAND_HEIGHT" "$nbr" "$nbg" "$nbb" 40 0.01 \
      > "$SCRATCH/$tag.bandrows.txt"
    local num0="" num1="" tk0="" tk1=""
    while read -r r0 r1; do
      case "$r0" in ""|none) continue ;; esac
      if [ "$r1" -eq "$((BAND_HEIGHT - 1))" ] && [ -z "$tk0" ]; then
        tk0=$r0; tk1=$r1
      elif [ -z "$num0" ]; then
        num0=$r0; num1=$r1
      fi
    done < "$SCRATCH/$tag.bandrows.txt"
    [ -n "$tk0" ] || abort "the $tag band ink ends on rows that are not the band bottom: the tick run was not found (runs in $SCRATCH/$tag.bandrows.txt)"
    [ -n "$num0" ] || abort "the $tag band has no number row run above its ticks (runs in $SCRATCH/$tag.bandrows.txt)"
    local num_top=$((band_top + num0)) num_bot=$((band_top + num1))
    local tick_top=$((band_top + tk0)) tick_bot=$((band_top + tk1))
    echo "${tag}_NUM=\"$num_top $num_bot\"" >> "$GEOM"
    echo "${tag}_TICK=\"$tick_top $tick_bot\"" >> "$GEOM"
    say "$tag: number glyph rows ${num_top}..${num_bot} (band rows $num0..$num1), tick rows ${tick_top}..${tick_bot} (band rows $tk0..$tk1, $((tk1 - tk0 + 1)) of the ${BAND_HEIGHT}) - read as ROWS; the glyph edge pixels are AA and are not compared"

    # ---- the number ink inside the pill ----
    # `far` inside the PILL BOX against the FILL: the pixel farthest from the fill is the
    # glyph core, and it is the token the two pages must agree about (the shipped defect
    # put a near-white token on the accent fill). Its ROW run is read the same way the
    # ruler numbers are - as a row run, so AA cannot fail it. The box is cropped from the
    # capture in absolute coordinates, so it carries no band-window arithmetic.
    local inkline
    raw "$s" "${pw}x${ph}+${absx0}+${absy0}" "$SCRATCH/$tag.pillbox.raw"
    inkline=$(inkpx far "$SCRATCH/$tag.pillbox.raw" "$pw" "$ph" 0 0 "$pw" "$ph" \
                    "$hr" "$hg" "$hb" 30)
    echo "${tag}_INK=\"$inkline\"" >> "$GEOM"
    inkpx inkcolmap "$SCRATCH/$tag.pillbox.raw" "$pw" "$ph" "$hr" "$hg" "$hb" 40 \
      > "$SCRATCH/$tag.glyphcols.txt"
    local gy0="" gy1="" gcols=0 gx y0 y1 gn gr gg gb
    while read -r gx y0 y1 gn gr gg gb; do
      case "$gx" in ""|none) continue ;; esac
      [ "$gx" -ge 4 ] || continue
      [ "$gx" -le $((pw - 5)) ] || continue
      gcols=$((gcols + 1))
      if [ -z "$gy0" ] || [ "$y0" -lt "$gy0" ]; then gy0=$y0; fi
      if [ -z "$gy1" ] || [ "$y1" -gt "$gy1" ]; then gy1=$y1; fi
    done < "$SCRATCH/$tag.glyphcols.txt"
    [ -n "$gy0" ] || abort "the $tag page has no number glyph ink inside the pill (columns 4..$((pw - 5)) of a ${pw}px pill)"
    echo "${tag}_GLYPH=\"$((absy0 + gy0)) $((absy0 + gy1)) $gcols\"" >> "$GEOM"
    say "$tag: the number inside the pill: $inkline (far from the fill); its glyph rows $((absy0 + gy0))..$((absy0 + gy1)) over ${gcols} columns (pill-relative $gy0..$gy1)"

    # ---- the playhead LINE under the strip: the SAME FRAME on both pages ----
    # The probe seeks ONCE (in the timeline ruler) and the Graph capture takes the state
    # it left, so the line - the current frame column on the frame axis both views
    # share - must sit under the pill and at the same page-relative column on both pages.
    # A tab switch that also seeked or re-zoomed would move it, and this assertion says
    # so. The line is the only accent ink in these rows (the lane pill is neutral, its
    # rows are 220 below, and the ruler ticks end on the strip last row).
    local line_top=$((band_bottom + 2))
    local line_h=$((pbot - line_top + 1))
    [ "$line_h" -gt 9 ] && line_h=9
    [ "$line_h" -ge 4 ] \
      || abort "the $tag page has no rows under its strip (from row ${line_top}) to read the playhead line in"
    raw "$s" "${w}x${line_h}+${pl}+${line_top}" "$SCRATCH/$tag.line.raw"
    inkpx cols "$SCRATCH/$tag.line.raw" "$w" "$line_h" 0 $((line_h - 1)) \
               "$hr" "$hg" "$hb" 2 4 > "$SCRATCH/$tag.linecols.txt"
    local lc=-1 lrun=0
    while read -r c0 c1 cn; do
      case "$c0" in ""|none) continue ;; esac
      local len=$((c1 - c0 + 1))
      if [ "$len" -le 4 ] && [ "$len" -gt "$lrun" ]; then lrun=$len; lc=$((c0 + (len - 1) / 2)); fi
    done < "$SCRATCH/$tag.linecols.txt"
    [ "$lc" -ge 0 ] \
      || abort "the $tag page draws no playhead line under its strip: rows ${line_top}..$((line_top + line_h - 1)) hold no 1..4px run of the pill fill colour"
    local lcx=$((px0 + pw / 2))
    local ld=$((lc - lcx)); [ "$ld" -ge 0 ] || ld=$((0 - ld))
    [ "$ld" -le 2 ] \
      || abort "the playhead line of the $tag page is at page-relative x ${lc}, not under its pill (centre ${lcx}, tolerance 2): the pill and the line disagree"
    echo "${tag}_LINE=\"$lc\"" >> "$GEOM"
    say "$tag: playhead line page-relative x ${lc} (${lrun} column(s), rows ${line_top}..$((line_top + line_h - 1))), under the pill centre ${lcx} - the frame is the one the timeline seek set"

    # ---- the hairline under the band (the band bottom witness) ----
    # The ROW is the band own arithmetic (the row under the band, as TimeRuler draws its
    # rule on the first track row and the panel draws it one row under its band). It is
    # READ as a row: its modal colour differs from the band last row (the rule is there)
    # and covers the page own columns (the rule spans the page). Its VALUE is the shared
    # palette token: both views draw the rule OPAQUE (the timeline turns the AA hint off
    # for its 1px pen, the panel draws theme::Graph::rulerLine at full strength), so the
    # modal colours of the two pages are compared below.
    raw "$s" "${w}x1+${pl}+${hair_y}" "$SCRATCH/$tag.hairrow.raw"
    local hmodal
    hmodal=$(inkpx modal "$SCRATCH/$tag.hairrow.raw" "$w" 1 0 0 "$w" 1)
    case "$hmodal" in ""|none) abort "the $tag page has no hairline row at ${hair_y} to read"; ;; esac
    read -r hair_r hair_g hair_b hair_n <<< "$hmodal"
    raw "$s" "${w}x1+${pl}+${band_bottom}" "$SCRATCH/$tag.bandlast.raw"
    local blmodal blr blg blb bln
    blmodal=$(inkpx modal "$SCRATCH/$tag.bandlast.raw" "$w" 1 0 0 "$w" 1)
    case "$blmodal" in ""|none) abort "the $tag page has no band bottom row at ${band_bottom} to read"; ;; esac
    read -r blr blg blb bln <<< "$blmodal"
    echo "${tag}_HAIR=\"$hair_y $hair_n $hair_r $hair_g $hair_b $blr $blg $blb\"" >> "$GEOM"
    say "$tag: hairline row ${hair_y} (band bottom ${band_bottom}): modal rgb $hair_r,$hair_g,$hair_b over $hair_n of ${w} columns; the band last row modal rgb $blr,$blg,$blb"

    # ---- the horizontal LANE, in the page own last 12 rows ----
    # The trough is invisible (AppStyle draws none), so the lane is measured by its own
    # arithmetic and its PILL: the slider is the farthest neutral ink of the band, and its
    # rows are the rows of its own column run that are off the lane base. The lane band
    # is cropped two columns in from each page edge so the splitter and the dock own
    # rules are not mistaken for the slider; the page edge columns are sampled
    # separately for the flush check.
    local lane_top=$((pbot - LANE_ROWS + 1))
    local lwp=$((w - 4))
    [ "$lwp" -gt 40 ] || abort "the $tag page is too narrow (${w} columns) to measure its lane"
    raw "$s" "${lwp}x${LANE_ROWS}+$((pl + 2))+${lane_top}" "$SCRATCH/$tag.lane.raw"
    local lbase lbr lbg lbb
    lbase=$(modal "$SCRATCH/$tag.lane.raw" "$lwp" "$LANE_ROWS" 0 0 "$lwp" "$LANE_ROWS")
    case "$lbase" in ""|none) abort "the $tag page own lane band has no modal colour"; ;; esac
    read -r lbr lbg lbb <<< "$lbase"
    local nline pill
    nline=$(inkpx neutral "$SCRATCH/$tag.lane.raw" "$lwp" "$LANE_ROWS" 0 0 "$lwp" "$LANE_ROWS" \
                     "$lbr" "$lbg" "$lbb" 6 8)
    case "$nline" in ""|none|*none*) abort "the $tag page draws no neutral pill in its last ${LANE_ROWS} rows: the scroll bar is absent or invisible"; ;; esac
    pill=$(echo "$nline" | cut -d" " -f4-6)
    local sr sg sb
    read -r sr sg sb <<< "$pill"
    inkpx cols "$SCRATCH/$tag.lane.raw" "$lwp" "$LANE_ROWS" 0 $((LANE_ROWS - 1)) "$sr" "$sg" "$sb" 6 3 \
      > "$SCRATCH/$tag.lane_cols.txt"
    local lx0="" lx1="" lw=0
    while read -r c0 c1 cn; do
      case "$c0" in ""|none) continue ;; esac
      local len=$((c1 - c0 + 1))
      if [ "$len" -gt "$lw" ]; then lw=$len; lx0=$c0; lx1=$c1; fi
    done < "$SCRATCH/$tag.lane_cols.txt"
    [ -n "$lx0" ] || abort "the $tag page own lane pill has no column run"
    raw "$s" "${lw}x${LANE_ROWS}+$((pl + 2 + lx0))+${lane_top}" "$SCRATCH/$tag.lanepill.raw"
    inkpx rows "$SCRATCH/$tag.lanepill.raw" "$lw" "$LANE_ROWS" "$lbr" "$lbg" "$lbb" 20 0.5 \
      > "$SCRATCH/$tag.lane_rows.txt"
    local ly0="" ly1="" lh=0
    while read -r r0 r1; do
      case "$r0" in ""|none) continue ;; esac
      ly0=$r0; ly1=$r1; lh=$((r1 - r0 + 1))
      break
    done < "$SCRATCH/$tag.lane_rows.txt"
    [ -n "$ly0" ] || abort "the $tag page own lane pill has no row run in its own columns"
    local lpx=$((2 + lx0))
    echo "${tag}_LANE=\"$lane_top $((lane_top + ly0)) $lh $lpx $lw $sr $sg $sb\"" >> "$GEOM"
    echo "${tag}_LANEBASE=\"$lbr $lbg $lbb\"" >> "$GEOM"
    # THE PAGE EDGE SAMPLES: one column in from each page edge, on the slider own
    # centre row. The trough paints nothing, so these columns must be the lane base -
    # that is the lane column geometry reaching the page edges with no rail, no
    # margin and no border drawn by the bar.
    local lmid=$((lane_top + ly0 + lh / 2))
    raw "$s" "1x1+$((pl + 1))+${lmid}" "$SCRATCH/$tag.edge_l.raw"
    raw "$s" "1x1+$((right - 2))+${lmid}" "$SCRATCH/$tag.edge_r.raw"
    local elr elg elb err erg erb
    read -r elr elg elb <<< "$(inkpx px "$SCRATCH/$tag.edge_l.raw" 1 1 0 0)"
    read -r err erg erb <<< "$(inkpx px "$SCRATCH/$tag.edge_r.raw" 1 1 0 0)"
    echo "${tag}_LANEEDGE=\"$elr $elg $elb $err $erg $erb\"" >> "$GEOM"
    say "$tag: lane rows ${lane_top}..${pbot} (the page last ${LANE_ROWS}), pill rows $((lane_top + ly0))..$((lane_top + ly1)) (${lh} thick), page-relative x ${lpx}..$((lpx + lw - 1)), ink $sr,$sg,$sb; the lane base $lbr,$lbg,$lbb sampled at the page edges (x $((pl + 1)) and $((right - 2)), row ${lmid}): $elr,$elg,$elb and $err,$erg,$erb"
  }

  measure "$SHOTS/parity_01_timeline.png" T "$PL_T" "$RIGHT_T" "$BODY_TOP_T" "$PAGE_BOTTOM_T"
  . "$GEOM"

  # ---- the GRAPH page, the SAME state ----
  tab "Graph" || abort "the dock header has no pressable page tab named Graph (role \"page tab\")"
  TAB_T_AT_G=$(ae extents "Timeline" "page tab")
  TAB_G_AT_G=$(ae extents "Graph" "page tab")
  echo "TAB_T_AT_G=\"${TAB_T_AT_G:-}\"" >> "$GEOM"
  echo "TAB_G_AT_G=\"${TAB_G_AT_G:-}\"" >> "$GEOM"
  sleep 0.8
  shot "$SHOTS/parity_02_graph.png"
  say "graph page up; tabs Timeline [${TAB_T_AT_G:-none}] Graph [${TAB_G_AT_G:-none}]"

  DOCK_G=$(ae extents "Animation")
  [ -n "$DOCK_G" ] || abort "the dock published no rectangle with the Graph page up"
  read -r DX_G DY_G DW_G DH_G <<< "$DOCK_G"
  echo "DOCK_G=\"$DOCK_G\"" >> "$GEOM"
  PL_G=$(page_left "$SHOTS/parity_02_graph.png" "$DX_G" "$DY_G" "$DW_G" "$DH_G")
  [ -n "$PL_G" ] || abort "the graph page own splitter hairline was not found (no column is inked in every body row)"
  echo "PL_G=\"$PL_G\"" >> "$GEOM"

  # THE PANEL OWN RECTANGLE, from its root Pane found by the Fit graph action it owns
  # (the route probe_graph_anchor.sh uses): Model::panel is the whole editor rect, i.e.
  # the graph page own widget, so its edges are corroborated by the widget that draws
  # the lane and not by this probe own arithmetic.
  ae dump > "$SCRATCH/dump_graph.txt" 2>/dev/null
  PNAME=$(awk "
    /^ *panel *\\|/ { nm = \$0; sub(/^ *panel *\\| */, \"\", nm) }
    /^ *button *\\| Fit graph\$/ { print nm; exit }
  " "$SCRATCH/dump_graph.txt")
  PANEL=""
  if [ -n "$PNAME" ]; then PANEL=$(ae extents "$PNAME" "panel"); fi
  [ -n "$PANEL" ] || abort "the graph page published no panel Pane owning the Fit graph action: the page rect cannot be corroborated"
  read -r PNX PNY PNW PNH <<< "$PANEL"
  echo "PANEL=\"$PANEL\"" >> "$GEOM"
  echo "PANEL_NAME=\"$PNAME\"" >> "$GEOM"
  say "graph page left $PL_G; panel [${PANEL}] (published by [$PNAME])"

  measure "$SHOTS/parity_02_graph.png" G "$PL_G" "$((PNX + PNW))" "$PNY" "$((PNY + PNH - 1))"
  . "$GEOM"

  # ---- THE TWO CAPTURES ARE TWO PAGES ----
  # Cropped to the area both pages own - from the higher of the two measured band tops,
  # inside both page rectangle - so the difference is what the tab switch drew and not a
  # width or a header. A press that resolved the tab LIST leaves the same page up and this
  # count is 0.
  read -r _tb0 _tb1 <<< "$T_BAND"
  read -r _gb0 _gb1 <<< "$G_BAND"
  read -r _tp0 _tp1 _tp2 _tp3 <<< "$T_PAGE"
  read -r _gp0 _gp1 _gp2 _gp3 <<< "$G_PAGE"
  IY=$_tb0; [ "$_gb0" -gt "$IY" ] && IY=$_gb0
  IX=$_tp0; [ "$_gp0" -gt "$IX" ] && IX=$_gp0
  IEX=$_tp1; [ "$_gp1" -lt "$IEX" ] && IEX=$_gp1
  IEY=$_tp3; [ "$_gp3" -lt "$IEY" ] && IEY=$_gp3
  IW=$((IEX - IX)); IH=$((IEY - IY + 1))
  PAGES_AE=0
  if [ "$IW" -gt 40 ] && [ "$IH" -gt 40 ]; then
    crop "$SHOTS/parity_01_timeline.png" "${IW}x${IH}+${IX}+${IY}" "$SCRATCH/diff_t.png"
    crop "$SHOTS/parity_02_graph.png" "${IW}x${IH}+${IX}+${IY}" "$SCRATCH/diff_g.png"
    PAGES_AE=$(magick compare -metric AE "$SCRATCH/diff_t.png" "$SCRATCH/diff_g.png" null: 2>&1)
    PAGES_AE=${PAGES_AE%% *}
  fi
  echo "PAGES_AE=\"${PAGES_AE:-0}\"" >> "$GEOM"
  say "the two page captures differ by ${PAGES_AE:-0} px in the shared page area (${IW}x${IH} at ${IX},${IY})"

  kill $app 2>/dev/null
  wait $app 2>/dev/null
  say "body done"
' 2>&1 | tail -6

. "$GEOM" 2>/dev/null
if [ -n "${ABORT:-}" ]; then
    echo "steps: $(tr "\n" ";" < "$STEPS" 2>/dev/null)"
    check 1 "the parity probe reached both pages and measured them (it stopped: ${ABORT})"
    summary probe_graph_parity
fi

read -r T_PL T_RR T_BT T_PB <<< "${T_PAGE:-0 0 0 0}"
read -r TFR TFG TFB TFN <<< "${T_FILL:-0 0 0 0}"
read -r T_btop T_bbot <<< "${T_BAND:-0 0}"
read -r T_hy T_hn T_hrr T_hgg T_hbb T_blr T_blg T_blb <<< "${T_HAIR:-0 0 0 0 0 0 0 0}"
read -r T_n0 T_n1 <<< "${T_NUM:-0 0}"
read -r T_k0 T_k1 <<< "${T_TICK:-0 0}"
read -r T_px T_pw T_ax T_ay T_ph <<< "${T_PILL:-0 0 0 0 0}"
read -r T_in T_ix T_iy T_ir T_ig T_ib T_id <<< "${T_INK:-0}"
read -r T_g0 T_g1 T_gc <<< "${T_GLYPH:-0 0 0}"
read -r T_line <<< "${T_LINE:--1}"
read -r T_lt T_lpt T_lh T_lx T_lw T_lr T_lg T_lb <<< "${T_LANE:-0 0 0 0 0 0 0 0}"
read -r T_lbr T_lbg T_lbb <<< "${T_LANEBASE:-0 0 0}"
read -r T_elr T_elg T_elb T_err T_erg T_erb <<< "${T_LANEEDGE:-0 0 0 0 0 0}"

read -r G_PL G_RR G_BT G_PB <<< "${G_PAGE:-0 0 0 0}"
read -r GFR GFG GFB GFN <<< "${G_FILL:-0 0 0 0}"
read -r G_btop G_bbot <<< "${G_BAND:-0 0}"
read -r G_hy G_hn G_hrr G_hgg G_hbb G_blr G_blg G_blb <<< "${G_HAIR:-0 0 0 0 0 0 0 0}"
read -r G_n0 G_n1 <<< "${G_NUM:-0 0}"
read -r G_k0 G_k1 <<< "${G_TICK:-0 0}"
read -r G_px G_pw G_ax G_ay G_ph <<< "${G_PILL:-0 0 0 0 0}"
read -r G_in G_ix G_iy G_ir G_ig G_ib G_id <<< "${G_INK:-0}"
read -r G_g0 G_g1 G_gc <<< "${G_GLYPH:-0 0 0}"
read -r G_line <<< "${G_LINE:--1}"
read -r G_lt G_lpt G_lh G_lx G_lw G_lr G_lg G_lb <<< "${G_LANE:-0 0 0 0 0 0 0 0}"
read -r G_lbr G_lbg G_lbb <<< "${G_LANEBASE:-0 0 0}"
read -r G_elr G_elg G_elb G_err G_erg G_erb <<< "${G_LANEEDGE:-0 0 0 0 0 0}"

read -r DX_T DY_T DW_T DH_T <<< "${DOCK_T:-0 0 0 0}"
read -r DX_G DY_G DW_G DH_G <<< "${DOCK_G:-0 0 0 0}"
read -r PNX PNY PNW PNH <<< "${PANEL:-0 0 0 0}"
read -r _t1x _t1y t1w _t1h <<< "${TAB_T_AT_TL:-0 0 0 0}"
read -r _g1x _g1y g1w _g1h <<< "${TAB_G_AT_TL:-0 0 0 0}"
read -r _t2x _t2y t2w _t2h <<< "${TAB_T_AT_G:-0 0 0 0}"
read -r _g2x _g2y g2w _g2h <<< "${TAB_G_AT_G:-0 0 0 0}"
AE=${PAGES_AE:-0}

assert_rendered "$SHOTS/parity_01_timeline.png" "the timeline page"
assert_rendered "$SHOTS/parity_02_graph.png" "the graph page"

echo "measured: timeline page [${T_PL:-?}..$((T_RR - 1))] x [${T_BT:-?}..${T_PB:-?}]: band rows ${T_btop:-?}..${T_bbot:-?}, hairline row ${T_hy:-?} (rgb ${T_hrr:-?},${T_hgg:-?},${T_hbb:-?}), number rows ${T_n0:-?}..${T_n1:-?}, tick rows ${T_k0:-?}..${T_k1:-?}, pill box page-relative x ${T_px:-?}+${T_pw:-?} at rows ${T_ay:-?}..$((T_ay + T_ph - 1)) (${T_ph:-?} tall), fill ${TFR:-?},${TFG:-?},${TFB:-?}, number ink ${T_ir:-?},${T_ig:-?},${T_ib:-?} (${T_in:-0} px, ${T_id:-?} away from the fill) over glyph rows ${T_g0:-?}..${T_g1:-?} in ${T_gc:-0} columns, playhead line page-relative x ${T_line:-?}, lane rows ${T_lt:-?}..${T_PB:-?} with pill rows ${T_lpt:-?}..$((T_lpt + T_lh - 1)) (${T_lh:-?} thick) at x ${T_lx:-?}+${T_lw:-?}"
echo "measured: graph page [${G_PL:-?}..$((G_RR - 1))] x [${G_BT:-?}..${G_PB:-?}]: band rows ${G_btop:-?}..${G_bbot:-?}, hairline row ${G_hy:-?} (rgb ${G_hrr:-?},${G_hgg:-?},${G_hbb:-?}), number rows ${G_n0:-?}..${G_n1:-?}, tick rows ${G_k0:-?}..${G_k1:-?}, pill box page-relative x ${G_px:-?}+${G_pw:-?} at rows ${G_ay:-?}..$((G_ay + G_ph - 1)) (${G_ph:-?} tall), fill ${GFR:-?},${GFG:-?},${GFB:-?}, number ink ${G_ir:-?},${G_ig:-?},${G_ib:-?} (${G_in:-0} px, ${G_id:-?} away from the fill) over glyph rows ${G_g0:-?}..${G_g1:-?} in ${G_gc:-0} columns, playhead line page-relative x ${G_line:-?}, lane rows ${G_lt:-?}..${G_PB:-?} with pill rows ${G_lpt:-?}..$((G_lpt + G_lh - 1)) (${G_lh:-?} thick) at x ${G_lx:-?}+${G_lw:-?}"
echo "geometry: dock with the Timeline page up [${DOCK_T:-none}], dock with the Graph page up [${DOCK_G:-none}], panel [${PANEL:-none}] published by [${PANEL_NAME:-none}], the two page captures differ by ${AE} px (bar ${PAGES_CHANGE_MIN}), tabs Timeline [${TAB_T_AT_TL:-none}]/[${TAB_T_AT_G:-none}] Graph [${TAB_G_AT_TL:-none}]/[${TAB_G_AT_G:-none}] (widths ${t1w:-?}/${t2w:-?} and ${g1w:-?}/${g2w:-?})"

maxdiff() { awk -v a="$1" -v b="$2" 'BEGIN { d = a - b; if (d < 0) d = -d; print d }'; }
near() { [ "$(maxdiff "$1" "$2")" -le "$3" ]; }
dist3() { awk -v r1="$1" -v g1="$2" -v b1="$3" -v r2="$4" -v g2="$5" -v b2="$6" 'BEGIN { printf "%d", sqrt((r1-r2)^2 + (g1-g2)^2 + (b1-b2)^2) }'; }

# ---- 0. THE ROUTE: THE PAGE TAB BUTTONS, AND TWO DIFFERENT PAGES ----
# THE TAB BAR'S GEOMETRY IS NOT THE WITNESS of the page change: TimeLineHeader inks the
# selected segment (refreshTabStyle's raised knob inside its segmented field) and leaves
# both tabs in their own slots, so
# on this build they measure the same width whichever is current - a width comparison
# asserted nothing and failed a correct layout. The press is proven by the probe's route
# (the tab is addressed BY NAME WITH THE ROLE page tab, and the dock opens on the Timeline
# page) and the change by the captures' CONTENT: two different pages repaint the ruler,
# the content and the panel, while a press that resolves the page-tab LIST leaves one page
# up and the count at 0.
route_ok=1
[ "${t1w:-0}" -gt 0 ] && [ "${g1w:-0}" -gt 0 ] || route_ok=0
[ "${t2w:-0}" -gt 0 ] && [ "${g2w:-0}" -gt 0 ] || route_ok=0
check "$((1 - route_ok))" "the dock header's page tabs are found and pressed by NAME with the role page tab: with the Timeline page up Timeline [${TAB_T_AT_TL:-none}] and Graph [${TAB_G_AT_TL:-none}], after the Graph press Timeline [${TAB_T_AT_G:-none}] and Graph [${TAB_G_AT_G:-none}]. The selected tab is INKED, not inflated (both tabs measure ${t1w:-?}/${g1w:-?}px then ${t2w:-?}/${g2w:-?}px), so the page change is asserted by the captures' content and by each page's own rectangle, not by tab geometry"
check "$([ "${AE:-0}" -ge "$PAGES_CHANGE_MIN" ] && echo 0 || echo 1)" "and the two captures ARE two different pages: ${AE} pixels differ in the shared page area, bar ${PAGES_CHANGE_MIN} (a press that resolved the page-tab LIST leaves the same page up and this count is 0)"
rect_ok=1
[ "${T_RR:-0}" -gt "${T_PL:-0}" ] || rect_ok=0
[ "${G_RR:-0}" -gt "${G_PL:-0}" ] || rect_ok=0
# "Inside the dock" with the one-pixel rounding the panel's own rect is compared with
# (route2 below): the page cannot start left of/top of the dock, and its right/bottom is
# the dock's own edge or one pixel inside it.
[ "${T_PL:-0}" -ge "${DX_T:-0}" ] && [ "${T_RR:-0}" -le "$((DX_T + DW_T + 1))" ] || rect_ok=0
[ "${T_BT:-0}" -ge "${DY_T:-0}" ] && [ "${T_PB:-0}" -le "$((DY_T + DH_T))" ] || rect_ok=0
[ "${G_PL:-0}" -ge "${DX_G:-0}" ] && [ "${G_RR:-0}" -le "$((DX_G + DW_G + 1))" ] || rect_ok=0
[ "${G_BT:-0}" -ge "${DY_G:-0}" ] && [ "${G_PB:-0}" -le "$((DY_G + DH_G))" ] || rect_ok=0
check "$((1 - rect_ok))" "and both pages were measured against a page rect INSIDE the dock that was up in that state: timeline [${T_PL:-?}..$((T_RR - 1))] x [${T_BT:-?}..${T_PB:-?}] in [${DOCK_T:-none}], graph [${G_PL:-?}..$((G_RR - 1))] x [${G_BT:-?}..${G_PB:-?}] in [${DOCK_G:-none}]"

# THE PAGE'S RECT IS THE SPLITTER'S, and on the graph page it is also the panel's own
# published rectangle - the widget that draws the lane. A page-left taken from the dock's
# border would put the panel far outside its own page, and this check names both numbers.
route2_ok=1
[ "${T_RR:-0}" -gt "${T_PL:-0}" ] || route2_ok=0
[ "${G_RR:-0}" -gt "${G_PL:-0}" ] || route2_ok=0
near "${PNX:-0}" "${G_PL:-0}" 1 || route2_ok=0
near "$((PNX + PNW))" "$((DX_G + DW_G))" 1 || route2_ok=0
near "${PNY:-0}" "${G_btop:-0}" 1 || route2_ok=0
near "$((PNY + PNH - 1))" "$((DY_G + DH_G - 1))" 1 || route2_ok=0
check "$((1 - route2_ok))" "the page's left edge is the tree splitter's (the first column inked in EVERY body row), not the dock's border: timeline page starts at x ${T_PL:-?}, graph page at x ${G_PL:-?}; and the graph page's own published rectangle [${PANEL:-none}] is the page measured from that splitter and the dock's own rectangle [${DOCK_G:-none}] - left ${PNX:-?} vs ${G_PL:-?}, right $((PNX + PNW)) vs $((DX_G + DW_G)), top ${PNY:-?} vs band top ${G_btop:-?}, bottom $((PNY + PNH - 1)) vs $((DY_G + DH_G - 1))"

# ---- 1. THE RULER'S ROWS: band top..bottom, and its bottom rule ----
band_ok=1
near "${T_btop:-0}" "${G_btop:-0}" 1 || band_ok=0
near "${T_bbot:-0}" "${G_bbot:-0}" 1 || band_ok=0
[ "$((T_bbot - T_btop))" -eq "$((BAND_HEIGHT - 1))" ] || band_ok=0
[ "$((G_bbot - G_btop))" -eq "$((BAND_HEIGHT - 1))" ] || band_ok=0
check "$((1 - band_ok))" "THE RULER BAND IS THE SAME STRIP ON BOTH PAGES: rows ${T_btop:-?}..${T_bbot:-?} on the timeline against ${G_btop:-?}..${G_bbot:-?} on the graph, and each band is its ${BAND_HEIGHT} rows (top ${T_btop:-?}/${G_btop:-?} from the pill's own kPillTop ${PILL_TOP} above it, bottom = top + $((BAND_HEIGHT - 1)))"
hair_ok=1
near "${T_hy:-0}" "${G_hy:-0}" 1 || hair_ok=0
[ "$((T_hy - T_bbot))" -eq 1 ] || hair_ok=0
[ "$((G_hy - G_bbot))" -eq 1 ] || hair_ok=0
[ "$(dist3 "${T_hrr:-0}" "${T_hgg:-0}" "${T_hbb:-0}" "${T_blr:-0}" "${T_blg:-0}" "${T_blb:-0}")" -gt 3 ] || hair_ok=0
[ "$(dist3 "${G_hrr:-0}" "${G_hgg:-0}" "${G_hbb:-0}" "${G_blr:-0}" "${G_blg:-0}" "${G_blb:-0}")" -gt 3 ] || hair_ok=0
[ "${T_hn:-0}" -ge "$((T_RR - T_PL - 8))" ] || hair_ok=0
[ "${G_hn:-0}" -ge "$((G_RR - G_PL - 8))" ] || hair_ok=0
# THE RULE'S VALUE IS THE TOKEN ON BOTH PAGES. Both views draw it opaque - the timeline
# turns the AA hint off for its 1px pen and the panel draws theme::Graph::rulerLine at
# full strength - so the two rows' modal colours must be the same colour. The tolerance
# is the capture's own channel rounding; the half-tone defect is 13 steps away (51
# against 64 on the dark palette), so it cannot hide inside it.
near "${T_hrr:-0}" "${G_hrr:-0}" 2 || hair_ok=0
near "${T_hgg:-0}" "${G_hgg:-0}" 2 || hair_ok=0
near "${T_hbb:-0}" "${G_hbb:-0}" 2 || hair_ok=0
check "$((1 - hair_ok))" "and the hairline under the band is THE SAME ROW WITH THE SAME INK on both, one row under each band: row ${T_hy:-?} (band bottom ${T_bbot:-?}) against row ${G_hy:-?} (band bottom ${G_bbot:-?}), carrying modal rgb ${T_hrr:-?},${T_hgg:-?},${T_hbb:-?} on the timeline against ${G_hrr:-?},${G_hgg:-?},${G_hbb:-?} on the graph, within 2 steps per channel. The VALUE is compared because both views now draw the one palette token opaque (timeline ${T_hrr:-?},${T_hgg:-?},${T_hbb:-?}; graph ${G_hrr:-?},${G_hgg:-?},${G_hbb:-?}): a return of either the timeline's stray-AA half tone or the panel's old half-coverage reproduction moves one page to 51,51,51 and fails here. The rule is also INKED there (row modal differs from the band's last row: ${T_hrr:-?},${T_hgg:-?},${T_hbb:-?} vs ${T_blr:-?},${T_blg:-?},${T_blb:-?} on the timeline, ${G_hrr:-?},${G_hgg:-?},${G_hbb:-?} vs ${G_blr:-?},${G_blg:-?},${G_blb:-?} on the graph) and spans the page's columns (${T_hn:-0} of ${T_RR:-?}-${T_PL:-?} on the timeline, ${G_hn:-0} of ${G_RR:-?}-${G_PL:-?} on the graph)"

# ---- 2. THE NUMBER GLYPH ROWS AND THE TICK ROWS ----
num_ok=1
near "${T_n0:-0}" "${G_n0:-0}" 1 || num_ok=0
near "${T_n1:-0}" "${G_n1:-0}" 1 || num_ok=0
[ "${T_n0:-0}" -ge "${T_btop:-0}" ] && [ "${T_n1:-0}" -le "$((T_btop + NUMBER_HEIGHT + 1))" ] || num_ok=0
[ "${G_n0:-0}" -ge "${G_btop:-0}" ] && [ "${G_n1:-0}" -le "$((G_btop + NUMBER_HEIGHT + 1))" ] || num_ok=0
[ "${T_n1:-0}" -lt "${T_k0:-0}" ] && [ "${G_n1:-0}" -lt "${G_k0:-0}" ] || num_ok=0
check "$((1 - num_ok))" "THE RULER'S NUMBER ROWS ARE THE SAME ROWS ON BOTH PAGES: ${T_n0:-?}..${T_n1:-?} on the timeline against ${G_n0:-?}..${G_n1:-?} on the graph, each inside its band's number row (band top + 1..+${NUMBER_HEIGHT}) and above its ticks. Read as an ink ROW RUN with the pill's columns cut out, so a glyph's antialiased edge pixel cannot fail it (one-row tolerance)"

tick_ok=1
near "${T_k0:-0}" "${G_k0:-0}" 1 || tick_ok=0
near "${T_k1:-0}" "${G_k1:-0}" 1 || tick_ok=0
[ "$((T_k1 - T_k0 + 1))" -eq "$TICK_ROWS" ] || tick_ok=0
[ "$((G_k1 - G_k0 + 1))" -eq "$TICK_ROWS" ] || tick_ok=0
[ "${T_k1:-0}" -eq "${T_bbot:-0}" ] || tick_ok=0
[ "${G_k1:-0}" -eq "${G_bbot:-0}" ] || tick_ok=0
check "$((1 - tick_ok))" "and THE TICK ROWS ARE THE SAME ROWS: ${T_k0:-?}..${T_k1:-?} against ${G_k0:-?}..${G_k1:-?}, each run ${TICK_ROWS} rows ending ON the band's bottom row (${T_bbot:-?}/${G_bbot:-?}: the baseline plus the five the tick length inks). Read as ROWS, not as the ticks' ink value"

# ---- 3. THE PLAYHEAD PILL'S BOX ----
found_ok=1
[ "${T_pw:-0}" -gt 0 ] && [ "${T_ph:-0}" -gt 0 ] || found_ok=0
[ "${G_pw:-0}" -gt 0 ] && [ "${G_ph:-0}" -gt 0 ] || found_ok=0
[ "$TFR" -gt 0 ] && [ "$GFR" -gt 0 ] || found_ok=0
check "$((1 - found_ok))" "both pages draw the playhead pill in their ruler band and it was found by its fill colour (timeline ${TFR:-?},${TFG:-?},${TFB:-?} ${TFN:-0} px, graph ${GFR:-?},${GFG:-?},${GFB:-?} ${GFN:-0} px): a page with no pill has nothing to compare"

box_ok=1
near "${T_ay:-0}" "${G_ay:-0}" 1 || box_ok=0
near "${T_ph:-0}" "${G_ph:-0}" 1 || box_ok=0
near "${T_ph:-0}" "$NUMBER_HEIGHT" 1 || box_ok=0
near "${G_ph:-0}" "$NUMBER_HEIGHT" 1 || box_ok=0
# ...and each box is the model's own row INSIDE its strip (kPillTop), so a clipped pill
# fails even if both pages were clipped the same way: the reference is the strip's top,
# not the other capture.
near "$((T_ay - ${T_btop:-0}))" "$PILL_TOP" 1 || box_ok=0
near "$((G_ay - ${G_btop:-0}))" "$PILL_TOP" 1 || box_ok=0
check "$((1 - box_ok))" "THE PILL'S BOX is the same box on both pages, and each is the model's ${NUMBER_HEIGHT}-row box at kPillTop ${PILL_TOP} of its own ${BAND_HEIGHT}-row strip: rows ${T_ay:-?}..$((T_ay + T_ph - 1)) (${T_ph:-?} tall) at ${T_ay} - ${T_btop:-?} = $((T_ay - ${T_btop:-0})) rows into the timeline's strip ${T_btop:-?}..${T_bbot:-?}, against rows ${G_ay:-?}..$((G_ay + G_ph - 1)) (${G_ph:-?} tall) at ${G_ay} - ${G_btop:-?} = $((G_ay - ${G_btop:-0})) rows into the graph's ${G_btop:-?}..${G_bbot:-?} - the two rulers' strips are the same rows of the same page"
cols_ok=1
# EQUAL, not within an AA band: the pill is the model's rect drawn solid, and the model
# measures it from the very string TimeCursor measures, with the host's own widget font
# (the dock's stylesheet font). A tolerance here hid a 1px left shift and a 2px width
# difference caused by measuring with QFont() instead.
[ "${T_px:-0}" -eq "${G_px:-0}" ] || cols_ok=0
[ "${T_pw:-0}" -eq "${G_pw:-0}" ] || cols_ok=0
check "$((1 - cols_ok))" "and its column is the same column OF ITS PAGE: ${T_px:-?}+${T_pw:-?} from the timeline page's splitter against ${G_px:-?}+${G_pw:-?} from the graph page's (the pages are different widths, so x is page-relative). Start and width are EQUAL: the pill is the model's rect, measured from the same string with the same widget font TimeCursor uses - the 1px shift and 2px width difference of the QFont()-measured box are gone"

# ---- 4. THE PILL'S INKS: the fill token, and the number's glyph ----
fill_ok=1
near "$TFR" "$GFR" 6 || fill_ok=0
near "$TFG" "$GFG" 6 || fill_ok=0
near "$TFB" "$GFB" 6 || fill_ok=0
check "$((1 - fill_ok))" "the pill's FILL is one token on both pages: ${TFR:-?},${TFG:-?},${TFB:-?} against ${GFR:-?},${GFG:-?},${GFB:-?} (theme::Colors::accentBright through theme::Graph::selected() and TimeCursor's badge). Read as the pill's MODAL colour - a solid interior, so the corner AA does not enter it"
glyph_ok=1
[ "${T_in:-0}" -ge 10 ] && [ "${G_in:-0}" -ge 10 ] || glyph_ok=0
[ "${T_id:-0}" -ge 30 ] && [ "${G_id:-0}" -ge 30 ] || glyph_ok=0
near "${T_g0:-0}" "${G_g0:-0}" 1 || glyph_ok=0
near "${T_g1:-0}" "${G_g1:-0}" 1 || glyph_ok=0
[ "${T_gc:-0}" -ge 4 ] && [ "${G_gc:-0}" -ge 4 ] || glyph_ok=0
# ...and the glyph is INSIDE its own pill: a box that lost its lower rows would otherwise
# still find a truncated glyph and compare it with a full one.
[ "${T_g0:-0}" -ge "${T_ay:-0}" ] && [ "${T_g1:-0}" -lt "$((T_ay + T_ph))" ] || glyph_ok=0
[ "${G_g0:-0}" -ge "${G_ay:-0}" ] && [ "${G_g1:-0}" -lt "$((G_ay + G_ph))" ] || glyph_ok=0
check "$((1 - glyph_ok))" "THE NUMBER INSIDE THE PILL OCCUPIES THE SAME GLYPH ROWS ON BOTH PAGES: ${T_g0:-?}..${T_g1:-?} over ${T_gc:-0} columns against ${G_g0:-?}..${G_g1:-?} over ${G_gc:-0} columns, from ${T_in:-0}/${G_in:-0} ink pixels far from the fill (${T_id:-?}/${G_id:-?} away, so the glyph is drawn, not a near-copy of the fill). This is the shipped defect's shape: the timeline writes theme::Colors::accentBrightText on the accent fill, and the panel's near-white token left two lit pixels"
ink_ok=1
near "${T_ir:-0}" "${G_ir:-0}" 24 || ink_ok=0
near "${T_ig:-0}" "${G_ig:-0}" 24 || ink_ok=0
near "${T_ib:-0}" "${G_ib:-0}" 24 || ink_ok=0
check "$((1 - ink_ok))" "and the glyph's CORE ink is one token on both: the pixel farthest from the fill is ${T_ir:-?},${T_ig:-?},${T_ib:-?} on the timeline against ${G_ir:-?},${G_ig:-?},${G_ib:-?} on the graph, within the 24-step antialiasing band. The glyph's edge pixels are rasteriser AA and are NOT compared - the row run above is"

# ---- 4b. ONE STATE: the two captures are the SAME FRAME at the SAME ZOOM ----
# The probe seeks once and zooms once, both on the Timeline page, and the Graph capture is
# taken without touching the state. The playhead LINE is the current frame's column on the
# frame axis both views share, so the assertion is the direct same-frame check: the
# playhead's COLUMN moves if the tab switch re-seeked, while the pill's box alone would
# only report the frame text. The line must lie under its own pill and at the same
# page-relative column on both pages.
line_ok=1
[ "${T_line:--1}" -ge 0 ] && [ "${G_line:--1}" -ge 0 ] || line_ok=0
near "${T_line:--1}" "${G_line:--1}" 2 || line_ok=0
near "${T_line:--1}" "$((T_px + T_pw / 2))" 2 || line_ok=0
near "${G_line:--1}" "$((G_px + G_pw / 2))" 2 || line_ok=0
check "$((1 - line_ok))" "and the two captures are ONE STATE AT ONE FRAME: the probe seeks ONCE in the timeline's ruler, zooms ONCE, and presses the Graph tab without re-seeking, so the playhead line - the frame's column on the shared axis - is at the same page-relative x on both: ${T_line:-?} on the timeline (its pill's centre ${T_px:-?}+$((T_pw / 2))) against ${G_line:-?} on the graph (centre ${G_px:-?}+$((G_pw / 2))), within the AA band's 2px"

# ---- 5. BOTH SCROLL BAR LANES: the page's last 12 rows, flush at both edges ----
lane_rows_ok=1
[ "${T_lh:-0}" -eq "$PILL_ROWS" ] && [ "${G_lh:-0}" -eq "$PILL_ROWS" ] || lane_rows_ok=0
[ "${T_lpt:-0}" -eq "$((T_PB - LANE_ROWS + 1 + (LANE_ROWS - PILL_ROWS) / 2))" ] || lane_rows_ok=0
[ "${G_lpt:-0}" -eq "$((G_PB - LANE_ROWS + 1 + (LANE_ROWS - PILL_ROWS) / 2))" ] || lane_rows_ok=0
[ "$((T_lpt - T_PB))" -eq "$((G_lpt - G_PB))" ] || lane_rows_ok=0
check "$((1 - lane_rows_ok))" "the horizontal scroll bar is the SAME LANE on both pages: its ${PILL_ROWS}px pill sits at rows ${T_lpt:-?}..$((T_lpt + T_lh - 1)) on the timeline and ${G_lpt:-?}..$((G_lpt + G_lh - 1)) on the graph, i.e. page.bottom()-7..page.bottom()-4 on both (${T_PB:-?}/${G_PB:-?} are the pages' own bottom rows) - the lane is the page's last ${LANE_ROWS} rows, flush with the bottom edge, no gap and no trough"
lane_cols_ok=1
[ "${T_lx:-0}" -ge 0 ] && [ "$((T_lx + T_lw))" -le "$((T_RR - T_PL))" ] || lane_cols_ok=0
[ "${G_lx:-0}" -ge 0 ] && [ "$((G_lx + G_lw))" -le "$((G_RR - G_PL))" ] || lane_cols_ok=0
[ "$(dist3 "${T_elr:-0}" "${T_elg:-0}" "${T_elb:-0}" "${T_lbr:-0}" "${T_lbg:-0}" "${T_lbb:-0}")" -le 6 ] || lane_cols_ok=0
[ "$(dist3 "${T_err:-0}" "${T_erg:-0}" "${T_erb:-0}" "${T_lbr:-0}" "${T_lbg:-0}" "${T_lbb:-0}")" -le 6 ] || lane_cols_ok=0
[ "$(dist3 "${G_elr:-0}" "${G_elg:-0}" "${G_elb:-0}" "${G_lbr:-0}" "${G_lbg:-0}" "${G_lbb:-0}")" -le 6 ] || lane_cols_ok=0
[ "$(dist3 "${G_err:-0}" "${G_erg:-0}" "${G_erb:-0}" "${G_lbr:-0}" "${G_lbg:-0}" "${G_lbb:-0}")" -le 6 ] || lane_cols_ok=0
check "$((1 - lane_cols_ok))" "and each lane spans its page's columns with nothing drawn at the edges: the pill's run is inside the page (timeline x ${T_lx:-?}+${T_lw:-?} of $((T_RR - T_PL)), graph x ${G_lx:-?}+${G_lw:-?} of $((G_RR - G_PL))), and the lane's own base is what the page's first and last-but-one columns carry on the slider's row (timeline ${T_elr:-?},${T_elg:-?},${T_elb:-?} / ${T_err:-?},${T_erg:-?},${T_erb:-?} against base ${T_lbr:-?},${T_lbg:-?},${T_lbb:-?}; graph ${G_elr:-?},${G_elg:-?},${G_elb:-?} / ${G_err:-?},${G_erg:-?},${G_erb:-?} against base ${G_lbr:-?},${G_lbg:-?},${G_lbb:-?}) - the invisible trough paints no rail, no margin and no border, so the lane's surface reaches both page edges"
travel_ok=1
[ "${T_lw:-0}" -gt 0 ] && [ "${T_lw:-0}" -lt "$((T_RR - T_PL))" ] || travel_ok=0
[ "${G_lw:-0}" -gt 0 ] && [ "${G_lw:-0}" -lt "$((G_RR - G_PL))" ] || travel_ok=0
check "$((1 - travel_ok))" "and both bars exist WITH TRAVEL in this state: the timeline's pill is ${T_lw:-?}px of its $((T_RR - T_PL))px page and the graph's is ${G_lw:-?}px of its $((G_RR - G_PL))px page (the frame window is a fraction of the 600-frame range at the ${ZOOM_NOTCHES} notches this probe zooms in, so a thumb shorter than the lane is the proof that the bar is not suppressed)"

summary probe_graph_parity
