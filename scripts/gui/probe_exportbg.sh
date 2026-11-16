#!/bin/bash
# See _lib.sh: these drive the real app under Xvfb because the wiring they check
# is not reachable from a unit test.
#
# THE EXPORT'S BACKGROUND, through the real app and back out of the written PNG.
# When the export's transparency resolves OFF the artifact must show the effective
# viewport colour (#171717, the theme's recessed canvas token, with no project
# override) at alpha 255 in every pixel the artwork does not paint; when it is ON
# those pixels stay (0,0,0,0) - the exporter's own clear (Exporter::clearBackground
# clears transparent black for an invalid background, and the viewport colour at
# alpha 1 for the opaque one).
#
# THE PROJECT IS THE ONE THAT MAKES THAT OBSERVABLE, and it is why this probe does
# not take just any project: tests/fixtures/e2e_stage.png is a 96x64 image with two
# shapes on a transparent ground, so the artwork leaves whole regions of the canvas
# UNPAINTED INSIDE the art. The background is therefore readable at a pixel of the
# art itself, with no pad margin to construct and no guess about what some other
# project happens to paint. Read against the file: pixel (0,0) is srgba(0,0,0,0),
# (32,32) is #e8503a (the circle) and (72,32) is #2f6fd0 (the rounded rectangle) -
# so (0,0) is the export background and nothing else, and the circle pixel is the
# control that proves the artwork really was rendered rather than a blank canvas
# that would satisfy both states for the wrong reason.
#
# WHICH ROUTE IT DRIVES, and why not the other one. The destination comes from the
# GENERAL rows at the TOP of the picker ("Video", "Animated image", "Image
# sequence", "Archive file (highest quality)") - no search. That is not a
# preference: the picker's search field is NOT a value-bearing control, so
# `ae set "Search destinations" ...` neither filters nor reports (measured
# 2026-10-11: `ae value` printed nothing, and the list stayed at 134 `list item`
# nodes after a query), and a probe built on it asserts about a screen it never
# changed. "Image sequence" is the general row whose artifact is a PNG, so the
# pixels can be read back exactly; the row is a real click, because a list row is
# reported by two nested nodes and `ae_activate ... "list item"` picks the one with
# no action.
#
# THE FIXTURE IS AN IMAGE, NOT A PROJECT FILE. The application's command line opens
# only .anie (Main.cpp: arguments.last().contains(".anie")), so the fixture cannot
# arrive as an argument: the project is created the way a user creates it - File >
# New project, the asset named in the platform OPEN dialog (the one step that
# cannot be addressed by name: that dialog is a different toolkit inside the same
# process, so its widgets are not in the application's accessibility tree), then
# Create. The canvas comes from the image itself (96x64, not the 512x512
# new-project default), which the assertions below read off the form - so a dialog
# that silently took no asset cannot pass by exporting a blank canvas.
#
# WHAT THE PROBE SETS ITSELF: where the transparency check box STARTS is not this
# probe's to choose - it comes from the saved `export_allow_transparency`
# preference, and the destination's own alpha rule is applied over it
# (applyDestinationDefaults ticks an `alpha="default-on"` row on, which the Image
# sequence row is - measured: the box reads Checked when the form opens even after
# a run that exported with it OFF). So xcheck reads the real state (the control's
# own Checked/Unchecked description, then the AT-SPI CHECKED bit) and toggles the
# box until it IS the state each case wants - the state is never inherited, and a
# box that refused to move fails one of the two cases. The toggle comes AFTER the
# frame range is set, so nothing re-applies a destination default behind it. The
# pointer is parked at the same place before every capture, so a hover highlight
# cannot enter the evidence.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# THE FIXTURE IS THIS PROBE'S PROJECT. _lib.sh insists on a project file (arg 1 or
# AE_PROJECT, else the smallest .anie it can find), and there is no .anie in the
# repository at all - so the default is stated here rather than left to a search
# that would exit 2 on a clean checkout.
FIXTURE="$SCRIPT_DIR/../../tests/fixtures/e2e_stage.png"
if [ -z "${1:-}" ] && [ -z "${AE_PROJECT:-}" ]; then
    set -- "$FIXTURE"
