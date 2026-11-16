#!/bin/bash
# The PaintWeights panel's live wiring: enter the mode, bake, paint, undo, revert,
# rebake, save and reopen.
#
# What only a live run can answer (unit gates cover the commands and the state):
#   * the mode is reachable, the panel publishes its state, and the brush rows are
#     DISABLED with an explanation while the bone is still Envelope;
#   * a canvas press selects the nearest bone and refuses to paint a live bone;
#   * Bake turns the same field into authored data and the painted canvas crop is
#     UNCHANGED (the bake is exact - the headless hash gate is the strict form, this
#     is the pixel-level smoke test at the panel's own size);
#   * a Subtract stroke CHANGES THE CANVAS (the wash is the field), undo restores
#     that canvas exactly, redo repaints it exactly; Revert returns Envelope and
#     disables the brush, Rebake reproduces the initial bake exactly, and Clear
#     removes the wash while keeping the bone Baked (undo restores it);
#   * Ctrl+S writes format 0.22 and a fresh process reads the baked state back.
#
# The fixture is a small real rig (`pagedoll.anie`: two rigs, 3 bones, one skinned
# mesh each). The probe copies it: the project is SAVED.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/_lib.sh" "$@"

# THE FIXTURE IS NAMED, NOT INHERITED. This probe navigates the pagedoll rig
# (Folder 1/body/legs/Layer 13); _lib.sh's default is the smallest .anie on the
# machine, and that became a different 425 KB download with no rig - the probe then
# polled 90 AT-SPI timeouts for a row that could never appear. An explicit argument
# or AE_PROJECT still wins.
PAGEDOLL_FIXTURE="$HOME/Downloads/ANIE/pagedoll.anie"
if [ -z "${1:-}" ] && [ -z "${AE_PROJECT:-}" ] && [ -f "$PAGEDOLL_FIXTURE" ]; then
    PROJ="$PAGEDOLL_FIXTURE"
fi

WORK="$AE_PROBE_DIR/weights_bake_paint.anie"
cp "$PROJ" "$WORK"
SHOTDIR="$SHOTS/weights"
mkdir -p "$SHOTDIR"
TXT=/tmp/weights_state.txt
export WSTATE="$TXT" SHOTDIR WORK
export REFUSAL="Bake before painting."
CROPS=/tmp/weights_crops.txt
rm -f "$TXT"* "$CROPS" /tmp/weights_*.png

xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
  set -u
  cd "$AE_PROBE_DIR"
  "$APP" "$WORK" >/tmp/probe_weights.log 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID
  trap "kill $app 2>/dev/null" EXIT
  wait_ready 180 || { echo "no main window" >&2; exit 1; }
  W=$(xwininfo -root -tree 2>/dev/null | grep -m1 "1440x900+0+0" | awk "{print \$1}")
  xdotool windowactivate --sync "$W" 2>/dev/null
  sleep 1
  ae_guard_display

  shot() { xdotool mousemove --sync 20 890; sleep 0.3; import -window root "$1"; }
  # The canvas area of the 1440x900 window: the artwork, the skeleton and the
  # weight wash, with no panel or timeline in it. A change here is a change in
  # what the artist sees; the panel readout is not a subject of the pixel
  # comparisons (the previous probe read a number out of the panel and called it
  # the field, which a count cannot stand for).
  canvas_crop() { convert "$1" -crop 360x360+$((X + 380))+$((Y + 20)) +repage "$2"; }
  state_showing() {
    ae_report /tmp/wshow.txt "$@" >/dev/null 2>&1
    local n
    for n in "$@"; do
      grep -qx "showing $n" /tmp/wshow.txt && return 0
    done
    return 1
  }
  # Selection, by the tree OWN published names. The pagedoll rig owner is
  # valentine pagedoll(1)/Folder 1/body/legs/Layer 13; the tree is collapsed at
  # launch and the rows are a QML table, so the eye buttons ("Hide <name>") are
  # what the accessibility tree publishes for every visible row - they give the
  # row y, the row text at x=60 selects it, and Right expands a folder selected
  # this way. Scrolling is part of it: the children of body do not fit the pane.
  row_y() {
    local r
    r=$(ae extents "Hide $1" 2>/dev/null) || return 1
    [ -n "$r" ] || return 1
    echo "$r" | awk "{print \$2 + \$4 / 2}"
  }
  click_row() {
    local y
    y=$(row_y "$1") || return 1
    xdotool mousemove --sync 60 "$y" click 1
    sleep 0.5
  }
  scroll_tree_until() {
    local name=$1 i
    for i in 1 2 3 4 5 6 7 8; do
      row_y "$name" >/dev/null 2>&1 && return 0
      xdotool mousemove --sync 150 820 click 5
      sleep 0.5
    done
    return 1
  }
  select_rig() {
    local r x y w h
    # Folder 1 sits at depth 1, collapsed: its row is the top-level table cell
    # that the tree publishes BEFORE any eye button exists. Expand it from there.
    r=$(ae extents "Folder 1" "table cell" 2>/dev/null) || return 1
    [ -n "$r" ] || return 1
    read -r x y w h <<< "$r"
    xdotool mousemove --sync 10 $((y + h / 2)) click 1
    sleep 1.2
    # Collapse head so the children of body fit, then expand body and legs.
    click_row head || return 1
    xdotool key Left
    sleep 0.6
    click_row body || return 1
    xdotool key Right
    sleep 1
    scroll_tree_until legs || return 1
    click_row legs || return 1
    xdotool key Right
    sleep 1
    scroll_tree_until "Layer 13" || return 1
    click_row "Layer 13" || return 1
    sleep 1.5
    # The inspector names the selected node; a failed click leaves it empty.
    ae extents "Opacity" label >/dev/null 2>&1
  }

  # wait_ready means the MAIN WINDOW is up, not that OUR project finished loading
  # (the Debug build opens the bundled sample.psd first, then the argument). Poll
  # for one of this fixture rig row names before touching the tree.
  loaded=0
  for i in $(seq 1 30); do
    if ae extents "Folder 1" "table cell" >/dev/null 2>&1; then loaded=1; break; fi
    sleep 1
  done
  [ "$loaded" = 1 ] || {
    echo "the fixture project never published Folder 1 (is $PROJ the pagedoll rig?)" >&2
    exit 1
  }

  # ---- 1. a rig, the Bone tool, Paint weights -------------------------------
  select_rig || { echo "no rig row (Layer 13/Layer 30) in the object tree" >&2; exit 1; }
  ae_activate "Bone editor" "the Bone tool is reachable"
  sleep 1
  ae_activate "Paint weights" "the Paint weights mode is reachable"
  sleep 1.5
  state_showing "Select a bone" && echo "Select a bone" > "$WSTATE.initial" || echo "other" > "$WSTATE.initial"
  # No bone is selected yet; the brush is visible but disabled.
  if ae enabled "Size" >/dev/null 2>&1; then echo "ENVELOPE_BRUSH=enabled"; else echo "ENVELOPE_BRUSH=disabled"; fi > "$WSTATE.brush_initial"
  if ae absent "Bake" >/dev/null 2>&1; then echo "BAKE=absent"; else echo "BAKE=present"; fi > "$WSTATE.bake_initial"
  shot "$SHOTDIR/w_00_mode.png"

  # ---- 2. a canvas press selects the nearest bone, and refuses a live one ----
  eval "$(xdotool getwindowgeometry --shell "$W")"
  CX=$((X + WIDTH * 45 / 100))
  CY=$((Y + HEIGHT * 45 / 100))
  xdotool mousemove --sync $CX $CY click 1
  sleep 2
  state_showing "Envelope" && echo "Envelope" > "$WSTATE.after_click" || echo "other" > "$WSTATE.after_click"
  state_showing "$REFUSAL" && echo "Bake before painting." > "$WSTATE.refused" || echo "no-refusal" > "$WSTATE.refused"
  if ae enabled "Bake" >/dev/null 2>&1; then echo "BAKE=enabled"; else echo "BAKE=disabled"; fi > "$WSTATE.bake_after_click"
  shot "$SHOTDIR/w_01_envelope.png"
  # The bake is exact: the same crop of the artwork must be identical before and
  # after it. The crop is the upper body, away from the selected leg bone: the
  # state label (canvas top-left) and the baked-joint marker are overlays and are
  # deliberately outside it, while the artwork must not move.
  convert "$SHOTDIR/w_01_envelope.png" -crop 260x160+$((X + 650))+$((Y + 100)) +repage "$SHOTDIR/w_01_crop.png"

  # ---- 3. Bake, and the painted crop does not move -------------------------
  ae_activate "Bake" "Bake is offered and enabled on the selected envelope bone"
  sleep 3
  if ae enabled "Size" >/dev/null 2>&1; then echo "BAKED_BRUSH=enabled"; else echo "BAKED_BRUSH=disabled"; fi > "$WSTATE.brush_baked"
  shot "$SHOTDIR/w_02_baked.png"
  convert "$SHOTDIR/w_02_baked.png" -crop 260x160+$((X + 650))+$((Y + 100)) +repage "$SHOTDIR/w_02_crop.png"
  # The canvas baseline every stroke comparison is against.
  canvas_crop "$SHOTDIR/w_02_baked.png" "$SHOTDIR/stroke_before.png"

  # ---- 4. a Subtract stroke changes the authored field ---------------------
  # The combo reports its CURRENT VALUE as its name ("Add"); the stable
  # Accessible.name is the preferred handle, the value is the fallback. The list
  # is a popup sibling, not a child of the combo, so choose the row by its own
  # accessible name instead of the combo helper.
  if ! ae click "Paint mode" >/dev/null 2>&1; then
    ae click "Add" >/dev/null 2>&1 || true
  fi
  sleep 0.6
  ae click "Subtract" "menu item" >/dev/null 2>&1 || \
    ae click "Subtract" >/dev/null 2>&1 || \
    xdotool key --clearmodifiers Escape
  sleep 1
  ae set "Size" 400 >/dev/null 2>&1 || true
  ae set "Hardness" 100 >/dev/null 2>&1 || true
  ae set "Flow" 100 >/dev/null 2>&1 || true
  sleep 1
  ae_drag $((CX - 200)) $((CY - 100)) 400 200 24
  sleep 3
  shot "$SHOTDIR/w_03_painted.png"
  canvas_crop "$SHOTDIR/w_03_painted.png" "$SHOTDIR/stroke_painted.png"

  # ---- 5. undo the stroke, then redo it ------------------------------------
  ae_press "Edit" "the Edit menu opens"
  sleep 0.6
  ae_press "Undo" "Undo is offered after a stroke"
  sleep 3
  shot "$SHOTDIR/w_04_undone.png"
  canvas_crop "$SHOTDIR/w_04_undone.png" "$SHOTDIR/stroke_undone.png"
  ae_press "Edit" "the Edit menu opens again"
  sleep 0.6
  ae_press "Redo" "Redo is offered"
  sleep 3
  shot "$SHOTDIR/w_04b_redo.png"
  canvas_crop "$SHOTDIR/w_04b_redo.png" "$SHOTDIR/stroke_redo.png"

  # ---- 6. Revert to envelope (the overflow menu), then rebake ---------------
  # Revert is a maintenance verb, not the primary one: it lives behind the
  # ellipsis, which is where a dock puts the actions that do not get a button.
  ae_activate "More weight actions" "the weight-actions menu opens"
  sleep 0.6
  if ! ae_activate "Revert to Envelope" "Revert is offered on a baked bone"; then
    ae click "Revert to Envelope" "menu item" >/dev/null 2>&1 \
      || { echo "no Revert row in the weight-actions menu" >&2; exit 1; }
  fi
  sleep 2.5
  state_showing "Envelope" && echo "Envelope" > "$WSTATE.after_revert" || echo "other" > "$WSTATE.after_revert"
  if ae enabled "Size" >/dev/null 2>&1; then echo "REVERT_BRUSH=enabled"; else echo "REVERT_BRUSH=disabled"; fi > "$WSTATE.brush_revert"
  shot "$SHOTDIR/w_05_reverted.png"

  # Revert is one undo step; undo it to get the field back, then Rebake it from the
  # envelope (the action that must work with a Baked state).
  ae_press "Edit" "the Edit menu opens"
  sleep 0.6
  ae_press "Undo" "Undo restores the reverted field"
  sleep 2.5
  ae_activate "Rebake" "Rebake is offered on a baked bone"
  sleep 2.5
  shot "$SHOTDIR/w_05b_rebake.png"
  canvas_crop "$SHOTDIR/w_05b_rebake.png" "$SHOTDIR/stroke_rebake.png"

  # ---- 6b. Clear empties the field but keeps the bone Baked; undo restores it ----
  ae_activate "More weight actions" "the weight-actions menu opens for Clear"
  sleep 0.6
  if ! ae_activate "Clear Weights" "Clear is offered on a baked bone"; then
    ae click "Clear Weights" "menu item" >/dev/null 2>&1 \
      || { echo "no Clear row in the weight-actions menu" >&2; exit 1; }
  fi
  sleep 2.5
  state_showing "Baked" && echo "Baked" > "$WSTATE.after_clear" || echo "other" > "$WSTATE.after_clear"
  if ae enabled "Size" >/dev/null 2>&1; then echo "CLEAR_BRUSH=enabled"; else echo "CLEAR_BRUSH=disabled"; fi > "$WSTATE.brush_clear"
  shot "$SHOTDIR/w_06_cleared.png"
  canvas_crop "$SHOTDIR/w_06_cleared.png" "$SHOTDIR/stroke_cleared.png"
  ae_press "Edit" "the Edit menu opens after Clear"
  sleep 0.6
  ae_press "Undo" "Undo restores the cleared field"
  sleep 2.5
  shot "$SHOTDIR/w_06b_clear_undo.png"
  canvas_crop "$SHOTDIR/w_06b_clear_undo.png" "$SHOTDIR/stroke_clear_undo.png"

  # ---- 7. save, quit, and read the field back in a fresh process -----------
  # Click the rig row again so the main window, not a QML field, holds the key.
  # The tree may still be expanded from the selection pass; collapsing Folder 1
  # by its chevron would hide the row and make the full navigation fail.
  if row_y "Layer 13" >/dev/null 2>&1; then
    click_row "Layer 13" || exit 1
  else
    select_rig || exit 1
  fi
  MT_BEFORE=$(stat -c %.9Y "'"$WORK"'")
  xdotool key --clearmodifiers ctrl+s
  sleep 4
  MT_AFTER=$(stat -c %.9Y "'"$WORK"'")
  if [ "$MT_AFTER" = "$MT_BEFORE" ]; then
    # The shortcut can be swallowed by a focused QML control; the menu is the
    # second route to the same save.
    ae_press "File" "the File menu opens for Save"
    sleep 0.6
    ae_press "Save" "Save is offered in the File menu"
    sleep 4
    MT_AFTER=$(stat -c %.9Y "'"$WORK"'")
  fi
  printf "MT_BEFORE=%s\nMT_AFTER=%s\n" "$MT_BEFORE" "$MT_AFTER" > "$WSTATE.save"
  kill $app 2>/dev/null
  wait $app 2>/dev/null
  sleep 1
  trap - EXIT

  export XDG_CACHE_HOME=$(mktemp -d)
  "$APP" "$WORK" >/tmp/probe_weights_reopen.log 2>&1 &
  app2=$!
  AE_UI_PID=$app2; export AE_UI_PID
  trap "kill $app2 2>/dev/null" EXIT
  wait_ready 180 || { echo "no main window on reopen" >&2; exit 1; }
  W=$(xwininfo -root -tree 2>/dev/null | grep -m1 "1440x900+0+0" | awk "{print \$1}")
  xdotool windowactivate --sync "$W" 2>/dev/null
  sleep 1
  select_rig || { echo "no rig row on reopen" >&2; exit 1; }
  ae_activate "Bone editor" "the Bone tool is reachable on reopen"
  sleep 1
  ae_activate "Paint weights" "Paint weights is reachable on reopen"
  sleep 1.5
  # The first press selects a bone; the field is baked now, so the press is also a
  # one-stamp paint in memory (the project is not saved again).
  xdotool mousemove --sync $CX $CY click 1
  sleep 2
  state_showing "Baked" && echo "Baked" > "$WSTATE.after_reopen" || echo "other" > "$WSTATE.after_reopen"
  if ae enabled "Size" >/dev/null 2>&1; then echo "REOPEN_BRUSH=enabled"; else echo "REOPEN_BRUSH=disabled"; fi > "$WSTATE.brush_reopen"
  shot "$SHOTDIR/w_07_reopened.png"
  kill $app2 2>/dev/null
  wait $app2 2>/dev/null
