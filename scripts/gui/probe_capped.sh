#!/bin/bash
# See _lib.sh: these drive the real app under Xvfb because the wiring they check
# is not reachable from a unit test.
#
# A BYTE CAP IS A HARD UPLOAD REJECTION, NOT A PREFERENCE, and the only way to check
# that the file the app produced respects one is to weigh the file. So this probe runs
# real capped exports, end to end, and puts the results on a scale.
#
# TWO CAPPED DESTINATIONS, and the second one is not a spare. A cap is enforced by the
# fit-to-cap loop, and that loop has BRANCHES PER ARTIFACT: the video encoder's, and
# media::GifEncoder's. The loop once knew about the video path and about GIF and nothing
# else, and it enforced the cap on the video path only, so a GIF went out at whatever
# size it came to - and later, the bisection left the LAST PROBE's file on disk, so the
# app could report "fitted at 119199 bytes" over a 120687-byte file. A capped VIDEO
# destination and a capped GIF destination therefore cover different code, and dropping
# the GIF case would quietly stop covering the branch with the bug history. Both cases
# run a 20-frame range so the cost stays in seconds rather than minutes.
#
# What it replaces, and why every step is by name. The destination used to be
# preselected by writing QSettings from the probe, because the quick-export menu is
# grouped by family and "the third row" stopped being Telegram when a family was added
# in front of it: an INDEX into a reordered list is not a destination, and the probe
# exported a chat video while asserting a sticker's name. The body then clicked recorded
# pixels ((81,162) for the export entry), which the destination-first rewrite moved, so
# it drove a flow that no longer existed and still called itself a cap check. The
# destination is chosen in the picker by its own id now, the frame range is set by name,
# and the export goes through the form the destination dictates.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/_lib.sh" "$@"
# The revision of the export flow this probe's navigation was recorded against. See
# AE_FLOW_REVISION in _lib.sh: the guard refuses to run a probe that predates it.
export RECORDED_AGAINST=2

# THROWAWAY OUTPUT, never the checkout: the platform file dialog resolves a relative
# name against the application's working directory, so the app is launched from here and
# the names typed below land here.
OUTDIR="$AE_PROBE_DIR"
# Both ends of the range are set by name, so a case costs one second of animation rather
# than the project's 601 frames. What is covered here is the fit loop, not the range.
FRAMES=20