fi
source "$SCRIPT_DIR/_lib.sh" "$@"
# The revision of the export flow this probe's navigation was recorded against. See
# AE_FLOW_REVISION in _lib.sh: the guard refuses to run a probe that predates it.
export RECORDED_AGAINST=2

# THE CHECK BOX ACCESSIBLE NAME IS THE VIEW MODEL'S OWN LABEL, not a guess: the
# scene binds `text: exportModel.transparency.label` (ExportDialog.qml:580) and
# ExportViewModel::retranslateUi sets that to tr("Export with transparency")
# (ExportViewModel.cpp:1434). Plain "Transparency" is the FACTS row - a different
# node, with a colon - and addressing it would assert the wrong widget.
TRANSPARENCY="Export with transparency"

# The general row this probe drives: the PNG artifact, so the exported pixels can
# be read back.
DESTINATION_ROW="Image sequence"

# The theme's recessed canvas token, which MainWindow::effectiveViewportBackground()
# falls back to when the project carries no override. The fixture project has none.
# Read back as 8-bit channels, and compared with the same 2-step tolerance the
# e2e harness uses for the same colour through the same GL path (the clear is
# exact, but the framebuffer round trip is not something a probe should assume).
MARGIN_RGB="23 23 23"
RGB_TOLERANCE=2

# THE ASSET THAT IS IMPORTED, and it is absolute. The fixture is what this probe is
# for; a project named as arg 1 is taken instead only when it is an image the New
# project chooser accepts (its filter is psd/ora/jpg/jpeg/png/gif/tiff/tif/webp),
# because an .anie cannot be imported as an asset at all. Absolute because the
# platform dialog resolves a relative path against its own idea of a directory: the
# save dialog opens in the last visited directory, which is $HOME on a fresh
# sandbox (measured 2026-10-11: a bare name landed in /home/gambot, not in the
# working directory the app was launched from).
ASSET="$PROJ"
case "$ASSET" in
    *.psd|*.ora|*.jpg|*.jpeg|*.png|*.gif|*.tiff|*.tif|*.webp) ;;
    *) ASSET="$FIXTURE" ;;
esac
ASSET="$(cd "$(dirname "$ASSET")" && pwd)/$(basename "$ASSET")"

# THROWAWAY OUTPUT, never the checkout. The export is named by an ABSOLUTE path in
# the platform save dialog, so where the dialog happens to open cannot decide where
# the artifact lands. Distinct stems per case, so the second export can never be
# satisfied by the first file being left behind.
OUTDIR="$AE_PROBE_DIR"
OFF_NAME="probe_exportbg_off.png"
ON_NAME="probe_exportbg_on.png"
OFF_STEM="${OFF_NAME%.*}"
ON_STEM="${ON_NAME%.*}"
# A sequence is a directory of numbered stills, so the artifact is <stem>0001.png
# (the exporter numbers from 0001 at the stem the user typed).
OFF_PNG="$OUTDIR/${OFF_STEM}0001.png"
ON_PNG="$OUTDIR/${ON_STEM}0001.png"
# Exported for the same reason _lib.sh exports APP/ROOT/PROJ: the bodies below run
# in a child shell, and a name this probe owns has to reach it.
export OUTDIR OFF_NAME ON_NAME TRANSPARENCY DESTINATION_ROW ASSET
rm -f "$OFF_PNG" "$ON_PNG"
rm -f /tmp/exportbg_off.txt /tmp/exportbg_off_state.txt /tmp/exportbg_off_facts.txt \
      /tmp/exportbg_off_picker.txt \
      /tmp/exportbg_on.txt /tmp/exportbg_on_state.txt /tmp/exportbg_on_facts.txt \
      /tmp/exportbg_on_picker.txt

