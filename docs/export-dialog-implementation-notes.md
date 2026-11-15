# Implementation notes per contract line — search/list/scroll/tooltip cluster

Companion to `docs/export-dialog-behavior.md`. That file is the design; this one records what each
line actually costs, from the Qt 6.11 sources (file and line) and from measurement where reading
could not settle it. Verdicts are marked **[read]**, **[driven]** or **[open]**.

## Method — and the two instruments I first wrote off, both wrongly

- **Read:** the Qt 6.11 sources of the Basic style and the templates, cited inline.
- **Driven — keyboard, in-process:** key events injected into the QQuickWidget, which is how Qt's
  own autotests drive a scene. Isolates QML routing from the platform input path:

  ```cpp
  QKeyEvent press(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
  QCoreApplication::sendEvent(quickWidget, &press);   // reaches the scene's focus item
  ```

- **Driven — keyboard, at the OS level: it works.** My "not one key ever reached the application"
  claim was a scripting bug, not a limitation. Under Xvfb **with a window manager** the window does
  take focus — measured `focusWindow=yes active=1 activeFocusItem=QQuickTextInput` — and `xdotool`
  keys arrive (`sceneKeys=KEY 65 / KEY 66`). What actually broke it: `xdotool search --name "^fx$"`
  returned nothing, so focus went to the wrong window, and my click used guessed coordinates that
  fell outside the window, which *removed* focus. The recipe that works:

  ```bash
  xvfb-run -n 53 --server-args="-screen 0 1024x768x24" bash -c '
    openbox & sleep 2
    ./app & sleep 3
    W=$(xdotool getactivewindow)                     # or: search --class <name>
    eval $(xdotool getwindowgeometry --shell $W)     # X, Y, WIDTH, HEIGHT
    xdotool windowactivate --sync $W
    xdotool mousemove $((X + WIDTH/2)) $((Y + HEIGHT/2)) click 1
    xdotool key Down'
  ```

  Coordinate clicks need none of this, which is why the existing probes get away with `ae_press` and
  never noticed the gap. `ydotool` stays the tool for a real Wayland compositor, where Xvfb cannot
  stand in. Keyboard lines can therefore be verified with **real key events**, not only injection.

- **Qt and QML log output: not swallowed, redirected.** Same program, stderr redirected to a file:

  | environment | marks reaching stderr |
  | --- | --- |
  | none | 0 |
  | `QT_FORCE_STDERR_LOGGING=1` | 1 — `console.info` arrives, `console.log` still filtered |
  | `QT_LOGGING_RULES=*.debug=true` alone | 0 |

  and the journal holds what stderr lost (`journalctl --user`, `fx[…] INFO_MARK scene ready`). Two
  separate documented mechanisms: Qt's default handler writes to **journald** when stderr is not a
  console (`QT_FORCE_STDERR_LOGGING`; formerly `QT_LOGGING_TO_CONSOLE` /
  `QT_ASSUME_STDERR_HAS_CONSOLE`), and `console.log` maps to `qCDebug` in category `qml`, which the
  default rules filter (`*.debug=true` or `qml.debug=true`). **Consequence for this project: the
  probes launch the app with its output redirected to `/tmp/probe_app.log`, so every Qt warning and
  QML error has been going to the journal instead of the log — `scripts/gui/_lib.sh` now exports
  `QT_FORCE_STDERR_LOGGING=1`.**

## Measured: where a key pressed in a focused TextField actually goes

Scene: a `TextField` with focus, a sibling `ListView` with its own `Keys.onDownPressed`, and a
handler on the root `Item`. Injected `Down Down Up Right`:

```
FIELD DOWN (not accepted)      <- the focused control's own handler
ROOT DOWN -> index=1           <- then its ANCESTOR, because the field did not accept
FIELD DOWN (not accepted)
ROOT DOWN -> index=2
ROOT UP -> index=1
ROOT saw key=16777236 accepted=false
caret=1 text='a'               <- text insertion; the arrow never moved the caret
```

- The **sibling list's handler never fired once**. Qt Quick key propagation follows the **item
  hierarchy, not the visual list**: an unhandled key travels up the parent chain, never sideways.
- Therefore contract **B7** ("Down/Up move the LIST's highlighted row while the field has focus")
  cannot be met by putting a `Keys` handler on the list, however the list is implemented. It needs a
  handler on a common ancestor, or explicit forwarding from the field. **[driven]**
- Consequence for **B9** and **C16** (arrows must not expand/collapse while searching): the arrows
  *do* arrive; the behaviour that consumes them is our own key handling. Toolkit-neutral.

## Per line

| Line | Mechanism | Evidence | Verdict |
| --- | --- | --- | --- |
| **B12, F31** pointer is an I-beam over text | Stock `TextField` sets it itself, including the read-only nuance | `QQuickTextField` calls `setCursor(isReadOnly && !selectByMouse ? Qt::ArrowCursor : Qt::IBeamCursor)` (`qquicktextfield.cpp:262`) and `setCursor(Qt::IBeamCursor)` (`:364`) | **stock does it** [read]; our `ScrubSpinBox` sets `SizeHorCursor` for the whole field, which is the defect |
| **B13** selection tokens | Basic binds them to the palette | `selectionColor: control.palette.highlight`, `selectedTextColor: control.palette.highlightedText` (`Basic/TextField.qml`) | **stock, via palette or two overrides** [read] |
| **D23** scrollbar drags the content | Template implements press/move → `setPosition` | `qquickscrollbar.cpp:304-330` | **stock does it** [read]; the picker's bar is decorative |
| **D24** scrollbar look | Basic's bar is a `Rectangle` content item | `Basic/ScrollBar.qml` | **ours either way** — the design's tokens are a delegate, not a stock look |
| **C19** grouped list with headers | `ListView.section.property` / `section.delegate` | Qt Quick core, documented | **free** [read] |
| **G39** combo type-ahead | Template implements it | `QQuickComboBoxPrivate::keySearch` (`qquickcombobox.cpp:287,743`) | **stock has it** [read]; whether the design wants it is Q6 |
| **G37** combo Up/Down | Template moves the highlight | `setHighlightedIndex(highlightedIndex ± 1, …)` (`qquickcombobox.cpp:700,715`) | **stock does it** [read] |
| **E26** tooltip delay, never instant | `int delay = 0`, and the delay timer only starts `if (delay > 0)` | `qquicktooltip.cpp:111-130` | **the stock default is instant** — the line requires setting the delay explicitly, in any implementation [read] |
| **E27** tooltip placement | Basic positions it above its parent with `margins: 6`; the shared instance's position is the framework's | `Basic/ToolTip.qml`; `qquickpopuppositioner.cpp` | **mechanism exists; the design's placement must still be specified and driven** [open] |
| **E28, G38** never clipped by the dialog | `Popup.Window` creates a real popup window | Re-measured 2026-10-03 (weston 15.0.1, Qt 6.11.2): on xcb both hosts behave identically (byte-identical captures); on Wayland the **compositor backend** decides — weston's headless backend never maps it from a `QQuickWidget`, its `x11` backend does, and a `QQuickView` host maps it on both. See *The Wayland limitation is a backend limitation* | **closed, and the reopen condition is declined: the user ruled 2026-10-03 that the overhang look is not wanted; in-scene flip/shift satisfies E28, so the host question stays settled** [driven] |
| **C21** no wrong hover frame | State binding in our own delegate | — | **ours either way**; needs a driven reproduction first [open] |
| **C18, C20** indentation, keyboard selection | Our model and delegate | — | **ours either way** [read] |
| **B6, B10, B11** search behaviour | Our field's code | — | **ours either way** [read] |

## What this settles, and what it does not

### The principle, since this document keeps being asked the question

