#!/bin/bash
# See _lib.sh: these drive the real app under Xvfb because the wiring they check
# is not reachable from a unit test.
#
# THE TWO IMAGE ARTIFACTS, end to end through the real app:
#
#   still     ONE image file at the name the user typed. Posting an illustration is
#             the common case for this audience, and it is the opposite of what every
#             other destination does: the save dialog asks for a file and the file it
#             is given is what must appear, not `name0001.png` beside it. What the
#             form must NOT ask for is a frame rate or a frame range: a still has no
#             timeline, and two questions that cannot change the answer are the same
#             disease as hiding a control that can.
#   sequence  a directory of numbered stills. pixiv's ugoira format is exactly this,
#             and it is deliberately NOT a video and NOT a GIF: the frame range IS an
#             input (the format caps it at 150 frames), the frame RATE is not, and the
#             numbering is the whole promise.
#
# Both are produced by Qt's image writers inside ctrl::Exporter, so a probe that only
# checked the plan would miss the file naming, which is the whole promise.
#
# What this replaces. The still case used to click recorded pixels (16,10 for the
# export entry, 81,162 for the menu item) and to assert that the screenshot was not
# black; the sequence case clicked (335,341) and (335,376) for the size boxes. Export
# opens the destination picker FIRST now, so the menu coordinates belong to a flow
# that no longer exists, and every control below is addressed by its accessible name
# instead. The destinations are chosen by ID: their labels share words ("pixiv") and a
# search that matches several rows selects whichever the tree lists first.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/_lib.sh" "$@"
# The revision of the export flow this probe's navigation was recorded against. See
# AE_FLOW_REVISION in _lib.sh: the guard refuses to run a probe that predates it.
export RECORDED_AGAINST=2

# THROWAWAY OUTPUT, never the checkout: the file dialog resolves a relative name
# against the application's working directory, so the app is launched from here and
# the names typed below land here.
OUTDIR="$AE_PROBE_DIR"
STILL_NAME="probe_still.jpg"
STILL="$OUTDIR/$STILL_NAME"
SEQ_NAME="probe_frames.png"
SEQBASE="${SEQ_NAME%.*}"
SEQ_FRAMES=20
rm -f "$STILL" "$OUTDIR/$SEQBASE"*.png
rm -f /tmp/still_form.txt /tmp/still_facts.txt /tmp/still_status.txt
rm -f /tmp/seq_form.txt /tmp/seq_facts.txt /tmp/seq_status.txt

