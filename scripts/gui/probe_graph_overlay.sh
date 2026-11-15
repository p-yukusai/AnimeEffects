#!/bin/bash
# DOES THE VIEW SETTINGS GRAPH TOGGLE PUT THE ANIMATION-GRAPH HUD ON THE VIEWPORT?
#
# WHAT IT PROVES, AND WHY NOTHING ELSE CAN. tests/graph_curve.cpp drives the graph value
# model, tests/graph_drag.cpp the edit path and tests/graph_panel_model.cpp the
# engine-neutral panel geometry; tests/graph_panel_pixels.cpp is the pixel gate, and it
# hands gui::drawPanel a SYNTHETIC model inside one real ImGui frame and reads the
# framebuffer back with glReadPixels; tests/overlay_engine_primitives.cpp pins the GL paint
# engine's primitive subset and tests/graph_image_step.cpp the image-offset channel's step
# against a headless GL context. Every one of them passes whether or not the panel is ever
# put on screen - and tests/imgui_a11y.cpp says as much itself: it decides that the
# published tree says what the model says, and names its own gap as "whether the AT-SPI
# bridge forwards this tree, and whether a real screen reader sees it - that needs the live
# overlay under Xvfb plus a dump of the real window". That is this probe. What no test can
# see is the thing asserted here: whether ctrl::Driver draws that panel OVER the live canvas
# inside gui::MainDisplayWidget, for the curves of the layers the object tree ACTUALLY has
# selected, inside the CENTRAL widget - and whether the icon-only toggle in the View
# Settings panel is wired to it at all. That composition happens in the GL paint path of a
# real, mapped window.
#
# WHY THE PROJECT IS PINNED, AND WHAT THE PIN REALLY LOADS. This probe used to take
# whatever _lib.sh found - "the smallest .anie under ~/Downloads" - so its own SUBJECT moved
# when the filesystem did: one run measured object-tree cells named `running thru my mind`
# and `Layer1` where the recorded run had `Folder16`, and a phase-3 assertion flipped with
# it. A probe whose data comes from the current contents of ~/Downloads cannot be compared
# between runs, and its thresholds cannot be re-measured. It now passes the repository's own
# fixture (tests/fixtures/e2e_stage.png, the file tests/media_exporter_e2e and
# probe_exportbg use) as its project ARGUMENT - and that argument is NOT what the
# application opens: Main.cpp:212 opens only a `.anie` (arguments.last().contains(".anie")),
# so MEASURED, the app keeps the Debug build's own startup project assets/sample.psd
# (AE_SAMPLE_PSD, Main.cpp:208) and the fixture's name appears nowhere in its log. That file
# is in the repository too, so the probe's subject is a committed file either way; the pin
# ASSERTIONS say which one it really was, from the app's own log ("image path = ...") and
# from the object tree's first row, so a run that silently loads something else fails
# instead of quietly re-recording. Pass arg 1 or AE_PROJECT to test another project: the pin
# assertions are skipped then, because the subject is the caller's choice.
#
# THE HONEST STATEMENT OF THAT GAP: the subject is an accident of the BUILD CONFIGURATION,
# not the fixture. There is no .anie file anywhere in this repository (MEASURED: `find . -name
# '*.anie'` returns nothing, and only the command line's .anie branch reaches
# MainWindow::onOpenRecentTriggered), and the only way to give the probe another subject
# would be to make the application open one - a source change, which this probe may not
# make. So: the fixture argument's ONLY job is to keep _lib.sh from searching the filesystem,
# the loaded project is the Debug build's own `assets/sample.psd`, and the header says so
# here and in the pin assertion's message. If AE_SAMPLE_PSD is ever changed or removed, the
# probe fails loudly at those two assertions rather than measuring something else - which is
# the whole point of pinning it - and a run that must use a project of its own says
# AE_PROJECT=<path> (template: the pin assertions are skipped, and the header's measured
# numbers stop applying).
#
# THE RENDERER, since thresholds are pixels. These captures are llvmpipe. MEASURED:
# `xvfb-run -a --server-args="-screen 0 1440x900x24" glxinfo -B` (the same wrapper this
# probe runs the app in) reports Device: llvmpipe (LLVM 22.1.8, 256 bits), Mesa 26.2.2,
# Accelerated: no - while the developer's own session reports the real GPU (AMD Radeon
# R9 380, radeonsi, tonga). So every number below is a software-rasterised Mesa
# measurement, and the same probe on a radeonsi or llvmpipe-with-other-LLVM build is
# expected to agree on counts and disagree on a handful of antialiased edge pixels: this is
# why the assertions that matter are exact-zero identities or order-of-magnitude bars, and
# never a two-pixel tolerance.
#
# WHAT IT MEASURES, recorded on 2026-10-13 on this machine's 1440x900 probe window, with
# the probe's own pinned project (see above: the fixture argument, so the loaded project is
# assets/sample.psd - a project whose top node carries NO keys and whose child layers carry
# none, which is why phase 2 adds one and phase 3 can then delete the very layer that
# carries it):
#   * the window's client area is 702x455 - the top-level frame reports [0 0 702 455] -
#     and inside it the accessibility tree names the three regions this probe measures
#     against: `Properties` [382 21 320 434] (docked right) and `Animation`
#     [0 185 382 270] (docked bottom), plus the graph toggle itself [8 88 24 24].
#   * OFF -> ON changes 2332 pixels with the tree's top row selected and 2200 with
#     nothing selected - all of it inside the crop. RE-MEASURED after the contrast-outline
#     pass landed, which is why these are not the 3681/3585 recorded against the halo
#     build: the same states move fewer pixels inside the same box. The toggle's own 24x24
#     checked highlight is 568 px = 0.91% of the crop and the bar is 1000, so the bar cannot
#     be cleared by the button repainting itself; the per-column split recorded earlier (0 px
#     across the Tools dock) is NOT re-measured here - the claim this bullet carries is the
#     bar, not the split.
#   * the changed region's bounding box is 370x143+8+39 on BOTH of those states: x 8..377
#     and y 39..181, i.e. LEFT of the Properties dock (x=382) and ABOVE the Animation
#     panel (y=185). Everything the panel draws - frame ticks, frame numbers, the value
#     labels, the zero rule, the 1px playhead, the centred state line and the legend -
#     is in there: the Tools dock (x 0..176) changes 0 pixels apart from the toggle.
#   * OFF -> ON -> OFF restores the pixels exactly: the two OFF shots differ by 0.
#   * the empty state, with the panel ON and NO layer selected, publishes the model's own
#     sentence as an accessible label ("Select a layer to show its curves"), and its
#     change inside the crop is 2200 px = 3.51% (3585 = 5.72% before the outline pass) -
#     the smallest of the states this probe measures, as a
#     text-only panel should be.
#   * PHASE 2, one explicit key added to a child layer by pressing the Current Keys knob:
#     the control pair is 0 px, and the plot repaints by 2162 px = 3.45% inside the crop.
#   * the legend row the press produces ("Folder16 : Rotate") is a real checkable
#     control: activating it flips it to unchecked and removes its curve, 2078 px = 3.32% inside
#     the crop and 0 outside it; activating it again flips it back and restores the crop
#     EXACTLY (0 px).
#   * PHASE 3, THE DANGLING TARGET - the gate the rest of this probe cannot be. The layer
#     the panel is DRAWING is deleted through the object tree while the panel is on
#     screen, and the app has to come out of it alive. MEASURED on the state phase 2
#     leaves behind: the model names the layer ("Folder16 : Rotate"), the tree publishes
#     it as a cell at 40 221 115 22, a right-click on the LEFT EDGE of that cell (not its
#     centre: see WHERE THE MEASUREMENTS ARE MADE) opens the object tree's row menu at the
#     click point (window 0x200036, 158x223+44+232, Map State IsViewable), and Up+Return
#     with the input focus pointed at that window presses its last entry, which is Delete.
#     Afterwards: the process is ALIVE, no `table cell` named Folder16 answers, the panel
#     publishes one of the model's sentences again ("No keys on the selected layers") with
#     0 legend rows, and the crop changes 9043 px = 14.43% against a control pair of 0 px (28944 px whole screen).
#     The crop number is NOT the curve alone and is not claimed to be - the deleted layer
#     also drew in the crop's canvas strip (the same crop still differs from the panel's ON
#     empty state by 12799 px) - which is why the MODEL is what pins the panel here (the
#     sentence, and a legend that went from one row to none) and the pixels corroborate it.
#     What would make THAT claim false is a target left in GraphEditor's raw-pointer list:
#     the draw after the deletion walks it, and a fault there is a dead process, which the
#     alive assertion reports (and which no AT-SPI assertion could, since a dead app just
#     stops answering).
#   * PHASE 4, THE KEY CONTEXT MENU. A right-click on a key opens the timeline's easing list
#     at the pointer and choosing an entry changes the SELECTED key's easing (one undo step
#     on the timeline's notifier). THE PANEL PUBLISHES THE PLOT TWICE, and this phase leans
#     on it: the state sentence and the fit action both carry model.plot as their rectangle
#     (gui::ImGuiOverlay publishes `model.plot.isEmpty() ? model.panel : model.plot` for
#     both, because a double-click on the plot performs the fit), so asking the fit action
#     where it is IS asking the panel where its plot is. WHICH POINT IN THAT PLOT THE KEY IS
#     AT is FOUND, not predicted: a right-click on a key opens the menu and a right-click
#     anywhere else opens nothing at all (the graph takes no right-button action), so the
#     phase walks the fit rect's CENTRE ROW in 10 px steps - and that row then +/- 8 px - and
#     accepts a sample only when its click really opened the popup, measured against a control
#     captured immediately before the scan against the same menu_shaped rule the anchor search
#     applies to it afterwards (a menu-sized, in-viewport rectangle with thousands of changed
#     pixels; 10 is the largest step that cannot straddle a 10 px chip). Why the centre ROW:
#     the value axis is fitted, so a single key is vertically centred - while its frame
#     position is the FIT's business, which this phase deliberately does not assert (the frame
#     half of ctrl::GraphEditor::fitAll is what the user asked to change this round). MEASURED
#     in the run this fix is recorded against: the menu opened 9 samples in, at (302,103) -
#     2 px from the fit rect's centre (222,25 156x156 -> 300,103) - because this build's fit
#     does centre both axes, and the phase still does not lean on that. THE ONE LEFT PRESS the
#     phase makes follows the scan and is accepted only if it MOVED the panel's state line: its
#     selected-key half has to go from EMPTY to the key, and the phase clears the selection
#     first (the graph's own Ctrl-press deselect gesture, which the graph owns and which changes
#     no document byte) because the right-click that found the key selects it by design. The
#     menu itself is then addressed BY POSITION AND CONFIRMED BY EFFECT (its own rectangle is
#     measured from its pixels; the entry is the 4th easing in the shared display order, row 1
#     of the menu's two columns), and the readback is the panel's own state line, which names
#     the selected key and the easing it carries - built from the DOCUMENT, not from the menu's
#     return value. The dock's Easing chip is NOT a readback: MEASURED, its accessible name is
#     the static tooltip "Easing" and no type name. The NEGATIVE is the true claim, not
#     "changes nothing": a right-click that hits no key opens no menu and leaves the state line
#     alone (MEASURED, 0 px changed and 0 px in every off-key scan sample of the recorded run).
#     THREE THINGS THE PHASE HAD TO LEARN, all measured on this build. (1) THE RIGHT-CLICK'S
#     TARGET IS SMALLER THAN THE LEFT PRESS'S: the menu opens only when the click is inside the
#     key's DRAWN square (ctrl::graph::kKeyRadius = 5, so 10x10, and drawKeyContextMenu asks
#     curve.keys.at(i).contains(pos)), while the editor's own pick accepts kKeyHitRadius = 10
#     around the same centre. The scan and the anchor search therefore address the SMALLER one
#     and confirm it by the popup's own pixels - the scan across the fit rect's centre row, the
#     anchor search (the found point and a 4 px lattice out to 8, nearest first) for the
#     openings the rest of the phase needs. AND THE TWO HAVE TO AGREE ON WHAT A MENU IS: they
#     did not, and that is what the run this phase was fixed against failed on. The scan used
#     to accept a point from the panel's state line (which is sticky - see below) and the
#     anchor search then rejected the very same point, because its "a menu is DENSE" rule
#     demanded that 60% of the rectangle a menu covers be changed pixels while the real menu
#     changes 17165 of its 202x145 = 58.6%: the popup's own background IS the panel's
#     background (the overlay applies the panel style to its menu), so a large part of the
#     rectangle is pixel-identical to what was drawn there before. Both places now apply ONE
#     rule - menu_shaped: a menu-sized rectangle, inside the viewport, with thousands of
#     changed pixels - and which rectangle it is is settled by the clamp assertion below.
#     (2) THE POPUP IS CLAMPED INTO THE VIEWPORT, not hung at the pointer: the menu measures
#     about 202x145 and the viewport is 210x164, so its x is clamped and, being taller than the
#     room left below the pointer, it is pushed UP - which puts its own top edge under the
#     project tab bar that is drawn over the viewport's first 18 px. MEASURED in the recorded
#     run the popup's rectangle is 202x145+179+39. The clamp assertion holds for that
#     rectangle, and that is not luck: if H is the popup's height and C the part the bar hides,
#     the visible top is V_B+1-H+C = V_B+1-(H-C) = V_B+1-MBH, i.e. the same rule, expressed in
#     the measured rectangle. MEASURED, the scan finds the key at (302,103), offset 2 0 from
#     the published plot centre - the published rect and the drawn panel agree here, so the
#     14 px note under WHAT IS NOT COVERED is about where the ink starts (this zoom has no top
#     ticks), not about where the keys are.
#     (3) THE ENTRY CLICK IS MADE ONCE, and making that true took a fix in the overlay. An
#     ImGui Selectable fires on the RELEASE, and ImGui applies a queued press and its release
#     on TWO frames rather than one (io.ConfigInputTrickleEventQueue, ImGui's own default: the
#     two are split so neither is flattened away), so the frame that carried the press cannot
#     be the frame that fires the entry. MEASURED BEFORE THE FIX, with no unrelated frame in
#     between: a single click at the derived point left the popup ON SCREEN with the entry
#     unpressed and the state line unchanged - the release was simply still sitting in ImGui's
#     queue, because the press's own frame was the last frame anybody asked for - and a SECOND
#     press at the same point was what produced the frame that carried it (the state line then
#     moved to the chosen easing and the popup closed; the two presses were more than a
#     double-click interval apart, so the widget saw two plain clicks). THE FIX IS IN THE
#     FRAME, where the fact is observable: the overlay's frame ends by asking for its own
#     successor while ImGui still holds input for it (ImGuiOverlay::imguiHasQueuedInput). A
#     requestRepaint() from the event handler cannot stand in for it - Qt coalesces everything
#     requested while a paint is already pending into that one pending paint, which is why the
#     consumed press's own repaint, and the widget's update() for it, were both already there
#     and still not enough.
#     THE HALF OF THAT STORY THAT IS THIS WIDGET, and not a defect: while the menu is open the
#     overlay answers a left press as the MENU's and returns before MainDisplayWidget hands it
#     to its cursor (ImGuiOverlay::press), so the release that follows reads as a DUPLICATE
#     there (core::AbstractCursor::setMouseReleaseImpl answers false for a release with no
#     press) and schedules no repaint of its own. The press belonged to the popup, so that is
#     what it should do - and it is why the frame the popup needs has to come from the overlay.
#     AND WHY THERE IS A PHASE 4b UNDER IT. The state line is the only readback the menu's
#     effect HAS while the panel shows ONE key: graph::Curve::buildPolyline emits nothing for a
#     single-key map, so no segment is drawn at all; the panel paints its summary line only
#     while the model is EMPTY; and MEASURED, changing that one key's easing moves 0 pixels.
#     Phase 4b therefore MAKES the segment - a second key one frame right of the first,
#     carrying a DIFFERENT value, added through the dock's own add-key knob and its Angle spin
#     box with a real edit (the box's handler hangs off editingFinished, which an
#     accessibility write does not fire) - and then asserts the CURVE'S own pixels: the
#     segment appears with the second key, re-shaping it from Sine to Linear moves the
#     polyline, and picking the SAME easing a second time moves exactly 0 px, which is what
#     makes the first assertion about the EASING rather than about the menu having been used.
#     AND THE MENU'S KEYBOARD, which is the other half of this phase. ImGui's popup keyboard
#     behaviour - Escape closing the front popup, the arrows moving the popup's own entry
#     HIGHLIGHT, Enter activating the highlighted entry - is gated on
#     ImGuiConfigFlags_NavEnableKeyboard, and the overlay sets that flag for EXACTLY as long
#     as this popup is open: with it set, ImGui claims the keyboard (io.NavActive ->
#     io.WantCaptureKeyboard, and io.ConfigNavCaptureKeyboard is true by default) and this
#     overlay covers the whole viewport, so a globally set flag would stop the application's
#     own key commands (gui::KeyCommandMap, routed through MainDisplayWidget::keyPressEvent
#     and gated again in gui::KeyCommandInvoker) from reaching MainWindow for as long as the
#     panel is on screen. So the phase drives BOTH SIDES of the toggle, and every readback is
#     either the panel's own state line (the document) or a diff of the popup's own pixels
#     (the menu): with the menu OPEN, Delete - an application command that would remove
#     exactly the key the menu was opened on - must NOT reach the timeline, Escape must close
#     the popup without moving the line, the arrows must draw and move the highlight inside
#     the popup's measured rectangle, and Return must activate the entry the arrows reached
#     (the second of the shared display order, Round, reached with Right from the entry the
#     highlight starts on - MEASURED in a standalone ImGui 1.92.7 harness driving this exact
#     fixed two-column layout, then confirmed by the run); and with the menu CLOSED, ctrl+Z
#     must undo the last edit and the line must say so. THE HARNESS IS ALSO THE CONTROL for
#     the gate itself: with the flag left off, Escape does NOT close the popup (MEASURED) and
#     no arrow draws anything.
#     AND THAT IS THE MUTATION THIS HALF WAS RECORDED AGAINST - one line of
#     ImGuiOverlay::paint, the ImGuiConfigFlags_NavEnableKeyboard toggle, plus the
#     ImGui::SetNextFrameWantCaptureKeyboard(true) beside it, replaced by nothing (git
#     checkout restores it). RUN: the probe ends `=== probe_graph_overlay: 9 FAILED ===` and
#     exit 1, and the lines below are the probe's own, so a red run can be read against them
#     one for one:
#       * FAIL  with the key menu OPEN the application's own Delete command does not reach the
#         timeline (the panel's state line is unchanged: [none], and it read [1 channel(s)
#         shown, focused: FIGURE : Rotate, selected key: Rotate Lin] before the menu was opened)
#       * FAIL  Escape closed the key menu WITHOUT changing anything: the panel's state line
#         still reads [none], exactly the pre-menu [...]
#       * FAIL  and the popup's own pixels stopped being drawn (0 px changed against the
#         menu-open control, bar 17574 = 60% of the popup's own 202x145 rectangle)
#         - the bar in that line has since been corrected to the popup's MEASURED own size
#         (80% of the px its opening changed; see THREE THINGS THE PHASE HAD TO LEARN (1)):
#         60% of the rectangle is 17574 px, while the real menu changes 17165 of them.
#       * FAIL  the menu was reopened after Escape closed it, at the same key and the same
#         place (none against the first 202x145+179+39)
#       * FAIL  the Down arrow drew the popup's own entry highlight INSIDE its rectangle
#         (39873 px changed, [610x352+92+39], inside 202x145+179+39)
#       * FAIL  and Return activated the entry the arrows reached: the panel's state line
#         names the second entry of the menu's own display order, Round ([none], which read
#         [none] before the keyboard)
#       * FAIL  with the key menu CLOSED the application's own Undo key command still reaches
#         the timeline: the line went from the keyboard's [none] back to [... Rotate Lin] ...
#     The cascade in that run is the mutation's own doing and is expected: without the gate
#     the Delete reaches the timeline and removes the key the phase is about, so the two
#     assertions that read that key (`choosing an easing from the menu changed the selected
#     key's easing to it`, and 4b's `re-shaping that segment moves the CURVE's own pixels`)
#     fail with it. ONE OF THE EIGHT NEW CHECKS IS NOT FALSIFIED BY THAT MUTATION and is
#     recorded as such rather than implied: `and the popup closed on that activation, by its
#     own pixels` measured a full popup's worth of change even in the mutated run, because
#     the derailed flow closed the popup for another reason. What makes the keyboard pick
#     non-vacuous is the assertion beside it - the line naming Round, which does go red.
#
#   * PHASE 5, THE WHEEL. Which side gets a wheel over the viewport is decided in
#     ctrl::Driver::updateGraphWheel, and MEASURED here in both directions at ONE point (the
#     viewport's centre) with the same 3 notches: with the panel VISIBLE the wheel moves the
#     panel's own pixels (798 px = 1.27% of the crop - the frame ladder relaid out) and leaves
#     every pixel outside the panel's ink box byte-identical (0 px), while with the panel
#     HIDDEN the same wheel scales the artwork (12842 px = 20.5% of the crop, changed region
#     96x137+223+18 - the viewport). The first is the claim "the wheel is the graph's while it
#     is up, unconditionally"; the second is its control, since it is the same region and the
#     same gesture with the other owner. The wheel is also the ONLY thing in this probe that
#     writes the panel's frame window, so the phase restores it (unasserted) and phase 4's own
#     Fit graph settles it after that. NEITHER WHEEL CAPTURE FORCES A PAINT: the frame is the
#     application's own (MainDisplayWidget::wheelEvent asks for it when the graph took the
#     event), so the visible case would go red if that request were dropped - the same way it
#     did before it was added, when the whole phase needed a toggle cycle to see the zoom.
#   * THE RING CAPTURE, which is NOT a gate and does not carry a threshold. One frame is taken
#     for the EYE: the panel drawn over the artwork ZOOMED IN, so that its white ink carries
#     its 1px dark outline ring (the contrast-outline pass) over something bright. The recipe
#     is the wheel phase 5 has just driven in both directions, used the other way round: the
#     graph hidden so the wheel is the CANVAS's, zoomed IN at the brightest area the artwork
#     has (found by blurring a capture of it taken with the graph hidden), and then the graph
#     shown again over that. MEASURED in the run recorded here, the anchor lands on the
#     character's face and hair and 12 notches (about 3x) puts them under the whole panel: the
#     tick row, the value column, the zero rule and the state line all sit on skin and hair.
#     The only assertions about it are that the file exists and is not a black frame; the mean
#     luminance of the artwork under the panel's ink box is RECORDED beside it in the geometry
#     file. Whether the ring READS at that zoom is the reader's judgement, which is exactly
#     why it is not an assertion here. See the (iii) comment in the body for the derivation of
#     the anchor and for why the zoom round-trips exactly.
# WHERE THE MEASUREMENTS ARE MADE, AND WHAT THE CROP IS (this probe used to ask for the
# project tab strip and crop from its bottom edge; that is gone, and this is why).
# MEASURED: the tab strip is not in the accessibility tree of this build AT ALL. The full
# role inventory of `ae dump` (the whole window, every node) contains no `page tab`, `tab`
# or `page tab list` role under any name, with the panel on and with it off; the only
# table-ish nodes are the object tree's own (`table column header`, `table cell`), and the
# roles that do answer are `frame`, `menu bar`, `menu item`, `panel`, `label`, `button`,
# `check box`, `spin button`, `slider`, `list`, `tree` and the fillers around them. Earlier
# passes of this probe did not notice because they ran against projects whose FILE name and
# tab title agreed, so `ae extents` matched a node that happened to carry the project's
# name. The crop is therefore derived from nodes that DO answer:
#   * the window's client area, `ae extents AnimeEffects frame` - the top-level frame
#     [0 0 702 455] - which bounds the crop on the left and provides the x origin the
#     screen capture is cropped at (the window is at the screen origin under Xvfb, but the
#     crop says so rather than assuming it);
#   * `Properties` [382 21 320 434]: its LEFT edge is where the viewport ends;
#   * `Animation` [0 185 382 270]: its TOP edge is where the viewport ends;
#   * the menu bar's bottom, which is where the crop starts. The menu bar itself has NO
#     accessible name (measured: the frame's own row is `menu bar` with an empty name
#     beside `panel Properties`, `panel Animation` and `panel Tools`), so it cannot be
#     asked for; the dock area starts exactly under it, so the TOP EDGE OF THE
#     PROPERTIES DOCK is that line (measured y=21, the same as the Tools dock's
#     [0 21 172 164]), and the frame's top is asserted to be above it.
# ONE KIND OF RECTANGLE THIS PROBE DOES NOT TRUST, and phase 3 is the only place it
# matters: the object tree's rows report their full LOGICAL width, not what the tree's
# viewport shows. MEASURED: "Folder16" reports 40 221 115 22 while the dock's splitter
# leaves the tree about 68 px wide, so a click at the CENTRE of such a rect lands in the
# timeline beside the tree - and opens the TIMELINE's menu (read off a screenshot: Copy
# key / Copy key to clipboard / Paste key / Select spacing between keys / Change key(s)
# easing to / Change key(s) range to / Delete key) instead of the tree's. Phase 3
# therefore clicks the LEFT edge of a row rect + 4 px, which is inside the row and inside
# the tree. Nothing above uses a tree row's rect as a bound: the crop comes from the two
# docks and the frame (this paragraph is why).
# So the crop is 382x164+0+21 = 62648 px: the full width left of the Properties dock,
# from the menu bar's bottom to the Animation panel's top. It contains the whole drawn
# panel (measured x 192..377) and the toggle's highlight, and nothing that changes for
# any other reason.
#
# WHAT IS NOT COVERED, stated so nobody has to measure it again:
#   * the panel's OWN accessible rectangles do not agree with where it draws. MEASURED:
#     the state line reports [222 25 156 156] (the plot) and the legend swatch
#     [228 163 10 10], while the pixels the panel changes are at x 192..377, y 39..181.
#     The x is consistent with the drawn panel - 192 is where its leftmost ink is (the
#     value column and its labels, left of the plot at 222) - but the published top (25)
#     is 14 px ABOVE the highest pixel the panel paints (39), so the two spaces are offset
#     vertically. Nothing here uses those rectangles as geometry: the crop comes from the
#     docks, the legend is activated through the AT-SPI ACTION interface (never at a
#     coordinate), and the assertions are about pixels. The offset is the accessibility
#     layer's business, not this probe's, and it is recorded here rather than worked
#     around.
#   * the tab-strip role is asserted nowhere here; a build that starts enumerating the tab
#     strip would neither fix nor fail this probe.
#   * the object tree's CONTEXT MENU is not published to AT-SPI, so phase 3 cannot address
#     its entries by accessible name - the route every other step of this probe uses.
#     MEASURED with the menu open on screen and its window mapped IsViewable: the tree of
#     the app still held exactly the five menu-bar popups and no `menu item` named Delete,
#     while a desktop-wide walk found "Delete" only in OTHER applications on the bus. The
#     menu is a QMenu created on the stack inside ObjectTreeWidget::onContextMenuRequested
#     and, unlike the menus of the menu bar (which are published even while closed), it is
#     never exposed. Phase 3 therefore finds the menu by its WINDOW - the app opens it at
#     the pointer, so the window whose origin is the click point is it - and drives it with
#     Up+Return after pointing the input focus at that window: that is the mechanism this
#     probe can actually drive, because there is no window manager (the menu takes no focus
#     by itself and keys sent without focusing it do nothing - MEASURED, Up+Return left the
#     row in place), and the menu type-ahead is no name route either (MEASURED: typing
#     d,e,l,e,t,e then Return deleted nothing). WHICH entry that presses is bounded by the
#     SOURCE (onContextMenuRequested adds mDeleteAction last) and by a screenshot read of
#     the menu (Contract / Rename / Create layer / Create folder / Paste from clipboard /
#     Duplicate node / Add default keys / Delete - exactly ONE wording means
#     remove-this-layer), and then by the EFFECT: a wrong entry leaves the row in place,
#     which is an assertion rather than a hope.
#
# ---- POSITIVE CONTROL: PROOF THAT THIS PROBE CAN GO RED ------------------------------
# A gate that cannot be shown to fail is not a gate. The recipe below is written, not run
# here, because it needs a build and this machine runs one build at a time.
#
# THE MUTATION: ctrl::GraphEditor::setVisible turned into a no-op. The toggle still
# flips the view setting, MainDisplayWidget still calls the driver, the setting still
# reaches setVisible - and nothing ever becomes visible, so MainDisplayWidget::paintEvent
# keeps passing `editor && editor->isVisible()` = false to gui::ImGuiOverlay::paint and
# the panel is never drawn nor published. It is the smallest break that removes the
# FEATURE while leaving its wiring intact, and it touches one line.
#
# WITH scripts/mutation_check.sh (it takes a tab-separated spec, builds the mutants itself,
# restores the file byte-exactly with `cmp`, and REQUIRES the reverted tree to be green
# again - a run that cannot be restored stops with REVERT-FAILED):
#     cat > /tmp/graph_overlay_mutation.tsv <<'TSV'
#     graph_overlay_setvisible_noop<TAB>src/ctrl/GraphEditor.cpp<TAB>s/void GraphEditor::setVisible\(bool aVisible\) \{/void GraphEditor::setVisible(bool aVisible) { (void)aVisible; return;/<TAB>src/gui/Debug/AnimeEffects<TAB>bash scripts/gui/probe_graph_overlay.sh
#     TSV
#     scripts/mutation_check.sh /tmp/graph_overlay_mutation.tsv graph_overlay
#   (<TAB> is a real tab; the harness refuses to start while another ninja builds or while
#   an AnimeEffects instance is running, both of which would poison its verdict.)
#
# WITHOUT the harness, the same break by hand - patch that one line of
# src/ctrl/GraphEditor.cpp, then:
#     ninja -C build src/gui/Debug/AnimeEffects
#     bash scripts/gui/probe_graph_overlay.sh ; echo "exit=$?"
#     git checkout -- src/ctrl/GraphEditor.cpp
#     ninja -C build src/gui/Debug/AnimeEffects
#     bash scripts/gui/probe_graph_overlay.sh ; echo "exit=$?"   # green again
#
# WHAT A WORKING PROBE MUST REPORT UNDER THAT MUTATION (all of it is expected, and the
# run must end non-zero - `=== probe_graph_overlay: N FAILED ===` and exit 1, or the
# mutation is a survivor and this probe is the finding). The lines below are the probe's
# own messages, so a red run can be read against them one for one:
#   * FAIL  toggling the graph ON redraws the viewport (0 px changed on the whole screen, bar 1000)
#   * FAIL  the panel drew inside the crop (0 px = 0.00% of 382x164, floor 2%)
#   * FAIL  the changed region is not empty (0x0)
#   * FAIL  the empty state line names the empty state (the model's own sentence is the accessible label, rect none)
#   * FAIL  the empty panel still drew its state line (0 px = 0.00% of the crop, floor 2%)
#   * FAIL  the Current Keys add-key knob is addressable for the layer the tree had selected (last row activated: none; rect none)
#   * FAIL  the press turned the knob row into the channel rows, i.e. the key now exists (Angle row none)
#   * FAIL  the phase-2 selection publishes exactly one legend row (0 rows: none)
#   * FAIL  activating the legend item flips the panel's own state to hidden (absent)
#   * FAIL  hiding the legend row removes its curve from the crop (0 px = 0.00% gone, bar 200)
#   * FAIL  the panel falls back to one of the model's own empty-state sentences after the
#     deletion (none; rects: empty none, no-keys none, all-hidden none)
#   * FAIL  the right-click opened a popup whose pixels are a menu, not a sliver (none)
#   * FAIL  with the graph visible a wheel over the viewport moves the panel alone (0 px ...)
#     and FAIL  and the canvas is byte-identical ... (the panel never being visible hands the
#     wheel to the canvas, so BOTH wheel assertions go red under this mutation, in opposite
#     directions - the visible case stops moving and the hidden case starts)
#   * FAIL  the second key reached the drawn panel ... (0 px: nothing was drawn at all)
# EXPECTED, not measured here (the build is yours to run): the whole-screen change under
# that mutation is the toggle's own checked highlight and nothing else - 568 px is what
# that highlight measures on this build, which is exactly why the bars sit above it. The
# ceiling assertions (the panel is not a wash, the empty state is text only) are expected
# to still PASS on 0 px, and that is correct rather than a gap: a panel that draws nothing
# cannot be a wash. What the ceiling buys is the other direction - it cannot be cleared by
# a panel that fills its background. A run that ends GREEN under this mutation is the
# finding: it would mean no assertion above depends on the panel being drawn.
#
# PHASE 3 UNDER THAT SAME MUTATION, read the same way. The Current Keys knob is a widget of
# the Properties dock, not of this overlay, so the deletion still happens: the tree row is
# found, the menu opens at it, the row goes, and the process survives - those three
# assertions are expected to PASS, and they are about the tree rather than about the panel.
# The one line that must go red is the sentence one above: with setVisible a no-op the
# overlay publishes nothing at all, so no empty-state sentence is there to be found, while
# the legend-row count (0) passes vacuously. Note also what this mutation does NOT
# exercise: it never leaves a destroyed node in a live target list, so the assertion that
# carries THIS gate's verdict - the process still being alive after the deletion - is
# expected to pass under it. THAT control is the other direction and is named rather than
# written out, because it changes different code: the gate exists for the ORDERING that
# takes the node out of ctrl::GraphEditor::mTargets before the next draw (the tree's
# selection change, MainWindow's setGraphTargets), so anything that lets a destroyed node
# stay in that list across a repaint - dropping the selection re-emit on the deletion path,
# or moving setGraphTargets off the tree's selection signal - is what phase 3 is aimed at,
# and a use-after-free there is a dead process rather than a red assertion.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# ---- PIN THE PROJECT, OR THE PROBE'S OWN SUBJECT DRIFTS (see the header) ----
# _lib.sh, given no project, opens "the smallest .anie under ~/Downloads" - so the
# probe's data was whatever the filesystem happened to hold: a run recorded against
# `running through one_s head.anie` measured object-tree cells named `Folder16` and a
# phase-3 assertion flipped when a different project became the smallest. A probe whose
# data comes from the current contents of ~/Downloads cannot be compared between runs.
#
# The pin is the repository's own fixture (the one tests/media_exporter_e2e and
# probe_exportbg use). It cannot be the project the app OPENS: the application's command
# line opens only .anie (Main.cpp:212), so this argument is NOT opened - MEASURED, the
# app's log then names its own Debug startup project assets/sample.psd (AE_SAMPLE_PSD,
# Main.cpp:208), which is also in the repository. That is what the probe runs on, and it
# says so below rather than pretending the fixture is loaded. What the argument buys is
# the one thing the probe needs: it is a real file, so _lib.sh never falls back to the
# drifting search. Pass arg 1 or AE_PROJECT to test another project; the pin assertions
# are skipped then, because the subject is the caller's choice and not this pin.
PIN_FIXTURE="$SCRIPT_DIR/../../tests/fixtures/e2e_stage.png"
PINNED=0
if [ -z "${1:-}" ] && [ -z "${AE_PROJECT:-}" ]; then
    set -- "$PIN_FIXTURE"
    PINNED=1