# READ AND SET the transparency check box, by its model label, for one case.
#
# The saved preference decides where the box STARTS, so reading the real state and
# toggling until it matches is the only way a run is deterministic. AT-SPI reports a
# Qt Quick CheckBox state in its StateSet (CHECKED) and toggles it through the Action
# interface - the same walk/find shape the rest of the tooling uses, pid-guarded so a
# second AnimeEffects on the bus cannot be driven instead. Prints "checked" or
# "unchecked"; exit 0 only when the wanted state was reached.
xcheck() { # xcheck <want:0|1> <name>
    python3 - "$@" <<PY
import os, sys, time, warnings
warnings.filterwarnings("ignore")
import gi
gi.require_version("Atspi", "2.0")
from gi.repository import Atspi as A

want = sys.argv[1] == "1"
name = sys.argv[2]
pid = os.environ.get("AE_UI_PID", "").strip()

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
            cand = desktop.get_child_at_index(i)
            if "AnimeEffects" not in (cand.get_name() or ""):
                continue
            if pid and str(cand.get_process_id()) != pid:
                continue
            if best is None or cand.get_child_count() > best.get_child_count():
                best = cand
        except Exception:
            continue
    return best

def box():
    app = root()
    if app is None:
        return None
    # PREFER THE ROLE: the control contentItem Text carries the same words, so a match
    # by name alone can land on a static text with no CHECKED state and no action.
    fallback = None
    for node in walk(app):
        try:
            if (node.get_name() or "") != name:
                continue
            if (node.get_role_name() or "") == "check box":
                return node
            if fallback is None:
                fallback = node
        except Exception:
            continue
    return fallback

def state_of(node):
    # AeCheckBox authors the description itself: Accessible.description is
    # "Checked" or "Unchecked", bound to the control own checked property - the same
    # value the model holds. Qt also maps the check state bit, so both are read and
    # either is accepted: a Qt that reports only one of them still gives an answer.
    try:
        desc = (node.get_description() or "").strip().lower()
    except Exception:
        desc = ""
    if desc in ("checked", "unchecked"):
        return desc
    try:
        return "checked" if node.get_state_set().contains(A.StateType.CHECKED) else "unchecked"
    except Exception:
        return None

want_state = "checked" if want else "unchecked"
node = box()
if node is None:
    print("absent")
    sys.exit(1)
if state_of(node) != want_state:
    iface = node.get_action_iface()
    names = []
    if iface is not None:
        try:
            names = [(iface.get_action_name(i) or "").lower()
                     for i in range(iface.get_n_actions())]
        except Exception:
            names = []
        idx = 0
        for word in ("toggle", "click", "press", "activate"):
            if word in names:
                idx = names.index(word)
                break
        try:
            iface.do_action(idx)
        except Exception:
            pass
    # The toggle is synchronous in QML, but the accessibility bus can lag a moment;
    # poll for the new state rather than assuming it after a fixed sleep.
    for _ in range(6):
        time.sleep(0.3)
        node = box()
        if state_of(node) == want_state:
            break
state = state_of(node) if node is not None else None
print(state if state else "absent")
sys.exit(0 if state == want_state else 1)
PY
}
export -f xcheck

