#!/usr/bin/env python3
"""Address the application's widgets by NAME, through the accessibility bus.

Why this exists. The GUI probes drive the real application under Xvfb to check the
parts no unit test can reach (the export dialog's wiring, the file dialog, whether
an export lands inside a platform's cap). They used to do it by PIXEL COORDINATES:
"click 462,458 for the Format row". That is wrong twice over.

  * It silently tests the WRONG THING after any layout change. The probes assert
    that a screenshot is not black, so a click that misses lands on another widget
    and the probe still passes. Restructuring the export form moved the Format row
    from y=458 to y=389 and every one of the ten probes kept passing while driving
    something else.
  * It makes a layout change cost a re-recording of every probe, which is exactly
    the cost that stops anyone from fixing a form.

A widget that a screen reader cannot name is a widget this cannot name either, so
the two problems have one fix: the dialog sets accessibleName() on the controls a
user has to reach (see exportdiag.h), and these helpers find them by that name.

Usage (see any probe_*.sh):
  ae_ui.py dump                       tree of every window, for debugging
  ae_ui.py wait   <name>              exit 0 once the widget exists
  ae_ui.py click  <name>              press a button / click a check box
  ae_ui.py combo  <name> <item text>  pick an item in a combo box by its text
  ae_ui.py set    <name> <value>      write a spin box / line edit
  ae_ui.py value  <name>              print a spin box / line edit's value
  ae_ui.py label  <name>              print a label's text
  ae_ui.py labeled <name>            the text of the label that names this widget
  ae_ui.py text                       print every label, in tree order

Every command exits non-zero with a message when the widget is not there, so a
probe fails loudly instead of clicking whatever happens to be at that pixel.
"""
import sys
import time
import warnings

import gi

# pyatspi still marks get_action_name() deprecated; the noise buries the probe's
# own diagnostics, and the replacement is not available in every pyatspi we run on.
warnings.filterwarnings("ignore", category=DeprecationWarning)

gi.require_version("Atspi", "2.0")
from gi.repository import Atspi  # noqa: E402

TIMEOUT = float(10.0)


def _roots():
    """Every accessible application whose name is ours, newest last.

    There can be SEVERAL: the developer's own running instance is on the same bus
    as the one a probe launches under Xvfb, and an instance started without the
    accessibility bridge exposes an empty tree. Matching by name alone therefore
    risks driving the developer's window (or silently finding nothing). The probes
    export AE_UI_PID with the pid they launched, and that is the only reliable
    handle: accessibility gives us the application's process id.
    """
    desktop = Atspi.get_desktop(0)
    out = []
    for i in range(desktop.get_child_count()):
        try:
            app = desktop.get_child_at_index(i)
            if "AnimeEffects" in (app.get_name() or ""):
                out.append(app)
        except Exception:
            continue
    return out


def _app():
    import os

    want = os.environ.get("AE_UI_PID", "").strip()
    roots = _roots()
    if want:
        # One process registers SEVERAL roots, and only one of them carries the
        # tree (the others report zero children): pick the populated one, or every
        # query answers "no widget named ..." while the window is plainly there.
        best = None
        for app in roots:
            try:
                if str(app.get_process_id()) != want:
                    continue
                kids = app.get_child_count()
            except Exception:
                continue
            if best is None or kids > best[0]:
                best = (kids, app)
        return best[1] if best is not None else None
    # No pid given: the last registered instance is the one just started, and any
    # instance without a live tree cannot answer a query anyway. This is a GUESS,
    # and it was a silent one: no probe set AE_UI_PID (the contract this file
    # documented was never implemented), so with any other AnimeEffects on the bus -
    # a developer's own window, or one left running for a manual look - every probe
    # drove that instance instead, where "Export project as" is disabled because no
    # project is open, and reported the flow as stale. Say so rather than guess
    # quietly: the caller can still proceed, but the log names the window it used.
    if len(roots) > 1:
        import sys

        print(
            "ae_ui: AE_UI_PID is not set and %d AnimeEffects instances are on the bus; "
            "using pid %s. A probe must export AE_UI_PID with the process it launched."
            % (len(roots), _pid_of(roots[-1])),
            file=sys.stderr,
        )
    for app in reversed(roots):
        try:
            if app.get_child_count() > 0:
                return app
        except Exception:
            continue
    return roots[-1] if roots else None