fi
source "$SCRIPT_DIR/_lib.sh" "$@"

rm -f /tmp/probe_graph_overlay_geom.txt

# SCRATCH ON THE REPOSITORY FILESYSTEM, NOT THE QUOTA-ED tmpfs. /tmp on this machine is
# a tmpfs with a 19 GB per-user quota, and this probe's own runs have filled it: a 20 GB
# /tmp produced "Disk quota exceeded" mid-run (measured 2026-10-14), which is a failure
# of the filesystem, not of the panel, and it surfaces as a mysteriously dead run.
# Everything that honours TMPDIR - ImageMagick above all, which this probe calls for
# every pixel comparison - now writes beside the repository, and the directory goes away
# with the script.
#
# AND IT MUST BE A DIRECTORY THAT EXISTS: the first version of this used
# "$AE_PROBE_DIR/build/tmp-probe-XXXXXX", and $AE_PROBE_DIR is a bare `mktemp -d` with no
# `build` in it, so the mktemp FAILED ("no such file or directory"), TMPDIR came out EMPTY,
# every scratch consumer fell back to /tmp - the quota-ed tmpfs this block exists to avoid -
# and the trap removed nothing. Measured 2026-10-14 on the run that still died at 37 s.
# $ROOT/build is the repository's own build tree: it exists whenever the binary this probe
# needs exists, and it is on the same 23 GB filesystem, so no quota can reach it.
export TMPDIR="$(mktemp -d "$ROOT/build/tmp-probe-XXXXXX")"
trap 'rm -rf "$TMPDIR"' EXIT

# THE BODY GOES IN ON STDIN, NOT AS A `bash -c '<body>'` ARGUMENT. THIS IS NOT STYLE.
#
# MEASURED 2026-10-14, the failure this replaces: the body used to be one single-quoted
# argument, and an APOSTROPHE in any of those comments - the body is 1800 lines and its
# comments are full of them - ENDS that quoted string. The shell then went on splitting
# what followed into ordinary words, so `bash -c` was handed THIRTEEN arguments: it ran the
# first 12355 bytes of the 101876-byte body as the script and turned the remaining 89453
# bytes into positional parameters that nothing ever read. The body therefore died at the end
# of that comment, which is 37 s in (wait_ready had just returned), writing NOTHING:
#   * no /tmp/probe_graph_overlay_geom.txt (the body's first write is its geometry one),
#   * an empty /tmp/probe_graph_overlay_body.log (nothing had failed, so nothing was said),
#   * Xvfb killed by xvfb-run's own teardown and the application logging "The X11 connection
#     broke (error 1). Did the X11 server die?" - which is what sent this investigation after
#     an X server that was never the problem.
# The two apostrophes that did it were in the comment "THE FIELD'S OWN ... a spin box's
# Value" at the top of angle_value(); the git-era snapshot /tmp/probe_graph_overlay.sh.good
# (Oct 13) has a body of ONE 79243-byte word and runs, so this was a comment-only regression.
#
# A QUOTED heredoc cannot suffer this: `<<'AE_PROBE_BODY_END'` is verbatim, apostrophes and
# `$` and backticks included, and its length is bounded by nothing (a single ARGUMENT is
# limited to MAX_ARG_STRLEN = 128 KB, which this body was within ~40 KB of). The terminator
# must be a line that appears NOWHERE in the body; keep it that way.
#
# Two consequences to hold on to when editing the body:
#   * `$SHOTS`, `$ROOT`, `$APP` and `$PROJ` are EXPORTED, so the body expands them itself -
#     the `"$SHOTS/x.png"` dance the body used to need is gone (it existed only because
#     the OUTER shell expanded it). Write `"$SHOTS/x.png"` there now.
#   * the body is fed on stdin, so a bare `read` in it would eat the script; every `read`
#     in the body is redirected (`<<<` / `done < <(...)`), and a new one must be too.
xvfb-run -a --server-args="-screen 0 1440x900x24" bash -s 2>&1 <<'AE_PROBE_BODY_END' | tee /tmp/probe_graph_overlay_body.log | tail -4
  cd "$AE_PROBE_DIR"
  "$APP" "$PROJ" >/tmp/probe_graph_overlay.log 2>&1 &
  app=$!
  AE_UI_PID=$app; export AE_UI_PID
  if ! wait_ready 180; then
      echo "TIMEOUT waiting for the main window" > /tmp/probe_graph_overlay_geom.txt
      kill $app 2>/dev/null; wait $app 2>/dev/null; exit 0
  fi
  W=$(xdotool getactivewindow 2>/dev/null || xdotool search --name AnimeEffects | tail -1)
  xdotool windowactivate --sync $W 2>/dev/null
  sleep 1

  G=/tmp/probe_graph_overlay_geom.txt

  # Park the pointer off every control before every capture: a hover highlight on a
  # button or on a row is a pixel change of its own.
  park() { xdotool mousemove --sync 900 700; sleep 0.8; }

  # NO FORCED REPAINT ANY MORE. This probe used to force the panel to draw its current
  # content - by toggling the graph off and on - before every capture of a CHANGED panel
  # state, because MEASURED WHEN IT WAS FIRST WRITTEN the HUD did not redraw for a change
  # made while it was already on screen (across a key press the central widget changed
  # 0 px), and because the graph own WHEEL asked for no paint at all: updateGraphWheel
  # returned from MainDisplayWidget::wheelEvent before the viewport own updateRender, so
  # the new frames-per-pixel stayed invisible until something unrelated repainted.
  #
  # EVERY ONE OF THOSE PATHS NOW SCHEDULES THE FRAME ITSELF, and each was re-measured in a
  # run of this script with every toggle removed: the wheel asks for the frame in
  # MainDisplayWidget::wheelEvent (the same request the cursor path makes), a key push and
  # an undo reach MainDisplayWidget::onVisualUpdated through DriverHolder (the slot it keeps
  # on onTimeLineModified, which MainWindow connects to the viewport), a legend toggle asks
  # for one in ImGuiOverlay::onAccessibilityAction, and the menu entry is followed by the
  # overlay own successor frame (ImGuiOverlay::imguiHasQueuedInput, at the end of paint).
  # So the captures below are taken straight after the state change with nothing in
  # between, and a phase whose pixels then do NOT move is a real finding rather than a
  # missing toggle.
  #
  # AND ONE THING THE REMOVED CYCLE DID BESIDES PAINT, which is why three recorded numbers
  # moved in the same pass instead of only being re-measured: the toggle ran the panel own
  # fit on the way past - GraphEditor::setVisible applies the pending focus fit when
  # hiding and syncs the value axis when showing - so a capture taken after one was a
  # capture of a window that had been refitted. Re-recorded from the no-toggle run: the
  # key push moves 2162 px inside the crop (was 2637), the whole screen 12807 (was
  # 13282), and hiding the legend row 2078 (was 2116). The bars and the shapes of the
  # assertions are untouched.

  # ---- the overlay DRIVES its own items through accessibility, and so does this probe
  #
  # The geometry assertions below are about pixels, but the panel is a drawn surface with
  # no widgets in it: the only handles on its controls are the accessible items it
  # publishes (a11y::sync, see gui/ImGuiOverlay.cpp). Both helpers read the tree through
  # the repository own tooling (scripts/gui/ae_ui.py, imported rather than re-implemented)
  # and are pid-guarded by it, so a second AnimeEffects on the bus cannot be addressed.
  #
  # legend list            -> one line per legend row: "<label>\t<checked|unchecked>"
  # legend toggle <label>  -> flips that row through the AT-SPI Toggle action and waits
  #                           for the state bit to move; prints "STATE <before> -> <after>"
  #                           and exits 0 only when it really moved.
  #
  # A legend row is a CheckBox whose accessible NAME is the model label, which
  # GraphPanelModel builds as "<layer> : <channel>" (measured: "Folder16 : Rotate"). That
  # " : " is the filter, and it is the panel own convention rather than a guess about the
  # Qt tree: every other check box in this window is named after its panel ("View
  # Settings", "Toolbox", ...) and carries its meaning in the DESCRIPTION instead.
  legend() { # legend list | legend toggle <exact row label>
    python3 - "$@" <<PY
import sys, time, warnings
warnings.filterwarnings("ignore")
sys.path.insert(0, "$ROOT/scripts/gui")
import ae_ui
from gi.repository import Atspi

def rows():
    app = ae_ui._app()
    out = []
    if app is None:
        return out
    for _depth, role, name, node in ae_ui._walk(app):
        if role == "check box" and " : " in name:
            out.append((name, node))
    return out

def state(node):
    try:
        return "checked" if node.get_state_set().contains(Atspi.StateType.CHECKED) else "unchecked"
    except Exception:
        return "?"

mode = sys.argv[1] if len(sys.argv) > 1 else "list"
if mode == "list":
    for name, node in rows():
        print("%s\t%s" % (name, state(node)))
    sys.exit(0)

want = sys.argv[2]
node = None
for name, cand in rows():
    if name == want:
        node = cand
        break
if node is None:
    print("absent")
    sys.exit(1)
before = state(node)
# The Toggle action, which the panel advertises first for a check box and which fires the
# very callback a pointer press on the swatch fires.
if not ae_ui.do(node, "toggle"):
    print("NOACTION")
    sys.exit(2)
after = before
deadline = time.time() + 5.0
while time.time() < deadline:
    now = None
    for name, cand in rows():
        if name == want:
            now = state(cand)
    if now is not None and now != before:
        after = now
        break
    time.sleep(0.2)
print("STATE %s -> %s" % (before, after))
sys.exit(0 if after != before else 1)
PY
  }

  # The Angle field current value, read from the UNNAMED spin box that sits beside the
  # "Angle" label in the Current Keys panel. It cannot be reached by name - measured: the
  # spin button has no accessible name at all, and `ae value Angle` therefore answers with
  # the label own text - so it is read here from the Value interface of the first spin
  # button that follows a SHOWING "Angle" label in document order (the Rotate group comes
  # before the Pose group in the tree, and only the expanded one is on screen). Empty
  # output and a non-zero exit when the row is not there, which is exactly what "the key
  # does not exist yet" looks like.
  #
  # THE SHOWING FILTER IS THE POINT, and it was measured the hard way: Qt keeps a HIDDEN
  # widget in the accessibility tree, so the first version of this helper read the value
  # "0" out of the Angle field of the Rotate group BEFORE the press, while
  # prop::CurrentKeyPanel::RotateKeyGroup had that whole group hidden and its own knob row
  # standing in for it. `ae extents` does not have that problem because it asks for
  # visible nodes only (ae_ui.find) - so this helper asks the same question through
  # ae_ui.showing, which is what makes "the value is not readable before the press" a
  # statement about the panel rather than about the walk.
  angle_value() {
    python3 - <<PY
import sys, warnings
warnings.filterwarnings("ignore")
sys.path.insert(0, "$ROOT/scripts/gui")
import ae_ui

app = ae_ui._app()
if app is None:
    sys.exit(1)
for _depth, role, name, node in ae_ui._walk(app):
    if role != "label" or name != "Angle":
        continue
    if not ae_ui.showing(node):
        continue
    parent = node.get_parent()
    if parent is None:
        continue
    seen = False
    for i in range(parent.get_child_count()):
        child = parent.get_child_at_index(i)
        if child is None:
            continue
        crole = child.get_role_name() or ""
        if not seen:
            if crole == "label" and (child.get_name() or "") == "Angle":
                seen = True
            continue
        if crole != "spin button" or not ae_ui.showing(child):
            continue
        iface = child.get_value_iface()
        if iface is None:
            continue
        value = iface.get_current_value()
        print(int(value) if float(value).is_integer() else value)
        sys.exit(0)
sys.exit(1)
PY
  }

  # THE SAME NODES SCREEN RECTANGLE, for the one place this probe WRITES to the dock
  # (phase 4b own second key needs a value the knob own seeding cannot give it). The write is
  # a real edit - click, select all, type, Return - because the item own change handler hangs
  # off QAbstractSpinBox::editingFinished (prop_Items.cpp:543) and an AT-SPI value write
  # does not fire it. The rectangle comes from the widget itself (ae_ui own own idiom: the
  # SCREEN extents, which for a QWidget in this window are already in window coordinates),
  # so a layout change moves the click with it.
  angle_rect() {
    python3 - <<PY
import sys, warnings
warnings.filterwarnings("ignore")
sys.path.insert(0, "$ROOT/scripts/gui")
import ae_ui
from gi.repository import Atspi

app = ae_ui._app()
if app is None:
    sys.exit(1)
for _depth, role, name, node in ae_ui._walk(app):
    if role != "label" or name != "Angle":
        continue
    if not ae_ui.showing(node):
        continue
    parent = node.get_parent()
    if parent is None:
        continue
    seen = False
    for i in range(parent.get_child_count()):
        child = parent.get_child_at_index(i)
        if child is None:
            continue
        crole = child.get_role_name() or ""
        if not seen:
            if crole == "label" and (child.get_name() or "") == "Angle":
                seen = True
            continue
        if crole != "spin button" or not ae_ui.showing(child):
            continue
        comp = child.get_component_iface()
        if comp is None:
            continue
        rect = comp.get_extents(Atspi.CoordType.SCREEN)
        print("%d %d %d %d" % (rect.x, rect.y, rect.width, rect.height))
        sys.exit(0)
sys.exit(1)
PY
  }

  # WHO HOLDS THE KEYBOARD FOCUS, recorded around the typed write in phase 4b. A property
  # field commits on QAbstractSpinBox::editingFinished - Enter, or the focus leaving the
  # field - so a write that lands and does not commit is either a FOCUS problem (the field
  # never saw the Return) or a TARGET problem (it wrote a key the readback does not name).
  # The accessibility tree is the only instrument this probe has for the focus, and this
  # reads the two things that decide it: every node that holds FOCUSED at that moment (the
  # viewport or the panel holding it is the failure mode that matters, because gui::ImGuiOverlay
  # takes the Qt focus while the key menu is open and must hand it back when the menu closes),
  # and the Angle field own states.
  dock_focus() {
    python3 - <<PY
import sys, warnings
warnings.filterwarnings("ignore")
sys.path.insert(0, "$ROOT/scripts/gui")
import ae_ui
from gi.repository import Atspi

def states_of(node):
    out = []
    try:
        ss = node.get_state_set()
    except Exception:
        return "?"
    for attr in ("SHOWING", "ENABLED", "FOCUSED", "EDITABLE"):
        if not hasattr(Atspi.StateType, attr):
            continue
        try:
            if ss.contains(getattr(Atspi.StateType, attr)):
                out.append(attr.lower())
        except Exception:
            pass
    return " ".join(out) if out else "none"

def holds_focus(node):
    try:
        return node.get_state_set().contains(Atspi.StateType.FOCUSED)
    except Exception:
        return False

app = ae_ui._app()
if app is None:
    sys.exit(1)
focused = []
angle_label = None
for _depth, role, name, node in ae_ui._walk(app):
    if holds_focus(node):
        focused.append("%s|%s" % (role, (name or "")[:36]))
    if angle_label is None and role == "label" and (name or "") == "Angle":
        angle_label = node
print("focus-holders: %s" % ("; ".join(focused) if focused else "none"))
if angle_label is None:
    print("angle-field: absent")
    sys.exit(0)
parent = angle_label.get_parent()
if parent is None:
    print("angle-field: no-parent")
    sys.exit(0)
seen = False
for i in range(parent.get_child_count()):
    child = parent.get_child_at_index(i)
    if child is None:
        continue
    crole = child.get_role_name() or ""
    if not seen:
        if crole == "label" and (child.get_name() or "") == "Angle":
            seen = True
        continue
    if crole != "spin button":
        continue
    print("angle-field: %s" % states_of(child))
    sys.exit(0)
print("angle-field: no-spin-button")
PY
  }

  # THE FIELD'S OWN IN-PROGRESS TEXT, which is NOT what angle_value reads: a spin box's Value
  # interface answers QAbstractSpinBox::value(), i.e. the COMMITTED value, so it reads 0 while
  # the edit is still open even when the line edit already holds the typed characters. Qt
  # exposes the editable line edit as a CHILD of the spin button, and its accessible name is
  # its text - that is what says whether the typed characters ARRIVED. Read-only.
  angle_text() {
    python3 - <<PY
import sys, warnings
warnings.filterwarnings("ignore")
sys.path.insert(0, "$ROOT/scripts/gui")
import ae_ui
from gi.repository import Atspi

app = ae_ui._app()
if app is None:
    sys.exit(1)
for _depth, role, name, node in ae_ui._walk(app):
    if role != "label" or name != "Angle":
        continue
    if not ae_ui.showing(node):
        continue
    parent = node.get_parent()
    if parent is None:
        continue
    seen = False
    for i in range(parent.get_child_count()):
        child = parent.get_child_at_index(i)
        if child is None:
            continue
        crole = child.get_role_name() or ""
        if not seen:
            if crole == "label" and (child.get_name() or "") == "Angle":
                seen = True
            continue
        if crole != "spin button":
            continue
        out = []
        for j in range(child.get_child_count()):
            sub = child.get_child_at_index(j)
            if sub is None:
                continue
            srole = sub.get_role_name() or ""
            sname = sub.get_name() or ""
            text = sname
            try:
                tiface = sub.get_text_iface()
                if tiface is not None:
                    text = tiface.get_text(0, -1) or sname
            except Exception:
                pass
            out.append("%s[%s]" % (srole, text))
        print("; ".join(out) if out else "no-children")
        sys.exit(0)
print("no-field")
PY
  }

  knock_rotate() {
    python3 - <<PY
import sys, warnings
warnings.filterwarnings("ignore")
sys.path.insert(0, "$ROOT/scripts/gui")
import ae_ui

app = ae_ui._app()
if app is None:
    sys.exit(1)
for _depth, role, name, node in ae_ui._walk(app):
    if role != "label" or name != "Rotate":
        continue
    if not ae_ui.showing(node):
        continue
    parent = node.get_parent()
    if parent is None:
        continue
    for i in range(parent.get_child_count()):
        child = parent.get_child_at_index(i)
        if child is None:
            continue
        if (child.get_role_name() or "") != "push button":
            continue
        if not ae_ui.showing(child):
            continue
        if ae_ui.do(child, "click") or ae_ui.do(child, "press"):
            print("pressed")
            sys.exit(0)
sys.exit(1)
PY
  }

  # WAIT FOR THE PROJECT, NOT FOR A GUESSED NUMBER OF SECONDS. The main window - and the
  # menu wait_ready saw - exists before the project named on the command line finishes
  # loading, and everything below needs that project. The object tree is the signal: its
  # first row appears only once the project is in, and that row is also the one the first
  # selection below is made on. The object tree is the only "table cell" in the whole
  # accessibility tree, and its first row is the top layer.
  ROW=""
  for i in $(seq 1 60); do
      ROW=$(ae dump 2>/dev/null | sed -n "s/^ *table cell *| //p" | head -1)
      [ -n "$ROW" ] && break
      sleep 1
  done

  # ---- THE BOUNDS, ASKED FOR BY NAME (see the header for why not the tab strip) ----
  # Each rectangle is written as an assignment line so the outer shell can eval it back
  # into one "x y w h" value. The frame is asked for by ROLE as well as name: the
  # application node carries the same name, and the frame is the window.
  FRAME=$(ae extents "AnimeEffects" "frame")
  PROP=$(ae extents "Properties")
  ANIM=$(ae extents "Animation")
  [ -n "$FRAME" ] || echo "NOGEOM frame"      >> $G
  [ -n "$PROP" ]  || echo "NOGEOM properties" >> $G
  [ -n "$ANIM" ]  || echo "NOGEOM animation"  >> $G
  echo "FRAME=\"$FRAME\"" >> $G
  echo "PROP=\"$PROP\""   >> $G
  echo "ANIM=\"$ANIM\""   >> $G
  echo "ROW=\"$ROW\""     >> $G

  # ---- EMPTY STATE, BEFORE ANYTHING IS SELECTED ----
  # Nothing is selected at startup, so this is the one moment the panel can be asked for
  # its "no target" sentence: ObjectTreeWidget has no current row until one is clicked.
  park
  import -window root "$SHOTS/graph_probe_01_off_empty.png"

  ae_press "Show animation graph" "the graph toggle is pressable by name"
  sleep 1.2; park
  import -window root "$SHOTS/graph_probe_02_on_empty.png"
  EMPTY_LINE=$(ae extents "Select a layer to show its curves" "label")
  NOKEYS_LINE=$(ae extents "No keys on the selected layers" "label")
  echo "EMPTY_LINE=\"$EMPTY_LINE\""   >> $G
  echo "NOKEYS_LINE=\"$NOKEYS_LINE\"" >> $G

  ae_press "Show animation graph" "the graph toggle is pressable a second time"
  sleep 1.2; park
  import -window root "$SHOTS/graph_probe_03_off_empty.png"

  # ---- THE TOP ROW SELECTED: THE PLAIN PANEL, AND THE OFF/ON/OFF TRIPLE ----
  if [ -n "$ROW" ]; then
      ae_activate "$ROW" "the first object-tree row is selected ($ROW)" "table cell"
  else
      check 1 "the object tree answered with a first row to select (no table cell in ae dump after 60s)"
  fi
  park
  import -window root "$SHOTS/graph_probe_04_off_top.png"

  ae_press "Show animation graph" "the graph toggle is pressable a third time"
  sleep 1.2; park
  import -window root "$SHOTS/graph_probe_05_on_top.png"
  echo "TOP_LINE=\"$(ae extents "No keys on the selected layers" "label")\"" >> $G

  ae_press "Show animation graph" "the graph toggle is pressable a fourth time"
  sleep 1.2; park
  import -window root "$SHOTS/graph_probe_06_off_top.png"

  # ---- PHASE 5: THE WHEEL - THE GRAPHS WHILE IT IS VISIBLE, THE CANVASS WHEN IT IS NOT ----
  # WHAT THIS PROVES, AND WHY NOTHING ELSE CAN. ctrl::Driver::updateGraphWheel hands the
  # wheel to the graph whenever the panel is VISIBLE, unconditionally: a rect test against
  # the panel own rectangle (which is the whole viewport), not the hit test the cursor uses,
  # and it returns false while the panel is hidden so the canvas gets the wheel back
  # (Driver.cpp:215, and MainDisplayWidget::wheelEvent is the only route to either). That
  # routing exists only in a mapped window own QWheelEvent, so no unit test reaches it.
  #
  # WHAT IS ASSERTED, IN BOTH DIRECTIONS, AT ONE POINT INSIDE THE VIEWPORT:
  #   * the graph VISIBLE: the wheel moves the PANEL own own pixels (its frame ticks and
  #     their labels are re-laid-out for the new frames-per-pixel) and the CANVAS region
  #     is BYTE-IDENTICAL - the artwork did not move at all;
  #   * the graph HIDDEN: the same wheel moves the CANVAS region (the artwork scales),
  #     which is also the control that proves the region this phase calls the canvas
  #     really is canvas;
  #   * and the panel publishes nothing at all while it is hidden ("Fit graph" absent),
  #     which is what makes the second case about the canvas rather than about the panel.
  # THE TWO REGIONS ARE MEASURED, NOT ASSUMED, and the outer shell names them: the PANEL
  # region is the bounding box of the OFF->ON change (the panel own own ink, dilated by 2 px
  # so the outline pass own 1 px rings cannot straddle the edge) and the CANVAS region is
  # the crop minus that box. The crop itself is the viewport plus the Tools dock, so the
  # canvas region is the strip of the crop the panel never paints in - the region a canvas
  # zoom would move and nothing else could.
  #
  # THE POINT IS DERIVED FROM THE DOCKS: the viewport own centre, which is right of the
  # Tools dock, left of the Properties dock, below the menu bar own row and above the
  # Animation dock - the same three rectangles the crop comes from, so a layout change
  # moves the point with them rather than leaving it on a dock.
  WHEEL_TOOLS=$(ae extents "Tools")
  read -r WTX WTY WTW WTH <<< "${WHEEL_TOOLS:-0 0 0 0}"
  read -r WPX WPY WPW WPH <<< "${PROP:-0 0 0 0}"
  read -r WAX WAY WAW WAH <<< "${ANIM:-0 0 0 0}"
  WHEEL_X=$(( (WTX + WTW + WPX) / 2 ))
  WHEEL_Y=$(( (WPY + WAY) / 2 ))
  # The viewport rectangle in screen coordinates: left of the Properties dock, right of the
  # Tools dock, under the menu bar row, above the Animation dock. The key menu popup is
  # CLAMPED into it (see open_key_menu), so this is what its rectangle is checked against.
  VIEW_L=$((WTX + WTW)); VIEW_T=$WPY; VIEW_R=$WPX; VIEW_B=$WAY
  WHEEL_NOTCHES=3
  echo "WHEEL_TOOLS=\"$WHEEL_TOOLS\""        >> $G
  echo "WHEEL_POINT=\"$WHEEL_X $WHEEL_Y\""   >> $G
  echo "WHEEL_NOTCHES=\"$WHEEL_NOTCHES\""    >> $G
  echo "VIEW_L=\"$VIEW_L\""                   >> $G
  echo "VIEW_T=\"$VIEW_T\""                   >> $G
  echo "VIEW_R=\"$VIEW_R\""                   >> $G
  echo "VIEW_B=\"$VIEW_B\""                   >> $G

  # One wheel gesture: the pointer at the viewport own centre, then the notches. IN BOTH
  # DIRECTIONS the paint is the application own now: the graph own zoom is a PANEL state,
  # and MainDisplayWidget::wheelEvent asks for the widget own frame when the graph took the
  # event, exactly as the cursor path does - while the canvas mover own path calls
  # updateRender itself, which is why the artwork own zoom always painted. The sleeps are
  # only settle time for the frame that was asked for; nothing here forces a paint.
  wheel_here() { # wheel_here <capture path> <direction: up|down>
      xdotool mousemove --sync $WHEEL_X $WHEEL_Y
      sleep 0.4
      if [ "$2" = "up" ]; then xdotool click --repeat $WHEEL_NOTCHES --delay 120 4
      else                    xdotool click --repeat $WHEEL_NOTCHES --delay 120 5
      fi
      sleep 0.6
      park
      sleep 1.0
      import -window root "$1"
  }

  # UNDO THAT ZOOM, so every phase AFTER this one works on the window the earlier phases
  # recorded (the wheel is the only thing in the probe that writes the frame window, and
  # the key phases that follow add keys at frames the pre-wheel window is looking at).
  # Not asserted - it is hygiene, not evidence - and the notches are the same count in the
  # opposite direction at the same anchor, which is what the scaler own own per-notch factor
  # makes reversible. The undo paints itself the same way the wheel above does.
  wheel_restore() {
      xdotool mousemove --sync $WHEEL_X $WHEEL_Y
      sleep 0.3
      xdotool click --repeat $WHEEL_NOTCHES --delay 120 5
      sleep 0.6
      park
      sleep 0.4
  }

  # (i) THE GRAPH VISIBLE. Two captures of the same state first, so the pair below can
  # only be the wheel. The zoom goes IN: the frame ladder own rung is at its floor for a
  # project nobody has fitted yet, so zooming OUT would be a request the scaler may clamp
  # while zooming in cannot be.
  ae_press "Show animation graph" "the graph toggle is pressable (the wheel phase, on)"
  sleep 1.2; park
  import -window root "$SHOTS/graph_probe_20_wheel_on_a.png"
  import -window root "$SHOTS/graph_probe_21_wheel_on_b.png"
  wheel_here "$SHOTS/graph_probe_22_wheel_on_after.png" up
  wheel_restore

  # (ii) THE GRAPH HIDDEN: same point, same notches, the other owner.
  ae_press "Show animation graph" "the graph toggle is pressable (the wheel phase, off)"
  sleep 1.2; park
  import -window root "$SHOTS/graph_probe_23_wheel_off_a.png"
  import -window root "$SHOTS/graph_probe_24_wheel_off_b.png"
  WHEEL_HIDDEN_FIT=$(ae extents "Fit graph")
  echo "WHEEL_HIDDEN_FIT=\"$WHEEL_HIDDEN_FIT\"" >> $G
  wheel_here "$SHOTS/graph_probe_25_wheel_off_after.png" up
  wheel_restore

  # ---- (iii) THE RING OVER BRIGHT ARTWORK: ONE CAPTURE FOR THE EYE, NOT FOR THE GATE ----
  # WHAT THIS IS, AND WHY IT ASSERTS NOTHING. The panel draws its ink in white with a 1px
  # dark outline ring (the contrast-outline pass whose re-measured pixel counts the header
  # records). On this project the ring mostly has nothing to do: the ink of the panel falls
  # on the dark ground the character stands on. So this takes ONE capture with the ink where
  # the ring matters - over BRIGHT artwork - and it is a capture to LOOK AT, not a gate:
  # whether a ring reads at a given zoom is a judgement, and any threshold for it would have
  # to be re-measured per renderer (see the llvmpipe note in the header). What is asserted
  # about it below is only that the file exists and is not a black frame.
  #
  # THE RECIPE, every step of it a measurement rather than a coordinate:
  #   * the graph is HIDDEN here - the canvas case of phase 5 just left it that way - so the
  #     wheel is the CANVAS own, at the same point and in the same notches phase 5 measured
  #     in both directions;
  #   * the anchor is the BRIGHTEST AREA the artwork has, found by blurring the canvas region
  #     of a capture taken with the graph hidden (so what is measured is the artwork and not
  #     the panel) and asking ImageMagick where its maximum is. The blur is what makes it an
  #     area rather than a 1px highlight. The crop is INSET by 20 px on every side, for two
  #     measured reasons: a blurred window must not run off the edge of the region being
  #     measured, and the project tab bar is drawn over the first 18 px of the viewport (the
  #     panel own ink begins at y=39, i.e. 18 px below the dock top at 21) - so an un-inset
  #     crop would find the brightest thing in the frame to be that bar and not the artwork;
  #   * the wheel zooms IN at that anchor - button 5, because MEASURED the other way: the own
  #     canvas capture of phase 5 shows the artwork SHRINK under button 4 - by 12 notches of
  #     about 10% each (ctrl::CanvasMover keys the zoom on an integer notch index, one notch
  #     per 120 units of wheel delta, so the factor is 1.1 per notch in and 0.9 out), which is
  #     what puts a bright area larger than the viewport under the panel rather than a patch
  #     inside it;
  #   * the graph is then shown again through its own toggle, so the labels, the tick row, the
  #     zero rule and the legend sit over that bright artwork: that frame is the capture;
  #   * and the canvas zoom is UNDONE with the same notches at the same anchor. The round trip
  #     is exact rather than approximate - CanvasMover re-centres about the pointer
  #     ((scale/pre)*(center - cursor) + cursor, whose inverse is itself, and the notch index
  #     is an integer) - which is why every phase after this one still sees the window phase 5
  #     restored.
  #
  # THE ONE NUMBER BESIDE IT IS RECORDED, NOT ASSERTED: the mean luminance of the artwork
  # under the ink box of the panel, where that box is measured the same way the header
  # measures it - the bounding box of the graph-off -> graph-on change. A reader can then
  # tell a capture that had something to show from one that did not.
  RING_SHOT="$SHOTS/graph_probe_31_ring_over_art.png"
  RING_NOTCHES=12
  RING_INSET=20
  RING_BRIGHT=0
  RING_INK_BOX=""
  RING_POINT=""
  import -window root /tmp/probe_ring_art.png
  RING_MAX=$(magick /tmp/probe_ring_art.png \
      -crop $((VIEW_R - VIEW_L - 2 * RING_INSET))x$((VIEW_B - VIEW_T - 2 * RING_INSET))\
+$((VIEW_L + RING_INSET))+$((VIEW_T + RING_INSET)) +repage \
      -colorspace Gray -blur 0x10 /tmp/probe_ring_root.png 2>/dev/null; \
      magick identify -define identify:locate=maximum -define identify:limit=1 \
      /tmp/probe_ring_root.png 2>/dev/null | awk "/:/ {print \$NF}" | tail -1)
  read -r RING_MX RING_MY <<< "$(printf "%s" "${RING_MAX:-}" | tr "," " ")"
  if [ -n "${RING_MX:-}" ] && [ -n "${RING_MY:-}" ]; then
      RING_X=$((RING_MX + VIEW_L + RING_INSET))
      RING_Y=$((RING_MY + VIEW_T + RING_INSET))
      RING_POINT="$RING_X $RING_Y"
      # THE ZOOM, while the graph is hidden: the face of the artwork this project ships is
      # what the measurement above finds, and 12 notches put its bright area under the whole
      # panel.
      xdotool mousemove --sync $RING_X $RING_Y
      sleep 0.4
      xdotool click --repeat $RING_NOTCHES --delay 120 5
      sleep 1.0
      park
      import -window root /tmp/probe_ring_art_zoomed.png
      # THE FRAME TO LOOK AT: the same toggle press every capture above is taken behind.
      ae_press "Show animation graph" "the graph toggle is pressable (the ring capture, on)"
      sleep 1.4; park
      import -window root "$RING_SHOT"
      # THE ARTWORK THE INK SITS ON, measured in the box that ink occupies. The box is the
      # graph-off -> graph-on change INSIDE THE VIEWPORT - the crop keeps the Tools dock out
      # of it, because the toggle own checked highlight changes there and is not panel ink -
      # and the mean is then taken from the ZOOMED capture (which has no panel in it at all),
      # so it is the brightness of the artwork itself.
      magick /tmp/probe_ring_art_zoomed.png -crop $((VIEW_R - VIEW_L))x$((VIEW_B - VIEW_T))\
