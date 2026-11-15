#!/bin/bash
# See _lib.sh: these drive the real app under Xvfb because the wiring they check
# is not reachable from a unit test.
#
# ONE REAL EXPORT, END TO END, THROUGH THE DESTINATION FLOW: File > Export project as,
# a destination chosen by name in the picker, the parameter form that destination
# dictates, the Export button, the platform file dialog, and the file that lands on
# disk.
#
# What this replaces. The probe used to click recorded pixels (mousemove 16 10, then
# 81 162 for the export entry, then 462 458 for the Format row) and to assert that the
# screenshot was not black. Export opens the destination picker FIRST now, so those
# coordinates belong to a flow that no longer exists: 81 162 is not the export entry
# any more, the click landed somewhere else, the screenshot was still not black, and
# the probe passed while never opening an export dialog at all. Every step below asks
# the widget where it is (ae_ui.py), and asserts something the application has to be
# right about.
#
# The BODY only navigates and leaves evidence. Every assertion runs out here, where the
# helpers and the failure count live.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/_lib.sh" "$@"
# The revision of the export flow this probe's navigation was recorded against. See
# AE_FLOW_REVISION in _lib.sh: the guard refuses to run a probe that predates it.
export RECORDED_AGAINST=2

# THROWAWAY OUTPUT, never the checkout: the platform file dialog resolves a relative
# name against the application's working directory, so the app is launched from here
# and the name typed below lands here. The status message carries that absolute path,
# which is asserted below - the cheapest way to notice a probe writing into the repo.
OUTDIR="$AE_PROBE_DIR"
OUTNAME="probe_export.mp4"
OUT="$OUTDIR/$OUTNAME"
# The frame range this probe sets BY NAME. The project is 601 frames and a probe has
# minutes; what the file must match is the range the FORM ends up holding, which is read
# back inside the body and asserted below rather than assumed from these two numbers.
FIRST_FRAME=1
LAST_FRAME=24
rm -f "$OUT"
rm -f /tmp/exp_form.txt /tmp/exp_facts.txt /tmp/exp_range.txt /tmp/exp_status.txt

xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
  # NUMBER BOXES, BY NAME, THROUGH THE VALUE INTERFACE.
  #
  # The shared tool does this as well now: ae set and ae value reach for the Value
  # interface before the editable text (this Qt build answers EditableText with an EMPTY
  # string for a QSpinBox, so a write can report success while changing nothing), and a
  # name is matched EXACTLY before anything that merely contains it, so the box wins over
  # the label "- First frame -" that reads the same words. This helper stays for two
  # reasons of its own: one process sets a range and reads it BACK in a single walk, which
  # is what lets the assertions below be about the range the FORM holds rather than about
  # the numbers this probe typed at it, and its bar mode below is the cheap poll the
  # status message needs. Setting goes through the Value interface, which Qt wires to
  # QSpinBox::setValue.
  #
  #   spin set <name> <value>...   one "name=value" line per box that took one
  #   spin read <file> <name>...   one "name=value" line per box ("absent" if no node)
  #   spin bar  <text>             the status bar message containing <text>, if it is
  #                                on screen right now (exit 1 when it is not)
  spin() {
    python3 - "$@" <<PY
import os, sys, time, warnings
warnings.filterwarnings("ignore")
import gi
gi.require_version("Atspi", "2.0")
from gi.repository import Atspi as A

mode = sys.argv[1]
want = os.environ.get("AE_UI_PID", "").strip()

def walk(node, depth=0):
    if depth > 16:
        return
    try:
        count = node.get_child_count()
    except Exception:
        return
    for i in range(count):
        try:
            child = node.get_child_at_index(i)
        except Exception:
            continue
        yield child
        yield from walk(child, depth + 1)

def root():
    best = None
    desktop = A.get_desktop(0)
    for i in range(desktop.get_child_count()):
        try:
            app = desktop.get_child_at_index(i)
            if "AnimeEffects" not in (app.get_name() or ""):
                continue
            if want and str(app.get_process_id()) != want:
                continue
            if best is None or app.get_child_count() > best.get_child_count():
                best = app
        except Exception:
            continue
    return best

def number(node):
    iface = node.get_value_iface()
    if iface is None:
        return None
    try:
        return iface.get_current_value()
    except Exception:
        return None

def box(name):
    app = root()
    if app is None:
        return None
    for node in walk(app):
        if number(node) is None:
            continue
        try:
            shown = node.get_name() or ""
            described = node.get_description() or ""
        except Exception:
            continue
        if name == shown or name == described or name in shown:
            return node
    return None

def show(v):
    return str(int(v)) if v == int(v) else str(v)

if mode == "bar":
    # The status bar text, which is ONE node whose name is the whole message. This is a
    # single walk with no retry of its own, unlike ae label (which waits ten seconds
    # before admitting a widget is not there): a message that lives twenty seconds has
    # to be polled for cheaply, or the poll itself outlives it.
    needle = sys.argv[2]
    for _ in range(3):
        app = root()
        if app is not None:
            for node in walk(app):
                try:
                    name = node.get_name() or ""
                except Exception:
                    continue
                if needle in name:
                    print(name)
                    sys.exit(0)
        time.sleep(0.3)
    sys.exit(1)
elif mode == "read":
    with open(sys.argv[2], "w") as out:
        for name in sys.argv[3:]:
            node = box(name)
            out.write(name + "=" + ("absent" if node is None else show(number(node))) + "\n")
else:
    bad = 0
    pairs = sys.argv[2:]
    for i in range(0, len(pairs) - 1, 2):
        name, value = pairs[i], pairs[i + 1]
        node = box(name)
        if node is None:
            print("MISSING " + name)
            bad += 1
            continue
        try:
            node.get_value_iface().set_current_value(float(value))
            print("set " + name + "=" + show(number(node)))
        except Exception as exc:
            print("FAILED " + name + ": " + str(exc))
            bad += 1
    sys.exit(1 if bad else 0)
PY
  }

  # cd as its OWN command, not as `cd X && App &`. A background job whose body is a
  # compound command is a SUBSHELL, so $! names the subshell rather than the
  # application: AE_UI_PID then matches no accessible root, and every by-name helper
  # answers "no widget named ..." while a window is plainly on screen. That is what the
  # version this replaces did.
  cd "'"$OUTDIR"'"
  '"$APP"' "'"$PROJ"'" >/tmp/probe_export.log 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID   # the window the tooling must address
  wait_ready 120 || sleep 12
  W=$(xdotool search --name "AnimeEffects" | tail -1)
  xdotool windowactivate --sync $W 2>/dev/null

  # 1. Export asks WHERE first. The menu and the entry are reached by name: nothing here
  #    depends on where a menu happens to be drawn.
  ae_press "File" "the File menu opens"
  ae_press "Export project as" "the export entry is there"
  require_export_flow
  # The ID, not the label: the new first row label is "Video", and "video" is a word
  # several row haystacks contain, so a label query would answer with whichever the tree
  # lists first. The id "generic.video" is unique, so the destination this probe exports
  # with is the one it names. (This id replaced chat.generic.video, removed 2026-10-08.)
  ae set "Search destinations" "generic.video" >/dev/null
  sleep 1.2
  ae_activate "Continue" "the destination is accepted"
  # Wait for the plan to have been applied before touching the form: a report answers as
  # soon as ANY name it was asked about is on screen, so it can describe the form BEFORE
  # the destination has filled it in.
  ae wait "Advanced" >/dev/null 2>&1 || true
  # H41: the frame range lives under Advanced, so the section has to be OPEN before
  # its rows can be addressed by name.
  ae_activate "Advanced" "the Advanced section opens, adding its rows"
  sleep 1.5

  # 2. A short range, set BY NAME. A destination that caps the DURATION decides the range
  #    itself, which is why probe_capped sets none; this destination has no say in it, so
  #    the range is the input that proves the form this probe drove is the export that
  #    ran.
  if spin set "First frame 1" "'"$FIRST_FRAME"'" "Last frame 1" "'"$LAST_FRAME"'"; then
    check 0 "the frame range takes the values this probe sets by name"
  else
    check 1 "the frame range takes the values this probe sets by name"
  fi
  sleep 0.8
  ae_report /tmp/exp_form.txt "Destination" "Width" "Height" "Frame rate" "First frame 1" "Last frame 1" "Image quality"
  # What the form HOLDS, read back through the same interface: a box clamps to whatever
  # the range of the project allows, and the file has to match the form, not this probe.
  spin read /tmp/exp_range.txt "First frame 1" "Last frame 1" "Frame rate"
  # What the form says it is about to make. The destination DICTATES the format, so the
  # form states it as a fact instead of offering a Format control, and the facts are the
  # data the destination declares, arriving through the plan. Read with a retry: a single
  # query against a busy application can come back with an empty tree, and an empty file
  # here would be read as "the form said nothing".
  for i in 1 2 3 4 5; do
    ae text > /tmp/exp_facts.txt 2>/dev/null
    [ -s /tmp/exp_facts.txt ] && break
    sleep 1
  done
  import -window root '"$SHOTS"'/exp_01_form.png

  # 3. The form is answered, and the platform file dialog is TYPED INTO. That dialog is
  #    the one step that cannot be driven by name: it is a different toolkit in the same
  #    process, so its widgets are not in the accessibility tree of the application
  #    (ae_ui.py sees the main window tree). It opens with the name field focused and the
  #    suggested name selected, and a relative name resolves against the working
  #    directory of the application - which is why the app was launched from the output
  #    directory above.
  ae_activate "Export" "the export form is answered"
  sleep 4
  xdotool key --clearmodifiers ctrl+a; sleep 0.4
  xdotool type --delay 25 "'"$OUTNAME"'"; sleep 0.6
  xdotool key Return

  # 4. THE COMPLETION SIGNAL IS THE APP OWN EXPORT BOOKKEEPING, not the file on disk and
  #    not the status message. Not the file, because the muxer writes its header the
  #    moment the export opens: a 48-byte file that exists is not a finished export, and
  #    that is exactly the file this probe used to accept. Not the status message,
  #    because it lives for twenty seconds and a single accessibility query against a
  #    loaded application can take longer than that - measured, and the reason an earlier
  #    draft of this probe sat through a finished export and reported nothing.
  #
  #    When reportExportOutcome() returns it writes export_last_name into QSettings, and
  #    that is on disk for as long as the run lasts. The library sandboxes
  #    XDG_CONFIG_HOME, so this is the probe own settings file, never the developer own.
  cf="$XDG_CONFIG_HOME/AnimeEffectsProject/AnimeEffects.conf"
  for i in $(seq 1 120); do
    grep -q "^export_last_name='"$OUTNAME"'$" "$cf" 2>/dev/null && break
    sleep 2
  done
  echo "waited up to $((i * 2))s for the export to report itself finished"
  # THE SNAPSHOT COMES FIRST. The status message lives twenty seconds, and the picture is
  # what has to catch it; reading it costs time (a miss is a whole walk of the tree), so
  # the screenshot must not queue behind the read.
  import -window root '"$SHOTS"'/exp_02_done.png
  # ...and then the message itself, polled for cheaply while it is up.
  status=""
  for i in $(seq 1 40); do
    status=$(spin bar "Exported '"$OUTNAME"'" 2>/dev/null)
    [ -n "$status" ] && break
  done
  printf "%s\n" "$status" > /tmp/exp_status.txt
  kill $app 2>/dev/null; wait $app 2>/dev/null