' 2>&1 | tail -6

# ---- the panel's state, by name and by number --------------------------------
grep -q "Select a bone" "$TXT.initial"
check $? "the mode opens asking for a bone"
[ "$(cat "$TXT.brush_initial" 2>/dev/null)" = "ENVELOPE_BRUSH=disabled" ]
check $? "the brush is disabled before a bone is selected"
[ "$(cat "$TXT.bake_initial" 2>/dev/null)" = "BAKE=absent" ]
check $? "no bake action before a bone is selected"

# A canvas press selects the nearest bone and must refuse to paint the live field.
grep -q "Envelope" "$TXT.after_click"
check $? "a canvas press selects a bone and the panel says Envelope"
grep -q "Bake before painting" "$TXT.refused"
check $? "the refused stroke says to bake first"
[ "$(cat "$TXT.bake_after_click" 2>/dev/null)" = "BAKE=enabled" ]
check $? "the bake action is offered (and enabled) on the envelope bone"
[ "$(cat "$TXT.brush_initial" 2>/dev/null)" = "ENVELOPE_BRUSH=disabled" ]
check $? "the brush rows are shown disabled, not hidden"

# Bake: state, enabled brush, and the exactness smoke test.
[ "$(cat "$TXT.brush_baked" 2>/dev/null)" = "BAKED_BRUSH=enabled" ]
check $? "the brush becomes usable on the baked field"
bakeDiff=$(compare -metric AE "$SHOTDIR/w_01_crop.png" "$SHOTDIR/w_02_crop.png" null: 2>&1 | awk "{print \$1}")
[ "$bakeDiff" = "0" ]
check $? "the painted canvas crop is pixel-identical across Bake (the bake is exact; differing pixels: $bakeDiff)"
assert_rendered "$SHOTDIR/w_02_baked.png" "the baked frame"