# ------------------------------------------------------------------ a still image
xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
  # THE STATUS BAR MESSAGE, by name, read cheaply and repeatedly.
  #
  # The message is ONE node (the status bar) whose accessible name is the whole text.
  # ae label would find it too, but it waits ten seconds before admitting a widget is not
  # there, and this message lives for twenty: a poll built on it can outlive the thing it
  # is polling for. One walk of the tree with no retry of its own costs a fraction of a
  # second, so a poll catches the message while it is up.
  status_text() { # status_text <text> -> the message containing it, exit 1 if none
    python3 - "$@" <<PY
import os, sys, time, warnings
warnings.filterwarnings("ignore")
import gi
gi.require_version("Atspi", "2.0")
from gi.repository import Atspi as A

needle = sys.argv[1]
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

app = None
desktop = A.get_desktop(0)
for i in range(desktop.get_child_count()):
    try:
        candidate = desktop.get_child_at_index(i)
        if "AnimeEffects" not in (candidate.get_name() or ""):
            continue
        if want and str(candidate.get_process_id()) != want:
            continue
        if app is None or candidate.get_child_count() > app.get_child_count():
            app = candidate
    except Exception:
        continue

if app is not None:
    for node in walk(app):
        try:
            name = node.get_name() or ""
        except Exception:
            continue
        if needle in name:
            print(name)
            sys.exit(0)
sys.exit(1)
PY
  }

  # cd as its OWN command, never `cd X && App &`: that backgrounds a SUBSHELL, so $!
  # is not the application and AE_UI_PID addresses an accessible root that has no
  # tree - every by-name query then answers "no widget named ..." while a window is
  # plainly on screen. The previous version of this probe did exactly that.
  cd "'"$OUTDIR"'"
  '"$APP"' "'"$PROJ"'" >/tmp/probe_still.log 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID   # the window the tooling must address
  wait_ready 120 || sleep 12
  W=$(xdotool search --name "AnimeEffects" | tail -1)
  xdotool windowactivate --sync $W 2>/dev/null

  # 1. The destination, by id, in the picker the flow opens first. Instagram feeds
  #    are the general case for a still: JPEG only, which is the artifact this checks.
  ae_press "File" "the File menu opens"
  ae_press "Export project as" "the export entry is there"
  require_export_flow
  ae set "Search destinations" "still.instagram.feed" >/dev/null
  sleep 1.2
  ae_activate "Continue" "the still destination is accepted"
  ae wait "Advanced" >/dev/null 2>&1 || true
  sleep 1.5

  # 2. What the form asks a still for: a destination, the image quality the Qt image
  #    writer has, and a size. NOT a frame rate and NOT a frame range - and the
  #    Format row is a fact the destination states, not a control.
  # No ae_activate "Advanced" here, unlike the sequence flow below: a Still
  # destination has no frame range, and since H41 (2026-10-09) moved the range
  # rows into that section, a Still has nothing left inside it - so the section is
  # not offered at all, which is the item-41 "uncluttered default surface" this
  # the assert_hidden below is about.
  ae_report /tmp/still_form.txt "Destination" "Image quality" "Width" "Height" \
                                "Frame rate" "First frame 1" "Last frame 1" "Format" "Quality"
  # Read with a retry: one query against a busy application can come back with an empty
  # tree, and an empty file here would be read as "the form said nothing".
  for i in 1 2 3 4 5; do
    ae text > /tmp/still_facts.txt 2>/dev/null
    [ -s /tmp/still_facts.txt ] && break
    sleep 1
  done
  import -window root '"$SHOTS"'/still_01_form.png

  # 3. Export, and name the image in the platform file dialog. It is the one step that
  #    cannot be driven by name: it is a different toolkit in the same process, so its
  #    widgets are not in the application accessibility tree. It opens with the name
  #    field focused and the suggested name selected, and a relative name resolves
  #    against the working directory of the application.
  ae_activate "Export" "the export form is answered"
  sleep 4
  xdotool key --clearmodifiers ctrl+a; sleep 0.4
  xdotool type --delay 25 "'"$STILL_NAME"'"; sleep 0.6
  xdotool key Return
  # THE COMPLETION SIGNAL IS THE APP OWN EXPORT BOOKKEEPING: reportExportOutcome()
  # writes export_last_name into QSettings when it returns, and that survives on disk -
  # unlike the status message, which lives for twenty seconds and can be missed by a
  # single accessibility query against a loaded application. The library sandboxes
  # XDG_CONFIG_HOME, so this is the probe own settings file.
  cf="$XDG_CONFIG_HOME/AnimeEffectsProject/AnimeEffects.conf"
  for i in $(seq 1 120); do
    grep -q "^export_last_name='"$STILL_NAME"'$" "$cf" 2>/dev/null && break
    sleep 2
  done
  # THE SNAPSHOT COMES FIRST: the message lives twenty seconds and the picture is what
  # has to catch it, so the screenshot must not queue behind the read below.
  import -window root '"$SHOTS"'/still_02_done.png
  # ...and then the message itself, which names the file, its size and the directory it
  # landed in, polled cheaply while it is up.
  status=""
  for i in $(seq 1 40); do
    status=$(status_text "Exported '"$STILL_NAME"'" 2>/dev/null)
    [ -n "$status" ] && break
    sleep 0.4
  done
  printf "%s\n" "$status" > /tmp/still_status.txt
  kill $app 2>/dev/null; wait $app 2>/dev/null
' 2>&1 | tail -4