# ------------------------------------------------------- case 1: transparency OFF
xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
  cd "$OUTDIR"
  "$APP" "$PROJ" >/tmp/probe_exportbg_off.log 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID   # the window the tooling must address
  wait_ready 120 || sleep 12
  W=$(xdotool search --name "AnimeEffects" | tail -1)
  xdotool windowactivate --sync $W 2>/dev/null

  # 1. THE PROJECT, built from the fixture. The command line takes only .anie, so
  #    the asset is taken the way a user takes it: File > New project, the platform
  #    open dialog (not addressable by name) typed into with the absolute path, then
  #    Create. The canvas the dialog then reports is the image own size - asserted
  #    below, so a dialog that took no asset cannot pass.
  ae_press "File" "the File menu opens"
  sleep 1
  ae_press "New project" "the New project entry is pressed"
  sleep 2.5
  # The ACTION path, not a coordinate click: a Qt Quick control answers an action
  # where a click would be shifted by the QQuickWidget origin.
  ae_activate "Browse…" "the asset chooser is opened"
  sleep 3
  xdotool key --clearmodifiers ctrl+a; sleep 0.4
  xdotool type --delay 25 "$ASSET"; sleep 0.6
  xdotool key Return
  sleep 2
  ae_activate "Create" "the project is created from the fixture asset"
  sleep 5

  # 2. Export asks WHERE first, and the answer is a GENERAL row: the manual block at
  #    the top of the list, which needs no search to be on screen. A single click
  #    selects the row (a double click would answer with it), so Continue accepts it.
  ae_press "File" "the File menu opens"
  sleep 1
  ae_press "Export project as" "the export entry is there"
  require_export_flow
  ae_report /tmp/exportbg_off_picker.txt "Video" "Animated image" "Image sequence" \
                                         "Archive file (highest quality)" "Continue"
  ae_press "Image sequence" "the manual PNG-sequence row is selected"
  sleep 1.2
  ae_activate "Continue" "the sequence destination is accepted"
  sleep 3

  # 3. ONE frame, so the artifact is a single PNG and the export is quick: the frame
  #    range lives under Advanced, and both ends are named so the count cannot depend
  #    on where the project range starts.
  ae_activate "Advanced" "the Advanced section opens, adding the frame range"
  sleep 1.5
  ae set "First frame 1" 1
  ae set "Last frame 1" 1
  sleep 1
  # The form own answers, written down so the assertions outside read what the form
  # really held rather than what this probe asked for.
  {
    printf "destination=%s\n" "$(ae value "Destination")"
    printf "width=%s\n" "$(ae value "Width")"
    printf "height=%s\n" "$(ae value "Height")"
    printf "first=%s\n" "$(ae value "First frame 1")"
    printf "last=%s\n" "$(ae value "Last frame 1")"
  } > /tmp/exportbg_off_facts.txt

  # 4. SET the box OFF; never inherit the saved preference. The state is written down
  #    so the assertions outside read what the box really held, not what was asked.
  #    LAST, after every other control: a refresh re-applies the destination defaults
  #    and would tick a default-on row back on.
  st=$(xcheck 0 "$TRANSPARENCY")
  printf "%s\n" "$st" > /tmp/exportbg_off_state.txt
  ae_report /tmp/exportbg_off.txt "Destination" "Export with transparency" "Export" "Cancel"
  xdotool mousemove 700 600; sleep 0.5
  import -window root "$SHOTS"/exportbg_01_off_form.png

  # 5. Export for real. Naming the file is the one step that cannot be driven by
  #    name: the platform dialog is a different toolkit inside the same process, so
  #    its widgets are not in the application tree. It opens with the name field
  #    focused and the suggestion selected, and the name typed here is ABSOLUTE, so
  #    the artifact lands in the throwaway directory this probe owns whatever
  #    directory the dialog itself happened to open in.
  ae_activate "Export" "the opaque export form is answered"
  sleep 4
  xdotool key --clearmodifiers ctrl+a; sleep 0.4
  xdotool type --delay 25 "$OUTDIR/$OFF_NAME"; sleep 0.6
  xdotool key Return
  # THE COMPLETION SIGNAL IS THE APP OWN EXPORT BOOKKEEPING: reportExportOutcome()
  # writes export_last_name into QSettings when it returns, and that survives on disk -
  # unlike the status message, which lives twenty seconds. The value is stored
  # UNQUOTED (measured: the developer conf holds "export_last_name=nene.mp4"), so a
  # pattern carrying quotes never matches and would idle out the whole timeout.
  cf="$XDG_CONFIG_HOME/AnimeEffectsProject/AnimeEffects.conf"
  for i in $(seq 1 150); do
    grep -q "^export_last_name=$OFF_NAME$" "$cf" 2>/dev/null && break
    sleep 2
  done
  xdotool mousemove 700 600; sleep 0.5
  import -window root "$SHOTS"/exportbg_02_off_done.png
  kill $app 2>/dev/null; wait $app 2>/dev/null
