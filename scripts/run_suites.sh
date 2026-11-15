#!/bin/bash
# Run every test target the CURRENT CMakeLists declares.
#
# Deliberately not `for t in build/tests/*/`: a stale binary left behind by a
# test whose source has since been deleted passes happily and reports a green
# suite that tests nothing. The declared list is the source of truth.
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CFG=${1:-Debug}
# Target names come from `add_executable(<name> ...)`; keep only those that are
# actual test programs (all of them, minus the interactive/arg-requiring ones).
declare -A SKIP=(
  [roundtrip]=1            # needs .anie arguments
  [easing_picker_shot]=1   # writes screenshots
  # pose_pull_crash drives the real ctrl::Driver with the Pose tool's "Pull
  # bones" mode against a project of the caller's choosing; with no .anie it
  # only prints usage and exits 2, which a suite would read as a failure. It is
  # the reproduction tool for the TreeUtil::deleteAll heap corruption, not a
  # gate -- tests/treeutil_delete_uaf.cpp is the gate for that fix.
  [pose_pull_crash]=1
  # picker_probe used to be skipped here. It constructs widgets (no display needed:
  # it runs under QT_QPA_PLATFORM=offscreen) and it now EXITS NON-ZERO on a
  # mismatch, so it is a gate: it reported a stale height expectation for months
  # while exiting 0.
  [picker_pos_probe]=1
  [media_link_probe]=1
)
# The pattern must allow leading whitespace: targets registered inside an `if()`
# block (the overlay's, behind AE_ENABLE_IMGUI) are indented, and the anchored form
# silently dropped three of them - a test that is never run is worse than one that
# fails, because the suite still reports success.
targets=$(grep -oP '^\s*add_executable\(\K[A-Za-z0-9_]+' tests/CMakeLists.txt | sort -u)

# Targets that only EXIST when the overlay's ImGui dependency is enabled
# (AE_ENABLE_IMGUI, default ON). The list above is read out of the CMakeLists text,
# which says nothing about the option, so a configure with it OFF would otherwise
# report these as MISSING and fail the suite for a build that is legitimately lean.
if ! grep -q '^AE_ENABLE_IMGUI:BOOL=ON' build/CMakeCache.txt 2>/dev/null; then
  SKIP[imgui_qt_font_loader]=1
  SKIP[imgui_input_bridge]=1
  SKIP[graph_panel_pixels]=1
fi

# Source level checks that are not C++ programs. They run first because they cost about
# a second, and they answer a question no test binary can: is every catalog context
# still one that code somewhere can ASK for? A class that calls tr() without Q_OBJECT
# looks perfectly translated in the .ts files and ships English in all six languages,
# with nothing failing anywhere. That is how the whole export dialog and the
# destination picker were unreachable, so the check has to be a gate.
script_fail=0; script_failed=""
for check in scripts/i18n/check_tr_contexts.py scripts/i18n/gen_export_targets_i18n.py; do
  [ -f "$check" ] || continue
  args=""; [ "$check" = "scripts/i18n/gen_export_targets_i18n.py" ] && args="--check"
  if out=$(python3 "$check" $args 2>&1); then echo "PASS $check"
  else script_fail=$((script_fail+1)); script_failed="$script_failed $(basename "$check")"
       echo "FAIL $check"; echo "$out" | tail -12; fi
done