+${VIEW_L}+${VIEW_T} +repage /tmp/probe_ring_off.png 2>/dev/null
      magick "$RING_SHOT" -crop $((VIEW_R - VIEW_L))x$((VIEW_B - VIEW_T))\
+${VIEW_L}+${VIEW_T} +repage /tmp/probe_ring_on.png 2>/dev/null
      magick compare -compose src /tmp/probe_ring_off.png /tmp/probe_ring_on.png /tmp/probe_ring_ink.png 2>/dev/null
      RING_INK_BOX=$(magick identify -format "%@" /tmp/probe_ring_ink.png 2>/dev/null)
      read -r RIBW RIBH RIBX RIBY <<< "$(printf "%s" "${RING_INK_BOX:-0x0+0+0}" | tr "x+" "  ")"
      if [ "${RIBW:-0}" -gt 0 ] && [ "${RIBH:-0}" -gt 0 ]; then
          RING_BRIGHT=$(magick /tmp/probe_ring_art_zoomed.png \
              -crop "${RIBW}x${RIBH}+$((RIBX + VIEW_L))+$((RIBY + VIEW_T))" +repage -colorspace Gray \
              -format "%[fx:mean*255]" info: 2>/dev/null)
      fi
      # UNDO, with the same notches at the same anchor (see the note above): the graph first,
      # because with the panel up the wheel belongs to the PANEL and would write the frame
      # window.
      ae_press "Show animation graph" "the graph toggle is pressable (the ring capture, off)"
      sleep 1.0; park
      xdotool mousemove --sync $RING_X $RING_Y
      sleep 0.4
      xdotool click --repeat $RING_NOTCHES --delay 120 4
      sleep 1.0
      park
  fi
  echo "RING_POINT=\"$RING_POINT\""     >> $G
  echo "RING_NOTCHES=\"$RING_NOTCHES\"" >> $G
  echo "RING_INK_BOX=\"$RING_INK_BOX\"" >> $G
  echo "RING_BRIGHT=\"$RING_BRIGHT\""   >> $G
  echo "ring capture: anchor ${RING_POINT:-none}, $RING_NOTCHES notches of canvas zoom in and the same out; the ink box of the panel over the zoomed artwork ${RING_INK_BOX:-none}, its mean luminance ${RING_BRIGHT:-?} (recorded, not asserted)"


  # ---- PHASE 2: DOES THE DATA OF THE SELECTED LAYER REACH THAT PANEL? ----
  # Everything above measures the PANEL. It draws frame ticks, the value axis and the
  # state line whether or not the layer behind it has a single key - measured on this
  # machine: the projects this probe defaults to (the smallest .anie under ~/Downloads)
  # expose one top node whose channel maps are EMPTY, so a screenshot of it is chrome and
  # nothing else. That would let a broken target feed - the panel drawing no curve for any
  # selection, ever - pass every assertion above.
  #
  # So swing the data instead of hunting for a project that has it: the Properties
  # dock Current Keys panel offers one add-key knob per channel, and pressing it puts
  # an EXPLICIT key at the current frame. If the key reaches the panel the plot
  # repaints: the fit basis gains a channel and the curve appears. Four measured facts
  # stand between this probe and that press, and every one of them is asserted rather
  # than hoped for:
  #
  #   * the knob is NOT the check box named "Create rotate key". That one is the
  #     Transform tool option (gui::tool::SRTPanel), inside the Tools dock Transform
  #     panel, and it is not even showing while the Toolbox is the active tool panel
  #     (its a11y rect is 0x0 and its role is check box, ancestor panel->Transform).
  #     The knob is a prop::KeyKnocker row: a label named after the channel (Rotate)
  #     plus an UNNAMED plus button, and it exists only while the frame has no key for
  #     that channel. Pressing it is a click anywhere on the row, because the label
  #     forwards its click to the row.
  #   * the whole Current Keys panel is hidden while the selection is the tree TOP
  #     node: ObjectTreeWidget::findSelectingRepresentNode returns nullptr for it, the
  #     dock calls setTarget(nullptr) and every panel hides. So the probe selects a
  #     CHILD row first (the branch indicator is the only expander, 12px left of the
  #     row own cell rect).
  #   * the board is taller than the dock viewport, so the Current Keys rows sit BELOW
  #     the fold: a click there hits the log pane and adds nothing (recorded when this
  #     probe was first written: the key count stayed 0), the wheel does not move the board
  #     (recorded the same way: 8 notches at the dock centre changed 0 px), but the panels
  #     above it collapse through their own title labels, and collapsing Constants and
  #     Default Keys lifts the row into the viewport. MEASURED IN THIS RUN, and asserted
  #     below rather than hoped for: the Rotate knob row is at y=394 after the row is
  #     selected and at y=194 after the two collapses - so the press that follows is a
  #     press on a row that MOVED into reach, not on whatever used to be at those
  #     coordinates.
  #   * a "Rotate" node that is not the knob is the View Settings strip toggle, whose
  #     accessible name is the group name and whose DESCRIPTION is "Rotate": pressing
  #     it activates a tool and changes the canvas, which is why no assertion here is
  #     made against a whole-screen change alone.
  #
  # The control matters as much as the effect: the panel is captured TWICE after the
  # row is selected and the panels above are collapsed, and before the key is added.
  # Those two shots are the same state, so they must be identical - the change below
  # can then only be the key.
  ae_press "Show animation graph" "the graph toggle is pressable a fifth time (back on)"
  sleep 1.0; park

  # A CHILD ROW, because the top node hides every panel of the dock.
  FIRST=$(ae dump 2>/dev/null | sed -n "s/^ *table cell *| //p" | head -1)
  FRECT=$(ae extents "$FIRST" "table cell")
  if [ -n "$FRECT" ]; then
      read -r FX FY FW FH <<< "$FRECT"
      xdotool mousemove --sync $((FX - 12)) $((FY + FH / 2)) click 1
      sleep 1.5
  fi

  KEY_ROW=""
  KNOB_BEFORE=""
  while IFS= read -r cand; do
      [ -n "$cand" ] || continue
      ae click "$cand" "table cell" >/dev/null 2>&1
      park
      KNOB_BEFORE=$(ae extents "Rotate" "label")
      if [ -n "$KNOB_BEFORE" ]; then KEY_ROW="$cand"; break; fi
  done < <(ae dump 2>/dev/null | sed -n "s/^ *table cell *| //p" | head -8)

  # BEFORE THE PRESS, and this is the pair of states the press has to move:
  #   * the knob ROW exists (the channel has no key at this frame);
  #   * the channel ROWS do not - the Angle label is part of the group the knob stands
  #     in for, and prop_CurrentKeyPanel::RotateKeyGroup::setKeyExists hides it while
  #     mKnocker is visible. MEASURED on the default project: Angle resolves to nothing
  #     here, and to a row at [382 245 82 23] after the press.
  ANGLE_BEFORE=$(ae extents "Angle" "label")
  ANGLE_VALUE_BEFORE=$(angle_value 2>/dev/null)
  LEGEND_BEFORE=$(legend list | tr "\n" ";")

  # AND INTO THE VIEWPORT, through the title labels of the panels above it.
  for title in "Constants" "Default Keys"; do
      TRECT=$(ae extents "$title" "label")
      [ -n "$TRECT" ] || continue
      read -r CX CY CW CH <<< "$TRECT"
      xdotool mousemove --sync $((CX + CW / 2)) $((CY + CH / 2)) click 1
      sleep 1.2
      park
  done
  KNOB=$(ae extents "Rotate" "label")
  ANIM=$(ae extents "Animation")
  echo "KEY_ROW=\"$KEY_ROW\""           >> $G
  echo "KNOB_BEFORE=\"$KNOB_BEFORE\""   >> $G
  echo "KNOB=\"$KNOB\""                 >> $G
  echo "ANGLE_BEFORE=\"$ANGLE_BEFORE\"" >> $G
  echo "ANGLE_VALUE_BEFORE=\"$ANGLE_VALUE_BEFORE\"" >> $G
  echo "LEGEND_BEFORE=\"$LEGEND_BEFORE\"" >> $G
  echo "ANIM=\"$ANIM\""                 >> $G

  # READ THE PANEL TWICE, WITH NOTHING IN BETWEEN: no toggle, no forced paint. The pair is
  # still expected to be exactly 0 px, which is the control that makes the press below the
  # only difference between it and the keyed capture.
  import -window root "$SHOTS/graph_probe_07_on_control.png"
  import -window root "$SHOTS/graph_probe_08_on_control.png"

  # THE PRESS: the centre of the row rect AT-SPI reports for the knob.
  if [ -n "$KNOB" ]; then
      read -r KX KY KW KH <<< "$KNOB"
      xdotool mousemove --sync $((KX + KW / 2)) $((KY + KH / 2)) click 1
      sleep 1.2; park
  fi
  # The key exists once the knob row has become the channel rows: the knob is the
  # stand-in for the panel header only while the frame has no key.
  ANGLE=$(ae extents "Angle" "label")
  ANGLE_VALUE=$(angle_value 2>/dev/null)
  # The capture right after the push: the key reaches the document through the timeline own
  # notifier (core::Project::onTimeLineModified), and the DriverHolder slot on that signal
  # asks the viewport for the frame (DriverHolder::onTimeKeyUpdated -> onVisualUpdated ->
  # MainDisplayWidget::onVisualUpdated), so the panel own pixels move without a nudge.
  import -window root "$SHOTS/graph_probe_09_after_key.png"
  echo "ANGLE=\"$ANGLE\""               >> $G
  echo "ANGLE_VALUE=\"$ANGLE_VALUE\""   >> $G
  echo "LEGEND_ROWS=\"$(legend list | tr "\n" ";")\"" >> $G

  # ---- THE LEGEND SWATCH, DRIVEN THROUGH THE PANEL OWN ACCESSIBLE ITEM ----
  # The row the phase-2 selection produces is named by its model label, and the model
  # only lists channels that HAVE a key - so before the press this name does not exist
  # and after it there is exactly one of them. That is the transition asserted outside;
  # here the row is found, then toggled twice round the pixel captures. The toggle is the
  # panel own accessible action, and it asks for its own frame
  # (ImGuiOverlay::onAccessibilityAction -> requestRepaint), so again nothing is forced.
  LEGEND_NAME=$(legend list | head -1 | cut -f1)
  LEGEND_EXT=$(ae extents "$LEGEND_NAME" "check box")
  LEGEND_OFF=$(legend toggle "$LEGEND_NAME")
  import -window root "$SHOTS/graph_probe_10_legend_hidden.png"
  ALLHIDDEN=$(ae extents "All channels of the selected layers are hidden" "label")
  LEGEND_ON=$(legend toggle "$LEGEND_NAME")
  import -window root "$SHOTS/graph_probe_11_legend_shown.png"
  ALLHIDDEN_BACK=$(ae extents "All channels of the selected layers are hidden" "label")
  echo "LEGEND_NAME=\"$LEGEND_NAME\""   >> $G
  echo "LEGEND_EXT=\"$LEGEND_EXT\""     >> $G
  echo "LEGEND_OFF=\"$LEGEND_OFF\""     >> $G
  echo "LEGEND_ON=\"$LEGEND_ON\""       >> $G
  echo "ALLHIDDEN=\"$ALLHIDDEN\""       >> $G
  echo "ALLHIDDEN_BACK=\"$ALLHIDDEN_BACK\"" >> $G

  # ---- PHASE 4: THE KEY CONTEXT MENU, AND THE EASING IT CHANGES ----
  # WHAT THIS PROVES. The panel had no way to change the easing TYPE of a key; a right-click
  # on a key now opens the timeline own easing list at the pointer, and choosing an entry
  # changes the easing of the SELECTED key through the editor own action (one undo step on
  # the timeline notifier and event). What no headless test can see is that this is reachable
  # at all: the popup is drawn by ImGui inside the widget own GL frame, so it exists only in
  # a mapped window under Xvfb, and the key it opens on is a MODEL rectangle.
  #
  # WHERE THE KEY IS, AND HOW THE PHASE FINDS IT. The plot IS the rectangle the panel
  # publishes for its fit action (gui::ImGuiOverlay::publishAccessibility gives the action
  # model.plot as its rect, because a double-click on the plot performs it), so the phase can
  # start from the panel own published geometry - but it must not ASSUME where inside that
  # plot the key is drawn, and it does not: it finds the key by its own pixels. A right-click
  # ON a key opens the menu; a right-click anywhere else opens nothing at all (the graph takes
  # no right-button action), so the scan below walks the fit rect own centre ROW - the value
  # axis IS fitted, so a single key is vertically centred - in 10 px steps, plus that row +/-
  # 8 px, and accepts only a sample whose click really opened the menu. MEASURED in the run
  # this fix is recorded against: the menu opened 9 samples in, at (302,103), i.e. 2 px from
  # the fit rect own centre (300,103) - this build own fit DOES centre both axes (fitAll calls
  # fitFrameAxis, and the phase reaches it through the panel own "Fit graph" action).
  # The point the scan finds is asserted to be INSIDE that published rect and to be the point
  # where the popup really opened. It is deliberately NOT asserted to be near the rect own
  # centre: where the key sits inside the plot is the fit policy own business - the frame half
  # of the fit is what the user asked to change this round - while the phase own business is
  # that the point it presses is on the key the panel DRAWS, which is what the popup own
  # pixels and the editor own selection answer both say.
  #
  # THE ONE LEFT PRESS THE PHASE MAKES IS THE PRE-CONDITION, AND ITS READING IS MADE EMPTY
  # FIRST. A press on a key SELECTS it, so the panel own state line then names that key - and
  # the press is accepted only if that half of the line MOVED from the reading taken
  # immediately before it. That reading has to start EMPTY for the statement to be about the
  # press, and the phase cannot assume it is: the right-click that found the key SELECTS that
  # key by design (the overlay own key menu acts on the selection, exactly as the timeline
  # own key menu does, so it makes its own key the selection). The phase therefore clears the
  # selection with the graph own deselect gesture first - a Ctrl-press in the plot, off every
  # key and band - and asserts that the half is empty BEFORE the press, so the "moved"
  # statement is about the press in every state rather than about the right-click that found
  # the point. MEASURED in both runs recorded against this fix: the selection was already
  # empty at phase-4 entry (nothing had pressed a key yet in this phase) and it was empty
  # again after the scan, so the deselect was a no-op there and is kept as the honest way to
  # make the statement decidable - the comment beside it in the phase says why that gesture
  # belongs to the graph and changes nothing else.
  #
  # HOW THE CHANGE IS READ BACK, AND WHY THE PIXELS ALONE ARE NOT ENOUGH HERE.
  #   * the curve PIXELS cannot show it AT THIS POINT: this layer has ONE key, and
  #     graph::Curve::buildPolyline emits nothing for a map of one key (there is no segment),
  #     so the panel draws a key square and no curve at all - MEASURED, changing the easing
  #     on that state changes 0 pixels. Worse, the drawn summary line the state sentence
  #     comes from is NOT drawn while a curve exists (ImGuiOverlay.cpp:843 paints it only
  #     for an empty model), so at this point the menu own effect is invisible on screen
  #     altogether. Phase 4b below is where a curve is MADE so it is not: a second key one
  #     frame later carrying a different value, after which the segment is drawn and
  #     re-shaping it moves the curve own own pixels. (The field that gives a second key
  #     another value is the dock own unnamed Angle spin box: this probe reads it through its
  #     Value interface, and phase 4b writes it with a real edit - see angle_value and
  #     angle_rect above.)
  #   * the panel own STATE LINE is therefore the readback HERE: gui::ImGuiOverlay publishes
  #     the panel summary to the accessibility tree, and it names the selected key and the
  #     easing it carries ("... selected key: Rotate Linear"). The easing name comes from
  #     util::Easing::getTypeName - the same "EasingName" strings the menu entries are built
  #     from - and the line is built from the DOCUMENT (the key the selection names), not
  #     from the menu return value, so "the line changed" is a statement about the document.
  #     The menu itself cannot be the readback: an ImGui popup is not in the accessibility
  #     tree at all (documented in the overlay class comment), which is exactly why the line
  #     exists.
  #   * the dock Easing chip is read beside it as a DIAGNOSTIC, not an assertion: it is
  #     prop::EasingChip, a self-painting QAbstractButton with no text and no accessibleName,
  #     so there may be no easing name in the tree to read. The dump below records what the
  #     tree actually says about it, so the next reader does not have to re-measure.
  #
  # THE MENU ITSELF IS ADDRESSED BY POSITION AND CONFIRMED BY EFFECT, like the object tree
  # QMenu in phase 3 - the same reason: it is not in the accessibility tree. Its layout is
  # the fixed one the overlay documents (zero item spacing, one uniform row per entry, two
  # columns, a separator and a delete row after the 13 easing entries, anchored at the
  # right-click and moved up when it would not fit), so the row under a pixel is a function
  # of the popup top edge and height - both MEASURED here from the popup pixels, so no style
  # number is assumed. Sine is the 4th easing in the shared display order
  # (Floor, Round, Linear, Sine, ...): index 3, i.e. row 1 of column 1.
  #
  # AND THE NEGATIVE: a right-click on empty plot space opens NO MENU. The graph takes no
  # right-button action, so the press falls through to the viewport; the panel own state line
  # is untouched by it, which is the half of that claim this phase asserts. With one key there
  # is no drawn curve there, so the graph owns no band either.
  summary_line() { ae dump 2>/dev/null | grep "selected key" | head -1 | sed "s/.*| //"; }
  # THE PANEL OWN STATE LINE, WHETHER OR NOT IT CARRIES A SELECTED KEY. summary_line()
  # above can only see a line that HAS the half - which is exactly the reading the press
  # below has to be able to move FROM an empty one, so the half is read from the line
  # itself. The base sentences are the model own four (the GraphPanel model own emptyState and
  # the three counted ones), the same strings the empty-state assertions above address by
  # name; `ae dump` prints "<role> | <name>" with the name cut at 70 characters (ae_ui.py),
  # so a long line can lose the END of the half - the halves below are compared with each
  # other and never against a spelling, so a cut is harmless.
  panel_line() {
      ae dump 2>/dev/null \
          | grep -m1 -E "channel\(s\) shown|Select a layer to show its curves|No keys on the selected layers|All channels of the selected layers are hidden" \
          | sed "s/.*| //"
  }
  # The selected-key half of that line, and EMPTY when the line has none at all (which is
  # the reading the strengthened pre-condition below needs).
  selected_half() {
      local line
      line=$(panel_line)
      case "$line" in
          *"selected key:"*) printf "%s" "${line##*selected key: }" ;;
          *)                 printf "" ;;
      esac
  }
  # THE ONE RULE THAT SAYS "THAT CHANGE IS THE KEY MENU", used by the scan below and by every
  # candidate the anchor search tries afterwards, so the two can never disagree about what a
  # menu looks like - which they did: this phase used to accept a point from the panel
  # own state line and then REJECT the very same point here, and the run it was fixed against
  # failed for that and nothing else.
  # AND IT IS NOT A DENSITY RULE, MEASURED RATHER THAN PREFERRED. The popup own background
  # is the PANEL background - the overlay applies the panel style to its menu - so a large
  # part of the rectangle a menu covers is pixel-identical to what was drawn there before.
  # MEASURED on this build: the menu changes 17165 px inside a 202x145 region, 58.6% of it,
  # while the rule this replaces demanded 60% and therefore rejected the REAL menu on every
  # candidate of the run this was fixed against. What makes a menu a menu here: a menu-sized
  # rectangle (a sliver is not a menu), a rectangle INSIDE the viewport the panel is confined
  # to (this is where "not the canvas" lives - a canvas reaction is the artwork own
  # region and runs past the viewport edges), and THOUSANDS of changed pixels (17165 for the
  # menu, against 0 for a right-click that hits no key). WHICH rectangle it is, exactly, is
  # settled afterwards by the clamp assertion, against the overlay own clamp rule.
  menu_shaped() { # menu_shaped <w> <h> <x> <y> <changed>
      [ "${1:-0}" -gt 60 ] && [ "${2:-0}" -gt 60 ] \
          && [ "${3:-0}" -ge "$VIEW_L" ] && [ "${4:-0}" -ge "$VIEW_T" ] \
          && [ $(( ${3:-0} + ${1:-0} )) -le $((VIEW_R + 1)) ] \
          && [ $(( ${4:-0} + ${2:-0} )) -le $((VIEW_B + 1)) ] \
          && [ "${5:-0}" -ge "${MENU_PIXEL_BAR:-3000}" ]
  }

  MENU_FIT=""
  MENU_DX0=0
  MENU_DY0=0
  MENU_KEY=""
  MENU_OFFSET=""
  MENU_BEFORE=""
  MENU_AFTER=""
  MENU_NOKEY_LINE=""
  MENU_DOCK_BEFORE=""
  MENU_DOCK_AFTER=""
  MENU_BBOX=""
  MENU_EMPTY_BBOX=""
  MENU_ENTRY=""
  MENU_EMPTY=""
  MENU_SAMPLES=0
  MENU_PIXEL_BAR=3000
  MENU_SCAN_LATTICE=""
  MENU_SCAN_NOTE=""
  MENU_SCAN_BBOX=""
  MENU_SCAN_TRIES=""
  MENU_TRIES=""
  MENU_ANCHOR=""
  MENU_POPUP_PIXELS=""
  MENU_PRE_SCAN=""
  MENU_PRESELECT=""
  MENU_DESELECT=""
  MENU_STATE_BEFORE=""
  MENU_SEL_BEFORE=""
  MENU_SEL_AFTER=""

  # The panel OWN fit, through the panel OWN action. Its PUBLISHED RECT IS THE PLOT: the
  # overlay publishes model.plot for the fit action (the same rectangle the state sentence
  # carries), because a double-click on the plot performs it. So asking the action where it
  # is IS asking the panel where its plot is - see the header note.
  ae_activate "Fit graph" "the panel own fit action is activated (its published rect is the plot)"
  sleep 1.0
  park
  MENU_FIT=$(ae extents "Fit graph")
  if [ -z "$MENU_FIT" ]; then
      echo "NOFIT the panel published no fit-action rectangle" >> $G
      kill $app 2>/dev/null
      wait $app 2>/dev/null
      exit 0
  fi
  read -r MFX MFY MFW MFH <<< "$MENU_FIT"
  MENU_DX0=$((MFX + MFW / 2))
  MENU_DY0=$((MFY + MFH / 2))
  # THE READING THE PRESS BELOW HAS TO MOVE, taken before phase 4 makes a single press in
  # the plot: it is a DIAGNOSTIC (echoed), not an assertion - what it shows is whether the
  # panel already had a key selected when the phase began, which is why the phase clears the
  # selection before the press (see below).
  MENU_PRE_SCAN=$(selected_half)

  # ---- FIND THE KEY HONESTLY, WITH RIGHT-CLICKS, AND LET THE MENUS-OWN PIXELS BE THE ONLY
  #      WITNESS ----
  # WHAT WAS WRONG BEFORE, MEASURED. This walk used to be a coarse spiral of LEFT presses
  # that accepted a point as soon as the panel own state line contained "focused:" - and
  # that test cannot fail on a press that hit nothing, because the focus is STICKY: the press
  # that added the key had already focused the channel, so the spiral accepted its FIRST
  # candidate whether or not a key was under it. That is the test this scan replaces: the
  # witness is now the MENU OWN PIXELS and nothing else, so a point is accepted only when a
  # right-click there really opened the popup the rest of the phase then drives.
  # AND THE POINT IS FOUND RATHER THAN PREDICTED, because the phase own business is the
  # DRAWN key and not the fit policy: the right-click this phase needs hit-tests the key own
  # DRAWN square (ctrl::graph::kKeyRadius = 5, so 10x10, and drawKeyContextMenu asks
  # curve.keys.at(i).contains(pos)), while the editor own pick accepts kKeyHitRadius = 10
  # around the same centre - two different targets, and the smaller one is the popup one. Where
  # that square ends up depends on the fit: MEASURED in the run this fix is recorded against,
  # the menu opened at (302,103), i.e. 2 px from the fit rect own centre (whose rect is
  # (222,25 156x156), so its centre row is y=103) - the value axis IS fitted, so the key is
  # on the centre row, and this build own fit centres the frame axis too (fitAll calls
  # fitFrameAxis, and the phase reaches it through the panel own "Fit graph" action). A walk
  # that assumed any fixed offset from the centre would be asserting the fit again; this one
  # measures. Three things follow, and they are the design.
  #
  #   * THE WITNESS IS THE POPUP OWN PIXELS. A right-click on a key opens the menu; a
  #     right-click anywhere else opens nothing, and the graph takes no right-button action
  #     at all, so a miss is inert. ONE CONTROL is captured immediately before the scan and
  #     every sample is compared against it: a menu moves THOUSANDS of pixels - MEASURED,
  #     17165 of them, in a 202x145 region - while a miss moves none (MEASURED across every
  #     sample of that run that was not on the key: 0 px, and the same 0 px for a right-click
  #     on empty plot space later in the phase). Nothing about the panel own state line is
  #     consulted here, so nothing sticky can accept a point with no key under it.
  #   * THE WALK IS THE FIT RECT OWN CENTRE ROW, AT 10 px STEPS, and then the same row +/-
  #     8 px. The value fit puts the key on that row, and the step is 10 rather than the
  #     "about 12" this phase was first asked for because 10 is the largest step that CANNOT
  #     straddle a 10 px chip: any 10 consecutive integers contain a multiple of 10, while a
  #     chip at +1..+10 is missed by samples at 0 and 12. The rows are tried centre, -8, +8,
  #     and MEASURED, the run recorded against this fix needed the centre row and 9 samples.
  #   * RIGHT-CLICKS, NOT LEFT PRESSES, because a press the graph does not own is handed to
  #     the viewport tools (GraphEditor::ownsCursor claims a press on a key, on a handle, or
  #     WITH CTRL inside the plot; a PLAIN press in the plot is deliberately left to the
  #     tools), so a blind left-press walk perturbs the document and the tool state, while a
  #     right-click that hits no key is inert. The ONE left press this phase makes follows the
  #     scan, and the phase says there why it is the only one it can make.
  # THE ACCEPTANCE IS THE PHASE OWN menu_shaped RULE, the same one the anchor search below
  # applies to the same point, so the two can never disagree about what a menu looks like -
  # which they did: the phase used to accept a point from the state line and then reject it
  # here on a density bar the real menu does not clear (see menu_shaped; that bar, not the
  # pointer position, is what the run this was fixed against failed on).
  # EVERY SAMPLE IS ALSO WRITTEN TO THE GEOMETRY FILE - the point, the changed pixel count and
  # the changed region - so a run says what every sample saw rather than only what the last
  # one did.
  MENU_SAMPLES=0
  MENU_SCAN_LATTICE="x ${MFX}..$((MFX + MFW - 1)) step 10, rows 0 -8 8 around y ${MENU_DY0}"
  MENU_SCAN_NOTE=""
  MENU_SCAN_BBOX=""
  # EVERY SAMPLE, AS point=changed-px, because a scan that finds nothing has to be able to say
  # WHAT it looked at rather than leaving an empty MENU_KEY to fail somewhere else: the phase
  # own first assertion below is about exactly this trace.
  MENU_SCAN_TRACE=""
  park
  import -window root /tmp/probe_menu_scan_ctl.png
  for mrow in 0 -8 8; do
      MY=$((MENU_DY0 + mrow))
      if [ "$MY" -lt "$MFY" ] || [ "$MY" -gt "$((MFY + MFH))" ]; then
          continue
      fi
      MX=$MFX
      while [ "$MX" -le $((MFX + MFW - 1)) ]; do
          MENU_SAMPLES=$((MENU_SAMPLES + 1))
          xdotool mousemove --sync $MX $MY
          sleep 0.4
          xdotool click 3
          sleep 1.3
          import -window root "$SHOTS/graph_probe_41_key_scan.png"
          CHANGED=$(magick compare -metric AE /tmp/probe_menu_scan_ctl.png "$SHOTS/graph_probe_41_key_scan.png" null: 2>&1 | awk "{print \$1+0}")
          magick compare -compose src /tmp/probe_menu_scan_ctl.png "$SHOTS/graph_probe_41_key_scan.png" /tmp/probe_menu_scan_diff.png 2>/dev/null
          SBB=$(magick identify -format "%@" /tmp/probe_menu_scan_diff.png 2>/dev/null)
          read -r SW SH SBX SBY <<< "$(printf "%s" "${SBB:-0x0+0+0}" | tr "x+" "  ")"
          echo "# menu scan $(date +%T): sample $MX,$MY changed=${CHANGED:-0} region=${SBB:-unreadable}" >> $G
          MENU_SCAN_TRACE="${MENU_SCAN_TRACE}${MENU_SCAN_TRACE:+ }${MX},${MY}=${CHANGED:-0}"
          if menu_shaped "${SW:-0}" "${SH:-0}" "${SBX:-0}" "${SBY:-0}" "${CHANGED:-0}"; then
              MENU_KEY="$MX $MY"
              MENU_OFFSET="$((MX - MENU_DX0)) $((MY - MENU_DY0))"
              MENU_SCAN_BBOX="${SBB:-unreadable}"
              MENU_SCAN_NOTE="hit on row $mrow at x $MX after ${MENU_SAMPLES} sample(s): ${CHANGED} px changed in ${SBB:-unreadable}"
              echo "# menu hit $(date +%T): $MX,$MY after ${MENU_SAMPLES} sample(s), changed=${CHANGED} region=${SBB:-unreadable}" >> $G
              break 2
          fi
          # A click that missed opened nothing; Escape is the cheap guard against an
          # accidental popup being left behind by a sample this test could not read.
          xdotool key --clearmodifiers Escape
          sleep 0.4
          MX=$((MX + 10))
      done
  done

  # THE MENU THE SCAN OPENED IS CLOSED AT ONCE, before anything else in the phase runs: a
  # right-click on a key selects that key and opens this popup, and nothing else is left
  # behind by it (a press while it is open is the menu own). The phase own anchor search
  # below reopens it on the same point for the assertions that need it open.
  if [ -n "$MENU_KEY" ]; then
      xdotool key --clearmodifiers Escape
      sleep 1.2
      park
  fi
  MENU_SCAN_TRIES="$MENU_TRIES"
  MENU_TRIES=""

  # ---- THE ONE LEFT PRESS THIS PHASE IS ABOUT, AND THE READING IT HAS TO MOVE ----
  # WHY THE SELECTION IS CLEARED FIRST, MEASURED RATHER THAN ASSUMED. The press has to be
  # shown to have SELECTED the key, and the reading it moves is taken immediately before it -
  # but the right-click that found the key already selected it: the overlay own
  # drawKeyContextMenu calls aEditor.selectKey for the key it opens on, because the menu acts
  # on the selection (the timeline own key-menu behaviour). MEASURED on this state, the
  # panel own line names a selected key even before the scan makes its first press
  # (MENU_PRE_SCAN). So "the half moved" needs a starting point that is EMPTY, and the probe
  # has one honest way there: THE GRAPH OWN DESELECT GESTURE, a CTRL-press inside the plot on
  # no key and no band. That press reaches GraphEditor::beginMarquee - Ctrl in the plot is
  # where the editor own plot gestures begin - and endMarquee applies a rectangle that
  # selects nothing, which is exactly the "clear the selection" gesture the editor documents
  # that press to be. It is the graph own (ownsCursor claims a Ctrl-press in the plot), so it
  # reaches no viewport tool, and it changes no document byte: nothing is under it - 40 px
  # along the frame axis from the key is another FRAME (a one-key map draws no polyline at
  # all, and the band test is per frame), so insertKeyAt refuses and the press opens the
  # marquee rather than an insert. The point is read back below, so a gesture that failed to
  # deselect is a red assertion rather than a silent licence.
  # AND THEN THE PRESS ITSELF: plain, with no modifier, at the point the popup own pixels
  # confirmed - so it is a press ON the key, which is the one press kind the phase can make
  # without perturbing anything else: GraphEditor::ownsCursor claims it, so no tool sees it,
  # and the press only SELECTS (a press arms a key move, and nothing is committed by it).
  # WHAT THE RIGHT-CLICK LEFT BEHIND, read before anything else touches it: a DIAGNOSTIC. The
  # overlay own key menu selects the key it opens on (drawKeyContextMenu calls
  # aEditor.selectKey), so this reading says whether the deselect below was needed or was
  # already a no-op - and the phase asserts the state BEFORE THE PRESS either way, which is
  # what makes the "moved" statement below true in both cases.
  MENU_PRESELECT=$(selected_half)
  MENU_DESELECT=""
  MENU_STATE_BEFORE=""
  MENU_SEL_BEFORE=""
  MENU_SEL_AFTER=""
  if [ -n "$MENU_KEY" ]; then
      read -r KX KY <<< "$MENU_KEY"
      MENU_DESELECT_X=$((KX + 40))
      if [ "$MENU_DESELECT_X" -gt "$((MFX + MFW - 4))" ]; then
          MENU_DESELECT_X=$((KX - 40))
      fi
      MENU_DESELECT="$MENU_DESELECT_X $KY"
      park
      # CTRL DOWN BEFORE THE MOVE, not after: the widget re-reads the modifiers on every
      # event it routes, and the move is what writes the pointer position the press is
      # tested against, so the state has to be the deselect press one while the pointer is
      # already over the point.
      xdotool keydown --clearmodifiers ctrl
      sleep 0.2
      xdotool mousemove --sync $MENU_DESELECT_X $KY
      sleep 0.4
      xdotool click 1
      sleep 0.2
      xdotool keyup --clearmodifiers ctrl
      sleep 1.2
      park
      MENU_STATE_BEFORE=$(panel_line)
      MENU_SEL_BEFORE=$(selected_half)
      xdotool mousemove --sync $KX $KY
      sleep 0.4
      xdotool click 1
      sleep 1.2
      park
      MENU_SEL_AFTER=$(selected_half)
  fi
  MENU_BEFORE=$(summary_line)
  MENU_DOCK_BEFORE=$(ae dump 2>/dev/null | grep -i "easing" | tr "\n" ";" | head -c 200)

  # The control pair, straight off the state the scan left: nothing changed between them, so
  # nothing is forced and the pair is expected to be 0 px.
  import -window root "$SHOTS/graph_probe_12_menu_control_a.png"
  import -window root "$SHOTS/graph_probe_13_menu_control_b.png"

  # ---- THE MENUS ANCHOR: SEARCHED, AND EVERY CANDIDATE CONFIRMED BY THE POPUPS-OWN
  # PIXELS ----
  # WHY THE SCANNED POINT IS NOT ASSUMED TO WORK. The right-click that opens the menu
  # hit-tests the key own DRAWN square: ctrl::graph::kKeyRadius is 5 (so the rect is 10x10)
  # and ImGuiOverlay::drawKeyContextMenu asks curve.keys.at(i).contains(pos), while the
  # editor own pick accepts kKeyHitRadius = 10 around the same centre - so a point the
  # editor answers for is NOT necessarily ON that square, and a right-click there opens
  # nothing at all. The scan above is what makes this cheap: it found a point where the menu
  # really opened, so the first candidate below (the point itself) is the hit and the
  # agreement case costs exactly one attempt; its neighbours are still tried, nearest first,
  # if it does not (4 px steps, out to 8 - the pick radius bounds the distance, and a 4 px
  # lattice is inside a 10x10 square wherever the key really is).
  # EACH CANDIDATE IS CONFIRMED, never hoped for: a menu is an opaque rectangle whose
  # top-left corner is the pointer (the overlay clamps that only when the popup would not
  # fit below the pointer, impossible at the viewport own middle), while a right-click that
  # hits no key falls through to the viewport and its change starts at the VIEWPORT own own
  # top edge instead. MENU_TRIES records every point tried, so a red run says how far the
  # search had to go, and every candidate ALSO writes its own changed count and changed
  # region to the geometry file: the scan above accepts on a count alone, so a candidate a
  # count accepted and this test rejects has to be readable afterwards rather than guessed at.
  MENU_BBOX=""
  MENU_ANCHOR=""
  MENU_TRIES=""
  open_key_menu() { # open_key_menu <x> <y> [shot]; on success leaves the menu OPEN and sets MENU_BBOX
      # The capture path is a parameter because this phase now opens the menu more than once
      # (the keyboard half below reopens it), and 14_menu_open is the ONE capture the phase
      # own "the menu own rectangle" assertion is taken from: a later opening writing the
      # same file would make that recorded measurement belong to whichever attempt came last.
      local shot="${3:-"$SHOTS/graph_probe_14_menu_open.png"}"
      local ax="$1" ay="$2" off dx dy tx ty bb w h bx by CHANGED
      for off in "0 0" "0 -4" "0 4" "-4 0" "4 0" "-4 -4" "4 -4" "-4 4" "4 4" \
                 "0 -8" "0 8" "-8 0" "8 0" "-8 -4" "8 -4" "-8 4" "8 4" \
                 "-4 -8" "4 -8" "-4 8" "4 8" "-8 -8" "8 -8" "-8 8" "8 8"; do
          set -- $off
          dx=$1; dy=$2
          tx=$((ax + dx)); ty=$((ay + dy))
          MENU_TRIES="${MENU_TRIES}${MENU_TRIES:+ }${tx},${ty}"
          park
          import -window root /tmp/probe_menu_scan_ctl.png
          xdotool mousemove --sync "$tx" "$ty"
          sleep 0.4
          xdotool click 3
          sleep 1.3
          import -window root "$shot"
          magick compare -compose src /tmp/probe_menu_scan_ctl.png "$shot" /tmp/probe_menu_scan_diff.png 2>/dev/null
          bb=$(magick identify -format "%@" /tmp/probe_menu_scan_diff.png 2>/dev/null)
          read -r w h bx by <<< "$(printf "%s" "${bb:-0x0+0+0}" | tr "x+" "  ")"
          CHANGED=$(magick compare -metric AE /tmp/probe_menu_scan_ctl.png "$shot" null: 2>&1 | awk "{print \$1+0}")
          echo "# menu candidate $(date +%T): $tx,$ty changed=${CHANGED:-0} region=${bb:-unreadable}" >> $G
          # THE SAME RULE THE SCAN ACCEPTED THE POINT ON (see menu_shaped): a menu-sized,
          # in-viewport rectangle with thousands of changed pixels in it.
          if menu_shaped "${w:-0}" "${h:-0}" "${bx:-0}" "${by:-0}" "${CHANGED:-0}"; then
              MENU_BBOX="$bb"
              MENU_ANCHOR="$tx $ty"
              MENU_POPUP_PIXELS="$CHANGED"
              echo "menu anchor: opened at $tx,$ty after ${MENU_TRIES}" >> $G
              return 0
          fi
          # A click that missed opened nothing; Escape is the cheap guard against an
          # accidental popup being left behind by a candidate this test could not read.
          xdotool key --clearmodifiers Escape
          sleep 0.4
      done
      return 1
  }
  if [ -n "$MENU_KEY" ]; then
      read -r KX KY <<< "$MENU_KEY"
      open_key_menu $KX $KY
  fi
  read -r MBW MBH MBX MBY <<< "$(printf "%s" "${MENU_BBOX:-0x0+0+0}" | tr "x+" "  ")"

  # ---- THE KEYBOARD OF THE MENU, AND THE APPLICATION KEYS BESIDE IT ----
  # WHAT THIS PROVES. The popup keyboard behaviour is ImGui own - Escape closes the front
  # popup, the arrows move its entry HIGHLIGHT (an ImGui rectangle, drawn in ImGui own theme
  # colours) and Enter activates the highlighted entry - and ImGui gates all three on
  # ImGuiConfigFlags_NavEnableKeyboard. That flag is global to the CONTEXT while it is set,
  # and this overlay covers the whole viewport, so with it set ImGui claims the keyboard
  # (io.NavActive -> io.WantCaptureKeyboard, io.ConfigNavCaptureKeyboard is true by default)
  # and the application own key commands - gui::KeyCommandMap, routed through
  # MainDisplayWidget::keyPressEvent and gated again in gui::KeyCommandInvoker, which
  # consults the SAME flag - would stop reaching MainWindow while the panel is on screen.
  # The overlay therefore sets it EXACTLY while this popup is open, and this half of the
  # phase is what that toggle is for, both ways round:
  #   * with the menu OPEN the keys are the menu own: Delete (a real application command,
  #     "Delete selected key(s)") must NOT remove the key the menu was opened on, Escape must
  #     close the popup, the arrows must draw and move the highlight, and Enter must activate
  #     the entry the arrows reached;
  #   * with the menu CLOSED the application keys work again: ctrl+Z undoes the last edit and
  #     the panel own state line says so.
  # THE STATE LINE IS THE READBACK for the document side of all of it, and the pixels for the
  # popup own coming and going (an ImGui popup is not in the accessibility tree at all - see
  # the phase header - and the state line is the one thing that is).
  # WHERE THE HIGHLIGHT STARTS AND WHAT EACH ARROW DOES is not guessed: MEASURED in a
  # standalone Dear ImGui 1.92.7 harness driving this exact fixed two-column layout, the
  # highlight starts on the first entry (Floor: row 0, column 0), one Down moves it to the
  # next ROW of the same column (Linear: index 2), Up returns it, and Right moves it WITHIN
  # the row to the second entry of the shared display order (Round: index 1). Enter
  # activates on the frame that carries the press, so the line ends up naming Round - an
  # easing the key did NOT carry before, which is what makes the change readable in the line
  # at all.
  MENU_DEL_LINE=""
  MENU_ESCAPE_LINE=""
  MENU_CLICK_BBOX=""
  MENU_KB_LINE=""
  MENU_APPKEY_LINE=""
  if [ -n "${MENU_BBOX:-}" ] && [ "${MBW:-0}" -gt 0 ]; then
      # THE KEYS THE MENU ACTS ON BELONG TO THE MENU. Delete is the sharpest of them: it is
      # bound to a timeline command that would remove exactly the key this menu was opened
      # on, so a menu that did NOT consume it would say so in the state line.
      xdotool key --clearmodifiers Delete
      sleep 1.4
      import -window root "$SHOTS/graph_probe_32_menu_delete.png"
      MENU_DEL_LINE=$(summary_line)
      # ESCAPE, with the pointer where the menu was opened and nothing else moved: the two
      # captures the diff is taken from below differ by the popup and nothing else, and the
      # document must be untouched by it (the line must not move).
      xdotool key --clearmodifiers Escape
      sleep 1.4
      import -window root "$SHOTS/graph_probe_33_menu_escape.png"
      MENU_ESCAPE_LINE=$(summary_line)
  fi
  # AND THE MENU IS STILL USABLE: reopened at the same anchor for the CLICK below, which is
  # the existing route of this phase and stays its route. The anchor has to be on the drawn
  # square again - it is, since Escape changed nothing and the editor selection is where the
  # phase left it - so the search costs exactly one attempt.
  if [ -n "${MENU_ANCHOR:-}" ] && open_key_menu ${MENU_ANCHOR} "$SHOTS/graph_probe_34_menu_reopen.png"; then
      MENU_CLICK_BBOX="$MENU_BBOX"
  fi

  # THE ENTRY: ADDRESSED BY POSITION, AND CONFIRMED BY THE EFFECT IT HAS TO HAVE.
  #
  # WHERE THE ROW AND THE COLUMN ARE. Sine is easing index 3 of the shared display order
  # (Floor, Round, Linear, Sine, ...) and the menu fills its grid ROW BY ROW across two
  # columns (ImGuiOverlay::drawKeyContextMenu: index = row * 2 + column), so it is row 1 of
  # column 1. The columns are equal halves of the measured popup, so their centres are its
  # quarter and three-quarter points. The ROWS are counted UP FROM THE POPUP OWN BOTTOM EDGE,
  # which is the one edge its clamp leaves where the pixels say it is: the popup is clamped
  # into the viewport here (the assertion below) and its TOP is partly hidden under the
  # project tab bar, which is drawn over the viewport own first 18 px. MEASURED, and this is
  # why the top is not used as the origin: the popup own background starts at y=39, which is
  # exactly the bar own bottom, while the first row own glyphs run 39..50 with a cap height of
  # 12 px - they could not be that tall if the popup window itself began at 39. The pitch is
  # taken from the popup own measured height over the 8 rows it holds (7 easing rows and the
  # delete row), and the delete row own centre sits one half pitch plus the popup own padding
  # above the bottom: PAD is 5 px on this build, the window border plus WindowPadding.y, the
  # same value that is hidden at the top - MEASURED at the bottom, where it is visible: the
  # delete row own text band ends at y=179 and the popup own background runs on to 183.
  #
  # AND ONE CLICK IS WHAT THE ENTRY GETS, because the overlay asks for the frame that applies
  # what ImGui had to leave queued. An ImGui Selectable fires on the RELEASE, and ImGui
  # applies a queued press and its release on TWO frames rather than one
  # (io.ConfigInputTrickleEventQueue: the two are split so neither is lost), so the frame that
  # carried the press is not the frame that fires the entry. MEASURED BEFORE THE FIX in the run
  # this probe was fixed against: the single click at the derived point left the state line
  # exactly as it was, and the capture taken after the next forced paint was
  # BYTE-IDENTICAL to the pre-menu one (0 px) - the popup had been closed in a frame where it
  # was already no longer drawn. MEASURED again while diagnosing it, with no unrelated frame in
  # between: the same single click leaves the popup ON SCREEN with the entry unpressed, because
  # the release is still sitting in ImGui own queue - the press own frame was the last frame
  # anybody asked for. A SECOND press at the same point was what produced the frame that carried
  # the first press own release (MEASURED: the state line then moved to the chosen easing and the
  # popup closed in one run, from that double press at the rounded centre of the entry own row),
  # and it is the workaround this phase no longer needs: the overlay own frame now ends by asking
  # for its own successor while ImGui still holds input (imguiHasQueuedInput, at the end of
  # ImGuiOverlay::paint). A requestRepaint() from the event handler could not have stood in for
  # it - Qt coalesces everything requested while a paint is already pending into that one
  # pending paint, which is why the consumed press own repaint already was there and was still
  # not enough.
  #
  # THE HALF OF THAT STORY THAT IS THIS WIDGET, and not a defect: while the key menu is open,
  # ImGuiOverlay::press answers a left press as the MENU own and returns before
  # MainDisplayWidget hands it to its cursor (the overlay own controls take precedence, and with
  # a menu open every left press is its). That press is therefore never registered on the
  # cursor, so the release that follows reads as a DUPLICATE
  # (core::AbstractCursor::setMouseReleaseImpl answers false for a release with no press) and
  # schedules no repaint of its own. The press belonged to the popup, so that is what it should
  # do - and it is why the frame the popup needs has to come from the overlay.
  MENU_ROWS=8    # 7 easing rows and the delete row
  MENU_PAD=5     # the popup own padding, border plus WindowPadding.y (MEASURED, see above)
  menu_entry() { # menu_entry <bbox: w h x y> <row> <column> -> echoes "x y"
      local w=$1 h=$2 bx=$3 by=$4 row=$5 col=$6
      local pitch=$((h / MENU_ROWS))
      local y=$((by + h - 1 - MENU_PAD - pitch / 2 - pitch * (MENU_ROWS - 1 - row)))
      local x=$((bx + w / 4 + col * (w / 2)))
      echo "$x $y"
  }
  click_entry() { # click_entry <bbox: w h x y> <row> <column> -> echoes the point clicked
      # ONE press. The entry fires on the release, in the frame the overlay own frame asked
      # for (see the comment above): a second press here would be the workaround this phase
      # was fixed against, and it would hide the overlay defect again.
      local ex ey
      read -r ex ey <<< "$(menu_entry $1 $2 $3)"
      xdotool mousemove --sync $ex $ey
      sleep 0.4
      xdotool click 1
      sleep 2.0
      park
      echo "$ex $ey"
  }
  if [ -n "${MENU_BBOX:-}" ] && [ "${MBW:-0}" -gt 0 ]; then
      MENU_ENTRY=$(click_entry "$MBW $MBH $MBX $MBY" 1 1)
  fi
  # Nothing forced here either: the frame that applies the queued press and its release is
  # the successor frame the overlay own paint asked for while ImGui still held input (see
  # imguiHasQueuedInput), and click_entry has already waited for it above.
  import -window root "$SHOTS/graph_probe_15_menu_after.png"
  MENU_AFTER=$(summary_line)
  MENU_DOCK_AFTER=$(ae dump 2>/dev/null | grep -i "easing" | tr "\n" ";" | head -c 200)

  # THE NEGATIVE, and the claim it makes is the TRUE one: a right-click that hits no key
  # opens NO MENU. It does not "change nothing" - the graph takes no right-button action, so
  # the press falls through to the viewport and the canvas reacts (measured: 183x178+173+21
  # changed on the first run of this phase). What is asserted instead: the changed region
  # does not START at the pointer (a menu would put its own left edge there), and the panel
  # own state line is unchanged. The point is the key found above, 40 px to the right and
  # inside the plot, so it is on no key and on no drawn curve (a one-key map has no polyline).
  MENU_EMPTY_X=0
  MENU_EMPTY_Y=0
  if [ -n "$MENU_KEY" ]; then
      read -r KX KY <<< "$MENU_KEY"
      MENU_EMPTY_X=$((KX + 40))
      MENU_EMPTY_Y=$KY
      if [ "$MENU_EMPTY_X" -gt "$((MFX + MFW - 4))" ]; then
          MENU_EMPTY_X=$((KX - 40))
      fi
      MENU_EMPTY="$MENU_EMPTY_X $MENU_EMPTY_Y"
      xdotool mousemove --sync $MENU_EMPTY_X $MENU_EMPTY_Y
      sleep 0.4
      xdotool click 3
      sleep 1.4
      park
  fi
  import -window root "$SHOTS/graph_probe_16_menu_nokey.png"
  MENU_NOKEY_LINE=$(summary_line)
  magick compare -compose src "$SHOTS/graph_probe_15_menu_after.png" "$SHOTS/graph_probe_16_menu_nokey.png" /tmp/probe_menu_nokey_diff.png 2>/dev/null
  MENU_EMPTY_BBOX=$(magick identify -format "%@" /tmp/probe_menu_nokey_diff.png 2>/dev/null)

  # ---- THE HIGHLIGHT, THE ARROWS AND ENTER ----
  # WHY THIS IS A SECOND OPENING rather than part of the one above: Escape closed that one,
  # which is the thing this phase now proves; the keyboard inside the menu is proven on a
  # fresh opening at the same anchor, and the CLICK above already showed the menu usable
  # again from a keyboard-closed one. What is measured here: down/up/right move the ImGui
  # entry highlight - it starts on Floor and Right reaches the second entry of the display
  # order, Round (see the comment above the first opening) - and Return activates the
  # highlighted entry on the frame that carries the press. Both are ONE press each: ImGui
  # applies a queued press and its release on two frames (the trickle the click above is
  # about), and the overlay own frame asks for its successor while ImGui still holds one, so
  # no step here needs a second press or a forced paint.
  if [ -n "${MENU_ANCHOR:-}" ] && open_key_menu ${MENU_ANCHOR} "$SHOTS/graph_probe_35_menu_kb_open.png"; then
      xdotool key --clearmodifiers Down
      sleep 1.4
      import -window root "$SHOTS/graph_probe_36_menu_down.png"
      # Up returns the highlight to the first entry, so the Right below starts from it - the
      # same starting point the measured harness had.
      xdotool key --clearmodifiers Up
      sleep 1.0
      xdotool key --clearmodifiers Right
      sleep 1.0
      xdotool key --clearmodifiers Return
      sleep 1.6
      # The pointer is LEFT WHERE IT IS: the capture below is diffed against the one above
      # it, and a park in between would change the pixels for a reason that has nothing to
      # do with the popup.
      import -window root "$SHOTS/graph_probe_37_menu_kb_after.png"
      MENU_KB_LINE=$(summary_line)
  fi

  # ---- AND THE APPLICATION OWN KEYS, WITH THE MENU CLOSED ----
  # THE REGRESSION THE GATE COULD CAUSE, and the half of this phase that matters most: the
  # flag is cleared as soon as the popup closes, so the overlay claims nothing and a
  # rebindable application command reaches MainWindow exactly as it did before this menu
  # existed. ctrl+Z is the cleanest one to read back here: it undoes the LAST edit, which
  # after the keyboard pick above is that pick, so the panel own state line has to move back
  # to what it read before it - and it leaves the document where phase 4b expects it (the key
  # carries Sine, which is what MENU_AFTER recorded and what the Linear pick of 4b then
  # re-shapes).
  xdotool key --clearmodifiers ctrl+z
  sleep 1.6
  import -window root "$SHOTS/graph_probe_38_appkey_undo.png"
  MENU_APPKEY_LINE=$(summary_line)

  echo "MENU_FIT=\"$MENU_FIT\""                 >> $G
  echo "MENU_DERIVED=\"$MENU_DX0 $MENU_DY0\""   >> $G
  echo "MENU_KEY=\"$MENU_KEY\""                 >> $G
  echo "MENU_OFFSET=\"$MENU_OFFSET\""           >> $G
  echo "MENU_SAMPLES=\"$MENU_SAMPLES\""         >> $G
  echo "MENU_PIXEL_BAR=\"$MENU_PIXEL_BAR\""     >> $G
  echo "MENU_SCAN_LATTICE=\"$MENU_SCAN_LATTICE\"" >> $G
  echo "MENU_SCAN_NOTE=\"$MENU_SCAN_NOTE\""     >> $G
  echo "MENU_SCAN_TRACE=\"$MENU_SCAN_TRACE\""   >> $G
  echo "MENU_SCAN_BBOX=\"$MENU_SCAN_BBOX\""     >> $G
  echo "MENU_SCAN_TRIES=\"$MENU_SCAN_TRIES\""   >> $G
  echo "MENU_PRE_SCAN=\"$MENU_PRE_SCAN\""       >> $G
  echo "MENU_PRESELECT=\"$MENU_PRESELECT\""     >> $G
  echo "MENU_DESELECT=\"$MENU_DESELECT\""       >> $G
  echo "MENU_STATE_BEFORE=\"$MENU_STATE_BEFORE\"" >> $G
  echo "MENU_SEL_BEFORE=\"$MENU_SEL_BEFORE\""   >> $G
  echo "MENU_SEL_AFTER=\"$MENU_SEL_AFTER\""     >> $G
  echo "MENU_BEFORE=\"$MENU_BEFORE\""           >> $G
  echo "MENU_BBOX=\"$MENU_BBOX\""               >> $G
  echo "MENU_ANCHOR=\"$MENU_ANCHOR\""           >> $G
  echo "MENU_POPUP_PIXELS=\"$MENU_POPUP_PIXELS\"" >> $G
  echo "MENU_TRIES=\"$MENU_TRIES\""             >> $G
  echo "MENU_ENTRY=\"$MENU_ENTRY\""             >> $G
  echo "MENU_AFTER=\"$MENU_AFTER\""             >> $G
  echo "MENU_DOCK_BEFORE=\"$MENU_DOCK_BEFORE\"" >> $G
  echo "MENU_DOCK_AFTER=\"$MENU_DOCK_AFTER\""   >> $G
  echo "MENU_EMPTY=\"$MENU_EMPTY\""             >> $G
  echo "MENU_EMPTY_BBOX=\"$MENU_EMPTY_BBOX\""   >> $G
  echo "MENU_NOKEY_LINE=\"$MENU_NOKEY_LINE\""   >> $G
  echo "MENU_DEL_LINE=\"$MENU_DEL_LINE\""       >> $G
  echo "MENU_ESCAPE_LINE=\"$MENU_ESCAPE_LINE\"" >> $G
  echo "MENU_CLICK_BBOX=\"$MENU_CLICK_BBOX\""   >> $G
  echo "MENU_KB_LINE=\"$MENU_KB_LINE\""         >> $G
  echo "MENU_APPKEY_LINE=\"$MENU_APPKEY_LINE\"" >> $G
  # The values are echoed as ONE assignment (not as a free-form sentence): the outer shell
  # eval()s this file, and a bare sentence with brackets and parentheses in it is a syntax
  # error there - which would leave every variable BELOW it unset and turn one broken line
  # into a cascade of unrelated failures.
  echo "MENU_KBDNOTE=\"delete[${MENU_DEL_LINE:-none}] escape[${MENU_ESCAPE_LINE:-none}] reopen[${MENU_CLICK_BBOX:-none}] kb[${MENU_KB_LINE:-none}] undo[${MENU_APPKEY_LINE:-none}]\"" >> $G

  # ---- PHASE 4b: THE CURVE THE MENUS EASING ACTUALLY SHAPES ----
  # WHY THIS IS NEEDED. Everything above reads the easing back from the panel own state
  # line - and MEASURED, that line is NOT DRAWN while a curve exists: the panel paints
  # its summary only while the model is EMPTY (ImGuiOverlay.cpp:843 draws
  # aModel.emptyState inside the plot; with curves present emptyState is empty and the
  # line lives in the accessibility tree alone). The easing is drawn in exactly ONE
  # place: the polyline of the segment it shapes - and a map of ONE key has no segment
  # (graph::Curve::buildPolyline loops i + 1 < keys.size()), so a one-key panel draws a
  # key square and no curve, and the easing change above moved no pixel at all. So this
  # phase MAKES the segment: a second key at the NEXT frame carrying a DIFFERENT value,
  # after which the segment from the first key is drawn, and re-shaping that segment
  # changes the curve own pixels.
  #
  # HOW THE SECOND KEY IS MADE, all of it by driving the app the way a user does:
  #   * the playhead moves one frame right through the app own own key command, Ctrl+Right
  #     ("Move one frame to the right": KeyCommandMap -> MainWindow::onDisplacementTriggered
  #     -> timeLineWidget().setFrame(current + 1), MainWindow.cpp:885). The key event
  #     reaches the main window because nothing else consumes it: the viewport own own
  #     keyPressEvent accepts a key only while an ImGui text field owns the keyboard
  #     (MainDisplayWidget.cpp:672).
  #   * THE MOVE IS ASSERTED BY THE DOCKS OWN OUTPUT, not by a frame number: the Current
  #     Keys panel shows a channel own rows only while a key exists at the current frame and
  #     its add-key KNOB row otherwise (prop::CurrentKeyGroup::setKeyExists), so the knob
  #     row coming back IS the dock saying there is no key here any more.
  #   * pressing that knob adds the key at this frame, seeded from the value playback
  #     holds - the same value, so the segment would be flat and the easing invisible
  #     again. The value is changed through the dock own own Angle field with a REAL edit
  #     (click, select all, type, Return): the field own handler hangs off editingFinished,
  #     which an accessibility write does not fire (prop_Items.cpp:543), and the edit is
  #     read back through the field own Value interface and asserted.
  #   * THE KEY THE MENU ACTS ON HAS NOT MOVED. A second key on a channel already shown
  #     does not change the shown-curve basis, and only a basis change refits the value
  #     window (GraphEditor::syncValueAxis), while the frame window is written only by a
  #     fit or the wheel - so the point the bounded scan found is still on that first key.
  ANGLE_KEY=$(angle_value 2>/dev/null)
  xdotool key --clearmodifiers ctrl+Right
  sleep 1.2; park
  KNOB_2ND=$(ae extents "Rotate" "label")
  ANGLE_2ND_SEEDED=""
  ANGLE_2ND_WRITTEN=""
  ANGLE_2ND_RECT=""
  ANGLE_2ND_TRIES=0
  ANGLE_2ND_TYPED_TEXT=""
  ANGLE_2ND_EDIT_TEXT=""
  DOCK_FOCUS_BEFORE=""
  DOCK_FOCUS_CLICK=""
  DOCK_FOCUS_AFTER=""
  DOCK_TARGET_BEFORE=""
  DOCK_TARGET_AFTER=""
  NEWDEG=""
  if [ -n "$KNOB_2ND" ]; then
      # PRESSED AT THE ROW THE TREE REPORTS, the same route phase 2 uses and the one the
      # recorded runs prove works (the Angle field becomes readable, which is what the
      # assertions below read). The widget OWN action was tried and does NOT add the key here
      # (MEASURED: the channel rows stayed hidden), so the coordinate route is the one this
      # probe drives.
      #
      # AND IT IS PRESSED AGAIN IF IT DID NOT LAND, bounded at three attempts. MEASURED in the
      # run this probe was fixed against: ONE press at this exact rectangle left the channel
      # rows hidden, while the same press at the same rectangle had added the frame-0 key in
      # phase 2 minutes earlier. MEASURED again while fixing it: the SAME press DID land on the
      # next run of the same state (the Angle field answered 0 and the edit below took), so the
      # failure is a press that was lost rather than a knob that cannot be pressed - and a
      # press that does not land cannot be told apart from a dead knob by anything except the
      # dock OWN output, which is what the loop below reads. A retry after a press that DID land can never fire: the Angle field is
      # readable by then, and that readability is also the evidence the assertions below use.
      #
      # AND WHY THIS RETRY IS NOT THE MENU DEFECT CLASS (a consumed event that no repaint
      # processes). The knob is a prop::KeyKnocker, a plain QWidget in the Animation dock: it
      # acts in its own mouseReleaseEvent - a left release inside its own rect - and the row it
      # turns into is a widget-tree change driven by the document notifier (setKeyExists, which
      # calls setVisible), not by anything drawn. No viewport repaint is in that path at all,
      # and the tool that clicks it here never reaches MainDisplayWidget, the only caller of
      # ImGuiOverlay::press - so an ImGui event held back by the overlay cannot be what a lost
      # knob press is. It is a press that did not arrive (or did not land inside the rect), and
      # the bounded retry is the honest instrument for that.
      read -r K2X K2Y K2W K2H <<< "$KNOB_2ND"
      KNOB_2ND_PRESSES=0
      for attempt in 1 2 3; do
          KNOB_2ND_PRESSES=$attempt
          xdotool mousemove --sync $((K2X + K2W / 2)) $((K2Y + K2H / 2))
          sleep 0.4
          xdotool click 1
          sleep 1.6; park
          ANGLE_2ND_SEEDED=$(angle_value 2>/dev/null)
          if [ -n "$ANGLE_2ND_SEEDED" ]; then break; fi
      done
      ANGLE_2ND_RECT=$(angle_rect 2>/dev/null)
      ANGLE_2ND_TRIES=0
      if [ -n "$ANGLE_2ND_RECT" ] && [ -n "$ANGLE_2ND_SEEDED" ]; then
          NEWDEG=$(awk -v a="$ANGLE_2ND_SEEDED" "BEGIN { printf \"%.2f\", a + 30 }")
          read -r A2X A2Y A2W A2H <<< "$ANGLE_2ND_RECT"
          ANGLE_2ND_TRIES=1
          # THREE READINGS, before the click into the field, straight after it, and after the
          # commit key - plus the names the dock is showing beside them (the panel own legend
          # rows name the layer and channel it is drawing, which is the TARGET of the write).
          # Nothing here is a retry and nothing here changes the write: the one attempt below
          # is the phase own, and these readings say what the field could see.
          DOCK_FOCUS_BEFORE=$(dock_focus 2>/dev/null | tr "\n" "; ")
          DOCK_TARGET_BEFORE=$(legend list 2>/dev/null | tr "\n" ";")
          xdotool mousemove --sync $((A2X + A2W / 2)) $((A2Y + A2H / 2)) click 1
          sleep 0.5
          DOCK_FOCUS_CLICK=$(dock_focus 2>/dev/null | tr "\n" "; ")
          xdotool key --clearmodifiers ctrl+a
          sleep 0.2
          xdotool type --delay 60 "$NEWDEG"
          sleep 0.3
          # THE FIELD OWN VALUE WHILE THE EDIT IS STILL OPEN, read BEFORE the commit key. This
          # is the reading that tells the two ways a typed write can fail apart, and it changes
          # nothing: the commit key follows it, and the phase own assertion is still about the
          # value AFTER the commit. 30.00 here means the characters ARRIVED and the commit was
          # refused (a lock or a re-entrancy in the write path); 0 means they never reached the
          # field at all (an ImGui keyboard capture still holding the keys, for instance).
          ANGLE_2ND_TYPED_TEXT=$(angle_value 2>/dev/null)
          ANGLE_2ND_EDIT_TEXT=$(angle_text 2>/dev/null)
          xdotool key --clearmodifiers Return
          sleep 1.2; park
          DOCK_FOCUS_AFTER=$(dock_focus 2>/dev/null | tr "\n" "; ")
          DOCK_TARGET_AFTER=$(legend list 2>/dev/null | tr "\n" ";")
          ANGLE_2ND_WRITTEN=$(angle_value 2>/dev/null)
      fi
  fi
  KNOB_2ND_AFTER=$(ae extents "Rotate" "label")
  # Straight after the typed edit: the angle write goes through the dock own key accessor,
  # which notifies through ctrl::TimeLineUtil::Notifier (prop_KeyAccessor.cpp) and so reaches
  # the viewport repaint on the same signal a timeline key push does. Nothing forced.
  import -window root "$SHOTS/graph_probe_26_curve_sine.png"

  # THE MENU AGAIN, ON THE SAME KEY, CHANGING THE EASING - and this time a segment is
  # drawn, so the change is in the panel own OWN pixels rather than in a line only a screen
  # reader can see. The entry is addressed by position in the popup own fixed grid, exactly
  # as above: Linear is the 3rd easing in the shared display order (index 2 -> row 1 of
  # column 0). The anchor is the one the search found - the key has not moved, since adding
  # a key to a channel already shown does not refit the frame window - and the search is
  # reused rather than assumed, which costs exactly one attempt when the anchor is still on
  # the drawn square.
  read -r MKX2 MKY2 <<< "${MENU_ANCHOR:-${MENU_KEY:-0 0}}"
  MENU2_BBOX=""
  MENU2_ENTRY=""
  if [ -n "${MENU_ANCHOR:-}" ] && open_key_menu $MKX2 $MKY2; then
      MENU2_BBOX="$MENU_BBOX"
      cp "$SHOTS/graph_probe_14_menu_open.png" "$SHOTS/graph_probe_27_menu_linear_open.png"
  fi
  read -r M2W M2H M2X M2Y <<< "$(printf "%s" "${MENU2_BBOX:-0x0+0+0}" | tr "x+" "  ")"
  if [ "${M2W:-0}" -gt 0 ]; then
      MENU2_ENTRY=$(click_entry "$M2W $M2H $M2X $M2Y" 1 0)
  fi
  # Menus closed by the overlay own successor frame, as in phase 4 - nothing forced.
  import -window root "$SHOTS/graph_probe_28_curve_linear.png"
  MENU2_LINE=$(summary_line)

  # AND THE SAME EASING A SECOND TIME: re-picking what the key already carries must move
  # nothing at all - the control that makes the assertion above an assertion about the
  # EASING rather than about the menu having been used.
  MENU3_BBOX=""
  MENU3_ENTRY=""
  if [ -n "${MENU_ANCHOR:-}" ] && open_key_menu $MKX2 $MKY2; then
      MENU3_BBOX="$MENU_BBOX"
      cp "$SHOTS/graph_probe_14_menu_open.png" "$SHOTS/graph_probe_29_menu_linear_again_open.png"
  fi
  read -r M3W M3H M3X M3Y <<< "$(printf "%s" "${MENU3_BBOX:-0x0+0+0}" | tr "x+" "  ")"
  if [ "${M3W:-0}" -gt 0 ]; then
      MENU3_ENTRY=$(click_entry "$M3W $M3H $M3X $M3Y" 1 0)
  fi
  import -window root "$SHOTS/graph_probe_30_curve_linear_again.png"
  MENU3_LINE=$(summary_line)

  echo "phase 4b: the angle of the key was [${ANGLE_KEY:-unreadable}], the knob row at the next frame [${KNOB_2ND:-none}], the seeded value after [${KNOB_2ND_PRESSES:-?}] press(es) [${ANGLE_2ND_SEEDED:-unreadable}], the field own rect [${ANGLE_2ND_RECT:-none}], typed [${NEWDEG:-none}], read back [${ANGLE_2ND_WRITTEN:-unreadable}], the knob row after the press [${KNOB_2ND_AFTER:-none}]; the menu own own rect [${MENU2_BBOX:-none}] and the entry clicked [${MENU2_ENTRY:-none}], the state line [${MENU2_LINE:-none}]; picking it again: rect [${MENU3_BBOX:-none}], entry [${MENU3_ENTRY:-none}], line [${MENU3_LINE:-none}]"
  echo "ANGLE_KEY=\"$ANGLE_KEY\""             >> $G
  echo "KNOB_2ND=\"$KNOB_2ND\""               >> $G
  echo "KNOB_2ND_PRESSES=\"$KNOB_2ND_PRESSES\"" >> $G
  echo "KNOB_2ND_AFTER=\"$KNOB_2ND_AFTER\""   >> $G
  echo "ANGLE_2ND_SEEDED=\"$ANGLE_2ND_SEEDED\"" >> $G
  echo "ANGLE_2ND_RECT=\"$ANGLE_2ND_RECT\""   >> $G
  echo "ANGLE_2ND_TRIES=\"$ANGLE_2ND_TRIES\"" >> $G
  echo "ANGLE_2ND_TYPED_TEXT=\"$ANGLE_2ND_TYPED_TEXT\"" >> $G
  echo "ANGLE_2ND_EDIT_TEXT=\"$ANGLE_2ND_EDIT_TEXT\"" >> $G
  echo "DOCK_FOCUS_BEFORE=\"$DOCK_FOCUS_BEFORE\"" >> $G
  echo "DOCK_FOCUS_CLICK=\"$DOCK_FOCUS_CLICK\""   >> $G
  echo "DOCK_FOCUS_AFTER=\"$DOCK_FOCUS_AFTER\""   >> $G
  echo "DOCK_TARGET_BEFORE=\"$DOCK_TARGET_BEFORE\"" >> $G
  echo "DOCK_TARGET_AFTER=\"$DOCK_TARGET_AFTER\""   >> $G
  echo "NEWDEG=\"$NEWDEG\""                   >> $G
  echo "ANGLE_2ND_WRITTEN=\"$ANGLE_2ND_WRITTEN\"" >> $G
  echo "MENU2_BBOX=\"$MENU2_BBOX\""           >> $G
  echo "MENU2_ENTRY=\"$MENU2_ENTRY\""         >> $G
  echo "MENU2_LINE=\"$MENU2_LINE\""           >> $G
  echo "MENU3_BBOX=\"$MENU3_BBOX\""           >> $G
  echo "MENU3_ENTRY=\"$MENU3_ENTRY\""         >> $G
  echo "MENU3_LINE=\"$MENU3_LINE\""           >> $G

  # ---- PHASE 3: THE DELETED TARGET, THE GATE THIS PROBE EXISTS FOR ----
  # ctrl::GraphEditor keeps its targets as RAW core::ObjectNode* (GraphEditor.h:409,
  # mTargets), and buildEntries() dereferences every one of them on every draw
  # (GraphEditor.cpp:641). Nothing in that list is told when a node is destroyed, so the
  # only thing standing between a deletion and a use-after-free is an ORDERING property
  # in the tree widget: the removal of the row re-emits itemSelectionChanged before
  # anything repaints, its handler resolves the selection to nullptr, and the lambda in
  # MainWindow then hands setGraphTargets() the (now empty) selection - the dead node
  # leaves the target list before buildEntries() can walk it. That order is asserted
  # nowhere: the panel tests in the suite drive a SYNTHETIC model, and this probe never
  # deleted a target. A regression would not fail an assertion, it would SIGSEGV inside
  # the draw of the panel, and under AT-SPI that surfaces as a walk that times out. So
  # delete the very layer the panel is showing and require: the process still alive, the
  # row gone, and the panel back in one of the model own empty states with the pixels of
  # the curve gone.
  #
  # HOW THE DELETE ENTRY IS PRESSED. Three measured facts decide the route, and each was
  # established by running the app under Xvfb rather than assumed:
  #   * the row reported by AT-SPI is NOT clipped to the tree viewport (MEASURED:
  #     "Folder16" reports 40 221 115 22 while the splitter leaves the tree about 68 px
  #     wide), so the CENTRE of the rect lands in the timeline beside it: a right-click
  #     there opens the menu of the TIMELINE (read off a screenshot: Copy key / Copy key
  #     to clipboard / Paste key / Select spacing between keys / Change key(s) easing to /
  #     Change key(s) range to / Delete key). The click is therefore made at the LEFT edge
  #     of the row rect + 4 px, which is inside the row and inside the tree, and it is the
  #     menu the object tree owns that opens (read off a screenshot: Contract / Rename /
  #     Create layer / Create folder / Paste from clipboard / Duplicate node / Add default
  #     keys / Delete).
  #   * that menu is NOT in the accessibility tree at all, so its entries cannot be
  #     addressed by accessible name: MEASURED with the menu on screen and its window
  #     mapped IsViewable, the tree of the app still published exactly the five menu-bar
  #     popups and no `menu item` named Delete, while a desktop-wide walk found "Delete"
  #     only in OTHER applications on the bus (KeePassXC and friends). The menu is a QMenu
  #     created on the stack inside ObjectTreeWidget::onContextMenuRequested
  #     (ObjectTreeWidget.cpp:699) and the a11y bridge never exposes it, unlike the menus
  #     of the menu bar, which are published even while closed. So there is no name to
  #     use, and this is recorded as a gap rather than worked around.
  #   * what IS usable is the window of the menu itself plus the keyboard. The app opens
  #     it AT THE POINTER, so `xwininfo -root -children` names the window whose origin is
  #     the click point (MEASURED: 0x200032 158x223+44+232, Map State IsViewable). With
  #     no window manager the menu does not take the input focus by itself - MEASURED:
  #     `xdotool getwindowfocus` reports the window of the app while the menu is up, and
  #     keys sent without focusing it do nothing (Up+Return left the row in place) - so
  #     the probe points the focus AT that window and then drives the menu with Up (which
  #     highlights the LAST entry of the menu) and Return. That entry is Delete by
  #     construction (onContextMenuRequested adds mDeleteAction last, after the other
  #     seven) and by measurement (the menu was read off the screenshot listed above: it
  #     offers exactly ONE wording that means remove-this-layer, "Delete"). The Qt menu
  #     type-ahead is NOT a name route here: MEASURED, typing d,e,l,e,t,e then Return
  #     deleted nothing. The probe therefore identifies the entry by menu position and
  #     asserts the EFFECT, which is what makes the gate sound either way: if the entry
  #     pressed were any other one, the row would not be gone and the panel would not
  #     fall back - both of which are asserted below.
  # The pre-deletion control pair, with nothing forced between them: the panel is drawing
  # the state the deletion is about to change, and the pair is expected to be 0 px.
  import -window root "$SHOTS/graph_probe_17_pre_delete_a.png"
  import -window root "$SHOTS/graph_probe_18_pre_delete_b.png"

  # The layer to delete is the one the LEGEND named - the label of the model itself,
  # "<layer> : <channel>" - addressed as the `table cell` the tree publishes for it. That
  # is what ties the deletion to what the panel is showing; the row the phase-2 loop
  # happened to activate is only the fallback.
  LAYER=$(printf "%s" "$LEGEND_NAME" | sed "s/ *: .*//")
  LROW=$(ae extents "$LAYER" "table cell")
  if [ -z "$LROW" ] && [ -n "$KEY_ROW" ]; then
      LAYER="$KEY_ROW"
      LROW=$(ae extents "$LAYER" "table cell")
  fi
  POP=""
  POP_INFO=""
  DEL_FOCUS=""
  if [ -n "$LROW" ]; then
      read -r RX RY RW RH <<< "$LROW"
      DX=$((RX + 4)); DY=$((RY + RH / 2))
      xdotool mousemove --sync $DX $DY
      sleep 0.4
      xdotool click 3
      sleep 1.2
      POP=$(xwininfo -root -children 2>/dev/null | grep -E "[0-9]+x[0-9]+\+${DX}\+${DY}" | grep -oE "0x[0-9a-f]+" | head -1)
      if [ -n "$POP" ]; then
          POP_INFO=$(xwininfo -id "$POP" 2>/dev/null | grep -E "Map State|Width:|Height:" | tr -s " " | tr "\n" ";")
          xdotool windowfocus --sync "$POP" 2>/dev/null
          DEL_FOCUS=$(xdotool getwindowfocus 2>/dev/null)
          xdotool key --clearmodifiers Up
          sleep 0.4
          xdotool key --clearmodifiers Return
          sleep 2
      fi
  fi

  # THE ORDER OF THE TWO QUESTIONS MATTERS AND SO DOES THE FIRST ONE: a process that died
  # in this deletion cannot be asked anything, and every later `ae` call would spend its
  # own timeout saying so.
  DEL_ALIVE=$(kill -0 $app 2>/dev/null && echo yes || echo no)
  DEL_ROW_AFTER=$(ae extents "$LAYER" "table cell")
  DEL_EMPTY=$(ae extents "Select a layer to show its curves" "label")
  DEL_NOKEYS=$(ae extents "No keys on the selected layers" "label")
  DEL_ALLHID=$(ae extents "All channels of the selected layers are hidden" "label")
  DEL_LEGEND=$(legend list | tr "\n" ";")
  DEL_CELLS=$(ae dump 2>/dev/null | sed -n "s/^ *table cell *| //p" | tr "\n" ";")
  # No forced paint: deleting the row changes the tree selection, and the tree own
  # selection signal asks the viewport for a frame (DriverHolder::onSelectionChanged ->
  # onVisualUpdated, plus the setGraphTargets call MainWindow makes on the same signal).
  import -window root "$SHOTS/graph_probe_19_after_delete.png"

  echo "DEL_LAYER=\"$LAYER\""             >> $G
  echo "AE_PID=\"$app\""                   >> $G
  echo "DEL_ROW=\"$LROW\""               >> $G
  echo "DEL_POPUP=\"$POP\""              >> $G
  echo "DEL_POPUP_INFO=\"$POP_INFO\""    >> $G
  echo "DEL_FOCUS=\"$DEL_FOCUS\""        >> $G
  echo "DEL_ALIVE=\"$DEL_ALIVE\""        >> $G
  echo "DEL_ROW_AFTER=\"$DEL_ROW_AFTER\"" >> $G
  echo "DEL_EMPTY=\"$DEL_EMPTY\""        >> $G
  echo "DEL_NOKEYS=\"$DEL_NOKEYS\""      >> $G
  echo "DEL_ALLHID=\"$DEL_ALLHID\""      >> $G
  echo "DEL_LEGEND=\"$DEL_LEGEND\""      >> $G
  echo "DEL_CELLS=\"$DEL_CELLS\""        >> $G

  kill $app 2>/dev/null; wait $app 2>/dev/null