' 2>&1 | tail -4

# ---- the row this probe drives, and the form it led to ------------------------
assert_showing /tmp/exportbg_off_picker.txt "Image sequence" \
    "the manual PNG-sequence row is on screen where the probe clicks it"
assert_showing /tmp/exportbg_off.txt "Destination" "the form states the destination it was given"
assert_showing /tmp/exportbg_off.txt "Export with transparency" \
    "and offers the transparency row, which a PNG artifact can carry"
assert_showing /tmp/exportbg_off.txt "Export" "with an Export action to answer it"

# ---- the form held the project and the range this probe set -------------------
# THE FIXTURE'S OWN SIZE, not the 512x512 new-project default: this is the assertion
# that the asset really was imported, and it is what makes the pixel assertions below
# about the fixture's transparent artwork rather than about an empty canvas.
value_of() { sed -n "s/^$2=//p" "$1" 2>/dev/null | head -1; }
off_w=$(value_of /tmp/exportbg_off_facts.txt width)
off_h=$(value_of /tmp/exportbg_off_facts.txt height)
[ "$off_w" = "96" ] && [ "$off_h" = "64" ]
check $? "the project is the fixture at its own size ($off_w x $off_h, not the 512x512 default)"
[ "$(value_of /tmp/exportbg_off_facts.txt destination)" = "$DESTINATION_ROW" ]
check $? "the manual row reached the form as the destination ($(value_of /tmp/exportbg_off_facts.txt destination))"
off_first=$(value_of /tmp/exportbg_off_facts.txt first)
off_last=$(value_of /tmp/exportbg_off_facts.txt last)
[ "$off_first" = "1" ] && [ "$off_last" = "1" ]
check $? "the form holds the one-frame range this probe set by name ($off_first..$off_last)"

# ---- the box held the state the probe set, read back from the application -----
grep -qx "unchecked" /tmp/exportbg_off_state.txt
check $? "the opaque case left the box OFF"
# The app own bookkeeping, which is also the signal this case waited on: the export
# reached the end of its reporting path rather than merely leaving a file behind.
# Checked HERE, before the transparent case runs: export_last_name is one value, and
# the second export overwrites it.
grep -q "^export_last_name=$OFF_NAME$" "$XDG_CONFIG_HOME/AnimeEffectsProject/AnimeEffects.conf"
check $? "the opaque export finished its reporting path"

# ---- the artifact, read back ---------------------------------------------------
assert_exists "$OFF_PNG" "the opaque export wrote a PNG at the sequence numbering"
off_count=$(ls "$OUTDIR/$OFF_STEM"[0-9]*.png 2>/dev/null | wc -l)
[ "$off_count" = "1" ]
check $? "and wrote exactly one frame, as the form's range said ($off_count file(s))"

# One pixel, as its 8-bit channels: "r g b a". Read through ImageMagick so the value
# comes from the FILE, not from the app's own report of what it wrote.
channels_at() { # channels_at <png> <x> <y>
    magick "$1" -format \
        "%[fx:round(255*p{$2,$3}.r)] %[fx:round(255*p{$2,$3}.g)] %[fx:round(255*p{$2,$3}.b)] %[fx:round(255*p{$2,$3}.a)]" \
        info: 2>/dev/null
}
within() { # within <value> <target> <tolerance>
    case "$1" in ''|*[!0-9]*) return 1 ;; esac
    local delta=$(( $1 - $2 ))
    [ "${delta#-}" -le "$3" ]
}