pass=0; fail=0; skipped=""; failed=""
for n in $targets; do
  [ -n "${SKIP[$n]:-}" ] && continue
  bin=build/tests/$CFG/$n
  if [ ! -x "$bin" ]; then
    # A target registered inside an option's `if()` block exists only in the
    # configurations that option allows (frame_ruler_canvas needs Qt >= 6.12 and
    # AE_CANVAS_PAINTER). "Not built here" is a SKIP, not a failure - but a target the
    # build system KNOWS and did not produce is still a failure.
    if [ -f "build/build-$CFG.ninja" ] && ! grep -q "^build $n: phony" "build/build-$CFG.ninja"; then
      echo "SKIP $n (not configured in this build: an option excludes it)"
      skipped="$skipped $n(not-configured)"
      continue
    fi
    echo "MISSING $n"; fail=$((fail+1)); failed="$failed $n(missing)"; continue
  fi
  # export_encoder_fallback renders too (a real export writes pixels), and takes
  # the same optional project argument or its own fixture default. The same GL
  # requirement puts psd_layer_identity here: it drives core::ImageKey's image
  # tree, which needs a live context to build its textures. asset_remove_layers
  # builds core::LayerNode default ImageKeys the same way.
  # overlay_engine_primitives draws through the viewport's own paint engine
  # (ctrl::PainterHandle) and reads an offscreen FBO back: the GL context IS the
  # subject under test. filter_card_geometry builds a core::LayerNode for the inspector,
  # and a layer's mesh holder binds GL in its constructor, so it needs a context too -
  # offscreen it aborts before its first check. graph_image_step needs the context only to construct
  # core::ImageKey - the one channel whose step-hold is unobservable without one,
  # which is why graph_curve (headless, offscreen) reports it as a skip. Both
  # ignore the project argument they are handed. The two Canvas gates are here for the
  # plainest reason: a QCanvasPainterWidget is a QRhiWidget, so it needs a GL context to
  # produce a frame at all - an offscreen run reports "no Canvas Painter frame" and fails.
  if [ "$n" = media_exporter_e2e ] || [ "$n" = export_encoder_fallback ] ||
     [ "$n" = psd_layer_identity ] || [ "$n" = asset_remove_layers ] ||
     [ "$n" = overlay_engine_primitives ] || [ "$n" = graph_image_step ] ||
     [ "$n" = graph_panel_pixels ] || [ "$n" = frame_ruler_canvas ] ||
     [ "$n" = time_body_canvas ] || [ "$n" = wave_pass_pixels ] ||
     [ "$n" = layer_mask_pixels ] ||
     [ "$n" = filter_card_geometry ]; then
    # The end-to-end export harness needs a real GL context and a loaded project, so it
    # runs under Xvfb against the COMMITTED fixture (tests/fixtures/e2e_stage.png) rather
    # than being skipped for want of artwork on the machine. It used to be in the SKIP
    # list above, which meant 45 assertions - the only ones that drive ctrl::Exporter
    # through the real exporter wiring - ran when a developer remembered to run them.
    # Override the project with AE_E2E_PROJECT (e.g. a real .anie); the suite is a gate
    # either way. xcb and an UNSET WAYLAND_DISPLAY are both required: with Wayland set,
    # Qt prefers it and the GL widget never gets a context.
    if ! command -v xvfb-run >/dev/null 2>&1; then
      echo "SKIP $n (no xvfb-run; needs a GL context)"; skipped="$skipped $n(no-xvfb)"
      continue
    fi
    proj="${AE_E2E_PROJECT:-tests/fixtures/e2e_stage.png}"
    # One GL suite at a time, across concurrent invocations of THIS script.
    #
    # `xvfb-run -a` is not safe to run two copies of at once: it scans for a free
    # display number and then starts Xvfb, so two runs that look inside the same
    # window are both handed the same one (measured: both got :99, in about half of
    # six pairs). The loser then connects to the WINNER's X server and renders fine,
    # so it passes every check - and when the winner's teardown kills that server,
    # the loser's X connection dies during its exit and Xlib's fatal I/O handler
    # exits 1 in place of main()'s 0. The result is a suite that passed reporting a
    # failure, which is the same disease this file already fixed twice. The lock
    # costs nothing (the suite takes ~14s) and makes
    # `run_suites.sh Debug & run_suites.sh Release &` mean what it says.
    # flock is util-linux; without it the suite still runs, just not in parallel.
    lock=""
    command -v flock >/dev/null 2>&1 && lock="flock /tmp/ae_e2e_xvfb.lock"
    out=$(env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb timeout 600 \
              $lock xvfb-run -a "$bin" "$proj" 2>&1); rc=$?
  else
    # The ambient platform theme is dropped on this branch for a MEASURED reason:
    # with QT_QPA_PLATFORMTHEME=cosmic exported, libcutecosmictheme.so's watcher
    # thread calls back into Qt after teardown, and an offscreen test can exit
    # 134/139 having already printed its PASS (measured on 2026-10-12: 16 of 60
    # runs of one such test, while a pure-Qt control crashed 2 of 30). A test that
    # never maps a window has no use for a platform theme either way.
    out=$(env -u QT_QPA_PLATFORMTHEME QT_QPA_PLATFORM=offscreen timeout 180 "$bin" 2>&1); rc=$?
  fi
  if [ $rc -eq 0 ]; then pass=$((pass+1)); echo "PASS $n";
  else fail=$((fail+1)); failed="$failed $n"; echo "FAIL($rc) $n"; echo "$out" | tail -10; fi
done
echo "=== $CFG: pass=$pass fail=$((fail+script_fail)) skipped:$skipped failed:$failed$script_failed ==="
fail=$((fail+script_fail))
# THE EXIT CODE IS THE GATE. Without this line the script ended on an echo, so it
# always exited 0: a run with every suite failing looked identical to a clean one to
# any caller that checks the status (CI, a hook, or a person reading "rc=0"), and
# "all suites pass" rested on the reader noticing the word FAIL in the output. An
# unfailable check is how a suite that tests nothing stays green.
exit $((fail > 0 ? 1 : 0))