AE_PROBE_BODY_END

# THE BODY'S OWN FIRST WRITE IS ITS WORD ON WHETHER THE BODY RAN AT ALL, and the two cases
# are different failures that used to share one message. An ABSENT file means the body never
# reached its geometry write - it died early, its output is in the body log, and nothing here
# can be said about the panel. A file that is there but empty of FRAME/PROP/ANIM is the panel
# (or the AT-SPI tree) reporting no usable rectangle. Measured 2026-10-14: the absent case
# printed only "the overlay bounds came back non-empty from AT-SPI ()" and sent a whole
# investigation looking for an X server fault.
if [ ! -e /tmp/probe_graph_overlay_geom.txt ]; then
    check 1 "the probe body ran to its first geometry write (nothing wrote /tmp/probe_graph_overlay_geom.txt; the body's own output is in /tmp/probe_graph_overlay_body.log)"
    summary probe_graph_overlay
fi

if grep -q TIMEOUT /tmp/probe_graph_overlay_geom.txt 2>/dev/null; then
    check 1 "the application reached its main window"
    summary probe_graph_overlay
fi
if grep -q NOGEOM /tmp/probe_graph_overlay_geom.txt 2>/dev/null; then
    # NOT a silent pass: with no frame, no Properties dock and no Animation panel there
    # is no way to say WHERE the change happened, and a probe that cannot bound its
    # measurement must fail rather than measure the whole screen and call it good.
    check 1 "the overlay bounds were readable from AT-SPI ($(tr '\n' ' ' < /tmp/probe_graph_overlay_geom.txt 2>/dev/null))"
    summary probe_graph_overlay
