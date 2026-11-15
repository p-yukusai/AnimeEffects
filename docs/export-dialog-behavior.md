# The export dialogs: behaviour contract

This is the specification the export form and the destination picker are graded against. It exists
because the port was accepted on the wrong evidence: screenshots that cannot show a cursor, an
accessibility assertion that a control *exists*, and unit tests on the view model. None of those can
observe focus, keys, hover, drag, tooltip timing or overflow — which is why fourteen defects reached
the user with every gate green.

**The design is the specification. The implementation is chosen to meet it, not the other way round.**
Where no stock control meets a line below, the line still stands and the behaviour is written and
tested; a toolkit's limits are never a reason to weaken this document.

## Sources

- **[R]** the corrections from the review of the ported dialogs (sixteen items, numbered in the
  traceability table). These are the design speaking.
- **[D]** the **current Qt Quick exporter UI** — its look, its layout, and the behaviours it already
  specifies in its own control files. It is the design reference; it is not a first draft to be
  replaced by a toolkit's idiom. Evidence for what it already specifies lives in
  `src/gui/qml/AeTextField.qml` (theme selection tokens, `selectByMouse`, the `navigationKeys`
  forwarding), `ScrubSpinBox.qml` (`selectAll()`, `Keys.onEscapePressed`, the scrub
  `cursorShape`), `AeComboBox.qml` (Up/Down/Space/Escape), `AeButton`/`AeIconButton`
  (`activeFocusOnTab`, Return/Space) and `DestinationPicker.qml` (its own key handling and
  `HoverHandler`).
- **[Q]** design questions: behaviours that neither the corrections nor the current UI settle. They
  are listed at the end and are the user's to decide. Nothing from another toolkit's conventions is
  smuggled into this document as a requirement — an earlier draft of it did exactly that, and the
  affected lines are now `[Q]`.

The design covers **look as well as behaviour**: layout, typography, spacing, colours and states are
fixed as they stand. An implementation that reproduces them differently is not meeting this spec.

## How each line is verified

Driving the real application, never reading QML:

| Instrument | Shows |
| --- | --- |
| typed key sequences (type, arrows, tab, escape, enter, ctrl+…) | focus, routing, caret, navigation |
| click on another control / empty space | focus loss, commit, click-outside |
| `ffmpeg -f x11grab -draw_mouse 1` capture | pointer shape per region (X11 renders the cursor; `import` cannot) |
| timestamped hover frames (0 ms → wake-up delay → timeout) | tooltip delay, position, flipping, hiding |
| drag with motion, then reading the scroll position | scrollbar drag, proportion, page and wheel |
| resize to the minimum size and to maximised | overflow, clipping, layout at the extremes |
| a second capture at 1.5×/2× scaling | icon and text rendering, aliasing |

A line counts as verified only with a captured observation or an automated test that fails when the
behaviour regresses. "It looks right in a screenshot" verifies none of the lines below.

## A. Dialog level — focus and keys

1. [D] Tab and Shift+Tab move through the controls in visual order, and the focused control shows
   its focus ring. (The current buttons and combos already declare `activeFocusOnTab`.)
2. [D] Enter activates the default action (Export), Escape cancels. (The current shell wires
   accept/reject and the buttons handle Return.)
3. [R3] A click outside a focused text field removes its focus. The field's text and the list's
   selection are unchanged by that click. *(An earlier draft of this document required the
   opposite; it was written from widget convention and was wrong.)*
4. [D] The dialog is modal over the main window and transient for it, as it is today.
5. [D] The dialog restores its last size between sessions. (Decided 2026-09-30: yes — geometry is
   persisted across sessions, as users expect of desktop dialogs; the first open uses the designed
   default size.)
   Scope confirmed 2026-10-01: the EXPORT dialog's size (the picker's is not persisted); and on
   first open a remembered destination's platform opens so the current answer is never hidden —
   "start collapsed" governs the rest.

## B. Search field (picker)

6. [D] The field takes focus on open, with a visible caret; typing filters the list as you type.
7. [R1] While the field has focus, Down/Up move the LIST's highlighted row. They must not move the
   caret and must not alter the text.
