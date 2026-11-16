#!/bin/bash
# Ctrl+Z with the Properties panel holding the keyboard.
#
# notes.md: "Ctrl + Z shortcut for some reason ceased to work." The key map was never
# the problem: "Undo" is a rebindable command (KeyCommandMap.cpp, Ctrl+Z) dispatched
# from the live MainWindow::keyPressEvent, and the Edit menu's own Undo proves the
# slot behind it. What ate the key was FOCUS. The Properties dock is a QQuickWidget
# with Qt's default StrongFocus, and QQuickWidget::keyPressEvent forwards the event to
# its offscreen QQuickWindow and never calls QWidget::keyPressEvent - with no active
# focus item in the scene the key is handed to nobody and the event stays accepted, so
# QApplication::notify's propagation walk stops at the widget and MainWindow never sees
# it. Every rebindable command was dead there, not just Ctrl+Z.
#
# So this probe drives the REAL application and asserts, in pixels and in numbers:
#   a. an inspector edit reaches the canvas (the premise: there is something to undo);
#   b. with the Properties panel focused and NO text field focused, Ctrl+Z takes the
#      document back to the pre-edit pixels - the half that changes no pixel today;
#   c. Ctrl+Shift+Z redoes it and Ctrl+S saves the project from the same focused
#      state - a SECOND and a THIRD rebindable command, so the probe proves the whole
#      key map came back rather than one special case being wired up somewhere.
#
# THE EDIT COMES FROM THE INSPECTOR'S OWN ACCESSIBILITY VALUE (`ae set "Opacity"`),
# which is the same one-undo-step setter the field uses (InspectorRowModel::setOpacity
# -> KeyAccessor::assignOpacity) and which focuses nothing. That matters: the probe
# must NOT start from a focused text field, because a focused field's Ctrl+Z is its
# own text undo and is supposed to stay that way.
#
# THE PROJECT IS OPENED FROM A COPY. Ctrl+S saves in place, and the fixture lives in
# the checkout or in the user's Downloads: a probe that saved it would rewrite the
# project it is testing against. AE_PROBE_DIR is the sandbox _lib.sh already made.
#
# Addressed by name through the accessibility tree; input is xdotool inside xvfb-run,
# and _lib.sh's display guard runs before the first synthetic event, so nothing here
# can reach the developer's real desktop.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/_lib.sh" "$@"

WORK="$AE_PROBE_DIR/undo_key.anie"
cp "$PROJ" "$WORK"

STATE=/tmp/undo_key_state.txt
SAVEF=/tmp/undo_key_save.txt
ROWS=/tmp/undo_key_rows.txt
rm -f "$STATE" "$SAVEF" "$ROWS" /tmp/undo_key_label.txt
: > "$STATE"

xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
  set -u
  cd "$AE_PROBE_DIR"
  '"$APP"' "'"$WORK"'" >/tmp/probe_undo_key.log 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID
  wait_ready 120 || sleep 12
  W=$(xdotool search --name "AnimeEffects" | tail -1)
  xdotool windowactivate --sync $W 2>/dev/null
  sleep 1

  # NOTHING BELOW MAY REACH A REAL DESKTOP. Every event here is synthesized with
  # xdotool, so the display is checked first: the guard exits 97 rather than letting a
  # leaked click or key land in the developer session (see _lib.sh).
  ae_guard_display

  FR=$(ae extents "File")
  [ -n "$FR" ] || { echo "no File menu: this is not the application window" >&2; exit 1; }
  read -r FX FY FW FH <<< "$FR"
  PARKX=$((FX + 3)); PARKY=$((FY + FH / 2))
  park() { xdotool mousemove --sync $PARKX $PARKY; sleep 0.5; }
  shot() { park; import -window root "$1"; }

  # ---- 1. a layer to inspect ------------------------------------------------
  # Opening a project selects nothing, and the inspector has rows only for a selected
  # node. The object tree publishes its rows as table cells, so click them from the
  # second on (the first is the project root) until the panel publishes the Opacity
  # row - the row that owns both the value this probe edits and the label it clicks.
  # A tree that publishes only its root is expanded through the root own expander
  # first, the route probe_graph_keys uses.
  rows_dump() { ae dump 2>/dev/null | sed -n "s/^ *table cell *| //p"; }
  rows_dump | sed -n "1,8p" > "'"$ROWS"'"
  if [ "$(grep -c . "'"$ROWS"'")" -lt 3 ]; then
    TOP=$(sed -n 1p "'"$ROWS"'")
    R=$(ae extents "$TOP" "table cell")
    if [ -n "$R" ]; then
      read -r EX EY EW EH <<< "$R"
      xdotool mousemove --sync $((EX - 12)) $((EY + EH / 2)) click 1
      sleep 1.5
      rows_dump | sed -n "1,8p" > "'"$ROWS"'"
    fi
  fi
  PICKED=""
  while IFS= read -r cand; do
    [ -n "$cand" ] || continue
    R=$(ae extents "$cand" "table cell") || continue
    [ -n "$R" ] || continue
    read -r RX RY RW RH <<< "$R"
    xdotool mousemove --sync $((RX + 4)) $((RY + RH / 2)) click 1
    sleep 1.5
    if [ -n "$(ae extents "Opacity" label)" ]; then PICKED="$cand"; break; fi
  done < <(sed -n "2,8p" "'"$ROWS"'")
  [ -n "$PICKED" ] || { echo "no object-tree row published an Opacity row in the inspector" >&2; exit 1; }
  echo "inspecting row: $PICKED"

  # ---- 2. the focused state the bug was about -------------------------------
  # Done BEFORE the baseline capture, so the only difference between the shots is the
  # edit itself. The click lands on the row label - a Qt Quick Text, not a control -
  # so the press makes the QQuickWidget the focus widget while no item in the scene
  # takes active focus. That is exactly the state in which Ctrl+Z used to be dropped.
  LBL=$(ae extents "Opacity" label)
  [ -n "$LBL" ] || { echo "the Opacity row published no label to click" >&2; exit 1; }
  read -r LX LY LW LH <<< "$LBL"
  echo "opacity label: $LBL" > /tmp/undo_key_label.txt
  xdotool mousemove --sync $((LX + LW / 2)) $((LY + LH / 2)) click 1
  sleep 1
  echo "focus after the panel click: $(ae focus)"
  if ae focus 2>/dev/null | grep -q "^text "; then
    check 1 "the panel click left a text field focused ($(ae focus))"
  else
    check 0 "the panel click focuses the panel and no text field ($(ae focus))"
  fi

  # ---- a. the edit, and the premise ----------------------------------------
  # Opacity 1 -> 0 or 0 -> 1: the largest change the field can make, so the pixel
  # counts below do not depend on how much of the canvas the layer of this fixture covers.
  V0=$(ae value "Opacity")
  TARGET=0
  [ "$V0" = "0" ] && TARGET=1
  printf "V0=%s\n" "$V0" >> "'"$STATE"'"
  echo "opacity before: [$V0], setting $TARGET"
  shot "'"$SHOTS"'/undokey_00_before.png"
  ae set "Opacity" "$TARGET"
  sleep 2
  V1=$(ae value "Opacity")
  printf "V1=%s\n" "$V1" >> "'"$STATE"'"
  echo "opacity after the edit: [$V1]"
  shot "'"$SHOTS"'/undokey_01_edited.png"

  # ---- b. Ctrl+Z from the focused panel ------------------------------------
  # This is the assertion the whole probe exists for: today the key never leaves the
  # QQuickWidget and the two shots are identical.
  xdotool key --clearmodifiers ctrl+z
  sleep 2
  V2=$(ae value "Opacity")
  printf "V2=%s\n" "$V2" >> "'"$STATE"'"
  echo "opacity after ctrl+z: [$V2]"
  shot "'"$SHOTS"'/undokey_02_undone.png"

  # ---- c1. Ctrl+Shift+Z, the same gate -------------------------------------
  xdotool key --clearmodifiers ctrl+shift+z
  sleep 2
  V3=$(ae value "Opacity")
  printf "V3=%s\n" "$V3" >> "'"$STATE"'"
  echo "opacity after ctrl+shift+z: [$V3]"
  shot "'"$SHOTS"'/undokey_03_redone.png"

  # ---- c2. a second rebindable command, from the same focused state --------
  # Ctrl+S saves the copy opened above (it has a file name, so no dialog opens). The
  # save is the copy mtime moving; the clock is read here, inside the sandbox, so the
  # value asserted below is the file this probe opened and nothing else.
  SAVE_BEFORE=$(stat -c %.9Y "'"$WORK"'")
  sleep 1.2
  xdotool key --clearmodifiers ctrl+s
  sleep 3
  SAVE_AFTER=$(stat -c %.9Y "'"$WORK"'")
  printf "SAVE_BEFORE=%s\nSAVE_AFTER=%s\n" "$SAVE_BEFORE" "$SAVE_AFTER" > "'"$SAVEF"'"
  echo "save mtime: [$SAVE_BEFORE] -> [$SAVE_AFTER]"

  # ---- d. and a focused text field keeps its OWN Ctrl+Z --------------------
  # The other side of the guard: PropertyWidget forwards a key only when the scene
  # has NO active focus item. The number field begins editing with its TextInput
  # focused (ScrubSpinBox::beginEdit calls forceActiveFocus), so Ctrl+Z there must
  # leave the document alone - the field owns the caret and its own undo. Qt Quick
  # pre-accepts the key for the focused item (QQuickDeliveryAgentPrivate::
  # deliverKeyEvent accepts before it sends), so the document must still hold the
  # redone value after this press.
  V4=$(ae value "Opacity")
  ae click "Opacity" "spin button"
  sleep 1
  echo "focus with the field editing: $(ae focus)"
  xdotool key --clearmodifiers ctrl+z
  sleep 1
  V5=$(ae value "Opacity")
  printf "V4=%s\nV5=%s\n" "$V4" "$V5" >> "'"$STATE"'"
  echo "opacity across the field Ctrl+Z: [$V4] -> [$V5]"
  xdotool key --clearmodifiers Escape
  sleep 0.5

  # ---- the comparison region: the document, not the command log -------------
  # The bottom pane of the Properties dock IS the command log, and undo/redo append a line
  # to it (CommandLogView is "the one place undo/redo says which command it moved").
  # A full-window comparison would therefore read the log entry as if it were the
  # document. Everything LEFT of the Properties dock is document: the canvas, the
  # timeline lanes and the object tree. Cropped here, where the rectangle of the dock
  # is known, so the outer assertions compare the drawing rather than the prose.
  DOCKP=$(ae extents "Properties")
  read -r DKX DKY DKW DKH <<< "$DOCKP"
  if [ -z "${DKX:-}" ] || [ "$DKX" -lt 200 ]; then
    echo "no usable Properties dock rectangle: [$DOCKP]" >&2
    exit 1
  fi
  for n in 00_before 01_edited 02_undone 03_redone; do
    H=$(magick identify -format "%h" "'"$SHOTS"'/undokey_$n.png")
    magick "'"$SHOTS"'/undokey_$n.png" -crop "${DKX}x${H}+0+0" +repage \
        "'"$SHOTS"'/undokey_$n.canvas.png"
  done
  echo "document crop: ${DKX}px wide, left of the Properties dock at x=$DKX"

  kill $app 2>/dev/null; wait $app 2>/dev/null