def _pid_of(aApp):
    try:
        return aApp.get_process_id()
    except Exception:
        return "?"


def _walk(node, depth=0, maxdepth=14):
    """Every descendant, as (depth, role, name, node). Qt exposes the tree lazily,
    so a node that raises is skipped rather than fatal."""
    if depth > maxdepth:
        return
    try:
        count = node.get_child_count()
    except Exception:
        return
    for i in range(count):
        try:
            child = node.get_child_at_index(i)
            yield (depth, child.get_role_name(), child.get_name() or "", child)
        except Exception:
            continue
        yield from _walk(child, depth + 1, maxdepth)


def names_of(node):
    """What this node answers to: its name, and its description.

    A QComboBox reports its current VALUE as the name (Qt's rule, good for a screen
    reader: "WebM") and ignores accessibleName, so the dialog puts the control's
    identity in the description as well and this matches either.
    """
    out = []
    try:
        out.append(node.get_name() or "")
    except Exception:
        pass
    try:
        desc = node.get_description()
        if desc:
            out.append(desc)
    except Exception:
        pass
    return [n for n in out if n]


def showing(node):
    """Whether the widget is actually on screen, ancestors included.

    The accessibility tree keeps HIDDEN widgets: every control the export form
    hides for the selected artifact is still in the tree, so "the widget exists"
    proves nothing about what the user can see - a probe asserting presence would
    pass while the form showed the wrong thing.

    The node's own SHOWING state is not enough either: a control inside a COLLAPSED
    section still reports itself as showing, which is how "Loop animation" read as
    visible while its section was closed and its toggle (buggily) hidden. So every
    ancestor has to be showing too - WITHIN THE APPLICATION, and no further.

    Walking above the application is what made every probe report STALE. The chain
    is button < panel < dialog < application < desktop frame, and under Xvfb there
    is no window manager, so the DESKTOP frame reports showing=False while the
    dialog and the application report True (measured, with the picker plainly on
    screen). Every widget in the process therefore read as hidden, `wait` for any
    name timed out, and require_export_flow called ten working probes stale - a
    verdict that hid the real state instead of reporting it, which is the one thing
    a stale marker must never do. The desktop is the session's business, not the
    window's.
    """
    seen = 0
    while node is not None and seen < 32:
        try:
            if not node.get_state_set().contains(Atspi.StateType.SHOWING):
                return False
        except Exception:
            return False
        try:
            role = node.get_role_name()
        except Exception:
            # A node whose interface is gone is not a widget on screen. This used to
            # return True, so a STALE node (the widgets of a dialog that has since been
            # destroyed are still reachable through the tree for a while) answered
            # "showing" for a control that no longer exists - which is how an audit run
            # reported "Loop animation: showing" for an MP4 export in a collapsed
            # Advanced section, twice over.
            return False
        if role == "application":
            return True
        try:
            node = node.get_parent()
        except Exception:
            return False
        seen += 1
    return True


def match_tier(node, name):
    """How well this node's name matches: 0 exact, 1 starts with, 2 contains, None no.

    The tier exists because a control and the LABEL that names it both contain the
    words: the spin box "First frame" is preceded by the label "- First frame -", and
    "Width" is a substring of the label "Width: ". Taking the first substring match in
    tree order picks the label, which has no Value interface and no editable text, so
    `ae set Width 160` reported "no widget named" or wrote nothing while the control
    was on screen the whole time. An exact name wins over a longer name that contains
    it, always.
    """
    for candidate in names_of(node):
        if candidate == name:
            return 0
    for candidate in names_of(node):
        if candidate.startswith(name):
            return 1
    for candidate in names_of(node):
        if name in candidate:
            return 2
    return None


def candidates(name, role=None, visible=True):
    """Every node that answers to the name, best match first."""
    app = _app()
    if app is None:
        return []
    out = []
    for depth, r, _n, node in _walk(app):
        if role is not None and r != role:
            continue
        if visible and not showing(node):
            continue
        tier = match_tier(node, name)
        if tier is not None:
            out.append((tier, depth, node))
    out.sort(key=lambda item: (item[0], item[1]))
    return [node for _tier, _depth, node in out]


