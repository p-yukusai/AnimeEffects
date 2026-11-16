#!/bin/bash
set -u
unset WAYLAND_DISPLAY
export QT_QPA_PLATFORM=xcb QT_QPA_PLATFORMTHEME=generic QT_FORCE_STDERR_LOGGING=1
export QT_ACCESSIBILITY=1 QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1
"$1" "/home/gambot/Downloads/AE ForTesting/AnimeEffects/I_think_its_cursed_honestly.anie" >/tmp/gt.log 2>&1 &
APP=$!
OUT="$2"
BIN="$1"
sleep 22
R=$(python3 scripts/gui/ae_ui.py extents "animation" "table cell" 2>/dev/null | head -1)
echo "row: $R"
set -- $R
if [ $# -ge 4 ]; then xdotool mousemove $(( $1 - 12 )) $(( $2 + $4 / 2 )) click 1; sleep 2; fi
# the Graph tab, by name through the accessibility bus
python3 scripts/gui/ae_ui.py click "Graph" 2>&1 | head -2
sleep 2
ffmpeg -loglevel error -f x11grab -draw_mouse 0 -video_size 1440x900 -i "$DISPLAY" -frames:v 1 -y "$OUT"
echo "log warnings:"; grep -icE "error|assert|crash" /tmp/gt.log || true
kill $APP 2>/dev/null; wait $APP 2>/dev/null
echo done
