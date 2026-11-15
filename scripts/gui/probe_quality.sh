#!/bin/bash
# See _lib.sh: these drive the real app under Xvfb because the wiring they check
# is not reachable from a unit test.
#
# This probe is about the QUALITY INTENT and the codec s own value, which are two
# controls for one number: the intent ("Balanced") is the codec knob seen from
# further away, and the spin box in Advanced is that same knob named as a number.
# The invariant is one number, one control in charge, and it now holds in BOTH
# directions: with the codec s own box on Auto the Quality intent is operable, and
# naming a number there takes the intent out of charge until the box own Set to
# auto button hands it back.
#
# THIS PROBE RECORDED THE DEFECT THAT MADE THE FIRST HALF OF THAT IMPOSSIBLE, and
# the state it asserted is gone. The override spin was ranged to the codec s own
# span (VP9 CRF 24..52) while the "no override" sentinel was the fixed value -1,
# and QSpinBox::setRange CLAMPS the current value: the sentinel was erased the
# moment the range was applied, the box always held a real number, the Quality
# intent was disabled as "not in charge", and the export wrote that number over
# the intent the user had chosen. The sentinel is now the value ONE BELOW the
# codec span (ctrl::QualityControl::autoValue(): 23 for VP9, drawn as "Auto"), so
# it survives the range, and the in-charge question is asked after the range is
# applied. An earlier version of this probe therefore asserted the rule in one
# direction only (a number stands, so the intent is inert); what is asserted here
# is the round trip: Auto -> intent in charge, number -> intent out of charge, and
# the box own button -> Auto and the intent in charge again.
#
# Everything here addresses widgets BY NAME, and the state of the two controls
# (which one is operable) comes from the accessibility tree, never from a pixel.
#
# The BODY only navigates and leaves evidence. The assertions run out here.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/_lib.sh" "$@"
# The revision of the export flow this probe's navigation was recorded against. See
# AE_FLOW_REVISION in _lib.sh: the guard refuses to run a probe that predates it.
export RECORDED_AGAINST=2

rm -f /tmp/qual_form.txt /tmp/qual_advanced.txt /tmp/qual_collapsed_state.txt \
      /tmp/qual_auto_state.txt /tmp/qual_named_state.txt /tmp/qual_back_state.txt \
      /tmp/qual_auto_value.txt /tmp/qual_named_value.txt /tmp/qual_back_value.txt

