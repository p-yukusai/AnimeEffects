#!/bin/bash
set -u
BIN="$1"; OUT="$2"
"$BIN" "/home/gambot/Downloads/AE ForTesting/AnimeEffects/I_think_its_cursed_honestly.anie" >/tmp/cageapp.log 2>&1 &
APP=$!
sleep 22
# expand (the indicator's hit area) and select 'head', then the Graph tab
for x in 10 18 22; do xdotool mousemove $x 477; sleep 0.3; xdotool click 1; sleep 0.8; done
xdotool mousemove 60 500; sleep 0.4; xdotool click 1; sleep 2
xdotool mousemove 93 443; sleep 0.4; xdotool click 1; sleep 3
grim "$OUT-1-base.png" 2>/dev/null
xdotool mousemove 800 650; sleep 2; grim "$OUT-2-nohover.png" 2>/dev/null
xdotool mousemove 593 511; sleep 2; grim "$OUT-3-hover-key.png" 2>/dev/null
xdotool mousemove 450 560 mousedown 3 mouseup 3; sleep 2; grim "$OUT-4-menu.png" 2>/dev/null
xdotool key Escape; sleep 1
xdotool mousemove 520 600 mousedown 1; for x in 560 600 640; do xdotool mousemove $x 630; sleep 0.12; done
grim "$OUT-5-marquee.png" 2>/dev/null; xdotool mouseup 1; sleep 2; grim "$OUT-6-after.png" 2>/dev/null
echo "errors: $(grep -icE 'error|assert|segmentation' /tmp/cageapp.log)"
kill $APP 2>/dev/null; sleep 1