fi

eval "$(cat /tmp/probe_graph_overlay_geom.txt 2>/dev/null)"
if [ -z "${FRAME:-}" ] || [ -z "${PROP:-}" ] || [ -z "${ANIM:-}" ]; then
    check 1 "the overlay bounds came back non-empty from AT-SPI ($(tr '\n' ' ' < /tmp/probe_graph_overlay_geom.txt 2>/dev/null))"
    summary probe_graph_overlay
fi
read -r FRAME_X FRAME_Y FRAME_W FRAME_H <<< "${FRAME:-0 0 0 0}"
read -r PROP_X PROP_Y PROP_W PROP_H <<< "${PROP:-0 0 0 0}"
read -r ANIM_X ANIM_Y ANIM_W ANIM_H <<< "${ANIM:-0 0 0 0}"
# Pre-initialized so a rectangle that parses short cannot abort the run under `set -u`
# (the checks below would fail with an unbound variable instead of a readable FAIL).
FRAME_X=${FRAME_X:-0}; FRAME_Y=${FRAME_Y:-0}; FRAME_W=${FRAME_W:-0}; FRAME_H=${FRAME_H:-0}
PROP_X=${PROP_X:-0}; PROP_Y=${PROP_Y:-0}; PROP_W=${PROP_W:-0}; PROP_H=${PROP_H:-0}
ANIM_Y=${ANIM_Y:-0}; ANIM_H=${ANIM_H:-0}
# The crop's top edge is the menu bar's bottom, which is the dock area's top edge: the
# menu bar has no accessible name (see the header), and the docks start exactly under it.
VIEW_TOP=$PROP_Y
CROP_W=$((PROP_X - FRAME_X))
CROP_H=$((ANIM_Y - VIEW_TOP))
CROP_AREA=$((CROP_W * CROP_H))
echo "geometry: frame ($FRAME); Properties ($PROP) -> viewport right x=$PROP_X; Animation ($ANIM) -> viewport bottom y=$ANIM_Y; crop ${CROP_W}x${CROP_H}+${FRAME_X}+${VIEW_TOP}"

