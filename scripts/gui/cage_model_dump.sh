#!/bin/bash
set -u
BIN="$1"; OUT="$2"
AE_PANEL_MODEL_DUMP="$OUT.json" "$BIN" "/home/gambot/Downloads/AE ForTesting/AnimeEffects/I_think_its_cursed_honestly.anie" >/tmp/dmp.log 2>&1 &
APP=$!
sleep 22
for x in 10 18 22; do xdotool mousemove $x 477; sleep 0.3; xdotool click 1; sleep 0.8; done
xdotool mousemove 60 500; sleep 0.4; xdotool click 1; sleep 2
xdotool mousemove 93 443; sleep 0.4; xdotool click 1; sleep 3
xdotool mousemove 900 690; sleep 2
grim "$OUT.png" 2>/dev/null
cp "$OUT.json" "$OUT-final.json" 2>/dev/null
kill $APP 2>/dev/null; sleep 1