**Use the abstraction we already have unless a contract line says otherwise.** Stock Qt Quick
Controls is the default; hand-rolling one is justified per line, in writing, by the line it fails —
not by a preference for owning the pixels, and not by a constraint that is asserted instead of
measured. "Which toolkit" is a real question with a real answer, and the answer is a mix: five lines
of this cluster are met outright by stock code (see the table), the rest are the same work either
way, and where stock is close but not exact the delegate/template override is still less code than
a control from scratch. My own `AeToolTip.qml` is the counter-example to keep in mind: its comment
stated, as fact, a limitation I never tested ("a QQuickWidget cannot render a popup outside its own
bounds") — and the limitation is not true at all, a `Popup.Window` does draw outside the scene. The
correction that followed over-shot in the other direction ("works for Qt 6.8+"), which is its own
lesson: the tested claim is narrower and is stated, with its platform, in the 2026-10-03 correction
below.

### Settled

My earlier claim that a Controls-based implementation "is not less work" was wrong. Stock covers the
I-beam cursor, the scrollbar drag, combo type-ahead and keyboard highlight, list sections, and
palette-driven selection colours — five lines for free.

The lines the review actually hit — routing arrows from a focused field into a list, tooltip delay
and placement policy, expansion, indentation, hover paint, keyboard selection semantics — are
**neutral**: our code in either implementation, and no toolkit choice removes them. Measured, the
defect cluster sits in behaviour we wrote and never tested.

### Not settled (do not claim these)

1. Whether popups and tooltips can be made to work from the *existing QQuickWidget host* on Wayland.
   Needs a real compositor and real input (`ydotool`), not Xvfb. This is the one line that decides
   the host question.
2. Whether the popup positioner keeps an in-scene tooltip inside the window bounds. The source
   suggests it clamps; nobody has watched it.
3. The picker's specific section + expansion + focus interaction, in the design's own terms.

# Control inventory: which rung each element lands on

The order asked for was: Controls used directly first, then delegates, then templates, then
hand-rolled. This is that walk over the exporter's controls, done in that order, with each element
stopped at the first rung that meets the design. "Rung" is not a quality ranking — R3 is correct
where no Controls type fits — it is the order a type gets tried in.

### What "delegate" means here, because it is two different things

- **A style component.** A Controls type is a C++ object owning the state and the behaviour, plus
  QML objects that draw that state. The documentation calls those objects *delegates*:
  `background` (the surface), `contentItem` (what the control shows or edits), `indicator` (the mark
  — checkbox tick, combo caret), `handle` (a draggable part; `ScrollBar` has none, its
  `contentItem` *is* the handle), `popup` (a detached surface — the combo's list, the tooltip's
  bubble). The C++ object instantiates them, sizes itself from them, and pushes state into them
  (`down`, `hovered`, `pressed`, `visualFocus`, `palette`), which is why replacing them is the
  intended hook rather than restyling from outside.
- **A model-row renderer.** A control that has a model also has a `delegate` that renders **one
  row**: `ListView.delegate`, `ComboBox.delegate`. Same word, unrelated job — it renders data, not
  the control's own state.

`ComboBox` has both, which is the whole confusion: its `background`/`contentItem`/`indicator`/
`popup` are style components, and its `delegate` is the row renderer. So the `background:
Rectangle {}` in a `Button` **is** a delegate, and that snippet is what R1 means: keep the C++
behaviour (focus, press, Return/Space, the accessible action) and replace the components that paint
it.

- **R0 — Controls directly.** Instantiate a Controls type and set declared properties only (`text`,
  `model`, `palette.*`, `padding`, `delay`, `orientation`, `elide`). No `background`,
  `contentItem`, `indicator`, `handle`, `popup` or `delegate` assignment; the look is the style's.
- **R1 — delegates over a Controls type.** The same type with the same C++ behaviour; the
  style-owned delegates are replaced with the design's.
- **R2 — templates.** `import QtQuick.Templates as T` and build the control from the template,
  owning the whole assembly — the route a custom style takes. For when the part that must change has
  no delegate slot, or the style's own assembly is wrong. One type looks like this case and is not:
  `TextField` has **no `contentItem` at all** (measured), because it *is* its own editor, so it is
  R1 with its text behaviour already intact rather than R2.
- **R3 — hand-rolled.** QtQuick primitives only, which is what the exporter is today.

**R2 has no members as a per-element choice, but it does have a shape: a custom style.** The Qt
Quick Controls documentation's "Creating a Custom Style" is R2 exactly — a directory of
`QtQuick.Templates`-rooted QML files plus a `qmldir`, found on the QML import path and selected by
name through `QQuickStyle::setStyle()` (or `-style`), with `import QtQuick.Controls.Basic auto` in
the `qmldir` as the fallback for controls the style does not implement. With a style, every use site
becomes R0 — `Button { text: qsTr("Export") }` — and the design lives in one directory instead of
being repeated in N wrapper files.

That matters because our `AeButton`/`AeCheckBox`/… files are the documentation's **second** way
("move the code into its own QML file within your project"), not a different idea: converting them
to R1 keeps the same file structure and changes only what is inside each file. Which of the two
routes to take — a wrapper per control, or one custom style — is a deliberate architectural choice
and the only one on this page that is not per-element. It is the difference between the design being
restated at each control and being defined once.

One element cannot be done per-site at all, and the documentation says so: **the attached
`ToolTip`**. "The three approaches mentioned here do not work for customizing the attached ToolTip,
as that is a shared item created internally … To customize the attached ToolTip, it must be provided
as part of your own style." So the tooltip's E-cluster is satisfied either by instantiating
`ToolTip { }` objects with our `background`/`contentItem` and our `delay`, or by a style-provided
ToolTip. That is a real constraint on E26–E30, not a preference.

(The install also carries `Qt/labs/StyleKit` — a declarative styling layer over the templates, and a
*Technology Preview* in Qt 6.11. It is another way to define the design once; not recommended here
because of the preview status.)

## Three premises, measured rather than asserted

1. **Controls works in this host.** A spike scene hosted in a `QQuickWidget` — `Button`,
   `TextField`, `CheckBox`, `ComboBox`, `ScrollBar`, `SearchField`, `ToolTip`, `SpinBox`,
   `ScrollView`, `ItemDelegate`, `Label`, `TreeView` — loads and renders, `palette.highlight` is
   writable, and the delegate hooks are populated by the Basic style where they exist. Which hooks
   *exist* is not uniform, and one absence changes a rung (below). Measured property by property,
   `exists=0` meaning the property does not exist at all as opposed to holding null:

   | type | `background` | `contentItem` | `indicator` | `popup` | `delegate` | other |
   | --- | --- | --- | --- | --- | --- | --- |
   | `Button` | yes | yes | — | — | — | |
   | `TextField` | yes | **no such property** | — | — | — | `placeholderTextColor` exists |
   | `SpinBox` | yes | yes | — | — | — | |
   | `ComboBox` | yes | yes | yes | yes | yes | |
   | `ScrollBar` | yes | yes | — | — | — | **no `handle`**; `contentItem` *is* the handle |
   | `CheckBox` | — | yes | yes | — | — | |
   | `ScrollView` | — | yes | — | — | — | |

   (Source kept at `.git/dsh-scratch/controls-spike` for re-measurement.)

   The documentation says the same thing in different words — each control "consists of" a number
   of *visual items*: `Button` two (background, contentItem), **`TextField` one (background)**,
   `CheckBox` three (adds indicator), `ComboBox` five (adds popup, indicator, delegate), `ScrollBar`
   two, `SpinBox` four (adds up/down indicator). That agrees with the table, and `TextField`'s
   single item is the documented form of the no-`contentItem` finding.
2. **The default style here is not Basic.** With no style requested, `QQuickStyle::name()` is
   `Fusion`. Basic — the documented base for customisation — must be selected explicitly, before the
   first scene is created.
3. **The design's look does not fit in a palette.** The `theme` context property carries 25 colour
   tokens (26 properties; `closeIcon` is a URL). `QQuickPalette` exposes 20 roles ×
   active/inactive/disabled: `base`, `brightText`, `button`, `buttonText`, `dark`, `highlight`,
   `highlightedText`, `light`, `link`, `linkVisited`, `mid`, `midlight`, `shadow`, `text`,
   `toolTipBase`, `toolTipText`, `window`, `windowText`, `placeholderText`, `accent`. Exactly six
   of the 25 have a true role: `base`, `text`, `textDisabled` (the disabled group), `accent`,
   `selection` (`highlight`), `selectionText` (`highlightedText`). The surfaces the design is built
   from — `raised`, `raisedHover`, `recessed`,
   `active`, `activeHover`, `hairline`, `outline`, `focus`, `hover`, `textMuted`, `textMutedDim`,
   `accentHover`, `accentBright`, `accentText`, `icon`, `danger`, `warning`, `floaterBody`,
   `floaterEdge` — have none.

**So R0 is nearly empty by construction, and for a different reason than the hand-rolled files
give.** The chrome is replaceable — that is what `background`/`contentItem` are for. The palette is
the part that is genuinely too small: three quarters of this design's colours have nowhere to live
in it, so every control that paints a surface needs at least one delegate no matter how good
Controls is. "Use Controls directly where we can" therefore has a real, short answer rather than a
disappointing one.

## The inventory

| Element (today) | Rung | Type, and what is overridden | Lines met, and what the rung buys |
| --- | --- | --- | --- |
| Picker search field (`AeTextField`, 150 l, hand-rolled `TextInput`) | **R1** | `TextField` — `background`, `padding`, `placeholderTextColor`; **no `contentItem` exists to override**, because `T.TextField` is not a Control but the editor itself (measured). The leading glyph and the clear button are therefore composition beside the field, shifted in by `leftPadding`/`rightPadding` | B12 I-beam and B13 selection/`selectByMouse` come from the type; `placeholderText`/`accepted`/context menu follow. Ours either way: focus-on-open (B6), `navigate()` forwarding (B14), click-outside (B11) |
| Buttons (`AeButton` 106 l, `AeIconButton` 81 l) | **R1** | `Button` / `ToolButton` — `background`, `contentItem` | A1 focus ring, A2 Return/Space, `down`/`hovered`/`pressed`, `Accessible.Button` and the press action Qt already exposes for that role, `checkable`/`checked` for the aspect toggle. The design's look is those two delegates |
| Checkbox (`AeCheckBox` 88 l) | **R1** | `CheckBox` — `indicator`, label colour via `palette.text` | one click/Space toggle action, `checked`, focus, the accessible toggle action |
| Combo (`AeComboBox` 254 l) | **R1** | `ComboBox` — `background`, `contentItem`, `indicator`, `popup`, `delegate` | G39 type-ahead, G37 Up/Down highlight and Space/Escape/Enter, highlight-follows-keyboard, and the positioner that flips and clamps (G38). **The in-scene popup is not abandoned to meet G38** — `popup` is our delegate either way; what stops being ours is the list, its highlight and its keys |
| Scrollbar (today a decorative hand-drawn Rectangle) | **R1** | `ScrollBar` attached to the list — `contentItem` (which *is* the handle; there is no `handle` property, measured) and `background` (the track) | D23 press/drag → `setPosition`, the visible/total ratio, wheel and paging, `policy: AsNeeded`; D24 is the two delegates |
| Tooltip (`AeToolTip` 43 l) | **R1, but not the attached form** | an instantiated `ToolTip` — `background`, `contentItem`; `delay`/`timeout` are properties | E28 (a real popup, not clipped by the dialog), E27 placement and flip, E30 pass-through, one shared instance. **The stock `delay` is 0** (`qquicktooltip.cpp:111-130`), so E26 is met by the value the design sets — true on any rung. The *attached* `ToolTip` is excluded from per-site customisation by the documentation, so this is an instance or a style, not `ToolTip.text:` on the owner |
| Numeric field (`ScrubSpinBox` 205 l, `AeNumberField` 121 l) | **R3, with an R1 editor inside it if we want one** | Three responsibilities, and only two of them are a delegate anywhere. **Value semantics** — `from`/`to`/`value`/`stepSize`/`editable`/`live`/`validator`/`textFromValue`/`valueFromText` — are `SpinBox`'s (measured from the type's property list). **The editor** is `SpinBox.contentItem` (a `TextInput` with `readOnly: !editable`, measured in `Basic/SpinBox.qml`). **The scrub shell** — display-until-clicked, horizontal drag, shift-fine, click-to-edit, Escape-revert — is a delegate in *no* toolkit, because it is behaviour rather than a component | the scrub is ours on any base. F31/F33 (cursor, selection tokens) are a one-line fix each in the existing control. **[open, and the reason this row is not a conversion]** `SpinBox` assumes an editor is always present (`readOnly: !editable`), while the design hides the caret until the field is clicked — so a SpinBox base still needs the mode switch written by hand *and* has to stop the always-present editor from taking the click and focus the scrub shell needs |
| Disclosure (`AeDisclosure` 63 l) | **R1** | a checkable `Button` (`checked` = open) with a `contentItem` (caret + label) and a `background` (hover) | today it hand-writes a `MouseArea`, `Keys.onSpacePressed`/`onReturnPressed` and its own `Accessible.role`/`onPressAction`; `Button` already has all of that (`text`, `down`, `pressed`, `checked`, `checkable`, `indicator`, `activeFocusOnTab`). There is no Controls type that *is* a disclosure, so what remains ours is the caret and the words |
| Section header (`AeSectionHeader` 46 l) | **R3** | `Label` + hairline `Rectangle` | nothing; no type is a ruled heading, and `T.Label` is `Text` plus a palette binding we do not use |
| Fact row (`AeFactRow` 67 l) | **R3** | two `Label`s | nothing; not interactive, so no Template has behaviour to contribute. Its `Accessible.role: StaticText` is required on any rung |
| Field label (`AeFieldLabel` 32 l) | **R0** | `Label` | **nothing** — every property it sets is set anyway, and `Layout.*`/`Accessible.*` are not Controls features. This is the honest shape of R0 here: `Label` is `Text` plus a palette binding we do not use |
| Frame range row (`AeFrameRangeRow` 76 l) | **R3** | composition of two numeric fields + a trash button | composition is not a rung decision; its parts have theirs |
| Destination list **rows** (`DestinationPicker.qml`'s row `Rectangle` + `Text` + `MouseArea`) | **R1** | `ItemDelegate` — `background`, `contentItem` | this row was missing from the first draft and belongs here. `ItemDelegate` publishes `highlighted`, `down`, `pressed`, `checked`/`checkable`, `text`, `background`, `contentItem` and handles focus and the accessible action; C21 (hover paint) and C20 (the highlighted row the keyboard drives) are then one state binding each, not a MouseArea we reimplement. The picker's *list* stays core `ListView`; only the rows change |
| Destination list (`DestinationPicker.qml`) | **not a Controls question** | `ListView` (Qt Quick core) over the flat row model | the model's rows are non-contiguous once filtered, so `ListView.section` cannot own the groups; `TreeView` resolves in this Qt (measured) but would move expansion and filtering into Qt's model contract and out of the headless tests. C18 indentation and the model-backed selection stay ours on every rung |
| Form scroll container (`ExportDialog` parameters) | **R1** | `ScrollView` with the same ScrollBar delegate, or a core `Flickable` | D25's wheel/drag/keys; Q9 is a layout question, not a toolkit one |
| Dialog shell (`QDialog` + `QQuickWidget`) | out of this table | — | the host question belongs to E28's Wayland measurement |

## What this means for the files we have

- Two elements were under-called in the first draft and are corrected above: the **disclosure**,
  which hand-writes focus, Return/Space and its own accessible action that `Button` already
  provides; and the picker's **rows**, where `ItemDelegate` publishes the `highlighted` state that
  C20 and C21 are about.
- The one place an R1 conversion is plausibly **negative** is the numeric field. `SpinBox` would
  replace roughly seven lines of clamping and parsing with a dependency, and its always-present
  editor contradicts the design's display-until-clicked mode; F31 and F33 are a one-line fix each
  in the control we already have. Converting it is the conversion that has to be justified, not the
  one to assume.

- Seven files stated a constraint as fact that is now measured false — `AeTextField.qml:7-9` (DONE,
  converted to `TextField`; see below), `ScrubSpinBox.qml:4-6`, `AeComboBox.qml:7-12`, and copies at
  `AeCheckBox.qml:8-11`, `AeSectionHeader.qml:15-16`, `AeToolTip.qml:7-11`. "QC2 brings its own
  chrome and its own palette" is half true (the palette is too small, the chrome is a delegate);
  "a QQuickWidget cannot render a popup outside its own bounds" is wrong — a window-backed
  `Popup.Window` does draw outside the scene, and on Wayland whether it *maps* from a
  `QQuickWidget` host depends on the compositor's backend (2026-10-03 correction below). Whichever
  rungs we take, those comments must stop giving untested limits as the reason.
  [Corrected 2026-10-03: the line numbers above are the state *at the time* — every one of those
  headers has since been rewritten and moved. Note that the bullet conflated two different claims:
  the QC2 chrome/palette one (the list) and the popup-bounds one. The popup-bounds claim lived in
  exactly three headers — `AeComboBox.qml`, `AeToolTip.qml` and `DestinationPicker.qml` — and all
  three now state the measured, backend-scoped result instead; it survives in no QML file.]
- The accessibility layer is orthogonal and survives every rung: `QmlFormAccessibility`'s factory,
  the `aeAccessible*` nodes and the value interface exist because Qt exposes no value protocol for
  QML spin boxes — not because these controls were hand-rolled.
- One type decision is worth taking before the search field is converted: `SearchField` (Controls
  6.10, measured present) is the B-cluster's exact shape — magnifier indicator, clear indicator,
  `delegate`/`highlightedIndex` — but it is a `ComboBox` subclass, so it announces ComboBox and
  carries a suggestion popup the picker has no use for. `TextField` keeps the accessible role the
  picker's probes already address. Recorded as a decision, not left implicit.

## Increment 2, landed: the search field is a `TextField`

`AeTextField.qml` is now Qt Quick Controls' `TextField` with the design's own `background`. Same file
name and same addressing, so no call site moved. The header comment that claimed Controls "brings
its own chrome and its own palette, and cannot be customised per site" is gone; the chrome *is* a
delegate and the design's background is a drop-in for it.

Measured on this installation (the Basic style's `TextField.qml`, read from
`/usr/lib64/qt6/qml/QtQuick/Controls/Basic/`):

- `T.TextField` **inherits `TextInput`** — it *is* the editor, which is why the earlier probe found
  no `contentItem`: there is nothing between the control and its text.
- The style supplies the placeholder itself, as a child: `PlaceholderText` positioned from
  `control.leftPadding` / `rightPadding` / `verticalAlignment`, coloured by `placeholderTextColor`,
  and hidden while there is `length` or `preeditText`. So the hand-written placeholder `Text` is
  gone and the leading glyph and the clear button are paid for with `leftPadding` / `rightPadding`
  rather than with anchors.
- Basic's defaults are `padding: 6`, `leftPadding: padding + 4`, `verticalAlignment: AlignVCenter`.
- It installs `ContextMenu.menu: TextEditingContextMenu` — right-click cut/copy/paste/undo, which the
  hand-rolled field did not have.
- **`hoverEnabled` defaults to `false`**, so a background that reacts to `hovered` must set it. The
  hand-rolled version got hover from its own `MouseArea`.
- Two more measured API facts the conversion depends on: `textEdited()` and `accepted()` take **no
  arguments**, so this wrapper must not declare `signal accepted(...)` (duplicate name with the
  inherited one — Return already reaches the caller unchanged) and the clear button, whose clear is
  programmatic and therefore emits nothing, is the only reason the custom `edited(string)` survives.

What the conversion actually bought, in the terms the contract uses: caret motion, keyboard
selection, drag-select, word jumps, the clipboard shortcuts and undo, input methods, the horizontal
scrolling that keeps a long line's caret in view, and the I-beam cursor. That last one is B12, one of
the reported defects: the full-bleed `MouseArea` that focused the hand-rolled field also painted an
arrow over its text.

**Behaviour decided here, not merely preserved:** B9. The field no longer forwards Left/Right at all
— `navigationKeys` is now the VERTICAL keys only — so an arrow pressed while typing can no longer
collapse or expand the highlighted platform. Left/Right stay with the caret. B14 was rewritten to
say this, and Q4 was answered (focus selects nothing) and moved out of the open list.

Verified by `tests/picker_keyboard.cpp`: the real `DestinationPicker.qml`, built offscreen, with the
keys injected into the item, asserting on the model — 32 checks, B6/B7/B8/B9/B13/B15. Run it against
the old forwarding and exactly three B9 checks fail and it exits 1, so the test is known to be able
to fail. B11 (click outside removes focus) and B12 (the cursor shape) are deliberately absent: both
need a real window, which this test does not have.

## Verification by driving the UI, one interaction at a time

The alternative to a probe script is just *looking*: one application on one Xvfb display, driven with
`xdotool`, captured with `import`, inspected between steps. About ten minutes, no encoded navigation,
and it covers everything a person sees. This is how the search-field conversion was verified after
`picker_keyboard`:

| line | observation |
| --- | --- |
| B6 | typed `telegram` → list filters to the four Telegram rows, clear button appears |
| B7 | two × `Down` → highlight moved row 1 → row 3; text still `telegram`, caret still at the end |
| B9 | two × `Left` → **group still open**, all four rows present, text and highlight untouched |
| B9, caret | then typed `X` → **`telegrXam`**, i.e. `Left` really moved the caret to position 6 instead of being swallowed |
| B12 | `ffmpeg -draw_mouse 1`, cropped and scaled 400 % → **I-beam** over the text (first end-to-end use of that path in this repo) |
| C17 | platforms open collapsed |
| C18 | childless platforms (Bilibili, Dailymotion, Douyin, LINE sticker) align with the section icons rather than being indented |

One defect **found**, that nothing had asserted:

- **E26 — the tooltip has no wake-up delay.** Contract line 26 requires "the platform's wake-up
  delay — never instantly". Measured from a 30 fps recording while the pointer entered a row: the row
  highlight and the tooltip changed on the **same frame** (51 → 52), so the delay is ≤33 ms —
  effectively instant. A single screenshot cannot show this, because ffmpeg's own ~300 ms startup
  makes any quick tooltip look like it had a delay: the instrument has to compare the first-change
  frame of the hover highlight against the first-change frame of the tooltip.

The contract already prescribes the instrument for this line ("timestamped hover frames, 0 ms →
wake-up delay → timeout"), and that is the whole argument in miniature: a probe has to *encode* that
measurement to have it, an agent driving the UI takes it while looking, and neither one gives you a
regression guard for free — that part stays a test.

## E26, landed: the tooltip waits, and it was not a matter of picking a number

`AeToolTip` now arms on `shown` and reveals after **500 ms**, retires itself after **10 s**, and
cancels immediately if the pointer leaves. Two named constants at the top of the file.

The interesting part was proving there was nothing to read. Contract line 26 says "the platform's
wake-up delay", which sounds like a lookup:

- `QStyleHints::toolTipWakeUpDelay` is **Qt 6.12+**, and a grep of the whole installed 6.11.2 header
  tree matches **no** `showDelay`, `toolTipDelay` or `hideDelay` at all.
- Qt Quick Controls' `ToolTip.delay` **defaults to 0** ("a tooltip with a negative delay is shown
  immediately", and `timeout` defaults to -1 = never), and **no installed style sets either** — so
  switching to Controls would have reproduced the defect.
- Qt's *own* `QToolTip`, measured with a self-timing Widgets spike (records `QEvent::Enter`, polls
  for the tooltip window, prints the delta):

  ```
  ENTER    at 2846 ms
  TOOLTIP  at 2848 ms -> DELTA 2 ms
  ```

So following the platform on Qt 6.11.2 **is** the defect: Qt's tooltip is instant here. The delay is
therefore the design's own constant (500 ms / 10 s, chosen as the conventional desktop values), and
the file says so, with a note that `Qt.styleHints.toolTipWakeUpDelay` should replace the first one
if the project moves to 6.12.

**The hole worth recording:** the picker sets `shown: hoveredRow >= 0`, which stays `true` while the
pointer scans from row to row — only `text` changes. Driving it that way showed the label jumping to
the next row instantly, i.e. the same E26 defect through a different door. `onTextChanged` now
re-arms, which also restarts the hide timeout.

Verified by the instrument the contract prescribes (timestamped hover frames, 30 fps):

| E26 requirement | measurement |
| --- | --- |
| appears after the delay, never instantly | cursor at frame 021, tooltip at **036** → 15 frames = **500 ms** |
| hides when the pointer leaves | drop at **081**, the frame the pointer left |
| hides after the timeout | frame 036 → **336** = 300 frames = **10.0 s** |
| scanning rows re-delays | row B: vanish at **096**, reappear at **112** (+16 frames ≈ 500 ms) |

Before the fix the same measurement put the hover highlight and the tooltip on the **same frame**.

## R1, landed: `AeButton` is a `Button`

Same file name, same call sites (they use only `text`, `kind`, `iconName`, `enabled`, `onClicked`).
Chrome comes from delegates, so the design keeps its three fills and its rule that a focus ring is
the only border ever drawn; Qt supplies hover, pressed, `focusPolicy`, `checked`, the button role,
and `click()`.

Measured before converting — three things had to be checked rather than assumed:

| question | answer | consequence |
| --- | --- | --- |
| does Qt fire `clicked` on Space? | **yes, on release** (press-only fires nothing) | the hand-written `Keys.onSpacePressed` would have **fired it twice** — dropped |
| Return / Enter? | **no** — Qt's Button has no default-button concept in QML | contract line A2 still needs `Keys.onReturnPressed` — **kept** |
| cursor after dropping the `MouseArea`? | `HoverHandler.cursorShape` exists | the pointing-hand cursor survives without a MouseArea that would double-handle the click |

Also measured: `visualFocus`, `hovered`, `pressed`, `down`, `focusPolicy`, `checked`, `palette` and
`click()` all exist, `clicked()` takes **no** argument (calling it with one prints "Too many
arguments, ignoring 1"), and Basic's padding is symmetric (8/8) so centred content stays centred.
`icon.source` is a *group* and reads invalid through `QObject::property`, which is why the content
stays ours rather than moving to Qt's `IconLabel` — Qt colourises images, and this theme hands out
pre-tinted SVGs.

One behaviour change worth naming: the focus ring now binds `visualFocus` (Qt's, true for keyboard
focus) instead of `activeFocus`, which also lit the ring after an ordinary mouse click. Contract line
A1 asks for a focus ring on the focused control, and a ring left behind by the pointer is not one.

Verified by driving it: rest state has no border, hover gives the lighter fill **and** the hand
cursor (captured with `-draw_mouse`), a Tab puts the ring on Cancel, and Return with that focus
closes the picker (`Use this destination`: showing → absent). Suites 16/16 in Debug; Release built
zero-warning and also 16/16.

## R1, landed: `AeCheckBox` is a `CheckBox`

Same file name, same call sites (`text`, `checked`, `enabled`, `onToggled` — all four are inherited,
none redeclared). The box, the tick and the words are the `indicator` and `contentItem` delegates;
Qt supplies the toggle, Space, `focusPolicy`, hover, `checked`/`toggled`, the checkbox role and the
press action. The old header's reason for hand-rolling — *"a QC2 indicator draws its own chrome"* —
was the same non-reason as the button's: that chrome is the delegate being replaced.

Measured before converting (`.git/dsh-scratch/key-spike`, offscreen):

| question | answer | consequence |
| --- | --- | --- |
| does `toggled()` exist? | **yes** (AbstractButton's), and it fires **only on activation** — `click()`, Space, a press on the row. Writing `checked` from a binding or directly does **not** fire it | exactly what `onToggled: model.value = checked` needs: no write-back when the model pushes a value |
| Space? | toggles **on release**, with no `Keys` filter involved | the hand-written `Keys.onSpacePressed` would have toggled **twice** (our press, Qt's release) — dropped |
| Return / Enter? | **neither consumed nor acted on** (`accepted=0`, `checked` unchanged) | the dialog's own Enter handling still receives them, and no handler is needed — the old control had none either |
| does Qt place our delegates? | in **x/y** our anchors win; but it writes their **width and height** from its padding: the label came out 556×12 where the hand-rolled `Text` was 17 tall | centring a box Qt had just shrunk moved every word **3 px down** (1205 differing pixels). Fixed by anchoring the label's **top** to its own line box: `topMargin = floor((height - implicitHeight)/2)` = 3, where they were |
| can the label anchor to the box? | no: `anchors.left: ind.right` throws `ReferenceError: ind is not defined` at creation | the offset comes from `boxSize`, not from the other delegate's width |
| `hoverEnabled` default | **false** | hover has to be asked for |
| `focusPolicy` default | **0xB**, which *is* `Qt.StrongFocus` — the enum is Tab \| Click \| 0x8, not 3 | Tab and Click, matching the widget `QCheckBox`; a click that left the previous control focused is how Space after ticking a box types into a text field |

**The focus ring is contract line A1**, which the hand-rolled control had no provision for at all.
Its shape is not invented: `standard.ssa` already specifies it for the widget checkbox —
`QCheckBox::indicator:focus { border: 0.04em solid @focus@ }` — so the box's hairline turns to
`theme.focus`, focus ahead of hover exactly as the stylesheet's cascade has them. `activeFocus`
rather than `visualFocus` (the button's choice) because the stylesheet says `:focus` and not a
keyboard-only variant, and because the widget `QCheckBox` is `StrongFocus`: clicking it focuses it
and lights the same border.

### Verification: offscreen first, then the real application

Two instruments, because a probe stack costs a machine that is being used
(`PROBE-SAFETY.md`: measure the compositor before and after, abort when it is hot):

1. **`geom` renders both versions offscreen** — old file from git, new from the working tree, four
   states each (rest, hover over the box, hover over the label, focus), ~2 s, no display:

   | old vs new | differing pixels |
   | --- | --- |
   | rest | **0** |
   | hover over the box / over the label | **0** |
   | focus | **72** — the ring: `focus vs rest` is 0 for the old control and 72 for the new |

   Each state prints an `achieved=` line, and it earned its keep twice: the first run's "rest" was
   silently a **hover** grab (the offscreen pointer sits at 0,0, on top of a control drawn there),
   and posting the hover event to the root never reached the hand-rolled control's `MouseArea`
   *child*, so the two sides were in different states and their difference meant nothing. Driving
   the hover through the **window**, the way a pointer does, fixed both.

2. **The real application**, three runs: the before run, the run with the fix, and a repeat of the
   same build:

   | comparison | differing pixels |
   | --- | --- |
   | the control's own box, any state | **0** in every pair |
   | geometry string | identical (`rect=436,619 568x24`) in all three |
   | hover lights | the same 72 pixels in both builds, at all three hover points |
   | focus ring | absent after 30 Tabs before; present on Tab #9 (72 px) after |
   | Space | `checked` yes → no → yes, read from the accessibility tree |
   | the form as a whole | **52** between two runs of the *same* build, 53 and 72 across builds |

   That last row is why the comparison now has two kinds of assertion. Qt's rasteriser does not
   reproduce glyph edges between processes: two runs of one binary differed in 52 scattered pixels
   **including buttons nobody touched**, and 36 of them were the same pixels in both run pairs —
   so a dialog-wide number is judged against a measured floor (2×), while the box, the geometry,
   the hover delta and the ring are asserted exactly. A real regression clears that floor by an
   order of magnitude: the 3 px label drift measured **1205** in the row alone.

Two counting traps, both measured: ImageMagick stamps a `date:timestamp` into every PNG it writes,
so **compare with `magick compare -metric AE`, never a file hash**; and a raw
`-compose difference` over un-flattened PNGs counts RGB differences *inside fully transparent
pixels* (745 against the true 72, while the alpha channels were identical) — flatten first, or use
`compare`.

## R1, landed: `AeDisclosure` is a `ToolButton`

Both call sites are unchanged — `text`, `open`, `onToggled`, `Layout.fillWidth` — and the header's
original argument survives, narrowed to one property instead of ruling out the toolkit: the widget
form used `QToolButton:checked`, and that paints the checked state with the QSS `@focus@` colour,
which would fill "Advanced" with the accent. So `checkable` stays **off**, `open` remains the
model's word, and Qt still supplies activation, Space, hover, the focus policy and the button role.

Measured before converting:

| question | answer | consequence |
| --- | --- | --- |
| does `toggled()` exist, and can we emit it? | **yes**, and `QMetaObject::invokeMethod(..., "toggled")` returns true | `onClicked: root.toggled()` keeps `onToggled` working at the call sites while the control is deliberately not checkable — Qt's own `toggled` never fires, so there is one signal and one owner of it |
| Space? | fires `clicked` **on release** (press only sets `down`) | `Keys.onSpacePressed` would have toggled twice — **dropped** |
| Return / Enter? | **neither consumed nor acted on** (`accepted=0`) | unlike the checkbox, this control has always handled Return, so the design's `Keys.onReturnPressed` is **kept** and cannot double-fire |
| `hoverEnabled` default | **false** | hover asked for explicitly |
| `focusPolicy` default | 0xB (StrongFocus) — but the **widget `QToolButton` is TabFocus** (measured) | stated as `Qt.TabFocus`, which is also what the hand-rolled control did: `activeFocusOnTab` plus a `MouseArea` that never took focus |
| `click()` | exists and fires `clicked` | `Accessible.onPressAction` goes through it, as on `AeButton` |

**The ring is contract line A1 again**: this header declares `activeFocusOnTab`, so it must show one,
and it drew none. `standard.ssa` has no `QToolButton:focus` rule to borrow — it has `QPushButton:focus
{ border-color: @focus@ }` — and Qt's own Basic `ToolButton` draws a border on `visualFocus`, so the
ring is AeButton's: 1 px of `theme.focus` around the row, the only border it ever draws.

Verification, offscreen (`geom`, both files in one engine, four states each):

| old vs new | differing pixels |
| --- | --- |
| rest | **0** |
| hover over the row / over its right end | **0** (both light the whole 568×30 row: 17036 px, identically) |
| focus | **1208** — a 1 px ring around 568×30, sampled at `#a78bfa` = `theme.focus` |

and within a version: `focus vs rest` is **0** for the hand-rolled control (the defect) and **1208**
for the new one. One instrument detail worth keeping: the ring binds `visualFocus`, so the grab must
give focus with `Qt::TabFocusReason` — focus given for any other reason photographs as "no ring" and
reads like a defect that is not there.

## R1, landed: the picker's rows are `ItemDelegate`s

The last `Rectangle` in the picker became Qt Quick Controls' `ItemDelegate`: the control
supplies activation (`clicked`), the pointer machinery and the focus policy; the row keeps how it
looks (its own `background`), what a click means (`onClicked` → toggle a section, select a
destination), and — unchanged — the keyboard, which the ListView's `activeFocusOnTab` plus
`Keys.onPressed` have always owned. The caret, badge and labels stayed anchored to the delegate
root, whose parent is still the control, so the style's padding cannot move them, and the explicit
`width`/`height` stay for the same reason: a Control sizes its delegates from padding and
`contentItem`, and the collapsed row's `height: 0` is load-bearing (it is how expansion and
filtering avoid renumbering rows).

Measured before converting:

| question | answer | consequence |
| --- | --- | --- |
| `hoverEnabled` default | **true** (unlike `Button`/`CheckBox`, which default false) | see the latch defect below — the property ends up **false** on purpose |
| `focusPolicy` default | 0x0 = **NoFocus** | a row is not a Tab stop; contract A1's tab sequence is untouched, nothing to state |
| Basic style's own `ItemDelegate` | `padding: 12`, a `background` that draws for down/highlighted/visualFocus, an `IconLabel` `contentItem` | our `background` replaces all of that — no pressed fill appears that this design does not have |
| does `clicked` coexist with a `TapHandler`? | yes: one tap gives `clicked=1 AND tapped=1`, a double tap adds `double=1` | `onClicked` owns select/toggle, the handler keeps **only** `onDoubleTapped`; wiring both would toggle a section twice. A double tap on a section still nets two toggles — two clicks plus a `doubleTapped` that ignores sections — exactly as before |

### The latch defect, found by the verification rather than by reading

The first conversion had the background read the control's `hovered`. The drag state of `geom`
(press a row, drag 100 px, release) then disagreed with the old build by **31544 px** over a
bounding box of exactly two rows. A per-event trace named the mechanism: the gesture delivers
**MouseMove to every crossed row and no HoverLeave at all** — not during the drag, not after it —
so `hovered` latched true on all three crossed rows and two of them (the third was the selected
row, whose colour the selection token overrides) kept `theme.raised` after the pointer had long
left. The same trace exposed a second defect: during that gesture the row's own `HoverHandler`
reported nothing (`listArea.hoveredRow` stayed −1 where the old Rectangle tracked 1→2→3), so the
tooltip and the pointing-hand cursor would have diverged too.

The fix is the old design, restated with the reason: `hoverEnabled: false` (its `hovered`
deliberately unread) and the colour reads `rowHover.hovered` again, the handler that also feeds
the cursor and the tooltip. One divergence remains and was accepted as-is: **while a button is
held**, the handler no longer follows the pointer across rows the way the old Rectangle did; the
first buttonless move after release puts it back exactly where the old code left it, all five
pixel states are equal, and in a normal click the freeze is invisible.

### Verification

| gate | result |
| --- | --- |
| builds | Debug **0 warnings**, Release **0 warnings** |
| units | `destination_picker` 82 checks PASS both configs; `picker_keyboard` **32 checks, 0 failures** both configs; full suites **16/16** Debug and Release |
| `geom` preconditions | **10/10 `achieved=yes`**, 0 `no` |
| old vs new, `magick compare -metric AE` | rest **0**, hover-over-box **0**, hover-over-label **0**, focus **0**, drag **0** |
| catalogs | six files, `grep -c unfinished` = **0** |
| end-to-end sweep | **13/13** probes, 0 stale, 0 portal lines (dialog canary first); `cosmic-comp`'s main thread 0–7% across 189 samples, never near the 35% abort |

Instrument notes worth keeping: `findChildren<QQuickItem*>` reaches **0** ListView delegates
(QObject parenting) — walk `childItems()`; a scratch binary left uncompilable by a previous edit
made the first run silently use a **stale binary**; and the old baseline only loads from the
directory that holds the component symlinks (`oldpicker/`), since the picker's `Ae*` siblings are
not on its own.

## R1, landed: `AeComboBox` is a `ComboBox`

The last risky R1 item. The root is now `Controls.ComboBox`; the call sites (Format, Codec,
Quality) did not change — `options`, `index`, `flat`, `overlay`, `value`, `text`, `activated` all
survive, and every user choice still reaches the model through `activated(int)` with `index`
written only by the call sites' bindings (the control mirrors `index → currentIndex` one-way and
never writes `index`, so no binding can be destroyed).

### The header's reason for existing was false — and irrelevant to the design

The old header claimed Controls' ComboBox cannot render its popup outside the hosting
QQuickWidget. Measured on this Qt (6.11, QQuickWidget host): the stock popup is
`popupType: Popup.Item`, parented to the overlay **inside the widget's own window** (window count
unchanged), clipped to the widget, with Qt's positioner doing below → flip-above → clamp — the
same dance `updatePopupGeometry()` hand-wrote. `Popup.Window` also works here (a fourth window
drawing over the title bar, screenshot captured) — that capture is xcb, and on Wayland the mapping
is backend-dependent (2026-10-03 correction below). The design decides anyway: `export-dialog-
behavior` G37 — *"the popup is the design's in-scene list"* — so in-scene stays, now as a choice.

### Decisions taken during the conversion (all from the contract, none invented)

| question | verdict | basis |
| --- | --- | --- |
| type-ahead (Q6/G39) | **adopted**, ships with `textRole: "label"` | user decision 2026-09-29; without the role `keySearch` has no text (idx never moved) and the style logs `Unable to assign QVariantMap to QString` — the role fixes both |
| Enter, popup closed | stock **swallows**; observably identical to old | the old key propagated into a **void**: no QML Return handler (only `Ctrl+Return`/`Escape` shortcuts), no C++ key handler, no default `QPushButton` for `QDialog` — an earlier claim that it fired Export was retracted as inference from acceptance flags, never observed |
| Escape, popup closed | stock **propagates** → cancels | A2 `[D] Escape cancels`; the old control's unconditional `event.accepted = true` failed A2 whenever the list was closed — a defect the conversion fixes |
| Down/Up with list open | **choose** (emits `activated`, list stays) | design reference = current UI (which chose) and G37 lists Down among the choose actions; stock's highlight-only deviates |
| Enter with list open | stock: **commit + dismiss** | G37 says dismiss; old left the list open (its Enter would have reached Export with the list up) |
| Space | stock's **release** timing; with list open it also chooses | `AeDisclosure` precedent (release, not press); G37's literal "Space … choose and dismiss" |
| Tab with list open | **dismisses the list** | decided 2026-09-29, recorded in the contract (item 37): old's list-and-scrim-left-behind was an unowned accident, not a specified behaviour |
| click focus | `focusPolicy: Qt.TabFocus` — click opens, does **not** focus | old = Tab-only; stock's default StrongFocus would take focus on click (measured) |
| outside click while open | `popup { modal: true; dim: false }` | old's transparent scrim swallowed the press (measured 0→0); stock's default non-modal passed it through (0→1) — clicking Export under an open list would have fired it |
| pointer cursor | `HoverHandler { cursorShape: Qt.PointingHandCursor }` | old field's `MouseArea` carried it; every sibling control keeps it (AeButton precedent). **Pixel comparison cannot see a pointer** — the contract's own verification table names that trap — so it was caught by diff review, not by AE=0, and verified by `ffmpeg -draw_mouse 1` photographs of both controls on Xvfb |

### Five defects the gates caught, each measured then re-measured

1. **`flat` is Qt's own FINAL property** (`Q_PROPERTY … REVISION(2,1)`) — redeclaring it killed the
   file at load, while the app build's qmlcache compiled happily. Dropped ours; no call site ever
   set it; the background reads Qt's.
2. **The style's double-arrow drew anyway** — overriding `background`/`contentItem` does not remove
   `indicator`: 133 px in every closed-field state, a second caret beside ours. `indicator: null`
   (the style null-checks it) → 0.
3. **The popup opened AT the field** (y=0) — the positioner's baseline is a *style binding*
   (`y: control.height …` in Basic/ComboBox.qml). `y: root.height + 2` + 4 px margins restored
   old's geometry exactly: open box at 130 = 130, flip at 134 = 134, same positioner dance.
4. **Qt auto-scrolled the list to the current row on open** (contentY=48 vs old 0). No contract line
   asks for the scroll; the reference decides → `onOpened: list.contentY = 0`.
5. **`highlightedIndex` is read-only from QML** — the first Keys preempt threw mid-handler, so
   Down-with-list-open committed nothing (the closed case worked only because stock rescues it).
   Caught by the against-the-file key matrix. The shipped design: the filter only *marks* Down/Up
   (`_keyNav`), stock moves the highlight, and `onHighlightedIndexChanged` mirrors it to
   `activated` **once, only when a key put it there** — type-ahead (unmarked) stays highlight-only
   per the ruling, and the flag resets on the next key/open/close.

### Verification

| gate | result |
| --- | --- |
| builds | Debug **0 warnings**, Release **0 warnings** |
| units | suites **16/16** both configs (includes `export_form`, which covers the combos, and the real 45-assertion `media_exporter_e2e`) |
| `geom` preconditions | **12/12 `achieved=yes`** (rest, hover×2, focus, popup-open, popup-flipped-above) |
| old vs new, `magick compare -metric AE` | all six states **0** |
| key matrix, shipped file vs baseline (`run_keys_final.log`) | parity on Down/Up closed+open (exactly one `activated` per press, list stays), Space, Escape (as ruled), Enter (as ruled), type-ahead (decided behaviour: moves + emits closed; highlight-only open), Tab/Shift+Tab closed, outside-click swallow 0→0, wheel; one decided divergence (Tab + list open) recorded in the contract |
| cursor | both controls photographed showing the pointing hand (`out/cursor_final_pair.png`) |
| targeted probes (`format`, `quality`, `axes`, `dialog` — the combo drivers, via AT-SPI) | **4/4 PASS**, 446 s, no file dialogs opened (no canary needed) |
| catalogs | untouched, `grep -c unfinished` = 0 |

Instrument note: the cursor hunt shows why a synthetic pointer *teleport* (`QCursor::setPos` /
xdotool XTEST jumping across windows in one event) is not evidence about hover: only the
in-window approach exercised the entry event, and only there did the HoverHandler's cursor apply —
the old `MouseArea` path survived the jump by setting the cursor per-motion, which is why the old
control always looked right in the same test. Photograph the real path, or photograph nothing.

## R1, landed: `ScrubSpinBox` keeps its shell — F31 and F33 fixed, two headers measured true

The last R1 item was never a conversion (the inventory's numeric-field row decides the scrub shell
is behaviour no toolkit ships, ours on any base): two review defects inside the control, plus two
header comments whose stated reasons had stopped being true.

### F31 — the pointer follows the mode, and the defect was actively wrong, not stale

Measured on the real X pointer path (`ffmpeg -draw_mouse 1`, judged only at 600% nearest zoom) in
**three** edit states — stationary after the click (the acceptance frame), after a 2px move-and-back
jiggle (fresh hover re-evaluation), and after leaving to the background and re-entering over the
text. The baseline shows `Qt.SizeHorCursor` over the text in **all three**, so no amount of hover
re-evaluation was going to fix it. The fix has two owners, matching the two ways the cursor can
arrive: `cursorShape: editInput.activeFocus ? Qt.IBeamCursor : Qt.SizeHorCursor` on the scrub
`MouseArea` (the switch lands at the click itself — the property write applies at the focus flip,
before the area disables itself and without pointer motion), and `HoverHandler { cursorShape:
Qt.IBeamCursor }` on `editInput` (owns a fresh hover in edit mode). Scrub-hover keeps
`Qt.SizeHorCursor` (F32): rest-frame AE before vs after = **0**.

| state | baseline | after |
| --- | --- | --- |
| rest / scrub hover | SizeHor | SizeHor (F32, AE=0 before-vs-after) |
| edit, stationary after click | **SizeHor (the defect)** | **I-beam** |
| edit, after jiggle | **SizeHor** | **I-beam** |
| edit, leave + re-enter | **SizeHor** | **I-beam**, AE=0 vs the stationary frame |

### F33 — the selection is the theme pair, sampled not asserted

Live-object readings of the real `editInput` (objectName `scrubEdit`, both fields in one window):
baseline `#ff000080`/`#ffffffff` (Qt default palette), after `#666d28d9`/`#fff5f5f5` — **exactly**
`theme.selection`/`theme.selectionText` as `QmlTheme` prints them (the only pair QML can bind;
`Colors::textSelection` exists in the C++ struct but is not exposed, and the design reference
`AeTextField` binds `selection`/`selectionText`). Rendered proof: the highlight's clean 22x3 block
is `#52367D` = `theme.selection` (alpha 0x66) composited over the measured `theme.raised` `#404040`
— `magick compare -metric AE` against a synthetic blend = **0**; the baseline's navy sample fails
the same check **66/66 px**. Selected-glyph pixels are subpixel-AA blends at 11px (never full
coverage), so `selectionText` is honestly evidenced by the live-object reading plus the baseline
contrast, not by pixel sampling.

### The two headers, each rewritten only after measuring its claim

- `ScrubSpinBox.qml:3-6` claimed "a QC2 SpinBox cannot be restyled … nor given scrub-drag
  behavior". Unmeasured and false in the interesting half: the inventory row measured that a
  `SpinBox` **can** host the styling (delegate-based; its editor is `SpinBox.contentItem`, a
  `TextInput` with `readOnly: !editable`, from `Basic/SpinBox.qml`) but assumes an always-present
  editor, which contradicts the design's display-until-clicked mode and the scrub shell's
  click/focus needs. The header now says exactly that, with the citation.
- `AeSectionHeader.qml:15-16` claimed "NOT a Controls Label, for the same reason every control in
  this dialog is hand rolled". The premise died with `f5aa28d..f7ef22e` (AeButton/AeCheckBox/
  AeDisclosure/AeComboBox all import QtQuick.Controls — verified). A fresh offscreen probe settled
  the true reason: `Label` **is** a `Text` (`QQuickLabel → QQuickText`; Qt docs "Inherits: Text"),
  its only property delta over `Text` is `background` + the four inset slots (chrome this heading
  never fills), default colour and font are identical, and the one live difference is the palette
  binding (`palette.windowText` green moved `Label.color`, not `Text.color`) — which this design
  replaces with `theme.textMutedDim` anyway. And no Controls type is a ruled heading.

### Traps recorded

- **ImageMagick hex is `#RRGGBBAA`, Qt's is `#AARRGGBB`** — and ImageMagick's 16-bit internal depth
  inflates AE even between identical 8-bit colours. Compare at `-alpha off -depth 8 -colorspace
  sRGB -type TrueColor` (the AE=0 above is at matched depth).
- AE=0 still cannot see a pointer (the contract's own table): the cursor claims are photographs,
  per state, with the re-enter frame AE=0 against the acceptance frame as the stability check.

### Verification

| gate | result |
| --- | --- |
| builds | Debug **0 warnings**, Release **0 warnings** (logs in `scrub-spike/gates/`; artifacts rebuilt after the edit) |
| units | `scripts/run_suites.sh` **16/16 Debug, 16/16 Release**, exit 0 both (orchestrator re-run; includes `picker_keyboard` and `media_exporter_e2e`) |
| cursor | three edit states photographed I-beam after / SizeHor before; rest SizeHor both (AE=0) |
| selection | live-object tokens exact; rendered blend AE=0 after / 66-px fail before |
| field crops | harness symmetric at baseline (old-vs-new AE=0); fix delta in edit only (AE=640, 0.37%); rest unchanged (AE=0) |
| catalogs | untouched, `grep -c unfinished` = 0 |

Evidence: `.git/dsh-scratch/scrub-spike/` (`out/before` the baseline set, `out/after` the fixed set
incl. `_edit_hover`/`_edit_reenter`, `gates/` the gate logs; `run.sh` extended with the jiggle and
re-enter states).

## The host question, decided: keep the `QQuickWidget` (2026-09-30, measured on the live Wayland session)

The last open architecture question — host the export dialogs in the existing `QDialog` +
`QQuickWidget` shell, or in real QML windows — turned on exactly one unknown: whether in-scene
popups and tooltips can work from a `QQuickWidget` host on Wayland ("Not settled" item 1), and how
the positioner treats them at the window edges (item 2). The user authorized driving the live
Wayland session for it; a two-variant spike (`--host=widget|window`, the real `AeComboBox.qml` and
`AeToolTip.qml` via symlinks) answered it.

### Measured — in-scene placement is identical in both hosts, on both platforms

(Only the window-backed popup differs, and not by host: see the backend correction below.)

Zero-input selftest (programmatic `popup.open()` / `shown = true`) on Wayland-native windows, once
per host; the same sequence under Xvfb/xcb exercised the real pointer path. **Every geometry line
identical** across host × platform (raw logs `host-spike/out/selftest/*_app.log`):

| probe | logged geometry | verdict |
| --- | --- | --- |
| combo popup at the bottom edge (G38) | `x=20 y=108 w=200 h=220 win 500x400`, settled same — baseline below was y=366 | the positioner **flips above** and stays fully in-bounds — G38 satisfied from either host, Wayland-native included |
| `AeToolTip` at the right edge (E28) | `x=290 y=142 w=324 h=22` → spans 614 of 500 | **114 px overhang, clipped**, state at the time of this selftest — the `AeToolTip.qml` of `31cfb2d` had no flip and no clamp (`x: parent.width/2 - width/2`); identical in both hosts. **Fixed later by `9abcea2`** ("the bubble keeps itself inside"): the flip chooses the side, the clamp guarantees the fit, so the shipped tooltip no longer clips. The measurement still stands as evidence that *placement is host-independent* |
| stock `Controls.ToolTip` (E27 baseline) | `x=180 y=183 w=140 h=29`, `popupType=0 sameWindow=true` | an **in-scene Item popup**, placed **above** its owner even with room below — E27 wants below-first, so a delegate must place it, stock does not |
| `Popup.Window` | the same diff box `620x30+0+420` (`AE=18600`) under **both** hosts on xcb | **corrected 2026-10-03**: it *is* a real window painting outside the host's rect (its left edge 200 px left of the host, `OVERGEO … left=200`); on Wayland, mapping it from a `QQuickWidget` depends on the compositor's **backend** — see below |

### The ruling, and the decision it yields

The E28 wording was put to the user: "The design here allows for either. This is nuanced." — both
resolutions (overhang unclipped, or flip/shift inside) satisfy the line; clipping is the defect.
Consequences, in order:

1. No contract line requires a tooltip that overhangs the dialog ⇒ no line requires window-backed
   tooltips ⇒ **no line requires the real-QML-window host** (its only measured advantage on Wayland
   is mapping `Popup.Window`, which the design does not need — and which, per the backend
   correction below, is not even a property of the host).
2. The chosen resolution in this host is **flip/shift-inside**: it is plain positioning code ("ours
   either way") and works identically in both hosts — so the fix for the 114 px clip is the tooltip
   increment's clamp work, not a host migration.
3. **Verdict: keep `QQuickWidget`.** By the settled principle — use the abstraction we already have
   unless a contract line says otherwise — and against the measured evidence there is no line that
   says otherwise. **The reopen condition** was: wanting the overhang look for tooltips would
   require window-backed tooltips, which on Wayland requires the real-window host (and, per the
   backend correction below, a compositor backend that maps popups at all).

**RULED 2026-10-03, the user — the overhang look is NOT wanted; keep the in-scene flip/shift.** The
condition above is therefore not triggered and the host question stays settled. The question was put
only after the measurement half was finished (`c30c6e5`), because the condition had been sitting on
the user's desk since 2026-09-30 with its premise unmeasured. It was decided against a *measured*
cost: the overhang needs a window-backed tooltip, which on Wayland needs both a real-window host and
a compositor backend that maps popups, so it would have degraded on exactly the hosts that cannot.
Contract line E28 is satisfied by the resolution already shipped — `AeToolTip` flips the side and
clamps to the fit-rect (item 2 above, landed in `9abcea2`) — and no code changed for this ruling.

### Correction, 2026-10-03: "The Wayland limitation is a backend limitation"

The table row above said "never mapped from a `QQuickWidget` on Wayland" on the strength of a single
measurement that was never re-run, and the ruling weighed it as a property of the *host*. Re-measured
2026-10-03 with a purpose-built spike (`host-spike/popup-spike.cpp`, `--host=widget|window
--popup=item|window --open=0|1 --open-ms=N`), weston **15.0.1**, Qt **6.11.2**, always in a private
`XDG_RUNTIME_DIR` and a private socket — never the live session:

| host | compositor | `Popup.Window` maps? | evidence |
| --- | --- | --- | --- |
| `QQuickWidget` | weston `--backend=headless` | **no — 0/9** | `out/count-headless/` (desktop shell, 0/6), `out/count-headless-kiosk/` (0/3), `out/delay-headless/` |
| `QQuickWidget` | weston `--backend=x11` | **yes — 9/9** | `out/count-x11/` (6/6), `out/count-x11-kiosk/` (3/3) |
| `QQuickView` | weston `--backend=headless` | **yes — 9/9** | the same two directories, `window_*` |
| `QQuickView` | weston `--backend=x11` | **yes — 9/9** | the same two directories, `window_*` |
| either host | xcb (Xvfb) | **yes** | `out/popup_at200/`, byte-identical across hosts |

- **Not timing.** `delay_sweep.sh headless 300 1200 3000 5000` (`out/delay-headless/`): `mapped=0
  closed=1` at every delay, with an identical settle line (`TIP settled x=0 y=0 w=620 h=30
  visible=false popupType=1`) and an identical client warning — `qt.qpa.wayland: Failed to create
  popup. Ensure popup  QQuickPopupWindow(0x…) has a transientParent set`. A 16× spread of settle
  time changes nothing.
- **Not the shell**: reproduced under weston's default desktop shell *and* `--shell=kiosk`.
- **Not the client's GL/EGL integration.** `WAYLAND_DEBUG=1` wire (`wl_trace.sh`,
  `out/trace-headless/wire.log`, `out/trace-x11/wire.log`) shows the same mesa bootstrap on both
  backends — same registry order, same globals (`wl_compositor` v5, `wl_subcompositor` v1,
  `wp_viewporter` v1, `zxdg_output_manager_v1` v2, `wp_presentation` v2) — and no MESA/EGL/driver
  error line in either log.
- **What the wire does show.** *Headless*: the host toplevel is created and the client **never sends
  `get_xdg_popup`** — one `get_xdg_surface`, two surfaces, no `create_positioner`, no
  `xdg_popup.configure`. *x11*: the full handshake — `get_xdg_surface` ×2 (three surfaces),
  `create_positioner`, `xdg_surface#27.get_popup(xdg_popup#35, xdg_surface#25, xdg_positioner#23)`,
  `xdg_popup#35.configure(0, 210, 620, 30)`, `ack_configure` — and the popup is parented to the
  **host widget's** toplevel (`#25`), not to the scene-graph offscreen window, which on *both*
  backends gets a `wl_surface` and **never a role**. So the refusal happens inside Qt before any
  protocol call, and weston's own log contains no popup/xdg/error line at all.
- **Not claimed: the mechanism.** *Why* the same client window tree resolves a transient parent
  against the x11-backed compositor and not the headless one is **not** determined; the wire stops
  at the client boundary, so it lives in `QQuickWidget`/`QWaylandWindow` parent resolution and would
  need client-side instrumentation. The table is the finding.
- **On xcb there was never a host difference to find.** `out/popup_at200/` gives `widget/item` =
  `window/item` = `AE 14820`, box `494x30+206+234`, and `widget/window` = `window/window` =
  `AE 18600`, box `620x30+0+420` — the same images to the byte. The window-backed popup's rect sits
  200 px to the **left** of the host (`OVERGEO popup=0,420 620x30 right=-80 left=200 top=-300
  bottom=-70`), i.e. it really does paint outside the host's own X window, which an item popup
  cannot do (its box lies wholly inside the host's 200..700 span). Positions in that no-WM capture
  are not the app's requested ones; the host/window *identity* is what the pairs prove.
- **Harness traps paid for here, so nobody re-derives them.** (1) `weston-screenshooter` is a dead
  end on this box: on the headless backend it prints `Output capture error: unauthorized`, then the
  shm buffer format and size, and **writes no file**, exit 0 — hence the weston-on-X11/Xvfb capture
  path. (2) `--shell` takes the shell's **base** name: `--shell=kiosk` loads `kiosk-shell.so`, but
  `--shell=desktop-shell` resolves to `desktop-shell-shell.so`, which does not exist — weston exits,
  the harness takes its `FAIL no weston socket` path (which prints no rows), and both
  `out/count-*-desktop-shell/` directories contain a weston log and nothing else. Use
  `--shell=desktop`. (3) `pkill -f weston` **and** `ps | grep weston`-derived PID loops both match
  the invoking shell's own command line (the second time the pattern sat inside an `awk` program
  text and SIGTERM'd the shell mid-loop): filter on `ps -eo pid,comm`.

**Effect on the ruling: none, and it now rests on firmer ground.** The host difference the ruling
weighed was Wayland-only even before the backend split, and in shipping code **no QML file uses a
window-backed popup at all** (`grep -rn popupType src/gui/qml` matches one comment, no code) — the
in-scene, item-backed tooltip meets E28 by flipping/shifting, never by overhanging. So the reopen
condition stands exactly as stated: wanting the *overhang look* is a design change, not a defect.

### Verification honesty

- Placement/flip/clip: **Wayland-native, measured** (selftest logs above; screenshots via
  `cosmic-screenshot --interactive=false --notify=false`).
- Input semantics (real hover reveals the tooltip after the 500 ms delay, real click opens the
  combo): exercised under Xvfb/xcb only. Wayland-native input delivery through `ydotool` did not
  land (its absolute-move/acceleration caveat) and was NOT retried past the budget — a re-run with
  pointer acceleration disabled is available if Wayland input-path proof is ever wanted. The
  placement math itself is platform-independent Qt Quick code, and the measured zero divergence
  between platforms supports that.
- Disturbance (user-facing disclosure): three brief spike windows ≈45 s + two selftest windows
  ≈50 s, one ~5 s window from a mechanics check that leaked onto the XWayland desktop (should have
  used `xvfb-run`), and one driver bug whose garbage targets injected ~18 synthetic events near the
  screen's left edge. No typing beyond Escape, no file dialogs.

## Post-R1, landed: Escape belongs to the picker policy and the edit's first moment (B10, F34)

The first post-R1 increment, both lines decided by the user 2026-09-30. Tests are the evidence home
here (`tests/picker_keyboard.cpp` extended, `tests/scrub_escape.cpp` new — auto-discovered by
`run_suites.sh`, so the suite count is now 17): keys are observable headlessly, so every behavior
below is a **failing-on-regression assertion**, not a photograph.

### Measured first: three routing facts the design rests on

Scratch probes (`escape-spike/`): a key sent straight to an item reaches only **that** item's `Keys`
filters and editor and **never bubbles to ancestors** (direct send is a same-item probe), while a
key sent to a `QQuickWindow` under `offscreen` goes to the `activeFocusItem` and **does** bubble up
the parent chain (so window delivery is the honest end-to-end route, headlessly), and specific
`Keys.onXPressed` handlers auto-accept while generic `Keys.onPressed` does not. Hence B10's policy
sits on the scene **root** and the tests drive it **through a window** — the route is under test,
not just the policy.

### Before → after

| scenario | before | after |
| --- | --- | --- |
| picker Escape, text present | text kept; key leaks to the shell (which cancels — with text present)* | clears text + query, hidden rows return, **not** rejected |
| picker Escape, field empty | leaks to shell → closes | `model.rejected` (= the shell's `QDialog::reject` — the Cancel route) |
| same two steps with LIST focus | n/a (no policy existed) | identical clear-then-close |
| F34: type "12", owner re-syncs `value` 95→50 mid-edit, Escape | **keeps 50** (the property at Escape time) | restores **95**, owner re-synced, edit exits |
| F34: committed 30, second edit, Escape | already right (keep-committed == edit-start absent a re-sync) | restores 30 — not 95 ("dialog open") |
| F34: sentinel "Auto" (−1), type "30", re-sync to 7, Escape | **keeps 7** | restores **−1** and the placeholder word returns |
| F34: plain cancel / Enter commit | already right | unchanged |

\* The before column's shell close is the known `QDialog::reject`-on-unaccepted-Escape path plus the
measured "the scene leaves Escape unaccepted"; the model-level observable cannot see the shell's
close — recorded so nobody reads more into the row than was measured.

### The F34 rule the implementation embodies

`editStartValue` is snapshotted where the edit **begins** (both entry routes: `beginEdit()` and the
editor's focus-gain), and Escape restores *that* — "keep the committed value" was the old wording
and is only equal to it while nothing re-syncs `value` mid-edit (the dialog re-syncs on aspect locks
and range clamps; the a11y setter writes the same path — measured). The restore is a scrub-style
write: `changed()` fires only if the value actually moved, `committed()` never fires (it has **no
consumers** — verified before relying on that).

### Regression proof (deliberate breaks, run, reverted)

Removing the F34 restore failed exactly its 4 restore assertions (value/owner/sentinel/word);
removing the B10 clear failed 7 (both clear assertions and the close coupling — without the clear
the field is never empty so nothing closes, in both focus states). Clean reverts, 0 failures.

### Harness lesson this increment bought: the canary false-FAIL

`probe_inputs` opens an OS file dialog by design, so the portal canary ran first and said FAIL —
**wrongly**. The journal grep for `file_chooser` was matching the harness's own `[systemd-run]`
scope START lines, which log the command **text**: any command containing the word contaminates its
own canary window (measured: 4 hits in a clean window, all echoes — one was the check itself — 0
real portal lines; the only genuine portal line all day preceded this work). Fixed in
`canary_dialog.sh`, `sweep_runner.sh` (both checks) and the AGENTS.md recipe with
`grep -v '\[systemd-run\]'`, keeping the broad match so real leaks still trip it.

### Verification

| gate | result |
| --- | --- |
| builds | Debug **0 warnings**, Release **0 warnings** (affected units force-recompiled with kept logs) |
| units | `scripts/run_suites.sh` **17/17 Debug, 17/17 Release**, exit 0 (orchestrator re-run; `scrub_escape` in both) |
| regression proof | deliberate breaks caught by exactly 4 and 7 assertions |
| targeted probes | `dest`, `inputs`, `axes`, `quality` **4/4 PASS** (~8 min; `noproject` skipped — its Escape asserts the picker's absence, untouched here) |
| catalogs | 0 `unfinished` ×6; no strings changed |

## Post-R1, landed: the tooltip set is two kinds, and the bubble keeps itself inside (E29, E27, E28)

The decided set (user, 2026-09-30) plus the placement lines. Mechanism stays the design's own
in-scene `AeToolTip` (item 30: drawn above its owner in z, never intercepts the pointer); what
changed is **who has one, what it says, and where it sits**.

### E29 — the set, per call site (measured before judging)

| call site | before | after | why |
| --- | --- | --- | --- |
| 5× Auto/Reset affordances (3 `AeNumberField` resets + size "Reset to native" + bitrate "Reset to auto") | tooltip = `resetLabel` | **KEPT** | the decided kind (b); strings unchanged — "Reset to …" is the user's "along the lines of 'Reset to default'" |
| aspect toggle "Keep aspect ratio" | tooltip = label | **DROPPED** | a toggle, not a reset (link icon toggling a flag) |
| `AeTextField` Clear | "Clear" | **DROPPED** | not one of the two kinds |
| `AeFrameRangeRow` remove | "Remove range N" | **DROPPED** | not one of the two kinds |
| picker rows | family+id, **always** | **KEPT but gated + enriched**: only when the row label actually elides (`Text.truncated`), text = the full label ahead of the identification | the decided kind (a): "tooltip only where text can be truncated" |
| manual button | explanatory prose | **DROPPED** (instance removed) | neither kind; `AeButton` has no elide (measured) so it can never truncate. **Judgment flag**: the model property + translated string are left in place — if the user ever wants explanation tooltips back anywhere, this is the one to restore. |

Accessible names are kept everywhere (a dropped tooltip loses hover text only). The gate is explicit
per call site (`AeIconButton.showTip`, default false) — the set is a declared list, not a side
effect.

### E27/E28 — placement: below-first, flipped, clamped

Interpretation recorded: **E27 governs placement** ("below, or above when there is no room, not at
the pointer") while **item 30 governs layering + hit-testing** (drawn above = z; does not intercept
the pointer) — the two are not in conflict under that reading, and the tooltip's own header now
says so. (If the design intends the old always-*above* look instead, it is a one-line revert in
`AeToolTip` and a contract edit — flagged, not assumed.)

Measured at real call sites in a 500×400 window (`tooltip-spike/`, re-runnable):

| site | before | after |
| --- | --- | --- |
| reset at the left edge | `winX=−8` — **clipped** | `winX=0` — clamped inside, and **below** the owner |
| owner near the bottom-right corner | `winX=433..523` — **23 px cut** | `winX=410..500` — clamped + **flipped above** (no room below for a 370 px owner) |
| every site | above the owner always (E27 violation) | below-first, `y = ownerBottom+6` exactly, flip only when needed |

The bubble also grows for multi-line text (the composed picker tooltip is 3 lines; the old fixed
22 px height would itself have clipped it — single-line look unchanged). E26 timing is **pinned by
asserts**: no reveal at 150/250 ms, reveal after the dwell, both for a driven tip and a real
hovered reset button.

### Regression proof (deliberate breaks, run, reverted)

Removing the clamp failed the E28 asserts in `tooltip_contract` **and** the picker's E28 assert
(cross-catch); removing the truncation gate failed "an untruncated row gets NO tooltip". Reverted:
all green.

### Finding routed to the overflow increment

The picker's list will not shrink below its content's natural width (measured `rootW=180` with
`listW=211` = the widest row's natural width, at every window width): ColumnLayout minimums
overflow and labels are **cut by the viewport clip instead of eliding**. D25 must check the same
content-minimum behavior on the form side.

### Verification

| gate | result |
| --- | --- |
| builds | Debug **0 warnings**, Release **0 warnings** |
| units | `scripts/run_suites.sh` **18/18 Debug, 18/18 Release**, exit 0 (orchestrator re-run; `tooltip_contract` + `scrub_escape` auto-discovered) |
| regression proof | clamp break → 3 asserts fail across 2 suites; gate break → 1 assert fails |
| visual/measured | before/after geometry dumps at real call sites (`tooltip-spike/`), clip −8→0 and 433..523→410..500 |
| targeted probes | `dest`, `inputs`, `dialog` **3/3** (portal canary first, clean with the fixed `[systemd-run]` filter) |
| catalogs | 0 `unfinished` ×6; kept strings unchanged |

## Post-R1, landed: the form scrolls, the action row does not (D25 + Q9)

### Before, measured (`overflow-spike/`, real `ExportDialog.qml` + real `ExportViewModel`, offscreen)

| scenario | content vs page | before |
| --- | --- | --- |
| 560×640 Advanced open + 4 ranges | 630 / 558 | **+72 px CUT, no recourse** — the defect |
| 400×300 / 320×300, Advanced open | 534 / 218 | +316 px cut, +68 / +148 px horizontal overflow |
| the action row | — | was already anchored (footer y=588) — Q9's "fixed" half held; D25's scroll was missing entirely |

### What landed

- `page` lives in a clipped 2-D `Flickable` (`formScroller`; `StopAtBounds`; content grows to
  `implicitWidth/Height`); the footer is untouched and stays pinned. Wheel and drag are the
  Flickable's own (both driven in tests). Keys: focus travel scrolls the target's whole **row** into
  view (`ensureVisible`), PageUp/PageDown/Home/End page at the root (editors keep their Home/End —
  they consume first).
- A themed `ScrollBar` (the picker's D23 pattern: attached, parented **outside** the clip, no fade,
  visible only on overflow, 28 px minimum thumb). **Flagged choice**: the contract does not require
  a form scrollbar — this applies item 23/24's rules by analogy ("a scroll area with no affordance
  is its own defect"). Pixel evidence shows exactly this lane and nothing else in the overflow
  states (AE=3762/2155/1675 = lane + one 1 px AA clip-edge fix at (544,401); no-overflow states
  AE=1 — that same pixel only).
- `AeToolTip.clampRect()`: the fit rect is the window **intersected with every clipping ancestor** —
  necessary because the Flickable is clipped now (and measured: `QQuickFlickable` does NOT clip by
  default), so in-content tooltips would be cut at the viewport without it. E27 flip / E28 clamp
  unchanged in spirit, now against the visible viewport.
- **Flagged interpretation (shell)**: the window minimum/default now clamps to the available screen
  (`min(560×640, avail)`) — D25's "small screen" clause otherwise leaves a 640 px floor taller than
  the screen, pushing the action row off it. Revert in `ExportDialog.cpp` if the line is read as
  scene-only.

### Regression proofs (deliberate breaks, run, reverted)

Un-clamping the tooltip fails exactly the 2 E28/E27 asserts **at the measured old defect position**
(342,635 vs viewport 0,72 528×558); disabling `ensureVisible` fails exactly the 4 deep controls.
**One self-catch worth its own line**: the first reachability block passed with `ensureVisible`
deliberately disabled — it ran at `contentY=max`, where everything already fit. A test that could
not observe the regression was tightened (start at `contentY=0`, the destination row as a real
button) and re-proved. That is the precondition rule from the key-routing notes, caught in the act.

### Two QML traps the edge test bought (now in AGENTS.md)

`parentItem` is a C++ getter — QML walks must use `.parent` or they die silently after one hop; and
bindings cannot track `mapToItem` — x/y stay stale across scrolls until the walk explicitly reads
each ancestor's geometry (those reads are the dependencies).

### The routed finding: labels-cut-vs-elide does NOT reproduce here

`AeFieldLabel` elides (`Text.ElideRight`) with slack at the 560 minimum (w=104 vs 34–69 px text).
Below ~468 px window width the layout minimums overflow **horizontally** (measured +68/+148) —
covered by the same line ("never exceeds **without a scroll**") via the 2-D scroll host. Nothing
fixed because nothing was broken.

### Verification

| gate | result |
| --- | --- |
| builds | Debug **0 warnings**, Release **0 warnings** |
| units | `scripts/run_suites.sh` **19/19 Debug, 19/19 Release**, exit 0 (orchestrator re-run; `form_layout` in both) |
| regression proof | un-clamp → exactly 2 asserts; no-`ensureVisible` → exactly 4; both reverted green |
| visual | before/after per scenario (`overflow-spike/out_before` vs `out`); no-overflow AE=1 (the clip-edge pixel), overflow AE = the flagged scrollbar lane |
| targeted probes | canary PASS (fixed pattern), `dialog`/`export`/`format`/`inputs` **4/4** (`probe_scroll` measured to drive the PICKER's bar, not the form — substituted deliberately) |
| catalogs | 0 `unfinished` ×6; no string churn |

**Three flagged choices for the user** (all reversible): root-level key paging, the added form
scrollbar, the screen-clamped window floor.

## Post-R1, landed: the ghost fill was black in its RGB — C21, reproduced then removed

*"No row paints a wrong intermediate colour… with no black or unstyled frame"* (item 21). The
review's "Add range hover flashes black" now has both a reproduction and a root cause, measured.

### Reproduced twice before touching anything

- **Deterministic** (real dialog + real view model offscreen; `QHoverEvent` posted to the window —
  the proven hover route — per-frame read of the background Rectangle's own colour): hover entry
  went `#ff000000 → #ff020202 → #ff0a0a0a → #ff171717 → #ff292929 → #ff404040` over ~86 ms; leave
  mirrored it downward. Entries 1–5 identical.
- **Real pointer** (Xvfb, 30 fps video, 5 entries on the real Add-range button, 316 frames): the
  **leave** transition dipped below the base on screen — (31,33,30) vs base (37,37,37) — visible
  (`hover-spike/out/crop_0082.png`). Honest caveat: at 30 fps the entry fade fell between frames;
  entry evidence is the deterministic instrument + the test's 8 ms sampling.

### Root cause and fix (one line)

`Behavior on color { ColorAnimation { duration: 80 } }` lerps **straight RGBA**, and the ghost's
rest fill `"transparent"` = `rgba(0,0,0,0)` carries **black** in its RGB — so every fade passed
through dark semi-transparent fills. Fix: the rest fill is now
`Qt.rgba(theme.raised.r, theme.raised.g, theme.raised.b, 0)` — zero alpha in the **hover colour's
own RGB** — so the fade is a clean alpha ramp. Signature proof: before, the grab timeline is
quadratic-ish (RGB *and* alpha ramping 2,10,23,41,64); after, linear alpha-only (13,26,38,51,64).
Only `AeButton` has this class (`AeIconButton`/`AeCheckBox` have no `Behavior`/`ColorAnimation` —
grep-checked; instant swaps cannot flash).

### Proof and gate

Settled visuals byte-identical (AE=0 before/after, rest and hover); first entry == fifth; no
transition frame leaves the base↔hover range after the fix. `tests/hover_parity.cpp` (suite #20)
samples the **animated property** every ~8 ms: RGB-constancy through both fades, a black-frame
detector, first==fifth parity, the invisible-rest rule, and the secondary variant's fade staying
within its endpoints. Deliberate break (`"transparent"` restored) fails exactly 5 asserts.

**Instrument note worth keeping**: `grabToImage` composites the item onto **opaque black** — it
hides alpha and turns every fade into a climb from 0, i.e. it is blind to this whole defect class.
The gate samples the property, not the grab.

### Verification

| gate | result |
| --- | --- |
| builds | Debug **0 warnings**, Release **0 warnings** |
| units | `scripts/run_suites.sh` **20/20 Debug, 20/20 Release**, exit 0 (orchestrator re-run) |
| regression proof | `"transparent"` restored → exactly 5 asserts fail |
| visual | settled AE=0 both states; timeline signature quadratic→linear; video frames + crops |
| targeted probes | canary PASS, `dialog`/`inputs`/`export` **3/3** |
| catalogs | 0 `unfinished` ×6 |

## Post-R1, landed: the picker remembers its platforms and the dialog its size (C17 + A5)

### The headline finding: stable keying was impossible as shipped

Section rows never carried their identity — `ExportGroup::id` exists ("matches
`ExportTarget::group`") but the model copied only `title`/`icon` into the section row, so
`idOf(sectionRow)` returned `""` for every platform and "remember which groups were open" could
only have remembered **row numbers** (the wrong platform in a reordered tree). Caught by the first
test run (saved ids came out empty), fixed with one line (`section.id = g.id`). Proof it is real
keying: `'bluesky'`'s state restored correctly across a **reversed table where its row moved
5 → 77**.

### What landed

- **C17**: `expandedIds()`/`applyExpandedIds()` in `DestinationPickerModel`, `expansionChanged()`
  on every change (toggle, a search opening what it found, preselection), saved to
  `picker/expansion` on every change + at `done()`; absent key = first run = collapsed. A restore
  guard prevents write-back echo. **Preselection invariant preserved**: a restore reopens the
  current row's platform — C17 decides which groups start open, never that the current answer may
  be hidden.
- **A5**: `exportdialog/size` restored at construction (first open = designed default 620×720,
  screen-clamped like the default), saved at `done()`. **Position is not persisted** (the
  compositor's business on Wayland).

### Behavior table

| scenario | before | after |
| --- | --- | --- |
| first open (no key) | collapsed (accidentally — no ids to save!) | collapsed **except** the preselection's platform (the invariant); default size |
| toggle groups, restart | never remembered | the same **platforms** reopen (stable ids, verified across the reversed table) |
| search opens groups, clear, restart | n/a | search-opened state persists ("as the user left it"); clearing never closes |
| remembered state would hide the selection | n/a | the invariant reopens it (M4) |
| dialog size, restart | always default | the closed-at size returns (700×680 round-trip; 20000×20000 clamps to screen) |

### Regression proofs (deliberate breaks, exact counts)

Break the SAVE → 2 failures; break the APPLY → 1; break the MACHINERY → 3; reverted → all 27 green
(`persist_state` M0–M5, D1–D3, A5×3, P1–P3 + preconditions).

### "Not settled" item 3 — closed

Picker section + expansion + focus, in the design's own terms: header click/Enter toggles and never
selects (B16, incl. search-focused); arrows never expand while typing (B9/C16 — `picker_keyboard`);
a search opens what it found and clearing never closes (M5); collapsed rows are unreachable by
keyboard and open rows are (C20 — P3 + B7/B8); childless top-level rows unindented (C18); first
hover == fifth with no black frame (C21 — `hover_parity`); the preselection stays on screen through
any restore (M4). The line is no longer open.

### Scope decisions flagged (each one line to change)

1. **A5 covers the export dialog only** — the decided question's scope ("Should the export dialog
   remember its size?"). The picker dialog's size is NOT persisted; `picker/size` would be a
   5-line copy.
2. **First-open "all collapsed" is literal only when there is nothing to preselect** — with a
   remembered/current destination its platform opens (the preselection invariant predates C17 and
   forbids hiding the current answer).
3. Save policy = on every change **and** at close (the close-save is the belt).

### Sandbox interplay (routed)

Probes run with `XDG_CONFIG_HOME=$(mktemp -d)` (`_lib.sh`), so every probe starts "first run" —
deterministic. Within a single run, close+reopen now remembers state; `probe_reexport`'s "Export
again" is the one candidate that could notice. The full sweep at this increment is the check; if it
trips, the probe's expectation is stale, not the persistence.

### Verification

| gate | result |
| --- | --- |
| builds | Debug **0 warnings**, Release **0 warnings** |
| units | `scripts/run_suites.sh` **21/21 Debug, 21/21 Release**, exit 0 (orchestrator re-run; `persist_state` in both) |
| regression proof | save/apply/machinery breaks fail 2/1/3 asserts exactly |
| targeted probes | canary PASS, `dest` 98s, `dialog` 92s (agent run; the full sweep supersedes at this increment) |
| full sweep | run by the orchestrator at this increment (shell/dialog wiring per the gate policy) — result in the run log |
| catalogs | 0 `unfinished` ×6 |

**Test caveat (honest)**: dialog-level tests construct the real `gui::DestinationPicker` /
`gui::ExportDialog` (real ctors, keys, close path) but their QML scenes live in the app's
`qt_add_qml_module` and load empty in test binaries — the state machinery is fully real; the
scene's own routes are covered by `picker_keyboard`/`tooltip_contract`/probes.

## Post-R1, landed: the codec row speaks for the destination (I44 — a list, not a mode)

### Schema

`codecSelect fixed|ladder` (a mode) is gone. Each destination row now declares `codecs=` (accepted
**format** keys, preference order) + `default=` (declared default; absent = the class policy,
**constrained to the list** — the same "empty = derive" convention `AudioSpec` uses). `codec=`
survives only as an internal **encoder pin** (safari.transparent's `hevc_videotoolbox` needs one).
66 rows migrated: 50 dictated (`codecs="X" default="X"`, pins untouched), 8 merely-defaulted by
hand, 8 image rows (none). The codec row offers `accepted ∩ build-capable` — one entry per
**format** ("H.264/H.265/AV1") — with the default preselected; dictated rows keep the row **hidden**
(item 42: "stated, not offered"). The user's choice travels: `ExportRequest.codec` → `planExport`,
validated against the offered list (a stale pick cannot smuggle a refused codec), and the plan's
exact/hardware encoder passes through until the user actually pins one. `src/gui/MainWindow.cpp`'s
4 lines are the wire that makes the row non-decorative — without `request.codec = …` the reviewed
defect would merely move from the schema to the UI.

### The intersection table (facts ∩ this build → offered + default)

| row | accepted (source) | offered here | default | note |
| --- | --- | --- | --- | --- |
| chat.discord.file | h264 hevc av1 (review I44 names the trio) | H.264 H.265 AV1 | h264 (declared) | the reviewed case |
| post.instagram.reels | h264 hevc (its fact) | H.264 H.265 | h264 (declared) | declared beats the policy's H.265 |
| post.pinterest.video | h264 hevc (its fact) | H.264 H.265 | h264 (declared) | |
| post.vimeo.video | h264 prores (its fact) | **H.264 only** | h264 | honest intersection: this build cannot put ProRes in mp4 |
| post.x.video | h264 (its fact) | H.264 | policy→h264 | **delta**: ladder used to export AV1 at efficiency buckets — its facts name only H.264 |
| youtube.video | h264 hevc av1 (its fact) | H.264 H.265 AV1 | policy→av1 | **delta at Lossless**: was FFV1; FFV1 is not YouTube-accepted per facts — the policy is now list-constrained |
| archive.master | ffv1 hevc (its own note) | FFV1 H.265 | Lossless→ffv1 else hevc | **delta at non-lossless**: ladder picked AV1, contradicting the row's own note — the note is now literally true |
| website / generic | h264 hevc av1 (design rows) | all three | policy→h264 | unchanged |
| 50 single rows | their one codec | 1 (or 0) | declared | dictated → no combo (item 42) |

Build-capable here: h264/hevc/av1/ffv1/vp9/webp/apng/gif ✓, prores ✗. The three deltas are
constraint 1 applied ("declare what the facts support") — each is one attribute to revert.

### Verification

- `media_export_target`'s new I44 section + `export_form`'s row/re-sync blocks: the intersection
  rule per row, declared-default-beats-policy (the reels case where they diverge — discord's
  declared H.264 equals the policy's, so the first net was blind and was strengthened),
  list-constrained policy (YouTube no longer receives FFV1 at Lossless), `request.codec` reaches
  the plan, a refused codec falls back to the default, exact-encoder passthrough survives,
  destination switch re-syncs options and lands on the new default while dropping a user's pin.
- Deliberate breaks: ignoring the declared default fails exactly its named assert; ignoring the
  user's choice fails exactly "the codec row's choice reaches the plan". Reverted: all green.
- Gates: builds **0 warnings ×2**, suites **21/16→21 both configs** exit 0, `ExportTargetStrings.h
  up to date (151 strings)` = **zero string changes** (attribute-only edits; no lupdate), catalogs
  0, targeted probes `format`/`axes`/`quality` **3/3** (no OS dialog → no canary needed).

### Flagged for the user (batch)

1. The three fact-constrained deltas above (X, YouTube@Lossless, archive@non-lossless).
2. Placement: the offered codec row stays at its Encoding call site (contract G groups codec with
   format/quality; item 42's "under Advanced" clause is evidenced against the capabilities). The
   strict reading of item 42 would move it under Advanced — one QML move either way.
3. hevc-into-mp4 rows don't carry the `hvc1` tag (only safari.transparent pins it) — not a
   regression (the ladder didn't tag either), facts silent; one attribute per row fixes it.

## Post-R1, landed: the size cap asks first (I43 — opt-in and settable)

### Before, measured

`ExportTarget::Caps::maxBytes` fed `MainWindow::exportProject`'s gate → `exportWithFit()` **silently
fitted every export** under the destination's declared cap (encode/measure/re-encode bisections).
The status message already stated the guidance as fact ("…of %2 KB allowed…; OVER THE LIMIT, it will
be rejected") — the defect was the enforcement, not the words.

### After

| scenario | behavior |
| --- | --- |
| default ("Auto" sentinel = 0) | **no cap enforced**: one encode, untouched (e2e: attempts==1) |
| opt-in ("Size cap (MB)" in Advanced, the `imageQualityField` idiom + icon-only "Reset to auto" per item 35/E29) | fitted under the **user's** bytes (`GeneralParams::sizeCapBytes`, 1 MB = 1,048,576) |
| at the limit | refuse with the visible "could not fit under…" reason (unchanged — now the ONLY limit semantics; pre-agreed flag) |
| reset / destination switch | back to Auto; the cap is a user setting (global) and survives switches (flagged) |
| guidance (20/50/500 etc.) | stays stated verbatim in facts + status — a fact the user may apply |

### Findings this increment bought

- **The audit generalizes I43 beyond Discord** (flagged): Telegram's 256 KB and WeChat's 500 KB also
  stop auto-enforcing; their facts/badges keep stating the caps. The same defect class the review
  named cannot survive "never enforced silently" on other rows.
- **A prerequisite bug, found by measurement**: `AeNumberField`'s accessibility setter did
  `Math.round(Number(value))` — `spin set "Size cap (MB)" 0.25` landed as **0** (probe read-back
  proved it), and scrub drags rounded the same way for any `decimals>0` field. Fixed with a
  decimals-aware `quantize()` at both call sites (`AeNumberField` gained `decimals`, default 0 —
  every existing caller unchanged); regression check in `scrub_escape`.
- **The old probe cases were VACUOUS** — the 20-frame GIF measured 142,141 B, under every cap, so
  the old "fit" asserts were no-ops. `probe_capped` now runs heavier ranges (72/100 frames) with a
  **loud natural-size precondition** and a new **default case** whose two asserts (file lands OVER
  the declared cap + status states the guidance and consequence) are the silent-enforcement
  detector. Frame-count change justified per case; the range-assert bound relaxed 60→120.

### probe_capped edits, justified line-by-line vs contract 43

(a) `capmb`/`cframes` args + a `spin set "Size cap (MB)"` opt-in step = 43's "opt-in and settable";
(b) weigh against the USER's bytes = the cap the user applied; (c) the new Auto-case = "stated as a
fact … never enforced silently" (a silent fit makes it fit and fails the case); (d) the
facts/status asserts unchanged — the message clause is the guidance statement, not enforcement;
(e) the heavier ranges fix the vacuous cases.

### String delta and the extraction debt (corrected 2026-10-03)

> What stood here read: *"Catalogs deliberately NOT lupdate'd: a throwaway run showed a full-tree
> lupdate churns **2,151 lines/catalog**, surfaces a **54-string extraction debt (53 pre-existing)**
> and would fill the EN skeleton … with unfinished entries … Exactly what remains: one source string
> awaiting a future extraction pass."* **Every number in it was wrong, and the debt was ours to pay**,
> not the user's to carry. It is kept visible because a refuted claim must not disappear quietly.

Measured on 2026-10-03, through the build's own target and never by hand:

    ninja -C build 'src/gui/update_translations:Debug'   # the bare target name does not exist

Per catalog it reported `Found 893 source text(s) (2 new and 891 already existing)` and
`Kept 1 obsolete entries`. There was no 54-string debt: **two** strings were new — `Size cap (MB)`
and the archive rule's note rewritten for the AV1 ruling — and 891 already existed in every catalog.
`translation_en.ts` is untouched (md5 unchanged) because `en` is not a declared
`I18N_TRANSLATED_LANGUAGES` entry: it is the source-language stub, and lupdate does not fill it.
Both new strings were translated in all five non-English catalogs. The AV1 rewrite is also why each
catalog's `vanished` count rises by one: the superseded H.265 sentence is kept, as lupdate keeps it,
for a translator to read.

Re-running the target is a **no-op** (`0 new and 893 already existing`, all six md5 unchanged), so
what is committed is canonical lupdate output, not hand-shaped XML. And the words reach a reader:
a `QTranslator` over the **built** `.qm` returns both strings, under the context the application
actually asks (`ExportTargets` for the note, `AnimeEffects.Dialogs` for the label), in all five
languages, with no fallback to English — 10/10, and the same lookup in a wrong context fails, which
is the control that it is a real lookup (`.git/dsh-scratch/i18n-witness/`, orchestrator-run).

### Verification

| gate | result |
| --- | --- |
| builds | Debug **0 warnings**, Release **0 warnings** |
| units | `scripts/run_suites.sh` **21/21 Debug, 21/21 Release**, exit 0 (orchestrator re-run) |
| regression proof | readBack-drop → exactly 1 assert; silent enforcement reintroduced at the real gate → exactly the default case's 2 asserts |
| e2e boundary | Auto = 1 attempt; opt-in fits below every declared cap (only the user's number achieves it); impossible cap refuses with a non-empty reason |
| targeted probes | canary PASS ×2, `capped` **57/57**, `export` 165s, `format` 121s |
| catalogs | 0 `unfinished` ×6; both i18n checks pass (151 strings up to date) |
| full sweep | run by the orchestrator at this increment (probe change = harness change; plan completion = rung milestone) — result in the run log |

### The sweep's catch: two writers disagreed about sequence names (fixed in the same landing)

The final milestone sweep failed `probe_still` — the numbered-PNG sequence wrote "0 of 20" by the
probe's globs. **The files were never lost**: they landed as `nene0000.png…` (PROJECT stem,
0-based) instead of `probe_frames0001.png…` (TYPED stem, 1-based). Two writers disagreed on both
axes and their comments falsely claimed parity:

- the **fit writer** (`exportWithFit`'s image arm) numbered `arg(i+1, 4, …)` from the typed name;
- the **direct writer** (`Exporter::decideImagePath`) used `QFileInfo(fileName).baseName()` = the
  **project** name and the **0-based** render index.

Before I43 every capped sequence rode the fit writer (`caps.maxBytes > 0`), so the disagreement
never surfaced; I43's opt-in gate routes uncapped sequences to the direct writer — and the status
bar, a third source, named the typed file. Fixed structurally: both writers now take
`request.imageStem` (typed name first, project as fallback) and number 1-based 4-digit;
`fitRequest.imageStem` is set so parity cannot drift again. Net: `media_exporter_e2e` Scenario 7
naming asserts + **Scenario 7b naming-parity** (direct vs fit, same frames, identical name sets —
deliberate break fails exactly 4 asserts with the divergence printed). The manual PNG/JPEG
sequence hatch follows the same convention now (it was 0-based project-named — untested before).

### And the harness budget the honest cases outran

`probe_capped`'s honest cases (72/100-frame real exports with fit bisections) measure **1120 s**
standalone — the blanket `timeout 900` in `run_probes.sh` killed it mid-case even unloaded. The
budget is now **per probe** (`PROBE_TIMEOUT=( [probe_capped]=1800 )`), so one probe's honest
runtime cannot silently eat the sweep budget the others live in. Harness edit.

## Post-R1, landed: the batch rulings (2026-10-01) — archive rule, Advanced codec row, and an alpha measurement

### The measurement that decided the archive rule

Real encodes through the app's own `media::VideoEncoder` with `yuva420p`, decoded back (the suite's
side-data-aware method — a first run mis-called `libvpx` and falsely cleared VP9; corrected to
`libvpx-vp9`, matching `media_video_encoder`'s own "VP9/WebM alpha" case):

| encoder | yuva420p verdict |
| --- | --- |
| `libsvtav1` (mkv/mp4/webm/ivf) | **SILENT_FALLBACK** — encode "ok", alpha GONE (advertises only yuv420p/10le; SVT logs "color format: 8 / YUV420") |
| `libvpx-vp9` (webm/mkv) | **ALPHA_WORKS** (255/0 verified at pixel level) |
| `libx265` | **REJECTED** ("does not support alpha layer encoding") |
| `ffv1` | **ALPHA_WORKS** |

So "AV1 with alpha" is measured-forever-off in this build — shipping the unmeasured rule would
have silently destroyed transparency (the fallback is silent: the file "succeeds").

### The archive rule (`archive.master`, ruled 2026-10-01)

Offered list: `ffv1 av1 hevc vp9`. Default follows the transparency toggle + bucket:

| bucket | transparency ON | transparency OFF |
| --- | --- | --- |
| Lossless | FFV1 | FFV1 |
| Best quality | **VP9** | **AV1** |
| Balanced | **VP9** | **AV1** |
| Smaller file | **VP9** | **AV1** |

The first cut mapped "efficiency buckets" onto the ladder's own `fastFirst` split (Best → AV1,
Balanced/Smaller → H.265) and flagged it to the user as one branch swap. **The user rejected it:
"Why would the ladder pick H.265? AV1 all the way makes more sense."** Ruled 2026-10-01: with
transparency OFF the archive row takes AV1 at every non-Lossless bucket; H.265 is only the
fallback where no AV1 encoder is usable, and the `fastFirst` split no longer orders this row's
families at all. Transparency ON stays VP9 — that half is a MEASURED capability limit (table
above), not a preference. The fact text is rewritten
to state the rule (one source string replaced; `ExportTargetStrings.h` regenerated = 151 up to date;
no lupdate per the I43 precedent — catalogs still at 0). Wiring found missing en route:
`ExportRequest::allowTransparency` never reached `planExport` (the rule couldn't follow the toggle
without it) and `familyEncoders` silently returned `{}` for vp9 (the alpha-ON pick would have
fallen through).

### The codec row: a placement-only split

Measurement first: the row was ONE shared call site for both modes — and `probe_format`/`probe_axes`
**pin manual mode's row beside Format** (verified behavior). Both rulings honored by splitting the
placement: manual mode's escape-hatch row stays in Encoding (probes green again); the offered
destination-mode row sits at the top of the **Advanced** grid, directly under the transparency
toggle (one of its inputs now). Dictated rows stay stated-not-offered.

**Trap found en route (now in AGENTS.md)**: `visible: … && exportModel.manualMode()` — the CALL
form of a `Q_PROPERTY` method registers no binding dependency and the binding froze at its first
value. The property form (with `NOTIFY`) works. The last instance of it was the Quality row: the
predicate it should have had was ruled 2026-10-01 (contract item 46, section K) and the fix landed
2026-10-03 — section below.

### The Quality row: last instance of the call-form trap, and the predicate it needed

Two defects sat on the same two lines (`ExportDialog.qml:511` label, `:516` combo), both
`visible: !exportModel.manualMode()`:

1. **The call form.** `manualMode` is a `Q_PROPERTY(bool)`, so `manualMode()` was not a function.
   The binding threw and never wrote `visible`, and the property kept its default `true`: measured
   on `89da2ed`, `QT_QPA_PLATFORM=offscreen QT_FORCE_STDERR_LOGGING=1 ./build/tests/Debug/form_layout`
   exited 0 with `ALL PASS 0` while stderr carried, twice (two dialog instances),
   `ExportDialog.qml:511: TypeError: Property 'manualMode' of object gui::ExportViewModel(...) is
   not a function` and the same for `:516`. The suite passed because nothing asserted the label, and
   the row was drawn everywhere: for a PNG sequence or a still destination it sat beside
   `Image quality` as a dead control (its `enabled: exportModel.qualityInCharge` is false wherever no
   codec owns a quality control), which is exactly the shape the widget era never had.
2. **The predicate was the mode, and it should be the artifact.** The widget era's own rule was
   `setVisible({qualityLabel, qualityCombo}, !isSequence)` — the sequence, not the mode. Manual mode
   with an MP4/WebM container must keep the Encoding trio (Format + Codec + Quality); a destination
   whose artifact is a still or a sequence must withdraw the row. Ruling 2026-10-01: "How does one
   pick quality in manual mode then?" → show only the control that governs the file.

The fix is one predicate, negated: `ExportViewModel` gained `qualityVisible` = `!mImageQualityVisible`
(same `NOTIFY constraintsChanged`), and `mImageQualityVisible` is true exactly where Qt's image
writers own the artifact — `currentFormatId().startsWith("image/")` (the manual PNG/JPEG sequence
escape hatch) or a destination whose artifact resolves to Still/Sequence. So the two rows are
complementary by construction and neither mode is special-cased. Verified by the `form_layout`
assertions (manual MP4 → Quality shown / Image quality hidden; manual PNG sequence → the reverse; a
still destination → the reverse; a video destination → the first) plus an explicit
`assert_hidden /tmp/fmt_seq.txt "Quality"` in `probe_format`, which fails on the old binding and
passes on the new one.

### The conditional `hvc1` tag (ruled 2026-10-03)

Six destination rows write an MP4/MOV artifact and offer HEVC among their codecs — `chat.discord.file`,
`post.instagram.reels`, `post.pinterest.video`, `youtube.video`, `website.video`, `generic.file`. None
of them carried a `<codecTag>`, so whenever the ladder resolved to an HEVC encoder the stream got
whatever tag FFmpeg's muxer chose for it. The one row whose artifact is explicitly an Apple-facing
MOV, `safari.transparent` (`container="mov" codecs="hevc"`), had carried an unconditional
`<codecTag value="hvc1"/>` from the start — this project already treated `hvc1` as the tag that makes
HEVC play in QuickTime/Safari.

Put to the user on 2026-10-03 as a choice between "tag `hvc1` whenever HEVC enters mp4/mov" and
"keep the facts as-is and record a known limitation"; the user chose the former. *Not measured here:
no Apple client exists in this environment, so "Apple's decoder wants `hvc1` over `hev1`" is the
user's premise plus the in-repo precedent above, not an observation of ours.* The scope is therefore
the smallest thing that cannot be wrong: only a row that already declares HEVC can ever get the tag,
and only when the planner in fact resolved HEVC for that export.

Shape — an optional family qualifier on the existing element:

```xml
<codecTag value="hvc1" codec="hevc"/>
```

The *value* is static but the *whether* is dynamic (the ladder resolves a family at plan time from
the quality bucket and the encoders this build can actually use), so the condition has to be
evaluated in the planner rather than expressed as two data rows. An absent `codec` attribute means
unconditional — which is exactly how the pre-existing `safari.transparent` pin keeps working,
untouched. The schema comment in `ExportTargets.txt` lives inside a non-translatable comment block
(so it adds no catalog string) and documents the rule, including the trap that a qualifier written
as a container value (`codec="hev1"`) canonicalizes to no family and then silently never fires — a
data-entry hazard the tests cannot catch, because their negative branch asserts the tag's *absence*.

The gate, at the end of `planExport` (`src/ctrl/ExportTarget.cpp`, after the pixfmt fallback):

```cpp
plan.config.codecTag = (aTarget.codecTagCodec.isEmpty() ||
                        aTarget.codecTagCodec == formatKeyForEncoder(plan.config.codec))
                           ? aTarget.codecTag
                           : QString();
```

Two details that each took a wrong turn to find:

- **The witness must be `plan.config.codec`, read after `plan.config = fallback`.** The first draft
  keyed on a local named `codec`; that local (`:1042`) holds an *encoder name*, and
  `CodecChoice::format` is never stored in it, so `codec.format` does not compile. The pixfmt probe
  can replace the whole config (`fallback` built at `:1186-1188`, assigned at `:1222`), so a value
  captured before it can name a different encoder than the one that will be written.
- **`formatKeyForEncoder()` is the one normalizer**, not a second family test: it is what the codec
  row's eligibility (`:835`) and the request→resolved match (`:1048`) already use, and the comment at
  the gate binds the two together. Pinning an HEVC encoder through `ExportRequest::codec` is admitted
  by `:1048` (`formatKeyForEncoder(aRequest.codec) == c.format`) and stored verbatim at `:1054-1057`,
  so the pinned path ends in the same normalizer — no new equivalence class, no second spelling.

H.264 and AV1 output are unchanged **structurally**, not merely by measurement: for those exports the
gate yields an empty string, `VideoEncoder` assigns `codecpar->codec_tag` only for a non-empty
4-character tag, and no other producer exists — `MainWindow` copies `plan.config.codecTag` into the
video parameters and `Exporter` copies it once more into the encoder config; nothing else writes it.
The `archive.master` row is Matroska (`mkv`), where `hvc1`/`hev1` have no meaning, and stays
deliberately untagged.

### Verification

Deliberate break (alpha branch picks AV1) fails exactly 4 named asserts (3 unit buckets + the
plan-level toggle wire); reverted green. Builds 0 warnings ×2; suites 21/21 ×2; catalogs 0; both
i18n checks green; targeted probes `format` 124s / `axes` 134s PASS (post-split). The placement
asserts are model-level and carried over unchanged. Full sweep run by the orchestrator as the
closing rung gate — result in the run log.

## Post-R1, landed: a childless top-level destination is no longer indented as a child (C18, 2026-10-03)

Reported 2026-10-03: "in the destination list, why is a top-level item with no children indented as
though it were a second level?" That is the contract already — item 18 `[R6]`: *"a top-level
destination with no children is not indented like one with a parent."* The delegate did not have a
level to read, so it asked `isSection` instead: caret 6/24, badge 24/38, label 48/62. `isSection` is
true only for a real heading, so every destination took the child lane — including the ones that
have no parent at all.

They have no parent by design: `build()` appends a group's targets **flat**, with `parent = -1`,
when the group has one row or its rows are not variants of one platform
(`DestinationPickerModel.cpp`: `if (rows.size() == 1 || !g.variants) { … appendDestination(t, -1); }`).
That is a sectionless group — "General" and every one-row platform — not a defect. The defect was
one expression in the scene, not the model.

Fix, and it is two lines of meaning plus three of geometry: the model gains `LevelRole`
(`"level"`, derived at build time from the `parent` it already has — 0 for a section or a
destination with no section above it, 1 for a nested one; no QML walks a parent chain), and the
delegate states its horizontal position **once** as `indentPx: model.level * 14` with every lane
that statement plus a fixed offset — caret 6, badge 24, label 48. Measured, live delegate geometry,
before -> after:

| row | before (badge / label) | after (caret / badge / label) |
| --- | --- | --- |
| section, level 0 | 24 / 48 | 6 / 24 / 48 |
| **childless top-level destination, level 0** | **38 / 62** | **6 (hidden) / 24 / 48** |
| nested destination, level 1 | 38 / 62 | 20 (hidden) / 38 / 62 |

`model.isSection` survives on every non-geometric use — the disclosure caret's `visible`, the
background colour, `font.bold`, the label colour branch, `Accessible.role`, `toggleSection()` and the
`if (!model.isSection) picker.activate(index)` branch — because it still says what a row *means*
(a heading that discloses, versus a row that can be selected). It no longer says where anything is
drawn.

Verification: `tests/destination_picker.cpp` resolves the role **by name** (`levelOf()` returns -1
when it is absent), so the new C18 block compiles and fails honestly on the pre-fix tree; it asserts
both directions and sweeps the whole model for wrongly-levelled rows. `tests/picker_keyboard.cpp`
asserts the same against the real delegate geometry, with its preconditions asserted in-test (the
search field's stale filter is cleared, each row is scrolled on screen before it is read). Builds
0 warnings x2; suites 21/21 x2; catalogs 0; `probe_dest` PASS.

## Post-R1, landed: the aspect lock crashed the app (2026-10-03)

Reported from the live session as "the keep-aspect-ratio button crashes the application". The button
is not the cause; **any** width or height write while the lock is on is. A core dump was waiting
(`coredumpctl info 1437101`, 28 MB, `build/src/gui/Debug/AnimeEffects`) and it unwinds to:

```
#0  ExportViewModel::setWidth (this=0xe910080, aValue=418) at ExportViewModel.cpp:663
#1  ExportViewModel::setWidth (aValue=418)                 at ExportViewModel.cpp:665
#2  ExportViewModel::setHeight (aValue=680)                at ExportViewModel.cpp:684
    ... alternating to ...
#261283 setHeight (aValue=682)
#261284 qt_static_metacall (_c=QMetaObject::WriteProperty, _id=19)  moc_ExportViewModel.cpp:943
```

261 291 frames of **unbounded mutual recursion**, i.e. a stack overflow — not a QML binding loop,
and the frame above it is the property write the QML field performs (`ExportDialog.qml:392`/`:411`
→ moc `WriteProperty`).

There is no fixed point to converge on. 720×1172 native, width 418 → height `(1172*418)/720 = 680`
(even) → width `(720*680)/1172 = 417.7 → 417 → even 418`. The pair alternates `418×680` forever.
The one guard that existed, `if (mWidth != aValue)`, sits *before* the derivation and cannot stop a
call that always arrives with a new value; `writeSize()` also runs unprotected, so a locked edit
reaches it from either direction.

Fix: **the write-back is one way.** `setWidth` holds `mWritingSize` across its `setHeight()` call and
`setHeight` mirrors that, so the derived field is written without deriving back. Arithmetic, the
even-up rounding and `writeSize()` are untouched; no header change. Pre/post, both directions:

| run | before | after |
| --- | --- | --- |
| `export_form` under `ulimit -s 512` | RC=139, dumped core, output stops after "PASS the aspect lock is on over the crash's own 720x1172 native size" | RC=0, 1232 PASS, 0 FAIL |
| `export_form`, default stack | RC=139 | RC=0, 0 FAIL |

Both post-fix rows being green matters: the recursion is bounded, not merely surviving one stack
size. The new regression block drives the same moc `WriteProperty` path QML uses (`setProperty`) and
asserts that a locked edit still moves its partner in both directions, and that `setSizeToNative()`
lands exactly on 720×1172. Unverified corner, flagged rather than guessed: the *order* of clamping
and deriving is unspecified when a ceiling is active and the partner value was itself clamped
(reachable on `max`-mode destinations) — there was no working behaviour to match, because that path
crashed.

### Open evidence gap, deferred by the user (2026-10-03): the Wayland input path

Every run behind this contract drives input through **X11** — `scripts/gui` sends synthetic X events
with `xdotool` / AT-SPI actions inside `Xvfb`, and the widget-free suites inject `QKeyEvent`s
in-process. Nothing here has measured the **Wayland** input path: libinput → compositor → client,
with the compositor's own focus and pointer-grab semantics. A defect that lives only in that path
(a key dropped on the way to the scene's focus item, a click that the compositor routes to the wrong
surface, a dialog that never becomes activatable without a WM) is invisible to every observation we
hold, including a green 13-probe sweep.

That gap was put to the user on 2026-10-03 and **deliberately deferred**: closing it means driving
the live desktop session with `ydotool`, and the cost of a wrong tooltip or a stray keystroke landing
on the user's own session is higher than the confidence it buys. It is not an unverified contract
line — each line is backed by a captured observation or a regression test that fails on the old
behaviour — it is a **platform-confidence gap**, recorded so it is not mistaken for coverage.
Reopens if a Wayland-only input complaint ever arrives; the measurement would then be: activate the
window, drive one dialog with `ydotool`, and screenshot the pointer path with
`ffmpeg -f x11grab -draw_mouse 1` (see AGENTS.md).
