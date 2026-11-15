#!/bin/bash
# The destination picker's scroll bar - D23 in docs/export-dialog-behavior.md.
#
# The reported defect was "inconsistent with the app and does nothing on drag", and
# a probe that only asserts the bar EXISTS cannot see either half of that. So this
# one drags the bar with motion and reads the list's position off a ROW's
# rectangle: the rows are addressed by the destination names the model gives them,
# and the bar's coordinates are derived from the search field's own rectangle
# rather than typed in. Nothing here is a pixel match.
#
# What it establishes, in order:
#   a press on the handle with no motion does not scroll (a decorative bar passes
#     the opposite of this too - nothing happens either way, so the next line is
#     what separates them);
#   a drag with motion scrolls the content, and a second identical drag moves it by
#     the same amount (the list tracks the pointer, not a jump);
#   dragging back returns the list to where it was;
#   with the content filtered down to one row there is nothing to scroll and the
#     bar is not there to drag (ScrollBar.AsNeeded, not "always drawn").
#
# Recorded against the export flow revision below and addressed by name, never by
# pixel.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/_lib.sh" "$@"
export RECORDED_AGAINST=2

YS=/tmp/probe_scroll_ys.txt
GEOM=/tmp/probe_scroll_geom.txt
rm -f "$YS" "$GEOM"

xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
  '"$APP"' "'"$PROJ"'" >/tmp/probe_app.log 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID
  # Ask whether the window is up rather than sleeping a guessed number of seconds.
  if ! wait_ready 120; then
      echo "TIMEOUT waiting for the main window" > '"$GEOM"'
      kill $app 2>/dev/null; wait $app 2>/dev/null; exit 0
  fi
  W=$(xdotool getactivewindow 2>/dev/null || xdotool search --name AnimeEffects | tail -1)
  xdotool windowactivate --sync $W 2>/dev/null
  # The top-level windows BEFORE the picker opens. The dialog is a new one, so the
  # difference below identifies it without depending on its title (a translated
  # string) or on a window manager (there is none under Xvfb).
  W_BEFORE=$(xdotool search --onlyvisible --name "." 2>/dev/null | sort | tr "\n" " ")

  ae_press "File" "the File menu opens"
  ae_press "Export project as" "the export entry is there"
  require_export_flow
  sleep 1.5

  # Geometry, asked of the application. The search field spans the list area, and
  # the bar is the 8px gutter at its right edge; the handle is at the top of the
  # track while the list is at the top.
  #
  # THE OFFSET IS NOT OPTIONAL. A QML item inside a QQuickWidget reports its rectangle
  # in the coordinates of the WIDGET, not of the screen (the same defect ae_activate
  # documents): the search field reports x=14 while it is really at x=75, because the
  # dialog window sits at x=61. A drag has no accessibility action to fall back on, so
  # the dialog origin is added here. First measured the hard way: without it every drag
  # landed 61px to the left, on a SECTION ROW, and the probe read a section toggling
  # open as the list moving.
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

  box=$(ae extents "Search destinations")
  [ -n "$box" ] || { echo "NO SEARCH FIELD" > '"$GEOM"'; kill $app; wait $app 2>/dev/null; exit 0; }
  read -r SX SY SW SH <<< "$box"
  BX=$((SX + SW - 4 + OX))
  BY=$((SY + SH + 12 + OY))
  PY=$BY

  # The row to measure. With no query the model is CLOSED by default, so the rows
  # with a rectangle are the platform SECTION rows - a collapsed destination reports
  # nothing, which is how this probe first read an empty y0 and blamed the widget.
  # Which sections are inside the viewport depends on the destination file, so the row
  # is discovered rather than typed in, and it is the LOWEST one on screen (greatest y)
  # because scrolling down moves it UP and it then survives the whole test. Taking the
  # last name in the list below instead is a mistake this probe already made: the
  # ordering of that list is not the order on screen, and the row it chose left the
  # viewport on the second drag.
  ROW=""
  for cand in Discord Dailymotion DeviantArt Facebook Instagram Mastodon Messenger \
              Pinterest Reddit Telegram TikTok Tumblr Twitter Vimeo YouTube Bluesky; do
    y=$(ae_y "$cand")
    if [ -n "$y" ] && { [ -z "$ROW" ] || [ "$y" -gt "$y0" ]; }; then
      ROW=$cand; y0=$y
    fi
  done
  echo "dialog=${OX},${OY} search=$SX,$SY,${SW}x${SH} bar=$BX,$BY row=$ROW y0=$y0" >> '"$GEOM"'

  # (a) press on the handle, no motion.
  xdotool mousemove --sync $BX $PY; sleep 0.2
  xdotool mousedown 1; sleep 0.3; xdotool mouseup 1; sleep 0.5
  y1=$(ae_y "$ROW")

  # (b) first drag: down 40, which scrolls the content. 40 rather than 100 because the
  # list moves further than the pointer does (the ratio of content to viewport), and the
  # measured row has to stay inside the viewport for the whole test.
  ae_drag $BX $PY 0 40
  PY=$((PY + 40))
  y2=$(ae_y "$ROW")

  # (c) the same drag again, from where the handle now is. The press MUST follow
  # the handle: pressing the track above it pages instead of dragging.
  ae_drag $BX $PY 0 40
  PY=$((PY + 40))
  y3=$(ae_y "$ROW")

  # (d) drag back up by the total.
  ae_drag $BX $PY 0 -80
  PY=$BY
  y4=$(ae_y "$ROW")

  # (e) nothing overflows: filter to the one row the query matches.
  ae set "Search destinations" "discord sticker" >/dev/null
  sleep 1.5
  y5=$(ae_y "Discord sticker")
  ae_drag $BX $BY 0 60
  y6=$(ae_y "Discord sticker")

  echo "y0=$y0 y1=$y1 y2=$y2 y3=$y3 y4=$y4 y5=$y5 y6=$y6" > '"$YS"'
  xdotool mousemove 700 300; sleep 0.5
  import -window root '"$SHOTS"'/scroll_01_picker.png
  kill $app 2>/dev/null; wait $app 2>/dev/null