# A rectangle that is not a rectangle is not a bound: every edge used below is asserted
# usable before anything is measured against it, and the crop is asserted non-empty.
[ "$FRAME_W" -gt 0 ] && [ "$FRAME_H" -gt 0 ] && [ "$PROP_W" -gt 0 ] && [ "$ANIM_H" -gt 0 ]
check $? "the AT-SPI rectangles are usable (frame $FRAME, Properties $PROP, Animation $ANIM)"
[ "$PROP_X" -gt "$FRAME_X" ] && [ "$ANIM_Y" -gt "$VIEW_TOP" ]
check $? "the viewport crop is a usable rectangle (${CROP_W}x${CROP_H}+${FRAME_X}+${VIEW_TOP} = $CROP_AREA px)"
[ "$VIEW_TOP" -gt "$FRAME_Y" ] && [ "$VIEW_TOP" -lt "$((FRAME_Y + FRAME_H))" ]
check $? "the crop starts below the window's top edge and inside it (menu bar bottom $VIEW_TOP, frame y $FRAME_Y..$((FRAME_Y + FRAME_H)))"

# ---- THE SUBJECT IS PINNED, AND THE PROBE SAYS WHICH PROJECT IT REALLY RAN ON ----
# Only when this probe chose the project itself (arg 1 / AE_PROJECT unset): a caller who
# names a project is choosing the subject on purpose, and the probe then records what it
# got rather than asserting a pin it did not make. See the header for why the fixture
# argument is not what the app opens.
if [ "$PINNED" -eq 1 ]; then
    LOADED=$(sed -n 's/^image path = "\(.*\)"$/\1/p' /tmp/probe_graph_overlay.log 2>/dev/null | head -1)
    [ "$LOADED" = "$ROOT/assets/sample.psd" ]
    check $? "the pinned project really loaded: the app's own log names [$LOADED] (the fixture argument is not opened - the command line opens only .anie, Main.cpp:212 - so the Debug build's startup project assets/sample.psd is the subject, and it is in the repository)"

    [ "${ROW:-}" = "sample" ]
    check $? "and the object tree's first row names that project (row [${ROW:-none}], the PSD's own base name; a different project here means the pin needs re-recording)"
fi

assert_exists "$SHOTS/graph_probe_01_off_empty.png" "the viewport with the graph off and nothing selected"
assert_exists "$SHOTS/graph_probe_02_on_empty.png"  "the viewport with the graph on and nothing selected"
assert_exists "$SHOTS/graph_probe_03_off_empty.png" "the viewport with the graph off again"
assert_exists "$SHOTS/graph_probe_04_off_top.png"   "the viewport with the top row selected and the graph off"
assert_exists "$SHOTS/graph_probe_05_on_top.png"    "the viewport with the top row selected and the graph on"
assert_exists "$SHOTS/graph_probe_06_off_top.png"   "the viewport with the top row selected and the graph off again"
assert_exists "$SHOTS/graph_probe_20_wheel_on_a.png"    "the viewport with the graph on, before the wheel (control a)"
assert_exists "$SHOTS/graph_probe_21_wheel_on_b.png"    "the same state a second time, as the control"
assert_exists "$SHOTS/graph_probe_22_wheel_on_after.png" "the viewport after a wheel over it, with the graph on"
assert_exists "$SHOTS/graph_probe_23_wheel_off_a.png"    "the viewport with the graph off, before the wheel (control a)"
assert_exists "$SHOTS/graph_probe_24_wheel_off_b.png"    "the same state a second time, as the control"
assert_exists "$SHOTS/graph_probe_25_wheel_off_after.png" "the viewport after the same wheel, with the graph off"

# COMPARE NORMALIZED COPIES. `import` writes an image whose PNG type follows its content
# (a bland frame comes out Grayscale, a colourful one sRGB), and `compare` seeds its
# metric from the FIRST image's colorspace - measured on this machine, comparing a
# Grayscale shot against an sRGB one reported 0 differing pixels for images that differ
# by 1200. Forcing both sides to truecolor sRGB removes that trap. The copies live in a
# temp dir so $SHOTS holds only the captures; the diff image stays there for diagnosis.
NORM=$(mktemp -d)
norm() { magick "$1" -colorspace sRGB -type TrueColor PNG24:"$2"; }
norm "$SHOTS/graph_probe_01_off_empty.png" "$NORM/off_empty.png"
norm "$SHOTS/graph_probe_02_on_empty.png"  "$NORM/on_empty.png"
norm "$SHOTS/graph_probe_03_off_empty.png" "$NORM/off_empty2.png"
norm "$SHOTS/graph_probe_04_off_top.png"   "$NORM/off_top.png"
norm "$SHOTS/graph_probe_05_on_top.png"    "$NORM/on_top.png"
norm "$SHOTS/graph_probe_06_off_top.png"   "$NORM/off_top2.png"
norm "$SHOTS/graph_probe_20_wheel_on_a.png"     "$NORM/wheel_on_a.png"
norm "$SHOTS/graph_probe_21_wheel_on_b.png"     "$NORM/wheel_on_b.png"
norm "$SHOTS/graph_probe_22_wheel_on_after.png" "$NORM/wheel_on_after.png"
norm "$SHOTS/graph_probe_23_wheel_off_a.png"    "$NORM/wheel_off_a.png"
norm "$SHOTS/graph_probe_24_wheel_off_b.png"    "$NORM/wheel_off_b.png"
norm "$SHOTS/graph_probe_25_wheel_off_after.png" "$NORM/wheel_off_after.png"
echo "normalized shots and the diffs: $NORM"

# Differing pixels between two shots. No fuzz: both frames are the same renderer in the
# same state, and the OFF-vs-OFF measurements below are 0 differing pixels, so there is no
# antialiasing noise for a tolerance to absorb.
ae_diff() { # ae_diff <a.png> <b.png>
    magick compare -metric AE "$1" "$2" null: 2>&1 | awk '{print $1+0}'
}

# The viewport crop, applied to an already normalized shot. Its rectangle comes from the
# two docks and the frame (above); `crop` never touches the full frame again.
crop() { magick "$1" -crop "${CROP_W}x${CROP_H}+${FRAME_X}+${VIEW_TOP}" +repage -colorspace sRGB -type TrueColor PNG24:"$2"; }

# Fractions, as percentages with two decimals: the assertions below are about how much of
# the crop the panel covers, and an integer percent would hide the difference between the
# states this probe measures (5.72 vs 5.88 vs 7.15).
pct() { awk -v a="$1" -v b="$2" 'BEGIN{printf "%.2f", 100*a/b}'; }
ge() { awk -v a="$1" -v b="$2" 'BEGIN{exit !(a >= b)}'; }
le() { awk -v a="$1" -v b="$2" 'BEGIN{exit !(a <= b)}'; }

# THE BARS FOR THE CROP, both measured on this build with real margin:
#   * FLOOR 2.00%: "something drew". The toggle's own 24x24 highlight is 568 px = 0.91% of
#     the crop, so a floor above that cannot be cleared by the button repainting itself,
#     and the smallest measured state (the empty panel, 2200 px = 3.51%) is nearly 2x it.
#   * CEILING 20.00%: "the panel is not a wash". It draws ticks, frame numbers, value
#     labels, a 1px playhead, the state line and the legend - measured 3.72% with the top
#     row selected and 7.15% after a key was added. A panel that filled its background
#     would cover the viewport (204x146 = 29784 px = 47.5% of the crop) and fail this by
#     a factor of two; 20% is 2.8x above the largest measurement and far below a wash.
FLOOR_PCT=2
CEIL_PCT=20

# THE WHEEL'S TWO BARS (phase 5), both measured on this build and re-measured with it:
#   * WHEEL_PANEL_PIXELS: "the graph really zoomed". The panel's frame ticks and their
#     labels are laid out from the frames-per-pixel the wheel changed, so a 3-notch zoom
#     moves them; MEASURED (see the header's numbers) this is far above the bar. It must
#     also stay below what a CANVAS move produces, which is the other number: the same
#     3 notches on the artwork move nearly every pixel of the region, so a canvas zoom
#     cannot hide under this bar.
#   * WHEEL_CANVAS_PIXELS: "the canvas really zoomed". MEASURED on this build: with the
#     panel hidden the same 3-notch wheel moves 12842 px of the crop (about 20%), and the
#     controls in that state are exactly 0 - so the bar sits an order of magnitude under the
#     measurement and far above the controls.
WHEEL_PANEL_PIXELS=300
WHEEL_CANVAS_PIXELS=1000

# THE CURVE'S TWO BARS (phase 4b), the pixel half of the key-menu phase:
#   * SEGMENT_PIXELS: "the second key reached the drawn panel". MEASURED on this build:
#     9527 px = 15.21% of the crop, because a new key changes the shown-curve basis and the
#     value window is refitted to it - the change is the segment, the new key's square and
#     that re-layout together. The bar sits 9x under the measurement; the one-key state it is
#     measured against is a 0 px control (the same state twice).
#   * EASE_PIXELS: "the easing really re-shaped the drawn curve". This is the load-bearing
#     bar of the phase: with a curve on screen the panel draws NO summary line, so the
#     changed pixels are the polyline itself. NOT YET MEASURED - the first recorded run never
#     changed the easing (its menu clicks were dropped by a detection bug since fixed), so
#     this is the number to replace from the first run whose state line reads Linear where
#     the key carried Sine. Its control is in place regardless: the same easing picked twice
#     must be EXACTLY 0.
SEGMENT_PIXELS=1000
EASE_PIXELS=100

# ---- (a) THE PANEL ON vs OFF: IT DREW, AND IT IS NOT A WASH ----
# MEASURED: 2332 px = 3.72% of the crop, against 0 for the OFF/OFF identity below. The
# toggle's 24x24 checked highlight is 568 px, all of it inside this crop and all of it
# part of this change; the rest is the panel.
ON_PIXELS=1000
d_on=$(ae_diff "$NORM/off_top.png" "$NORM/on_top.png")
[ "$d_on" -gt "$ON_PIXELS" ]; check $? "toggling the graph ON redraws the viewport ($d_on px changed on the whole screen, bar $ON_PIXELS)"
crop "$NORM/off_top.png" "$NORM/c_off_top.png"
crop "$NORM/on_top.png"  "$NORM/c_on_top.png"
d_on_crop=$(ae_diff "$NORM/c_off_top.png" "$NORM/c_on_top.png")
p_on=$(pct "$d_on_crop" "$CROP_AREA")
ge "$p_on" "$FLOOR_PCT"; check $? "the panel drew inside the crop ($d_on_crop px = ${p_on}% of ${CROP_W}x${CROP_H}, floor ${FLOOR_PCT}%)"
le "$p_on" "$CEIL_PCT";  check $? "the panel is not a wash: it covers only part of the crop (${p_on}% <= ceiling ${CEIL_PCT}%)"

# The bounding box of that change as magick sees it: the compare image is the source with
# the differing pixels marked, so its trim box is the region that moved.
magick compare -compose src "$NORM/off_top.png" "$NORM/on_top.png" "$NORM/diff_on.png" 2>/dev/null
BBOX=$(magick identify -format "%@" "$NORM/diff_on.png" 2>/dev/null)
BBOX_W=0; BBOX_H=0; BBOX_X=0; BBOX_Y=0
read -r BBOX_W BBOX_H BBOX_X BBOX_Y <<< "$(sed -E 's/^([0-9]+)x([0-9]+)([+-][0-9]+)([+-][0-9]+)$/\1 \2 \3 \4/; s/\+//g' <<< "${BBOX:-0x0+0+0}")"
echo "changed region: ${BBOX_W}x${BBOX_H}+${BBOX_X}+${BBOX_Y} (${BBOX:-unreadable}); crop is x $FRAME_X..$((FRAME_X + CROP_W - 1)), y $VIEW_TOP..$((VIEW_TOP + CROP_H - 1))"

[ "$BBOX_W" -gt 0 ] && [ "$BBOX_H" -gt 0 ]
check $? "the changed region is not empty (${BBOX_W}x${BBOX_H})"

# MEASURED bbox 370x143+8+39 - x 8..377 and y 39..181. The left edge x=8 is the highlight
# of the toggle itself, which is part of the same change, so this probe asserts the two
# edges that are the VIEWPORT's own bounds: the Properties dock is right-docked, so its
# left edge is where the viewport ends (measured: the panel's rightmost changed column is
# 377 against a dock at x=382), and the Animation panel is docked under the central
# widget, so its top edge is where the viewport ends (measured: the bottom of the change
# is 181 against a panel at y=185). An overlay that escaped the viewport would fail one of
# them - which is the whole claim "it draws over the viewport, not over a dock".
[ $((BBOX_X + BBOX_W)) -le "$PROP_X" ]
check $? "the change ends left of the Properties dock (right $((BBOX_X + BBOX_W)) <= dock left $PROP_X)"

[ $((BBOX_Y + BBOX_H)) -le "$ANIM_Y" ]
check $? "the change ends above the Animation panel (bottom $((BBOX_Y + BBOX_H)) <= panel top $ANIM_Y)"

# ...and inside the crop, which is what makes the fraction above a measurement of the same
# region the bbox describes.
[ "$BBOX_X" -ge "$FRAME_X" ] && [ "$BBOX_Y" -ge "$VIEW_TOP" ]
check $? "the change is inside the crop (bbox starts at ${BBOX_X}+${BBOX_Y}, crop at ${FRAME_X}+${VIEW_TOP})"

# MEASURED: the two OFF shots are identical (0 px), so this bar is not a tolerance on a
# jittery number - it only has to keep the effect of the second toggle from being
# mistaken for the first. A quarter of the ON change is far below it either way.
d_off=$(ae_diff "$NORM/off_top.png" "$NORM/off_top2.png")
[ $((d_off * 4)) -le "$d_on" ]; check $? "toggling it OFF restores the pixels (off-vs-off $d_off px against off-vs-on $d_on px)"

# ---- (f) PHASE 5: THE WHEEL, MEASURED IN THE TWO REGIONS THE MODEL'S OWN RECTS BOUND ----
# THE REGIONS, STATED ONCE AND USED BY EVERY ASSERTION BELOW. The PANEL region is the
# bounding box of the OFF->ON change measured just above - the panel's own ink, and only
# that - dilated by 2 px so the contrast outline's 1 px rings cannot straddle its edge.
# The CANVAS region is the crop WITHOUT that box: the strip of the viewport the panel
# never paints in. Both are derived from this run's own pixels and from the docks, never
# from a constant; the graph-hidden case below is the canvas region's own control, since
# the same pixels must move when the wheel reaches the canvas.
BOX_X=$((BBOX_X - FRAME_X - 2)); BOX_Y=$((BBOX_Y - VIEW_TOP - 2))
BOX_W=$((BBOX_W + 4));          BOX_H=$((BBOX_H + 4))
CANVAS_AREA=$((CROP_W * CROP_H - BOX_W * BOX_H))
echo "wheel regions: the panel's ink box is ${BOX_W}x${BOX_H}+${BOX_X}+${BOX_Y} in the crop (from the OFF/ON change ${BBOX_X}+${BBOX_Y} dilated 2 px); the canvas region is the rest of the ${CROP_W}x${CROP_H} crop = $CANVAS_AREA px"
# A region that is not a region is not a control: an ink box that swallowed the crop
# would make "the canvas is unchanged" true by having nothing left to check.
[ "$BOX_W" -gt 0 ] && [ "$BOX_H" -gt 0 ] && [ "$CANVAS_AREA" -gt 2000 ]
check $? "the two wheel regions are both real (ink box ${BOX_W}x${BOX_H}, canvas $CANVAS_AREA px of the ${CROP_AREA} px crop)"

# Mask the panel's ink box out of a crop, so what remains is exactly the canvas region.
mask_canvas() { # mask_canvas <in> <out>
    magick "$1" -fill '#000000' \
        -draw "rectangle ${BOX_X},${BOX_Y} $((BOX_X + BOX_W - 1)),$((BOX_Y + BOX_H - 1))" \
        -colorspace sRGB -type TrueColor PNG24:"$2"
}

crop "$NORM/wheel_on_a.png"     "$NORM/c_wheel_on_a.png"
crop "$NORM/wheel_on_b.png"     "$NORM/c_wheel_on_b.png"
crop "$NORM/wheel_on_after.png" "$NORM/c_wheel_on_after.png"
crop "$NORM/wheel_off_a.png"    "$NORM/c_wheel_off_a.png"
crop "$NORM/wheel_off_b.png"    "$NORM/c_wheel_off_b.png"
crop "$NORM/wheel_off_after.png" "$NORM/c_wheel_off_after.png"
mask_canvas "$NORM/c_wheel_on_b.png"     "$NORM/m_wheel_on_b.png"
mask_canvas "$NORM/c_wheel_on_after.png" "$NORM/m_wheel_on_after.png"
mask_canvas "$NORM/c_wheel_off_b.png"    "$NORM/m_wheel_off_b.png"
mask_canvas "$NORM/c_wheel_off_after.png" "$NORM/m_wheel_off_after.png"

# THE CONTROLS FIRST, in both visibility states: two captures of the same state, so
# neither change below can be the panel or the canvas repainting on its own.
d_wheel_on_ctl=$(ae_diff "$NORM/wheel_on_a.png" "$NORM/wheel_on_b.png")
[ "$d_wheel_on_ctl" -eq 0 ]
check $? "the panel does not repaint on its own before the wheel (control $d_wheel_on_ctl px, graph visible)"
d_wheel_off_ctl=$(ae_diff "$NORM/wheel_off_a.png" "$NORM/wheel_off_b.png")
[ "$d_wheel_off_ctl" -eq 0 ]
check $? "nor does the canvas (control $d_wheel_off_ctl px, graph hidden)"

# THE GRAPH'S WHEEL: THE PANEL MOVED, THE CANVAS DID NOT. The panel's frame ticks and
# their labels are laid out from the frames-per-pixel the wheel changed, so they move;
# every pixel OUTSIDE the panel's ink box is byte-identical, which is the whole claim
# "the wheel zoomed the graph and left the artwork alone".
d_wheel_panel=$(ae_diff "$NORM/c_wheel_on_b.png" "$NORM/c_wheel_on_after.png")
d_wheel_canvas=$(ae_diff "$NORM/m_wheel_on_b.png" "$NORM/m_wheel_on_after.png")
p_wheel_panel=$(pct "$d_wheel_panel" "$CROP_AREA")
[ "$d_wheel_panel" -gt "$WHEEL_PANEL_PIXELS" ]
check $? "with the graph visible a wheel over the viewport moves the panel alone ($d_wheel_panel px = ${p_wheel_panel}% of the crop changed, bar $WHEEL_PANEL_PIXELS)"
[ "$d_wheel_canvas" -eq 0 ]
check $? "and the canvas is byte-identical: all $CANVAS_AREA px outside the panel's ink box are unchanged ($d_wheel_canvas px changed; the graph owns the wheel while it is visible, Driver.cpp:215)"

