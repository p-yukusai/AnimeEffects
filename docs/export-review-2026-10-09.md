# Export review — 2026-10-09

A read-only review of the exporter and the export UI redesign, produced the
way the contract demands: three parallel reviews over the whole surface, the
project's own gates re-run, and the headline defect reproduced by driving the
real application. Written to be read without the chat session that produced it.

**Verdict: well engineered; the CRITICAL is fixed, two HIGH
defects remain.** Every gate is green, the design is implemented
against a 46-item contract with real coverage. The CRITICAL below
was reproduced at runtime the day this was written and fixed the
same day (see its status line); two HIGH defects and the MED/LOW
findings still stand between this work and "done". Six findings
are not new: `upstream-review/10-return-status.md` §5 (2026-09-27)
listed them with "None of these is fixed… items 1–6 are the ones
to decide before calling the exporter ready", and they are still
open.

## What was run (all green)

| Gate | Result |
| --- | --- |
| `ninja -C build` | 36/36 targets, 0 errors |
| `scripts/run_suites.sh Debug` | 27/27 PASS, 0 fail (incl. `media_exporter_e2e`, `export_form`, `destination_picker`, `picker_keyboard`, `media_export_target`, `fuzzy_match`, `scrub_escape`, `tooltip_contract`, and the four new decoder tests) |
| i18n gates | `check_tr_contexts.py` PASS; `gen_export_targets_i18n.py --check` PASS (159 strings up to date) |
| `scripts/gui/run_probes.sh` | 15/15 PASS, 0 fail, 0 stale (incl. `probe_capped`'s real exports with fit bisections, and the new `probe_fuzzy`, `probe_scrub`) |

The green gates are real, but they are not the whole contract's standard:
"a line counts as verified only with a captured observation or an automated
test that fails when the behaviour regresses" — see the coverage map below.

## CRITICAL — Escape in a numeric field cancels the entire export dialog

**Reproduced at runtime** (2026-10-09): focus the First-frame field, press
Escape expecting to discard the edit → the whole export session is cancelled.
Observed: the dialog window disappears (`xdotool search` no longer lists it)
and AT-SPI reports `First frame 1`, `Export`, `Cancel` all `absent`.

Mechanism, traced against the Qt 6.11.2 source:

- `src/gui/qml/ExportDialog.qml:77` — `Shortcut { sequence: "Escape"; onActivated: exportModel.cancel() }`, unconditional, default `Qt::WindowShortcut` context, no `enabled` binding. The only key on the export scene registered as a Shortcut.
- `src/gui/qml/ScrubSpinBox.qml:222-226` — the editor has `Keys.onEscapePressed` (cancel the edit, blur, `event.accepted = true; // swallow: do not let the dialog close`) but **no `Keys.onShortcutOverride`**. There is no `onShortcutOverride` anywhere in `src/gui/qml`.
- Every key press passes `QGuiApplicationPrivate::processKeyEvent` → `QWindowSystemInterface::handleShortcutEvent` **before** delivery. The scene's `QEvent::ShortcutOverride` ends unaccepted (`QQuickKeyEvent::reset()` always calls `setAccepted(false)`; only an explicit `Keys.onShortcutOverride` handler can accept it), so the shortcut map matches the Escape Shortcut, `exportModel.cancel()` runs, `root.rejected()` closes the QDialog — and the key press is consumed, never delivered to the scene. `onEscapePressed` never runs; F33/F34 (Escape ends the edit and restores the value from when the edit began) are unreachable in the real dialog.
Why nothing catches it: `tests/scrub_escape.cpp:55-59` sends keys with
`QCoreApplication::sendEvent` **directly to the editor item** — that route
bypasses `processKeyEvent`, the shortcut map and the ShortcutOverride layer,
and the test scene does not contain `ExportDialog.qml`'s Shortcut at all. The
sibling test states the measured difference (`tests/picker_keyboard.cpp:162-166`:
"direct send never bubbles to ancestors, window delivery does") and deliberately
drives the window route for that reason; `scrub_escape.cpp:25-28`'s claim that
same-item delivery is "the exact path a real keystroke takes" is false against
that measurement and the Qt source. No probe presses Escape with a field
focused (`probe_inputs.sh:203` presses Escape only at the file dialog).

The picker's Escape is correct by contrast: that scene has no Shortcut (root
`Keys.onEscapePressed`, `DestinationPicker.qml:498-506`) and is verified
through the window route by `picker_keyboard.cpp`.

