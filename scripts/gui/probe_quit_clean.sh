#!/bin/bash
# QUITTING THE APPLICATION, in the two ways it is closed here.
#
# Measured before the fix this probe exists for: EVERY quit exited 139 and left one
# core behind, whichever way the window was closed. The stack was the timeline dock's
# teardown, not the caller:
#
#   #0 std::equal_to<void>::operator()
#   #4 util::Signaler<void()>::disconnect
#   #5 gui::TimeLineHeader::~TimeLineHeader()
#   #9 gui::TargetWidget::~TargetWidget()
#  #16 gui::MainWindow::~MainWindow()
#
# gui::TargetWidget creates the timeline subtree (and its editor's Selection signaller)
# at the top of its constructor and the header bar LAST, so QObject's child sweep - which
# deletes children in CREATION ORDER - freed the signaller before the header could
# disconnect from it. The header now holds the surface through a QPointer and the dock
# deletes the header from its destructor body, before the sweep (see the comments in
# src/gui/TargetWidget.cpp and src/gui/TimeLineHeader.{h,cpp}).
#
# What this probe measures, printing the numbers it measured:
#
#   a. the window closed THROUGH THE WINDOW MANAGER while X stays alive -> exit 0
#      (pre-fix: 139). openbox is the only route that works on this machine: `xdotool
#      windowclose` does nothing here and a bare Xvfb has no WM, so a WM close request
#      is the real user action, delivered as alt+F4 to the activated MAIN window.
#      The application also owns a 1x1 window and a selection-owner window, all named
#      AnimeEffects, so the search is anchored and takes the LAST match.
#   b. the same application, with the X server killed under it (kill -9 on the Xvfb
#      pid) -> exit 0 (pre-fix: 139).
#   c. the journal gained no "dumped core" line for the application in the run's
#      window (pre-fix: one per run, which is what 139 looks like from outside).
#
# The application is run in a fresh HOME as well as the sandboxed XDG_CONFIG_HOME that
# _lib.sh already provides: MainWindow offers crash-recovery copies at startup, and
# those are read from the app cache - QStandardPaths::CacheLocation, i.e. $HOME/.cache
# (see src/gui/Main.cpp and ctrl::System::recoveryCopyPaths). With a fresh HOME there
# are none, so no dialog can sit in front of the close being measured.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/_lib.sh" "$@"

rm -f /tmp/quit_wm_code.txt /tmp/quit_xkill_code.txt /tmp/quit_journal.txt

start=$(date +%s)

xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
  cd "$AE_PROBE_DIR"
  # Before anything synthetic: the xdotool below must reach THIS display and no other.
  ae_guard_display
  export HOME="$AE_PROBE_DIR/home"
  mkdir -p "$HOME"

  # A WINDOW MANAGER IS THE ONLY WAY A CLOSE REACHES THIS APPLICATION. openbox runs
  # INSIDE this display, so its close request is a real WM_DELETE_WINDOW and the
  # assertions below measure the application, not a signal.
  openbox --sm-disable >/tmp/probe_quit_openbox.log 2>&1 &
  wm=$!
  sleep 2

  # Bounded: an application that never exits must fail the probe rather than hang it.
  # A ZOMBIE COUNTS AS EXITED - the process is gone and bash holds its status for the
  # `wait` below, but /proc still lists it, so a loop on `kill -0` alone would wait out
  # the whole ceiling and then report TIMEOUT for a clean quit.
  is_gone() { # is_gone <pid>
      [ ! -e "/proc/$1" ] && return 0
      [ "$(cut -d" " -f3 < "/proc/$1/stat" 2>/dev/null)" = "Z" ] && return 0
      return 1
  }

  wait_exit() { # wait_exit <pid> <code-file>
      local i
      for i in $(seq 1 60); do
          is_gone "$1" && break
          sleep 1
      done
      if is_gone "$1"; then
          wait "$1" 2>/dev/null
          echo "$?" > "$2"
      else
          kill -9 "$1" 2>/dev/null
          wait "$1" 2>/dev/null
          echo "TIMEOUT" > "$2"
      fi
  }

  # ---- a. closed through the WM, X stays alive -------------------------------
  "$APP" "$PROJ" >/tmp/probe_quit_wm.log 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID
  if ! wait_ready 120; then
      echo "NOTREADY" > /tmp/quit_wm_code.txt
      kill -9 "$app" 2>/dev/null; wait "$app" 2>/dev/null
  else
      # The MAIN window: anchored, and the last of the application windows. The
      # windowactivate is what gives alt+F4 a target at all.
      W=$(xdotool search --name "^AnimeEffects$" | tail -1)
      if [ -z "$W" ]; then
          echo "NOWINDOW" > /tmp/quit_wm_code.txt
          kill -9 "$app" 2>/dev/null; wait "$app" 2>/dev/null
      else
          xdotool windowactivate --sync "$W" 2>/dev/null
          sleep 1
          xdotool key --clearmodifiers alt+F4
          sleep 3
          # A modified project asks before the WM close goes through. Answering it is
          # part of quitting; leaving it up would make the probe time out on its own
          # unread prompt instead of measuring the teardown. A process that is already
          # gone is not queried: there is no tree left to ask.
          if ! is_gone "$app" && ae wait "Discard Changes" >/dev/null 2>&1; then
              ae click "Discard Changes" >/dev/null 2>&1
          fi
          wait_exit "$app" /tmp/quit_wm_code.txt
      fi
  fi

  # ---- b. the X server dies under the running application --------------------
  "$APP" "$PROJ" >/tmp/probe_quit_xkill.log 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID
  if ! wait_ready 120; then
      echo "NOTREADY" > /tmp/quit_xkill_code.txt
      kill -9 "$app" 2>/dev/null; wait "$app" 2>/dev/null
  else
      # The Xvfb SERVING THIS DISPLAY, found by its display number: other probes and
      # other sessions leave Xvfb servers running (:99, :100 were up when this was
      # written), and killing one of those would test nothing.
      n=${DISPLAY#*:}; n=${n%%.*}
      xvfb=""
      for pid in $(pgrep -x Xvfb); do
          if tr "\0" " " < "/proc/$pid/cmdline" 2>/dev/null | grep -qE "(^| ):${n}( |$)"; then
              xvfb=$pid
          fi
      done
      if [ -z "$xvfb" ]; then
          echo "NOXVFB" > /tmp/quit_xkill_code.txt
          kill -9 "$app" 2>/dev/null; wait "$app" 2>/dev/null
      else
          echo "$xvfb" > /tmp/quit_xvfb_pid.txt
          kill -9 "$xvfb" 2>/dev/null
          wait_exit "$app" /tmp/quit_xkill_code.txt
      fi
  fi

  kill "$wm" 2>/dev/null
' 2>&1 | tail -8

end=$(date +%s)

wm_code=$(cat /tmp/quit_wm_code.txt 2>/dev/null || echo "missing")
[ "$wm_code" = "0" ]
check $? "closing the window through the WM with X alive exits 0 (measured $wm_code; the pre-fix number was 139, one core per run)"

xkill_code=$(cat /tmp/quit_xkill_code.txt 2>/dev/null || echo "missing")
# EXIT 1 IS THE CORRECT ANSWER HERE, and asserting 0 would be asserting the wrong
# thing: when the X connection dies the xcb platform plugin ends the process with
# exit(1) after "The X11 connection broke". What must NOT happen is a CRASH (139/SIGSEGV
# and a core), which is what the pre-fix build did. So the code must be 0 or 1, and
# the journal check below is the half that catches a real crash.
case "$xkill_code" in
    0|1) xkill_ok=0 ;;
    *) xkill_ok=1 ;;
esac
[ "$xkill_ok" = 0 ]
check $? "killing the X server under the running application does not crash (measured exit $xkill_code, 0 or 1 is a clean finish; pre-fix 139)"

# The coredump a quit used to leave is a journal entry, logged a moment after the
# process dies - so the window is widened by a second at each end and the query waits
# for the record to land. Narrowed to the APPLICATION: another instance coredumping
# here would be a different defect, not this one.
sleep 3
journalctl --since "@$((start - 2))" --until "@$((end + 5))" --no-pager \
    > /tmp/quit_journal.txt 2>/tmp/quit_journal.err
jrc=$?
[ "$jrc" -eq 0 ]
check $? "the journal is readable for the run window ($(date -d "@$start" "+%H:%M:%S") to $(date -d "@$((end + 5))" "+%H:%M:%S"))"

jlines=$(wc -l < /tmp/quit_journal.txt 2>/dev/null || echo 0)
[ "${jlines:-0}" -gt 0 ]
check $? "the run window in the journal is not empty ($jlines lines), so a clean core count means something"

cores=$(grep -cE "\(AnimeEffects\).*dumped core" /tmp/quit_journal.txt)
[ "${cores:-0}" -eq 0 ]
check $? "no new dumped-core line for the application in the run window (measured ${cores:-0}; pre-fix one per run)"
if [ "${cores:-0}" -gt 0 ]; then
    grep -E "\(AnimeEffects\).*dumped core" /tmp/quit_journal.txt | sed "s/^/      /"
fi

summary probe_quit_clean