' 2>&1 | tail -16

v0=$(sed -n 's/^V0=//p' "$STATE" | tail -1)
v1=$(sed -n 's/^V1=//p' "$STATE" | tail -1)
v2=$(sed -n 's/^V2=//p' "$STATE" | tail -1)
v3=$(sed -n 's/^V3=//p' "$STATE" | tail -1)
v4=$(sed -n 's/^V4=//p' "$STATE" | tail -1)
v5=$(sed -n 's/^V5=//p' "$STATE" | tail -1)

# THE DOCUMENT, not just the ink: the inspector reads the value back out of the
# timeline, so these three say what happened to the project.
[ -n "${v0:-}" ] && [ -n "${v1:-}" ] && [ "$v0" != "$v1" ]
check $? "the inspector edit changed the document value ([${v0:-}] -> [${v1:-}])"
[ -n "${v2:-}" ] && [ "${v2:-x}" = "${v0:-y}" ]
check $? "ctrl+z with the Properties panel focused put it back ([${v2:-}], was [${v0:-}] before the edit)"
[ -n "${v3:-}" ] && [ "${v3:-x}" = "${v1:-y}" ]
check $? "ctrl+shift+z redid it from the same focused state ([${v3:-}], was [${v1:-}] after the edit)"
# The guard's other side: with the number field editing, the key is the FIELD's, so
# the document must not move.
[ -n "${v4:-}" ] && [ "${v5:-x}" = "${v4:-y}" ]
check $? "with the number field editing, ctrl+z stays the field's own (the document held [${v5:-}])"