8. [R1] Enter activates the highlighted row, identically to clicking it.
9. [R2] Arrows must not expand or collapse groups while the search field has focus. Decided: only
   Up/Down/PageUp/PageDown are forwarded, and they move the list's highlight. Left/Right are the
   CARET's; the field does not forward them at all, so a stray arrow while typing cannot collapse
   the highlighted platform (this was the reported defect). A group is opened and closed by a click
   on its header (B16), or by Left/Right while the LIST itself holds the focus.
10. [D] Escape clears the field first and closes the dialog only when the field is already empty.
    (Decided 2026-09-30.)
11. [R3] Clicking outside the field removes its focus and keeps its text (see A3).
12. [R4] The pointer is an I-beam over the text area. The current field specifies no cursor, so it
    inherits the arrow.
13. [D] The selection highlight uses `theme.selection` / `theme.selectionText`; the current field
    already binds both, and `selectByMouse` provides pointer selection.
14. [D] The `navigationKeys` forwarding is the design's behaviour and is preserved for the VERTICAL
    keys only (see B9): the field keeps the caret and the list gets the highlight movement.
15. [D] Decided (was Q4): focusing the field leaves the caret where it was and selects nothing, so
    typing refines the query instead of replacing it. Opening the picker is the one focus it gets,
    and there is nothing there to select.

## C. Destination list (picker)

16. [R2] A click on a group header expands or collapses it, including while the search field has
    focus, and never selects the row.
17. [D] Groups start collapsed and their expansion state is remembered across sessions.
    (Decided 2026-09-30: collapsed default, state persisted.)