**Fix (applied 2026-10-09):** the editor now carries (fires only
while the editor holds focus, so Escape-cancel stays available
everywhere else):

```qml
Keys.onShortcutOverride: (event) => {
    if (event.key === Qt.Key_Escape) event.accepted = true
}
```

plus a window-route regression test against the real `ExportDialog` scene (the
route `picker_keyboard.cpp` already uses for the picker).

**Status 2026-10-09 (later the same day): FIXED.** The window
route is pinned by a regression test in `tests/form_layout.cpp`
("escape window route" cases, seven checks): Escape in an editor
ends the edit and the dialog stays open, the discarded edit does
not reach the document, and Escape with no editor focused still
cancels the dialog (A2). The test was written against the unfixed
tree and failed there (the shortcut map cancelled the dialog), then
passed after the one-handler fix. The full suite is 27/27 after
the fix. The runtime check (below) was re-run against the rebuilt
app with a deterministic focus route — the AT-SPI press action on
"First frame 1" (`ae_activate`, which lands in
`ScrubSpinBox::beginEdit()` → `editInput.forceActiveFocus()`,
`ScrubSpinBox.qml:58-69`) instead of a coordinate click, which
can miss the field and turn Escape into the correct A2 cancel:
with the field in edit mode and a digit typed, `Escape` left the
dialog's widgets `showing` and the field's value back at its
edit-start value. The app loads the scene from the compiled
resource (`qrc:/…/ExportDialog.qml`, `src/gui/ExportDialog.cpp:77`),
so a QML edit needs `ninja -C build` before a runtime check sees
it — the first post-fix runtime run still closed the dialog
against the pre-fix binary. What remains of this list: the
two HIGH items and the MED/LOW findings below.

Reproduction recipe (what was run): `xvfb-run -a` with **openbox running**
(keyboard input needs a WM — without it keys are silently dropped); launch
the app with the smallest `.anie`; File → "Export project as"; in the picker,
search `discord video`, activate Continue; `ae_activate "First frame 1"`
(the AT-SPI press — the deterministic focus route); `xdotool key 7`;
`xdotool key Escape`; observe the dialog's widgets still `showing` and the
field's value unchanged. Before the fix the same flow lost the dialog to
the shortcut map; after it, the edit ends and the dialog stays open.

## HIGH — the transparency toggle never reaches the destination-mode plan

Verified by reading the tree: there is **no `request.allowTransparency =`
anywhere** in `src/gui/MainWindow.cpp` or `src/gui/qml/ExportViewModel.cpp`.
The destination-mode `ExportRequest` keeps its `true` default
(`src/ctrl/ExportTarget.h:349`), so:

- the archive ladder's alpha-OFF branch (`src/ctrl/ExportTarget.cpp:1088-1090`,
  VP9 → AV1) is **app-unreachable** — it is core-correct and unit-tested
  (`tests/media_export_target.cpp:1457-1470`), but no application path can
  exercise it;
- on an `alpha="default-on"` row the user can switch the checkbox off
  (`setCapabilityDefault` leaves it enabled, `ExportViewModel.cpp:781-784`)
  and the export **still writes alpha** — a control that lies.
  `applyDestinationDefaults` re-sets it to true on every refresh
  (`ExportViewModel.cpp:1033`).