# AND THE OTHER DIRECTION, which is also this region's control: with the graph hidden the
# same wheel at the same point reaches the canvas mover, and the artwork scales - over the
# WHOLE CROP, and the reason is measured: the canvas region
# defined above is the crop minus the panel's ink box, i.e. the crop's PERIPHERY (a 16 px band
# at the top and a few columns at the sides) - and the artwork can be letterboxed away from the
# periphery, in which case a canvas zoom does not touch it (MEASURED on this build: 0 px
# there, while the same wheel moved 12842 px inside the box). With the panel hidden nothing is
# painted over the crop at all, so the crop IS the canvas (plus the Tools dock, which a wheel
# over the viewport cannot reach), and that is what is compared.
d_wheel_hidden=$(ae_diff "$NORM/c_wheel_off_b.png" "$NORM/c_wheel_off_after.png")
p_wheel_hidden=$(pct "$d_wheel_hidden" "$CROP_AREA")
magick compare -compose src "$NORM/c_wheel_off_b.png" "$NORM/c_wheel_off_after.png" "$NORM/diff_wheel_off.png" 2>/dev/null
WHEEL_OFF_BBOX=$(magick identify -format "%@" "$NORM/diff_wheel_off.png" 2>/dev/null)
[ "$d_wheel_hidden" -gt "$WHEEL_CANVAS_PIXELS" ]
check $? "with the graph hidden the same wheel scales the artwork: the crop moves ($d_wheel_hidden px = ${p_wheel_hidden}% of it, bar $WHEEL_CANVAS_PIXELS, changed region [${WHEEL_OFF_BBOX:-unreadable}] inside the viewport - and the whole crop is canvas while the panel paints nothing)"
[ -z "${WHEEL_HIDDEN_FIT:-}" ]
check $? "and the panel really is off while it does: it publishes no fit action at all (rect ${WHEEL_HIDDEN_FIT:-none}), so that change is the canvas's and not the panel's"

# ---- (f2) THE RING CAPTURE: A FILE TO LOOK AT, AND NOTHING MORE ----
# The two assertions are the only honest ones for a capture that exists to be SEEN: the file
# is there, and it is not a black frame (which would mean the app died during the zoom). The
# brightness of the artwork under the panel's ink is recorded in the geometry file beside it
# (RING_BRIGHT/RING_INK_BOX) so a reader can tell whether the frame had anything to show; the
# judgement of whether the ring READS at this zoom is the reader's, not this probe's.
echo "ring capture: anchor [${RING_POINT:-none}], ${RING_NOTCHES:-?} notches of canvas zoom in and the same out; the panel's ink box over the zoomed artwork [${RING_INK_BOX:-none}], its mean luminance [${RING_BRIGHT:-?}]"
assert_exists "$SHOTS/graph_probe_31_ring_over_art.png" "the capture of the panel over the zoomed, bright artwork"
assert_rendered "$SHOTS/graph_probe_31_ring_over_art.png" "the same capture (not a black frame: the zoom and the panel both rendered)"

# ---- (c) THE EMPTY STATE: NO SELECTION, SO THE PANEL SAYS WHY AND DRAWS THE TEXT ----
# MEASURED: with nothing selected the panel publishes the model's own sentence
# ("Select a layer to show its curves", GraphPanelModel's emptyState) as an accessible
# label, and NOT the "No keys on the selected layers" one - that is what the top row
# selection produces below. Its change inside the crop is 2200 px = 3.51%, text and axes
# only: the smallest of the states this probe measures. Both are asserted, because the
# sentence IS the state the model is in: the panel could draw the right pixels and say the
# wrong thing.
echo "empty state: line [$EMPTY_LINE], 'no keys' line [$NOKEYS_LINE], top-row line later [$TOP_LINE]"
[ -n "${EMPTY_LINE:-}" ]
check $? "the empty state line names the empty state (the model's own sentence is the accessible label, rect ${EMPTY_LINE:-none})"
[ -z "${NOKEYS_LINE:-}" ]
check $? "and not the no-keys sentence, which belongs to a selected layer (rect ${NOKEYS_LINE:-none})"
[ -n "${TOP_LINE:-}" ]
check $? "the top row selected later publishes that no-keys sentence, i.e. the selection IS a target whose channels have no key (rect ${TOP_LINE:-none})"

crop "$NORM/off_empty.png" "$NORM/c_off_empty.png"
crop "$NORM/on_empty.png"  "$NORM/c_on_empty.png"
d_empty_crop=$(ae_diff "$NORM/c_off_empty.png" "$NORM/c_on_empty.png")
p_empty=$(pct "$d_empty_crop" "$CROP_AREA")
ge "$p_empty" "$FLOOR_PCT"; check $? "the empty panel still drew its state line ($d_empty_crop px = ${p_empty}% of the crop, floor ${FLOOR_PCT}%)"
le "$p_empty" "$CEIL_PCT";  check $? "the empty state is text only, not a wash (${p_empty}% <= ceiling ${CEIL_PCT}%)"
d_off_empty=$(ae_diff "$NORM/off_empty.png" "$NORM/off_empty2.png")
[ "$d_off_empty" -eq 0 ]; check $? "the two OFF shots of the empty state are identical (control $d_off_empty px)"

# ---- (d) PHASE 2, HARDENED: THE KNOB ROW AND THE ANGLE VALUE, BEFORE AND AFTER ----
# WHAT THE PROBE PRESSED, AND WHAT THE DOCK DID ABOUT IT. A key that no knob was ever
# shown for cannot have been added, so the press is attributed BEFORE the pixels are. The
# assertions are the TRANSITION, not the after-state: the knob row exists and the channel
# rows (Angle) do not, then the collapse lifts the knob row into the dock's viewport,
# then the press turns the row into the channel rows and the Angle field becomes readable.
# MEASURED on the default project: knob row 414 394 36 21 (below the fold) -> 414 194 36 21
# after collapsing Constants and Default Keys, Angle absent before and 382 245 82 23 after,
# and its value reads 0. The test then asserts that value is a NUMBER, not the label text.
#
# WHICH ROW GOT SELECTED is deliberately NOT asserted, because the tree cannot answer it
# unambiguously: MEASURED, the object tree publishes its rows as `table cell` nodes named
# after the objects (top `running thru my mind`, then `Folder16`, then `Layer1`), the probe
# activates them one by one until the knob appears, and the legend then names the node that
# received the key as `Folder16 : Rotate` - the model's own name for it, which need not be
# the cell text the probe activated (a cell click that selects nothing leaves the previous
# selection in place, and nothing in the accessibility tree reports which row is current).
# What IS asserted is the panel's own state: the knob row exists, the channel rows do not,
# the collapse lifts the row, and the press turns one into the other.
echo "phase 2: last row activated [${KEY_ROW:-none}], knob row before [${KNOB_BEFORE:-none}], knob row after the collapse [${KNOB:-none}], Angle before [${ANGLE_BEFORE:-none}], Angle after [${ANGLE:-none}] = ${ANGLE_VALUE:-unreadable}"
[ -n "${KNOB_BEFORE:-}" ]
check $? "the Current Keys add-key knob is addressable for the layer the tree had selected (last row activated: ${KEY_ROW:-none}; rect ${KNOB_BEFORE:-none})"
[ -z "${ANGLE_BEFORE:-}" ]
check $? "the channel rows are NOT there before the press (Angle rect ${ANGLE_BEFORE:-none}; prop::KeyKnocker stands in for them while the frame has no key)"
[ -z "${ANGLE_VALUE_BEFORE:-}" ]
check $? "and the Angle value is not readable before the press either (${ANGLE_VALUE_BEFORE:-empty}: the field is hidden with its group)"
KNOB_BEFORE_Y=$(printf '%s' "${KNOB_BEFORE:-0 0 0 0}" | cut -d' ' -f2)
read -r KX KY KW KH <<< "${KNOB:-0 0 0 0}"
[ -n "${KNOB:-}" ] && [ "$KY" -lt "${KNOB_BEFORE_Y:-0}" ]
check $? "collapsing the panels above lifted the knob row into the dock viewport (y ${KNOB_BEFORE_Y:-none} -> ${KY:-none})"
[ -n "${ANGLE:-}" ]
check $? "the press turned the knob row into the channel rows, i.e. the key now exists (Angle row ${ANGLE:-none})"
[ -n "${ANGLE_VALUE:-}" ]
check $? "and the Angle field answers with a value, read from its Value interface (${ANGLE_VALUE:-unreadable})"
case "${ANGLE_VALUE:-}" in
    ''|*[!0-9.eE+-]*) ANGLE_NUM=1 ;;
    *)                ANGLE_NUM=0 ;;
esac
check $ANGLE_NUM "that value is a number and not a label or a placeholder (${ANGLE_VALUE:-unreadable})"

assert_exists "$SHOTS/graph_probe_07_on_control.png" "the viewport with the keyed layer selected, before the press"
assert_exists "$SHOTS/graph_probe_08_on_control.png" "the same state a second time, as the control"
assert_exists "$SHOTS/graph_probe_09_after_key.png"  "the viewport after a key was added to the selected layer"
norm "$SHOTS/graph_probe_07_on_control.png" "$NORM/on7.png"
norm "$SHOTS/graph_probe_08_on_control.png" "$NORM/on8.png"
norm "$SHOTS/graph_probe_09_after_key.png"  "$NORM/on9.png"

# THE CONTROL FIRST. Two captures of the same state: if this drifts there is no
# measurement below, only a tolerance. MEASURED: 0 px.
d_control=$(ae_diff "$NORM/on7.png" "$NORM/on8.png")
[ "$d_control" -eq 0 ]; check $? "the panel does not repaint on its own (control $d_control px between two identical states)"

# THE EFFECT, MEASURED WHERE THE PLOT IS. The press also rewrites the dock row it was
# made on (the knob row becomes the channel rows) and touches the document (the
# Animation panel lane gains the key, the project tab gains the modified marker), so
# MEASURED the whole-screen change is 12807 px - of which the crop holds 2162. A
# whole-screen region therefore CANNOT be confined to the viewport once a real key is
# added, and the plot is measured in the crop itself (which also holds the Tools dock,
# whose pixels the key push leaves identical in both captures: see the x-profile
# measured for the header - 0 px changed at x 32..176).
crop "$NORM/on7.png" "$NORM/c_on7.png"
crop "$NORM/on9.png" "$NORM/c_on9.png"
CURVE_PIXELS=200
d_key=$(ae_diff "$NORM/c_on7.png" "$NORM/c_on9.png")
p_key=$(pct "$d_key" "$CROP_AREA")
[ "$d_key" -gt "$CURVE_PIXELS" ]; check $? "adding a key to the selected layer repaints the plot ($d_key px changed inside the crop = ${p_key}%, bar $CURVE_PIXELS)"
le "$p_key" "$CEIL_PCT"; check $? "and adding a key is still not a wash (${p_key}% <= ceiling ${CEIL_PCT}%)"

magick compare -compose src "$NORM/on7.png" "$NORM/on9.png" "$NORM/diff_key_whole.png" 2>/dev/null
WHOLE_BBOX=$(magick identify -format "%@" "$NORM/diff_key_whole.png" 2>/dev/null)
echo "changed region after the key: whole screen ${WHOLE_BBOX:-unreadable} (the dock and the timeline lane the key was added in), of which $d_key px are inside the crop"

# ---- (b) THE LEGEND SWATCH, THROUGH THE PANEL'S OWN CHECKABLE ITEM ----
# The row the phase-2 selection produces is named "<layer> : <channel>" by the model, and
# the model only lists channels that HAVE a key: MEASURED, the panel published NO legend
# check box at all before the press and exactly one after it ("Folder16 : Rotate",
# checked, at 228 163 10 10). The label is therefore not hardcoded - it is read from the
# panel in both states, and the appearance of exactly one row is itself an assertion.
echo "legend rows: before the press [${LEGEND_BEFORE:-none}], after [${LEGEND_ROWS:-none}], control [${LEGEND_NAME:-none}] at [${LEGEND_EXT:-none}]; toggle off: ${LEGEND_OFF:-none}; toggle on: ${LEGEND_ON:-none}"
ROWS_BEFORE=$(printf '%s' "${LEGEND_BEFORE:-}" | tr ';' '\n' | grep -c .)
[ "$ROWS_BEFORE" -eq 0 ]
check $? "the panel published no legend row before any channel had a key (${ROWS_BEFORE:-0} rows)"
ROWS_AFTER=$(printf '%s' "${LEGEND_ROWS:-}" | tr ';' '\n' | grep -c .)
[ "$ROWS_AFTER" -eq 1 ]
check $? "the phase-2 selection publishes exactly one legend row ($ROWS_AFTER rows: ${LEGEND_ROWS:-none})"
case "${LEGEND_NAME:-}" in
    *" : Rotate") LEGEND_OK=0 ;;
    *)            LEGEND_OK=1 ;;
esac
check $LEGEND_OK "that row is named by its channel, the one the probe pressed (${LEGEND_NAME:-none})"
[ -n "${LEGEND_EXT:-}" ]
check $? "and it is addressable as a check box with its own rectangle (${LEGEND_EXT:-none})"

# THE STATE BIT, READ BEFORE AND AFTER EACH ACTIVATION - the panel's own truth about
# whether a channel is shown, not a pixel inference: it is what a screen reader reads.
case "${LEGEND_OFF:-}" in
    "STATE checked -> unchecked") LEGEND_OFF_OK=0 ;;
    *)                            LEGEND_OFF_OK=1 ;;
esac
check $LEGEND_OFF_OK "activating the legend item flips the panel's own state to hidden (${LEGEND_OFF:-none})"
case "${LEGEND_ON:-}" in
    "STATE unchecked -> checked") LEGEND_ON_OK=0 ;;
    *)                            LEGEND_ON_OK=1 ;;
esac
check $LEGEND_ON_OK "activating it again flips the state back to shown (${LEGEND_ON:-none})"

assert_exists "$SHOTS/graph_probe_10_legend_hidden.png" "the viewport with that legend row hidden"
assert_exists "$SHOTS/graph_probe_11_legend_shown.png"  "the viewport with it shown again"
norm "$SHOTS/graph_probe_10_legend_hidden.png" "$NORM/on10.png"
norm "$SHOTS/graph_probe_11_legend_shown.png"  "$NORM/on11.png"
crop "$NORM/on10.png" "$NORM/c_on10.png"
crop "$NORM/on11.png" "$NORM/c_on11.png"

# THE CURVE'S PIXELS GO, AND THEY COME BACK EXACTLY. MEASURED: hiding the channel changes
# 2078 px, ALL inside the crop (the whole screen changes the same 2078 - the panel is the
# only thing that reacts), and showing it again returns the crop to its earlier state
# byte-for-byte (0 px against the capture taken before the toggle). The disappearing
# pixels are the curve the model stopped producing: the model drops a hidden channel from
# its curves, and hiding the only keyed channel is also why the panel then says so in
# words (the "All channels ..." sentence asserted next).
d_hidden=$(ae_diff "$NORM/c_on9.png" "$NORM/c_on10.png")
d_hidden_whole=$(ae_diff "$NORM/on9.png" "$NORM/on10.png")
p_hidden=$(pct "$d_hidden" "$CROP_AREA")
[ "$d_hidden" -gt "$CURVE_PIXELS" ]
check $? "hiding the legend row removes its curve from the crop ($d_hidden px = ${p_hidden}% gone, bar $CURVE_PIXELS)"
[ "$d_hidden_whole" -eq "$d_hidden" ]
check $? "and nothing changed outside the crop ($d_hidden_whole px whole screen vs $d_hidden inside)"

[ -n "${ALLHIDDEN:-}" ]
check $? "with the only keyed channel hidden the panel says so in the model's own words (rect ${ALLHIDDEN:-none})"

d_back=$(ae_diff "$NORM/c_on9.png" "$NORM/c_on11.png")
[ "$d_back" -eq 0 ]
check $? "activating the legend item again restores the pixels exactly ($d_back px against the pre-toggle capture)"
[ -z "${ALLHIDDEN_BACK:-}" ]
check $? "and the all-hidden sentence is gone again (rect ${ALLHIDDEN_BACK:-none})"

# ---- (d) PHASE 4: THE KEY CONTEXT MENU, DRIVEN WITH THE REAL POINTER ----
# WHAT IS ASSERTED, AND WHERE EACH PART COMES FROM. The route first: the panel's own fit
# action gives the plot's rectangle; the scan finds the key with right-clicks along the fit
# rect's centre row, and every candidate is confirmed by the POPUP'S OWN PIXELS, so the point
# the phase then presses is on the key the panel DRAWS; the LEFT press at that point is the
# panel's own answer that it selected the key (the state line's selected-key half moves from
# empty to that key, and the empty reading is asserted too); the popup's rectangle is measured
# from the pixels; and the state line then names the easing the chosen entry wrote. The menu
# cannot be read from the accessibility tree - it is an ImGui popup - so it is addressed by
# position and confirmed by EFFECT, exactly like the object tree's QMenu in phase 3. What is
# NOT asserted: that the key is at the fit rect centre. The value axis is fitted and the
# FRAME axis is the user's own window and is not, so the key sits where its frame is, and the
# old centre test was measuring the probe's own assumption rather than the panel.
echo "phase 4: the panel's fit rect [${MENU_FIT:-none}], its centre [${MENU_DERIVED:-none}], the point the right-click scan found [${MENU_KEY:-none}] (offset [${MENU_OFFSET:-none}], ${MENU_SAMPLES:-?} sample(s) against a bar of ${MENU_PIXEL_BAR:-?} px, [${MENU_SCAN_NOTE:-no hit}], every sample as point=changed-px [${MENU_SCAN_TRACE:-none}], its own popup rectangle [${MENU_SCAN_BBOX:-none}]); the selection before the phase [${MENU_PRE_SCAN:-empty}], after the scan [${MENU_PRESELECT:-empty}], after the deselect at [${MENU_DESELECT:-none}] [${MENU_SEL_BEFORE:-empty}], after the LEFT press [${MENU_SEL_AFTER:-empty}]; the state line before the menu [${MENU_BEFORE:-none}]; the menu's own rectangle from a right-click there [${MENU_BBOX:-none}], the entry clicked [${MENU_ENTRY:-none}]; the state line after [${MENU_AFTER:-none}]; the dock's easing rows before [${MENU_DOCK_BEFORE:-none}] after [${MENU_DOCK_AFTER:-none}]; a right-click on empty plot space at [${MENU_EMPTY:-none}] changed [${MENU_EMPTY_BBOX:-unreadable}]"
assert_exists "$SHOTS/graph_probe_12_menu_control_a.png" "the viewport with the keyed layer's curve, the key selected, no menu"
assert_exists "$SHOTS/graph_probe_13_menu_control_b.png" "the same state a second time, as the control"
assert_exists "$SHOTS/graph_probe_14_menu_open.png" "the viewport with the key menu open on the key"
assert_exists "$SHOTS/graph_probe_15_menu_after.png" "the viewport after choosing an easing from the menu"
assert_exists "$SHOTS/graph_probe_16_menu_nokey.png" "the viewport after a right-click on empty plot space"
assert_exists "$SHOTS/graph_probe_41_key_scan.png" "the viewport at the point the right-click scan found the key, with the menu its click opened"
norm "$SHOTS/graph_probe_12_menu_control_a.png" "$NORM/menu_ctl_a.png"
norm "$SHOTS/graph_probe_13_menu_control_b.png" "$NORM/menu_ctl_b.png"
norm "$SHOTS/graph_probe_14_menu_open.png" "$NORM/menu_open.png"
norm "$SHOTS/graph_probe_15_menu_after.png" "$NORM/menu_after.png"
norm "$SHOTS/graph_probe_16_menu_nokey.png" "$NORM/menu_nokey.png"

# A SCAN THAT CANNOT FIND ITS SUBJECT SAYS SO FIRST, and with its own evidence, because every
# assertion below is about a point it produces: with no point, they are all cascades, and a
# cascade hides which one was the first to be false. The trace is every sample the walk took,
# as point=changed-px, and the bar it was compared against - so a red run reads the lattice it
# tried rather than an empty MENU_KEY. The phase does NOT stop here: phases 4b and 3 still run
# and report their own state, and their failures are then read with this line as the cause.
[ -n "${MENU_KEY:-}" ]
check $? "the right-click scan found a point where the menu OPENS (found [${MENU_KEY:-none}] after ${MENU_SAMPLES:-?} sample(s) of the lattice [${MENU_SCAN_LATTICE:-none}], bar ${MENU_PIXEL_BAR:-?} px; every sample, as point=changed-px: [${MENU_SCAN_TRACE:-no samples taken}])"
# THE ROUTE, step by step. The fit action's rectangle is the plot; the scan found a point
# where a right-click really opened the menu (its own pixels are the witness, so the point
# is on the key the panel DRAWS); the press at that point is the panel's own answer that it
# selected the key; and the right-click there opened a popup whose pixels are a menu-sized
# rectangle at the pointer's own x. The press's reading is taken with the selection emptied
# first, and that is asserted rather than trusted: the half has to MOVE.
read -r MKX MKY <<< "${MENU_KEY:-0 0}"
read -r MDX MDY <<< "${MENU_DERIVED:-0 0}"
read -r MOX MOY <<< "${MENU_OFFSET:-}"
MOX=${MOX:-999}
MOY=${MOY:-999}
read -r MFX MFY MFW MFH <<< "${MENU_FIT:-0 0 0 0}"
read -r MBW MBH MBX MBY <<< "$(printf "%s" "${MENU_BBOX:-0x0+0+0}" | tr "x+" "  ")"
read -r EX2 EY2 <<< "${MENU_EMPTY:-0 0}"
read -r NW NH NX NY <<< "$(printf "%s" "${MENU_EMPTY_BBOX:-0x0+0+0}" | tr "x+" "  ")"
# THE PRESS'S OWN EFFECT. The reading it has to move is the one taken immediately before it,
# and the phase makes that reading EMPTY first - see its own comment: the right-click that
# found the key also selects it (the overlay's key menu acts on the selection), so a "before"
# reading taken after the scan would already name the key and any press on it would move
# nothing. The clear is the graph's own deselect gesture, a Ctrl-press in the plot on no key
# and no band, which the graph OWNS, so it reaches no viewport tool and changes no document
# byte. It is asserted here rather than trusted: a before-reading that is not empty would
# make the statement below about the wrong thing.
[ -n "${MENU_STATE_BEFORE:-}" ] && [ -z "${MENU_SEL_BEFORE:-}" ]
check $? "the selection was cleared before the press this assertion is about, by the graph's own Ctrl-press gesture in the plot at [${MENU_DESELECT:-none}]: the panel's state line [${MENU_STATE_BEFORE:-none}] carries NO selected-key half (and it IS a state line, so the panel was there to be read)"
[ -n "${MENU_SEL_AFTER:-}" ] && [ "${MENU_SEL_AFTER:-}" != "${MENU_SEL_BEFORE:-}" ]
check $? "a LEFT press at the point the scan found SELECTED the key: the panel's state line's selected-key half MOVED, from [${MENU_SEL_BEFORE:-empty}] to [${MENU_SEL_AFTER:-none}] (the line after the press: [${MENU_BEFORE:-none}])"
case "${MENU_BEFORE:-}" in
    *"selected key:"*) MENU_SELECTED=0 ;;
    *)                 MENU_SELECTED=1 ;;
esac
check $MENU_SELECTED "and that press is the PANEL's own answer that the point is on a key: its state line names the key it selected ([${MENU_BEFORE:-none}])"
[ -n "${MENU_KEY:-}" ] \
    && [ "$MKX" -ge "$MFX" ] && [ "$MKX" -le $((MFX + MFW)) ] \
    && [ "$MKY" -ge "$MFY" ] && [ "$MKY" -le $((MFY + MFH)) ]
check $? "and the point the scan found is INSIDE the panel's own published fit rect (found [${MENU_KEY:-none}] in ${MENU_FIT:-none}) - and it is NOT asserted to be at that rect's centre: where the key sits inside the plot is the fit POLICY's business (the frame half of fitAll is what the user asked to change this round; MEASURED in the run this fix is recorded against, the point is 2 px from the centre, offset [${MENU_OFFSET:-none}]), while the phase's business is that the point it presses is on the key that is DRAWN"
[ -n "${MENU_KEY:-}" ] && [ "${MENU_ANCHOR:-}" = "${MENU_KEY:-}" ]
check $? "and it is the point where the popup really OPENED: the search that confirms every candidate by the popup's own pixels recorded the same point as the scan ([${MENU_ANCHOR:-none}] against [${MENU_KEY:-none}], from ${MENU_SAMPLES:-?} sample(s) of the lattice [${MENU_SCAN_LATTICE:-none}], [${MENU_SCAN_NOTE:-no hit}])"
case "${MENU_BEFORE:-}" in
    *" Lin") MENU_BEFORE_LINEAR=0 ;;
    *)          MENU_BEFORE_LINEAR=1 ;;
esac
check $MENU_BEFORE_LINEAR "and that key's easing is the param's own default, Linear ([${MENU_BEFORE:-none}])"
read -r MAX MAY <<< "${MENU_ANCHOR:-0 0}"
V_L=${VIEW_L:-172}; V_T=${VIEW_T:-21}; V_R=${VIEW_R:-382}; V_B=${VIEW_B:-185}
[ "${MBW:-0}" -gt 60 ] && [ "${MBH:-0}" -gt 60 ]
check $? "the right-click opened a popup whose pixels are a menu, not a sliver (${MENU_BBOX:-none})"
# WHERE IT LANDED, AND WHY IT IS NOT AT THE POINTER. The overlay CLAMPS the popup into the
# viewport (a popup is an ImGui window and cannot leave it), and MEASURED on this build the
# menu is about 202x145 while the viewport is 210x164 - so it is wider than the room the
# pointer leaves to its right AND taller than the remainder below it, and BOTH of its axes
# are clamped: it lands in the viewport's top-left corner, not under the pointer. The
# assertion is therefore the overlay's own clamp rule, in screen coordinates:
#   x = max(viewL, min(anchor_x, viewR + 1 - popupW))
#   y = anchor_y while the popup fits below it, else max(viewT, viewB + 1 - popupH)
WANT_X=$MAX
[ "$WANT_X" -lt "$V_L" ] && WANT_X=$V_L
[ "$WANT_X" -gt $((V_R + 1 - MBW)) ] && WANT_X=$((V_R + 1 - MBW))
if [ $((MAY + MBH)) -le $((V_B + 1)) ]; then
    WANT_Y=$MAY
else
    WANT_Y=$((V_B + 1 - MBH)); [ "$WANT_Y" -lt "$V_T" ] && WANT_Y=$V_T
fi
[ $((MBX - WANT_X)) -ge -2 ] && [ $((MBX - WANT_X)) -le 2 ] && [ $((MBY - WANT_Y)) -ge -2 ] && [ $((MBY - WANT_Y)) -le 2 ]
check $? "and it is where the overlay's own clamp puts it (popup ${MBX:-none},${MBY:-none} ${MBW:-none}x${MBH:-none}; expected ${WANT_X},${WANT_Y} from the anchor ${MAX:-none},${MAY:-none} inside ${V_L}..${V_R} / ${V_T}..${V_B})"
[ $((MBY + MBH)) -le $((V_B + 1)) ] && [ "$MBY" -ge "$V_T" ] && [ "$MBX" -ge "$V_L" ] && [ $((MBX + MBW)) -le $((V_R + 1)) ]
check $? "and the whole popup is inside the viewport the panel is confined to (x ${MBX:-none}..$((MBX + MBW)) / y ${MBY:-none}..$((MBY + MBH)) inside ${V_L}..${V_R} / ${V_T}..${V_B})"
# THE ANCHOR IS BOUNDED, AND ITS DISTANCE FROM THE SCANNED POINT IS A MEASUREMENT, not a
# licence: the right-click's target is the key's drawn square (10x10) while the left press
# the scan judged accepts kKeyHitRadius = 10 - twice that - so the menu can only be opened
# within 10 px of where the scan said the key is, and the search stays inside that (a 4 px
# lattice out to 8). Tried points: [${MENU_TRIES:-none}].
[ -n "${MENU_ANCHOR:-}" ] && [ $((MAX - MKX)) -ge -8 ] && [ $((MAX - MKX)) -le 8 ] && [ $((MAY - MKY)) -ge -8 ] && [ $((MAY - MKY)) -le 8 ]
check $? "and that anchor is within the bound the pick radius sets, 8 px of the point the left-press scan found (scanned [${MENU_KEY:-none}], anchor [${MENU_ANCHOR:-none}], tried [${MENU_TRIES:-none}])"

# THE EFFECT: the chosen entry wrote the easing the state line then names. The entry clicked
# is Sine (the 4th easing in the shared display order, index 3 -> row 1 of column 1 in the
# menu's two-column grid), and the line is built from the DOCUMENT - the selected target's
# key - so this is a statement about the document, not about the menu's return value.
case "${MENU_AFTER:-}" in
    *" Sin") MENU_AFTER_SINE=0 ;;
    *)        MENU_AFTER_SINE=1 ;;