def find(name, role=None, timeout=TIMEOUT, visible=True):
    """The best match for the name, optionally restricted by role. Retries, because
    the dialog under test is built in response to a click.

    The FIRST candidate that satisfies the caller is not this function's job: a caller
    that needs an interface (a Value to set, editable text to read) asks for the list
    with candidates() and takes the first node that has one. Picking the best name
    match here and hoping it can also do the thing is what made `ae set` silently do
    nothing."""
    deadline = time.time() + timeout
    while True:
        found = candidates(name, role=role, visible=visible)
        if found:
            return found[0]
        if time.time() >= deadline:
            return None
        time.sleep(0.25)


def find_capable(name, timeout=TIMEOUT, want="value"):
    """The best match that can actually be read or written.

    want="value": a node with a Value interface (a spin box, a slider) or editable
    text. QSpinBox answers EditableText with an empty string in this Qt build while
    its Value interface holds the number, so both are asked for and the node that
    answers is the one returned.
    """
    deadline = time.time() + timeout
    while True:
        for node in candidates(name):
            if want == "value" and (node.get_value_iface() is not None
                                    or node.get_editable_text_iface() is not None):
                return node
            if want == "enabled" and _enabled(node):
                return node
        if time.time() >= deadline:
            return None
        time.sleep(0.25)


def _enabled(node):
    """Whether the widget is usable, which is a different question from whether it is
    on screen: a disabled menu entry is plainly visible and does nothing. Qt states it
    through the ENABLED and SENSITIVE states."""
    try:
        states = node.get_state_set()
    except Exception:
        return False
    for name in ("ENABLED", "SENSITIVE"):
        try:
            if not states.contains(getattr(Atspi.StateType, name)):
                return False
        except Exception:
            continue
    return True


def do(node, action="click"):
    """Run a named action, or the first one the widget offers.

    The action is looked up BY NAME, and the names are tried in order, because a
    QML control's action list is not one entry: a spin button can expose Qt's own
    role actions ("increase"/"decrease") beside the item's "press", and the old
    "first action wins" rule then selected "increase" - `ae_activate "Width"` bumped
    the value by one instead of entering edit mode, which is a silent, plausible
    wrong answer. "press" is the name the Accessible.onPressAction handlers use.
    """
    iface = node.get_action_iface()
    if iface is None:
        return False
    names = [iface.get_action_name(i) or "" for i in range(iface.get_n_actions())]

    def run(index):
        try:
            return bool(iface.do_action(index))
        except Exception as exc:
            # "Did not receive a reply" is what an action that OPENS A MODAL DIALOG
            # looks like: the handler runs, the dialog starts its own event loop, and
            # the D-Bus reply never comes. The click landed. Callers must assert the
            # effect (ae_ui.py wait <the widget that should now exist>), which is what
            # makes this safe to swallow rather than a silent failure.
            print(f"note: {action} on the widget did not reply ({exc}); "
                  f"the caller must verify the effect", file=sys.stderr)
            return True

    for want in (action, "press", "click", "activate"):
        for i, n in enumerate(names):
            if n.lower() == want:
                return run(i)
    # No named match: only a generic click may fall back to whatever is first, and
    # only when the widget offers something at all.
    if action == "click" and names:
        return run(0)
    return False


def children(node):
    out = []
    try:
        for i in range(node.get_child_count()):
            out.append(node.get_child_at_index(i))
    except Exception:
        pass
    return out


def cmd_dump():
    app = _app()
    if app is None:
        print("no AnimeEffects window on the accessibility bus", file=sys.stderr)
        return 2
    for depth, role, name, node in _walk(app):
        if role in ("list item", "menu item") and depth > 8:
            continue
        print(f"{'  ' * depth}{role:<14} | {name[:70]}")
    return 0


def cmd_wait(name):
    node = find(name)
    if node is None:
        print(f"no widget named {name!r}", file=sys.stderr)
        return 1
    return 0


PRESS_ACTIONS = ("press", "click", "activate")
# The one action that means "this node cannot be clicked": Qt hands it to every
# static text in the Qt Quick form.
FOCUS_ONLY = "setfocus"


def action_names(node):
    """The action names the node offers, lowercased."""
    iface = node.get_action_iface()
    if iface is None:
        return []
    try:
        return [(iface.get_action_name(i) or "").lower()
                for i in range(iface.get_n_actions())]
    except Exception:
        return []