# THE DRAWING. ImageMagick's AE metric counts differing pixels, so none of this needs
# to know where the canvas is; the pointer is parked in the same place before every
# capture, which keeps a hover highlight out of the comparison. The inputs are the
# probe's own crops of everything LEFT of the Properties dock (see the body): the
# command log lives inside that dock and undo/redo write a line into it, so a
# full-window count would be reading prose, not the document.
changed=$(compare -metric AE "$SHOTS/undokey_00_before.canvas.png" "$SHOTS/undokey_01_edited.canvas.png" null: 2>&1 | awk '{print $1}')
undone=$(compare -metric AE "$SHOTS/undokey_01_edited.canvas.png" "$SHOTS/undokey_02_undone.canvas.png" null: 2>&1 | awk '{print $1}')
restored=$(compare -metric AE "$SHOTS/undokey_00_before.canvas.png" "$SHOTS/undokey_02_undone.canvas.png" null: 2>&1 | awk '{print $1}')
redone=$(compare -metric AE "$SHOTS/undokey_01_edited.canvas.png" "$SHOTS/undokey_03_redone.canvas.png" null: 2>&1 | awk '{print $1}')

awk -v c="${changed:-0}" 'BEGIN { exit (c+0 >= 100) ? 0 : 1 }'
check $? "the inspector edit reaches the canvas ($changed pixels changed)"
# An undo has to change back about as much as the edit changed. A dead Ctrl+Z leaves
# the frame untouched, which reads as 0 here and as the full edit below.
awk -v u="${undone:-0}" -v c="${changed:-0}" 'BEGIN { exit (u+0 > 0 && u*2 >= c+0) ? 0 : 1 }'
check $? "ctrl+z with the Properties panel focused changes the canvas back ($undone pixels, the edit was $changed)"
awk -v r="${restored:-0}" -v c="${changed:-0}" 'BEGIN { exit (r+0 <= 300 && r*4 <= c+0) ? 0 : 1 }'
check $? "and the undone frame is the pre-edit one (only $restored of the $changed edited pixels still differ)"
awk -v d="${redone:-0}" -v c="${changed:-0}" 'BEGIN { exit (d+0 <= 300 && d*4 <= c+0) ? 0 : 1 }'
check $? "ctrl+shift+z returns the edited frame (only $redone pixels differ from it)"

# THE SECOND COMMAND. A save that never happened leaves the mtime where it was, and
# a Ctrl+S that opened the Save dialog instead would leave it there too.
save_before=$(sed -n 's/^SAVE_BEFORE=//p' "$SAVEF" 2>/dev/null | tail -1)
save_after=$(sed -n 's/^SAVE_AFTER=//p' "$SAVEF" 2>/dev/null | tail -1)
[ -n "${save_before:-}" ] && [ -n "${save_after:-}" ] && \
    awk -v b="$save_before" -v a="$save_after" 'BEGIN { exit (a+0 > b+0) ? 0 : 1 }'
check $? "ctrl+s from the same focused state saves the project (mtime ${save_before:-none} -> ${save_after:-none})"

assert_rendered "$SHOTS/undokey_00_before.png" "the window before the edit"
assert_rendered "$SHOTS/undokey_01_edited.png" "the window after the edit"
assert_rendered "$SHOTS/undokey_02_undone.png" "the window after ctrl+z"
assert_rendered "$SHOTS/undokey_03_redone.png" "the window after ctrl+shift+z"
summary probe_undo_key