18. [R6] A destination with no children is not indented as though it had a parent.
19. [D] Search matches across every group, and groups with no match are not listed.
    (Decided 2026-10-08: the match is fuzzy, and PER QUERY TOKEN — every
    whitespace- or punctuation-separated token of the query must match the row, and
    each token tolerates typos, prefixes and word fragments; a subsequence is
    matched only ever inside a single word, deliberately never across words, because
    a cross-word subsequence makes a three-letter query match nearly every capped
    row — every capped destination's search haystack ends in " capped size limit" —
    and the picker would stop narrowing. The phrase bonus is FIELD-LOCAL for the
    same reason: the row's fields are joined for scoring with a unit separator, so
    the +400 means "the query's words are adjacent INSIDE one field of the row",
    never a phrase that appears only by bridging two fields. It is measured, not
    aesthetic: with a flat join the platform title "Telegram" abutted the family
    word "sticker" in telegram.emoji's haystack, so typing the exact label
    "telegram sticker" scored telegram.emoji level with telegram.sticker and
    highlighted the earlier row, and the field boundary is what stops it. The score
    decides only which match is HIGHLIGHTED; the row order stays the table's order,
    and the highlighted row is still the row Enter answers with, B8 and C20.)
    (Decided 2026-10-08, the same day as the fuzzy rule and about the same list: the picker's own
    top is the `general` group's four rows — Video, Animated image, Image sequence, Archive file —
    and the ORDER they are listed in is designed, not alphabetical. The group is declared first
    and `variants="false"`, so per C18 its rows are listed directly rather than under a platform,
    and each of the four carries an explicit `order` the picker sorts by: ranked rows first, by the
    number the table gave them, and only an unranked row falls back to its label. The rank is
    load-bearing rather than decorative: the label is TRANSLATED, so a label sort is a different
    order in every language — it would put "Animated image" above "Video" — and a rank is also the
    only way the three generic rows can sit above the Archive row, a sibling of theirs rather than
    a variant of any platform above them. The row that claimed the top before them is gone:
    `chat.generic.video` ("A video for a chat app") declared `<size mode="max" w="1280" h="720"/>`
    and a 20 MB byte cap as the floor every chat app accepts, and neither holds for most of them,
    so the generic Video row that replaced it declares the project's own size and no cap at all,
    with each platform's ceiling left on that platform's own row. The eight labels renamed in the
    same change follow one rule: a destination listed inside a platform's section that would
    otherwise repeat the bare platform name carries its VARIANT instead — "Discord (video)",
    "Telegram (video)", "WhatsApp (video)", "Signal (video)", "Messenger (video)", "Bluesky (video)",
    "Reddit (video)", "Tumblr (video)" — because a bare platform name beside siblings that already say
    what they make is vague; a platform with a single destination is left alone, since there is
    nothing to disambiguate it from (YouTube, and likewise Vimeo, Dailymotion and Mastodon). The
    declarations and their derivation are in `docs/export-dialog-implementation-notes.md`, "the
    general trio replaces the chat-app floor".)
    (Decided 2026-10-08, about the same block of rows: a full-width RULE closes the rows that have
    no platform of their own. It sits after the WHOLE general block rather than after the third of
    the four generic rows — the Archive row is no more a platform than they are, so a rule above it
    would say the sections begin earlier than they do. Which block carries a rule is DATA: the table
    sets `separatorAfter="true"` on the group, like the grouping itself, and the scene draws one
    hairline in a 12 px band. It is not a row anyone can land on: it is not selectable, never the
    current row, not counted among the destinations or the visible rows, and it is hidden while a
    query is active, because a filtered list is a list of matches and the rule describes the
    unfiltered structure.)
20. [R1] Matches are reachable with the keyboard, not only with the pointer: the highlighted row is
    the row Enter activates and the row a click selects.
21. [R7] No row paints a wrong intermediate colour: the hover state on first entry is identical to
    the hover state on the fifth, with no black or unstyled frame.
22. [D] Hovering a row changes the highlight only; the selection changes on a click.
    (Decided 2026-09-30: hover must not select.)

## D. Scrolling and overflow

23. [R5] The list's scrollbar reflects the visible/total ratio, drags the content 1:1, pages on
    track click, responds to the wheel, and appears only when the content overflows.
24. [R5, D] Its thumb, track, hover and pressed states use the design's tokens — the same look the
    rest of the exporter UI uses.
25. [R11] The export form never exceeds the window without a scroll: when the content does not fit
    (minimum size, small screen, or the Advanced section open), the content scrolls by wheel, keys
    and drag, and the Cancel/Export row stays fixed at the bottom while the content scrolls —
    reachable without scrolling to the end. (Decided 2026-09-30.) Confirmed 2026-10-01 with its three
implementation calls: a scrollbar on the scroll area (item 23/24 rules, only on overflow),
PageUp/PageDown/Home/End paging at the root (editors keep their keys), and a screen-clamped window
minimum (the "small screen" clause).

## E. Tooltips

26. [R9] A tooltip appears after the platform's wake-up delay — never instantly — and hides after
    the platform's timeout or when the pointer leaves.
27. [R9] It is positioned relative to its control in the conventional place (below, or above when
    there is no room), not at the pointer. (Placement CONFIRMED 2026-10-01 by the user: below-first as written;
item 30's "drawn above its owner" is the z-order.)
28. [R10] It is never clipped by the dialog's edge. (Wording decided 2026-09-30, the user: "The
    design here allows for either. This is nuanced." — so both resolutions satisfy this line: a
    tooltip that **overhangs** the dialog unclipped, or one that **flips/shifts to stay inside**
    it. What the line forbids is clipping at the edge — which is what `AeToolTip` did *before*
    `9abcea2` (measured 2026-09-30: 114 px cut off, no flip, no clamp; that commit gave it the
    flip and the clamp, so the clipped state is historical, not current).
    **RESOLVED 2026-10-03, the user: keep the in-scene flip/shift; the overhang look is not
    wanted.** Asked only after the reopen condition's measurement half was finished (`c30c6e5`):
    the overhang needs a window-backed tooltip, which on Wayland needs both a real-QML-window host
    and a compositor backend that maps popups at all — so it would have degraded exactly where the
    flip/shift does not. The host ruling `31cfb2d` is therefore not reopened; the mechanism is in
    the implementation notes, "The Wayland limitation is a backend limitation".)
29. [R9] It does not appear for a control whose text already states the same thing; the set of
    controls that keep a tooltip is explicit. (Decided 2026-09-30: exactly two kinds — picker rows,
    where the name can be truncated and the tooltip shows it in full, and the icon-only Auto/Reset
    buttons, whose tooltip says "Reset to default". Every other tooltip is dropped, including any
    hint on the numeric fields.)
30. [D] It is drawn above its owner and does not intercept the pointer, as `AeToolTip` does today.

## F. Numeric fields (frames, resolution, frame rate, quality)

31. [R12] In text-entry mode the pointer over the text is an I-beam. `ScrubSpinBox` currently sets
    `Qt.SizeHorCursor` for the whole field, which is the defect.
32. [D] In scrub mode a horizontal drag changes the value by the field's step, clamped to the
    field's range, with the scrub cursor. (`ScrubSpinBox` specifies the drag and the cursor.)
    (Amended 2026-10-08, on the report "when dragging the scrubbers, the drag also propagates to the
    scroll area. This leads to the user accidentally scrolling while changing values": a drag that
    STARTS on a scrub field belongs to that field — the enclosing scroll host may not steal it — so a
    value edit can never scroll the page. The wheel still scrolls over such a field, and a drag
    starting anywhere else on the page still scrolls it.)
33. [D] `selectAll()` on entry and `Keys.onEscapePressed` are the design's behaviour. (Escape's
    effect decided 2026-09-30: restore the value the field had when this edit began — see 34.)