# The form a still gets: the destination it was given, the one quality control Qt's
# image writer has, and the size.
assert_showing /tmp/still_form.txt "Destination" "the still form states the destination it was given"
assert_showing /tmp/still_form.txt "Image quality" "a still offers the image writer's own quality control"
assert_showing /tmp/still_form.txt "Width" "and the size the image will be written at"
assert_showing /tmp/still_form.txt "Height" "both dimensions of it"
# ...and what it must NOT ask for. These are hidden rather than greyed: a control that
# cannot affect the file is not information.
assert_hidden /tmp/still_form.txt "Frame rate" "one image has no frame rate to ask for"
assert_hidden /tmp/still_form.txt "First frame 1" "and no frame range"
assert_hidden /tmp/still_form.txt "Last frame 1" "neither end of one"
assert_hidden /tmp/still_form.txt "Format" "and the destination dictates the format, so there is no Format control"
# The destination's own decision, in the words on screen: one JPG, not a sequence.
if grep -q "one JPG image" /tmp/still_facts.txt; then
    check 0 "the facts state that this destination writes one image (Artifact: one JPG image)"
else
    check 1 "the facts state that this destination writes one image (got: $(tr '\n' ' ' < /tmp/still_facts.txt 2>/dev/null | head -c 200))"
fi

# THE FILE, at the name the user typed, and ONE of it.
assert_exists "$STILL" "the still destination wrote the name it was given"
siblings=$(ls "$OUTDIR/$SEQBASE"[0-9]*.png "$OUTDIR/probe_still"[0-9]* 2>/dev/null | wc -l)
[ "$siblings" = "0" ]
check $? "no numbered sequence was written beside it ($siblings numbered files)"
if [ -s "$STILL" ]; then
    fmt=$(magick identify -format "%m %wx%h" "$STILL" 2>/dev/null)
    echo "      image check: $fmt"
    # magick reports the format as "JPEG", so the pattern has to be the one it
    # prints rather than the extension on disk.
    case "$fmt" in JPEG*) check 0 "and it is a JPEG, which is what Instagram accepts ($fmt)";;
                  *)      check 1 "and it is a JPEG, which is what Instagram accepts ($fmt)";; esac
fi
# What the app said when it finished: the status bar names the image, its size, and the
# directory it went to. A probe that exported somewhere other than its throwaway
# directory fails here as well as at the file assertion above.
if grep -q "Exported $STILL_NAME" /tmp/still_status.txt; then
    check 0 "the status bar reports the exported image by name ($(tr -d '\n' < /tmp/still_status.txt | head -c 120))"
else
    check 1 "the status bar reports the exported image by name (got: $(tr -d '\n' < /tmp/still_status.txt 2>/dev/null | head -c 120))"
fi
grep -qF "$OUTDIR" /tmp/still_status.txt
check $? "and says it wrote into the throwaway directory, not the checkout"
# The app own bookkeeping, which is also the signal this case waited on. It is checked
# HERE and not at the end of the file: the sequence case that follows overwrites it.
if grep -q "^export_last_name=$STILL_NAME$" "$XDG_CONFIG_HOME/AnimeEffectsProject/AnimeEffects.conf" 2>/dev/null; then
    check 0 "and the app recorded the finished image export in its own settings"
else
    check 1 "and the app recorded the finished image export in its own settings (nothing in $XDG_CONFIG_HOME)"
fi
assert_rendered "$SHOTS/still_01_form.png" "the export form for a still image"
assert_rendered "$SHOTS/still_02_done.png" "the app after the still export"

