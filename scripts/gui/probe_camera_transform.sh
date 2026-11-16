#!/bin/bash
# THE TRANSFORM CACHE, MEASURED.
#
# A camera move must not re-run the skinning pass for a layer whose transform inputs did not
# change (`LayerNode::transformShape` caches on the expansion's rebuild stamp). The app writes one
# line per painted frame to AE_FRAME_STATS (gl::FrameStats); this probe pans and zooms the canvas -
# camera only - and asks how many of the frames THAT caused skipped the pass.
#
# The control is a DOCUMENT change (one frame forward): it must re-run the pass, or the skip above
# would only prove that nothing is ever transformed.
#
#   scripts/gui/probe_camera_transform.sh [project]
#
# Exit: 0 ok, 1 the pass still runs on camera-only frames (or the control did not run it),
#       97 no display / no binary / the app never painted / memory guard fired.
set -u
here=$(cd "$(dirname "$0")" && pwd)
APP=${AE_APP:-$here/../../build/src/gui/Release/AnimeEffects}
PROJ=${1:-$HOME/Downloads/test.anie}
D=${AE_PROBE_DISPLAY:-87}
ROOT=${AE_PROBE_ROOT:-/tmp/ae-probe-transform}
CAP_KB=${AE_MAX_RSS_KB:-4194304}   # a guard, far above this project

command -v Xvfb >/dev/null || { echo "probe: no Xvfb"; exit 97; }
[ -x "$APP" ] || { echo "probe: no binary at $APP"; exit 97; }
[ -f "$PROJ" ] || { echo "probe: no project at $PROJ"; exit 97; }

rm -rf "$ROOT"; mkdir -p "$ROOT"/{config,cache,data}
LOG="$ROOT/frames.log"
unset WAYLAND_DISPLAY DISPLAY
Xvfb ":$D" -screen 0 1440x900x24 -nolisten tcp > "$ROOT/xvfb.log" 2>&1 &
XPID=$!
sleep 1

env -u WAYLAND_DISPLAY -u DISPLAY DISPLAY=":$D" QT_QPA_PLATFORM=xcb QT_QPA_PLATFORMTHEME=generic \
    XDG_CONFIG_HOME="$ROOT/config" XDG_CACHE_HOME="$ROOT/cache" XDG_DATA_HOME="$ROOT/data" \
    AE_FRAME_STATS="$LOG" \
    "$APP" "$PROJ" > "$ROOT/app.log" 2>&1 &
APID=$!

( while kill -0 "$APID" 2>/dev/null; do
    rss=$(awk '/VmRSS/{print $2}' "/proc/$APID/status" 2>/dev/null)
    if [ -n "${rss:-}" ] && [ "$rss" -gt "$CAP_KB" ]; then
        echo "probe: memory guard killed the app at ${rss}kB"; kill -9 "$APID" 2>/dev/null; break
    fi
    sleep 1
  done ) &

lines() { wc -l "$LOG" 2>/dev/null | awk '{print $1+0}'; }

for i in $(seq 1 120); do
    wid=$(DISPLAY=":$D" xwininfo -root -tree 2>/dev/null | grep '"AnimeEffects": ("' | awk '{print $1}' | head -1)
    [ -n "${wid:-}" ] && break
    sleep 0.5
done
if [ -z "${wid:-}" ]; then
    echo "probe: the window never appeared"; kill "$APID" "$XPID" 2>/dev/null; exit 97
fi
DISPLAY=":$D" xdotool windowsize "$wid" 1440 900 2>/dev/null

# SETTLED: at least two frames and no new line for a second. The load's first paints and a resize
# both rebuild the state, and those are not the camera's.
last=-1; stable=0
for i in $(seq 1 240); do
    n=$(lines)
    if [ "$n" -ge 2 ] && [ "$n" -eq "$last" ]; then stable=$((stable + 1)); else stable=0; fi
    last=$n
    [ "$stable" -ge 2 ] && break
    sleep 0.5
done
BASE=$(lines)
if [ "$BASE" -lt 2 ]; then echo "probe: the app never painted"; kill "$APID" "$XPID" 2>/dev/null; exit 97; fi
echo "probe: settled after $BASE frame(s)"

# A CAMERA-ONLY PAN, aimed at the window's own centre (nothing sizes an Xvfb window: no WM, so a
# guessed coordinate can land outside it and silently do nothing), then a camera-only ZOOM.
eval "$(DISPLAY=":$D" xdotool getwindowgeometry --shell "$wid")"
CX=$((WIDTH / 2)); CY=$((HEIGHT / 2))
echo "probe: viewport ${WIDTH}x${HEIGHT}, panning from $CX,$CY"
DISPLAY=":$D" xdotool mousemove "$CX" "$CY" mousedown 2
for i in $(seq 1 10); do DISPLAY=":$D" xdotool mousemove_relative -- 12 6; done
DISPLAY=":$D" xdotool mouseup 2
sleep 1
for i in 1 2 3; do DISPLAY=":$D" xdotool click 4; sleep 0.1; done
sleep 2

CAM=$(tail -n +$((BASE + 1)) "$LOG")
echo "$CAM" | tail -3
# A STEADY CAMERA IS A RUN OF FRAMES WITH mesh=0. A stray re-skin is reported, not failed: a
# background event (a thumbnail, a pending edit) can legitimately rebuild the state once. Before
# the cache there was no such run at all.
read -r RUN STRAYS <<< "$(echo "$CAM" | awk '
    { m=-1; for (i=1;i<=NF;i++) if ($i ~ /^mesh=/) { split($i,a,"="); m=a[2] }
      if (m==0) { cur++; if (cur>best) best=cur } else { cur=0; if (m>0) bad++ } }
    END { print (best+0), (bad+0) }')"
echo "probe: longest camera-only run without a skinning pass: $RUN frame(s); strays: $STRAYS"

# THE CONTROL: one frame forward is a DOCUMENT change and must re-run the pass.
TWOBASE=$(lines)
DISPLAY=":$D" xdotool key --clearmodifiers ctrl+Right
sleep 2
MOVED=$(tail -n +$((TWOBASE + 1)) "$LOG" 2>/dev/null | awk '
    { for (i=1;i<=NF;i++) if ($i ~ /^mesh=/) { split($i,a,"="); if (a[2] > 0) c++ } }
    END { print c+0 }')
echo "probe: after one frame step, $MOVED frame(s) re-ran the skinning pass"

kill "$APID" 2>/dev/null
sleep 1
kill "$XPID" 2>/dev/null

if [ "${RUN:-0}" -lt 5 ]; then
    echo "probe: FAIL - the camera does not get 5 consecutive frames without a skinning pass"
    exit 1
fi
if [ "${MOVED:-0}" -eq 0 ]; then
    echo "probe: FAIL - a frame step did NOT re-run the pass (the skip is too eager)"
    exit 1
fi
echo "probe: ok - the camera skips the pass ($RUN consecutive frames), a frame step re-runs it"
exit 0