# (0,0) is transparent in the fixture itself, so it is the export background and
# nothing else: #171717 at alpha 255 when transparency resolved OFF.
read -r off_r off_g off_b off_a <<<"$(channels_at "$OFF_PNG" 0 0)"
if [ "$off_a" = "255" ] \
   && within "$off_r" 23 "$RGB_TOLERANCE" \
   && within "$off_g" 23 "$RGB_TOLERANCE" \
   && within "$off_b" 23 "$RGB_TOLERANCE"; then
    check 0 "with transparency OFF the unpainted pixel IS the viewport background $MARGIN_RGB, opaque (got $off_r $off_g $off_b, alpha $off_a)"
else
    check 1 "with transparency OFF the unpainted pixel IS the viewport background $MARGIN_RGB, opaque (got $off_r $off_g $off_b, alpha $off_a)"
fi

# ...and the artwork really is in the frame: without this, a blank canvas would
# satisfy the assertion above for the wrong reason.
read -r off_r off_g off_b off_a <<<"$(channels_at "$OFF_PNG" 32 32)"
if [ "$off_a" = "255" ] \
   && within "$off_r" 232 "$RGB_TOLERANCE" \
   && within "$off_g" 80 "$RGB_TOLERANCE" \
   && within "$off_b" 58 "$RGB_TOLERANCE"; then
    check 0 "and the frame carries the fixture's artwork (#e8503a at the circle, got $off_r $off_g $off_b)"
else
    check 1 "and the frame carries the fixture's artwork (#e8503a at the circle, got $off_r $off_g $off_b)"
fi
assert_rendered "$SHOTS/exportbg_01_off_form.png" "the Image sequence form with transparency OFF"
assert_rendered "$SHOTS/exportbg_02_off_done.png" "the app after the opaque export"

# -------------------------------------------------------- case 2: transparency ON
xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
  cd "$OUTDIR"
  "$APP" "$PROJ" >/tmp/probe_exportbg_on.log 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID
  wait_ready 120 || sleep 12
  W=$(xdotool search --name "AnimeEffects" | tail -1)
  xdotool windowactivate --sync $W 2>/dev/null

  # The same project, taken the same way.
  ae_press "File" "the File menu opens"
  sleep 1
  ae_press "New project" "the New project entry is pressed"
  sleep 2.5
  ae_activate "Browse…" "the asset chooser is opened"
  sleep 3
  xdotool key --clearmodifiers ctrl+a; sleep 0.4
  xdotool type --delay 25 "$ASSET"; sleep 0.6
  xdotool key Return
  sleep 2
  ae_activate "Create" "the project is created from the fixture asset"
  sleep 5

  # The same manual row.
  ae_press "File" "the File menu opens"
  sleep 1
  ae_press "Export project as" "the export entry is there"
  require_export_flow
  ae_report /tmp/exportbg_on_picker.txt "Video" "Animated image" "Image sequence" \
                                        "Archive file (highest quality)" "Continue"
  ae_press "Image sequence" "the manual PNG-sequence row is selected"
  sleep 1.2
  ae_activate "Continue" "the sequence destination is accepted"
  sleep 3

  # The same one-frame range, then the box, for the same reason as above.
  ae_activate "Advanced" "the Advanced section opens, adding the frame range"
  sleep 1.5
  ae set "First frame 1" 1
  ae set "Last frame 1" 1
  sleep 1
  {
    printf "destination=%s\n" "$(ae value "Destination")"
    printf "width=%s\n" "$(ae value "Width")"
    printf "height=%s\n" "$(ae value "Height")"
    printf "first=%s\n" "$(ae value "First frame 1")"
    printf "last=%s\n" "$(ae value "Last frame 1")"
  } > /tmp/exportbg_on_facts.txt

  # SET the box ON. A stale ON from an earlier run cannot make this pass by accident:
  # the state is read back after the toggle, and the OFF case above asserts the
  # opposite state, so a box that refused to move fails one of the two.
  st=$(xcheck 1 "$TRANSPARENCY")
  printf "%s\n" "$st" > /tmp/exportbg_on_state.txt
  ae_report /tmp/exportbg_on.txt "Destination" "Export with transparency" "Export" "Cancel"
  xdotool mousemove 700 600; sleep 0.5
  import -window root "$SHOTS"/exportbg_03_on_form.png

  ae_activate "Export" "the transparent export form is answered"
  sleep 4
  xdotool key --clearmodifiers ctrl+a; sleep 0.4
  xdotool type --delay 25 "$OUTDIR/$ON_NAME"; sleep 0.6
  xdotool key Return
  cf="$XDG_CONFIG_HOME/AnimeEffectsProject/AnimeEffects.conf"
  for i in $(seq 1 150); do
    grep -q "^export_last_name=$ON_NAME$" "$cf" 2>/dev/null && break
    sleep 2
  done
  xdotool mousemove 700 600; sleep 0.5
  import -window root "$SHOTS"/exportbg_04_on_done.png
  kill $app 2>/dev/null; wait $app 2>/dev/null