def cmd_click(name, role=None):
    # Prefer a node that can actually be PRESSED, which is not the same question as
    # "does this node have an Action interface": Qt gives every static text in the
    # Qt Quick form an interface whose only entry is SetFocus, and a labeled field is
    # addressed by the LABEL's name because that is the name a user reads. Taking the
    # first node with an interface therefore pressed the label of the Format row and
    # ran SetFocus on it - exit status 0, no popup, and a probe left reporting that
    # the popup's items were missing.
    #
    # A name can match more than one node now that the Qt Quick form exposes its own
    # text (a facts row states "WebM" and the open Format popup offers a "WebM"
    # option), so both passes below walk the matches best-name-first and take the
    # first one that really answers.
    #
    # `role` is required for a combo OPTION: a Qt Quick combo reports its current
    # value as its own accessible NAME, so while the popup lists "WebM" the closed
    # field is ALSO named "WebM" - and it comes first, so a plain press toggles the
    # popup shut instead of choosing the item. A caller that means the item says
    # role="list item".
    found = candidates(name, role=role)
    node = None
    for candidate in found:  # a node that offers a press
        if _enabled(candidate) and any(n in PRESS_ACTIONS for n in action_names(candidate)):
            node = candidate
            break
    if node is None:
        for candidate in found:  # else any node with a non-focus action
            if _enabled(candidate) and any(n != FOCUS_ONLY
                                           for n in action_names(candidate)):
                node = candidate
                break
    if node is None:
        if not found:
            what = role if role is not None else "widget"
            print(f"no {what} named {name!r} to click", file=sys.stderr)
        else:
            print(f"{name!r} offers nothing to click (only "
                  f"{', '.join(action_names(found[0])) or 'no actions'})", file=sys.stderr)
        return 1
    if not do(node):
        print(f"{name!r} has no click action", file=sys.stderr)
        return 1
    return 0


def cmd_combo(name, item):
    combo = find(name, role="combo box")
    if combo is None:
        print(f"no combo box named {name!r}", file=sys.stderr)
        return 1
    if not do(combo):
        print(f"{name!r} could not be opened", file=sys.stderr)
        return 1
    # The popup is a child of the combo box in Qt's accessibility tree once open.
    entry = None
    for node in children(combo) + [combo]:
        for _, role, text, child in _walk(node, maxdepth=4):
            if role in ("menu item", "list item", "table cell") and item.lower() in text.lower():
                entry = child
                break
        if entry is not None:
            break
    if entry is None:
        # Not fatal for the caller's intent: the popup may expose the rows only
        # through the combo's own selection interface. Fall back to the current
        # text, and let the caller's assertion catch a wrong selection.
        current = combo.get_name()
        if item.lower() in current.lower():
            return 0
        print(f"{name!r} has no item containing {item!r}", file=sys.stderr)
        return 1
    return 0 if do(entry) else 1


def cmd_set(name, value):
    node = find_capable(name)
    if node is None:
        print(f"no widget named {name!r} that holds a value", file=sys.stderr)
        return 1
    # Value FIRST, not EditableText: a QSpinBox here answers EditableText with "" and
    # accepts set_text_contents() without changing the number, so writing through the
    # text interface looks successful and changes nothing. The Value interface is the
    # number itself.
    value_iface = node.get_value_iface()
    if value_iface is not None:
        try:
            value_iface.set_current_value(float(value))
            return 0
        except Exception as exc:
            print(f"cannot set {name!r} through its Value interface: {exc}", file=sys.stderr)
            return 1
    editable = node.get_editable_text_iface()
    if editable is not None:
        editable.set_text_contents(value)
        return 0
    print(f"{name!r} is not editable", file=sys.stderr)
    return 1