# run_case <dest id> <output name> <cap bytes> <cap KB> <cap label> <container> \
#          <codec line the facts must state> <exact size or empty> <duration limit or 0> \
#          <what the cap assertion is about>
run_case() {
    local dest="$1" name="$2" cap="$3" cap_kb="$4" label="$5" container="$6"
    local codec="$7" size="$8" limit="$9" what="${10}"
    local slug="${dest//./_}"
    local out="$OUTDIR/$name"
    local form="/tmp/cap_${slug}_form.txt" facts="/tmp/cap_${slug}_facts.txt"
    local range="/tmp/cap_${slug}_range.txt" status="/tmp/cap_${slug}_status.txt"
    rm -f "$out" "$form" "$form.names" "$facts" "$range" "$status"

    xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
      # THE NUMBER BOXES, BY NAME, THROUGH THE VALUE INTERFACE.
      #
      # This is one process that sets a range and reads it back, and it is also the cheap
      # poll for the status message below. The shared tool can do the first half on its
      # own (ae set/ae value use the Value interface and prefer an EXACT name, so the box
      # wins over the label "- First frame -" that reads the same words); this helper
      # stays because a probe reads several boxes in one walk, and because the status
      # message needs the poll described below.
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

def names(node):
    out = []
    for getter in (node.get_name, node.get_description):
        try:
            value = getter() or ""
        except Exception:
            value = ""
        if value:
            out.append(value)
    return out

def number(node):
    iface = node.get_value_iface()
    if iface is None:
        return None
    try:
        return iface.get_current_value()
    except Exception:
        return None

def box(name):
    # The best match that HOLDS A NUMBER: the same ordering rule the shared tool uses
    # (exact name, then a prefix, then a substring), with "must be settable" as the tie
    # breaker, so the label beside a box can never win it.
    app = root()
    if app is None:
        return None
    best = None
    for node in walk(app):
        if number(node) is None:
            continue
        tier = None
        for candidate in names(node):
            if candidate == name:
                tier = 0
                break
            if tier is None and candidate.startswith(name):
                tier = 1
            elif tier is None and name in candidate:
                tier = 2
        if tier is not None and (best is None or tier < best[0]):
            best = (tier, node)
    return best[1] if best is not None else None

def show(v):
    return str(int(v)) if v == int(v) else str(v)

if mode == "bar":
    # The status bar text, which is ONE node whose name is the whole message. This is a
    # single walk with no retry of its own, unlike a query that waits ten seconds before
    # admitting a widget is not there: the message lives for twenty seconds, so a poll
    # that pays ten seconds per miss can outlive the thing it is polling for.
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

      # cd as its OWN command, not as `cd X && App &`: a background job whose body is a
      # compound command is a SUBSHELL, so $! names the subshell rather than the
      # application, AE_UI_PID matches no accessible root, and every by-name helper
      # answers "no widget named ..." while a window is plainly on screen.
      cd "'"$OUTDIR"'"
      '"$APP"' "'"$PROJ"'" >/tmp/probe_capped_'"$slug"'.log 2>&1 &
      app=$!
      AE_UI_PID=$app; export AE_UI_PID   # the window the tooling must address
      sleep 12
      W=$(xdotool search --name "AnimeEffects" | tail -1)
      xdotool windowactivate --sync $W 2>/dev/null

      # 1. The destination, by ID, through the picker the flow opens first. The id is
      #    unique: a search for the label matches several rows, and the picker answers
      #    with whichever the tree lists first.
      ae_press "File" "the File menu opens"
      ae_press "Export project as" "the export entry is there"
      require_export_flow
      ae set "Search destinations" "'"$dest"'" >/dev/null
      sleep 1.2
      ae_press "Use this destination" "the '"$dest"' destination is accepted"
      ae wait "Advanced" >/dev/null 2>&1 || true
      sleep 1.5

      # 2. A SHORT RANGE, BY NAME, with both ends given: the fit loop is what this case
      #    covers, and one second of animation exercises it as well as twenty-five. The
      #    range is read back below, so the file is judged against what the form holds
      #    rather than against what this probe typed at it.
      if spin set "First frame" "1" "Last frame" "'"$FRAMES"'"; then
        check 0 "the frame range takes the values this probe sets by name ('"$dest"')"
      else
        check 1 "the frame range takes the values this probe sets by name ('"$dest"')"
      fi
      sleep 0.8
      ae_report "'"$form"'" "Destination" "Width" "Height" "Frame rate" "First frame" "Last frame"
      spin read "'"$range"'" "First frame" "Last frame"
      # The cap as the form states it, and the codec the plan resolved. Read with a
      # retry: one query against a busy application can come back with an empty tree, and
      # an empty file here would be read as "the form said nothing".
      for i in 1 2 3 4 5 6 7 8; do
        ae text > "'"$facts"'" 2>/dev/null
        grep -q "Limits:" "'"$facts"'" && break
        sleep 1
      done
      import -window root '"$SHOTS"'/cap_'"$slug"'_01_form.png

      # 3. Export, and name the file in the platform file dialog - the one step that
      #    cannot be driven by name, because that dialog is a different toolkit in the
      #    same process and its widgets are not in the accessibility tree of the
      #    application. It opens with the name field focused and the suggested name
      #    selected, and resolves a relative name against the working directory of the
  #    application.
      ae_press "Export" "the '"$dest"' form is answered"
      sleep 4
      xdotool key --clearmodifiers ctrl+a; sleep 0.4
      xdotool type --delay 25 "'"$name"'"; sleep 0.6
      xdotool key Return

      # 4. THE COMPLETION SIGNAL IS THE APP OWN EXPORT BOOKKEEPING, not the size on disk
      #    and not the status message. Not the size, because the fit loop rewrites the
      #    file on every attempt (removing the previous attempt first), so neither a file
      #    that exists nor one whose size has stopped changing is a finished export. Not
      #    the status message, because it lives for twenty seconds and has to be polled
      #    for cheaply (see the bar mode above).
      #
      #    reportExportOutcome() writes export_last_name into QSettings when it returns,
      #    and that survives on disk for the rest of the run. The library sandboxes
      #    XDG_CONFIG_HOME, so this is the probe own settings file, never the developer
      #    own.
      cf="$XDG_CONFIG_HOME/AnimeEffectsProject/AnimeEffects.conf"
      for i in $(seq 1 150); do
        grep -q "^export_last_name='"$name"'$" "$cf" 2>/dev/null && break
        sleep 2
      done
      echo "waited up to $((i * 2))s for the '"$dest"' export to report itself finished"
      # THE SNAPSHOT COMES FIRST: the message lives twenty seconds and the picture is what
      # has to catch it, so the screenshot must not queue behind the read below.
      import -window root '"$SHOTS"'/cap_'"$slug"'_02_done.png
      # ...and then the message itself, which states the size the app measured against
      # the cap, polled cheaply while it is up.
      status=""
      for i in $(seq 1 40); do
        status=$(spin bar "Exported '"$name"'" 2>/dev/null)
        [ -n "$status" ] && break
        sleep 0.4
      done
      printf "%s\n" "$status" > "'"$status"'"
      kill $app 2>/dev/null; wait $app 2>/dev/null
    ' 2>&1 | tail -4

    # The form the capped destination dictated, and the cap it says it will be held to. A
    # cap that is stated but never applied is the bug this probe exists for, so both
    # halves are asked for: the numbers on the form, and the bytes on disk.
    assert_showing "$form" "Destination" "the form states the capped destination it was given ($dest)"
    assert_showing "$form" "Width" "the destination's own size reaches the form ($dest)"
    assert_showing "$form" "Height" "both dimensions of it ($dest)"
    if grep -q "$cap_kb KB" "$facts"; then
        check 0 "the form states the cap this export will be held to ($cap_kb KB, $dest)"
    else
        check 1 "the form states the cap this export will be held to ($cap_kb KB, $dest) (got: $(tr '\n' ' ' < "$facts" 2>/dev/null | head -c 200))"
    fi
    if grep -q "$codec" "$facts"; then
        check 0 "and the codec the plan resolved for it ($codec)"
    else
        check 1 "and the codec the plan resolved for it ($codec) (got: $(tr '\n' ' ' < "$facts" 2>/dev/null | head -c 200))"
    fi
    first=$(sed -n 's/^First frame=//p' "$range" 2>/dev/null | head -1)
    last=$(sed -n 's/^Last frame=//p' "$range" 2>/dev/null | head -1)
    want=$(( ${last:-0} - ${first:-0} + 1 ))
    [ -n "$first" ] && [ -n "$last" ] && [ "$first" != "absent" ] && [ "$last" != "absent" ] \
        && [ "$want" -ge 2 ] && [ "$want" -le 60 ] 2>/dev/null
    check $? "the form holds the short range this probe set by name ($first..$last, $dest)"

    # THE ASSERTION THIS PROBE EXISTS FOR: the file on disk, weighed against the cap the
    # destination declares.
    assert_under "$out" "$cap" "$what"
    if [ -s "$out" ]; then
        # COUNTED, not read from the container: a WebM does not carry a frame count, so
        # nb_frames answers N/A for a file that is plainly an animation. -count_frames
        # decodes the stream and says how many frames came out.
        got=$(ffprobe -v error -count_frames -select_streams v -show_entries stream=nb_read_frames \
              -of csv=p=0 "$out" 2>/dev/null | head -1)
        [ -n "$got" ] && [ "$got" -gt 1 ] 2>/dev/null
        check $? "the fitted file is still an animation (frames=${got:-none}, $dest)"
        # ...and it is the artifact the destination asked for, not whatever the last
        # attempt happened to leave behind.
        fmt=$(ffprobe -v error -show_entries format=format_name -of csv=p=0 "$out" 2>/dev/null | head -1)
        case "$fmt" in *"$container"*) check 0 "and the container is the one the destination dictates ($fmt)";;
                           *)             check 1 "and the container is the one the destination dictates ($fmt)";; esac
        dur=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$out" 2>/dev/null | head -1)
        if [ "$limit" != "0" ]; then
            ok=$(awk -v d="${dur:-0}" -v l="$limit" 'BEGIN { print (d > 0 && d <= l + 0.05) ? 0 : 1 }')
            check "$ok" "and it is inside the destination's own ${limit} s duration limit (${dur:-none} s)"
        fi
        # A destination that declares an exact size owns it, and the file has to be that
        # size - which for a capped GIF is also what says the artifact is a real one.
        if [ -n "$size" ]; then
            got_size=$(ffprobe -v error -select_streams v -show_entries stream=width,height -of csv=p=0 "$out" 2>/dev/null | head -1)
            got_size="${got_size/,/x}"
            [ "$got_size" = "$size" ]
            check $? "and it is the exact size the destination declares ($size, got ${got_size:-none})"
        fi
    fi
    # The app's verdict, from the status bar: it names the file, states the size in KB, and
    # measures it against the cap. ONE assertion with both halves, so an empty message
    # cannot pass it: the message has to NAME the cap, and it must not be the sentence the
    # app uses for a file that went out over it.
    if grep -q "Exported $name" "$status"; then
        check 0 "the status bar reports the export by name ($(tr -d '\n' < "$status" | head -c 120))"
    else
        check 1 "the status bar reports the export by name (got: $(tr -d '\n' < "$status" 2>/dev/null | head -c 120))"
    fi
    if grep -qF "of $cap_kb KB allowed for $label" "$status" && ! grep -q "OVER THE LIMIT" "$status"; then
        check 0 "and measures it against that cap without reporting it as over the limit"
    else
        check 1 "and measures it against that cap without reporting it as over the limit (got: $(tr -d '\n' < "$status" 2>/dev/null | head -c 160))"
    fi
    grep -qF "$OUTDIR" "$status"
    check $? "and says it wrote into the throwaway directory, not the checkout ($dest)"
    # The app own bookkeeping, which is also the signal this case waited on: the export
    # reached the end of its reporting path rather than merely leaving bytes behind.
    if grep -q "^export_last_name=$name$" "$XDG_CONFIG_HOME/AnimeEffectsProject/AnimeEffects.conf" 2>/dev/null; then
        check 0 "and the app recorded the finished export in its own settings ($dest)"
    else
        check 1 "and the app recorded the finished export in its own settings ($dest)"
    fi
    assert_rendered "$SHOTS/cap_${slug}_01_form.png" "the export form carrying the capped destination ($dest)"
    assert_rendered "$SHOTS/cap_${slug}_02_done.png" "the app after the capped export ($dest)"
}

# A capped VIDEO destination: WebM/VP9 with alpha, 512 px, three seconds, 256 KB. This is
# the branch of the fit loop that already worked, and the one that proves the destination
# can cap a video whose natural size is far over the limit.
run_case telegram.sticker probe_capped.webm 262144 256 "Telegram sticker" webm \
    "VP9 in WebM" "" 3 \
    "the Telegram sticker is inside its 256 KB cap"

# A capped GIF destination: 240x240, 500 KB, GIF only. media::GifEncoder owns this
# artifact, and the cap loop did not know about it at all once - a GIF went out at
# whatever size it came to. Its file is also checked for the frames and the exact size
# that make it a real animated GIF rather than a still.
run_case wechat.sticker probe_capped.gif 512000 500 "WeChat sticker (微信表情)" gif \
    "animated GIF" "240x240" 0 \
    "the WeChat sticker GIF is inside its 500 KB cap"

summary probe_capped