' 2>&1 | tail -4

# The form the destination dictated: the destination itself, and the frames and rate it
# will be made of.
assert_showing /tmp/exp_form.txt "Destination" "the export form states the destination it was given"
assert_showing /tmp/exp_form.txt "Frame rate" "a video destination keeps the frame rate editable"
assert_showing /tmp/exp_form.txt "First frame 1" "and the frame range it will encode"
assert_showing /tmp/exp_form.txt "Last frame 1" "both ends of it"
# The other half of what a form asks: an image writer's quality control cannot affect a
# video, so it is hidden rather than greyed. (probe_still asks the same question the other
# way round, on the artifact that does go through QImage::save().)
assert_hidden /tmp/exp_form.txt "Image quality" "and does not offer an image quality control to a video"
# The chosen destination's own decisions, in the words on screen. generic.video is the
# no-ceiling row (mode="free" and no <caps>), so what its data must put on the form is
# its FORMAT - "H.264 · MP4", the codec and the container arriving through the plan -
# and the ABSENCE of a Limits line. The old "20 MB" assertion belonged to the removed
# chat.generic.video row's byte cap; asserting it against this row would be a lie about
# the data, so the second half is the negative that generic.video truly declares.
if grep -q "H.264 · MP4" /tmp/exp_facts.txt; then
    check 0 "the facts state what this destination decides (H.264 · MP4)"