The toggle's value reaches only `readBack()` (`ExportViewModel.cpp:1425` →
`g.allowTransparency`), which feeds manual mode (`MainWindow.cpp:1253`) and
the GIF path (`MainWindow.cpp:1590`) — never the destination-mode plan.
(`MainWindow.cpp:1853` sets `genParam.allowTransparency = true` with the
comment "the destination decides, in the plan" — the plan never receives the
user's answer.)

This is the GUI half of the contract's documented OPEN H42 defect
(`docs/export-dialog-behavior.md:279-287`); the implementation notes hold the
candidate fix ("give `buildRequest()`/`planExport()` the row's value",
`docs/export-dialog-implementation-notes.md` ~:1810-1814). The core's
`Choice → false` resolution (`ExportTarget.cpp:994`) is the other half and is
already recorded there.

## HIGH — quality intent is silently dropped for hardware-resolved codecs

Known since `upstream-review/10-return-status.md` §5.2 (2026-09-27); unfixed.
`applyQualityIntent` is keyed on `plan.config.codec` — the **resolved** encoder
(`ExportTarget.cpp:1280`) — but `<quality_control>`
(`assets/export/ExportTargets.txt:1221-1231`) names only the software
encoders (`libvpx-vp9, libsvtav1, libx264, libx265, libwebp, gif`). A
resolved `h264_nvenc/h264_qsv/h264_vaapi/h264_amf` hits
`control.option.isEmpty() → return false` with **no note** (the only note is
the missing-`<bucket_positions>` case, `ExportTarget.cpp:276-286`). On any
machine with a usable hardware encoder, every Balanced/Smaller export from a
non-dictated row — and the same in manual mode (`MainWindow.cpp:1272-1282`) —
silently loses the Quality selector's effect: exactly the inert-control defect
the code was added to fix (`ExportTarget.cpp:1268-1273`).

## MED — the six from 2026-09-27, still open

All known from `upstream-review/10-return-status.md` §5; none fixed:

1. **Hardware encoder preference order is reversed.** `order.prepend(name)` in
   `resolveLadderCodec` (`ExportTarget.cpp:812-820`) and `resolveDefaultCodec`
   (`:941-949`) reverses the declared hardware subsequence, so the
   last-declared usable hardware encoder wins (amf > vaapi > qsv > nvenc) —
   an Intel+NVIDIA laptop picks QSV over NVENC, opposite the declared policy.
   Silent on multi-hardware machines.
2. **`fastFirst` contradicts its own comment.** `ladderFor` sets
   `out.fastFirst = (aBucket != QualityBucket::Best)` (`ExportTarget.cpp:213`),
   so Lossless also takes hardware first, while the comment (`:181-186`) says
   fidelity buckets keep the declared (software) order.
3. **No encoder fallback at open time.** `Exporter::execute` returns
   `EncoderOpenFailed` immediately (`Exporter.cpp:264-268`). A hardware
   encoder that passes the 64×64 probe can still fail on the real
   resolution/option set; the export aborts instead of stepping down to the
   family's software encoder.
4. **`requiredSize` omits `SizeMode::MaxMinSide`** (`ExportTarget.cpp:1514-1515`):
   the fit's scale phase may shrink a MaxMinSide row (line.sticker's declared
   270 px min-side floor) below its rule.