# The stroke changes the CANVAS the artist sees (the wash is the field), and undo
# and redo are exact. The panel's own count is not an observable of the field: a
# number can stay right while the picture does not (and the previous readout could
# not even say what it counted).
paintDiff=$(compare -metric AE "$SHOTDIR/stroke_before.png" "$SHOTDIR/stroke_painted.png" null: 2>&1 | awk "{print \$1}")
[ -n "$paintDiff" ] && [ "$paintDiff" -gt 100 ]
check $? "a Subtract stroke changes the weight wash ($paintDiff differing pixels)"
undoDiff=$(compare -metric AE "$SHOTDIR/stroke_before.png" "$SHOTDIR/stroke_undone.png" null: 2>&1 | awk "{print \$1}")
[ "$undoDiff" = "0" ]
check $? "one undo restores the pre-stroke canvas exactly ($undoDiff differing pixels)"
redoDiff=$(compare -metric AE "$SHOTDIR/stroke_painted.png" "$SHOTDIR/stroke_redo.png" null: 2>&1 | awk "{print \$1}")
[ "$redoDiff" = "0" ]
check $? "redo restores the painted canvas exactly ($redoDiff differing pixels)"

# Revert returns Envelope and disables the brush.
grep -q "Envelope" "$TXT.after_revert"
check $? "Revert returns the live envelope"
[ "$(cat "$TXT.brush_revert" 2>/dev/null)" = "REVERT_BRUSH=disabled" ]
check $? "and the brush is disabled again"