34. [D] Escape restores the value from when the field was focused (the start of the current edit),
    not the value from when the dialog opened. (Decided 2026-09-30.)
35. [D] The Auto/Reset affordance is a separate, icon-only control with its words in `resetLabel`
    (`AeNumberField` already specifies this). The words surface as the button's tooltip — "Reset to
    default" (Decided 2026-09-30, see E29).
    (Amended 2026-10-08: a row that reads a WORD instead of a number does so from a STATE of the
    control, never from the value being equal to a sentinel — "Auto needs to be a special value, not
    just a visual thing". Every control whose number means "let the destination decide" carries that
    state beside its value (`qualityOverrideAuto`, `sizeCapAuto`, `imageQualityAuto`), the field's
    range is then the numbers the control really accepts, and the word is drawn for the state alone.
    The superseded comparison rounded both sides, so a 0.25 MB size cap — a real cap, 256 KiB —
    painted "Auto" over itself while the export really was fitted under it.)
36. [D] Width and height keep their aspect lock; the frame-range fields clamp against each other so
    a range can never invert. (Lock ruled again 2026-10-03, with the crash report: while the lock is
    on, an edit keeps the ratio; pressing Reset re-bases the ratio to the size it just reset to, so
    the NEXT edit holds that one. The lock's own write-back was unbounded — a locked edit recursed
    261 291 frames deep and killed the process; see `docs/export-dialog-implementation-notes.md`,
    "The aspect lock crashed the app".)
    (Ceiling and caps ruled 2026-10-08, on the report "The resolution aspect ratio linking is broken,
    and so is the resolution reset. Also the default resolutions are really strange. It seems to
    default to 720 in most cases even when the projects resolution is bigger." and the ruling "It
    should default to project resolution unless a destination enforces a strict resolution limit."
    A destination that only BOUNDS the size — `Free`, or `Max` as a ceiling — does not set it: the
    Width/Height boxes hold the project's/user's size (a 1080x1080 project over `chat.whatsapp.video`'s
    1280x720 ceiling opens 1080x1080, not the fitted 720x720), the ceiling bounds the OUTPUT and is
    STATED in the Limits fact beside the byte cap ("max 1280 x 720"), and the fields carry no
    destination maximum at all, so Reset to native lands the project's size exactly and the aspect
    lock derives its partner from the PROJECT's ratio rather than the ceiling's. A destination that
    PINS the size (`Exact`, `MaxExactSide`, `MaxMinSide`) still dictates it: the boxes report the
    plan's size and stay uneditable. The per-number clamp this replaces was wrong in both directions
    at once — it hid the project's size behind the ceiling's fit AND clamped the reset and the locked
    partner against the same ceiling, which is why the pair took the ceiling's aspect. The ceiling
    itself is not lost: `planExport` bounds the rule by the request and fits the source at export
    time. And a byte cap is a MEASURED number, not a word: the table's `maxBytes` is a `qint64`, so
    nine rows at or above 2 GiB (Telegram 2 GB through Vimeo 200 GB) used to parse as "no cap" and
    lose both their Limits row and the export fit loop's bound; the evidence for both fixes is in the
    implementation notes, "a resolution ceiling bounds the FILE, not the two boxes".)
    (Ratio and reset re-ruled 2026-10-08, on the report "Looks like aspect ratio lock and resolution
    reset to native are still broken. These dont follow mental model": the lock holds the ratio IN
    EFFECT WHEN IT WAS LAST SWITCHED ON — not the project's, which made a locked pair of any other
    shape snap back to the project's on the next edit — and Reset IGNORES the lock (it lands both
    numbers at the project's size) while RE-BASING it to what it just landed on, so the next locked
    edit holds the project's shape rather than the ratio that was there before. A lock that starts ON
    from the saved preference holds the pair the form OPENED on.)
    (Decided 2026-10-09, on this clause's second half: the pair-clamp is enforced at the MODEL
    and offered at the ROW. `src/gui/qml/ExportViewModel.cpp:151-185`
    (`ExportRangeModel::setRangeFirst`/`setRangeLast`) clamps each field against the project's
    own extent and carries the neighbour along when an edit passes it, and the whole pair is
    resolved in ONE pass so neither setter can re-enter the other and start an alternation;
    `src/gui/qml/AeFrameRangeRow.qml:48-71` bounds each field's `from`/`to` by the partner, so
    the form itself offers a pair that cannot invert — first=100 on a [10..50] range reads
    100..100, not an inverted one. Both fields are re-emitted whether or not only one of them
    moved, so a view redrawing only the edited field cannot keep showing the number the pair
    no longer holds. The backend's swap-with-warning (`src/ctrl/ExportParams.h:229-238`) stays
    as the last line rather than the only one. Pinned by `tests/export_form.cpp:1132-1205`,
    written against the tree that took the number as typed.)