xvfb-run -a --server-args="-screen 0 1440x900x24" bash -c '
  '"$APP"' "'"$PROJ"'" >/tmp/probe_qual.log 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID   # the window the tooling must address
  wait_ready 120 || sleep 12
  W=$(xdotool search --name "AnimeEffects" | tail -1)
  xdotool windowactivate --sync $W 2>/dev/null

  # The two controls, and WHICH OF THEM IS OPERABLE, plus the span the codec s own
  # box is ranged to. `report` answers showing/hidden/absent and nothing else, and
  # `enabled` cannot be asked about a NAMED widget: it returns the first ENABLED
  # match in name-tier order, so `enabled "Quality"` accepts the enabled "Quality
  # override" (a prefix match) as an answer and would call the intent operable when
  # it is not. That is a limitation of that command, not something to work around
  # quietly here: the exact node each question is about is asked for by name
  # through the same lookup the shared helpers use (ae_ui.find), and the state and
  # the span come from the accessibility tree. The number itself is read with the
  # shared `ae value`, which answers for a spin box and for a combo box alike.
  quality_state() {  # $1 = the file to write
    python3 - "$1" <<PY 2>/dev/null
import sys, warnings
warnings.filterwarnings("ignore")
sys.path.insert(0, "$ROOT/scripts/gui")
import ae_ui
from gi.repository import Atspi

def number(value):
    return int(value) if float(value).is_integer() else value

out = open(sys.argv[1], "w")
for name, role in [("Quality", "combo box"), ("Quality override", "spin button")]:
    # By ROLE: the redesigned form exposes its row labels as text too, and a
    # plain name match takes the label (enabled, no value) over the control -
    # which is how this reader reported enabled=True for a disabled combo.
    node = ae_ui.find(name, role=role, timeout=6.0)
    if node is None:
        out.write("%s: absent\n" % name)
        continue
    enabled = node.get_state_set().contains(Atspi.StateType.ENABLED)
    value = minimum = maximum = None
    iface = node.get_value_iface()
    if iface is not None:
        value = number(iface.get_current_value())
        minimum = number(iface.get_minimum_value())
        maximum = number(iface.get_maximum_value())
    out.write("%s: enabled=%s value=%s min=%s max=%s\n"
              % (name, enabled, value, minimum, maximum))
out.close()
PY
  }

  # 1. A destination first, then the form it dictates. Telegram sticker resolves
  #    its codec (libvpx-vp9, CRF 24..52 in the table) and its quality from the
  #    data, so this is the form in which both controls exist.
  ae_press "File" "the File menu opens"
  ae_press "Export project as" "the export entry is there"
  require_export_flow
  ae set "Search destinations" "telegram sticker" >/dev/null
  sleep 1.2
  ae_activate "Use this destination" "the Telegram sticker destination is chosen"
  ae wait "Advanced" >/dev/null 2>&1 || true
  sleep 1.5
  ae_report /tmp/qual_form.txt "Destination" "Quality" "Advanced" "Quality override" "Width"
  quality_state /tmp/qual_collapsed_state.txt
  xdotool mousemove 700 600; sleep 0.5
  import -window root '"$SHOTS"'/qual_01_destination.png

  # 2. Advanced is where the codec s own value lives, and it opens on Auto.
  ae_activate "Advanced" "Advanced opens on the codec s own value"
  sleep 1.5
  ae_report /tmp/qual_advanced.txt "Advanced" "Quality" "Quality override" "Reset to auto"
  ae value "Quality override" > /tmp/qual_auto_value.txt 2>/dev/null
  quality_state /tmp/qual_auto_state.txt
  xdotool mousemove 700 600; sleep 0.5
  import -window root '"$SHOTS"'/qual_02_advanced.png

  # 3. The user names the number themselves, and the intent stops being in charge.
  ae set "Quality override" 40
  sleep 1.5
  ae value "Quality override" > /tmp/qual_named_value.txt 2>/dev/null
  quality_state /tmp/qual_named_state.txt
  xdotool mousemove 700 600; sleep 0.5
  import -window root '"$SHOTS"'/qual_03_named.png

  # 4. The box own Set to auto button hands the intent back.
  ae_activate "Reset to auto" "the box s own Set to auto button is pressed"
  sleep 1.5
  ae value "Quality override" > /tmp/qual_back_value.txt 2>/dev/null
  quality_state /tmp/qual_back_state.txt
  xdotool mousemove 700 600; sleep 0.5
  import -window root '"$SHOTS"'/qual_04_auto_again.png
  ae_activate "Cancel" "the form closes on Cancel"
  sleep 1
  kill $app 2>/dev/null; wait $app 2>/dev/null
' 2>&1 | tail -4

# One field of one line of a state file, as a bare number ("value=23" -> 23).
state_field() { # state_field <file> <widget> <field>
    grep "^$2: " "$1" 2>/dev/null | sed -n "s/.*[ =]$3=\([-0-9]*\).*/\1/p" | head -1
}
value_of() { tr -d '\n' < "$1" 2>/dev/null; }

# ---- with a destination chosen the intent is offered AND operable -------------
assert_showing /tmp/qual_form.txt "Quality" "with a destination chosen the Quality intent is offered"
if grep -q "Quality: enabled=True" /tmp/qual_collapsed_state.txt; then
    check 0 "and it is operable, not disabled by a control nobody has touched (Advanced is still collapsed)"
else
    check 1 "and it is operable, not disabled by a control nobody has touched (got: $(tr '\n' ' ' < /tmp/qual_collapsed_state.txt 2>/dev/null))"