esac
check $MENU_AFTER_SINE "choosing an easing from the menu changed the selected key's easing to it (the panel's state line now reads [${MENU_AFTER:-none}], and it read [${MENU_BEFORE:-none}] before)"
case "${MENU_BEFORE:-}" in
    *" Sin") MENU_WAS_SINE=1 ;;
    *)        MENU_WAS_SINE=0 ;;
esac
check $MENU_WAS_SINE "and it was not already that easing, so the line moved for the menu and not for a no-op (before [${MENU_BEFORE:-none}])"

# THE MENU CLOSED AGAIN on the choice: the capture with the menu open and the capture after
# it differ by the popup's own pixels (the panel underneath was redrawn in the same place).
d_menu_gone=$(ae_diff "$NORM/menu_open.png" "$NORM/menu_after.png")
[ "$d_menu_gone" -gt 2000 ]
check $? "and the menu closed when the entry was chosen ($d_menu_gone px changed, bar 2000: the popup's own rectangle)"

d_menu_ctl=$(ae_diff "$NORM/menu_ctl_a.png" "$NORM/menu_ctl_b.png")
[ "$d_menu_ctl" -eq 0 ]
check $? "the panel does not repaint on its own between the two control captures (control $d_menu_ctl px)"

# THE NEGATIVE, as the TRUE claim: a right-click that hits no key opens NO MENU. What makes
# a menu recognisable is its CORNER: the overlay anchors the popup at the pointer (it clamps
# that only when the popup would not fit below), so a menu puts its own top-left there. A
# press that hits no key does not "change nothing" - the graph takes no right-button action,
# so it falls through to the viewport and the canvas reacts - and MEASURED, that reaction is
# a region starting at the VIEWPORT's top edge, not at the pointer. So the assertion is
# about the corner, both axes of it, and about the panel's own line: no menu opened.
[ "${NW:-0}" -eq 0 ] \
    || [ "${NX:-0}" -ne "${EX2:-0}" ] || [ "${NY:-0}" -ne "${EY2:-0}" ]
check $? "a right-click that hits NO key opened no menu: the changed region [${MENU_EMPTY_BBOX:-none}] does not put its top-left corner at the pointer ${EX2:-none},${EY2:-none} (the region is the viewport reacting, which is what the press falls through to)"
[ "${MENU_NOKEY_LINE:-}" = "${MENU_AFTER:-}" ]
check $? "and the panel's own state line is unchanged by it ([${MENU_NOKEY_LINE:-none}])"

# ---- (d) THE MENU'S KEYBOARD, AND THE APPLICATION'S KEYS BESIDE IT ----
# Two claims, and they are the two halves of one toggle (see the phase's own comment and
# gui::ImGuiOverlay's class comment): while the popup is open ImGuiConfigFlags_NavEnableKeyboard
# is set, so ImGui's own popup keyboard behaviour exists and the overlay consumes the keys;
# while it is closed the flag is clear, so the overlay claims nothing and the application's
# own key commands are untouched. Every readback here is either the panel's own state line
# (the document side) or a diff of the popup's own pixels (the menu side), and each has a
# positive control: the same steps with the flag toggle removed stop producing them.
assert_exists "$SHOTS/graph_probe_32_menu_delete.png" "the viewport with the key menu open and the Delete key command pressed at it"
assert_exists "$SHOTS/graph_probe_33_menu_escape.png" "the viewport after Escape was pressed with the key menu open"
assert_exists "$SHOTS/graph_probe_34_menu_reopen.png" "the key menu open again at the same key, after Escape closed it"
assert_exists "$SHOTS/graph_probe_35_menu_kb_open.png" "the key menu open for the keyboard: the arrows and Return"
assert_exists "$SHOTS/graph_probe_36_menu_down.png"   "the same menu after one Down arrow"
assert_exists "$SHOTS/graph_probe_37_menu_kb_after.png" "the viewport after Return activated the entry the arrows reached"
assert_exists "$SHOTS/graph_probe_38_appkey_undo.png" "the viewport after ctrl+Z with the menu closed"
norm "$SHOTS/graph_probe_32_menu_delete.png"   "$NORM/menu_delete.png"
norm "$SHOTS/graph_probe_33_menu_escape.png"   "$NORM/menu_escape.png"
norm "$SHOTS/graph_probe_34_menu_reopen.png"   "$NORM/menu_reopen.png"
norm "$SHOTS/graph_probe_35_menu_kb_open.png"  "$NORM/menu_kb_open.png"
norm "$SHOTS/graph_probe_36_menu_down.png"     "$NORM/menu_down.png"
norm "$SHOTS/graph_probe_37_menu_kb_after.png" "$NORM/menu_kb_after.png"
norm "$SHOTS/graph_probe_38_appkey_undo.png"   "$NORM/appkey_undo.png"

# THE KEYS THE MENU ACTS ON ARE THE MENU'S. Delete is an application command
# (gui::KeyCommandMap: "Delete selected key(s)", routed through MainWindow), and it would
# remove exactly the key this menu was opened on: a menu that did not consume it would
# show it in the line the assertion below reads. So this is the assertion about
# consumption, and it is a statement about the DOCUMENT (the key is still there).
[ "${MENU_DEL_LINE:-}" = "${MENU_BEFORE:-}" ] && [ -n "${MENU_BEFORE:-}" ]
check $? "with the key menu OPEN the application's own Delete command does not reach the timeline (the panel's state line is unchanged: [${MENU_DEL_LINE:-none}], and it read [${MENU_BEFORE:-none}] before the menu was opened)"

# ESCAPE CLOSES THE MENU AND CHANGES NOTHING. Two halves, because "closed" and "did not
# change the easing" are two different facts: the line must be EXACTLY what it was before
# the menu was opened, and the popup's own pixels must be gone. And the bar for "gone" is NOT
# a fraction of the popup's rectangle: MEASURED, opening the menu changes 17165 px inside its
# 202x145 rectangle (58.6% of it) because the popup's background IS the panel's background -
# see menu_shaped - so the bar this assertion used to carry (60% of the rectangle, 17574 px)
# was above the real popup's own size and would have failed on a menu that opened and closed
# correctly. What is asserted instead: the pixels that went away are at least 80% of the
# pixels the menu's own opening changed (MENU_POPUP_PIXELS, recorded when it opened), which
# is the same popup measured against itself.
[ "${MENU_ESCAPE_LINE:-}" = "${MENU_BEFORE:-}" ] && [ -n "${MENU_BEFORE:-}" ]
check $? "Escape closed the key menu WITHOUT changing anything: the panel's state line still reads [${MENU_ESCAPE_LINE:-none}], exactly the pre-menu [${MENU_BEFORE:-none}]"
d_menu_esc=$(ae_diff "$NORM/menu_delete.png" "$NORM/menu_escape.png")
[ -n "${MENU_POPUP_PIXELS:-}" ] && [ "$d_menu_esc" -ge $((MENU_POPUP_PIXELS * 8 / 10)) ]
check $? "and the popup's own pixels stopped being drawn ($d_menu_esc px changed against the menu-open control, bar $(( ${MENU_POPUP_PIXELS:-0} * 8 / 10 )) = 80% of the ${MENU_POPUP_PIXELS:-unknown} px opening this menu changed)"

# AND THE MENU IS STILL USABLE: the reopen above is what the click below ran against, and
# its own rectangle says it is the SAME popup at the same place (the anchor did not move,
# so neither did the clamp's answer).
read -r CW2 CH2 CX2 CY2 <<< "$(printf "%s" "${MENU_CLICK_BBOX:-0x0+0+0}" | tr "x+" "  ")"
[ "${CW2:-0}" -gt 60 ] && [ "${CH2:-0}" -gt 60 ] && [ $((CX2 - MBX)) -ge -2 ] && [ $((CX2 - MBX)) -le 2 ] && [ $((CY2 - MBY)) -ge -2 ] && [ $((CY2 - MBY)) -le 2 ]
check $? "the menu was reopened after Escape closed it, at the same key and the same place (${MENU_CLICK_BBOX:-none} against the first ${MENU_BBOX:-none}) - which is what the click above was a click INTO"

# THE ARROWS DRAW AND MOVE THE POPUP'S OWN HIGHLIGHT, and Return activates the entry they
# reached. The highlight is ImGui's own selection rectangle inside the popup window, so the
# one Down arrow changes pixels INSIDE the popup's measured rectangle and nowhere else; with
# the flag off no arrow draws anything at all, which is what makes this the pixel half of
# "the menu is keyboard-usable".
d_menu_hl=$(ae_diff "$NORM/menu_kb_open.png" "$NORM/menu_down.png")
magick compare -compose src "$NORM/menu_kb_open.png" "$NORM/menu_down.png" /tmp/probe_menu_hl_diff.png 2>/dev/null
MENU_HL_BBOX=$(magick identify -format "%@" /tmp/probe_menu_hl_diff.png 2>/dev/null)
read -r HW HH HX HY <<< "$(printf "%s" "${MENU_HL_BBOX:-0x0+0+0}" | tr "x+" "  ")"
[ "$d_menu_hl" -gt 200 ] && [ "${HW:-0}" -gt 0 ] && [ "${HH:-0}" -gt 0 ] \
    && [ "${HX:-0}" -ge $((MBX - 2)) ] && [ "${HY:-0}" -ge $((MBY - 2)) ] \
    && [ $((HX + HW)) -le $((MBX + MBW + 2)) ] && [ $((HY + HH)) -le $((MBY + MBH + 2)) ]
check $? "the Down arrow drew the popup's own entry highlight INSIDE its rectangle ($d_menu_hl px changed, [${MENU_HL_BBOX:-none}], inside ${MENU_BBOX:-none})"
case "${MENU_KB_LINE:-}" in
    *" Rou") MENU_KB_ROUND=0 ;;
    *)       MENU_KB_ROUND=1 ;;
esac
[ "$MENU_KB_ROUND" -eq 0 ] && [ "${MENU_KB_LINE:-}" != "${MENU_AFTER:-}" ]
check $? "and Return activated the entry the arrows reached: the panel's state line names the second entry of the menu's own display order, Round ([${MENU_KB_LINE:-none}], which read [${MENU_AFTER:-none}] before the keyboard)"
d_menu_kb=$(ae_diff "$NORM/menu_down.png" "$NORM/menu_kb_after.png")
[ -n "${MENU_POPUP_PIXELS:-}" ] && [ "$d_menu_kb" -ge $((MENU_POPUP_PIXELS * 8 / 10)) ]
check $? "and the popup closed on that activation, by its own pixels ($d_menu_kb px changed, bar $(( ${MENU_POPUP_PIXELS:-0} * 8 / 10 )) = 80% of the pixels opening this menu changed - the same measured bar as the Escape above, and for the same reason: the popup's background is the panel's)"

# THE REGRESSION, and the half that matters most: with the menu CLOSED the overlay claims
# nothing, so an application key command works exactly as it did before this menu existed.
# The last edit at this point is the keyboard pick above, so ctrl+Z has to undo it - and
# the panel's own state line is where that is read, from the DOCUMENT rather than from a
# return value. (It is also what leaves the key carrying Sine, which is what phase 4b
# below re-shapes.)
[ "${MENU_APPKEY_LINE:-}" = "${MENU_AFTER:-}" ] && [ -n "${MENU_AFTER:-}" ] && [ "${MENU_KB_LINE:-}" != "${MENU_AFTER:-}" ]
check $? "with the key menu CLOSED the application's own Undo key command still reaches the timeline: the line went from the keyboard's [${MENU_KB_LINE:-none}] back to [${MENU_APPKEY_LINE:-none}], the state before it"

# ---- (d2) PHASE 4b: THE CURVE - THE ONLY PLACE THE EASING IS DRAWN, AND SO THE ONLY
# PIXEL-LEVEL PROOF THAT THE MENU'S CHOICE REACHED THE PANEL ----
# WHY THE STATE LINE WAS NOT ENOUGH, MEASURED. The panel draws its summary line only while
# the model is EMPTY (ImGuiOverlay.cpp:843); with curves present the line exists in the
# accessibility tree alone. So every assertion above is invisible on screen, and the easing
# is drawn in exactly one place - the polyline of the segment it shapes - which a map of ONE
# key does not have (graph::Curve::buildPolyline emits nothing for it). MEASURED here on the
# one-key panel: changing the easing changed 0 pixels, because there was no curve to change.
# So phase 4b builds the segment - a second key one frame later carrying a DIFFERENT value -
# and then the assertions below are about the curve's own pixels, with the menu closed in
# every capture they compare:
#   * the segment appears when the second key does (the one-key panel drew no curve at all);
#   * re-shaping it with the menu's Linear moves the curve's pixels (the key carried Sine);
#   * and picking the SAME easing a second time moves nothing - which is what makes the line
#     above about the EASING rather than about the menu having been used.
# The route is asserted as well as the effect: the dock's knob row says the playhead moved
# (a channel shows its rows only while a key exists at the current frame), the knob's press
# says the key was added, and the Angle field's own Value interface says the value it was
# given - which is what makes the segment non-flat.
assert_exists "$SHOTS/graph_probe_26_curve_sine.png"            "the viewport with the segment shaped by the easing the menu chose"
assert_exists "$SHOTS/graph_probe_27_menu_linear_open.png"      "the same panel with the key menu open again, on the same key"
assert_exists "$SHOTS/graph_probe_28_curve_linear.png"          "the viewport after choosing Linear for that key"
assert_exists "$SHOTS/graph_probe_29_menu_linear_again_open.png" "the key menu open a third time, for the same choice"
assert_exists "$SHOTS/graph_probe_30_curve_linear_again.png"    "the viewport after picking the same easing again"
norm "$SHOTS/graph_probe_26_curve_sine.png"             "$NORM/curve_sine.png"
norm "$SHOTS/graph_probe_27_menu_linear_open.png"       "$NORM/menu_linear_open.png"
norm "$SHOTS/graph_probe_28_curve_linear.png"           "$NORM/curve_linear.png"
norm "$SHOTS/graph_probe_29_menu_linear_again_open.png" "$NORM/menu_linear_again_open.png"
norm "$SHOTS/graph_probe_30_curve_linear_again.png"     "$NORM/curve_linear_again.png"
crop "$NORM/curve_sine.png"          "$NORM/c_curve_sine.png"
crop "$NORM/curve_linear.png"        "$NORM/c_curve_linear.png"
crop "$NORM/curve_linear_again.png"  "$NORM/c_curve_linear_again.png"

echo "phase 4b: the second key's angle, seeded [${ANGLE_2ND_SEEDED:-unreadable}] at [${ANGLE_2ND_RECT:-none}], typed [${NEWDEG:-none}] in ${ANGLE_2ND_TRIES:-0} attempt(s), the field value read [${ANGLE_2ND_TYPED_TEXT:-unreadable}] and its line edit [${ANGLE_2ND_EDIT_TEXT:-unreadable}] while the edit was open, and [${ANGLE_2ND_WRITTEN:-unreadable}] after the commit key; the focus holders [${DOCK_FOCUS_BEFORE:-none}] before the click into the field, [${DOCK_FOCUS_CLICK:-none}] after it and [${DOCK_FOCUS_AFTER:-none}] after the commit key; the panel's own legend names [${DOCK_TARGET_BEFORE:-none}] before and [${DOCK_TARGET_AFTER:-none}] after; the knob row at the next frame [${KNOB_2ND:-none}] and after the press [${KNOB_2ND_AFTER:-none}]; the Linear popup [${MENU2_BBOX:-none}] entry [${MENU2_ENTRY:-none}], the state line then [${MENU2_LINE:-none}]; the same pick again: popup [${MENU3_BBOX:-none}] entry [${MENU3_ENTRY:-none}], line [${MENU3_LINE:-none}]"

[ -n "${KNOB_2ND:-}" ]
check $? "the playhead moved one frame right through the app's own key command: the dock's Rotate knob row is back, i.e. this frame carries no key ([${KNOB_2ND:-none}]; Ctrl+Right = MoveRight, MainWindow.cpp:885)"
awk -v a="${ANGLE_2ND_SEEDED:-}" 'BEGIN { exit !(a != "") }'
check $? "and pressing that knob added the second key there: the channel rows are readable (the Angle field answers [${ANGLE_2ND_SEEDED:-unreadable}] at [${ANGLE_2ND_RECT:-none}]), which is what a key at this frame turns on - the knob row's own presence in the tree is NOT the signal, MEASURED: it reports the same rect before and after the press" 
read -r M2BW M2BH M2BX M2BY <<< "$(printf "%s" "${MENU2_BBOX:-0x0+0+0}" | tr "x+" "  ")"
[ "${M2BW:-0}" -gt 60 ] && [ "${M2BH:-0}" -gt 60 ]
check $? "the key menu opened again on the same key ($(printf '%s' "${MENU2_BBOX:-none}"), i.e. the anchor the search found is still on it)"
# WHERE IT LANDED AGAIN, AND WHY THAT IS NOT THE POINTER'S X. This popup is CLAMPED into the
# viewport - the phase-4 assertion above reads that rule off the overlay and checks it - and
# the anchor (300) is far into the right-hand part of a 210 px viewport, so a popup hanging at
# the pointer's x would be a popup outside the viewport. What is asserted instead is the claim
# this line is really about: the SAME anchor puts the popup in the SAME place, which is what
# "the anchor the search found is still on that key" means in pixels. MEASURED here: the popup
# sits at x=179, not at 300 - and the earlier form of this assertion compared it with 300 and
# could never pass, which is why it is stated this way now.
[ $((M2BX - MBX)) -ge -2 ] && [ $((M2BX - MBX)) -le 2 ] && [ $((M2BY - MBY)) -ge -2 ] && [ $((M2BY - MBY)) -le 2 ]
check $? "and it landed where the first menu did, i.e. the same anchor is still on the same key (popup ${M2BX:-none},${M2BY:-none} against the first menu's ${MBX:-none},${MBY:-none}; the popup is clamped into the viewport, so its x is the viewport's right edge minus its width and not the pointer's x ${MAX:-none})"
awk -v a="${ANGLE_2ND_WRITTEN:-}" -v b="${NEWDEG:-}" 'BEGIN { exit !(a != "" && b != "" && (a + 0) == (b + 0)) }'
check $? "the dock's Angle field took the value it was typed, read back through the field's own Value interface (seeded [${ANGLE_2ND_SEEDED:-unreadable}], typed [${NEWDEG:-none}], reads [${ANGLE_2ND_WRITTEN:-unreadable}])"
awk -v a="${ANGLE_2ND_WRITTEN:-}" -v b="${ANGLE_2ND_SEEDED:-}" 'BEGIN { exit !(a != "" && b != "" && (a + 0) != (b + 0)) }'
check $? "and it differs from the value the knob seeded, which is what makes the segment not flat (${ANGLE_2ND_SEEDED:-?} -> ${ANGLE_2ND_WRITTEN:-?} degrees)"

# THE PIXELS. Every one of these captures is taken with the menu CLOSED (the overlay own
# successor frame has already closed it when the capture runs), so none of them is a
# measurement of the menu's own rectangle - that is the assertion the header calls out as
# the one this phase needed and did not have.
crop "$NORM/menu_after.png" "$NORM/c_menu_after.png"
d_segment=$(ae_diff "$NORM/c_menu_after.png" "$NORM/c_curve_sine.png")
p_segment=$(pct "$d_segment" "$CROP_AREA")
[ "$d_segment" -gt "$SEGMENT_PIXELS" ]
check $? "the second key draws the segment the one-key panel could not ($d_segment px = ${p_segment}% of the crop changed, bar $SEGMENT_PIXELS; the change is the segment's stroke, the new key's square and the 1px playhead moving one frame)"
d_ease=$(ae_diff "$NORM/c_curve_sine.png" "$NORM/c_curve_linear.png")
p_ease=$(pct "$d_ease" "$CROP_AREA")
[ "$d_ease" -gt "$EASE_PIXELS" ]
check $? "and re-shaping that segment moves the CURVE's own pixels: Linear where the key carried Sine ($d_ease px = ${p_ease}% of the crop changed, bar $EASE_PIXELS; the panel draws no summary line while a curve exists, so this change is the polyline and the tick/label layout it lives in)"
d_ease_same=$(ae_diff "$NORM/c_curve_linear.png" "$NORM/c_curve_linear_again.png")
[ "$d_ease_same" -eq 0 ]
check $? "while picking the SAME easing a second time moves nothing at all ($d_ease_same px against the capture taken before that menu was opened)"
case "${MENU2_LINE:-}" in
    *" Lin") MENU2_LINEAR=0 ;;
    *)          MENU2_LINEAR=1 ;;
esac
check $MENU2_LINEAR "and the panel's own line agrees with the choice the pointer made ([${MENU2_LINE:-none}], which read [${MENU_AFTER:-none}] before)"
case "${MENU3_LINE:-}" in
    *" Lin") MENU3_LINEAR=0 ;;
    *)          MENU3_LINEAR=1 ;;
esac
check $MENU3_LINEAR "and it still reads the same after the second, identical choice ([${MENU3_LINE:-none}])"

# ---- (e) PHASE 3: THE TARGET THAT GETS DELETED WHILE THE PANEL IS SHOWING IT ----
# WHAT THIS GATE IS FOR. ctrl::GraphEditor holds its targets as RAW core::ObjectNode*
# and walks them on every draw; if a node is destroyed while still in that list, the walk
# is a use-after-free. The application is safe only because the tree's own deletion order
# clears the target list first (see the phase-3 comment in the body), and nothing else in
# the suite or in this probe deletes a target at all. So: the row the panel was showing is
# deleted through the object tree, and the app has to come out of it alive, with the row
# gone, with the panel publishing one of the MODEL's empty-state sentences, and with the
# curve's pixels gone from the crop. The first assertion here is the one that would fire
# if the ordering ever broke: a use-after-free in the panel's draw is a dead process, and
# a dead process is not something AT-SPI can report - every later call would just time out.
echo "phase 3: deleted layer [$DEL_LAYER] as the tree row [$DEL_ROW]; the menu it opened [$DEL_POPUP] [$DEL_POPUP_INFO] with the input focus moved to [$DEL_FOCUS]; process alive [$DEL_ALIVE] (pid [${AE_PID:-?}]); the row after [$DEL_ROW_AFTER], cells now [${DEL_CELLS:-none}]; the panel's sentences: empty [$DEL_EMPTY] no-keys [$DEL_NOKEYS] all-hidden [$DEL_ALLHID]; legend rows after [${DEL_LEGEND:-none}]"

assert_exists "$SHOTS/graph_probe_17_pre_delete_a.png" "the viewport with the keyed layer's curve, before the deletion"
assert_exists "$SHOTS/graph_probe_18_pre_delete_b.png" "the same state a second time, as the control"
assert_exists "$SHOTS/graph_probe_19_after_delete.png" "the viewport after the layer the panel was showing was deleted"
norm "$SHOTS/graph_probe_17_pre_delete_a.png" "$NORM/pre_del_a.png"
norm "$SHOTS/graph_probe_18_pre_delete_b.png" "$NORM/pre_del_b.png"
norm "$SHOTS/graph_probe_19_after_delete.png" "$NORM/after_del.png"

# THE ROUTE FIRST, because a deletion that never happened cannot have been survived: the
# row was addressable, and the object tree opened its menu AT that row (the popup window's
# origin IS the click point, which is how it is found - see the body; it is not a
# hardcoded coordinate, and the entry it offers was read off a screenshot because the menu
# is not in the accessibility tree at all: see WHAT IS NOT COVERED). The menu has to be
# MAPPED for that reading to be about something on screen, which the window itself answers.
case "${DEL_POPUP_INFO:-}" in
    *IsViewable*) DEL_MAPPED=0 ;;
    *)            DEL_MAPPED=1 ;;
esac
[ -n "${DEL_ROW:-}" ] && [ -n "${DEL_POPUP:-}" ] && [ "$DEL_MAPPED" -eq 0 ]
check $? "the object tree opened its row menu for the layer the panel was showing ([$DEL_LAYER] at [$DEL_ROW], menu window [$DEL_POPUP] [$DEL_POPUP_INFO])"

[ "${DEL_ALIVE:-}" = "yes" ]
check $? "the application survived deleting the layer it was showing (alive [$DEL_ALIVE], pid [${AE_PID:-?}]): a target destroyed while still in ctrl::GraphEditor's raw-pointer list would be walked by buildEntries() and fault here"

[ -z "${DEL_ROW_AFTER:-}" ]
check $? "and the row is gone from the object tree (a cell named [$DEL_LAYER] answers with [$DEL_ROW_AFTER])"

# THE PANEL'S OWN WORDS. The model publishes one of three sentences for an empty target
# list, and which one the deleted layer leaves behind is the model's business; what is
# asserted is that ONE OF THEM IS BACK - the panel must not go on describing a layer that
# no longer exists. The rendered legend is asserted empty beside it: the model's list of
# channels to draw is exactly what buildEntries() produced, so an empty legend is the
# model saying it no longer has that curve.
DEL_SAID=""
[ -n "${DEL_EMPTY:-}" ]  && DEL_SAID="Select a layer to show its curves"
[ -n "${DEL_NOKEYS:-}" ] && DEL_SAID="${DEL_SAID:+$DEL_SAID / }No keys on the selected layers"
[ -n "${DEL_ALLHID:-}" ] && DEL_SAID="${DEL_SAID:+$DEL_SAID / }All channels of the selected layers are hidden"
[ -n "$DEL_SAID" ]
check $? "the panel falls back to one of the model's own empty-state sentences after the deletion ([$DEL_SAID]; rects: empty [$DEL_EMPTY], no-keys [$DEL_NOKEYS], all-hidden [$DEL_ALLHID])"
DEL_ROWS=$(printf '%s' "${DEL_LEGEND:-}" | tr ';' '\n' | grep -c .)
[ "$DEL_ROWS" -eq 0 ]
check $? "and the model lists no curve for the deleted layer any more ($DEL_ROWS legend rows: [${DEL_LEGEND:-none}])"

# THE PIXELS, with the control first: the two captures taken before the deletion, with
# nothing forced between them, are the same state, so the change below can only be the
# deletion.
d_del_control=$(ae_diff "$NORM/pre_del_a.png" "$NORM/pre_del_b.png")
[ "$d_del_control" -eq 0 ]
check $? "the panel does not repaint on its own between the two pre-deletion captures (control $d_del_control px)"

crop "$NORM/pre_del_b.png" "$NORM/c_pre_del.png"
crop "$NORM/after_del.png" "$NORM/c_after_del.png"
d_deleted=$(ae_diff "$NORM/c_pre_del.png" "$NORM/c_after_del.png")
p_deleted=$(pct "$d_deleted" "$CROP_AREA")
[ "$d_deleted" -gt "$CURVE_PIXELS" ]
check $? "the curve the deleted layer was showing left the crop ($d_deleted px = ${p_deleted}% changed, bar $CURVE_PIXELS)"
le "$p_deleted" "$CEIL_PCT"
check $? "and the deletion is still not a wash (${p_deleted}% <= ceiling ${CEIL_PCT}%)"
d_deleted_whole=$(ae_diff "$NORM/pre_del_b.png" "$NORM/after_del.png")
echo "deletion: the whole screen changed $d_deleted_whole px, of which $d_deleted px are inside the crop; the deleted row itself is BELOW the crop (the object tree sits in the Animation dock, y >= $ANIM_Y), so the crop's change is the viewport: the panel's curve leaving and its empty state line arriving, plus whatever the deleted layer contributed to the canvas strip"

# DIAGNOSTIC, NOT AN ASSERTION: the post-deletion crop against the panel's own ON empty
# state from the top of this run (nothing selected, before any key existed). If the deleted
# layer drew nothing in the crop's canvas strip these two are the same picture, which is
# the strongest form of "the panel is back to its no-target state"; the number is recorded
# here rather than asserted, because the canvas strip is not the panel's to control.
d_vs_empty=$(ae_diff "$NORM/c_on_empty.png" "$NORM/c_after_del.png")
echo "deletion: the crop now differs from the panel's ON empty state by $d_vs_empty px (the panel's own empty state is what it fell back to; any remainder is the canvas strip the deleted layer was in)"

assert_rendered "$SHOTS/graph_probe_05_on_top.png" "the viewport with the animation graph overlay"
summary probe_graph_overlay