5. **The fit's "frames" phase shortens duration at unchanged fps**
   (`ExportTarget.cpp:1842-1861`): stride > 1 drops frames, fps untouched, so
   a capped 3 s emote becomes shorter at the same rate. Deliberate ("the most
   destructive knob", `:1641-1644`) and frame-count-tested for GIF, but no
   test asserts a fitted video's duration.
6. **The app's `fitRequest` is not the plan's request** (`MainWindow.cpp:1457-1465`
   drops `playhead` and `imageStem`): a capped Still destination re-plans from
   frame 0 and can write a differently-named sequence than the plain path.

## MED — new findings

- **H41 is violated in the common case, with no recorded ruling.** The Frames
  row is the FIRST row of the main OUTPUT section (`ExportDialog.qml:321-377`,
  deliberate comment "Frames first: which frames, then how big, then how fast",
  `:328-329`), gated by `exportModel.rangeVisible` — and
  `rangeVisible() = !mArtifactIsStill` (`ExportViewModel.h:362`) is true for
  video, animated image and image sequence, i.e. the app's primary use. The
  contract (H41, `[R12]`, unamended) puts the frame range under
  Custom/Advanced. The D25 overflow measurements and `form_layout.cpp` bake in
  the current placement, so either the row moves under Advanced (and D25 is
  re-measured) or the ruling is recorded in the contract.
- **F36's inversion clamp is absent from the form.** The two frame-range
  fields are independent, full int32 (`AeFrameRangeRow.qml:41-65`), and
  `ExportRangeModel::setRangeFirst/setRangeLast` (`ExportViewModel.cpp:151-169`)
  bounds-check the index only — a user can set first=100 on a [10..50] range.
  The contract says "the frame-range fields clamp against each other so a range
  can never invert." The backend detects and swaps with a warning
  (`ExportParams.h:229-238`), which is why this is MED not HIGH, but the
  form-level guarantee is unmet and untested.
- **A3/B11 click-outside-focus is partial.** The shell MouseArea blurs ONLY
  `objectName === "scrubEdit"` (`ExportDialog.qml:108-115`); the bitrate
  field (`:738-748`) is never blurred by a background click, and the picker
  scene has no background MouseArea at all (`DestinationPicker.qml:39-54`) —
  clicking its empty space leaves focus in the search field. No test or probe
  covers click-outside for either dialog (`picker_keyboard.cpp:27-29`
  documents B11 as uncovered).

## LOW

- **11 of 46 contract items have no regression instrument at all** (see the
  coverage map below) — the contract's own verification standard is unmet for
  them.
- **Fitting messages report the wrong cap.** `MainWindow.cpp:1501-1503` and
  `:1526-1528` show the destination's cap (`plan.target.caps.maxBytes`) in
  the "Fitting under X's N KB limit" and failure messages while enforcing the
  USER's cap (`fitTarget.caps.maxBytes = g.sizeCapBytes`, `:1459-1461`) —
  the wrong number whenever they differ.
- **The destination row tooltip goes stale on scroll.** `DestinationPicker.qml:444-447`
  binds ownerX/ownerY/ownerW/ownerH from the delegate but never reads the
  ListView's `contentY`, so a wheel-scroll under a stationary hovered pointer
  leaves the tooltip anchored at the pre-scroll position (E27 violated in that
  case; E28 still holds via the clamp).
- **Dead weight:** `resolveLadderCodec` (`ExportTarget.cpp:798-829`, declared
  `ExportTarget.h:524`) has zero callers; `mDigitCount`/`getDigitCount` are
  written (`Exporter.cpp:330`) but never read (`decideImagePath` hardcodes 4
  digits, `:726-729`); `updateLog()`/`mUILogger` is a no-op (`Exporter.cpp:760-763`);
  `factsObject()`/`rangesObject()` are dead accessors (`ExportViewModel.h:322`,
  `:361`); a dead inner ternary in `artifactSuffix` (`ExportViewModel.cpp:1434-1437`);
  `PSDUtil::encodePackBits` (`src/img/PSDUtil.cpp:46-82`) duplicates
  `util::PackBits::encode`.
- **Doc staleness:** the I44 hvc1 paragraph (`docs/export-dialog-behavior.md:343-348`)
  names `website.video` and `generic.file`, both removed 2026-10-08 and folded
  into `generic.video` (which carries the qualified tag).
- **Latent default:** `parseCapability("")` returns `Choice` (`ExportTarget.cpp:22-28`)
  but the struct default for alpha is `Forbidden` (`ExportTarget.h:170`): a row
  added without an `alpha=` attribute silently becomes user-toggleable. Every
  current row declares alpha.

## Coverage map (contract → instrument)

35 of 46 items have an instrument that fails on regression (unit test and/or
GUI probe). Partially covered (named clauses have no instrument): **A2, C16,
F33, G37, G38**. No instrument at all: **A1** (Tab order/focus ring), **A3**
(click-outside removes focus), **A4** (modal+transient), **B11** (click-outside
keeps text), **B12** (I-beam pointer), **C22** (hover never selects), **D24**
(scrollbar tokens), **E30** (tooltip z-order/pass-through), **F31** (I-beam in
text mode), **G39** (type-ahead), **J45** (icons at 1×/1.5×/2×).

The pointer-shape lines (B12, F31) need `ffmpeg -f x11grab -draw_mouse 1` —
no probe runs it (no ffmpeg in `scripts/gui`). B11/B12 are explicitly deferred
in code (`tests/picker_keyboard.cpp:24-30`). Note the two lookalike files:
`tests/picker_probe.cpp` and `tests/picker_pos_probe.cpp` exercise
`EasingPickWidget` (the easing-curve picker) and cover zero contract items.

## Batch hygiene (the uncommitted tree: 48 modified, +5945/−1659; 9 untracked)

- **8 of the 9 untracked files are required** for the build and the gates:
  `assets/icons/ph/film-strip.svg` (referenced by `assets/assets.qrc:49` and
  `ExportTargets.txt:762,1154` — the qrc build breaks without it),
  `src/gui/qml/FuzzyMatch.h` (included by `DestinationPickerModel.cpp:3`),
  the four new tests (declared in `tests/CMakeLists.txt:44-72`), and
  `scripts/gui/probe_fuzzy.sh` + `probe_scrub.sh` (picked up by
  `run_probes.sh`'s `probe_*.sh` glob). The commit does not build without
  them.
- **`nene.mp4`** (10,459,082 bytes, repo root) is a stray export artifact —
  referenced by no code, test, probe or qrc, and not ignored. Delete it or add
  an ignore rule; a 10 MB binary must not land in the commit.
- **19 unfinished translation entries per catalog** (0 at HEAD): the 8 renamed
  "(video)" labels, "Video", "Animated image", "Image sequence",
  "Destination", "Continue", "Manual", the new "Search: …" placeholder,
  "max %1 x %2", and 3 long notes. All five non-English locales render these
  in English until translators fill them.
- **Two themes are mixed in one batch:** the export redesign (~90%) and
  separable image-decoder hardening (the PackBits 0x80-noop infinite-loop fix
  in `src/util/PackBits.cpp:55-65` and `src/img/PSDUtil.cpp:262-270`, the
  `Deserializer::readImage` line-length bounds check `src/core/Deserializer.cpp:341-352`,
  with their three tests). The decoder half deserves its own commit.
- No TODO/FIXME/XXX/HACK and no debug output in any added line.

## What was verified good

- Core contract items **I43, I44, F36 are MET**: qint64 byte caps that
  survive parsing at ≥2 GiB, per-destination codec lists with the archive
  ladder (Lossless → FFV1; alpha on → VP9; else AV1 at every bucket, H.265
  only as fallback), `hvc1` written only when H.265 is the resolved codec,
  and a Free/Max ceiling that bounds the file without setting the fields.
- The aspect-lock guard is **sound**: `mWritingSize` is carried across the
  recursive write-back (`ExportViewModel.cpp:699-705`, `:721-727`), bounding
  recursion at depth 2 for all inputs; Reset ignores the lock and re-bases it
  per the 2026-10-08 ruling.
- The QML **call-form binding trap is absent** — all `manualMode` references
  are property-form; the only `()` inside a binding (`ExportDialog.qml:655`)
  is a QML function whose context property is dependency-tracked.
- Capability checks are real open+one-frame encode probes, not static lists;
  the fit loop terminates (`kMaxAttempts = 16`), interpolates on real
  measurements, and re-encodes the winner to verify determinism before
  reporting success.

## Fix order

1. ~~The CRITICAL~~ DONE 2026-10-09: `Keys.onShortcutOverride` on
   the editor + the window-route regression test in
   `tests/form_layout.cpp` (red before the fix, green after; the
   full suite stayed 27/27).
2. Wire the transparency toggle into the destination-mode request
   (`buildRequest()` / `MainWindow`'s destination-mode path) — the notes hold
   the candidate fix; this also unblocks the app-unreachable alpha-OFF ladder
   branch and is the GUI half of the OPEN H42 defect.
3. The six 2026-09-27 §5 items (each with a test that can fail).
4. The H41 ruling (move the Frames row under Advanced and re-measure D25, or
   record the ruling in the contract), the F36 clamp, and the A3/B11 completion.
5. Batch close-out: fill the 19 translations, delete/ignore `nene.mp4`,
   commit in two commits (export redesign; decoder hardening).

## 2026-10-09 resolution

What this pass did to each finding above, and where it lives on the landed tree. Line
numbers are that tree's; the two findings still open at the end are named as open.

**CRITICAL — Escape in a numeric field cancels the dialog** (:32). Fixed the same day,
as the item's own status line records: `Keys.onShortcutOverride` on the editor at
`src/gui/qml/ScrubSpinBox.qml:233` and the window-route regression at
`tests/form_layout.cpp:1359-1429`, which also re-targets the escape-window-route block
at the frame row's editor.

**HIGH 1 — the transparency toggle never reaches the destination-mode plan** (:104).
Fixed; H42 is closed. `src/gui/qml/ExportViewModel.cpp:1268` writes
`request.allowTransparency = mTransparency.value()` in `buildRequest()`; the plan's
capability switch reads it (`src/ctrl/ExportTarget.cpp:1044-1056`, the request read at
:1055); `freezeCapability` (`src/gui/qml/ExportViewModel.cpp:814-834`) now freezes only
for `Required`/`Forbidden` and treats a refusal over a user's choice as the codec's own.
A `choice` row's plan therefore reports the choice it was given, and the archive ladder's
alpha-OFF branch (`src/ctrl/ExportTarget.cpp:941-978`, the AV1 fall-through at :978) is
app-reachable — as is the alpha-ON VP9 pick, which reaches the codec row at :1160. Pinned
by `tests/export_form.cpp:1103-1129` (both row states) and
`tests/media_export_target.cpp:1482-1551` (the plan half). The ruling is recorded in
`docs/export-dialog-behavior.md` item 42.

**HIGH 2 — quality intent is silently dropped for hardware-resolved codecs** (:135).
Fixed. ONE family→quality-table mapping, `src/ctrl/ExportTarget.cpp:143-148`
(`qualityTableNameForFamily`), consulted by the single lookup `qualityControlFor`
(:305-309), so `applyQualityIntent` (:312) and `qualityKnobFor` (:347) — and the fit
loop's own lookup (:1608) — resolve a hardware encoder to its family's software entry
instead of returning an empty option with no note. Pinned by
`tests/media_export_target.cpp:2349-2421` (the hardware-knob block; the Balanced and
Smaller buckets now differ for a hardware-resolved codec, and the plan carries crf).

**MED 1 — hardware preference order reversed** (:154). Fixed. The partition now keeps
each group in DECLARED order — `src/ctrl/ExportTarget.cpp:859-870` (`resolveLadderCodec`)
and :997-1008 (`resolveDefaultCodec`) — where `prepend` had reversed the hardware
subsequence (amf > vaapi > qsv > nvenc). Pinned by
`tests/media_export_target.cpp:1554-1613`.

**MED 2 — `fastFirst` contradicts its own comment** (:160). Fixed:
`src/ctrl/ExportTarget.cpp:235-237` sets it for Smaller and Balanced only, so Best and
Lossless keep the declared (software) order the struct's comment promises (:196-208).
Same test block.

**MED 3 — no encoder fallback at open time** (:164). Fixed:
`src/ctrl/Exporter.cpp:165-187` names the family's software encoder and :311-358 steps
down to it ONCE when a hardware open refuses, preserving the original reason when the
codec was already software. New gate `tests/export_encoder_fallback.cpp` (three cases),
registered at `tests/CMakeLists.txt:762-766`.

**MED 4 — `requiredSize` omits `MaxMinSide`** (:169). Fixed:
`src/ctrl/ExportTarget.cpp:1578-1590` now counts MaxMinSide as a required size, so the
fit's scale phase cannot shrink a min-side floor below its rule. Pinned by
`tests/media_export_target.cpp:3162-3243`.

**MED 5 — the fit's "frames" phase shortens duration at unchanged fps** (:172).
Fixed. The survivors are now written at `fps/stride`, so `frames/(fps/stride)` — the
running time — is what the plan described. `effectiveFpsForStride`
(`src/ctrl/ExportTarget.cpp:1413-1445`) computes the rate once and every encode path
takes it from there: the GIF branch's `gcfg.fps` (:1547) and the video branch's
`cfg.fps` (:1577, which the software step-down's `makeConfig` at :1615 inherits).
Stride 1 returns the plan's own rate unchanged, so no existing output moves; a rate
that rounds below the format's 1 fps floor is clamped there and the fit says so
rather than letting a shorter animation pass silently. When a stride > 1 wins, the
loop appends a step line (:2110-2144): "fit kept every 4 frames: 3 of 12 frames at
3 fps, the same playing time as the plan". Pinned by
`tests/media_export_target.cpp:172-194` (`fileVideoRate`, the rate the container
recorded) and :2621-2745: for telegram.sticker forced frames-only, the file's
frames/rate equals the plan's duration within one delivered frame, and the step line
records that the duration was preserved. Measured: 12 frames @ 10 fps (1.2 s) fitted
at stride 4 writes 3 frames @ 3 fps = 1 s; the pre-fix tree wrote those same 3 frames
at 10 fps, 0.3 s.

**MED 6 — the app's `fitRequest` is not the plan's request** (:177). Fixed:
`src/gui/MainWindow.cpp:1476-1490` re-plans the fit from a COPY of the plain path's
request (`ctrl::ExportRequest fitRequest = request;` at :1486), so `playhead` and
`imageStem` — and the other four fields the hand-built copy dropped — reach
`ctrl::exportWithFit`; a capped Still destination plans from the frame on screen, as
`tests/media_export_target.cpp:2442-2455` pins at the plan level.

**new MED — H41, the Frames row on the default surface** (:183). Fixed by moving the
row, not by amending the clause: it is now the first child of the Advanced column
(`src/gui/qml/ExportDialog.qml:598-656`), the OUTPUT grid that loses it records the move
at :342-347, and the D25 overflow measurements were re-taken around it — the
Advanced-open worst case is unchanged (the rows were already inside its height) and the
default surface is shorter by them, printed live by
`tests/form_layout.cpp:256` and :1510-1512 and asserted by
`tests/form_layout.cpp:1433-1546`. The ruling is recorded in
`docs/export-dialog-behavior.md` item 41 ([R12] MET, unamended).

**new MED — F36, no form-level inversion clamp** (:193). Fixed at the model and offered
at the row: `src/gui/qml/ExportViewModel.cpp:151-185` clamps each field against the
project's extent and carries the neighbour along in one pass, and
`src/gui/qml/AeFrameRangeRow.qml:48-71` bounds each field by the partner (:57 and :70).
Pinned by `tests/form_layout.cpp:1549-1654` and `tests/export_form.cpp:1132-1205`; the
ruling is recorded in `docs/export-dialog-behavior.md` item 36.

**new MED — A3/B11, partial click-outside-focus** (:201). Fixed. The export form's shell
MouseArea now blurs the bitrate field too (`src/gui/qml/ExportDialog.qml:107-124`, the
field's `objectName` at :779), and the picker finally has a background MouseArea of its
own (`src/gui/qml/DestinationPicker.qml:56-71`). Pinned by
`tests/form_layout.cpp:1657-1741` and `tests/picker_keyboard.cpp:886-946`.

**LOW — the eleven uninstrumented contract items** (:211). A3 and B11 gained instruments
this pass (the two test blocks above); nine remain without one: A1, A4, B12, C22, D24,
E30, F31, G39, J45. B12/F31 still need `ffmpeg -f x11grab -draw_mouse 1` against a
visible window, which no script in `scripts/gui` runs.

**LOW — fitting messages report the wrong cap** (:214). Fixed:
`src/gui/MainWindow.cpp:1518-1523` and :1544-1553 now name the cap the fit ENFORCES
(`fitTarget.caps.maxBytes`, the user's), with comments pinning why the two numbers differ
exactly when the old string would have lied.

**LOW — the destination row tooltip goes stale on scroll** (:219). Fixed: the owner rect
is re-read from the delegate with the view's `contentY` subtracted
(`src/gui/qml/DestinationPicker.qml:459-476`, the ownerY binding at :473), so every
scroll re-evaluates it; E28's clamp was unaffected and stays. Pinned by
`tests/picker_keyboard.cpp:948-1072`.

**LOW — dead weight** (:224). One entry was stale and is corrected rather than fixed:
`resolveLadderCodec` has seven callers in `tests/media_export_target.cpp`
(1136-1246) and stays. Removed: `mDigitCount`/`getDigitCount`,
`updateLog()`/`mUILogger` and the unused `setUILogger`, with the `ctrl/UILogger.h`
include, from `src/ctrl/Exporter.{h,cpp}` (the 4-digit naming in `decideImagePath` is
unchanged); `factsObject`/`rangesObject` from `src/gui/qml/ExportViewModel.h`; the dead
inner ternary in `artifactSuffix` (`src/gui/qml/ExportViewModel.cpp:1462-1467`).
`PSDUtil::encodePackBits` also stays — it is the PSD writer's own encoder
(`src/img/PSDUtil.cpp:76`), so that entry is a refactor note, not dead weight.

**LOW — doc staleness** (:232). Fixed: `docs/export-dialog-behavior.md` item 44's
`hvc1` paragraph now names the five rows that offer H.265 among others (`generic.video`,
not the removed `website.video`/`generic.file`), citing
`assets/export/ExportTargets.txt:1153` and `:1161` for the folded row and its qualified
tag.

**LOW — latent default** (:235). STILL OPEN: `parseCapability("")`
(`src/ctrl/ExportTarget.cpp:22-28`) still returns `Choice` against the struct default
`Forbidden`, so a row added without an `alpha=` attribute is silently user-toggleable.
Every current row declares alpha; no agent in this pass owned the change.

**Batch close-out.** The 19 unfinished entries per catalog are filled in
`assets/locale/translation_{es,ja,ko,ru,zh}.ts` — 19 → 0 unfinished in each of the four
real catalogs, with every `<location>` line and `%1`/`%2` placeholder preserved and no
guessed strings left unfinished; `translation_en.ts` is the 4-line stub and was not
touched. `nene.mp4` is deleted from the tree. What remains of the close-out is the
committer's: the two-commit split (export redesign; decoder hardening) and, in the code,
MED 5 and the latent default above.
