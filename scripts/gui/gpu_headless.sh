#!/bin/bash
# GPU-BACKED HEADLESS DISPLAY. Runs a command on an X server whose GL is the machine's real
# GPU, with no window on the developer's desktop.
#
# WHY NOT Xvfb: Xvfb's GLX is llvmpipe by construction (it has no DRM node), so every render,
# every probe and every timing taken under it is software. wlroots' headless backend composites
# through EGL on the real device and hands the GPU to Xwayland, so a client connecting to that
# DISPLAY gets radeonsi/i915/... - measured here: "Accelerated: yes, AMD Radeon R9 380".
#
# LIMITS, measured: cage is a KIOSK - it presents ONE client fullscreen, so the command must BE
# that client (a second X client can connect and drive it, but a wrapper process cannot become the
# presented window). No window manager runs, so keyboard focus is the client's own affair.
#
#   scripts/gui/gpu_headless.sh -- /path/to/app arg...
#
# Environment it sets for the child: DISPLAY (Xwayland), WLR_* (headless + gles2). It refuses to
# start if the GPU is not available (no /dev/dri) and prints the renderer string it got.
set -u

if [ "${1:-}" = "--" ]; then shift; fi
if [ $# -eq 0 ]; then echo "usage: $0 -- <command> [args...]" >&2; exit 2; fi
if [ ! -d /dev/dri ]; then echo "gpu_headless: no /dev/dri - this machine has no GPU node" >&2; exit 3; fi
if ! command -v cage > /dev/null; then echo "gpu_headless: cage not installed (dnf install cage)" >&2; exit 3; fi

RUNTIME=$(mktemp -d /tmp/ae-gpu-XXXXXX)
cleanup() { rm -rf "$RUNTIME"; }
trap cleanup EXIT

export XDG_RUNTIME_DIR=$RUNTIME
unset WAYLAND_DISPLAY DISPLAY
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 WLR_RENDERER=gles2
export QT_QPA_PLATFORM=xcb QT_QPA_PLATFORMTHEME=generic
export QT_FORCE_STDERR_LOGGING=1

cage -- "$@" &
CAGE_PID=$!
# Xwayland appears as :1 (or the next free); wait for it, then report the renderer.
for _ in $(seq 1 120); do
    for d in 1 2 3; do
        [ -S "/tmp/.X11-unix/X$d" ] && { DISPLAY=:$d glxinfo -B 2>/dev/null | grep -qE "Accelerated: yes" && {
            echo "gpu_headless: DISPLAY=:$d $(DISPLAY=:$d glxinfo -B 2>/dev/null | grep -m1 'renderer string')" >&2
            wait $CAGE_PID; exit $?
        }; }
    done
    sleep 0.25
done
echo "gpu_headless: no accelerated Xwayland appeared" >&2
kill $CAGE_PID 2>/dev/null
exit 4