def cmd_value(name):
    node = find_capable(name)
    if node is not None:
        value_iface = node.get_value_iface()
        if value_iface is not None:
            current = value_iface.get_current_value()
            # A spin box holds a number; printing it without a trailing ".0" makes the
            # caller's comparison readable ("17", not "17.0").
            print(int(current) if float(current).is_integer() else current)
            return 0
        editable = node.get_editable_text_iface()
        if editable is not None:
            print(editable.get_text(0, -1))
            return 0
    # The hand-rolled Qt Quick controls expose NO Value interface (Qt Quick's
    # Accessible attached property has no `value`), so their state is carried by the
    # accessible name and description instead. Read it by ROLE, because the two roles
    # carry it in opposite places:
    #   * a combo box reports its current VALUE as its NAME and its identity as the
    #     DESCRIPTION (Qt's rule, good for a screen reader);
    #   * a spin button reports its identity as the NAME and its current VALUE as the
    #     DESCRIPTION (AeNumberField.qml), so `ae value "Width"` still answers here.
    # Without the role split, `ae value "Format"` found the static label beside the
    # combo (the row is "Format" in both places) and reported the row's TITLE as the
    # control's value - which is how probe_axes "read" the word "Format" from a row
    # that was showing "WebM".
    combo = find(name, role="combo box")
    if combo is not None:
        print(combo.get_name())
        return 0
    fallback = find(name, role="spin button")
    if fallback is None:
        fallback = find(name)
    if fallback is None:
        print(f"no widget named {name!r}", file=sys.stderr)
        return 1
    try:
        described = fallback.get_description() or ""
    except Exception:
        described = ""
    print(described if described else fallback.get_name())
    return 0


def cmd_enabled(name):
    """Exit 0 when the widget is enabled, 1 when it is disabled or absent, printing
    which. The state a menu entry is in is not visible in a screenshot of an
    accessibility dump entry: it has to be asked for."""
    node = find_capable(name, want="enabled")
    if node is None:
        print(f"{name!r} is disabled or not on screen", file=sys.stderr)
        return 1
    print(f"{name} is enabled")
    return 0


def cmd_label(name):
    node = find(name)
    if node is None:
        print(f"no widget named {name!r}", file=sys.stderr)
        return 1
    print(node.get_name())
    return 0


def cmd_labeled(name):
    """The text of the label that names this widget, or nothing when none does.

    An accessible name is what a control answers to; the LABELLED_BY relation is what
    says which words on screen belong to it. They are set by different code - the
    dialog's setAccessibleName() and its setBuddy() - so a control can be addressable
    and still have nothing on the form that reads as its label. That is what a screen
    reader announces before it reads the control, so it is worth asserting rather than
    assuming. Exit 1 when no label names it.
    """
    node = find(name)
    if node is None:
        print(f"no widget named {name!r}", file=sys.stderr)
        return 1
    try:
        relations = node.get_relation_set()
    except Exception as exc:
        print(f"no relations on {name!r} ({exc})", file=sys.stderr)
        return 1
    for relation in relations:
        try:
            if relation.get_relation_type() != Atspi.RelationType.LABELLED_BY:
                continue
            for i in range(relation.get_n_targets()):
                target = relation.get_target(i)
                text = (target.get_name() or "").strip()
                if text:
                    print(text)
                    return 0
        except Exception:
            continue
    print(f"{name!r} has no label of its own", file=sys.stderr)
    return 1


def cmd_extents(name, role=None):
    """The widget's rectangle ON SCREEN, as `x y w h`.

    Input is still synthesized with xdotool (mailbox-free, and the only way to send
    keystrokes), but the POSITION comes from the widget itself, so it survives a
    layout change: that is the whole difference from a hardcoded coordinate, which
    goes on clicking whatever has since moved underneath it.

    `role` disambiguates a control from the LABEL beside it: the redesigned export
    form gives the field and its muted label the same words ("Frame rate"), and the
    label is the text-only node, so a caller that means the CONTROL says so
    (role="spin button").

    THE WINDOW OFFSET, and why it is added here. Qt reports a QML item's
    Atspi.CoordType.SCREEN rectangle in the coordinates of its QQuickWidget, not of
    the window that contains it: the picker's "Cancel" reported x=333..405 while the
    dialog sat at x=61, so every coordinate click landed beside its target. The tell
    is that SCREEN and WINDOW agree exactly, which a WIDGET's rectangle cannot do
    unless the window is itself at the screen origin (where the correction is zero
    anyway). So when they agree, the top-level frame's own screen origin is added -
    and the frame is a widget, so ITS rectangle is correct.
    """
    node = find(name, role=role)
    if node is None:
        print(f"no widget named {name!r}", file=sys.stderr)
        return 1
    component = node.get_component_iface()
    if component is None:
        print(f"{name!r} has no geometry", file=sys.stderr)
        return 1
    try:
        rect = component.get_extents(Atspi.CoordType.SCREEN)
    except Exception as exc:
        print(f"cannot read {name!r}'s geometry: {exc}", file=sys.stderr)
        return 1
    try:
        win = component.get_extents(Atspi.CoordType.WINDOW)
        if win.x == rect.x and win.y == rect.y:
            top = node
            for _ in range(32):
                if top is None:
                    break
                try:
                    role_name = top.get_role_name()
                except Exception:
                    break
                if role_name in ("dialog", "frame"):
                    tcomp = top.get_component_iface()
                    if tcomp is not None:
                        trect = tcomp.get_extents(Atspi.CoordType.SCREEN)
                        rect.x += trect.x
                        rect.y += trect.y
                    break
                try:
                    top = top.get_parent()
                except Exception:
                    break
    except Exception:
        pass
    print(f"{rect.x} {rect.y} {rect.width} {rect.height}")
    return 0