# ----------------------------------------------------------- a frame sequence
# The other image artifact, and the opposite set of questions: a frame range IS an
# input (ugoira caps it at 150 stills), a frame rate is not - the timing of a folder
# of numbered stills is authored on the upload page, and the destination says so
# (fps="ignored"), so the form must not ask for one.
xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
  # NUMBER BOXES, BY NAME, THROUGH THE VALUE INTERFACE - the same helper probe_export.sh
  # carries. The shared tool does this as well now: ae set and ae value reach for the
  # Value interface before the editable text (a QSpinBox answers EditableText with an
  # empty string, so a write can report success while changing nothing), and a name is
  # matched exactly before anything that merely contains it, so the label
  # "- First frame -" cannot win over the box. This helper stays because one process sets
  # several boxes and reads them back in a single walk - which is what the assertions
  # below are checked against - and because its bar mode is the cheap poll the status
  # message needs. Setting goes through the Value interface, which Qt wires to
  # QSpinBox::setValue.
  #
  #   spin set <name> <value>...   one "name=value" line per box that took one
  #   spin read <file> <name>...   one "name=value" line per box ("absent" if no node)
  #   spin bar  <text>             the status bar message containing <text>, if it is
  #                                on screen right now (exit 1 when it is not)
  spin() {
    python3 - "$@" <<PY
import os, sys, warnings
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
    # before admitting a widget is not there): a message that lives twenty seconds has to
    # be polled for cheaply, or the poll itself outlives it.
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

  cd "'"$OUTDIR"'"
  '"$APP"' "'"$PROJ"'" >/tmp/probe_sequence.log 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID
  wait_ready 120 || sleep 12
  W=$(xdotool search --name "AnimeEffects" | tail -1)
  xdotool windowactivate --sync $W 2>/dev/null
  ae_press "File" "the File menu opens"
  ae_press "Export project as" "the export entry is there"
  require_export_flow
  ae set "Search destinations" "post.pixiv.ugoira" >/dev/null
  sleep 1.2
  ae_activate "Continue" "the sequence destination is accepted"
  ae wait "Advanced" >/dev/null 2>&1 || true
  # H41: the frame range lives under Advanced, so the section has to be OPEN before
  # its rows can be addressed by name.
  ae_activate "Advanced" "the Advanced section opens, adding its rows"
  sleep 1.5
  # Shrink the frame so the stills render quickly: a sequence is a `free` size
  # destination, so its Width and Height belong to the user. The aspect lock comes off
  # first, or the two boxes drive each other and the width this probe sets is overwritten
  # by the height it sets next.
  ae_activate "Keep aspect ratio" "the size boxes are unlocked from each other"
  sleep 0.8
  # ...and a SHORT range, by name, with BOTH ends given so the count of files does not
  # depend on where the range of the project starts. The format caps the range at 150
  # frames and this probe is not here to render a hundred and fifty: what is asserted is
  # that the range the form carries is the range that reaches the disk, which 20 stills
  # show as well as 150.
  if spin set "Width" "160" "Height" "256" "First frame 1" "1" "Last frame 1" "'"$SEQ_FRAMES"'"; then
    check 0 "the sequence inputs take the values this probe sets by name"
  else
    check 1 "the sequence inputs take the values this probe sets by name"
  fi
  sleep 0.8
  ae_report /tmp/seq_form.txt "Destination" "Image quality" "Width" "Height" "First frame 1" \
                               "Last frame 1" "Frame rate" "Format" "Quality"
  # What the form HOLDS, read back through the same interface, so the file count below is
  # checked against the form rather than against this probe.
  spin read /tmp/seq_range.txt "First frame 1" "Last frame 1" "Width" "Height"
  # Read with a retry: one query against a busy application can come back with an empty
  # tree, and an empty file here would be read as "the form said nothing".
  for i in 1 2 3 4 5; do
    ae text > /tmp/seq_facts.txt 2>/dev/null
    [ -s /tmp/seq_facts.txt ] && break
    sleep 1
  done
  import -window root '"$SHOTS"'/seq_01_form.png
  ae_activate "Export" "the sequence form is answered"
  sleep 4
  xdotool key --clearmodifiers ctrl+a; sleep 0.4
  xdotool type --delay 25 "'"$SEQ_NAME"'"; sleep 0.6
  xdotool key Return
  # THE COMPLETION SIGNAL IS THE APP OWN EXPORT BOOKKEEPING, the same one the still case
  # above waits on: a numbered file appearing is not the sequence being finished, and the
  # status message is too short-lived to poll for under load.
  cf="$XDG_CONFIG_HOME/AnimeEffectsProject/AnimeEffects.conf"
  for i in $(seq 1 150); do
    grep -q "^export_last_name='"$SEQ_NAME"'$" "$cf" 2>/dev/null && break
    sleep 2
  done
  # THE SNAPSHOT COMES FIRST, before a read that may miss the twenty seconds the message
  # lives for.
  import -window root '"$SHOTS"'/seq_02_done.png
  # ...and then the message itself, polled cheaply while it is up.
  status=""
  for i in $(seq 1 40); do
    status=$(spin bar "Exported '"$SEQ_NAME"'" 2>/dev/null)
    [ -n "$status" ] && break
    sleep 0.4
  done
  printf "%s\n" "$status" > /tmp/seq_status.txt
  kill $app 2>/dev/null; wait $app 2>/dev/null
' 2>&1 | tail -4

assert_showing /tmp/seq_form.txt "Destination" "the sequence form states the destination it was given"
assert_showing /tmp/seq_form.txt "First frame 1" "a frame sequence keeps the frame range editable"
assert_showing /tmp/seq_form.txt "Last frame 1" "both ends of it"
assert_hidden /tmp/seq_form.txt "Frame rate" "and asks for no frame rate: a numbered sequence has no timing of its own"
assert_showing /tmp/seq_form.txt "Image quality" "a sequence offers the image writer's own quality control"
if grep -q "numbered PNG files" /tmp/seq_facts.txt; then
    check 0 "the facts state the artifact as numbered files (Artifact: N numbered PNG files)"
else
    check 1 "the facts state the artifact as numbered files (got: $(tr '\n' ' ' < /tmp/seq_facts.txt 2>/dev/null | head -c 200))"
fi

# One file per frame, numbered from 0001, at the name the user typed. The COUNT is
# checked against the range the FORM held, read back above: a box clamps to whatever the
# range of the project allows, and the disk has to match the form, not this probe.
seqfirst=$(sed -n 's/^First frame 1=//p' /tmp/seq_range.txt 2>/dev/null | head -1)
seqlast=$(sed -n 's/^Last frame 1=//p' /tmp/seq_range.txt 2>/dev/null | head -1)
seqwant=$(( ${seqlast:-0} - ${seqfirst:-0} + 1 ))
[ -n "$seqfirst" ] && [ -n "$seqlast" ] && [ "$seqfirst" != "absent" ] && [ "$seqlast" != "absent" ] \
    && [ "$seqwant" -ge 2 ] && [ "$seqwant" -le 60 ] 2>/dev/null
check $? "the sequence form holds the short range this probe set by name ($seqfirst..$seqlast)"
count=$(ls "$OUTDIR/$SEQBASE"[0-9]*.png 2>/dev/null | wc -l)
[ "$count" = "$seqwant" ]
check $? "the sequence wrote one file per frame of the range it was given ($count of $seqwant)"
assert_exists "$OUTDIR/${SEQBASE}0001.png" "the first frame is numbered from 0001"
assert_exists "$OUTDIR/${SEQBASE}$(printf %04d "$seqwant").png" "the last frame of the range is there"
total=$(du -cb "$OUTDIR/$SEQBASE"[0-9]*.png 2>/dev/null | tail -1 | cut -f1)
[ "${total:-0}" -le 31457280 ] && [ "${total:-0}" -gt 0 ]
check $? "the whole set is inside ugoira's 30 MB cap (${total:-0} bytes)"
# The app's own verdict on the sequence export.
if grep -q "Exported $SEQ_NAME" /tmp/seq_status.txt; then
    check 0 "the status bar reports the sequence export by name ($(tr -d '\n' < /tmp/seq_status.txt | head -c 120))"
else
    check 1 "the status bar reports the sequence export by name (got: $(tr -d '\n' < /tmp/seq_status.txt 2>/dev/null | head -c 120))"
fi
grep -qF "$OUTDIR" /tmp/seq_status.txt
check $? "and says it wrote the frames into the throwaway directory, not the checkout"
# The app own bookkeeping, which is also the signal both cases waited on: the export
# reached the end of its reporting path rather than merely leaving files behind.
if grep -q "^export_last_name=$SEQ_NAME$" "$XDG_CONFIG_HOME/AnimeEffectsProject/AnimeEffects.conf" 2>/dev/null; then
    check 0 "and the app recorded the finished sequence export in its own settings"
else
    check 1 "and the app recorded the finished sequence export in its own settings (nothing in $XDG_CONFIG_HOME)"
fi
assert_rendered "$SHOTS/seq_01_form.png" "the export form for a frame sequence"
assert_rendered "$SHOTS/seq_02_done.png" "the app after the sequence export"

summary probe_still