else
    check 1 "the facts state what this destination decides (H.264 · MP4) (got: $(tr '\n' ' ' < /tmp/exp_facts.txt 2>/dev/null | head -c 200))"
fi
if grep -q "Limits:" /tmp/exp_facts.txt; then
    check 1 "and state no limit for the no-ceiling row (found: $(grep -m1 'Limits:' /tmp/exp_facts.txt 2>/dev/null))"
else
    check 0 "and state no limit for the no-ceiling row (mode=free, no caps)"
fi

# THE FILE. Non-empty is the floor; the range the FORM held is the assertion that the form
# this probe drove is the export that ran.
assert_exists "$OUT" "the export wrote the file the user named"
first=$(sed -n 's/^First frame 1=//p' /tmp/exp_range.txt 2>/dev/null | head -1)
last=$(sed -n 's/^Last frame 1=//p' /tmp/exp_range.txt 2>/dev/null | head -1)
fps=$(sed -n 's/^Frame rate=//p' /tmp/exp_range.txt 2>/dev/null | head -1)
[ -n "$first" ] && [ -n "$last" ] && [ "$first" != "absent" ] && [ "$last" != "absent" ] \
    && [ "$first" -ge 0 ] 2>/dev/null && [ "$last" -gt "$first" ] 2>/dev/null
check $? "the form holds a readable frame range ($first..$last)"
want=$(( ${last:-0} - ${first:-0} + 1 ))
# ...and that range is the SHORT one this probe set by name, not the whole project:
# without this, a probe whose range input never arrived would still pass on a 601-frame
# export that took ten minutes to produce.
[ "$want" -ge 2 ] && [ "$want" -le 60 ]
check $? "and it is the short range set by name rather than the 601 frames of the project ($want frames)"
if [ -s "$OUT" ]; then
    frames=$(ffprobe -v error -select_streams v -show_entries stream=nb_frames -of csv=p=0 "$OUT" 2>/dev/null | head -1)
    [ -n "$frames" ] && [ "$frames" -gt 0 ] 2>/dev/null
    check $? "the export contains video frames (nb_frames=${frames:-none})"
    [ "${frames:-0}" = "$want" ]
    check $? "the frame range the form held is the range that was encoded ($want frames, got ${frames:-none})"
    duration=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$OUT" 2>/dev/null | head -1)
    ok=$(awk -v d="${duration:-0}" -v n="$want" -v f="${fps:-24}" \
         'BEGIN { sec = (f > 0) ? n / f : 0; print (sec > 0 && d > sec * 0.7 && d < sec * 1.3) ? 0 : 1 }')
    check "$ok" "and the file lasts as long as those frames at the rate on the form (${duration:-none} s for $want frames at ${fps:-?} fps)"
    size=$(stat -c %s "$OUT")
    [ "$size" -gt 1000 ]
    check $? "and it is a real file rather than a muxer header ($size bytes)"
fi

# What the app said when it finished. The status bar names the file, its size, and the
# directory it wrote to: a probe that exported into the checkout, or into a directory of
# its own choosing, fails here as well as at the file assertion above.
if grep -q "Exported $OUTNAME" /tmp/exp_status.txt; then
    check 0 "the status bar reports the export by name ($(tr -d '\n' < /tmp/exp_status.txt | head -c 120))"
else
    check 1 "the status bar reports the export by name (got: $(tr -d '\n' < /tmp/exp_status.txt 2>/dev/null | head -c 120))"
fi
grep -qF "$OUTDIR" /tmp/exp_status.txt
check $? "and says it wrote into the throwaway directory, not the checkout"
# The app own bookkeeping, which is also the signal this probe waited on: the export
# reached the end of its reporting path rather than merely leaving bytes behind.
if grep -q "^export_last_name=$OUTNAME$" "$XDG_CONFIG_HOME/AnimeEffectsProject/AnimeEffects.conf" 2>/dev/null; then
    check 0 "and the app recorded the finished export in its own settings"
else
    check 1 "and the app recorded the finished export in its own settings (nothing in $XDG_CONFIG_HOME)"
fi
assert_rendered "$SHOTS/exp_01_form.png" "the export form carrying the chosen destination"
assert_rendered "$SHOTS/exp_02_done.png" "the app after the export"
summary probe_export