def cmd_report(names):
    """One line per name: `present <name>` or `absent <name>`.

    Presence and absence are both assertions a probe wants: the export form HIDES
    what the selected artifact cannot use, so "there is no Bitrate control in front
    of me" is the feature. A single snapshot answers them all, and the shell side
    turns each line into a PASS/FAIL.
    """
    # An EMPTY tree is not evidence of absence: Qt builds the accessibility tree
    # lazily, and the first query after a window appears can come back with
    # nothing in it (measured: a report at 13s reported every widget absent, while
    # the same query a moment later found them all). Wait for a populated tree, so
    # "absent" means the widget is not there rather than "not there yet".
    wanted = set(names)
    deadline = time.time() + TIMEOUT
    visible = set()
    hidden = set()
    while True:
        app = _app()
        if app is None:
            print("no AnimeEffects window on the accessibility bus", file=sys.stderr)
            return 2
        visible, hidden = set(), set()
        for _, _role, _n, node in _walk(app):
            for n in names_of(node):
                (visible if showing(node) else hidden).add(n)
        # Wait for the names ASKED ABOUT, not merely for a populated tree: the main
        # window's tree is always populated, so "the tree is not empty" is satisfied
        # immediately and the answer would describe the window instead of the dialog
        # that is still coming up. A report whose names are all genuinely absent
        # takes the full timeout - ask for an anchor (the button that proves the
        # dialog is open) and it is immediate.
        if (visible & wanted) or (hidden & wanted) or time.time() >= deadline:
            break
        time.sleep(0.4)
    for name in names:
        if name in visible:
            print("showing " + name)
        elif name in hidden:
            print("hidden  " + name)
        else:
            print("absent  " + name)
    return 0


def cmd_absent(name, seconds="2"):
    deadline = time.time() + float(seconds)
    while time.time() < deadline:
        if find(name, timeout=0.3) is None:
            return 0
        time.sleep(0.2)
    print(f"{name!r} is present but should not be", file=sys.stderr)
    return 1


def cmd_text():
    app = _app()
    if app is None:
        return 2
    for _, role, name, _node in _walk(app):
        if role == "label" and name.strip():
            print(name)
    return 0


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd, rest = argv[1], argv[2:]
    table = {
        "dump": (cmd_dump, 0),
        "wait": (cmd_wait, 1),
        "click": (cmd_click, 1),
        "combo": (cmd_combo, 2),
        "set": (cmd_set, 2),
        "value": (cmd_value, 1),
        "label": (cmd_label, 1),
        "labeled": (cmd_labeled, 1),
        "text": (cmd_text, 0),
        "report": (cmd_report, None),
        "extents": (cmd_extents, None),
        "absent": (cmd_absent, None),
        "enabled": (cmd_enabled, 1),
    }
    if cmd not in table:
        print(f"unknown command {cmd!r}", file=sys.stderr)
        return 2
    fn, argc = table[cmd]
    if cmd == "report":
        return fn(rest)
    if cmd == "absent":
        return fn(*rest) if rest else 2
    if cmd == "extents":
        # extents <name> [role]
        return fn(*rest) if 1 <= len(rest) <= 2 else 2
    if cmd == "click":
        # click <name> [role]
        return fn(*rest) if 1 <= len(rest) <= 2 else 2
    if len(rest) != argc:
        print(f"{cmd} takes {argc} argument(s)", file=sys.stderr)
        return 2
    return fn(*rest)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