' 2>&1 | tail -4

assert_showing /tmp/exportbg_on_picker.txt "Image sequence" \
    "the manual PNG-sequence row is on screen in the transparent case too"
assert_showing /tmp/exportbg_on.txt "Export with transparency" \
    "and the transparency row is still offered with alpha ON"
on_w=$(value_of /tmp/exportbg_on_facts.txt width)
on_h=$(value_of /tmp/exportbg_on_facts.txt height)
[ "$on_w" = "96" ] && [ "$on_h" = "64" ]
check $? "the second session imported the fixture at its own size too ($on_w x $on_h)"
grep -qx "checked" /tmp/exportbg_on_state.txt
check $? "the transparent case left the box ON"
grep -q "^export_last_name=$ON_NAME$" "$XDG_CONFIG_HOME/AnimeEffectsProject/AnimeEffects.conf"
check $? "the transparent export finished its reporting path"

assert_exists "$ON_PNG" "the transparent export wrote a PNG at the sequence numbering"
on_count=$(ls "$OUTDIR/$ON_STEM"[0-9]*.png 2>/dev/null | wc -l)
[ "$on_count" = "1" ]
check $? "and wrote exactly one frame as well ($on_count file(s))"

# THE DISTINCTION, asserted rather than assumed: the same pixel of the same artwork,
# exported twice, once with the background resolved ON and once OFF. ON, the clear
# is transparent black - (0,0,0,0) exactly, which is what Exporter::clearBackground
# clears to when the background is invalid.
read -r on_r on_g on_b on_a <<<"$(channels_at "$ON_PNG" 0 0)"
[ "$on_r" = "0" ] && [ "$on_g" = "0" ] && [ "$on_b" = "0" ] && [ "$on_a" = "0" ]
check $? "with transparency ON the unpainted pixel is (0,0,0,0) - the transparent clear (got $on_r $on_g $on_b, alpha $on_a)"
# ...and the artwork is still there in this export, so the pixel above is transparent
# because transparency was ON and not because the frame was empty.
read -r on_r on_g on_b on_a <<<"$(channels_at "$ON_PNG" 32 32)"
if [ "$on_a" = "255" ] \
   && within "$on_r" 232 "$RGB_TOLERANCE" \
   && within "$on_g" 80 "$RGB_TOLERANCE" \
   && within "$on_b" 58 "$RGB_TOLERANCE"; then
    check 0 "and the transparent export carries the same artwork (#e8503a at the circle, got $on_r $on_g $on_b)"
else
    check 1 "and the transparent export carries the same artwork (#e8503a at the circle, got $on_r $on_g $on_b)"
fi
assert_rendered "$SHOTS/exportbg_03_on_form.png" "the Image sequence form with transparency ON"
assert_rendered "$SHOTS/exportbg_04_on_done.png" "the app after the transparent export"

summary probe_exportbg