' 2>&1 | tail -4

if grep -q TIMEOUT "$GEOM" 2>/dev/null; then
    check 1 "the application became ready ($(cat "$GEOM"))"
    summary probe_scroll
    exit 1
fi

eval "$(cat "$YS" 2>/dev/null)"
echo "geometry: $(cat "$GEOM" 2>/dev/null)"
echo "positions: $(cat "$YS" 2>/dev/null)"

if [ -z "${y0:-}" ]; then
    check 1 "the list's rows are addressable (got y0='${y0:-}')"
    summary probe_scroll
    exit 1
fi

check 0 "the picker's list rows expose a usable rectangle (y0=$y0)"
# (a) A press with no motion must not move the list. This is the control for (b):
# together they say the MOVEMENT comes from the motion, not from the press.
if [ "${y1:-}" = "$y0" ]; then
    check 0 "a press on the handle without motion does not scroll"
else
    check 1 "a press on the handle without motion does not scroll (moved $y0 -> ${y1:-})"
fi
# (b) The defect: this used to do nothing at all.
d1=$(( ${y1:-0} - ${y2:-0} ))
if [ "${y2:-0}" -lt "${y1:-0}" ]; then
    check 0 "dragging the bar with motion scrolls the list (moved up ${d1}px)"
else
    check 1 "dragging the bar with motion scrolls the list (y ${y1:-} -> ${y2:-})"
fi
# (c) The second drag moves it by the same amount: it tracks the pointer.
d2=$(( ${y2:-0} - ${y3:-0} ))
if [ "$d2" -gt 0 ] && [ $(( d1 - d2 )) -le 4 ] && [ $(( d2 - d1 )) -le 4 ]; then
    check 0 "two identical drags move the list by the same amount (${d1}px then ${d2}px)"
else
    check 1 "two identical drags move the list by the same amount (${d1}px then ${d2}px)"
fi
# (d) Reversible: the drag is connected to the position, not a one-way jump.
back=$(( ${y4:-0} - ${y0:-0} ))
if [ $back -le 4 ] && [ $back -ge -4 ]; then
    check 0 "dragging back returns the list to its previous position (${y0} -> ${y4})"
else
    check 1 "dragging back returns the list to its previous position (${y0} -> ${y4})"
fi
# (e) AsNeeded: one row cannot overflow, so there is no bar to drag.
if [ -n "${y5:-}" ] && [ "${y6:-}" = "${y5:-}" ]; then
    check 0 "with one matching row the bar is not there to drag (ScrollBar.AsNeeded)"
else
    check 1 "with one matching row the bar is not there to drag (${y5:-} -> ${y6:-})"
fi

summary probe_scroll