fi
assert_hidden /tmp/qual_form.txt "Quality override" "the codec s own value stays inside the collapsed Advanced section"
assert_rendered "$SHOTS/qual_01_destination.png" "the form carrying the destination s quality intent"

# ---- Advanced opens on Auto, and Auto is the codec s own sentinel -------------
assert_showing /tmp/qual_advanced.txt "Quality override" "and opening Advanced offers the codec s own value"
assert_showing /tmp/qual_advanced.txt "Reset to auto" "together with the box s own Set to auto button"
qoValue=$(state_field /tmp/qual_auto_state.txt "Quality override" value)
qoMin=$(state_field /tmp/qual_auto_state.txt "Quality override" min)
qoMax=$(state_field /tmp/qual_auto_state.txt "Quality override" max)
if [ -n "$qoValue" ] && [ "$qoValue" = "$qoMin" ] && [ "$qoMin" = "23" ] && [ "$qoMax" = "52" ]; then
    check 0 "the box opens on Auto: its value 23 IS its minimum, one below the codec s best (VP9 CRF 24..52, so the box is ranged 23..52)"
else
    check 1 "the box opens on Auto: its value should equal its minimum and be one below the codec s best (VP9 CRF 24..52) (got: value=$qoValue min=$qoMin max=$qoMax)"
fi
if [ "$(value_of /tmp/qual_auto_value.txt)" = "23" ]; then
    check 0 "and the shared value read agrees about that number (23)"
else
    check 1 "and the shared value read agrees about that number (got: $(value_of /tmp/qual_auto_value.txt))"
fi
if grep -q "Quality: enabled=True" /tmp/qual_auto_state.txt; then
    check 0 "and the intent stays in charge while that box is on Auto"
else
    check 1 "and the intent stays in charge while that box is on Auto (got: $(tr '\n' ' ' < /tmp/qual_auto_state.txt 2>/dev/null))"
fi
assert_rendered "$SHOTS/qual_02_advanced.png" "and its Advanced section, with the codec s own value on Auto"

# ---- naming a number takes the intent out of charge --------------------------
if [ "$(value_of /tmp/qual_named_value.txt)" = "40" ]; then
    check 0 "the number the user names reaches the codec s own value (40)"
else
    check 1 "the number the user names reaches the codec s own value (got: $(value_of /tmp/qual_named_value.txt))"
fi
if grep -q "Quality: enabled=False" /tmp/qual_named_state.txt; then
    check 0 "and naming it takes the Quality intent out of charge (one number, one control in charge)"
else
    check 1 "and naming it takes the Quality intent out of charge (got: $(tr '\n' ' ' < /tmp/qual_named_state.txt 2>/dev/null))"
fi
assert_rendered "$SHOTS/qual_03_named.png" "the form after the number was named"

# ---- and the box own button hands the intent back ----------------------------
backValue=$(state_field /tmp/qual_back_state.txt "Quality override" value)
backMin=$(state_field /tmp/qual_back_state.txt "Quality override" min)
if [ -n "$backValue" ] && [ "$backValue" = "$backMin" ] && [ "$(value_of /tmp/qual_back_value.txt)" = "$backValue" ]; then
    check 0 "the box s own Set to auto button puts the codec s own value back on Auto ($backValue = its minimum)"
else
    check 1 "the box s own Set to auto button puts the codec s own value back on Auto (got: value=$backValue min=$backMin, read $(value_of /tmp/qual_back_value.txt))"
fi
if grep -q "Quality: enabled=True" /tmp/qual_back_state.txt; then
    check 0 "and the Quality intent is in charge again"
else
    check 1 "and the Quality intent is in charge again (got: $(tr '\n' ' ' < /tmp/qual_back_state.txt 2>/dev/null))"
fi
assert_rendered "$SHOTS/qual_04_auto_again.png" "the form once Auto was handed back"

summary probe_quality