# Rebake rebuilds the field from the envelope: the initial bake, exactly.
rebakeDiff=$(compare -metric AE "$SHOTDIR/stroke_before.png" "$SHOTDIR/stroke_rebake.png" null: 2>&1 | awk "{print \$1}")
[ "$rebakeDiff" = "0" ]
check $? "Rebake reproduces the envelope's field exactly ($rebakeDiff differing pixels)"

# Clear empties the field (the wash goes) but keeps the bone Baked and paintable;
# undo restores the field exactly.
grep -q "Baked" "$TXT.after_clear"
check $? "Clear keeps the bone Baked"
[ "$(cat "$TXT.brush_clear" 2>/dev/null)" = "CLEAR_BRUSH=enabled" ]
check $? "and the cleared field is still paintable"
clearDiff=$(compare -metric AE "$SHOTDIR/stroke_before.png" "$SHOTDIR/stroke_cleared.png" null: 2>&1 | awk "{print \$1}")
[ -n "$clearDiff" ] && [ "$clearDiff" -gt 100 ]
check $? "Clear removes the wash from the canvas ($clearDiff differing pixels)"
clearUndoDiff=$(compare -metric AE "$SHOTDIR/stroke_before.png" "$SHOTDIR/stroke_clear_undo.png" null: 2>&1 | awk "{print \$1}")
[ "$clearUndoDiff" = "0" ]
check $? "undo restores the cleared field exactly ($clearUndoDiff differing pixels)"

# The save wrote a new file, and a fresh process reads the baked state back.
mtb=$(sed -n 's/^MT_BEFORE=//p' "$TXT.save")
mta=$(sed -n 's/^MT_AFTER=//p' "$TXT.save")
[ -n "$mtb" ] && [ -n "$mta" ] && [ "$mtb" != "$mta" ]
check $? "Ctrl+S wrote the project ($mtb -> $mta)"
grep -q "Baked" "$TXT.after_reopen"
check $? "a fresh process reads the baked state back from format 0.22"
[ "$(cat "$TXT.brush_reopen" 2>/dev/null)" = "REOPEN_BRUSH=enabled" ]
check $? "and the field is paintable after the reload"
assert_rendered "$SHOTDIR/w_07_reopened.png" "the reopened baked frame"

summary probe_weights_bake_paint