## G. Combo boxes (format, codec, quality intent)

37. [D] Click, Down, Space, Escape and Enter choose and dismiss. (`AeComboBox` specifies
    Up/Down/Space/Escape; the popup is the design's in-scene list.) Tab with the list open
    dismisses the list and moves on. (Decided 2026-09-29: the current UI left the list and its
    blocking scrim behind while focus moved away — an unowned accident of its implementation,
    not a specified behaviour; the dismissal is stock's and was adopted over it.)
38. [R10] The popup is never clipped by the dialog's edge, and it is positioned under its field,
    flipping above when there is no room below.
39. [D] Type-ahead: typing the first letters of an item selects it in the closed field.
    (Decided 2026-09-29: yes — the widget-era form did this; stock `ComboBox`'s `keySearch`
    supplies it.)

## H. Facts panel, layout and information architecture

40. [R13] The facts read as muted label / value pairs, one fact per row, with no row repeating its
    own value as its explanation.
41. [R12] The frame range lives under the Custom/Advanced section; the default surface stays
    uncluttered for the common case.
    (Ruled 2026-10-09: the row MOVED rather than the clause — the Frames row is now the first
    row of the Custom/Advanced section (`src/gui/qml/ExportDialog.qml:592-656`, the section's
    ColumnLayout) instead of the first row of the OUTPUT grid, so this [R12] clause is MET as
    written and was never amended; it stays gated by `rangeVisible`, so manual mode and a
    Still artifact are unchanged. With it there the default surface is shorter by the rows,
    which is the point of the move, and the D25 overflow measurements were re-taken around
    it: the Advanced-open worst case is unchanged (the rows were already inside its height
    while the section was open) and both figures are printed and re-measured by
    `tests/form_layout.cpp:13-21`, which says which of the two numbers is the historical
    before-column.)
42. [D] Controls the destination dictates are stated, not offered; controls it merely defaults stay
    reachable under Advanced. (Ruled 2026-10-01: the offered codec row belongs HERE for destination
mode — this clause governs it; manual mode's Encoding trio is unchanged.)
    (Decided 2026-10-03: transparency is not an Advanced control. It sits on the regular surface,
    below the Encoding rows, as the last regular row, and defaults OFF — turning it on moves the
    archive ladder off its own best pick, so it is an opt-in the user makes, not a state the form
    assumes. A destination's own `alpha="default-on"` still turns it on. What stays under Advanced
    is what a destination merely DEFAULTS; where transparency was the section's only resident it is
    no longer offered at all — `post.pixiv.ugoira` is the current example.)
    (Measured 2026-10-03, OPEN — the saved preference does NOT win over the plan, and this is a
    defect, not the rule: a saved `export_allow_transparency=true` is read into `Init::transparency`
    and does reach the row, but for an `alpha="choice"` destination `planExport()` resolves
    `Choice → false` and `freezeCapability()` then reads "row on, plan off" as a refusal — it clears
    the value AND leaves the box disabled, and `readBack()` writes the cleared value back to the
    setting. Measured on the landed tree: seeded `[General] export_allow_transparency=true` with
    `twitch.emote` gives value 0, enabled 0. Left as-is pending a ruling; the evidence and the
    one-line candidate fix are in the implementation notes under "the saved transparency preference
    is erased by a rule that was only ever meant for refusals".)
    (Decided 2026-10-08: the manual escape hatch has no button. The picker's footer used to carry
    "My platform isn't listed (I'll choose the format)" as an answer of its own, standing beside the
    destinations; it is gone, because the generic rows at the top of the list are what it was there
    for and a button restating them read as a fourth kind of answer. Manual mode itself is unchanged
    underneath: it is what the picker answers when the user types a query that matches nothing and
    accepts the default answer ("Continue") — the model's own empty-id fallback,
    `activate()` clearing the chosen id and accepting when the row it is asked about matched nothing
    — so the dialog still hands the export form no destination, which is what manual mode means, and
    the trio it hands over is item 46's. Both GUI probes already reached manual mode this way and
    never through the button — `scripts/gui/probe_axes.sh` and `scripts/gui/probe_format.sh` type
    "zzz no such platform" and then activate the default button, probe_format recording that it took
    that answer partly because the button's own name carries apostrophes its single-quoted probe body
    cannot address — so the surviving route is the one the probes already exercise. The model-side
    witnesses are `tests/destination_picker.cpp` and `tests/picker_keyboard.cpp`; the evidence is in
    `docs/export-dialog-implementation-notes.md`, "the general trio replaces the chat-app floor".)
    (Decided 2026-10-08, about the picker's OWN WORDS: the export surface speaks the app's usual
    dialog language — a noun-phrase title and an action verb on the confirm button — rather than
    addressing the user in a sentence of its own. The picker's title is "Destination", the button
    is "Continue", and manual mode's destination label is "Manual"; the field's placeholder lists
    its examples in the UI's own script. The old wording was the app's only question-shaped dialog
    title and its only sentence-shaped imperative, and the GUI probes address buttons by their
    accessible NAME, so a reword is a probe change as well as a catalog one. Two consequences of
    that are recorded here because they are not obvious: the manual button's name no longer carries
    apostrophes (the probe note above was written when it did), and a NEW source string has no
    catalog entry until translators fill it, so the picker's context is asserted in
    `tests/destination_picker.cpp` on the search field's accessible name — a string that has been
    in that context all along — instead of on the title it used to assert.)
    (Decided 2026-10-09: H42 is CLOSED. The GUI's transparency answer now reaches the
    destination-mode request — `src/gui/qml/ExportViewModel.cpp:1268` writes
    `request.allowTransparency = mTransparency.value()` in `buildRequest()`, and the plan's
    capability switch reads it (`src/ctrl/ExportTarget.cpp:1055`), so a `choice` row's plan
    reports the choice it was GIVEN instead of resolving `Choice → false` over an answer it
    never received. Two consequences the review recorded as dead are live again: the archive
    ladder's alpha-OFF branch (AV1 at every non-Lossless bucket — the fall-through at
    `src/ctrl/ExportTarget.cpp:978`, core-correct and unit-tested since the 2026-10-01 ruling)
    is app-reachable, and the alpha-ON side's VP9 pick reaches the codec row
    (`src/ctrl/ExportTarget.cpp:1160`). `freezeCapability`
    (`src/gui/qml/ExportViewModel.cpp:814-834`) freezes only for `Required`/`Forbidden` and reads
    a plan that refuses alpha over a choice the user made as the resolved CODEC's own refusal,
    which is what that freeze is for — the mismatch it used to misread was one the PLAN produced
    itself. Pinned by `tests/export_form.cpp:1103-1129` (the request carries the row in BOTH
    states, because ON is the one the request's `true` default already carried) and
    `tests/media_export_target.cpp:1482-1551` (the plan-side half).)
    (Decided 2026-10-11: transparency OFF now means the export's BACKGROUND is the effective
    viewport colour — the project's own override when it has one, the theme's `recessed` canvas
    token otherwise, i.e. exactly the colour the viewport shows behind the art. The artwork
    composites over it, so every pixel the art does not paint (the canvas, and any pad margin an
    exact-size destination adds) is that colour, and an alpha-less artifact carries no alpha
    there. Transparency ON is unchanged: transparent background, exactly as before. This
    replaced the previous behaviour, where the export rendered on a fully transparent clear and
    an alpha-less pixel format merely DROPPED that alpha — the background came out black in
    video and as whatever the artwork's own pixels were in a still, and the colour was never
    consulted at all. The colour is resolved in the GUI (the theme is a GUI concept) and travels
    down as a plain QColor: `ctrl::` and `media::` read no theme.)

## I. Destination data

43. [R14] The Discord 20 MB cap is opt-in and settable: the guidance (20 MB free, 50 MB Nitro,
    500 MB boosted) is stated as a fact the user may apply, never enforced silently. (Scope ruled 2026-10-01: this applies to EVERY destination's size cap —
the audit generalizes; Telegram/WeChat caps stop auto-enforcing too, their facts keep stating them.
Cap unit = MB. At the limit: refuse with a visible reason.)
44. [R15] A destination row declares the codecs it accepts and which one is the default, so the
    codec row can offer AV1/H.265/H.264 where the platform accepts them, instead of a fixed
    encoder. (Schema change: a list, not a mode.)
   Archive-row rule ruled 2026-10-01, amended 2026-10-01: `archive.master` offers FFV1/AV1/H.265
   and its default follows the transparency toggle + bucket — Lossless → FFV1; other buckets with
   alpha ON → VP9, unconditionally (amended 2026-10-01: this clause read "the alpha-capable lossy
   pick (AV1 if the build's encoder measures alpha-capable, else VP9)", which contradicted the
   measurement the code follows — `libsvtav1` silently drops alpha, `libx265` rejects it, and
   `libvpx-vp9` is the only lossy encoder that carries it (255/0); the code pins VP9 on the toggle
   alone and asks no capability question. The user's own confirmation stands: transparency ON stays
   VP9. Should a future build's AV1 encoder ever measure alpha-capable, that measurement reopens
   this line rather than silently changing it.); alpha OFF → AV1 at EVERY quality bucket, Best /
   Balanced / Smaller file alike (amended
   2026-10-01 on the user's ruling, "AV1 all the way makes more sense"; H.265 is now only the
   fallback where no AV1 encoder is usable, and the fastFirst ladder split no longer orders this
   row's families). X follows its facts (H.264 only)
   and YouTube@Lossless stays within its accepted list (no FFV1) — both ruled 2026-10-01.
   H.265 tag ruled 2026-10-03: a row that offers H.265 tags its stream `hvc1` whenever H.265 is the
   codec actually resolved, because the muxer's default for HEVC in an ISO container is `hev1`,
   which Safari/iOS and QuickTime refuse. The tag therefore belongs to the RESOLVED CODEC, not to the
   row: `safari.transparent` pins it unconditionally (H.265 is its only codec, so the row and the
   codec coincide), while the five rows that offer H.265 among others — `chat.discord.file`,
   `post.instagram.reels`, `post.pinterest.video`, `youtube.video`, `generic.video` — carry the same
   requirement behind that condition (the 2026-10-08 fold of `website.video` and `generic.file` into
   `generic.video` took the qualified tag with it — `assets/export/ExportTargets.txt:1153` is the row
   and `:1161` its `<codecTag value="hvc1" codec="hevc"/>`; the schema expresses it as an optional
   `codec="<family>"` qualifier on `<codecTag>`; a `codecTag` with no qualifier stays
   unconditional, which is how the Safari row keeps its pin). H.264 and AV1 output is unchanged: no tag is
   written for any other codec. (The facts documented the requirement only for Safari; the ruling
   extends the same mechanism wherever H.265 can be the resolved codec.)

## J. Rendering

45. [R7] Icons render without aliasing at 1×, 1.5× and 2×: vector sources are rasterised at the
    device pixel ratio, and nothing is scaled up from a 1× bitmap.

## K. The quality row's scope

46. [D] The Quality row appears wherever a video encoder writes the file, and only there: it is
    hidden exactly when the artifact is written by `QImage::save` — manual mode's `image/*` formats,
    and the destination rows whose artifact is a sequence or a single still. Those artifacts show
    `Image quality` instead, the number that actually reaches the file, and the two rows are
    complementary by construction (one predicate, negated). Manual mode's Encoding trio is
    unchanged for every video container: Format + Codec + Quality, in the same dialog and by the
    same rules as destination mode. (Ruled 2026-10-01, answering "How does one pick quality in
    manual mode then?": only the control that governs the file is shown. Measured basis: for a
    non-video artifact the planner returns before codec resolution and the writer takes
    `imageExportQuality`, so a bucket beside it would set nothing at all. The 2026-09-27 QML port
    wrote this row's condition as "not manual mode" — wrong in both directions, and masked because
    the call form `exportModel.manualMode()` registers no binding dependency and froze the row
    visible; see the notes' call-form account. Fixed 2026-10-03.)
    (Amended 2026-10-08, on "when CRF is used Quality should change to Custom instead of disabling,
    this is established UI convention": while the codec's own number is named, this row READS
    "Custom" — a fourth entry beside the three buckets, present only while it is the state — and
    stays usable. Choosing a bucket from it hands the decision back to the intent, so a named number
    and a bucket are never both in charge.)

## Design questions (all decided 2026-09-30 — kept for traceability)

Every line marked `[Q]` above, collected. All were decided by the user on 2026-09-30 and the lines
above now carry the decisions; Q4 (item 15), Q6 (item 39) and the Tab+list-open ruling (item 37)
were decided earlier the same way:

| Q | Question | Decided |
| --- | --- | --- |
| Q1 (C17) | Group expansion defaults and memory | Start collapsed; expansion state remembered across sessions |
| Q2 (C22) | Hover: selection or highlight? | Highlight only — hover never selects; selection is a click |
| Q3 (B10) | Escape in the picker | Clears the search field first; closes only when it is already empty |
| Q5 (F34) | Escape in a numeric field | Restores the value from when the field was focused |
| Q7 (E29) | Which controls keep a tooltip | Only picker rows (full truncated name) and the icon-only Auto/Reset buttons ("Reset to default"); all others dropped |
| Q8 (A5) | Remember dialog size between sessions | Yes, persisted; first open uses the designed default |
| Q9 (D25) | Action row on overflow | Fixed at the bottom while the content scrolls |

## Traceability: the review items → this document

| Review item | Contract |
| --- | --- |
| search: no keyboard selection/navigation | B7, B8, C20 |
| arrows expand/collapse while typing | B9, C16 |
| search keeps focus on outside click | B11, A3 |
| search: wrong pointer | B12 |
| scrollbar inconsistent, drag inert | D23, D24 |
| childless top-level items indented | C18 |
| icons aliased | J45 |
| groups expanded by default | C17, Q1 |
| tooltips instant, redundant, at the item | E26, E27, E29, Q7 |
| tooltips clipped by the window | E28 |
| Export parameters overflows | D25, Q9 |
| frame range belongs under Custom | H41 |
| scrubber: wrong pointer, wrong highlight | F31, F33 |
| Add range hover flashes black | C21 |
| Discord 20 MB enforced | I43 |
| Discord codec fixed | I44 |

## What this means for choosing an implementation

Because the design reference is the current exporter UI, "use a different toolkit" is not a fix in
itself: whatever implements these lines must also reproduce the design's look and its specified
behaviours. The hand-rolled controls are therefore not wrong by construction — they are wrong where
they fail a line, and each line is met by whichever mechanism does meet it: a small behavioural fix
in the existing control, a stock control where it matches the design exactly, a template/style
override where behaviour must change beneath a control's own API, or new code. The choice is graded
per line by driving the result, and the look is fixed either way.
